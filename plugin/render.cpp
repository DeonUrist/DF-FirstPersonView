#include "render.h"

#include <algorithm>
#include <cmath>

namespace fpv {

// ---------------------------------------------------------------------------
// worker pool

WorkerPool::WorkerPool(int threads) {
    for (int i = 0; i < threads; ++i)
        threads_.emplace_back([this, i] { loop(i); });
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto &t : threads_)
        t.join();
}

void WorkerPool::loop(int index) {
    uint64_t seen = 0;
    for (;;) {
        const std::function<void(int)> *job = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_)
                return;
            seen = generation_;
            job = job_;
        }
        (*job)(index);
        {
            std::lock_guard<std::mutex> lock(m_);
            --pending_;
        }
        done_.notify_one();
    }
}

void WorkerPool::run(const std::function<void(int)> &job) {
    {
        std::lock_guard<std::mutex> lock(m_);
        job_ = &job;
        pending_ = int(threads_.size());
        ++generation_;
    }
    cv_.notify_all();
    job(int(threads_.size()));
    std::unique_lock<std::mutex> lock(m_);
    done_.wait(lock, [&] { return pending_ == 0; });
    job_ = nullptr;
}

// ---------------------------------------------------------------------------

namespace {

constexpr float FAR_DEPTH = 1e30f;
constexpr int MAX_OPEN = 24;

// still-empty pixel ranges of one screen column, top to bottom
struct OpenList {
    int a[MAX_OPEN], b[MAX_OPEN];
    int n = 0;

    // take [lo, hi) out; the parts that were still open go to va/vb
    int clip(int lo, int hi, int *va, int *vb) {
        int nv = 0;
        int i = 0;
        while (i < n) {
            const int ia = a[i], ib = b[i];
            if (ib <= lo) {
                ++i;
            } else if (ia >= hi) {
                break;
            } else {
                const int s = std::max(lo, ia), e = std::min(hi, ib);
                va[nv] = s;
                vb[nv] = e;
                ++nv;
                if (s == ia && e == ib) {
                    for (int j = i; j < n - 1; ++j) {
                        a[j] = a[j + 1];
                        b[j] = b[j + 1];
                    }
                    --n;
                } else if (s == ia) {
                    a[i] = e;
                    ++i;
                } else if (e == ib) {
                    b[i] = s;
                    ++i;
                } else if (n < MAX_OPEN) {
                    for (int j = n; j > i + 1; --j) {
                        a[j] = a[j - 1];
                        b[j] = b[j - 1];
                    }
                    a[i + 1] = e;
                    b[i + 1] = ib;
                    b[i] = s;
                    ++n;
                    i += 2;
                } else {
                    // no room to split: give up the part below the span
                    b[i] = s;
                    ++i;
                }
            }
        }
        return nv;
    }

    // the parts of [lo, hi) that are still open, without taking them
    int peek(int lo, int hi, int *va, int *vb) const {
        int nv = 0;
        for (int i = 0; i < n; ++i) {
            const int s = std::max(lo, a[i]), e = std::min(hi, b[i]);
            if (s < e) {
                va[nv] = s;
                vb[nv] = e;
                ++nv;
            }
        }
        return nv;
    }
};

inline uint32_t lerp_color(int r0, int g0, int b0, int r1, int g1, int b1, float t) {
    return rgb(int(r0 + (r1 - r0) * t + 0.5f), int(g0 + (g1 - g0) * t + 0.5f), int(b0 + (b1 - b0) * t + 0.5f));
}

// blends towards the fog colour: f = 256 keeps the colour, 0 is pure fog
struct Shade {
    uint32_t f, fog_rb, fog_g;
    Shade(uint32_t fog, int f256) {
        f = uint32_t(std::clamp(f256, 0, 256));
        fog_rb = (fog & 0x00FF00FFu) * (256 - f);
        fog_g = (fog & 0x0000FF00u) * (256 - f);
    }
    inline uint32_t operator()(uint32_t c) const {
        const uint32_t rb = (((c & 0x00FF00FFu) * f + fog_rb) >> 8) & 0x00FF00FFu;
        const uint32_t g = (((c & 0x0000FF00u) * f + fog_g) >> 8) & 0x0000FF00u;
        return 0xFF000000u | rb | g;
    }
};

// lift a colour towards white, to mark what the pointer is on
inline uint32_t brighten(uint32_t c) {
    const uint32_t rb = (((c & 0x00FF00FFu) * 180 + 0x00FF00FFu * 76) >> 8) & 0x00FF00FFu;
    const uint32_t g = (((c & 0x0000FF00u) * 180 + 0x0000FF00u * 76) >> 8) & 0x0000FF00u;
    return 0xFF000000u | rb | g;
}

} // namespace

struct Renderer::Frame {
    const Scene *scene;
    int W, H;
    uint32_t *buf;
    float px, py, eye;
    float dirx, diry, rgtx, rgty, planex, planey;
    float S, hz;
    int hzi;
    float view_dist, max_d;
    uint32_t fog;
    int zlo, zhi, nz;
    const Cell *cells[64];       // per level, null if not captured
    float zrel[65];              // height of each level's floor relative to the eye
    int map_w, map_h;
    const int16_t *col_lo, *col_hi;   // per map tile: levels that have anything to draw
    const BoxSet *boxsets;            // shapes standing inside tiles
    const uint8_t *col_box;           // per map tile: whether any level has some
    int nboxsets;
    bool cam_sky;
    uint16_t roof;
    Pick hl;
    std::vector<const Billboard *> bills;   // far to near
    std::vector<float> bill_depth, bill_lat;
};

Renderer::Renderer()
    : pool_(std::clamp(int(std::thread::hardware_concurrency()) - 2, 1, 12)) {}

void Renderer::render(const Scene &scene, const Camera &cam, const RenderOptions &opt, int W, int H,
                      uint32_t *buf) {
    Frame f;
    f.scene = &scene;
    f.W = W;
    f.H = H;
    f.buf = buf;
    f.px = cam.x;
    f.py = cam.y;
    f.eye = cam.eye;
    const float P = std::tan(cam.fov * 3.14159265f / 360.0f);
    f.dirx = std::sin(cam.ang);
    f.diry = -std::cos(cam.ang);
    f.rgtx = std::cos(cam.ang);
    f.rgty = std::sin(cam.ang);
    f.planex = f.rgtx * P;
    f.planey = f.rgty * P;
    f.S = W / (2 * P);
    f.hz = H / 2.0f + cam.pitch;
    f.hzi = std::clamp(int(f.hz + 0.5f), 0, H);
    f.view_dist = opt.view_distance;
    f.max_d = opt.view_distance + 4;
    f.fog = opt.fog;
    f.zlo = std::max(0, opt.zlo);
    f.zhi = std::min({opt.zhi, scene.d - 1, f.zlo + 63});
    f.nz = std::max(0, f.zhi - f.zlo + 1);
    for (int k = 0; k < f.nz; ++k) {
        const Level *L = scene.level(f.zlo + k);
        f.cells[k] = L ? L->cells.data() : nullptr;
        f.zrel[k] = (f.zlo + k) * LEVEL_H - cam.eye;
    }
    f.zrel[f.nz] = (f.zlo + f.nz) * LEVEL_H - cam.eye;
    f.map_w = scene.w;
    f.map_h = scene.h;
    f.col_lo = scene.col_lo.data();
    f.col_hi = scene.col_hi.data();
    f.roof = scene.tex.existing(Special::Roof);
    f.boxsets = scene.boxsets.data();
    f.col_box = scene.col_box.data();
    f.nboxsets = int(scene.boxsets.size());
    f.hl = opt.highlight;

    // is the camera itself under the open sky?
    f.cam_sky = false;
    {
        const int cx = int(std::floor(cam.x)), cy = int(std::floor(cam.y));
        const int cz = int(std::floor(cam.eye / LEVEL_H));
        const Level *L = scene.level(cz);
        if (L && cx >= 0 && cy >= 0 && cx < scene.w && cy < scene.h)
            f.cam_sky = (L->cells[cy * scene.w + cx].flags & CELL_SKY) != 0;
    }

    if (W != last_w_ || H != last_h_) {
        spans_.assign(size_t(W) * MAX_SPANS, Span{});
        span_count_.assign(W, 0);
        hits_.assign(size_t(W) * MAX_HITS, BillHit{});
        hit_count_.assign(W, 0);
        masked_.assign(size_t(W) * MAX_MASKED, Masked{});
        masked_count_.assign(W, 0);
        row_inv_.assign(H, 0);
        sky_row_.assign(H, 0);
        last_w_ = W;
        last_h_ = H;
    }
    for (int y = 0; y < H; ++y) {
        const float dy = std::abs(y + 0.5f - f.hz);
        row_inv_[y] = f.S / std::max(dy, 0.01f);
        // sky: zenith at the top, haze towards the horizon
        float t = (y + 0.5f) / std::max(f.hz, 1.0f);
        t = std::min(t, 1.0f);
        t *= t;
        const float d = opt.daylight;
        const uint32_t zenith = lerp_color(4, 6, 18, 52, 104, 188, d);
        const uint32_t horizon = lerp_color(18, 22, 44, 158, 196, 226, d);
        sky_row_[y] = lerp_color(px_r(zenith), px_g(zenith), px_b(zenith), px_r(horizon), px_g(horizon),
                                 px_b(horizon), t);
    }

    // billboards in view, far to near
    f.bills.clear();
    struct Ref {
        const Billboard *b;
        float depth, lat;
    };
    std::vector<Ref> refs;
    const auto consider = [&](const Billboard &b) {
        if (!b.sprite || b.mz < f.zlo || b.mz > f.zhi)
            return;
        const float relx = b.x - f.px, rely = b.y - f.py;
        const float depth = relx * f.dirx + rely * f.diry;
        if (depth < 0.25f || depth > f.max_d)
            return;
        refs.push_back({&b, depth, relx * f.rgtx + rely * f.rgty});
    };
    for (int z = f.zlo; z <= f.zhi; ++z)
        if (const Level *L = scene.level(z))
            for (const Billboard &b : L->bills)
                consider(b);
    for (const Billboard &b : scene.units)
        consider(b);
    std::stable_sort(refs.begin(), refs.end(), [](const Ref &a, const Ref &b) {
        if (a.depth != b.depth)
            return a.depth > b.depth;
        return a.b->order < b.b->order;
    });
    for (const Ref &r : refs) {
        f.bills.push_back(r.b);
        f.bill_depth.push_back(r.depth);
        f.bill_lat.push_back(r.lat);
    }

    constexpr int CHUNK = 16;
    std::atomic<int> next{0};
    pool_.run([&](int) {
        for (;;) {
            const int x0 = next.fetch_add(CHUNK);
            if (x0 >= W)
                break;
            const int x1 = std::min(W, x0 + CHUNK);
            renderColumns(f, x0, x1);
            renderBillboards(f, x0, x1);
        }
    });
}

void Renderer::renderColumns(const Frame &f, int x0, int x1) {
    const Scene &scene = *f.scene;
    const int W = f.W, H = f.H;
    const float S = f.S, hz = f.hz;
    const float inv_view = 1.0f / f.view_dist;
    const float QH = LEVEL_H / 16;   // raised surfaces are placed in sixteenths of a level
    const uint16_t roof = f.roof;

    const auto shade_factor = [&](float d) {
        float t = d * inv_view;
        if (t > 1)
            t = 1;
        return int(256 * (1 - 0.97f * t * std::sqrt(std::sqrt(t)) ));
    };

    for (int sx = x0; sx < x1; ++sx) {
        uint32_t *col = f.buf + size_t(sx) * H;
        Span *spans = &spans_[size_t(sx) * MAX_SPANS];
        int nspans = 0;
        int nmasked = 0;
        const auto record = [&](int a, int b, float depth, int tx, int ty, int tz) {
            if (nspans < MAX_SPANS)
                spans[nspans++] = Span{int16_t(a), int16_t(b), depth, int16_t(tx), int16_t(ty), int16_t(tz)};
        };

        const float camx = (2 * (sx + 0.5f) / W) - 1;
        const float rx = f.dirx + f.planex * camx, ry = f.diry + f.planey * camx;

        int mapx = int(std::floor(f.px)), mapy = int(std::floor(f.py));
        const float ddx = rx == 0 ? 1e30f : std::abs(1 / rx);
        const float ddy = ry == 0 ? 1e30f : std::abs(1 / ry);
        int stepx, stepy;
        float sdx, sdy;
        if (rx < 0) {
            stepx = -1;
            sdx = (f.px - mapx) * ddx;
        } else {
            stepx = 1;
            sdx = (mapx + 1 - f.px) * ddx;
        }
        if (ry < 0) {
            stepy = -1;
            sdy = (f.py - mapy) * ddy;
        } else {
            stepy = 1;
            sdy = (mapy + 1 - f.py) * ddy;
        }

        OpenList open;
        open.n = 1;
        open.a[0] = 0;
        open.b[0] = H;
        int va[MAX_OPEN + 2], vb[MAX_OPEN + 2];

        float d0 = 0, inv0 = 0;
        int side = 0;
        bool col_sky = f.cam_sky;
        int prev_ci = -1, prev_mapx = 0, prev_mapy = 0;

        while (open.n > 0) {
            const float d1 = std::min(sdx, sdy);
            const float inv1 = S / d1;
            if (mapx >= 0 && mapy >= 0 && mapx < f.map_w && mapy < f.map_h) {
                const int ci = mapy * f.map_w + mapx;
                // only the levels of this tile that hold something
                const int k_lo = std::max(0, int(f.col_lo[ci]) - f.zlo);
                const int k_hi = std::min(f.nz - 1, int(f.col_hi[ci]) - f.zlo);
                // the tile under the pointer is drawn brighter (unless a creature is)
                const bool tile_lit = f.hl.unit_id < 0 && mapx == f.hl.x && mapy == f.hl.y;

                // where the ray enters and leaves this tile (and where that entry
                // point lies in the tile it came from), for slopes
                const float in_x = f.px + d0 * rx, in_y = f.py + d0 * ry;
                const float entry_u = std::clamp(in_x - mapx, 0.0f, 1.0f);
                const float entry_v = std::clamp(in_y - mapy, 0.0f, 1.0f);
                const float prev_u = std::clamp(in_x - prev_mapx, 0.0f, 1.0f);
                const float prev_v = std::clamp(in_y - prev_mapy, 0.0f, 1.0f);
                const float exit_u = std::clamp(f.px + d1 * rx - mapx, 0.0f, 1.0f);
                const float exit_v = std::clamp(f.py + d1 * ry - mapy, 0.0f, 1.0f);

                // vertical faces on the side of the tile the ray enters through
                if (d0 > 0) {
                    int texx = -1;
                    const float dark = side ? 0.82f : 1.0f;
                    const auto face_texx = [&] {
                        if (texx < 0) {
                            float wallx = side == 0 ? f.py + d0 * ry : f.px + d0 * rx;
                            wallx -= std::floor(wallx);
                            texx = std::min(31, int(wallx * 32));
                            if ((side == 0 && rx < 0) || (side == 1 && ry > 0))
                                texx = 31 - texx;
                        }
                        return texx;
                    };
                    // see-through faces do not hide anything: note them for later
                    for (int k = k_lo; k <= k_hi; ++k) {
                        const Cell *cells = f.cells[k];
                        if (!cells || !cells[ci].frame || nmasked >= MAX_MASKED)
                            continue;
                        const float h0 = f.zrel[k];
                        masked_[size_t(sx) * MAX_MASKED + nmasked++] =
                            Masked{d0, hz - (h0 + LEVEL_H) * inv0, hz - h0 * inv0, cells[ci].frame,
                                   uint8_t(face_texx()), int16_t(mapx), int16_t(mapy), int16_t(f.zlo + k),
                                   int16_t(shade_factor(d0) * dark)};
                    }
                    for (int k = k_lo; k <= k_hi && open.n > 0; ++k) {
                        const Cell *cells = f.cells[k];
                        if (!cells)
                            continue;
                        const Cell &c = cells[ci];
                        uint16_t tid = 0;
                        float h0 = f.zrel[k], h1 = 0;
                        if (c.solid) {
                            if (prev_ci >= 0 && cells[prev_ci].solid)
                                continue;   // buried between two blocks
                            tid = c.solid;
                            h1 = h0 + LEVEL_H;
                        } else if (c.ramp) {
                            // the side of a slope: as high as the slope is where the ray enters
                            if (prev_ci >= 0 && cells[prev_ci].solid)
                                continue;
                            const float hin = ramp_height(c.ramp, entry_u, entry_v);
                            if (hin < 0.02f)
                                continue;
                            if (prev_ci >= 0 && cells[prev_ci].ramp &&
                                ramp_height(cells[prev_ci].ramp, prev_u, prev_v) >= hin - 0.02f)
                                continue;   // the slope carries on from the tile before
                            tid = c.half;
                            h1 = h0 + hin * LEVEL_H;
                        } else if (c.half) {
                            // a low block, such as a fortification
                            if (prev_ci >= 0 &&
                                (cells[prev_ci].solid ||
                                 (cells[prev_ci].half && !cells[prev_ci].ramp && cells[prev_ci].topq >= c.topq)))
                                continue;
                            tid = c.half;
                            h1 = h0 + c.topq * QH;
                        } else {
                            continue;
                        }
                        if (!tid)
                            continue;
                        const float top = hz - h1 * inv0, bot = hz - h0 * inv0;
                        const int a = top <= 0 ? 0 : int(top + 0.5f);
                        const int b = bot >= H ? H : int(bot + 0.5f);
                        if (a >= b || b <= open.a[0] || a >= open.b[open.n - 1])
                            continue;
                        const int nv = open.clip(a, b, va, vb);
                        if (!nv)
                            continue;
                        face_texx();
                        const Tex &tex = scene.tex.get(tid);
                        const Shade shade(f.fog, int(shade_factor(d0) * dark));
                        const float span = tex.stretch ? 32.0f : (h1 - h0) * 32.0f;
                        const int step = int(span * 65536.0f / (bot - top));
                        const int voff = int((std::ceil(span / 32.0f) * 32.0f - span) * 65536.0f);
                        const bool lit = tile_lit && f.zlo + k == f.hl.z;
                        for (int i = 0; i < nv; ++i) {
                            const int s = va[i], e = vb[i];
                            int tpos = int((s + 0.5f - top) * step) + voff;
                            for (int y = s; y < e; ++y) {
                                col[y] = shade(tex.px[(((tpos >> 16) & 31) << 5) + texx]);
                                tpos += step;
                            }
                            if (lit)
                                for (int y = s; y < e; ++y)
                                    col[y] = brighten(col[y]);
                            record(s, e, d0, mapx, mapy, f.zlo + k);
                        }
                    }
                }

                // horizontal surfaces between where the ray enters and leaves the tile
                const auto surface = [&](float h, uint16_t tid, bool from_above, float darken, int tz) {
                    // h is relative to the eye: below it for floors, above it for ceilings
                    int a, b;
                    if (from_above) {
                        if (h >= 0)
                            return;
                        const float far = hz - h * inv1;
                        a = far <= 0 ? 0 : int(far + 0.5f);
                        if (d0 > 0) {
                            const float near = hz - h * inv0;
                            b = near >= H ? H : int(near + 0.5f);
                        } else {
                            b = H;
                        }
                    } else {
                        if (h <= 0)
                            return;
                        const float far = hz - h * inv1;
                        b = far >= H ? H : int(far + 0.5f);
                        if (d0 > 0) {
                            const float near = hz - h * inv0;
                            a = near <= 0 ? 0 : int(near + 0.5f);
                        } else {
                            a = 0;
                        }
                    }
                    if (a >= b || open.n == 0 || b <= open.a[0] || a >= open.b[open.n - 1])
                        return;
                    const int nv = open.clip(a, b, va, vb);
                    if (!nv)
                        return;
                    const Tex &tex = scene.tex.get(tid);
                    const Shade shade(f.fog, int(shade_factor(d1) * darken));
                    const float dh = std::abs(h);
                    for (int i = 0; i < nv; ++i) {
                        const int s = va[i], e = vb[i];
                        for (int y = s; y < e; ++y) {
                            const float d = dh * row_inv_[y];
                            const int u = int((f.px + d * rx) * 32.0f) & 31;
                            const int v = int((f.py + d * ry) * 32.0f) & 31;
                            col[y] = shade(tex.px[(v << 5) | u]);
                        }
                        if (tile_lit && tz == f.hl.z)
                            for (int y = s; y < e; ++y)
                                col[y] = brighten(col[y]);
                        record(s, e, d1, mapx, mapy, tz);
                    }
                };

                // A slope. Along one ray it is (very nearly) a straight line from
                // where the ray enters the tile to where it leaves, so each pixel
                // row finds its point on that line.
                // One straight piece of a slope, from distance da to db.
                const auto slope_piece = [&](const Cell &c, float base, int tz, float da, float db, float ua, float va_,
                                             float ub, float vb_) {
                    const float hin = base + ramp_height(c.ramp, ua, va_) * LEVEL_H;
                    const float hout = base + ramp_height(c.ramp, ub, vb_) * LEVEL_H;
                    const float m = (hout - hin) / std::max(db - da, 1e-4f);
                    const float n = m * da - hin;   // positive when the eye is above the slope
                    if (n <= 0)
                        return;
                    const float far = hz - hout * (S / db);
                    const int a = far <= 0 ? 0 : (far >= H ? H : int(far + 0.5f));
                    int b = H;
                    if (da > 0) {
                        const float near = hz - hin * (S / da);
                        b = near >= H ? H : (near <= 0 ? 0 : int(near + 0.5f));
                    }
                    if (a >= b || open.n == 0 || b <= open.a[0] || a >= open.b[open.n - 1])
                        return;
                    const int nv = open.clip(a, b, va, vb);
                    if (!nv)
                        return;
                    const Tex &tex = scene.tex.get(c.top);
                    const Shade shade(f.fog, shade_factor(d1));
                    for (int i = 0; i < nv; ++i) {
                        const int s = va[i], e = vb[i];
                        for (int y = s; y < e; ++y) {
                            const float den = (y + 0.5f - hz) / S + m;
                            const float d = den > 1e-5f ? std::clamp(n / den, da, db) : db;
                            const int u = int((f.px + d * rx) * 32.0f) & 31;
                            const int v = int((f.py + d * ry) * 32.0f) & 31;
                            col[y] = shade(tex.px[(v << 5) | u]);
                        }
                        if (tile_lit && tz == f.hl.z)
                            for (int y = s; y < e; ++y)
                                col[y] = brighten(col[y]);
                        record(s, e, db, mapx, mapy, tz);
                    }
                };
                // A slope, as straight pieces from near to far. A valley (a slope
                // between two walls) bends in the middle of the tile. A slope up
                // at one or three corners is curved: along a ray crossing it its
                // height can dip (or rise) between where the ray enters and leaves,
                // so one straight piece would lie flat above the dip.
                const auto slope = [&](const Cell &c, float base, int tz) {
                    float cuts[8];
                    int nc = 0;
                    const auto cut = [&](float d) {
                        if (d > d0 + 1e-4f && d < d1 - 1e-4f && nc < 8)
                            cuts[nc++] = d;
                    };
                    if ((c.ramp & RAMP_VALLEY_U) && rx != 0)
                        cut((mapx + 0.5f - f.px) / rx);
                    else if ((c.ramp & RAMP_VALLEY_V) && ry != 0)
                        cut((mapy + 0.5f - f.py) / ry);
                    const float nw = (c.ramp & RAMP_NW) ? 1.0f : 0.0f, ne = (c.ramp & RAMP_NE) ? 1.0f : 0.0f;
                    const float sw = (c.ramp & RAMP_SW) ? 1.0f : 0.0f, se = (c.ramp & RAMP_SE) ? 1.0f : 0.0f;
                    const float cc = nw - ne - sw + se;   // how much it curves
                    const bool curved = cc != 0 && rx != 0 && ry != 0;
                    if (curved) {
                        // where the height along the ray stops falling or rising
                        const float u0 = f.px - mapx, v0 = f.py - mapy;
                        const float lin = (ne - nw) * rx + (sw - nw) * ry + cc * (rx * v0 + ry * u0);
                        cut(-lin / (2 * cc * rx * ry));
                    }
                    std::sort(cuts, cuts + nc);
                    if (curved) {
                        // and halfway along each piece, so the curve is followed closely
                        float ends[9];
                        int ne_ = 0;
                        ends[ne_++] = d0;
                        for (int i = 0; i < nc; ++i)
                            ends[ne_++] = cuts[i];
                        const int pieces = ne_;
                        ends[ne_] = d1;
                        nc = 0;
                        for (int i = 0; i < pieces; ++i) {
                            if (i > 0)
                                cuts[nc++] = ends[i];
                            if (nc < 8)
                                cuts[nc++] = 0.5f * (ends[i] + ends[i + 1]);
                        }
                    }
                    float da = d0, ua = entry_u, va_ = entry_v;
                    for (int i = 0; i <= nc; ++i) {
                        const float db = i < nc ? cuts[i] : d1;
                        const float ub = i < nc ? std::clamp(f.px + db * rx - mapx, 0.0f, 1.0f) : exit_u;
                        const float vb_ = i < nc ? std::clamp(f.py + db * ry - mapy, 0.0f, 1.0f) : exit_v;
                        if (db > da + 1e-4f)
                            slope_piece(c, base, tz, da, db, ua, va_, ub, vb_);
                        da = db;
                        ua = ub;
                        va_ = vb_;
                    }
                };

                // Shapes standing inside the tile (furniture, battlements): blocks
                // the ray enters and leaves somewhere between the tile's edges.
                // They are drawn before the tile's own floor: seen from the eye,
                // the floor in front of a block lies below it on the screen and
                // the floor behind it is hidden by it.
                for (int k = k_lo; f.col_box[ci] && k <= k_hi && open.n > 0; ++k) {
                    const Cell *cells = f.cells[k];
                    if (!cells || !cells[ci].box || cells[ci].box >= f.nboxsets)
                        continue;
                    const BoxSet &set = f.boxsets[cells[ci].box];
                    struct BoxHit {
                        float tin, tout;
                        const Box *box;
                        int side;
                    };
                    BoxHit hits[16];
                    int nh = 0;
                    for (int i = 0; i < set.n; ++i) {
                        const Box &b = set.b[i];
                        float ax = -1e30f, bx = 1e30f, ay = -1e30f, by = 1e30f;
                        if (rx != 0) {
                            const float t0 = (mapx + b.u0 - f.px) / rx, t1 = (mapx + b.u1 - f.px) / rx;
                            ax = std::min(t0, t1);
                            bx = std::max(t0, t1);
                        } else if (f.px < mapx + b.u0 || f.px > mapx + b.u1) {
                            continue;
                        }
                        if (ry != 0) {
                            const float t0 = (mapy + b.v0 - f.py) / ry, t1 = (mapy + b.v1 - f.py) / ry;
                            ay = std::min(t0, t1);
                            by = std::max(t0, t1);
                        } else if (f.py < mapy + b.v0 || f.py > mapy + b.v1) {
                            continue;
                        }
                        const float tin = std::max(ax, ay), tout = std::min(bx, by);
                        if (tout <= 0.02f || tout <= tin)
                            continue;
                        int at = nh++;      // nearest first
                        while (at > 0 && hits[at - 1].tin > tin) {
                            hits[at] = hits[at - 1];
                            --at;
                        }
                        hits[at] = BoxHit{tin, tout, &b, ax > ay ? 0 : 1};
                    }
                    if (!nh)
                        continue;
                    const int tz = f.zlo + k;
                    const bool lit = tile_lit && tz == f.hl.z;
                    // distance of the tile's middle, where things standing in it are:
                    // the part of a block's top nearer than that hides them
                    const float mid = (mapx + 0.5f - f.px) * f.dirx + (mapy + 0.5f - f.py) * f.diry;
                    // Each pixel is tested against every block: blocks stacked on
                    // one another (a sheet on a bed frame) hide parts of each
                    // other that no order of drawing whole faces gets right.
                    constexpr float NEAR = 0.02f;
                    float ylo = float(H), yhi = 0;
                    for (int i = 0; i < nh; ++i) {
                        const Box &b = *hits[i].box;
                        const float h0 = f.zrel[k] + b.z0, h1 = f.zrel[k] + b.z1;
                        const float in_n = S / std::max(hits[i].tin, NEAR), in_f = S / hits[i].tout;
                        for (const float y : {hz - h1 * in_n, hz - h1 * in_f, hz - h0 * in_n, hz - h0 * in_f}) {
                            ylo = std::min(ylo, y);
                            yhi = std::max(yhi, y);
                        }
                    }
                    const int ya = std::clamp(int(std::floor(ylo)), 0, H);
                    const int yb = std::clamp(int(std::ceil(yhi)) + 1, 0, H);
                    if (ya >= yb)
                        continue;
                    int pa[MAX_OPEN + 2], pb[MAX_OPEN + 2];
                    const int np = open.peek(ya, yb, pa, pb);
                    enum { NONE, SIDE, TOP, UNDER };
                    for (int j = 0; j < np; ++j) {
                        int run = -1, run_box = -1, run_kind = NONE;
                        float run_depth = 0;
                        const auto flush = [&](int y) {
                            if (run < 0)
                                return;
                            const int nv = open.clip(run, y, va, vb);
                            for (int q = 0; q < nv; ++q) {
                                if (lit)
                                    for (int yy = va[q]; yy < vb[q]; ++yy)
                                        col[yy] = brighten(col[yy]);
                                record(va[q], vb[q], run_depth, mapx, mapy, tz);
                            }
                            run = -1;
                        };
                        for (int y = pa[j]; y < pb[j]; ++y) {
                            // height above the eye gained per tile of distance along this row
                            const float kk = (hz - (y + 0.5f)) / S;
                            float best = FAR_DEPTH;
                            int bi = -1, kind = NONE;
                            for (int i = 0; i < nh; ++i) {
                                const Box &b = *hits[i].box;
                                const float h0 = f.zrel[k] + b.z0, h1 = f.zrel[k] + b.z1;
                                const bool outside = hits[i].tin > NEAR;
                                float lo = std::max(hits[i].tin, NEAR), hi = hits[i].tout;
                                int kd = outside ? SIDE : NONE;
                                if (kk > 1e-6f) {
                                    const float d0 = h0 / kk;
                                    if (d0 > lo) {
                                        lo = d0;
                                        kd = UNDER;
                                    }
                                    hi = std::min(hi, h1 / kk);
                                } else if (kk < -1e-6f) {
                                    const float d0 = h1 / kk;
                                    if (d0 > lo) {
                                        lo = d0;
                                        kd = TOP;
                                    }
                                    hi = std::min(hi, h0 / kk);
                                } else if (h0 > 0 || h1 < 0) {
                                    continue;
                                }
                                if (kd != NONE && lo < hi && lo < best && b.face) {
                                    // a cut-out picture (a shop sign): where it is empty
                                    // (px 0) the ray goes on to what is behind
                                    const Tex &ft = scene.tex.get(b.face);
                                    float across;
                                    if (b.face_sides & (FACE_E | FACE_W)) {
                                        across = (f.py + lo * ry - mapy - b.v0) / std::max(b.v1 - b.v0, 1e-3f);
                                        if (rx < 0)
                                            across = 1 - across;
                                    } else {
                                        across = (f.px + lo * rx - mapx - b.u0) / std::max(b.u1 - b.u0, 1e-3f);
                                        if (ry > 0)
                                            across = 1 - across;
                                    }
                                    const float down = std::clamp((h1 - kk * lo) / std::max(h1 - h0, 1e-4f), 0.0f, 0.999f);
                                    if (ft.stretch &&
                                        ft.px[(int(down * 32) << 5) + std::clamp(int(across * 32), 0, 31)] == 0)
                                        continue;
                                }
                                if (kd != NONE && lo < hi && lo < best) {
                                    best = lo;
                                    bi = i;
                                    kind = kd;
                                }
                            }
                            if (bi < 0) {
                                flush(y);
                                continue;
                            }
                            const Box &b = *hits[bi].box;
                            const float wx = f.px + best * rx, wy = f.py + best * ry;
                            uint32_t c;
                            float depth;
                            if (kind == SIDE) {
                                const int side_in = hits[bi].side;
                                // which side of the block it is: a cabinet has its doors on one
                                const uint8_t bit = side_in == 0 ? (rx > 0 ? FACE_W : FACE_E) : (ry > 0 ? FACE_N : FACE_S);
                                const bool pictured = b.face && (b.face_sides & bit);
                                const Tex &tex = scene.tex.get(pictured ? b.face : b.side);
                                const float h0 = f.zrel[k] + b.z0, h1 = f.zrel[k] + b.z1;
                                const float down = std::clamp((h1 - kk * best) / std::max(h1 - h0, 1e-4f), 0.0f, 0.999f);
                                int texx, texy;
                                if (tex.stretch) {
                                    // stretched over the whole side, reading the right way round
                                    float across = side_in == 1 ? (wx - mapx - b.u0) / std::max(b.u1 - b.u0, 1e-3f)
                                                                : (wy - mapy - b.v0) / std::max(b.v1 - b.v0, 1e-3f);
                                    if (side_in == 1 ? ry > 0 : rx < 0)
                                        across = 1 - across;
                                    texx = std::clamp(int(across * 32), 0, 31);
                                    texy = int(down * 32);
                                } else {
                                    float along = side_in == 0 ? wy : wx;
                                    along -= std::floor(along);
                                    texx = std::min(31, int(along * 32));
                                    texy = int(down * (b.z1 - b.z0) * 32) & 31;
                                }
                                depth = hits[bi].tin;
                                const Shade shade(f.fog, int(shade_factor(depth) * (side_in && !pictured ? 0.8f : 1.0f)));
                                c = shade(tex.px[(texy << 5) + texx]);
                            } else {
                                const bool top = kind == TOP;
                                const Tex &tex = scene.tex.get(top ? b.top : b.side);
                                int u, v;
                                if (tex.stretch) {
                                    u = std::clamp(int((wx - mapx - b.u0) * 32.0f / std::max(b.u1 - b.u0, 1e-3f)), 0, 31);
                                    v = std::clamp(int((wy - mapy - b.v0) * 32.0f / std::max(b.v1 - b.v0, 1e-3f)), 0, 31);
                                } else {
                                    u = int(wx * 32.0f) & 31;
                                    v = int(wy * 32.0f) & 31;
                                }
                                // the far part of a top lies behind what stands on it, the
                                // near part in front of what stands inside the block
                                depth = hits[bi].tout;
                                const Shade shade(f.fog, int(shade_factor(depth) * (top ? 1.0f : 0.6f)));
                                c = shade(tex.px[(v << 5) | u]);
                                if (best < mid) {
                                    depth = best;
                                    kind += 8;
                                }
                            }
                            if (run >= 0 && (bi != run_box || kind != run_kind))
                                flush(y);
                            if (run < 0) {
                                run = y;
                                run_box = bi;
                                run_kind = kind;
                                run_depth = depth;
                            }
                            col[y] = c;
                        }
                        flush(pb[j]);
                    }
                }

                // nearest to eye level first, so that nearer surfaces win
                for (int pass = 0; pass < 2 && open.n > 0; ++pass) {
                    // pass 0: things below the eye, top down; pass 1: things above, bottom up
                    for (int j = 0; j <= k_hi - k_lo && open.n > 0; ++j) {
                        const int k = pass == 0 ? k_hi - j : k_lo + j;
                        const Cell *cells = f.cells[k];
                        if (!cells)
                            continue;
                        const Cell &c = cells[ci];
                        const float h0 = f.zrel[k], h1 = f.zrel[k + 1];
                        const Cell *below = k > 0 && f.cells[k - 1] ? &f.cells[k - 1][ci] : nullptr;
                        const Cell *above = k + 1 < f.nz && f.cells[k + 1] ? &f.cells[k + 1][ci] : nullptr;
                        if (c.flags & CELL_SKY)
                            col_sky = true;
                        const int tz = f.zlo + k;
                        if (pass == 0) {
                            if (c.solid) {
                                // top of a block that has no known floor above it
                                if (!above || !(above->solid || above->flr || above->half))
                                    surface(h1, c.solid, true, 1.0f, tz);
                            } else {
                                if (c.ramp) {
                                    if (c.top)
                                        slope(c, h0, tz);
                                } else if (c.top) {
                                    surface(h0 + c.topq * QH, c.top, true, 1.0f, tz);
                                }
                                if (c.flr)
                                    surface(h0, c.flr, true, 1.0f, tz);
                            }
                        } else {
                            if (c.solid) {
                                // underside of an overhang
                                // (ceilings all use the grey roof texture, not the
                                // pattern of the floor or wall above)
                                if (below && !below->solid)
                                    surface(h0, roof ? roof : c.solid, false, 0.7f, tz);
                            } else {
                                if (c.flr && (!below || !below->solid))
                                    surface(h0, roof ? roof : c.flr, false, 0.7f, tz);
                                if (roof && (c.flags & CELL_ROOFED) && !(above && (above->flr || above->solid)))
                                    surface(h1, roof, false, 0.7f, tz);
                            }
                        }
                    }
                }
                prev_ci = ci;
                prev_mapx = mapx;
                prev_mapy = mapy;
            } else {
                prev_ci = -1;
            }

            // step into the next tile
            if (sdx < sdy) {
                sdx += ddx;
                mapx += stepx;
                side = 0;
            } else {
                sdy += ddy;
                mapy += stepy;
                side = 1;
            }
            d0 = d1;
            inv0 = inv1;
            if (d0 > f.max_d)
                break;
        }

        // whatever is still empty is sky above the horizon and haze below it
        for (int i = 0; i < open.n; ++i) {
            const int s = open.a[i], e = open.b[i];
            const int sky_end = col_sky ? std::min(e, f.hzi) : s;
            int y = s;
            for (; y < sky_end; ++y)
                col[y] = sky_row_[y];
            for (; y < e; ++y)
                col[y] = f.fog;
            record(s, e, FAR_DEPTH, -1, -1, -1);
        }
        span_count_[sx] = uint8_t(nspans);
        hit_count_[sx] = 0;
        masked_count_[sx] = uint8_t(nmasked);
    }
}

// The creature under the pointer: a faint disc on the ground around its feet in
// its colour (how it stands to you) and, while it walks, an arrow from the disc
// towards the tile it steps to next. Drawn before the creature, over the ground.
template <class MaskedFn>
void Renderer::draw_ring(const Frame &f, const Billboard &b, float bill_depth, int x0, int x1,
                         MaskedFn &draw_masked_until) {
    const int W = f.W, H = f.H;
    constexpr float R = 0.36f, RIM = 0.29f, A0 = R * 0.95f, A1 = R + 0.42f;
    float ux = b.move_dx, uy = b.move_dy;
    const float ul = std::sqrt(ux * ux + uy * uy);
    const bool arrow = ul > 0;
    if (arrow) {
        ux /= ul;
        uy /= ul;
    }
    const float reach = arrow ? A1 : R;
    const float drop = f.eye - b.base;   // the eye's height over the disc
    if (drop <= 0.01f)
        return;
    // the part of the screen it can cover
    float sx_lo = 1e9f, sx_hi = -1e9f, d_lo = 1e9f, d_hi = 0;
    for (int i = 0; i < 4; ++i) {
        const float wx = b.x + ((i & 1) ? reach : -reach), wy = b.y + ((i & 2) ? reach : -reach);
        const float relx = wx - f.px, rely = wy - f.py;
        const float depth = relx * f.dirx + rely * f.diry;
        if (depth < 0.2f)
            return;
        const float sx = W / 2.0f + (relx * f.rgtx + rely * f.rgty) * f.S / depth;
        sx_lo = std::min(sx_lo, sx);
        sx_hi = std::max(sx_hi, sx);
        d_lo = std::min(d_lo, depth);
        d_hi = std::max(d_hi, depth);
    }
    const int cx0 = std::max(x0, int(std::floor(sx_lo))), cx1 = std::min(x1, int(std::ceil(sx_hi)) + 1);
    const int cy0 = std::clamp(int(f.hz + drop * f.S / d_hi) - 1, 0, H);
    const int cy1 = std::clamp(int(f.hz + drop * f.S / d_lo) + 2, 0, H);
    if (cx0 >= cx1 || cy0 >= cy1)
        return;
    const uint32_t col_ring = b.ring;
    const auto mix = [](uint32_t under, uint32_t over, int a) {
        const int r = (px_r(under) * (256 - a) + px_r(over) * a) >> 8;
        const int g = (px_g(under) * (256 - a) + px_g(over) * a) >> 8;
        const int bl = (px_b(under) * (256 - a) + px_b(over) * a) >> 8;
        return rgb(r, g, bl);
    };
    for (int sx = cx0; sx < cx1; ++sx) {
        const float camx = (2 * (sx + 0.5f) / W) - 1;
        const float rx = f.dirx + f.planex * camx, ry = f.diry + f.planey * camx;
        uint32_t *col = f.buf + size_t(sx) * H;
        const Span *spans = &spans_[size_t(sx) * MAX_SPANS];
        const int nspans = span_count_[sx];
        draw_masked_until(sx, bill_depth);   // as the creature itself would
        for (int si = 0; si < nspans; ++si) {
            const Span &s = spans[si];
            const int a = std::max<int>(s.a, cy0), e = std::min<int>(s.b, cy1);
            for (int y = a; y < e; ++y) {
                const float below = y + 0.5f - f.hz;
                if (below <= 0)
                    continue;
                const float d = drop * f.S / below;
                if (d > s.depth + 0.05f)
                    continue;   // something stands in front of the ground there
                const float dx = f.px + d * rx - b.x, dy = f.py + d * ry - b.y;
                const float r2 = dx * dx + dy * dy;
                int alpha = 0;
                if (r2 <= R * R) {
                    alpha = r2 >= RIM * RIM ? 120 : 46;
                } else if (arrow) {
                    const float fw = dx * ux + dy * uy, lat = std::abs(dx * -uy + dy * ux);
                    if ((fw >= A0 && fw <= R + 0.2f && lat <= 0.05f) || (fw >= R + 0.16f && fw <= A1 && lat <= (A1 - fw) * 0.6f))
                        alpha = 120;
                }
                if (!alpha)
                    continue;
                col[y] = mix(col[y], col_ring, alpha);
            }
        }
    }
}

void Renderer::renderBillboards(const Frame &f, int x0, int x1) {
    const int W = f.W, H = f.H;
    const float inv_view = 1.0f / f.view_dist;

    // See-through faces are drawn with the billboards, far to near, so that a
    // creature behind a door frame shows through the opening and one in front
    // of it covers the frame. next_masked[i] is the farthest face of column
    // x0 + i that has not been drawn yet.
    int next_masked[64];
    for (int sx = x0; sx < x1; ++sx)
        next_masked[sx - x0] = int(masked_count_[sx]) - 1;
    const auto draw_masked_until = [&](int sx, float nearer_than) {
        int &next = next_masked[sx - x0];
        uint32_t *col = f.buf + size_t(sx) * H;
        while (next >= 0) {
            const Masked &m = masked_[size_t(sx) * MAX_MASKED + next];
            if (m.depth <= nearer_than)
                break;
            --next;
            const Tex &tex = f.scene->tex.get(m.tex);
            const Shade shade(f.fog, m.shade);
            const float scale = 32.0f / std::max(m.bot - m.top, 1e-3f);
            const bool lit = f.hl.unit_id < 0 && f.hl.x == m.tx && f.hl.y == m.ty && f.hl.z == m.tz;
            const Span *spans = &spans_[size_t(sx) * MAX_SPANS];
            const int nspans = span_count_[sx];
            int lo = H, hi = 0;
            for (int si = 0; si < nspans; ++si) {
                const Span &s = spans[si];
                if (m.depth >= s.depth)
                    continue;
                const int a = std::max<int>(s.a, std::max(0, int(m.top + 0.5f)));
                const int e = std::min<int>(s.b, std::min(H, int(m.bot + 0.5f)));
                for (int y = a; y < e; ++y) {
                    const int v = std::clamp(int((y + 0.5f - m.top) * scale), 0, 31);
                    const uint32_t c = tex.px[(v << 5) + m.texx];
                    if (c) {
                        col[y] = lit ? brighten(shade(c)) : shade(c);
                        lo = std::min(lo, y);
                        hi = std::max(hi, y + 1);
                    }
                }
            }
            if (lo < hi) {
                uint8_t &n = hit_count_[sx];
                if (n < MAX_HITS)
                    hits_[size_t(sx) * MAX_HITS + n++] =
                        BillHit{int16_t(lo), int16_t(hi), -1, m.tx, m.ty, m.tz, int8_t(BillKind::Building), 0};
            }
        }
    };

    for (size_t bi = 0; bi < f.bills.size(); ++bi) {
        const Billboard &b = *f.bills[bi];
        const Sprite &sp = *b.sprite;
        const float depth = f.bill_depth[bi];
        if (b.ring && f.hl.unit_id >= 0 && b.unit_id == f.hl.unit_id)
            draw_ring(f, b, depth, x0, x1, draw_masked_until);
        const float inv = f.S / depth;
        const float pps = inv * b.scale * THING_SCALE / TEX;   // screen pixels per sprite pixel
        const float left = W / 2.0f + f.bill_lat[bi] * inv - b.anchor_px * pps;
        const float ground = f.hz + (f.eye - b.base) * inv;
        const float box_h = (sp.y1 - sp.y0) * pps;
        // the sprite's lowest opaque row stands on the ground (or sinks into it)
        const float topy = ground + b.sink * box_h - sp.y1 * pps;
        // an outlined sprite is one sprite pixel bigger all round
        const int edge = b.outline ? 1 : 0;
        const float fx0 = left + (sp.x0 - edge) * pps, fx1 = left + (sp.x1 + edge) * pps;
        const float fy0 = topy + (sp.y0 - edge) * pps, fy1 = std::min(topy + sp.y1 * pps, ground);
        const int cx0 = std::max(x0, int(fx0 + 0.5f)), cx1 = std::min(x1, int(fx1 + 0.5f));
        const int cy0 = std::max(0, int(fy0 + 0.5f)), cy1 = std::min(H, int(fy1 + 0.5f));
        if (cx0 >= cx1 || cy0 >= cy1)
            continue;

        float t = std::min(depth * inv_view, 1.0f);
        const Shade shade(f.fog, int(256 * (1 - 0.97f * t * std::sqrt(std::sqrt(t)))));
        const float inv_pps = 1.0f / pps;
        const int ystep = int(inv_pps * 65536.0f);
        const bool lit = f.hl.valid() &&
                         (f.hl.unit_id >= 0 ? b.unit_id == f.hl.unit_id
                                            : (b.mx == f.hl.x && b.my == f.hl.y && b.mz == f.hl.z));
        const auto solid = [&](int x, int y) {
            return x >= sp.x0 && x < sp.x1 && y >= sp.y0 && y < sp.y1 && sp.px[size_t(y) * sp.w + x] != 0;
        };

        for (int sx = cx0; sx < cx1; ++sx) {
            draw_masked_until(sx, depth);   // faces farther than this billboard go first
            const int tx = int(std::floor((sx + 0.5f - left) * inv_pps));
            if (tx < sp.x0 - edge || tx >= sp.x1 + edge)
                continue;
            uint32_t *col = f.buf + size_t(sx) * H;
            const Span *spans = &spans_[size_t(sx) * MAX_SPANS];
            const int nspans = span_count_[sx];
            bool drew = false;
            int lo = H, hi = 0;
            for (int si = 0; si < nspans; ++si) {
                const Span &s = spans[si];
                if (depth >= s.depth)
                    continue;
                const int a = std::max<int>(s.a, cy0), e = std::min<int>(s.b, cy1);
                if (a >= e)
                    continue;
                int tpos = int(std::floor((a + 0.5f - topy) * inv_pps * 65536.0f));
                for (int y = a; y < e; ++y) {
                    const int ty = tpos >> 16;
                    tpos += ystep;
                    if (ty < sp.y0 - edge || ty >= sp.y1)
                        continue;
                    if (solid(tx, ty)) {
                        const uint32_t c = sp.px[size_t(ty) * sp.w + tx];
                        col[y] = lit ? brighten(shade(c)) : shade(c);
                    } else if (edge && (solid(tx - 1, ty) || solid(tx + 1, ty) || solid(tx, ty - 1) ||
                                        solid(tx, ty + 1))) {
                        col[y] = b.outline;
                    }
                }
                drew = true;
                lo = std::min(lo, a);
                hi = std::max(hi, e);
            }
            if (drew) {
                uint8_t &n = hit_count_[sx];
                if (n < MAX_HITS)
                    hits_[size_t(sx) * MAX_HITS + n++] =
                        BillHit{int16_t(lo), int16_t(hi), b.unit_id, b.mx, b.my, b.mz, int8_t(b.kind), b.texpos};
            }
        }
    }
    // whatever see-through faces are left are nearer than every billboard
    for (int sx = x0; sx < x1; ++sx)
        draw_masked_until(sx, -1.0f);
}

void Renderer::swapRedBlue(uint32_t *buf, size_t count) {
    constexpr size_t CHUNK = 1 << 16;
    std::atomic<size_t> next{0};
    pool_.run([&](int) {
        for (;;) {
            const size_t a = next.fetch_add(CHUNK);
            if (a >= count)
                break;
            const size_t e = std::min(count, a + CHUNK);
            for (size_t i = a; i < e; ++i) {
                const uint32_t c = buf[i];
                buf[i] = (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
            }
        }
    });
}

bool Renderer::findCreature(int &x, int &y) const {
    // widest run of neighbouring columns whose topmost billboard is the same creature
    int best_len = 0, best_x = 0, best_y = 0;
    int run_id = -1, run_start = 0, run_y = 0;
    for (int sx = 0; sx <= last_w_; ++sx) {
        int id = -1, mid = 0;
        if (sx < last_w_) {
            for (int i = hit_count_[sx] - 1; i >= 0; --i) {
                const BillHit &h = hits_[size_t(sx) * MAX_HITS + i];
                if (h.unit_id >= 0) {
                    id = h.unit_id;
                    mid = (h.a + h.b) / 2;
                    break;
                }
            }
        }
        if (id != run_id) {
            if (run_id >= 0 && sx - run_start > best_len) {
                best_len = sx - run_start;
                best_x = (run_start + sx) / 2;
                best_y = run_y;
            }
            run_id = id;
            run_start = sx;
        }
        if (id >= 0)
            run_y = mid;
    }
    if (best_len < 4)
        return false;
    x = best_x;
    y = best_y;
    return true;
}

Pick Renderer::pick(int x, int y) const {
    Pick p;
    if (x < 0 || y < 0 || x >= last_w_ || y >= last_h_)
        return p;
    // billboards were drawn far to near, so the last hit covering the row is on top
    const int nh = hit_count_[x];
    for (int i = nh - 1; i >= 0; --i) {
        const BillHit &h = hits_[size_t(x) * MAX_HITS + i];
        if (y >= h.a && y < h.b) {
            p.x = h.tx;
            p.y = h.ty;
            p.z = h.tz;
            p.unit_id = h.unit_id;
            p.kind = h.kind;
            p.texpos = h.texpos;
            return p;
        }
    }
    const int ns = span_count_[x];
    for (int i = 0; i < ns; ++i) {
        const Span &s = spans_[size_t(x) * MAX_SPANS + i];
        if (y >= s.a && y < s.b) {
            p.x = s.tx;
            p.y = s.ty;
            p.z = s.tz;
            return p;
        }
    }
    return p;
}

} // namespace fpv
