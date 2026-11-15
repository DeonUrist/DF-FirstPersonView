#include "render3d.h"

#include <algorithm>
#include <cmath>

namespace fpv {

namespace {

constexpr float FAR_T = 1e30f;

inline float frac(float v) {
    return v - std::floor(v);
}

} // namespace

View3D View3D::of(const Camera &cam, int W, int H) {
    View3D v;
    v.px = cam.x;
    v.py = cam.y;
    v.eye = cam.eye;
    const float P = std::tan(cam.fov * 3.14159265f / 360.0f);
    v.S = W / (2 * P);
    v.cx = W / 2.0f;
    v.cy = H / 2.0f + cam.pitch;
    const float sa = std::sin(cam.ang), ca = std::cos(cam.ang);
    const float st = std::sin(cam.tilt), ct = std::cos(cam.tilt);
    v.fx = sa * ct;
    v.fy = -ca * ct;
    v.fz = st;
    v.rx = ca;
    v.ry = sa;
    v.ux = -sa * st;
    v.uy = ca * st;
    v.uz = ct;
    return v;
}

bool View3D::project(float x, float y, float z, float &sx, float &sy, float &depth) const {
    const float dx = x - px, dy = y - py, dz = z - eye;
    depth = dx * fx + dy * fy + dz * fz;
    if (depth < 0.05f)
        return false;
    const float lat = dx * rx + dy * ry, up = dx * ux + dy * uy + dz * uz;
    sx = cx + lat * S / depth;
    sy = cy - up * S / depth;
    return true;
}

Pick pick_ray(const Scene &scene, const Camera &cam, const RenderOptions &opt, int W, int H, float psx, float psy) {
    Pick result;
    const View3D v = View3D::of(cam, W, H);
    const int zlo = std::max(0, opt.zlo);
    const int zhi = std::min(opt.zhi, scene.d - 1);
    const int nz = std::max(0, zhi - zlo + 1);
    std::vector<const Cell *> cells(size_t(nz), nullptr);
    for (int k = 0; k < nz; ++k)
        if (const Level *L = scene.level(zlo + k))
            cells[k] = L->cells.data();
    const int map_w = scene.w, map_h = scene.h;
    if (map_w <= 0 || map_h <= 0 || scene.col_lo.size() < size_t(map_w) * map_h)
        return result;
    const float view_dist = std::max(1.0f, opt.view_distance), max_d = view_dist + 4;
    const uint16_t roof = scene.tex.existing(Special::Roof);
    const float QH = LEVEL_H / 16;
    const int nboxsets = int(scene.boxsets.size());
    float hit_t = FAR_T;

    // The ray is stepped through the map tile by tile and level by level
    // (each level is LEVEL_H tall). In each tile it passes through,
    // everything in that tile is tested and the nearest hit wins.
    const auto trace = [&]() {
        const float a = (psx - v.cx) / v.S, bb = (v.cy - psy) / v.S;
        // forward part 1: the ray's parameter t is the distance along the view
        const float Dx = v.fx + v.rx * a + v.ux * bb, Dy = v.fy + v.ry * a + v.uy * bb, Dz = v.fz + v.uz * bb;
        const float Ox = v.px, Oy = v.py, Oz = v.eye;

        int mx = int(std::floor(Ox)), my = int(std::floor(Oy)), mz = int(std::floor(Oz / LEVEL_H));
        const int stx = Dx > 0 ? 1 : -1, sty = Dy > 0 ? 1 : -1, stz = Dz > 0 ? 1 : -1;
        const float tdx = Dx != 0 ? std::abs(1 / Dx) : FAR_T, tdy = Dy != 0 ? std::abs(1 / Dy) : FAR_T;
        const float tdz = Dz != 0 ? std::abs(LEVEL_H / Dz) : FAR_T;
        float tmx = Dx > 0 ? (mx + 1 - Ox) * tdx : Dx < 0 ? (Ox - mx) * tdx : FAR_T;
        float tmy = Dy > 0 ? (my + 1 - Oy) * tdy : Dy < 0 ? (Oy - my) * tdy : FAR_T;
        float tmz = Dz > 0 ? ((mz + 1) * LEVEL_H - Oz) / Dz : Dz < 0 ? (mz * LEVEL_H - Oz) / Dz : FAR_T;

        float t0 = 0;
        int axis = -1;   // the side the ray came in through: 0 x, 1 y, 2 z
        for (int steps = 0; steps < 4096; ++steps) {
            const float t1 = std::min({tmx, tmy, tmz});
            if (mx < 0 || my < 0 || mx >= map_w || my >= map_h)
                break;
            const int k = mz - zlo;
            if ((k < 0 && Dz <= 0) || (k >= nz && Dz >= 0))
                break;
            const int ci = my * map_w + mx;
            const Cell *cl = k >= 0 && k < nz ? cells[k] : nullptr;
            if (cl) {
                const Cell &c = cl[ci];
                if (mz >= scene.col_lo[ci] && mz <= scene.col_hi[ci]) {
                    const float z0 = mz * LEVEL_H, z1 = z0 + LEVEL_H;
                    float best = FAR_T;
                    uint32_t color = 0;
                    float dark = 1;
                    // a vertical face: u across it (0..1), the height of the point
                    // and the face's top and bottom (texture rows run from the top)
                    const auto face_px = [&](const Tex &tex, float u, float pz, float zb, float zt) {
                        const int texx = std::clamp(int(u * 32), 0, 31);
                        int texy;
                        if (tex.stretch) {
                            texy = std::clamp(int((zt - pz) / std::max(zt - zb, 1e-4f) * 32), 0, 31);
                        } else {
                            const float span = (zt - zb) * 32.0f;
                            const float voff = std::ceil(span / 32.0f) * 32.0f - span;
                            texy = int((zt - pz) * 32.0f + voff) & 31;
                        }
                        return tex.px[(texy << 5) + texx];
                    };
                    // across a side face, reading the same way round from either side
                    const auto side_u = [&](float hx, float hy) {
                        if (axis == 0)
                            return Dx < 0 ? 1 - frac(hy) : frac(hy);
                        return Dy > 0 ? 1 - frac(hx) : frac(hx);
                    };
                    const float hx0 = Ox + Dx * t0, hy0 = Oy + Dy * t0, hz0 = Oz + Dz * t0;
                    const auto flat = [&](uint16_t tid, float t, float shade) {
                        if (t >= best || !tid)
                            return;
                        const Tex &tex = scene.tex.get(tid);
                        const float hx = Ox + Dx * t, hy = Oy + Dy * t;
                        const int u = int(hx * 32.0f) & 31, vv = int(hy * 32.0f) & 31;
                        best = t;
                        color = tex.px[(vv << 5) | u];
                        dark = shade;
                    };
                    const auto plane_t = [&](float z) { return Dz != 0 ? (z - Oz) / Dz : -1.0f; };
                    const auto within = [&](float t) { return t > 1e-4f && t >= t0 - 1e-4f && t <= t1 + 1e-4f; };
                    const Cell *below = k > 0 && cells[k - 1] ? &cells[k - 1][ci] : nullptr;
                    const Cell *above = k + 1 < nz && cells[k + 1] ? &cells[k + 1][ci] : nullptr;

                    if (c.solid) {
                        if (t0 > 1e-4f) {
                            if (axis == 2) {
                                if (Dz < 0)
                                    flat(c.top ? c.top : c.solid, t0, 1.0f);
                                else
                                    flat(c.half ? c.half : roof ? roof : c.solid, t0, 0.7f);
                            } else if (axis >= 0) {
                                // a built wall's plain bands at its foot and head
                                const bool band = c.trim && (((c.flags & CELL_FOOT) && hz0 < z0 + TRIM_H) ||
                                                             ((c.flags & CELL_HEAD) && hz0 >= z1 - TRIM_H));
                                const Tex &tex = scene.tex.get(band ? c.trim : c.solid);
                                best = t0;
                                color = face_px(tex, side_u(hx0, hy0), hz0, z0, z1);
                                dark = axis == 1 ? 0.82f : 1.0f;
                            }
                        }
                    } else {
                        // an open door frame on the side the ray came in through
                        if (c.frame && t0 > 1e-4f && (axis == 0 || axis == 1)) {
                            const uint32_t px = face_px(scene.tex.get(c.frame), side_u(hx0, hy0), hz0, z0, z1);
                            if (px && t0 < best) {
                                best = t0;
                                color = px;
                                dark = axis == 1 ? 0.82f : 1.0f;
                            }
                        }
                        // the floor, from above; its underside from below
                        if (c.flr) {
                            const float t = plane_t(z0);
                            if (within(t)) {
                                if (Dz < 0)
                                    flat(c.flr, t, 1.0f);
                                else if (!below || !below->solid)
                                    flat(roof ? roof : c.flr, t, 0.7f);
                            }
                        }
                        // a roof over the tile with nothing known above it
                        if (Dz > 0 && roof && (c.flags & CELL_ROOFED) && !(above && (above->flr || above->solid))) {
                            const float t = plane_t(z1);
                            if (within(t))
                                flat(roof, t, 0.7f);
                        }
                        if (c.ramp) {
                            // a slope: its side where the ray comes in below it, else
                            // the first point along the ray that is under its surface
                            const uint16_t top = c.top ? c.top : c.half, side = c.half ? c.half : c.top;
                            const auto height = [&](float t) {
                                const float hx = Ox + Dx * t, hy = Oy + Dy * t;
                                return z0 + ramp_height(c.ramp, std::clamp(hx - mx, 0.0f, 1.0f),
                                                        std::clamp(hy - my, 0.0f, 1.0f)) * LEVEL_H;
                            };
                            const float hin = height(t0);
                            if (t0 > 1e-4f && (axis == 0 || axis == 1) && hz0 <= hin && side && t0 < best) {
                                best = t0;
                                color = face_px(scene.tex.get(side), side_u(hx0, hy0), hz0, z0, hin);
                                dark = axis == 1 ? 0.82f : 1.0f;
                            } else if (top && Oz > z0) {
                                constexpr int N = 8;
                                float prev = std::max(t0, 1e-4f);
                                for (int i = 1; i <= N; ++i) {
                                    const float t = t0 + (t1 - t0) * i / N;
                                    if (Oz + Dz * t <= height(t)) {
                                        float lo = prev, hi = t;
                                        for (int j = 0; j < 6; ++j) {
                                            const float mid = 0.5f * (lo + hi);
                                            if (Oz + Dz * mid <= height(mid))
                                                hi = mid;
                                            else
                                                lo = mid;
                                        }
                                        flat(top, hi, 1.0f);
                                        break;
                                    }
                                    prev = t;
                                }
                            }
                        } else if (c.half || c.top) {
                            // a low block (fortification) or a raised surface (water)
                            const float hq = z0 + c.topq * QH;
                            if (c.half && t0 > 1e-4f && (axis == 0 || axis == 1) && hz0 <= hq && t0 < best) {
                                best = t0;
                                color = face_px(scene.tex.get(c.half), side_u(hx0, hy0), hz0, z0, hq);
                                dark = axis == 1 ? 0.82f : 1.0f;
                            } else if (Dz < 0 && Oz > hq) {
                                const float t = plane_t(hq);
                                if (within(t))
                                    flat(c.top ? c.top : c.half, t, 1.0f);
                            }
                        }
                        // (water is not hit: the pointer aims through it at the bottom
                        // and at what swims there)
                        // shapes standing inside the tile
                        if (c.box && c.box < nboxsets && scene.col_box[ci]) {
                            const BoxSet &set = scene.boxsets[c.box];
                            for (int i = 0; i < set.n; ++i) {
                                const Box &b = set.b[i];
                                const float lo[3] = {mx + b.u0, my + b.v0, z0 + b.z0};
                                const float hi[3] = {mx + b.u1, my + b.v1, z0 + b.z1};
                                const float O3[3] = {Ox, Oy, Oz}, D3[3] = {Dx, Dy, Dz};
                                float tin = -FAR_T, tout = FAR_T;
                                int ax = -1;
                                bool miss = false;
                                float rnu = 0, rnv = 0;
                                if (b.round) {
                                    // a round piece: its outline across, its top and bottom up and down
                                    float a, z;
                                    miss = !roundClip(b, Ox - mx, Oy - my, Dx, Dy, tin, tout, rnu, rnv);
                                    ax = 0;
                                    if (!miss && Dz != 0) {
                                        a = (lo[2] - Oz) / Dz;
                                        z = (hi[2] - Oz) / Dz;
                                        if (a > z)
                                            std::swap(a, z);
                                        if (a > tin) {
                                            tin = a;
                                            ax = 2;
                                        }
                                        tout = std::min(tout, z);
                                    } else if (!miss && (Oz < lo[2] || Oz > hi[2])) {
                                        miss = true;
                                    }
                                    if (!miss && ax == 0 && rnu == 0 && rnv == 0)
                                        miss = true;   // starts inside it
                                }
                                for (int q = 0; q < 3 && !miss && !b.round; ++q) {
                                    if (D3[q] == 0) {
                                        if (O3[q] < lo[q] || O3[q] > hi[q])
                                            miss = true;
                                        continue;
                                    }
                                    float ta = (lo[q] - O3[q]) / D3[q], tb = (hi[q] - O3[q]) / D3[q];
                                    if (ta > tb)
                                        std::swap(ta, tb);
                                    if (ta > tin) {
                                        tin = ta;
                                        ax = q;
                                    }
                                    tout = std::min(tout, tb);
                                }
                                if (miss || tout <= tin || tin <= 1e-4f || tin >= best || ax < 0)
                                    continue;
                                const float hx = Ox + Dx * tin, hy = Oy + Dy * tin, hz = Oz + Dz * tin;
                                uint32_t px;
                                float shade = 1;
                                if (ax == 2) {
                                    const bool top = Dz < 0;
                                    const Tex &tex = scene.tex.get(top ? b.top : (b.under ? b.under : b.side));
                                    int u, vv;
                                    if (tex.stretch) {
                                        u = std::clamp(int((hx - lo[0]) * 32.0f / std::max(b.u1 - b.u0, 1e-3f)), 0, 31);
                                        vv = std::clamp(int((hy - lo[1]) * 32.0f / std::max(b.v1 - b.v0, 1e-3f)), 0, 31);
                                    } else {
                                        u = int(hx * 32.0f) & 31;
                                        vv = int(hy * 32.0f) & 31;
                                    }
                                    px = tex.px[(vv << 5) | u];
                                    shade = top ? 1.0f : 0.6f;
                                } else if (b.round) {
                                    const Tex &tex = scene.tex.get(b.side);
                                    const float down = std::clamp((hi[2] - hz) / std::max(hi[2] - lo[2], 1e-4f), 0.0f, 0.999f);
                                    const int texx = std::min(31, int(frac(hy * rnu - hx * rnv) * 32));
                                    const int texy = int(down * (b.z1 - b.z0) * 32) & 31;
                                    px = tex.px[(texy << 5) + texx];
                                    shade = 0.8f + 0.2f * std::abs(rnu);
                                } else {
                                    const uint8_t bit = ax == 0 ? (Dx > 0 ? FACE_W : FACE_E) : (Dy > 0 ? FACE_N : FACE_S);
                                    const bool pictured = b.face && (b.face_sides & bit);
                                    const Tex &tex = scene.tex.get(pictured ? b.face : b.side);
                                    const float down = std::clamp((hi[2] - hz) / std::max(hi[2] - lo[2], 1e-4f), 0.0f, 0.999f);
                                    int texx, texy;
                                    if (tex.stretch) {
                                        float across = ax == 1 ? (hx - lo[0]) / std::max(b.u1 - b.u0, 1e-3f)
                                                               : (hy - lo[1]) / std::max(b.v1 - b.v0, 1e-3f);
                                        if (ax == 1 ? Dy > 0 : Dx < 0)
                                            across = 1 - across;
                                        texx = std::clamp(int(across * 32), 0, 31);
                                        texy = int(down * 32);
                                    } else {
                                        texx = std::min(31, int(frac(ax == 0 ? hy : hx) * 32));
                                        texy = int(down * (b.z1 - b.z0) * 32) & 31;
                                    }
                                    px = tex.px[(texy << 5) + texx];
                                    // a cut-out picture (a shop sign): see through where it is empty
                                    if (pictured && tex.stretch && px == 0)
                                        continue;
                                    shade = ax == 1 && !pictured ? 0.8f : 1.0f;
                                }
                                best = tin;
                                color = px;
                                dark = shade;
                            }
                        }
                    }
                    (void)color;
                    (void)dark;
                    if (best < FAR_T) {
                        hit_t = best;
                        result.x = int16_t(mx);
                        result.y = int16_t(my);
                        result.z = int16_t(mz);
                        return;
                    }
                }
            }
            if (t1 > max_d)
                break;
            if (tmx <= tmy && tmx <= tmz) {
                mx += stx;
                tmx += tdx;
                axis = 0;
            } else if (tmy <= tmz) {
                my += sty;
                tmy += tdy;
                axis = 1;
            } else {
                mz += stz;
                tmz += tdz;
                axis = 2;
            }
            t0 = t1;
        }
    };
    trace();

    // things standing on the tiles face the eye; the nearest one the ray goes
    // through an opaque part of (or the line around it) wins
    float best_d = hit_t;
    int best_order = -1;
    const auto test = [&](const Billboard &b) {
        if (!b.sprite || b.mz < zlo || b.mz > zhi)
            return;
        float bsx, bsy, d;
        if (!v.project(b.x, b.y, b.base, bsx, bsy, d) || d < 0.25f || d > max_d || d > best_d)
            return;
        if (d == best_d && int(b.order) < best_order)
            return;
        const Sprite &sp = *b.sprite;
        const float pps = v.S / d * b.scale * THING_SCALE / TEX;
        if (psy >= bsy || pps <= 0)
            return;
        const float left = bsx - b.anchor_px * pps;
        const float topy = bsy + b.sink * (sp.y1 - sp.y0) * pps - sp.y1 * pps;
        const int tx = int(std::floor((psx - left) / pps)), ty = int(std::floor((psy - topy) / pps));
        const auto solid = [&](int x, int y) {
            return x >= sp.x0 && x < sp.x1 && y >= sp.y0 && y < sp.y1 && sp.px[size_t(y) * sp.w + x] != 0;
        };
        if (!solid(tx, ty) &&
            !(b.outline && ty < sp.y1 && (solid(tx - 1, ty) || solid(tx + 1, ty) || solid(tx, ty - 1) || solid(tx, ty + 1))))
            return;
        best_d = d;
        best_order = b.order;
        result.x = b.mx;
        result.y = b.my;
        result.z = b.mz;
        result.unit_id = b.unit_id;
        result.kind = int8_t(b.kind);
        result.texpos = b.texpos;
    };
    for (int z = zlo; z <= zhi; ++z)
        if (const Level *L = scene.level(z))
            for (const Billboard &b : L->bills)
                test(b);
    for (const Billboard &b : scene.units)
        test(b);
    return result;
}

} // namespace fpv
