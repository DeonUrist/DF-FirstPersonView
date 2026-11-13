#include "gpu.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fpv {

namespace {

constexpr float NEAR_D = 0.08f;         // nothing nearer to the eye than this is drawn
constexpr int MAX_SPLIT = 12;           // pieces a surface is cut into along one side, at most
constexpr float SPLIT_RATIO = 1.12f;    // a piece may be this much deeper at one end than at the other
constexpr float INV_LOG_SPLIT = 1.0f / 0.113329f;   // 1 / ln(SPLIT_RATIO)
constexpr int WHITE_BOX = 8;            // a plain white square in the texture, for flat colours
constexpr int SKY_SECTORS = 32;

enum { PASS_BASE = 1, PASS_FOG = 2, PASS_LIT = 4, PASS_ALL = 7 };

// our pixels are 0xAABBGGRR, the texture wants 0xAARRGGBB
inline uint32_t to_argb(uint32_t c) {
    return (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
}

// a rectangle of the big texture, in its pixels
struct Region {
    float x = 0, y = 0, w = 0, h = 0;
    bool ok = false;
};

// A wall or floor texture is stored with smaller copies of itself next to it
// (each half the size of the one before), for when it is seen from far away:
// drawn from the full picture it would flicker as you move. One block of the
// big texture holds them all, each with a spare pixel all round:
//   the 32 pixel picture on the left, the smaller ones in a column right of it.
constexpr int MIPS = 6;
constexpr int MIP_BLOCK_W = 52, MIP_BLOCK_H = 34;
constexpr int MIP_X[MIPS] = {1, 35, 35, 45, 45, 49};   // where each copy starts inside the block
constexpr int MIP_Y[MIPS] = {1, 1, 19, 19, 25, 25};
constexpr int MIP_SIZE[MIPS] = {32, 16, 8, 4, 2, 1};

// how a surface is drawn
struct Mat {
    Region base;          // its picture
    Region sil;           // the same picture all white, for the haze and the highlight over a cut-out
    uint8_t shade = 255;  // sides and ceilings: under 255, the more haze lies over them
    bool mip = false;     // the picture has smaller copies for far away (a wall or floor texture)
    bool lit = false;     // under the pointer
    bool opaque = true;   // no holes: haze and highlight are plain rectangles
};

// a corner in the camera's space (x right, y up, z forward) with its place in the picture (0..1)
struct VV {
    float x, y, z, s, t;
};

inline VV mix(const VV &a, const VV &b, float k) {
    return VV{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, a.s + (b.s - a.s) * k,
              a.t + (b.t - a.t) * k};
}

// the part of a polygon at or beyond a distance in front of the eye
int clip_near(const VV *in, int n, VV *out, float nearz) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const VV &a = in[i], &b = in[(i + 1) % n];
        const bool ina = a.z >= nearz, inb = b.z >= nearz;
        if (ina)
            out[m++] = a;
        if (ina != inb) {
            // always from the corner that stays: two surfaces sharing the edge cut it at the same point
            const VV &p = ina ? a : b, &q = ina ? b : a;
            out[m++] = mix(p, q, (nearz - p.z) / (q.z - p.z));
        }
    }
    return m;
}

// how much haze lies over something, by its distance as a fraction of the view distance
struct FogTable {
    uint8_t a[1025];
    FogTable() {
        for (int i = 0; i <= 1024; ++i) {
            const float t = i / 1024.0f;
            a[i] = uint8_t(std::clamp(0.97f * t * std::sqrt(std::sqrt(t)) * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
};
const FogTable FOG;

struct TexEntry {
    Region r[3];                    // opaque, cut out, cut out in white
    std::atomic<uint8_t> ready[3];  // set once r is filled in: workers look these up side by side
};
constexpr int MAX_TEX = 65536;

// the triangles of one row of the map, made by one worker
struct RowOut {
    std::vector<SDL_Vertex> v;
    std::vector<int> i;
};

struct SpriteEntry {
    Region img, sil;
    std::unordered_map<uint32_t, Region> outlined;   // by the colour of the line around it
};

struct PtrRef {
    std::weak_ptr<const Sprite> alive;
    SpriteEntry *entry = nullptr;
};

struct BRef {
    int key;
    float depth;
    const Billboard *b;
};

// a flat piece of the mark under the creature the pointer is on
struct DecalPoly {
    float x[4], y[4];
    int n;
    uint8_t alpha;
};

} // namespace

struct GpuRenderer::Impl {
    GpuApi api;
    SDL_Texture *tex = nullptr;
    SDL_Renderer *owner = nullptr;
    int size = 0;
    float inv = 0;
    std::vector<uint32_t> px;                  // the big texture's pixels, as uploaded
    int shelf_x = 0, shelf_y = 0, shelf_h = 0; // pictures are put in rows, left to right
    int dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;    // the part changed since the last upload
    bool want_reset = false;
    uint32_t tex_gen = ~0u;
    Region white;
    std::unique_ptr<TexEntry[]> texs{new TexEntry[MAX_TEX]};
    std::mutex atlas_mutex;                    // guards everything about the big texture while workers run
    WorkerPool pool{std::clamp(int(std::thread::hardware_concurrency()) - 2, 1, 8)};
    std::vector<RowOut> rows;
    std::unordered_map<uint64_t, std::unique_ptr<SpriteEntry>> sprites;   // by what they show
    std::unordered_map<const Sprite *, PtrRef> by_ptr;
    uint32_t frame = 0;

    std::vector<SDL_Vertex> verts;
    std::vector<int> idx;
    std::vector<BRef> brefs;
    std::vector<int16_t> bill_lo, bill_hi;
    std::vector<int> order_x, order_y, order_z;

    Camera cam;
    RenderOptions opt;
    int W = 0, H = 0;
    bool valid = false;
    int last_tris = 0;
    int us_prep = 0, us_cells = 0, us_draw = 0, n_cols = 0, n_cells = 0, n_bills = 0;

    void dirty(int x, int y, int w, int h) {
        if (dx1 <= dx0 || dy1 <= dy0) {
            dx0 = x;
            dy0 = y;
            dx1 = x + w;
            dy1 = y + h;
        } else {
            dx0 = std::min(dx0, x);
            dy0 = std::min(dy0, y);
            dx1 = std::max(dx1, x + w);
            dy1 = std::max(dy1, y + h);
        }
    }

    bool alloc(int w, int h, int &x, int &y) {
        if (w > size || h > size)
            return false;
        if (shelf_x + w > size) {
            shelf_x = 0;
            shelf_y += shelf_h;
            shelf_h = 0;
        }
        if (shelf_y + h > size) {
            want_reset = true;   // full: start again with the next frame
            return false;
        }
        x = shelf_x;
        y = shelf_y;
        shelf_x += w;
        shelf_h = std::max(shelf_h, h);
        dirty(x, y, w, h);
        return true;
    }

    void resetAtlas() {
        shelf_x = shelf_y = shelf_h = 0;
        want_reset = false;
        for (int i = 0; i < MAX_TEX; ++i)
            for (auto &r : texs[i].ready)
                r.store(0, std::memory_order_relaxed);
        sprites.clear();
        by_ptr.clear();
        int x = 0, y = 0;
        alloc(WHITE_BOX, WHITE_BOX, x, y);
        for (int yy = 0; yy < WHITE_BOX; ++yy)
            for (int xx = 0; xx < WHITE_BOX; ++xx)
                px[size_t(y + yy) * size + x + xx] = 0xFFFFFFFFu;
        white = Region{x + WHITE_BOX / 2.0f, y + WHITE_BOX / 2.0f, 0, 0, false};
    }

    bool ensureTexture(SDL_Renderer *sdl) {
        if (tex && owner == sdl)
            return true;
        if (tex && api.destroy_texture)
            api.destroy_texture(tex);
        tex = nullptr;
        owner = sdl;
        int want = 4096;
        SDL_RendererInfo info;
        std::memset(&info, 0, sizeof(info));
        if (api.get_renderer_info && api.get_renderer_info(sdl, &info) == 0) {
            if (info.max_texture_width > 0)
                want = std::min(want, info.max_texture_width);
            if (info.max_texture_height > 0)
                want = std::min(want, info.max_texture_height);
        }
        for (; want >= 1024; want /= 2) {
            tex = api.create_texture(sdl, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, want, want);
            if (tex)
                break;
        }
        if (!tex)
            return false;
        api.set_texture_blend_mode(tex, SDL_BLENDMODE_BLEND);
        api.set_texture_scale_mode(tex, SDL_ScaleModeNearest);
        size = want;
        inv = 1.0f / want;
        px.assign(size_t(size) * size, 0);
        dx0 = dy0 = dx1 = dy1 = 0;
        tex_gen = ~0u;
        resetAtlas();
        return true;
    }

    void upload() {
        if (!tex || dx1 <= dx0 || dy1 <= dy0)
            return;
        const SDL_Rect r = {dx0, dy0, dx1 - dx0, dy1 - dy0};
        api.update_texture(tex, &r, px.data() + size_t(dy0) * size + dx0, size * 4);
        dx0 = dy0 = dx1 = dy1 = 0;
    }

    // a wall or floor texture: mode 0 as it is, 1 with its empty pixels
    // see-through, 2 the same in white
    Region texRegion(const Scene &scene, uint16_t id, int mode) {
        if (!id || id >= scene.tex.size())
            return white;
        if (texs[id].ready[mode].load(std::memory_order_acquire))
            return texs[id].r[mode];
        std::lock_guard<std::mutex> lock(atlas_mutex);
        if (texs[id].ready[mode].load(std::memory_order_relaxed))
            return texs[id].r[mode];
        int x, y;
        if (!alloc(MIP_BLOCK_W, MIP_BLOCK_H, x, y))
            return white;
        const Tex &t = scene.tex.get(id);
        uint32_t cur[TEXPIX], half[TEXPIX / 4];
        for (int i = 0; i < TEXPIX; ++i) {
            const uint32_t c = t.px[i];
            if (mode == 0)
                cur[i] = to_argb(c) | 0xFF000000u;
            else if (mode == 1)
                cur[i] = c ? (to_argb(c) | 0xFF000000u) : 0;
            else
                cur[i] = c ? 0xFFFFFFFFu : 0;
        }
        for (int level = 0; level < MIPS; ++level) {
            const int n = MIP_SIZE[level];
            // one more pixel all round, a copy of the edge, so that nothing of
            // the picture next to it shows along the edge
            for (int yy = -1; yy <= n; ++yy) {
                const int sy = std::clamp(yy, 0, n - 1);
                uint32_t *row = &px[size_t(y + MIP_Y[level] + yy) * size + x + MIP_X[level]];
                for (int xx = -1; xx <= n; ++xx)
                    row[xx] = cur[sy * n + std::clamp(xx, 0, n - 1)];
            }
            if (level + 1 == MIPS)
                break;
            // the next copy: every two by two pixels become one, see-through ones counting for nothing
            const int h = n / 2;
            for (int yy = 0; yy < h; ++yy)
                for (int xx = 0; xx < h; ++xx) {
                    uint32_t a = 0, r = 0, g = 0, b = 0;
                    for (int k = 0; k < 4; ++k) {
                        const uint32_t c = cur[(yy * 2 + (k >> 1)) * n + xx * 2 + (k & 1)];
                        const uint32_t ca = c >> 24;
                        a += ca;
                        r += ((c >> 16) & 255) * ca;
                        g += ((c >> 8) & 255) * ca;
                        b += (c & 255) * ca;
                    }
                    half[yy * h + xx] = a ? (((a + 2) / 4) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a) : 0;
                }
            std::copy(half, half + h * h, cur);
        }
        const Region r{float(x + MIP_X[0]), float(y + MIP_Y[0]), float(TEX), float(TEX), true};
        texs[id].r[mode] = r;
        texs[id].ready[mode].store(1, std::memory_order_release);
        return r;
    }

    static uint64_t hashSprite(const Sprite &sp) {
        uint64_t h = 1469598103934665603ull;
        const auto add = [&](uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        add(uint64_t(sp.w) | (uint64_t(sp.h) << 16) | (uint64_t(sp.x0) << 32) | (uint64_t(sp.y0) << 48));
        add(uint64_t(sp.x1) | (uint64_t(sp.y1) << 16));
        for (int y = sp.y0; y < sp.y1; ++y) {
            const uint32_t *row = &sp.px[size_t(y) * sp.w];
            for (int x = sp.x0; x < sp.x1; ++x)
                add(row[x]);
        }
        return h;
    }

    static bool solid(const Sprite &sp, int x, int y) {
        return x >= sp.x0 && x < sp.x1 && y >= sp.y0 && y < sp.y1 && sp.px[size_t(y) * sp.w + x] != 0;
    }

    // the sprite's pixels from (x0, y0) to (x1, y1), with a see-through pixel
    // all round. mode 0: as it is, 1: white, 2: with a line in `line` around it
    Region putSprite(const Sprite &sp, int x0, int y0, int x1, int y1, int mode, uint32_t line) {
        const int w = x1 - x0, h = y1 - y0;
        int x, y;
        if (w <= 0 || h <= 0 || !alloc(w + 2, h + 2, x, y))
            return Region();
        for (int yy = -1; yy <= h; ++yy) {
            uint32_t *row = &px[size_t(y + 1 + yy) * size + x + 1];
            for (int xx = -1; xx <= w; ++xx) {
                const int sx = x0 + xx, sy = y0 + yy;
                uint32_t o = 0;
                if (xx >= 0 && xx < w && yy >= 0 && yy < h) {
                    if (solid(sp, sx, sy))
                        o = mode == 1 ? 0xFFFFFFFFu : (to_argb(sp.px[size_t(sy) * sp.w + sx]) | 0xFF000000u);
                    else if (mode == 2 && (solid(sp, sx - 1, sy) || solid(sp, sx + 1, sy) || solid(sp, sx, sy - 1) ||
                                           solid(sp, sx, sy + 1)))
                        o = to_argb(line) | 0xFF000000u;
                }
                row[xx] = o;
            }
        }
        return Region{float(x + 1), float(y + 1), float(w), float(h), true};
    }

    // Creatures get a new picture object every time the game is read, mostly
    // of the same picture: pictures are told apart by what they show.
    SpriteEntry *spriteEntry(const std::shared_ptr<const Sprite> &sp) {
        std::lock_guard<std::mutex> lock(atlas_mutex);
        const auto it = by_ptr.find(sp.get());
        if (it != by_ptr.end()) {
            if (!it->second.alive.owner_before(sp) && !sp.owner_before(it->second.alive))
                return it->second.entry;
            by_ptr.erase(it);
        }
        const uint64_t h = hashSprite(*sp);
        SpriteEntry *e = nullptr;
        const auto found = sprites.find(h);
        if (found != sprites.end()) {
            e = found->second.get();
        } else {
            auto made = std::make_unique<SpriteEntry>();
            made->img = putSprite(*sp, sp->x0, sp->y0, sp->x1, sp->y1, 0, 0);
            made->sil = putSprite(*sp, sp->x0, sp->y0, sp->x1, sp->y1, 1, 0);
            if (!made->img.ok || !made->sil.ok)
                return nullptr;
            e = made.get();
            sprites[h] = std::move(made);
        }
        by_ptr[sp.get()] = PtrRef{sp, e};
        return e;
    }

    Region outlinedRegion(SpriteEntry &e, const Sprite &sp, uint32_t line) {
        std::lock_guard<std::mutex> lock(atlas_mutex);
        const auto it = e.outlined.find(line);
        if (it != e.outlined.end())
            return it->second;
        const Region r = putSprite(sp, sp.x0 - 1, sp.y0 - 1, sp.x1 + 1, sp.y1, 2, line);
        if (r.ok)
            e.outlined[line] = r;
        return r;
    }

    void prune() {
        for (auto it = by_ptr.begin(); it != by_ptr.end();) {
            if (it->second.alive.expired())
                it = by_ptr.erase(it);
            else
                ++it;
        }
    }
};

namespace {

struct Builder {
    GpuRenderer::Impl &g;
    const Scene &scene;
    const RenderOptions &opt;
    float px, py, pe;                      // the eye
    float fx, fy, fz, rx, ry, ux, uy, uz;  // forward, right, up
    float S, cx, cy, W, H;
    float inv_view, max_d;
    SDL_Color fogc;
    Pick hl;
    int zlo = 0, zhi = -1, nz = 0;
    const Cell *cells[64] = {};
    int map_w = 0, map_h = 0;
    uint16_t roof = 0;
    int nboxsets = 0;
    int wx0 = 0, wy0 = 0, ww = 0, wh = 0;   // the part of the map within the view distance
    bool decal_on = false;
    int decal_cx = 0, decal_cy = 0, decal_z = 0;
    float decal_h = 0;
    SDL_Color decal_col{};
    std::vector<DecalPoly> decal;
    std::vector<SDL_Vertex> *vp = nullptr;   // where the triangles go: each worker has its own
    std::vector<int> *ip = nullptr;
    int n_cols = 0, n_cells = 0;

    Builder(GpuRenderer::Impl &g_, const Scene &scene_, const Camera &cam, const RenderOptions &opt_, int W_, int H_)
        : g(g_), scene(scene_), opt(opt_), vp(&g_.verts), ip(&g_.idx) {
        const View3D v = View3D::of(cam, W_, H_);
        px = v.px;
        py = v.py;
        pe = v.eye;
        fx = v.fx;
        fy = v.fy;
        fz = v.fz;
        rx = v.rx;
        ry = v.ry;
        ux = v.ux;
        uy = v.uy;
        uz = v.uz;
        S = v.S;
        cx = v.cx;
        cy = v.cy;
        W = float(W_);
        H = float(H_);
        const float view_dist = std::max(1.0f, opt.view_distance);
        inv_view = 1.0f / view_dist;
        max_d = view_dist + 4;
        fogc = SDL_Color{Uint8(px_r(opt.fog)), Uint8(px_g(opt.fog)), Uint8(px_b(opt.fog)), 255};
        hl = opt.highlight;
    }

    inline void tv(float wx, float wy, float wz, VV &o) const {
        const float dx = wx - px, dy = wy - py, dz = wz - pe;
        o.x = dx * rx + dy * ry;
        o.y = dx * ux + dy * uy + dz * uz;
        o.z = dx * fx + dy * fy + dz * fz;
    }

    inline void fan(int base, int n) {
        for (int i = 1; i + 1 < n; ++i) {
            (*ip).push_back(base);
            (*ip).push_back(base + i);
            (*ip).push_back(base + i + 1);
        }
    }

    // a polygon wholly in front of the eye: onto the screen
    void emit(const VV *p, int n, const Mat &m, int passes, bool expand) {
        float sx[12], sy[12];
        uint8_t fa[12];
        float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
        int amin = 255, amax = 0;
        for (int i = 0; i < n; ++i) {
            const float k = S / p[i].z;
            sx[i] = cx + p[i].x * k;
            sy[i] = cy - p[i].y * k;
            minx = std::min(minx, sx[i]);
            maxx = std::max(maxx, sx[i]);
            miny = std::min(miny, sy[i]);
            maxy = std::max(maxy, sy[i]);
            const float d = std::sqrt(p[i].x * p[i].x + p[i].y * p[i].y + p[i].z * p[i].z) * inv_view;
            const int a = FOG.a[d >= 1.0f ? 1024 : int(d * 1024.0f)];
            amin = std::min(amin, a);
            // Sides and ceilings are set apart the way the software renderer
            // does it: by more haze over them, not by darkening them.
            const int shaded = 255 - (255 - a) * m.shade / 255;
            fa[i] = uint8_t(shaded);
            amax = std::max(amax, shaded);
        }
        if (maxx < 0 || minx > W || maxy < 0 || miny > H)
            return;
        std::vector<SDL_Vertex> &V = *vp;
        // lost in the haze altogether: the haze alone will do
        const bool hazed = m.opaque && amin >= 250 && (passes & PASS_FOG);
        // Which copy of the picture: the one whose pixels come out about as
        // big as the screen's, going by how much of each the piece covers.
        Region pic = m.base, sil = m.sil.ok ? m.sil : g.white;
        if (m.mip && !hazed) {
            // How many of the picture's pixels land on one pixel of the
            // screen: on the whole (by the areas), and across the way the
            // piece is squeezed most (a floor seen from low above is squeezed
            // in depth far more than sideways). A value between the two,
            // nearer the second: a calm far floor matters more than its detail.
            float as = 0, at = 0, along = 1e30f;
            for (int i = 0; i < n; ++i) {
                const int j = i + 1 < n ? i + 1 : 0;
                as += sx[i] * sy[j] - sx[j] * sy[i];
                at += p[i].s * p[j].t - p[j].s * p[i].t;
                const float ex = sx[j] - sx[i], ey = sy[j] - sy[i];
                const float es = p[j].s - p[i].s, et = p[j].t - p[i].t;
                const float pix = ex * ex + ey * ey, tex = (es * es + et * et) * (TEX * TEX);
                if (pix > 0.25f && tex > 0.01f)
                    along = std::min(along, tex / pix);
            }
            as = std::abs(as);
            at = std::abs(at) * (TEX * TEX);
            if (as > 0.5f && at > as * 1.5f && along < 1e29f) {
                const float whole = std::sqrt(at / as);                           // both ways alike
                const float across = (at / as) / std::sqrt(std::max(along, 1e-6f));  // the squeezed way
                const float most = std::max(whole, across);
                const float ratio = std::sqrt(most * std::sqrt(whole * most));
                int level = 0;
                for (float r = 2.2f; level + 1 < MIPS && ratio >= r; r *= 2)
                    ++level;
                if (level > 0) {
                    const auto copy = [&](const Region &r0) {
                        Region r = r0;
                        r.x = r0.x - MIP_X[0] + MIP_X[level];
                        r.y = r0.y - MIP_Y[0] + MIP_Y[level];
                        r.w = r.h = float(MIP_SIZE[level]);
                        return r;
                    };
                    pic = copy(pic);
                    if (m.sil.ok)
                        sil = copy(sil);
                }
            }
        }
        if ((passes & PASS_BASE) && !hazed) {
            float mx = 0, my = 0;
            if (expand) {
                for (int i = 0; i < n; ++i) {
                    mx += sx[i];
                    my += sy[i];
                }
                mx /= n;
                my /= n;
            }
            const int base = int(V.size());
            const SDL_Color col{255, 255, 255, 255};
            for (int i = 0; i < n; ++i) {
                SDL_Vertex v;
                v.position.x = sx[i];
                v.position.y = sy[i];
                if (expand) {
                    // a little bigger, to cover the hairline gaps between pieces
                    // that were cut differently along a shared edge
                    const float ex = sx[i] - mx, ey = sy[i] - my;
                    const float len = std::sqrt(ex * ex + ey * ey);
                    if (len > 1e-3f) {
                        v.position.x += ex / len * 0.7f;
                        v.position.y += ey / len * 0.7f;
                    }
                }
                v.color = col;
                v.tex_coord.x = (pic.x + p[i].s * pic.w) * g.inv;
                v.tex_coord.y = (pic.y + p[i].t * pic.h) * g.inv;
                V.push_back(v);
            }
            fan(base, n);
        }
        if ((passes & PASS_FOG) && amax > 2) {
            const int base = int(V.size());
            for (int i = 0; i < n; ++i) {
                SDL_Vertex v;
                v.position.x = sx[i];
                v.position.y = sy[i];
                v.color = fogc;
                v.color.a = hazed ? Uint8(255) : fa[i];
                v.tex_coord.x = (sil.x + p[i].s * sil.w) * g.inv;
                v.tex_coord.y = (sil.y + p[i].t * sil.h) * g.inv;
                V.push_back(v);
            }
            fan(base, n);
        }
        if ((passes & PASS_LIT) && m.lit) {
            const int base = int(V.size());
            for (int i = 0; i < n; ++i) {
                SDL_Vertex v;
                v.position.x = sx[i];
                v.position.y = sy[i];
                v.color = SDL_Color{255, 255, 255, 76};
                v.tex_coord.x = (sil.x + p[i].s * sil.w) * g.inv;
                v.tex_coord.y = (sil.y + p[i].t * sil.h) * g.inv;
                V.push_back(v);
            }
            fan(base, n);
        }
    }

    // a polygon in one flat colour (the corners already in the camera's space)
    void plain(const VV *in, int n, SDL_Color col) {
        VV c[12];
        n = clip_near(in, n, c, NEAR_D);
        if (n < 3)
            return;
        const int base = int((*vp).size());
        for (int i = 0; i < n; ++i) {
            const float k = S / c[i].z;
            SDL_Vertex v;
            v.position.x = cx + c[i].x * k;
            v.position.y = cy - c[i].y * k;
            v.color = col;
            v.tex_coord.x = g.white.x * g.inv;
            v.tex_coord.y = g.white.y * g.inv;
            (*vp).push_back(v);
        }
        fan(base, n);
    }

    // true if all the corners are off the same side of the screen, or behind the eye
    bool offscreen(const VV *v, int n) const {
        bool l = true, r = true, u = true, d = true, b = true;
        for (int i = 0; i < n; ++i) {
            const VV &q = v[i];
            l = l && q.x * S + cx * q.z < 0;
            r = r && q.x * S - (W - cx) * q.z > 0;
            u = u && q.y * S - cy * q.z > 0;
            d = d && q.y * S + (H - cy) * q.z < 0;
            b = b && q.z < NEAR_D;
        }
        return l || r || u || d || b;
    }

    // A flat four-sided piece of the world: its corners in order around it
    // and where each is in the picture. Close to the eye it is cut into
    // smaller pieces: the graphics card stretches a picture evenly over each
    // triangle, which only looks right while a piece is about equally far
    // away all over.
    void quad(const float P[4][3], const float ST[4][2], const Mat &m, int passes = PASS_ALL) {
        VV v[4];
        float zmin = 1e30f, zmax = -1e30f;
        for (int i = 0; i < 4; ++i) {
            tv(P[i][0], P[i][1], P[i][2], v[i]);
            v[i].s = ST[i][0];
            v[i].t = ST[i][1];
            zmin = std::min(zmin, v[i].z);
            zmax = std::max(zmax, v[i].z);
        }
        if (offscreen(v, 4))
            return;
        int nu = 1, nv = 1;
        if (zmin * SPLIT_RATIO < zmax) {
            const auto steps = [](float a, float b) {
                a = std::max(a, NEAR_D);
                b = std::max(b, NEAR_D);
                const float r = a > b ? a / b : b / a;
                return r <= SPLIT_RATIO ? 1 : std::min(MAX_SPLIT, int(std::log(r) * INV_LOG_SPLIT) + 1);
            };
            nu = std::max(steps(v[0].z, v[1].z), steps(v[3].z, v[2].z));
            nv = std::max(steps(v[0].z, v[3].z), steps(v[1].z, v[2].z));
        }
        if (nu == 1 && nv == 1) {
            if (zmin >= NEAR_D) {
                emit(v, 4, m, passes, false);
            } else {
                VV c[8];
                const int n = clip_near(v, 4, c, NEAR_D);
                if (n >= 3)
                    emit(c, n, m, passes, false);
            }
            return;
        }
        VV grid[(MAX_SPLIT + 1) * (MAX_SPLIT + 1)];
        for (int j = 0; j <= nv; ++j) {
            const float b = float(j) / nv;
            const VV left = mix(v[0], v[3], b), right = mix(v[1], v[2], b);
            for (int i = 0; i <= nu; ++i)
                grid[j * (nu + 1) + i] = mix(left, right, float(i) / nu);
        }
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const VV q[4] = {grid[j * (nu + 1) + i], grid[j * (nu + 1) + i + 1], grid[(j + 1) * (nu + 1) + i + 1],
                                 grid[(j + 1) * (nu + 1) + i]};
                const float lo = std::min({q[0].z, q[1].z, q[2].z, q[3].z});
                const float hi = std::max({q[0].z, q[1].z, q[2].z, q[3].z});
                if (hi < NEAR_D)
                    continue;
                if (lo >= NEAR_D) {
                    emit(q, 4, m, passes, m.opaque);
                } else {
                    VV c[8];
                    const int n = clip_near(q, 4, c, NEAR_D);
                    if (n >= 3)
                        emit(c, n, m, passes, m.opaque);
                }
            }
    }

    // An upright rectangle from (xa, ya) to (xb, yb) between the heights zb
    // and zt. Across it the picture runs from sa to sb; down it from t_top to
    // t_bot, starting again wherever that passes a whole number.
    void vface(float xa, float ya, float xb, float yb, float zb, float zt, float sa, float sb, float t_top,
               float t_bot, const Mat &m) {
        if (zt <= zb || t_bot <= t_top)
            return;
        const int k0 = int(std::floor(t_top + 1e-4f)), k1 = int(std::ceil(t_bot - 1e-4f)) - 1;
        for (int k = k0; k <= k1; ++k) {
            const float T0 = std::max(t_top, float(k)), T1 = std::min(t_bot, float(k + 1));
            const float za = zt - (T0 - t_top) / (t_bot - t_top) * (zt - zb);
            const float zc = zt - (T1 - t_top) / (t_bot - t_top) * (zt - zb);
            const float P[4][3] = {{xa, ya, za}, {xb, yb, za}, {xb, yb, zc}, {xa, ya, zc}};
            const float ST[4][2] = {{sa, T0 - k}, {sb, T0 - k}, {sb, T1 - k}, {sa, T1 - k}};
            quad(P, ST, m);
        }
    }

    // a level rectangle at height z
    void hface(float x0, float y0, float x1, float y1, float z, float s0, float t0, float s1, float t1, const Mat &m) {
        const float P[4][3] = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
        const float ST[4][2] = {{s0, t0}, {s1, t0}, {s1, t1}, {s0, t1}};
        quad(P, ST, m);
    }

    Mat opaque(uint16_t tid, float shade, bool lit) {
        Mat m;
        m.base = g.texRegion(scene, tid, 0);
        m.mip = m.base.ok;
        m.shade = uint8_t(shade * 255.0f + 0.5f);
        m.lit = lit;
        return m;
    }

    Mat cutout(uint16_t tid, float shade, bool lit) {
        Mat m;
        m.base = g.texRegion(scene, tid, 1);
        m.sil = g.texRegion(scene, tid, 2);
        m.mip = m.base.ok && m.sil.ok;
        m.shade = uint8_t(shade * 255.0f + 0.5f);
        m.lit = lit;
        m.opaque = false;
        return m;
    }

    // where a picture starts and ends down a face of height h: one picture
    // over the whole face, or repeating like masonry with a whole one at the bottom
    static void rows(const Tex &tex, float h, float &t_top, float &t_bot) {
        if (tex.stretch) {
            t_top = 0;
            t_bot = 1;
        } else {
            t_bot = std::ceil(h - 1e-4f);
            t_top = t_bot - h;
        }
    }

    bool boxVisible(float x0, float y0, float z0, float x1, float y1, float z1) const {
        VV v[8];
        for (int i = 0; i < 8; ++i)
            tv((i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0, v[i]);
        return !offscreen(v, 8);
    }

    const Cell *at(int k, int x, int y) const {
        if (k < 0 || k >= nz || !cells[k] || x < 0 || y < 0 || x >= map_w || y >= map_h)
            return nullptr;
        return &cells[k][y * map_w + x];
    }

    // something standing on a tile: a picture that faces the eye
    void bill(const Billboard &b) {
        const Sprite &sp = *b.sprite;
        if (sp.empty())
            return;
        VV c;
        tv(b.x, b.y, b.base, c);
        if (c.z < 0.25f)
            return;
        SpriteEntry *e = g.spriteEntry(b.sprite);
        if (!e)
            return;
        const float wpp = b.scale * THING_SCALE / TEX;   // tile widths per sprite pixel
        const float bh = float(sp.y1 - sp.y0);
        // the sprite's row that is at the ground: what sinks below it is cut off
        const float yb = sp.y1 - b.sink * bh;
        if (yb <= sp.y0)
            return;
        const auto X = [&](float tx) { return c.x + (tx - b.anchor_px) * wpp; };
        const auto Y = [&](float ty) { return c.y + (yb - ty) * wpp; };
        const bool lit = hl.valid() && (hl.unit_id >= 0 ? b.unit_id == hl.unit_id
                                                        : (b.mx == hl.x && b.my == hl.y && b.mz == hl.z));
        {
            Region r = e->img;
            int grow = 0;
            if (b.outline) {
                const Region o = g.outlinedRegion(*e, sp, b.outline);
                if (o.ok) {
                    r = o;
                    grow = 1;
                }
            }
            const float tx0 = float(sp.x0 - grow), tx1 = float(sp.x1 + grow), ty0 = float(sp.y0 - grow);
            const float tb = (yb - ty0) / (sp.y1 - ty0);
            const VV q[4] = {{X(tx0), Y(ty0), c.z, 0, 0}, {X(tx1), Y(ty0), c.z, 1, 0}, {X(tx1), Y(yb), c.z, 1, tb},
                             {X(tx0), Y(yb), c.z, 0, tb}};
            Mat m;
            m.base = r;
            m.opaque = false;
            emit(q, 4, m, PASS_BASE, false);
        }
        {
            // haze and highlight lie over the picture, not over the line around it
            const float tb = (yb - sp.y0) / bh;
            const VV q[4] = {{X(float(sp.x0)), Y(float(sp.y0)), c.z, 0, 0}, {X(float(sp.x1)), Y(float(sp.y0)), c.z, 1, 0},
                             {X(float(sp.x1)), Y(yb), c.z, 1, tb}, {X(float(sp.x0)), Y(yb), c.z, 0, tb}};
            Mat m;
            m.base = e->sil;
            m.sil = e->sil;
            m.opaque = false;
            m.lit = lit;
            emit(q, 4, m, PASS_FOG | PASS_LIT, false);
        }
    }

    // one block of a piece of furniture: the sides that face the eye
    void box(const Box &b, int mx, int my, float z0, bool lit) {
        const float X0 = mx + b.u0, X1 = mx + b.u1, Y0 = my + b.v0, Y1 = my + b.v1, Z0 = z0 + b.z0, Z1 = z0 + b.z1;
        if (X1 <= X0 || Y1 <= Y0 || Z1 <= Z0)
            return;
        const auto side = [&](uint8_t bit, float xa, float ya, float xb, float yb, bool flip, float sa, float sb,
                              float shade) {
            const bool pictured = b.face && (b.face_sides & bit);
            const uint16_t tid = pictured ? b.face : b.side;
            if (!tid)
                return;
            const Tex &tex = scene.tex.get(tid);
            if (tex.stretch) {
                // a cut-out picture (a shop sign) is see-through where it is empty
                const Mat m = pictured ? cutout(tid, 1.0f, lit) : opaque(tid, shade, lit);
                vface(xa, ya, xb, yb, Z0, Z1, flip ? 1.0f : 0.0f, flip ? 0.0f : 1.0f, 0, 1, m);
            } else {
                vface(xa, ya, xb, yb, Z0, Z1, sa, sb, 0, b.z1 - b.z0, opaque(tid, pictured ? 1.0f : shade, lit));
            }
        };
        if (px < X0)
            side(FACE_W, X0, Y0, X0, Y1, false, b.v0, b.v1, 1.0f);
        else if (px > X1)
            side(FACE_E, X1, Y0, X1, Y1, true, b.v0, b.v1, 1.0f);
        if (py < Y0)
            side(FACE_N, X0, Y0, X1, Y0, true, b.u0, b.u1, 0.8f);
        else if (py > Y1)
            side(FACE_S, X0, Y1, X1, Y1, false, b.u0, b.u1, 0.8f);
        const bool top = pe > Z1, under = pe < Z0;
        if (top || under) {
            const uint16_t tid = top ? b.top : b.side;
            if (tid) {
                const Mat m = opaque(tid, top ? 1.0f : 0.6f, lit);
                if (scene.tex.get(tid).stretch)
                    hface(X0, Y0, X1, Y1, top ? Z1 : Z0, 0, 0, 1, 1, m);
                else
                    hface(X0, Y0, X1, Y1, top ? Z1 : Z0, b.u0, b.v0, b.u1, b.v1, m);
            }
        }
    }

    // the furniture and the things standing in one tile, each drawn after
    // whatever of the others it can hide
    void contents(const BoxSet &set, int mx, int my, float z0, bool lit, const BRef *b0, const BRef *b1) {
        struct Item {
            float lo[3], hi[3];
            float dist;
            const Box *box;
            const Billboard *bill;
        };
        constexpr int MAX_ITEMS = 48;
        Item items[MAX_ITEMS];
        int n = 0;
        const float eye[3] = {px, py, pe};
        for (int i = 0; i < set.n && n < MAX_ITEMS; ++i) {
            const Box &b = set.b[i];
            Item &it = items[n++];
            it.lo[0] = mx + b.u0;
            it.hi[0] = mx + b.u1;
            it.lo[1] = my + b.v0;
            it.hi[1] = my + b.v1;
            it.lo[2] = z0 + b.z0;
            it.hi[2] = z0 + b.z1;
            it.box = &b;
            it.bill = nullptr;
        }
        const BRef *rest = b0;
        for (; rest != b1 && n < MAX_ITEMS; ++rest) {
            const Billboard &b = *rest->b;
            const Sprite &sp = *b.sprite;
            Item &it = items[n++];
            it.lo[0] = b.x - 0.03f;
            it.hi[0] = b.x + 0.03f;
            it.lo[1] = b.y - 0.03f;
            it.hi[1] = b.y + 0.03f;
            it.lo[2] = b.base;
            it.hi[2] = b.base + std::max(0.05f, (sp.y1 - sp.y0) * (1 - b.sink) * b.scale * THING_SCALE / TEX);
            it.box = nullptr;
            it.bill = &b;
        }
        for (int i = 0; i < n; ++i) {
            float d = 0;
            for (int a = 0; a < 3; ++a) {
                const float c = 0.5f * (items[i].lo[a] + items[i].hi[a]) - eye[a];
                d += c * c;
            }
            items[i].dist = d;
        }
        // +1: a goes first (b can hide it), -1: b goes first, 0: cannot tell
        const auto rel = [&](const Item &a, const Item &b) {
            for (int ax = 0; ax < 3; ++ax) {
                if (a.hi[ax] <= b.lo[ax] + 1e-4f) {
                    if (eye[ax] >= b.lo[ax] - 1e-4f)
                        return 1;
                    if (eye[ax] <= a.hi[ax] + 1e-4f)
                        return -1;
                } else if (b.hi[ax] <= a.lo[ax] + 1e-4f) {
                    if (eye[ax] >= a.lo[ax] - 1e-4f)
                        return -1;
                    if (eye[ax] <= b.hi[ax] + 1e-4f)
                        return 1;
                }
            }
            // something standing inside a piece of furniture is drawn over it
            if (a.box && b.bill)
                return 1;
            if (a.bill && b.box)
                return -1;
            return 0;
        };
        bool used[MAX_ITEMS] = {};
        for (int step = 0; step < n; ++step) {
            int pick = -1, far = -1;
            for (int i = 0; i < n; ++i) {
                if (used[i])
                    continue;
                if (far < 0 || items[i].dist > items[far].dist)
                    far = i;
                bool free = true;
                for (int j = 0; j < n && free; ++j)
                    if (j != i && !used[j] && rel(items[i], items[j]) < 0)
                        free = false;
                if (free && (pick < 0 || items[i].dist > items[pick].dist + 1e-5f))
                    pick = i;
            }
            if (pick < 0)
                pick = far;
            used[pick] = true;
            if (items[pick].box)
                box(*items[pick].box, mx, my, z0, lit);
            else
                bill(*items[pick].bill);
        }
        for (; rest != b1; ++rest)
            bill(*rest->b);
    }

    // the part of the mark under the pointed-at creature that lies in one tile
    void decalIn(int mx, int my) {
        for (const DecalPoly &d : decal) {
            float ax[12], ay[12], bx[12], by[12];
            int n = d.n;
            for (int i = 0; i < n; ++i) {
                ax[i] = d.x[i];
                ay[i] = d.y[i];
            }
            // cut to the tile, one side after another
            for (int side = 0; side < 4 && n >= 3; ++side) {
                const float lim = side == 0 ? float(mx) : side == 1 ? float(mx + 1) : side == 2 ? float(my) : float(my + 1);
                const auto inside = [&](float x, float y) {
                    return side == 0 ? x >= lim : side == 1 ? x <= lim : side == 2 ? y >= lim : y <= lim;
                };
                int m = 0;
                for (int i = 0; i < n; ++i) {
                    const int j = (i + 1) % n;
                    const bool ia = inside(ax[i], ay[i]), ib = inside(ax[j], ay[j]);
                    if (ia) {
                        bx[m] = ax[i];
                        by[m++] = ay[i];
                    }
                    if (ia != ib) {
                        const float va = side < 2 ? ax[i] : ay[i], vb = side < 2 ? ax[j] : ay[j];
                        const float k = (lim - va) / (vb - va);
                        bx[m] = ax[i] + (ax[j] - ax[i]) * k;
                        by[m++] = ay[i] + (ay[j] - ay[i]) * k;
                    }
                }
                n = m;
                std::copy(bx, bx + n, ax);
                std::copy(by, by + n, ay);
            }
            if (n < 3)
                continue;
            VV v[12];
            for (int i = 0; i < n; ++i) {
                tv(ax[i], ay[i], decal_h, v[i]);
                v[i].s = v[i].t = 0;
            }
            SDL_Color col = decal_col;
            col.a = d.alpha;
            plain(v, n, col);
        }
    }

    // A faint disc on the ground around the feet of the creature the pointer
    // is on, in its colour, and an arrow towards the tile it steps to next.
    void makeDecal(const Billboard &b) {
        constexpr float R = 0.36f, RIM = 0.29f, A1 = R + 0.42f;
        constexpr int N = 20;
        decal_on = true;
        decal_cx = int(std::floor(b.x));
        decal_cy = int(std::floor(b.y));
        decal_z = b.mz;
        decal_h = b.base + 0.012f;
        decal_col = SDL_Color{Uint8(px_r(b.ring)), Uint8(px_g(b.ring)), Uint8(px_b(b.ring)), 255};
        for (int i = 0; i < N; ++i) {
            const float a0 = 6.2831853f * i / N, a1 = 6.2831853f * (i + 1) / N;
            const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            DecalPoly in{};
            in.n = 3;
            in.alpha = 46;
            in.x[0] = b.x;
            in.y[0] = b.y;
            in.x[1] = b.x + c0 * RIM;
            in.y[1] = b.y + s0 * RIM;
            in.x[2] = b.x + c1 * RIM;
            in.y[2] = b.y + s1 * RIM;
            decal.push_back(in);
            DecalPoly rim{};
            rim.n = 4;
            rim.alpha = 120;
            rim.x[0] = b.x + c0 * RIM;
            rim.y[0] = b.y + s0 * RIM;
            rim.x[1] = b.x + c0 * R;
            rim.y[1] = b.y + s0 * R;
            rim.x[2] = b.x + c1 * R;
            rim.y[2] = b.y + s1 * R;
            rim.x[3] = b.x + c1 * RIM;
            rim.y[3] = b.y + s1 * RIM;
            decal.push_back(rim);
        }
        float dx = b.move_dx, dy = b.move_dy;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0)
            return;
        dx /= len;
        dy /= len;
        const float nx = -dy, ny = dx;
        const auto at_ = [&](float fw, float lat, float &x, float &y) {
            x = b.x + dx * fw + nx * lat;
            y = b.y + dy * fw + ny * lat;
        };
        DecalPoly shaft{};
        shaft.n = 4;
        shaft.alpha = 120;
        at_(R, -0.05f, shaft.x[0], shaft.y[0]);
        at_(R + 0.16f, -0.05f, shaft.x[1], shaft.y[1]);
        at_(R + 0.16f, 0.05f, shaft.x[2], shaft.y[2]);
        at_(R, 0.05f, shaft.x[3], shaft.y[3]);
        decal.push_back(shaft);
        DecalPoly head{};
        head.n = 3;
        head.alpha = 120;
        const float hw = (A1 - (R + 0.16f)) * 0.6f;
        at_(R + 0.16f, -hw, head.x[0], head.y[0]);
        at_(A1, 0, head.x[1], head.y[1]);
        at_(R + 0.16f, hw, head.x[2], head.y[2]);
        decal.push_back(head);
    }

    // everything in one tile of one level, the farthest of it first
    // (far: the tile's column is tall, so it is worth checking that this part of it is on the screen)
    void cell(int mx, int my, int mz, bool grid, bool far) {
        static const Cell EMPTY;
        const int k = mz - zlo;
        const int ci = my * map_w + mx;
        const Cell &c = (grid && cells[k]) ? cells[k][ci] : EMPTY;
        const BRef *b0 = nullptr, *b1 = nullptr;
        {
            const int col = (my - wy0) * ww + (mx - wx0);
            if (g.bill_lo[col] <= mz && g.bill_hi[col] >= mz) {
                const int key = k * ww * wh + col;
                const BRef *begin = g.brefs.data(), *end = begin + g.brefs.size();
                b0 = std::lower_bound(begin, end, key, [](const BRef &r, int kk) { return r.key < kk; });
                b1 = b0;
                while (b1 != end && b1->key == key)
                    ++b1;
            }
        }
        const bool roofed = roof && (c.flags & CELL_ROOFED);
        if (!(c.solid || c.half || c.flr || c.top || c.frame || c.box || c.ramp || roofed) && b0 == b1)
            return;
        const float z0 = mz * LEVEL_H, z1 = z0 + LEVEL_H;
        const float QH = LEVEL_H / 16;
        const bool lit = hl.unit_id < 0 && mx == hl.x && my == hl.y && mz == hl.z;
        const Cell *below = at(k - 1, mx, my), *above = at(k + 1, mx, my);
        const Cell *nW = at(k, mx - 1, my), *nE = at(k, mx + 1, my), *nN = at(k, mx, my - 1), *nS = at(k, mx, my + 1);
        const float X0 = float(mx), X1 = float(mx + 1), Y0 = float(my), Y1 = float(my + 1);
        // the sides of the tile that face the eye. Across a side the picture
        // reads the same way round from either side of the wall.
        const auto walls = [&](const Mat &mxf, const Mat &myf, float zb, float zt, float t_top, float t_bot,
                               const auto &hidden) {
            if (px < X0 && !hidden(nW))
                vface(X0, Y0, X0, Y1, zb, zt, 0, 1, t_top, t_bot, mxf);
            else if (px > X1 && !hidden(nE))
                vface(X1, Y0, X1, Y1, zb, zt, 1, 0, t_top, t_bot, mxf);
            if (py < Y0 && !hidden(nN))
                vface(X0, Y0, X1, Y0, zb, zt, 1, 0, t_top, t_bot, myf);
            else if (py > Y1 && !hidden(nS))
                vface(X0, Y1, X1, Y1, zb, zt, 0, 1, t_top, t_bot, myf);
        };

        if (c.solid) {
            // most blocks are buried among others
            const bool shows = (px < X0 && !(nW && nW->solid)) || (px > X1 && !(nE && nE->solid)) ||
                               (py < Y0 && !(nN && nN->solid)) || (py > Y1 && !(nS && nS->solid)) ||
                               (pe > z1 && !(above && (above->solid || above->flr || above->half))) ||
                               (pe < z0 && below && !below->solid);
            if (!shows && b0 == b1)
                return;
            if (far && !boxVisible(X0 - 0.5f, Y0 - 0.5f, z0, X1 + 0.5f, Y1 + 0.5f, z1 + 1.0f))
                return;
            float tt, tb;
            rows(scene.tex.get(c.solid), LEVEL_H, tt, tb);
            walls(opaque(c.solid, 1.0f, lit), opaque(c.solid, 0.82f, lit), z0, z1, tt, tb,
                  [](const Cell *n) { return n && n->solid; });
            // the top of a block that has no known floor above it
            if (pe > z1 && !(above && (above->solid || above->flr || above->half)))
                hface(X0, Y0, X1, Y1, z1, 0, 0, 1, 1, opaque(c.solid, 1.0f, lit));
            // the underside of an overhang (ceilings all use the grey roof texture)
            if (pe < z0 && below && !below->solid)
                hface(X0, Y0, X1, Y1, z0, 0, 0, 1, 1, opaque(roof ? roof : c.solid, 0.7f, lit));
            for (const BRef *p = b0; p != b1; ++p)
                bill(*p->b);
            return;
        }

        if (far && !boxVisible(X0 - 0.5f, Y0 - 0.5f, z0, X1 + 0.5f, Y1 + 0.5f, z1 + 1.0f))
            return;
        // a roof over the tile with nothing known above it: the farthest thing when looking up
        if (roofed && pe < z1 && !(above && (above->flr || above->solid)))
            hface(X0, Y0, X1, Y1, z1, 0, 0, 1, 1, opaque(roof, 0.7f, lit));
        // the floor from above: the farthest thing when looking down
        if (c.flr && pe > z0)
            hface(X0, Y0, X1, Y1, z0, 0, 0, 1, 1, opaque(c.flr, 1.0f, lit));

        if (c.ramp) {
            // a slope: its surface in small pieces, the farthest first, then
            // the upright sides that face the eye
            const uint16_t top = c.top ? c.top : c.half;
            if (top) {
                constexpr int N = 4;
                const Mat m = opaque(top, 1.0f, lit);
                int ou[N], ov[N];
                for (int i = 0; i < N; ++i)
                    ou[i] = ov[i] = i;
                const float cu = (px - mx) * N - 0.5f, cv = (py - my) * N - 0.5f;
                std::sort(ou, ou + N, [&](int a, int b) { return std::abs(a - cu) > std::abs(b - cu); });
                std::sort(ov, ov + N, [&](int a, int b) { return std::abs(a - cv) > std::abs(b - cv); });
                for (int jj = 0; jj < N; ++jj)
                    for (int ii = 0; ii < N; ++ii) {
                        const int i = ou[ii], j = ov[jj];
                        const float u0 = float(i) / N, u1 = float(i + 1) / N, v0 = float(j) / N, v1 = float(j + 1) / N;
                        const float h00 = z0 + ramp_height(c.ramp, u0, v0) * LEVEL_H;
                        const float h10 = z0 + ramp_height(c.ramp, u1, v0) * LEVEL_H;
                        const float h11 = z0 + ramp_height(c.ramp, u1, v1) * LEVEL_H;
                        const float h01 = z0 + ramp_height(c.ramp, u0, v1) * LEVEL_H;
                        // only its upper side: the eye must be above the piece's
                        // surface (which is not flat, so either of its two halves will do)
                        {
                            const float du = u1 - u0, dv = v1 - v0;
                            const float ex = px - (mx + u0), ey = py - (my + v0), ez = pe - h00;
                            const float a = -(h10 - h00) * dv * ex - du * (h01 - h00) * ey + du * dv * ez;
                            const float fx_ = px - (mx + u1), fy_ = py - (my + v1), fz_ = pe - h11;
                            const float b = -(h11 - h01) * dv * fx_ - du * (h11 - h10) * fy_ + du * dv * fz_;
                            if (a <= 0 && b <= 0)
                                continue;
                        }
                        const float P[4][3] = {{mx + u0, my + v0, h00}, {mx + u1, my + v0, h10}, {mx + u1, my + v1, h11},
                                               {mx + u0, my + v1, h01}};
                        const float ST[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                        quad(P, ST, m);
                    }
            }
            if (c.half) {
                // each side in two halves: a slope between two walls dips in the middle
                const auto side = [&](const Cell *n, float xa, float ya, float xb, float yb, float sa, float sb,
                                      float shade) {
                    if (n && n->solid)
                        return;
                    const Mat m = opaque(c.half, shade, lit);
                    for (int h = 0; h < 2; ++h) {
                        const float k0 = h * 0.5f, k1 = k0 + 0.5f;
                        const float x0 = xa + (xb - xa) * k0, y0 = ya + (yb - ya) * k0;
                        const float x1 = xa + (xb - xa) * k1, y1 = ya + (yb - ya) * k1;
                        const float h0 = ramp_height(c.ramp, x0 - mx, y0 - my) * LEVEL_H;
                        const float h1 = ramp_height(c.ramp, x1 - mx, y1 - my) * LEVEL_H;
                        if (h0 < 0.02f && h1 < 0.02f)
                            continue;
                        const float s0 = sa + (sb - sa) * k0, s1 = sa + (sb - sa) * k1;
                        const float P[4][3] = {{x0, y0, z0 + h0}, {x1, y1, z0 + h1}, {x1, y1, z0}, {x0, y0, z0}};
                        const float ST[4][2] = {{s0, 1 - h0 / LEVEL_H}, {s1, 1 - h1 / LEVEL_H}, {s1, 1}, {s0, 1}};
                        quad(P, ST, m);
                    }
                };
                if (px < X0)
                    side(nW, X0, Y0, X0, Y1, 0, 1, 1.0f);
                else if (px > X1)
                    side(nE, X1, Y0, X1, Y1, 1, 0, 1.0f);
                if (py < Y0)
                    side(nN, X0, Y0, X1, Y0, 1, 0, 0.82f);
                else if (py > Y1)
                    side(nS, X0, Y1, X1, Y1, 0, 1, 0.82f);
            }
        } else if (c.half) {
            // a low block, such as a fortification
            const float hq = z0 + c.topq * QH;
            float tt, tb;
            rows(scene.tex.get(c.half), hq - z0, tt, tb);
            const uint8_t topq = c.topq;
            walls(opaque(c.half, 1.0f, lit), opaque(c.half, 0.82f, lit), z0, hq, tt, tb, [topq](const Cell *n) {
                return n && (n->solid || (n->half && !n->ramp && n->topq >= topq));
            });
            if (pe > hq)
                hface(X0, Y0, X1, Y1, hq, 0, 0, 1, 1, opaque(c.top ? c.top : c.half, 1.0f, lit));
        } else if (c.top) {
            // a raised surface (water)
            const float hq = z0 + c.topq * QH;
            if (pe > hq)
                hface(X0, Y0, X1, Y1, hq, 0, 0, 1, 1, opaque(c.top, 1.0f, lit));
        }

        if (decal_on && mz == decal_z && std::abs(mx - decal_cx) <= 1 && std::abs(my - decal_cy) <= 1)
            decalIn(mx, my);

        if (c.box && c.box < nboxsets && scene.boxsets[c.box].n > 0)
            contents(scene.boxsets[c.box], mx, my, z0, lit, b0, b1);
        else
            for (const BRef *p = b0; p != b1; ++p)
                bill(*p->b);

        // an open door frame: see-through faces on the tile's sides, nearer than what is in it
        if (c.frame)
            walls(cutout(c.frame, 1.0f, lit), cutout(c.frame, 0.82f, lit), z0, z1, 0, 1,
                  [](const Cell *) { return false; });

        // the floor from below, a ceiling: nearer than everything in the tile
        if (c.flr && pe < z0 && !(below && below->solid))
            hface(X0, Y0, X1, Y1, z0, 0, 0, 1, 1, opaque(roof ? roof : c.flr, 0.7f, lit));
    }

    // haze everywhere, and over it the sky where it is open
    void background(bool cam_sky) {
        {
            const int base = int((*vp).size());
            const float xs[4] = {0, W, W, 0}, ys[4] = {0, 0, H, H};
            for (int i = 0; i < 4; ++i) {
                SDL_Vertex v;
                v.position.x = xs[i];
                v.position.y = ys[i];
                v.color = fogc;
                v.tex_coord.x = g.white.x * g.inv;
                v.tex_coord.y = g.white.y * g.inv;
                (*vp).push_back(v);
            }
            fan(base, 4);
        }
        // which ways the sky can be seen: everywhere under the open sky,
        // else only where the view runs out into the open
        bool sky[SKY_SECTORS];
        bool any = cam_sky;
        for (int s = 0; s < SKY_SECTORS; ++s) {
            sky[s] = cam_sky;
            if (cam_sky || map_w <= 0)
                continue;
            const float az = (s + 0.5f) * 6.2831853f / SKY_SECTORS;
            const float dx = std::sin(az), dy = -std::cos(az);
            for (float t = 0.5f; t < max_d && !sky[s]; t += 0.7f) {
                const int x = int(std::floor(px + dx * t)), y = int(std::floor(py + dy * t));
                if (x < 0 || y < 0 || x >= map_w || y >= map_h)
                    break;
                const int ci = y * map_w + x;
                for (int k = 0; k < nz; ++k)
                    if (cells[k] && (cells[k][ci].flags & CELL_SKY)) {
                        sky[s] = true;
                        break;
                    }
            }
            any = any || sky[s];
        }
        if (!any)
            return;
        const float d = opt.daylight;
        const float zen[3] = {4 + (52 - 4) * d, 6 + (104 - 6) * d, 18 + (188 - 18) * d};
        const float hor[3] = {18 + (158 - 18) * d, 22 + (196 - 22) * d, 44 + (226 - 44) * d};
        static const float RINGS[] = {-0.02f, 0.05f, 0.12f, 0.2f, 0.3f, 0.4545f, 0.75f, 1.0f};
        constexpr int NR = int(sizeof(RINGS) / sizeof(RINGS[0]));
        const auto corner = [&](float az, float se, VV &o) {
            const float ce = std::sqrt(std::max(0.0f, 1 - se * se));
            const float wx = std::sin(az) * ce, wy = -std::cos(az) * ce, wz = se;
            o.x = wx * rx + wy * ry;
            o.y = wx * ux + wy * uy + wz * uz;
            o.z = wx * fx + wy * fy + wz * fz;
            o.s = std::clamp(se * 2.2f, 0.0f, 1.0f);
            o.t = 0;
        };
        for (int s = 0; s < SKY_SECTORS; ++s) {
            if (!sky[s])
                continue;
            const float a0 = s * 6.2831853f / SKY_SECTORS, a1 = (s + 1) * 6.2831853f / SKY_SECTORS;
            for (int r = 0; r + 1 < NR; ++r) {
                VV q[4];
                corner(a0, RINGS[r + 1], q[0]);
                corner(a1, RINGS[r + 1], q[1]);
                corner(a1, RINGS[r], q[2]);
                corner(a0, RINGS[r], q[3]);
                VV c[8];
                const int n = clip_near(q, 4, c, 0.02f);
                if (n < 3)
                    continue;
                const int base = int((*vp).size());
                for (int i = 0; i < n; ++i) {
                    const float k = S / c[i].z;
                    SDL_Vertex v;
                    v.position.x = cx + c[i].x * k;
                    v.position.y = cy - c[i].y * k;
                    const float f = c[i].s;
                    v.color = SDL_Color{Uint8(hor[0] + (zen[0] - hor[0]) * f + 0.5f),
                                        Uint8(hor[1] + (zen[1] - hor[1]) * f + 0.5f),
                                        Uint8(hor[2] + (zen[2] - hor[2]) * f + 0.5f), 255};
                    v.tex_coord.x = g.white.x * g.inv;
                    v.tex_coord.y = g.white.y * g.inv;
                    (*vp).push_back(v);
                }
                fan(base, n);
            }
        }
    }

    void run() {
        const auto t_start = std::chrono::steady_clock::now();
        map_w = scene.w;
        map_h = scene.h;
        if (map_w <= 0 || map_h <= 0 || scene.col_lo.size() < size_t(map_w) * map_h ||
            scene.col_hi.size() < size_t(map_w) * map_h || scene.col_box.size() < size_t(map_w) * map_h)
            map_w = map_h = 0;
        zlo = std::max(0, opt.zlo);
        zhi = std::min({opt.zhi, scene.d - 1, zlo + 63});
        nz = std::max(0, zhi - zlo + 1);
        for (int k = 0; k < nz; ++k)
            if (const Level *L = scene.level(zlo + k))
                if (L->cells.size() >= size_t(map_w) * map_h)
                    cells[k] = L->cells.data();
        roof = scene.tex.existing(Special::Roof);
        nboxsets = int(scene.boxsets.size());

        const int ccx = int(std::floor(px)), ccy = int(std::floor(py)), ccz = int(std::floor(pe / LEVEL_H));
        bool cam_sky = false;
        if (const Cell *c = at(ccz - zlo, ccx, ccy))
            cam_sky = (c->flags & CELL_SKY) != 0;
        background(cam_sky);
        if (map_w <= 0 || nz <= 0)
            return;

        const int R = int(max_d) + 2;
        wx0 = std::max(0, ccx - R);
        wy0 = std::max(0, ccy - R);
        const int wx1 = std::min(map_w - 1, ccx + R), wy1 = std::min(map_h - 1, ccy + R);
        if (wx1 < wx0 || wy1 < wy0)
            return;
        ww = wx1 - wx0 + 1;
        wh = wy1 - wy0 + 1;

        // Far to near along each axis by itself: what can hide a tile is
        // never farther than it along any of the three.
        const auto order = [](std::vector<int> &o, int lo, int hi, int at_) {
            o.clear();
            for (int i = lo; i <= hi; ++i)
                o.push_back(i);
            std::sort(o.begin(), o.end(), [at_](int a, int b) { return std::abs(a - at_) > std::abs(b - at_); });
        };
        order(g.order_x, wx0, wx1, ccx);
        order(g.order_y, wy0, wy1, ccy);
        order(g.order_z, zlo, zhi, ccz);

        // things standing on tiles, by tile
        g.brefs.clear();
        g.bill_lo.assign(size_t(ww) * wh, int16_t(32767));
        g.bill_hi.assign(size_t(ww) * wh, int16_t(-32768));
        const Billboard *marked = nullptr;
        const auto consider = [&](const Billboard &b) {
            if (!b.sprite || b.mz < zlo || b.mz > zhi)
                return;
            const int x = int(std::floor(b.x)), y = int(std::floor(b.y));
            if (x < wx0 || y < wy0 || x > wx1 || y > wy1)
                return;
            const float dx = b.x - px, dy = b.y - py;
            if (dx * dx + dy * dy > max_d * max_d)
                return;
            const float depth = dx * fx + dy * fy + (b.base - pe) * fz;
            if (depth < 0.25f)
                return;
            const int col = (y - wy0) * ww + (x - wx0);
            g.brefs.push_back(BRef{(b.mz - zlo) * ww * wh + col, depth, &b});
            g.bill_lo[col] = std::min(g.bill_lo[col], int16_t(b.mz));
            g.bill_hi[col] = std::max(g.bill_hi[col], int16_t(b.mz));
            if (b.ring && hl.unit_id >= 0 && b.unit_id == hl.unit_id && pe - b.base > 0.01f)
                marked = &b;
        };
        for (int z = zlo; z <= zhi; ++z)
            if (const Level *L = scene.level(z))
                for (const Billboard &b : L->bills)
                    consider(b);
        for (const Billboard &b : scene.units)
            consider(b);
        std::stable_sort(g.brefs.begin(), g.brefs.end(), [](const BRef &a, const BRef &b) {
            if (a.key != b.key)
                return a.key < b.key;
            if (a.depth != b.depth)
                return a.depth > b.depth;
            return a.b->order < b.b->order;
        });
        if (marked)
            makeDecal(*marked);
        const auto t_prep = std::chrono::steady_clock::now();
        g.n_bills = int(g.brefs.size());

        // Rows of the map are worked out side by side, each into its own
        // list, and the lists are joined in the order of the rows.
        const int nrows = int(g.order_y.size());
        if (int(g.rows.size()) < nrows)
            g.rows.resize(nrows);
        std::atomic<int> next{0}, cols{0}, tiles{0};
        g.pool.run([&](int) {
            Builder w = *this;
            for (;;) {
                const int r = next.fetch_add(1);
                if (r >= nrows)
                    break;
                RowOut &o = g.rows[r];
                o.v.clear();
                o.i.clear();
                w.vp = &o.v;
                w.ip = &o.i;
                w.row(g.order_y[r]);
            }
            cols += w.n_cols;
            tiles += w.n_cells;
        });
        for (int r = 0; r < nrows; ++r) {
            const RowOut &o = g.rows[r];
            const int base = int(g.verts.size());
            g.verts.insert(g.verts.end(), o.v.begin(), o.v.end());
            const size_t at_ = g.idx.size();
            g.idx.resize(at_ + o.i.size());
            for (size_t i = 0; i < o.i.size(); ++i)
                g.idx[at_ + i] = o.i[i] + base;
        }
        g.n_cols = cols;
        g.n_cells = tiles;
        const auto t_done = std::chrono::steady_clock::now();
        g.us_prep = int(std::chrono::duration_cast<std::chrono::microseconds>(t_prep - t_start).count());
        g.us_cells = int(std::chrono::duration_cast<std::chrono::microseconds>(t_done - t_prep).count());
    }

    // one west-east row of the map
    void row(int y) {

        const float reach2 = (max_d + 1) * (max_d + 1);
        {
            for (const int x : g.order_x) {
                const int ci = y * map_w + x, col = (y - wy0) * ww + (x - wx0);
                const int glo = std::max<int>(scene.col_lo[ci], zlo), ghi = std::min<int>(scene.col_hi[ci], zhi);
                int lo = glo, hi = ghi;
                if (g.bill_hi[col] >= g.bill_lo[col]) {
                    if (lo > hi) {
                        lo = g.bill_lo[col];
                        hi = g.bill_hi[col];
                    } else {
                        lo = std::min<int>(lo, g.bill_lo[col]);
                        hi = std::max<int>(hi, g.bill_hi[col]);
                    }
                }
                if (lo > hi)
                    continue;
                const float ddx = x + 0.5f - px, ddy = y + 0.5f - py;
                if (ddx * ddx + ddy * ddy > reach2)
                    continue;
                if (!boxVisible(x - 0.5f, y - 0.5f, lo * LEVEL_H, x + 1.5f, y + 1.5f, (hi + 1) * LEVEL_H + 1.0f))
                    continue;
                ++n_cols;
                const bool tall = hi - lo >= 2;
                for (const int z : g.order_z) {
                    if (z < lo || z > hi)
                        continue;
                    ++n_cells;
                    cell(x, y, z, z >= glo && z <= ghi, tall);
                }
            }
        }
    }
};

} // namespace

GpuRenderer::GpuRenderer(const GpuApi &api) : d_(std::make_unique<Impl>()) {
    d_->api = api;
}

GpuRenderer::~GpuRenderer() = default;

void GpuRenderer::dropTextures() {
    Impl &g = *d_;
    if (g.tex && g.api.destroy_texture)
        g.api.destroy_texture(g.tex);
    g.tex = nullptr;
    g.owner = nullptr;
    g.valid = false;
}

bool GpuRenderer::build(SDL_Renderer *sdl, const Scene &scene, const Camera &cam, const RenderOptions &opt, int W,
                        int H) {
    Impl &g = *d_;
    if (!g.api.ok() || !g.ensureTexture(sdl))
        return false;
    ++g.frame;
    if (scene.tex.generation() != g.tex_gen || g.want_reset) {
        g.resetAtlas();
        g.tex_gen = scene.tex.generation();
    }
    if (g.frame % 512 == 0)
        g.prune();
    g.verts.clear();
    g.idx.clear();
    g.cam = cam;
    g.opt = opt;
    g.W = W;
    g.H = H;
    g.valid = true;
    Builder b(g, scene, cam, opt, W, H);
    b.run();
    g.last_tris = int(g.idx.size() / 3);
    return true;
}

bool GpuRenderer::draw(SDL_Renderer *sdl) {
    Impl &g = *d_;
    if (!g.tex || g.owner != sdl || g.idx.empty())
        return true;
    bool ok = true;
    const auto t0 = std::chrono::steady_clock::now();
    g.upload();
    // in parts: some back ends take only so many triangles at once
    constexpr size_t CHUNK = 60000;
    for (size_t at = 0; at < g.idx.size(); at += CHUNK) {
        const size_t n = std::min(CHUNK, g.idx.size() - at);
        if (g.api.render_geometry(sdl, g.tex, g.verts.data(), int(g.verts.size()), g.idx.data() + at, int(n)) != 0)
            ok = false;
    }
    g.us_draw = int(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    return ok;
}

Pick GpuRenderer::pick(const Scene &scene, float fx, float fy) const {
    const Impl &g = *d_;
    if (!g.valid || fx < 0 || fy < 0 || fx >= 1 || fy >= 1)
        return Pick();
    return pick_ray(scene, g.cam, g.opt, g.W, g.H, fx * g.W, fy * g.H);
}

bool GpuRenderer::findCreature(const Scene &scene, float &fx, float &fy) const {
    const Impl &g = *d_;
    if (!g.valid)
        return false;
    const View3D v = View3D::of(g.cam, g.W, g.H);
    float best = 1e30f;
    bool found = false;
    for (const Billboard &b : scene.units) {
        if (!b.sprite || b.unit_id < 0)
            continue;
        const Sprite &sp = *b.sprite;
        float sx, sy, d;
        if (!v.project(b.x, b.y, b.base, sx, sy, d) || d < 0.25f || d >= best)
            continue;
        // the middle of its picture
        const float pps = v.S / d * b.scale * THING_SCALE / TEX;
        const float mx = sx + ((sp.x0 + sp.x1) * 0.5f - b.anchor_px) * pps;
        const float my = sy - (sp.y1 - sp.y0) * (0.5f - b.sink) * pps;
        if (mx < 2 || my < 2 || mx >= g.W - 2 || my >= g.H - 2)
            continue;
        // a few points of it, in case the middle is empty or hidden
        for (const float oy : {0.0f, -0.2f, 0.2f}) {
            const float ty = my + oy * (sp.y1 - sp.y0) * pps;
            if (pick_ray(scene, g.cam, g.opt, g.W, g.H, mx, ty).unit_id == b.unit_id) {
                best = d;
                fx = mx / g.W;
                fy = ty / g.H;
                found = true;
                break;
            }
        }
    }
    return found;
}

int GpuRenderer::triangles() const {
    return d_->last_tris;
}

int GpuRenderer::atlasPercent() const {
    const Impl &g = *d_;
    return g.size > 0 ? int(100.0 * (double(g.shelf_y) + g.shelf_h) / g.size) : 0;
}

std::string GpuRenderer::timings() const {
    const Impl &g = *d_;
    return "prep " + std::to_string(g.us_prep) + " us, tiles " + std::to_string(g.us_cells) + " us, hand-over " +
           std::to_string(g.us_draw) + " us, " +
           std::to_string(g.n_cols) + " columns, " + std::to_string(g.n_cells) + " tiles, " +
           std::to_string(g.n_bills) + " things";
}

int GpuRenderer::atlasSize() const {
    return d_->size;
}

} // namespace fpv
