#include "sight.h"

#include <algorithm>
#include <cmath>

#include "TileTypes.h"
#include "modules/Maps.h"

#include "df/map_block.h"
#include "df/tile_designation.h"

using namespace DFHack;

namespace fpv {

float SightMap::level(float x, float y, int z) const {
    const int k = z - zlo;
    if (k < 0 || k >= nz)
        return -1;
    const float fx = x - 0.5f - x0, fy = y - 0.5f - y0;
    const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
    const float tx = fx - ix, ty = fy - iy;
    const size_t base = size_t(k) * w * h;
    float sw = 0, sv = 0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const int cx = ix + i, cy = iy + j;
            if (cx < 0 || cy < 0 || cx >= w || cy >= h)
                continue;
            const size_t idx = base + size_t(cy) * w + cx;
            if (!has[idx])
                continue;
            const float wt = (i ? tx : 1 - tx) * (j ? ty : 1 - ty) + 1e-4f;
            sw += wt;
            sv += wt * light[idx];
        }
    return sw > 0 ? sv / (sw * 255.0f) : -1.0f;
}

float SightMap::column(float x, float y) const {
    if (!on)
        return 1;
    if (w <= 0 || h <= 0)
        return 0;
    // beyond the part read: as its edge is
    const float fx = std::clamp(x - x0, 0.5f, w - 0.5f) - 0.5f, fy = std::clamp(y - y0, 0.5f, h - 0.5f) - 0.5f;
    const int ix = int(fx), iy = int(fy);
    const float tx = fx - ix, ty = fy - iy;
    float sw = 0, sv = 0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const int cx = std::min(ix + i, w - 1), cy = std::min(iy + j, h - 1);
            const size_t idx = size_t(cy) * w + cx;
            if (!col_has[idx])
                continue;
            const float wt = (i ? tx : 1 - tx) * (j ? ty : 1 - ty) + 1e-4f;
            sw += wt;
            sv += wt * col[idx];
        }
    return sw > 0 ? sv / (sw * 255.0f) : 0.0f;
}

float SightMap::at(float x, float y, float z) const {
    if (!on)
        return 1;
    // a point on a floor or a ceiling belongs to the levels on both sides of it:
    // the brighter is the one it is seen from
    const float zl = z / LEVEL_H;
    const int za = int(std::floor(zl - 0.04f)), zb = int(std::floor(zl + 0.04f));
    float r = level(x, y, za);
    if (zb != za)
        r = std::max(r, level(x, y, zb));
    return r >= 0 ? r : column(x, y);
}

namespace {

// what a tile is, for the light
enum : uint8_t {
    T_NONE = 0,     // unseen and nothing to light (rock, air): says nothing
    T_DARK = 1,     // unseen ground: the dark the seen fades into
    T_SEEN = 2,     // seen, open
    T_SEEN_WALL = 3,
    T_WALL = 8,     // flag: a solid tile, seen or not (the fade does not pass through it)
};

} // namespace

void readSight(SightMap &out, int ex, int ey, int zlo, int zhi, int radius, float day) {
    day = std::clamp(day, 0.0f, 1.0f);
    out.on = true;
    uint32_t mw = 0, mh = 0, md = 0;
    Maps::getTileSize(mw, mh, md);
    zlo = std::max(zlo, 0);
    zhi = std::min(zhi, int(md) - 1);
    const int x0 = std::max(0, ex - radius), y0 = std::max(0, ey - radius);
    const int x1 = std::min(int(mw), ex + radius + 1), y1 = std::min(int(mh), ey + radius + 1);
    out.x0 = x0;
    out.y0 = y0;
    out.w = std::max(0, x1 - x0);
    out.h = std::max(0, y1 - y0);
    out.zlo = zlo;
    out.nz = std::max(0, zhi - zlo + 1);
    const int W = out.w, H = out.h;
    const size_t plane = size_t(W) * H;
    out.light.assign(plane * out.nz, 0);
    out.has.assign(plane * out.nz, 0);
    out.col.assign(plane, 0);
    out.col_has.assign(plane, 0);
    if (!plane || !out.nz)
        return;

    static std::vector<uint8_t> kind, outside, dist;
    static std::vector<std::vector<int>> buckets;
    kind.resize(plane);
    outside.resize(plane);
    dist.resize(plane);
    // the fade is measured in steps of 2 across and 3 diagonally
    const int CAP = int(SIGHT_FADE * 2);
    buckets.resize(size_t(CAP) + 1);

    for (int k = 0; k < out.nz; ++k) {
        const int z = zlo + k;
        // read the level
        df::map_block *blk = nullptr;
        int bx = -1, by = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const int mx = x0 + x, my = y0 + y;
                if ((mx >> 4) != bx || (my >> 4) != by) {
                    bx = mx >> 4;
                    by = my >> 4;
                    blk = Maps::getTileBlock(mx, my, z);
                }
                const size_t i = size_t(y) * W + x;
                if (!blk) {
                    kind[i] = T_NONE;
                    outside[i] = 0;
                    continue;
                }
                const df::tile_designation des = blk->designation[mx & 15][my & 15];
                const df::tiletype tt = blk->tiletype[mx & 15][my & 15];
                const df::tiletype_shape_basic basic = tileShapeBasic(tileShape(tt));
                const bool wall = basic == df::tiletype_shape_basic::Wall;
                const bool ground = basic == df::tiletype_shape_basic::Floor ||
                                    basic == df::tiletype_shape_basic::Ramp ||
                                    basic == df::tiletype_shape_basic::Stair || des.bits.flow_size > 0;
                // in adventure mode the first of the dig bits is the line of sight
                const bool seen = (uint32_t(des.bits.dig) & 1) != 0;
                outside[i] = des.bits.outside ? 1 : 0;
                uint8_t kd = seen ? (wall ? T_SEEN_WALL : T_SEEN) : (ground ? T_DARK : T_NONE);
                if (wall)
                    kd |= T_WALL;
                kind[i] = kd;
            }

        // how far each seen open tile is from the unseen ground, through open tiles
        std::fill(dist.begin(), dist.end(), uint8_t(255));
        for (auto &b : buckets)
            b.clear();
        for (size_t i = 0; i < plane; ++i)
            if ((kind[i] & 7) == T_DARK) {
                dist[i] = 0;
                buckets[0].push_back(int(i));
            }
        for (int d = 0; d <= CAP; ++d) {
            std::vector<int> &bk = buckets[d];
            for (size_t q = 0; q < bk.size(); ++q) {
                const int i = bk[q];
                if (dist[i] != d)
                    continue;
                const int x = i % W, y = i / W;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy)
                            continue;
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H)
                            continue;
                        const int j = ny * W + nx;
                        if ((kind[j] & 7) != T_SEEN)
                            continue;
                        // not diagonally between two walls
                        if (dx && dy && (kind[y * W + nx] & T_WALL) && (kind[ny * W + x] & T_WALL))
                            continue;
                        const int nd = d + (dx && dy ? 3 : 2);
                        if (nd > CAP || nd >= dist[j])
                            continue;
                        dist[j] = uint8_t(nd);
                        buckets[nd].push_back(j);
                    }
            }
        }

        uint8_t *L = out.light.data() + plane * k;
        uint8_t *has = out.has.data() + plane * k;
        const auto open_light = [&](size_t i) {
            const float base = outside[i] ? 1.0f : SIGHT_INDOORS;
            const float t = dist[i] / 2.0f;
            const float ramp = t >= SIGHT_FADE ? 1.0f : std::pow(t / SIGHT_FADE, 0.75f);
            const float g = base * ramp;
            // under the open sky the sun lights everything, seen or not
            return outside[i] ? g + (1 - g) * day : g;
        };
        for (size_t i = 0; i < plane; ++i)
            if ((kind[i] & 7) == T_SEEN) {
                L[i] = uint8_t(open_light(i) * 255.0f + 0.5f);
                has[i] = 1;
            } else if ((kind[i] & 7) == T_DARK) {
                L[i] = outside[i] ? uint8_t(day * 255.0f + 0.5f) : 0;
                has[i] = 1;
            }
        // a seen wall is as light as the open ground it faces
        for (size_t i = 0; i < plane; ++i) {
            if ((kind[i] & 7) != T_SEEN_WALL)
                continue;
            const int x = int(i % W), y = int(i / W);
            int best = -1;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H)
                        continue;
                    const size_t j = size_t(ny) * W + nx;
                    if ((kind[j] & 7) == T_SEEN)
                        best = std::max(best, int(L[j]));
                }
            L[i] = uint8_t(best >= 0 ? best : int((outside[i] ? 1.0f : SIGHT_INDOORS) * 255.0f + 0.5f));
            has[i] = 1;
        }
        for (size_t i = 0; i < plane; ++i)
            if (has[i]) {
                out.col[i] = std::max(out.col[i], L[i]);
                out.col_has[i] = 1;
            }
    }
}

} // namespace fpv
