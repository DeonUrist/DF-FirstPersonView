#include "farland.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <tuple>

// FARLAND_OFFLINE: only the building of a view, for a test program without the game
#ifndef FARLAND_OFFLINE
#include "TileTypes.h"
#include "modules/Maps.h"

#include "df/biome_type.h"
#include "df/block_square_event.h"
#include "df/block_square_event_material_spatterst.h"
#include "df/global_objects.h"
#include "df/map_block.h"
#include "df/matter_state.h"
#include "df/plant.h"
#include "df/plant_growth.h"
#include "df/plant_tree_info.h"
#include "df/plant_tree_tile.h"
#include "df/plant_raw.h"
#include "df/plant_raw_flags.h"
#include "df/region_map_entry.h"
#include "df/tile_designation.h"
#include "df/world.h"
#include "df/world_data.h"
#include "df/world_population.h"
#include "df/world_population_type.h"
#include "df/world_region.h"
#include "df/world_region_details.h"
#include "df/world_region_type.h"

using namespace DFHack;
using df::global::world;
#endif

namespace fpv {

namespace {

constexpr int SQ = 48;            // tiles across a square of the mid-level map
constexpr int SQ_PER_TILE = 16;   // squares across a world tile
constexpr int C0 = 4;             // tiles across a cell of the finest ring
constexpr int RING = 48;          // cells from a ring's middle to its edge (each ring is 2 RING cells across)
constexpr int MAX_TREES = 6000;   // trees drawn as trees: beyond the last of them, woods are blocks

using World = FarLand::World;
using WorldTile = FarLand::WorldTile;
using B = df::biome_type;

uint32_t mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

uint32_t hash3(int a, int b, uint32_t c) {
    return mix32(uint32_t(a) * 0x9E3779B1u ^ mix32(uint32_t(b) * 0x85EBCA77u ^ mix32(c + 0x632BE5ABu)));
}

float rnd(int a, int b, uint32_t c) {
    return float(hash3(a, b, c) >> 8) * (1.0f / 16777216.0f);
}

int floordiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// smooth noise over the squares, 0..1, one lattice point every `wl` squares
float vnoise(int sx, int sy, int wl, uint32_t seed) {
    const int ix = floordiv(sx, wl), iy = floordiv(sy, wl);
    float fx = float(sx - ix * wl) / wl, fy = float(sy - iy * wl) / wl;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    const float a = rnd(ix, iy, seed), b = rnd(ix + 1, iy, seed);
    const float c = rnd(ix, iy + 1, seed), d = rnd(ix + 1, iy + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// Made-up relief, -1..1 (ridged: 0..1), without the waves shorter than `mw`
// squares: a coarse ring would only catch them here and there.
float relief(int sx, int sy, int mw, bool ridged) {
    static const int WL[] = {32, 16, 8, 4, 2};
    static const float AMP[] = {1.0f, 0.55f, 0.3f, 0.16f, 0.09f};
    float sum = 0, total = 0;
    for (int i = 0; i < 5; ++i) {
        total += AMP[i];
        if (WL[i] < mw)
            continue;
        float n = vnoise(sx, sy, WL[i], 977u + uint32_t(WL[i]));
        n = ridged ? 1 - std::abs(2 * n - 1) : 2 * n - 1;
        sum += AMP[i] * n;
    }
    return sum / total;
}

bool is_ocean(int b) {
    return b == int(B::OCEAN_TROPICAL) || b == int(B::OCEAN_TEMPERATE) || b == int(B::OCEAN_ARCTIC);
}

bool is_lake(int b) {
    return b >= int(B::LAKE_TEMPERATE_FRESHWATER) && b <= int(B::LAKE_TROPICAL_SALTWATER);
}

// the ground's colour in a biome, near the near view's colours of grass, sand and rock
uint32_t ground_colour(int b) {
    switch (B(b)) {
    case B::MOUNTAIN: return rgb(126, 122, 116);
    case B::GLACIER: return rgb(224, 232, 240);
    case B::TUNDRA: return rgb(168, 164, 138);
    case B::SWAMP_TEMPERATE_FRESHWATER:
    case B::SWAMP_TEMPERATE_SALTWATER:
    case B::SWAMP_TROPICAL_FRESHWATER:
    case B::SWAMP_TROPICAL_SALTWATER: return rgb(80, 98, 58);
    case B::SWAMP_MANGROVE: return rgb(72, 94, 60);
    case B::MARSH_TEMPERATE_FRESHWATER:
    case B::MARSH_TEMPERATE_SALTWATER:
    case B::MARSH_TROPICAL_FRESHWATER:
    case B::MARSH_TROPICAL_SALTWATER: return rgb(100, 120, 70);
    case B::FOREST_TAIGA: return rgb(86, 100, 66);
    case B::FOREST_TEMPERATE_CONIFER:
    case B::FOREST_TEMPERATE_BROADLEAF: return rgb(80, 112, 52);
    case B::FOREST_TROPICAL_CONIFER:
    case B::FOREST_TROPICAL_DRY_BROADLEAF:
    case B::FOREST_TROPICAL_MOIST_BROADLEAF: return rgb(64, 110, 46);
    case B::GRASSLAND_TEMPERATE: return rgb(92, 132, 56);
    case B::GRASSLAND_TROPICAL: return rgb(118, 138, 60);
    case B::SAVANNA_TEMPERATE:
    case B::SAVANNA_TROPICAL: return rgb(152, 140, 76);
    case B::SHRUBLAND_TEMPERATE:
    case B::SHRUBLAND_TROPICAL: return rgb(126, 126, 72);
    case B::DESERT_BADLAND: return rgb(164, 104, 70);
    case B::DESERT_ROCK: return rgb(150, 132, 112);
    case B::DESERT_SAND: return rgb(210, 186, 128);
    default: return rgb(122, 120, 112);
    }
}

// trees on a square of the biome at full vegetation
float trees_per_square(int b) {
    switch (B(b)) {
    case B::FOREST_TAIGA:
    case B::FOREST_TEMPERATE_CONIFER:
    case B::FOREST_TEMPERATE_BROADLEAF:
    case B::FOREST_TROPICAL_CONIFER:
    case B::FOREST_TROPICAL_DRY_BROADLEAF:
    case B::FOREST_TROPICAL_MOIST_BROADLEAF: return 260;
    case B::SWAMP_MANGROVE: return 180;
    case B::SWAMP_TEMPERATE_FRESHWATER:
    case B::SWAMP_TEMPERATE_SALTWATER:
    case B::SWAMP_TROPICAL_FRESHWATER:
    case B::SWAMP_TROPICAL_SALTWATER: return 140;
    case B::SAVANNA_TEMPERATE:
    case B::SAVANNA_TROPICAL: return 40;
    case B::SHRUBLAND_TEMPERATE:
    case B::SHRUBLAND_TROPICAL: return 28;
    case B::GRASSLAND_TEMPERATE:
    case B::GRASSLAND_TROPICAL: return 12;
    case B::MARSH_TEMPERATE_FRESHWATER:
    case B::MARSH_TEMPERATE_SALTWATER:
    case B::MARSH_TROPICAL_FRESHWATER:
    case B::MARSH_TROPICAL_SALTWATER: return 8;
    case B::TUNDRA: return 2;
    default: return 0;
    }
}

uint32_t mix(uint32_t a, uint32_t b, float k) {
    k = std::clamp(k, 0.0f, 1.0f);
    return rgb(int(px_r(a) + (px_r(b) - px_r(a)) * k + 0.5f), int(px_g(a) + (px_g(b) - px_g(a)) * k + 0.5f),
               int(px_b(a) + (px_b(b) - px_b(a)) * k + 0.5f));
}

uint32_t scaled(uint32_t c, float k) {
    return rgb(std::clamp(int(px_r(c) * k + 0.5f), 0, 255), std::clamp(int(px_g(c) * k + 0.5f), 0, 255),
               std::clamp(int(px_b(c) * k + 0.5f), 0, 255));
}

// The game's picture of water 7/7 deep (UNDERWATER_7 of its LIQUIDS page, vanilla
// graphics): in the near view it lies over the ground under it at 85% of its
// strength (WATER_OPACITY), so the far land's water is the ground under it mixed so.
const uint32_t WATER_DEEP = rgb(0, 77, 90);
const uint32_t SAND = rgb(196, 182, 132), SNOW = rgb(232, 236, 242), SOIL = rgb(120, 94, 64);
const uint32_t ROCK = rgb(122, 120, 126), ICE = rgb(172, 206, 230);

// what the side of a block of ground shows under its top
uint32_t side_of(uint32_t top, int biome) {
    if (top == SNOW || biome == int(B::GLACIER))
        return scaled(SNOW, 0.9f);
    switch (B(biome)) {
    case B::MOUNTAIN:
    case B::DESERT_ROCK:
    case B::TUNDRA: return scaled(ROCK, 0.95f);
    case B::DESERT_SAND: return scaled(SAND, 0.9f);
    case B::DESERT_BADLAND: return scaled(top, 0.9f);
    default: return SOIL;
    }
}

// one 48-tile square, as the far land sees it
struct Square {
    float el = 100;        // its ground, in elevations (a z-level each)
    float water = -1e9f;   // the elevation whose floor its water's surface is level with; none if far below
    int biome = 0;
    int region = -1, align = 0;
    float trees = 0;       // trees on it
    uint32_t colour = 0;   // its ground's colour
};

struct Builder {
    const World &w;

    const WorldTile &tile(int wx, int wy) const {
        wx = std::clamp(wx, 0, w.w - 1);
        wy = std::clamp(wy, 0, w.h - 1);
        return w.tiles[size_t(wy) * w.w + wx];
    }

    // the world map's elevation at a point given in squares, smoothed between world tiles
    float worldEl(float sx, float sy) const {
        const float u = sx / SQ_PER_TILE - 0.5f, v = sy / SQ_PER_TILE - 0.5f;
        const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        const float fx = u - x0, fy = v - y0;
        const float a = tile(x0, y0).el, b = tile(x0 + 1, y0).el, c = tile(x0, y0 + 1).el, d = tile(x0 + 1, y0 + 1).el;
        return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
    }

    Square square(int sx, int sy, int mw) const {
        Square s;
        const int wx = floordiv(sx, SQ_PER_TILE), wy = floordiv(sy, SQ_PER_TILE);
        const WorldTile &t = tile(wx, wy);
        const bool inside = wx >= 0 && wy >= 0 && wx < w.w && wy < w.h;
        const auto found = inside ? w.details.find(wy * w.w + wx) : w.details.end();
        if (found != w.details.end()) {
            const int ix = sx - wx * SQ_PER_TILE, iy = sy - wy * SQ_PER_TILE;
            s.el = found->second.el[ix][iy];
            s.biome = found->second.biome[ix][iy];
            s.region = found->second.region[ix][iy];
            s.align = found->second.align[ix][iy];
        } else {
            s.biome = t.biome;
            s.region = t.region;
            s.align = t.align;
            const float base = worldEl(sx + 0.5f, sy + 0.5f);
            if (t.ocean || is_ocean(t.biome)) {
                s.el = std::min(base, 96.0f);
            } else if (base >= 150 || t.biome == int(B::MOUNTAIN)) {
                // mountains: ridges as high as the range stands above the plains
                s.el = base + 0.55f * std::max(20.0f, base - 120.0f) * (relief(sx, sy, mw, true) - 0.45f);
            } else {
                const float amp = t.region_type == int(df::world_region_type::Hills) ? 9.0f : 3.0f;
                s.el = base + amp * relief(sx, sy, mw, false);
            }
        }
        // water: the sea over the ocean floor, a lake at its world tile's level
        if (is_ocean(s.biome) || (t.ocean && s.el < w.sea)) {
            s.water = w.sea;
        } else if (t.lake || is_lake(s.biome)) {
            s.water = t.el;
            s.el = std::min(s.el, s.water - 1);
        }
        s.trees = s.water > s.el ? 0.0f : trees_per_square(s.biome) * t.veg / 100.0f;
        if (s.el >= 150 && s.biome != int(B::MOUNTAIN))
            s.trees *= std::clamp((260 - s.el) / 110, 0.0f, 1.0f);   // thinner up a mountain side
        uint32_t c = ground_colour(s.biome);
        if (s.biome == int(B::GLACIER))
            c = SNOW;
        else if (s.el > 270)
            c = mix(c, SNOW, (s.el - 270) / 60);
        s.colour = c;
        return s;
    }

    // the ground's elevation at a point (global tiles), between the squares' middles
    float elevation(float gx, float gy, int mw) const {
        const float u = gx / SQ - 0.5f, v = gy / SQ - 0.5f;
        const int sx = int(std::floor(u)), sy = int(std::floor(v));
        const float fx = u - sx, fy = v - sy;
        const float a = square(sx, sy, mw).el, b = square(sx + 1, sy, mw).el;
        const float c = square(sx, sy + 1, mw).el, d = square(sx + 1, sy + 1, mw).el;
        return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
    }

    int level(float el) const { return int(std::floor(el + w.offset + 0.5f)); }
};

// a shape's blocks, standing at a place: boxes in the view's list
void place(FarView &v, const FarLand::Shape &sh, const std::vector<FarLand::RelBox> &boxes, float x, float y,
           float base, const FarTreeLook &look, float snow, uint32_t &first, uint32_t &count) {
    first = uint32_t(v.boxes.size());
    for (const FarLand::RelBox &r : boxes) {
        FarBox b;
        b.x0 = x + r.x0;
        b.y0 = y + r.y0;
        b.x1 = x + r.x1;
        b.y1 = y + r.y1;
        b.z0 = base + r.z0 * LEVEL_H;
        b.z1 = base + r.z1 * LEVEL_H;
        b.top = b.side = r.leaf ? look.leaves : look.bark;
        b.leaf = r.leaf;
        if (r.leaf && snow > 0) {
            // snow lies on the leaves in specks, as the map's snowy leaf tiles show it (the
            // renderers draw the specks), a little thicker on top
            b.snow = uint8_t(std::min(0.9f, snow * 1.15f) * 255 + 0.5f);
            b.top = mix(b.top, SNOW, 0.25f * snow);
        }
        v.boxes.push_back(b);
    }
    count = uint32_t(v.boxes.size()) - first;
    (void)sh;
}

std::shared_ptr<FarView> build(std::shared_ptr<const World> wp, std::shared_ptr<const FarLand::Snow> snow,
                               float eye_x, float eye_y, FarSettings s, std::shared_ptr<std::atomic<int>> ms_out) {
    const auto t0 = std::chrono::steady_clock::now();
    const World &w = *wp;
    Builder B_{w};
    auto view = std::make_shared<FarView>();
    FarView &v = *view;
    v.region_x = w.region_x;
    v.region_y = w.region_y;
    const float ox = float(w.region_x * SQ), oy = float(w.region_y * SQ);   // global tiles of the map's corner
    const float gx = ox + eye_x, gy = oy + eye_y;
    int levels = 1;
    while (levels < 10 && float(RING * C0) * float(1 << (levels - 1)) < s.horizon)
        ++levels;
    const auto surf_at = [&](int lx, int ly) -> int {
        if (lx < 0 || ly < 0 || lx >= w.map_w || ly >= w.map_h || w.surf.size() < size_t(w.map_w) * w.map_h)
            return -1;
        const size_t at = size_t(ly) * w.map_w + lx;
        return w.surf[at] == -32768 ? -1 : int(at);
    };
    // How much snow lies at a point (map tiles), 0..1: on the loaded map as
    // measured there; beyond it as much as on the map, less in warmer land (the
    // world map's temperature against that where you are), none where it is much warmer.
    const float t_here = float(B_.tile(int(std::floor(gx / (SQ * SQ_PER_TILE))), int(std::floor(gy / (SQ * SQ_PER_TILE)))).temp);
    const auto snow_at = [&](float lx, float ly) -> float {
        if (!snow || snow->cover <= 0)
            return 0.0f;
        const int tx = int(std::floor(lx)), ty = int(std::floor(ly));
        if (tx >= 0 && ty >= 0 && tx < w.map_w && ty < w.map_h && snow->tile.size() == size_t(w.map_w) * w.map_h)
            return snow->tile[size_t(ty) * w.map_w + tx] / 255.0f;
        const float wx = (lx + ox) / (SQ * SQ_PER_TILE), wy = (ly + oy) / (SQ * SQ_PER_TILE);
        const float t = float(B_.tile(int(std::floor(wx)), int(std::floor(wy))).temp);
        return snow->cover * std::clamp(1 - (t - t_here) / 25.0f, 0.0f, 1.0f);
    };

    // the rings: tops and colours (forest kinds noted, for the woods made into blocks later)
    std::vector<std::vector<float>> cover(levels);
    std::vector<std::vector<uint32_t>> kind(levels);
    const int n = 2 * RING;
    for (int k = 0; k < levels; ++k) {
        FarLevel lv;
        const int step = C0 << k;
        const int snap = 2 * step;
        const int c_x = int(std::floor(gx / snap + 0.5f)) * snap, c_y = int(std::floor(gy / snap + 0.5f)) * snap;
        const int gx0 = c_x - RING * step, gy0 = c_y - RING * step;
        lv.n = n;
        lv.step = float(step);
        lv.x0 = float(gx0) - ox;
        lv.y0 = float(gy0) - oy;
        const size_t cells = size_t(n) * n;
        lv.h.assign(cells, 0);
        lv.low.assign(cells * 4, 0);
        lv.top.assign(cells, 0);
        lv.side.assign(cells, 0);
        lv.wet.assign(cells, 0);
        cover[k].assign(cells, 0);
        kind[k].assign(cells, 0);
        if (k > 0) {
            const FarLevel &in = v.levels[k - 1];
            lv.hx0 = int(std::lround((in.x0 - lv.x0) / lv.step));
            lv.hy0 = int(std::lround((in.y0 - lv.y0) / lv.step));
            lv.hx1 = lv.hx0 + RING;
            lv.hy1 = lv.hy0 + RING;
        }
        const int mw = std::max(1, 2 * step / SQ);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const size_t at = size_t(j) * n + i;
                if (k > 0 && lv.hole(i, j))
                    continue;
                const float cgx = gx0 + (i + 0.5f) * step, cgy = gy0 + (j + 0.5f) * step;
                const int sx = int(std::floor(cgx / SQ)), sy = int(std::floor(cgy / SQ));
                const Square q = B_.square(sx, sy, mw);
                cover[k][at] = std::min(1.0f, q.trees / 200.0f);
                kind[k][at] = World::kindKey(q.region, q.biome, q.align);
                const int m = surf_at(int(std::floor(cgx - ox)), int(std::floor(cgy - oy)));
                if (m >= 0) {
                    // the loaded map: its real ground, or its water's surface as deep as the water is
                    const int depth = w.surf_water[m];
                    lv.h[at] = (depth ? w.surf[m] - (depth >= 4 ? 1 : 0) + depth / 7.0f : float(w.surf[m])) * LEVEL_H;
                    lv.wet[at] = depth ? 1 : 0;
                    lv.top[at] = w.surf_top[m];
                    lv.side[at] = w.surf_side[m];
                    // snow in patches, as on the map: a cell is snow or bare, not a blend of both
                    if (!w.surf_water[m] && snow && m < int(snow->tile.size()) &&
                        rnd(int(std::floor(cgx / step)), int(std::floor(cgy / step)), 50u + k) <
                            std::min(1.0f, snow->tile[m] / 255.0f * 1.3f))
                        lv.top[at] = scaled(SNOW, 0.96f + 0.04f * rnd(i, j, 51u + k));
                    continue;
                }
                const float el = B_.elevation(cgx, cgy, mw);
                const float snowy = snow_at(cgx - ox, cgy - oy);
                if (q.water > el) {
                    // (the sea exactly at the map's sea's surface: the two meet without a step)
                    lv.h[at] = (q.water == w.sea ? w.sea + w.offset : float(B_.level(q.water))) * LEVEL_H;
                    lv.top[at] = lv.side[at] = q.water != w.sea ? w.lake_col : w.sea_col;
                    lv.wet[at] = 1;
                    if (q.water != w.sea && snowy >= 0.6f)
                        lv.top[at] = ICE;   // a lake frozen over (its sides stay water: no wall is drawn for them)
                    continue;
                }
                const int lvl = B_.level(el);
                lv.h[at] = lvl * LEVEL_H;
                uint32_t top = q.colour;
                // a beach beside the sea or a lake
                if (lvl <= B_.level(w.sea) + 1)
                    for (int d = 0; d < 4; ++d) {
                        const Square nq = B_.square(sx + (d == 0) - (d == 1), sy + (d == 2) - (d == 3), mw);
                        if (nq.water > nq.el) {
                            top = mix(top, SAND, 0.8f);
                            break;
                        }
                    }
                // a little unevenness from cell to cell, as the map's own ground has
                top = scaled(top, 0.95f + 0.1f * rnd(int(std::floor(cgx / step)), int(std::floor(cgy / step)), 7u + k));
                // snow in patches: a cell is snow or bare ground, as much of it snow as lies there
                if (snowy > 0 && rnd(int(std::floor(cgx / step)), int(std::floor(cgy / step)), 50u + k) <
                                     std::min(1.0f, snowy * 1.3f))
                    top = scaled(SNOW, 0.96f + 0.04f * rnd(i, j, 51u + k));
                lv.top[at] = top;
                lv.side[at] = side_of(top, q.biome);
            }
        v.levels.push_back(std::move(lv));
    }
    const auto height_at = [&](float x, float y, float &h) {
        int k, i, j;
        if (!v.cellAt(x, y, k, i, j))
            return false;
        h = v.levels[k].h[size_t(j) * v.levels[k].n + i];
        return true;
    };

    // trees, nearest first: the real ones of the loaded map, and made-up ones outside it
    struct Cand {
        float d2, x, y, base;
        int plant, shape;
    };
    std::vector<Cand> cands;
    const float reach = std::max(0.0f, s.trees);
    if (reach > 0) {
        for (const FarLand::RealTree &t : w.real) {
            const float dx = t.x + 0.5f - eye_x, dy = t.y + 0.5f - eye_y;
            if (dx * dx + dy * dy <= reach * reach)
                cands.push_back(Cand{dx * dx + dy * dy, float(t.x), float(t.y), t.z * LEVEL_H, t.plant, t.shape});
        }
        const int esx = int(std::floor(gx / SQ)), esy = int(std::floor(gy / SQ));
        const int rs = int(reach / SQ) + 1;
        std::vector<std::pair<int, int>> squares;
        for (int dy = -rs; dy <= rs; ++dy)
            for (int dx = -rs; dx <= rs; ++dx)
                if ((dx * dx + dy * dy) * float(SQ * SQ) <= (reach + SQ) * (reach + SQ))
                    squares.emplace_back(dx, dy);
        std::sort(squares.begin(), squares.end(), [](const auto &a, const auto &b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
        int made = 0;
        for (const auto &[dx, dy] : squares) {
            if (made >= MAX_TREES)
                break;
            const int sx = esx + dx, sy = esy + dy;
            const Square q = B_.square(sx, sy, 1);
            if (q.trees <= 0)
                continue;
            const auto kinds = w.kinds.find(World::kindKey(q.region, q.biome, q.align));
            if (kinds == w.kinds.end() || kinds->second.empty())
                continue;
            const int count = int(q.trees) + (rnd(sx, sy, 5) < q.trees - int(q.trees) ? 1 : 0);
            for (int c = 0; c < count; ++c) {
                const float tx = std::floor(sx * float(SQ) + rnd(sx, sy, 100 + c) * SQ) - ox;
                const float ty = std::floor(sy * float(SQ) + rnd(sx, sy, 20000 + c) * SQ) - oy;
                if (tx >= 0 && ty >= 0 && tx < w.map_w && ty < w.map_h)
                    continue;   // the loaded map has its own
                const float ex = tx + 0.5f - eye_x, ey = ty + 0.5f - eye_y, d2 = ex * ex + ey * ey;
                if (d2 > reach * reach)
                    continue;
                float h;
                if (!height_at(tx + 0.5f, ty + 0.5f, h))
                    continue;
                int kk, ii, jj;
                v.cellAt(tx + 0.5f, ty + 0.5f, kk, ii, jj);
                if (v.levels[kk].wet[size_t(jj) * v.levels[kk].n + ii])
                    continue;
                const int plant = kinds->second[hash3(sx, sy, 60000 + c) % kinds->second.size()];
                const auto shapes = w.shapes_of.find(plant);
                if (shapes == w.shapes_of.end() || shapes->second.empty())
                    continue;
                const int shape = shapes->second[hash3(sx, sy, 80000 + c) % shapes->second.size()];
                cands.push_back(Cand{d2, tx, ty, h, plant, shape});
                ++made;
            }
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.d2 < b.d2; });
    // beyond the last tree drawn as a tree, woods are blocks of canopy
    float woods_from = reach;
    if (int(cands.size()) > MAX_TREES) {
        woods_from = std::sqrt(cands[MAX_TREES - 1].d2);
        cands.resize(MAX_TREES);
    }
    for (const Cand &c : cands) {
        const auto look = w.looks.find(c.plant);
        if (look == w.looks.end() || c.shape < 0 || size_t(c.shape) >= w.shapes.size())
            continue;
        const FarLand::Shape &sh = w.shapes[c.shape];
        FarTree t;
        t.x = c.x + 0.5f;
        t.y = c.y + 0.5f;
        t.reach = float(sh.reach) + 1;
        t.height = sh.height * LEVEL_H;
        const float snowy = snow_at(c.x + 0.5f, c.y + 0.5f);
        place(v, sh, sh.full, c.x, c.y, c.base, look->second, snowy, t.full, t.n_full);
        place(v, sh, sh.simple, c.x, c.y, c.base, look->second, snowy, t.simple, t.n_simple);
        v.trees.push_back(t);
    }

    // dense woods beyond the trees: the cell raised by its trees' height, in their leaves' colour
    for (int k = 0; k < levels; ++k) {
        FarLevel &lv = v.levels[k];
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const size_t at = size_t(j) * n + i;
                if ((k > 0 && lv.hole(i, j)) || cover[k][at] < 0.5f)
                    continue;
                const float cx = lv.x0 + (i + 0.5f) * lv.step - eye_x, cy = lv.y0 + (j + 0.5f) * lv.step - eye_y;
                if (cx * cx + cy * cy < woods_from * woods_from)
                    continue;
                if (lv.wet[at])
                    continue;
                const auto canopy = w.canopy.find(kind[k][at]);
                const auto tall = w.canopy_h.find(kind[k][at]);
                if (canopy == w.canopy.end())
                    continue;
                const float height = tall != w.canopy_h.end() ? tall->second : 8.0f;
                lv.h[at] += std::floor(height * 0.85f) * LEVEL_H;
                lv.top[at] = scaled(canopy->second, 0.95f + 0.1f * rnd(i, j, 31u + k));
                lv.side[at] = scaled(canopy->second, 0.85f);
                const float snowy = snow_at(lv.x0 + (i + 0.5f) * lv.step, lv.y0 + (j + 0.5f) * lv.step);
                if (snowy > 0) {
                    // far off, the specks on the leaves are too fine to see: their colour together
                    lv.top[at] = mix(lv.top[at], SNOW, 0.8f * std::min(0.9f, snowy * 1.15f));
                    lv.side[at] = mix(lv.side[at], SNOW, 0.6f * std::min(0.9f, snowy * 1.15f));
                }
            }
    }

    // how far down each side of a cell shows: to the lowest neighbour along it
    float top = -1e30f;
    for (int k = 0; k < levels; ++k) {
        FarLevel &lv = v.levels[k];
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                if (k > 0 && lv.hole(i, j))
                    continue;
                const size_t at = size_t(j) * n + i;
                const float h = lv.h[at];
                top = std::max(top, h);
                const float x0 = lv.x0 + i * lv.step, y0 = lv.y0 + j * lv.step, x1 = x0 + lv.step, y1 = y0 + lv.step;
                const float e = 0.01f;
                // north, south, west, east: points just outside the side
                const float px[4][3] = {{x0 + lv.step * 0.25f, x0 + lv.step * 0.5f, x0 + lv.step * 0.75f},
                                        {x0 + lv.step * 0.25f, x0 + lv.step * 0.5f, x0 + lv.step * 0.75f},
                                        {x0 - e, x0 - e, x0 - e},
                                        {x1 + e, x1 + e, x1 + e}};
                const float py[4][3] = {{y0 - e, y0 - e, y0 - e},
                                        {y1 + e, y1 + e, y1 + e},
                                        {y0 + lv.step * 0.25f, y0 + lv.step * 0.5f, y0 + lv.step * 0.75f},
                                        {y0 + lv.step * 0.25f, y0 + lv.step * 0.5f, y0 + lv.step * 0.75f}};
                for (int sd = 0; sd < 4; ++sd) {
                    float low = h;
                    for (int p = 0; p < 3; ++p) {
                        float nh;
                        if (height_at(px[sd][p], py[sd][p], nh))
                            low = std::min(low, nh);
                    }
                    lv.low[at * 4 + sd] = low;
                }
            }
    }
    for (const FarBox &b : v.boxes)
        top = std::max(top, b.z1);
    v.top = top;

    // the tops of the trees per tile, for the software renderer
    if (!v.trees.empty()) {
        const int half = std::min(320, int(std::ceil(woods_from)) + 16);
        v.tw = 2 * half;
        v.tx0 = int(std::floor(eye_x)) - half;
        v.ty0 = int(std::floor(eye_y)) - half;
        v.tree_top.assign(size_t(v.tw) * v.tw, -1e30f);
        v.tree_col.assign(size_t(v.tw) * v.tw, 0);
        v.tree_snow.assign(size_t(v.tw) * v.tw, 0);
        for (const FarTree &t : v.trees)
            for (uint32_t bi = t.full; bi < t.full + t.n_full; ++bi) {
                const FarBox &b = v.boxes[bi];
                for (int y = int(std::floor(b.y0)); y < int(std::ceil(b.y1)); ++y)
                    for (int x = int(std::floor(b.x0)); x < int(std::ceil(b.x1)); ++x) {
                        const int ix = x - v.tx0, iy = y - v.ty0;
                        if (ix < 0 || iy < 0 || ix >= v.tw || iy >= v.tw)
                            continue;
                        const size_t at = size_t(iy) * v.tw + ix;
                        if (b.z1 > v.tree_top[at]) {
                            v.tree_top[at] = b.z1;
                            v.tree_col[at] = b.top;
                            v.tree_snow[at] = b.snow;
                        }
                    }
            }
    }
    ms_out->store(int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
                          .count()));
    return view;
}

#ifndef FARLAND_OFFLINE
// the colours of the map's own ground on a tile, close to what the near view draws
void ground_look(df::tiletype tt, uint32_t &top, uint32_t &side) {
    switch (tileMaterial(tt)) {
    case df::tiletype_material::GRASS_LIGHT: top = rgb(92, 132, 56); side = SOIL; break;
    case df::tiletype_material::GRASS_DARK: top = rgb(64, 106, 46); side = SOIL; break;
    case df::tiletype_material::GRASS_DRY: top = rgb(152, 140, 76); side = SOIL; break;
    case df::tiletype_material::GRASS_DEAD: top = rgb(122, 100, 68); side = SOIL; break;
    case df::tiletype_material::SOIL: top = SOIL; side = scaled(SOIL, 0.9f); break;
    case df::tiletype_material::FROZEN_LIQUID: top = rgb(172, 206, 230); side = scaled(top, 0.9f); break;
    case df::tiletype_material::CONSTRUCTION: top = rgb(150, 140, 128); side = scaled(top, 0.9f); break;
    case df::tiletype_material::PLANT: top = rgb(92, 132, 56); side = SOIL; break;
    default: top = ROCK; side = scaled(ROCK, 0.92f); break;
    }
}

// the blocks of a real tree, from the game's own shape of it
FarLand::Shape shape_of(const df::plant_tree_info &ti) {
    FarLand::Shape sh;
    const int dx = ti.dim_x, dy = ti.dim_y, H = ti.body_height;
    const int ox = -(dx / 2), oy = -(dy / 2);
    if (!ti.body || dx <= 0 || dy <= 0 || H <= 0)
        return sh;
    std::vector<FarLand::RelBox> trunk, full, simple;
    for (int z = 0; z < H; ++z) {
        const df::plant_tree_tile *row = ti.body[z];
        if (!row)
            continue;
        struct Run {
            int y, x0, x1;
        };
        std::vector<Run> runs;
        for (int yy = 0; yy < dy; ++yy) {
            int lo = 1 << 30, hi = -(1 << 30);
            for (int xx = 0; xx < dx; ++xx) {
                const df::plant_tree_tile t = row[xx + yy * dx];
                if (t.bits.blocked)
                    continue;
                if (t.bits.trunk) {
                    trunk.push_back({int16_t(ox + xx), int16_t(oy + yy), int16_t(ox + xx + 1), int16_t(oy + yy + 1),
                                     int16_t(z), int16_t(z + 1), false});
                } else if (t.bits.branches || t.bits.leaves || t.bits.branch_w || t.bits.branch_n ||
                           t.bits.branch_e || t.bits.branch_s) {
                    lo = std::min(lo, xx);
                    hi = std::max(hi, xx);
                }
            }
            if (lo <= hi)
                runs.push_back({yy, lo, hi});
        }
        if (runs.empty())
            continue;
        // the leaves of the level: up to three bands of rows, each as wide as its widest row
        const int nr = int(runs.size());
        const int bands = nr >= 5 ? 3 : 1;   // narrow levels read as one block anyway
        for (int b = 0; b < bands; ++b) {
            const int r0 = b * nr / bands, r1 = (b + 1) * nr / bands;
            int lo = 1 << 30, hi = -(1 << 30);
            for (int r = r0; r < r1; ++r) {
                lo = std::min(lo, runs[r].x0);
                hi = std::max(hi, runs[r].x1);
            }
            full.push_back({int16_t(ox + lo), int16_t(oy + runs[r0].y), int16_t(ox + hi + 1),
                            int16_t(oy + runs[r1 - 1].y + 1), int16_t(z), int16_t(z + 1), true});
        }
        int lo = 1 << 30, hi = -(1 << 30);
        for (const Run &r : runs) {
            lo = std::min(lo, r.x0);
            hi = std::max(hi, r.x1);
        }
        simple.push_back({int16_t(ox + lo), int16_t(oy + runs.front().y), int16_t(ox + hi + 1),
                          int16_t(oy + runs.back().y + 1), int16_t(z), int16_t(z + 1), true});
    }
    // blocks one above another with the same outline become one
    const auto merge = [](std::vector<FarLand::RelBox> v) {
        std::sort(v.begin(), v.end(), [](const FarLand::RelBox &a, const FarLand::RelBox &b) {
            return std::tie(a.leaf, a.x0, a.y0, a.x1, a.y1, a.z0) < std::tie(b.leaf, b.x0, b.y0, b.x1, b.y1, b.z0);
        });
        std::vector<FarLand::RelBox> out;
        for (const FarLand::RelBox &b : v) {
            if (!out.empty()) {
                FarLand::RelBox &p = out.back();
                if (p.leaf == b.leaf && p.x0 == b.x0 && p.y0 == b.y0 && p.x1 == b.x1 && p.y1 == b.y1 && p.z1 == b.z0) {
                    p.z1 = b.z1;
                    continue;
                }
            }
            out.push_back(b);
        }
        return out;
    };
    trunk = merge(trunk);
    sh.full = merge(full);
    sh.simple = merge(simple);
    sh.full.insert(sh.full.begin(), trunk.begin(), trunk.end());
    sh.simple.insert(sh.simple.begin(), trunk.begin(), trunk.end());
    for (const FarLand::RelBox &b : sh.full) {
        sh.height = std::max<int>(sh.height, b.z1);
        sh.reach = std::max({sh.reach, int(-b.x0), int(b.x1) - 1, int(-b.y0), int(b.y1) - 1});
    }
    return sh;
}
#endif

// A tree of a kind none of which stands in the loaded map, after the raws the
// game grows trees by: a trunk, and leaves round its upper part, widest in the middle.
FarLand::Shape made_up_shape(int trunk_levels, int reach, bool palm, int variant) {
    FarLand::Shape sh;
    const float scale[3] = {0.6f, 0.8f, 1.0f};
    const int T = std::max(2, int(std::lround(trunk_levels * scale[variant % 3])));
    const int R = std::clamp(int(std::lround(reach * (0.7f + 0.15f * (variant % 3)))), 1, 5);
    sh.full.push_back({0, 0, 1, 1, 0, int16_t(T), false});
    const auto level = [&](int z, int r, std::vector<FarLand::RelBox> &out, bool bands) {
        if (r <= 0) {
            out.push_back({0, 0, 1, 1, int16_t(z), int16_t(z + 1), true});
            return;
        }
        if (!bands || r < 3) {
            out.push_back({int16_t(-r), int16_t(-r), int16_t(r + 1), int16_t(r + 1), int16_t(z), int16_t(z + 1), true});
            return;
        }
        // rounder: the middle rows full width, the outer rows a tile narrower at each end
        const int m = r / 2;
        out.push_back({int16_t(-(r - 1)), int16_t(-r), int16_t(r), int16_t(-m), int16_t(z), int16_t(z + 1), true});
        out.push_back({int16_t(-r), int16_t(-m), int16_t(r + 1), int16_t(m + 1), int16_t(z), int16_t(z + 1), true});
        out.push_back({int16_t(-(r - 1)), int16_t(m + 1), int16_t(r), int16_t(r + 1), int16_t(z), int16_t(z + 1), true});
    };
    if (palm) {
        level(T - 1, 1, sh.full, false);
        level(T, 2, sh.full, true);
    } else {
        const int c0 = std::max(1, int(T * 0.45f));
        const float mid = (c0 + T) * 0.5f, half = (T - c0) * 0.5f + 0.75f;
        for (int z = c0; z <= T; ++z) {
            const float f = (z - mid) / half;
            level(z, int(std::lround(R * std::sqrt(std::max(0.0f, 1 - f * f)))), sh.full, true);
        }
    }
    sh.simple.push_back(sh.full.front());
    for (size_t i = 1; i < sh.full.size(); ++i) {
        // one block a level: the outline of that level's bands
        const FarLand::RelBox &b = sh.full[i];
        if (!sh.simple.empty() && sh.simple.back().leaf && sh.simple.back().z0 == b.z0) {
            FarLand::RelBox &s = sh.simple.back();
            s.x0 = std::min(s.x0, b.x0);
            s.y0 = std::min(s.y0, b.y0);
            s.x1 = std::max(s.x1, b.x1);
            s.y1 = std::max(s.y1, b.y1);
        } else {
            sh.simple.push_back(b);
        }
    }
    for (const FarLand::RelBox &b : sh.full) {
        sh.height = std::max<int>(sh.height, b.z1);
        sh.reach = std::max({sh.reach, int(-b.x0), int(b.x1) - 1, int(-b.y0), int(b.y1) - 1});
    }
    return sh;
}

#ifndef FARLAND_OFFLINE
float median(std::vector<float> v) {
    if (v.empty())
        return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}
#endif

} // namespace

#ifdef FARLAND_OFFLINE
std::shared_ptr<FarView> farland_build_for_test(std::shared_ptr<const FarLand::World> w,
                                                std::shared_ptr<const FarLand::Snow> snow, float eye_x, float eye_y,
                                                const FarSettings &s, int &ms) {
    auto out = std::make_shared<std::atomic<int>>(0);
    auto v = build(std::move(w), std::move(snow), eye_x, eye_y, s, out);
    ms = out->load();
    return v;
}
FarLand::Shape farland_made_up_shape(int trunk, int reach, bool palm, int variant) {
    return made_up_shape(trunk, reach, palm, variant);
}
#endif

bool FarView::cellAt(float x, float y, int &k, int &i, int &j) const {
    for (k = 0; k < int(levels.size()); ++k) {
        const FarLevel &lv = levels[k];
        const float u = (x - lv.x0) / lv.step, v = (y - lv.y0) / lv.step;
        if (u < 0 || v < 0 || u >= lv.n || v >= lv.n)
            continue;
        i = int(u);
        j = int(v);
        return true;
    }
    return false;
}

bool FarView::sample(float x, float y, float &h, uint32_t &top_col, uint32_t &side_col, int &id) const {
    int k, i, j;
    if (!cellAt(x, y, k, i, j))
        return false;
    const FarLevel &lv = levels[k];
    const size_t at = size_t(j) * lv.n + i;
    h = lv.h[at];
    top_col = lv.top[at];
    side_col = lv.side[at];
    id = int(at) * 16 + k;
    if (tw > 0) {
        const int tx = int(std::floor(x)) - tx0, ty = int(std::floor(y)) - ty0;
        if (tx >= 0 && ty >= 0 && tx < tw && ty < tw) {
            const size_t t = size_t(ty) * tw + tx;
            if (tree_top[t] > h) {
                h = tree_top[t];
                top_col = tree_col[t];
                // snow on the leaves: the software renderer paints a run of rows at a
                // time, so specks would come out as streaks; their colour together instead
                if (t < tree_snow.size() && tree_snow[t])
                    top_col = mix(top_col, SNOW, tree_snow[t] / 255.0f * 0.85f);
                side_col = scaled(top_col, 0.85f);
                id = -1 - int(t);
            }
        }
    }
    return true;
}

#ifndef FARLAND_OFFLINE
void FarLand::clear(Scene &scene) {
    if (pending_.valid())
        pending_.wait();
    pending_ = {};
    world_.reset();
    world_id_ = nullptr;
    region_x_ = region_y_ = region_z_ = -1;
    dirty_ = true;
    std::lock_guard<std::mutex> lock(scene.mutex);
    scene.far.reset();
}

void FarLand::snapshotWorld(Scene &scene) {
    auto wp = std::make_shared<World>();
    World &w = *wp;
    df::world_data *wd = world->world_data;
    w.w = wd->world_width;
    w.h = wd->world_height;
    w.tiles.resize(size_t(w.w) * w.h);
    for (int y = 0; y < w.h; ++y)
        for (int x = 0; x < w.w; ++x) {
            WorldTile &t = w.tiles[size_t(y) * w.w + x];
            const df::region_map_entry *e = Maps::getRegionBiome(df::coord2d(int16_t(x), int16_t(y)));
            if (!e)
                continue;
            t.el = e->elevation;
            t.temp = e->temperature;
            t.veg = uint8_t(std::clamp<int>(e->vegetation, 0, 100));
            t.region = e->region_id;
            t.biome = uint8_t(Maps::getBiomeType(int16_t(x), int16_t(y)));
            t.align = e->evilness >= 66 ? 2 : e->evilness <= 32 ? 1 : 0;
            const df::world_region *r = e->region_id >= 0 && size_t(e->region_id) < wd->regions.size()
                                            ? wd->regions[e->region_id]
                                            : nullptr;
            t.region_type = r ? int16_t(r->type) : int16_t(-1);
            t.lake = e->flags.is_set(df::region_map_entry_flags::is_lake) ||
                     (r && r->type == df::world_region_type::Lake);
            t.ocean = e->elevation < 100 || (r && r->type == df::world_region_type::Ocean);
        }

    // the trees each region grows, by biome (and in good or evil land, the good or evil ones)
    const auto &plants = world->raws.plants.all;
    const auto is_tree = [&](int p) {
        return p >= 0 && size_t(p) < plants.size() && plants[p] && plants[p]->flags.is_set(df::plant_raw_flags::TREE);
    };
    std::vector<int> used;
    for (size_t ri = 0; ri < wd->regions.size(); ++ri) {
        const df::world_region *r = wd->regions[ri];
        if (!r)
            continue;
        std::unordered_map<uint32_t, std::vector<int>> found;
        for (size_t i = 0; i < r->tree_biomes.size(); ++i) {
            const int b = int(r->tree_biomes[i]);
            for (int align = 0; align < 3; ++align) {
                auto &list = found[World::kindKey(int(ri), b, align)];
                const auto add = [&](const auto &v) {
                    if (i < v.size() && is_tree(v[i]) && std::find(list.begin(), list.end(), int(v[i])) == list.end())
                        list.push_back(int(v[i]));
                };
                add(r->tree_tiles_1);
                add(r->tree_tiles_2);
                if (align == 1)
                    add(r->tree_tiles_good);
                if (align == 2)
                    add(r->tree_tiles_evil);
            }
        }
        // otherwise the trees living in the region, whatever the biome
        std::vector<int> any;
        for (const df::world_population *pop : r->population)
            if (pop && pop->type == df::world_population_type::Tree && is_tree(pop->plant) &&
                std::find(any.begin(), any.end(), int(pop->plant)) == any.end())
                any.push_back(int(pop->plant));
        constexpr int NB = int(sizeof(r->biome_tile_counts) / sizeof(r->biome_tile_counts[0]));
        for (int b = 0; b < NB; ++b)
            for (int align = 0; align < 3; ++align) {
                if (r->biome_tile_counts[b] <= 0 || trees_per_square(b) <= 0)
                    continue;
                const uint32_t key = World::kindKey(int(ri), b, align);
                auto it = found.find(key);
                std::vector<int> list = it != found.end() && !it->second.empty() ? it->second : any;
                if (list.empty())
                    continue;
                for (int p : list)
                    if (std::find(used.begin(), used.end(), p) == used.end())
                        used.push_back(p);
                w.kinds[key] = std::move(list);
            }
    }
    {
        std::lock_guard<std::mutex> lock(scene.mutex);
        for (int p : used)
            w.looks[p] = scene.farTreeLook(p);
    }
    // trees of every kind made up after the raws, for kinds none of which are loaded
    std::unordered_map<int, float> tall;
    for (int p : used) {
        const df::plant_raw &raw = *plants[p];
        bool palm = false;
        for (const df::plant_growth *g : raw.growths)
            if (g && g->id == "LEAVES" && g->locations.bits.trunk)
                palm = true;
        const int trunk = raw.max_trunk_height > 0 ? std::clamp(raw.max_trunk_height, 2, 40) : 6;
        const int reach = std::max(1, std::max(0, raw.heavy_branch_radius) + std::max(0, raw.light_branch_radius));
        float sum = 0;
        for (int v = 0; v < 3; ++v) {
            w.shapes_of[p].push_back(int(w.shapes.size()));
            w.shapes.push_back(made_up_shape(trunk, reach, palm, v));
            sum += w.shapes.back().height;
        }
        tall[p] = sum / 3;
    }
    w.made_up = w.shapes.size();
    for (const auto &[key, list] : w.kinds) {
        uint32_t r = 0, g = 0, b = 0, n = 0;
        float h = 0;
        for (int p : list) {
            const auto look = w.looks.find(p);
            if (look == w.looks.end() || !look->second.leaves)
                continue;
            r += px_r(look->second.leaves);
            g += px_g(look->second.leaves);
            b += px_b(look->second.leaves);
            h += tall[p];
            ++n;
        }
        if (n) {
            w.canopy[key] = rgb(int(r / n), int(g / n), int(b / n));
            w.canopy_h[key] = h / n;
        }
    }
    world_ = std::move(wp);
}

// the mid-level map the game keeps near the adventurer, the loaded map's ground
// and trees, and where its ground and sea lie against the mid-level map
void FarLand::readRegion() {
    const auto t0 = std::chrono::steady_clock::now();
    auto wp = std::make_shared<World>(*world_);
    World &w = *wp;
    df::world_data *wd = world->world_data;
    w.details.clear();
    for (const df::world_region_details *d : wd->midmap_data.region_details) {
        if (!d || d->pos.x < 0 || d->pos.y < 0 || d->pos.x >= w.w || d->pos.y >= w.h)
            continue;
        Details det;
        for (int x = 0; x < 16; ++x)
            for (int y = 0; y < 16; ++y) {
                det.el[x][y] = d->elevation[x][y];
                // which world tile's biome this square has: 1..9 as on a keypad, 5 its own
                const int code = d->biome[x][y] & 0x0F;
                int nx = d->pos.x, ny = d->pos.y;
                if (code >= 1 && code <= 9) {
                    nx += (code - 1) % 3 - 1;
                    ny += 1 - (code - 1) / 3;
                }
                nx = std::clamp(nx, 0, w.w - 1);
                ny = std::clamp(ny, 0, w.h - 1);
                const WorldTile &t = w.tiles[size_t(ny) * w.w + nx];
                det.biome[x][y] = t.biome;
                det.region[x][y] = t.region;
                det.align[x][y] = t.align;
            }
        w.details[d->pos.y * w.w + d->pos.x] = det;
    }
    w.region_x = world->map.region_x;
    w.region_y = world->map.region_y;
    int32_t mx = 0, my = 0, mz = 0;
    Maps::getTileSize(mx, my, mz);
    w.map_w = mx;
    w.map_h = my;

    // the loaded map's ground: on every tile, the level one would stand on at
    // the top (trees left out: they come as trees), or the water's surface
    w.surf.assign(size_t(mx) * my, int16_t(-32768));
    w.surf_top.assign(size_t(mx) * my, 0);
    w.surf_side.assign(size_t(mx) * my, 0);
    w.surf_water.assign(size_t(mx) * my, 0);
    int guess = mz - 1;
    uint64_t sea_sum[4] = {}, lake_sum[4] = {};   // the colours of the map's water: r, g, b and how many
    const auto open = [](const df::tiletype *tt) {
        if (!tt)
            return true;
        if (tileMaterial(*tt) == df::tiletype_material::TREE)
            return true;
        const df::tiletype_shape_basic basic = tileShapeBasic(tileShape(*tt));
        return basic == df::tiletype_shape_basic::Open || basic == df::tiletype_shape_basic::None;
    };
    for (int y = 0; y < my; ++y)
        for (int x = 0; x < mx; ++x) {
            // start a little above where the last column's ground was, and climb if that is inside the ground
            int z = std::min(mz - 1, guess + 12);
            while (z < mz - 1 && !open(Maps::getTileType(x, y, z)))
                z = std::min(mz - 1, z + 12);
            for (; z >= 0; --z) {
                const df::tiletype *tt = Maps::getTileType(x, y, z);
                const df::tile_designation *des = Maps::getTileDesignation(x, y, z);
                const size_t at = size_t(y) * mx + x;
                if (des && des->bits.flow_size > 0) {
                    w.surf[at] = int16_t(z + (des->bits.flow_size >= 4 ? 1 : 0));
                    w.surf_water[at] = uint8_t(des->bits.flow_size);
                    uint32_t col = rgb(200, 80, 20);
                    if (!des->bits.liquid_type) {
                        // what lies under the water shows through it
                        uint32_t floor = SAND, floor_side = SAND;
                        for (int zz = z, n = 0; zz >= 0 && n < 40; --zz, ++n) {
                            const df::tiletype *ft = Maps::getTileType(x, y, zz);
                            if (ft && !open(ft)) {
                                ground_look(*ft, floor, floor_side);
                                break;
                            }
                        }
                        col = mix(floor, WATER_DEEP, WATER_OPACITY);
                        uint64_t *sum = des->bits.water_salt ? sea_sum : lake_sum;
                        sum[0] += px_r(col);
                        sum[1] += px_g(col);
                        sum[2] += px_b(col);
                        ++sum[3];
                    }
                    w.surf_top[at] = w.surf_side[at] = col;
                    guess = z;
                    break;
                }
                if (open(tt))
                    continue;
                const bool wall = tileShapeBasic(tileShape(*tt)) == df::tiletype_shape_basic::Wall;
                w.surf[at] = int16_t(wall ? z + 1 : z);
                ground_look(*tt, w.surf_top[at], w.surf_side[at]);
                guess = z;
                break;
            }
        }

    // the real trees, as they stand, and their shapes for trees of their kinds outside the map
    w.shapes.resize(w.made_up);
    w.real.clear();
    std::unordered_map<int, std::vector<int>> real_shapes;
    for (const df::plant *p : world->plants.all) {
        if (!p || !p->tree_info || p->pos.x < 0 || p->pos.y < 0 || p->pos.x >= mx || p->pos.y >= my)
            continue;
        FarLand::Shape sh = shape_of(*p->tree_info);
        if (sh.full.empty())
            continue;
        const int plant = p->material;
        if (!w.looks.count(plant))
            continue;
        const int idx = int(w.shapes.size());
        w.shapes.push_back(std::move(sh));
        w.real.push_back(RealTree{p->pos.x, p->pos.y, p->pos.z, plant, idx});
        auto &list = real_shapes[plant];
        if (list.size() < 8)
            list.push_back(idx);
    }
    for (const auto &[plant, list] : real_shapes)
        w.shapes_of[plant] = list;

    // Line the heights up with the loaded map: in every square of it, the
    // level of the real ground (or sea) against the square's elevation.
    std::vector<float> ground, sea;
    for (int qy = 0; qy * SQ + SQ <= my; ++qy)
        for (int qx = 0; qx * SQ + SQ <= mx; ++qx) {
            const int sx = w.region_x + qx, sy = w.region_y + qy;
            const int wx = sx / SQ_PER_TILE, wy = sy / SQ_PER_TILE;
            const auto found = w.details.find(wy * w.w + wx);
            if (found == w.details.end())
                continue;
            const float el = found->second.el[sx - wx * SQ_PER_TILE][sy - wy * SQ_PER_TILE];
            const size_t at = size_t(qy * SQ + SQ / 2) * mx + qx * SQ + SQ / 2;
            if (w.surf[at] == -32768)
                continue;
            if (!w.surf_water[at] && el >= 100)
                ground.push_back(float(w.surf[at]) - el);
            else if (w.surf_water[at] && el < 100)
                sea.push_back(float(w.surf[at] - (w.surf_water[at] >= 4 ? 1 : 0)) + w.surf_water[at] / 7.0f);
        }
    const auto mean = [](const uint64_t *sum, uint32_t guess_col) {
        return sum[3] ? rgb(int(sum[0] / sum[3]), int(sum[1] / sum[3]), int(sum[2] / sum[3])) : guess_col;
    };
    w.sea_col = mean(sea_sum, mix(SAND, WATER_DEEP, WATER_OPACITY));
    w.lake_col = mean(lake_sum, mix(SOIL, WATER_DEEP, WATER_OPACITY));
    calib_n_ = int(ground.size());
    sea_n_ = int(sea.size());
    w.offset = ground.empty() ? -float(world->map.region_z) : median(ground);
    w.sea = sea.empty() ? 100.0f : median(sea) - w.offset;
    if (sea.empty())   // (then on a whole level, like the ground's)
        w.sea = std::floor(w.sea + w.offset + 0.5f) - w.offset;
    world_ = std::move(wp);
    read_ms_ = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
}

// how much snow lies on the loaded map's ground: the game's spatters of
// frozen water on the tile one stands on (full from 100, as the near view has it)
void FarLand::readSnow() {
    auto snow = std::make_shared<Snow>();
    const World &w = *world_;
    const size_t cells = size_t(w.map_w) * w.map_h;
    if (w.surf.size() != cells) {
        snow_ = snow;
        return;
    }
    snow->tile.assign(cells, 0);
    double sum = 0;
    int land = 0;
    const df::map_block *last = nullptr;
    const df::block_square_event_material_spatterst *drift[8];
    int nd = 0;
    for (int y = 0; y < w.map_h; ++y)
        for (int x = 0; x < w.map_w; ++x) {
            const size_t at = size_t(y) * w.map_w + x;
            if (w.surf[at] == -32768 || w.surf_water[at])
                continue;
            ++land;
            const df::map_block *block = Maps::getTileBlock(x, y, w.surf[at]);
            if (!block)
                continue;
            if (block != last) {
                last = block;
                nd = 0;
                for (df::block_square_event *ev : block->block_events)
                    if (auto *m = virtual_cast<df::block_square_event_material_spatterst>(ev))
                        if (m->mat_type == 6 && m->mat_state != df::matter_state::Liquid && nd < 8)
                            drift[nd++] = m;
            }
            int amount = 0;
            for (int i = 0; i < nd; ++i)
                amount = std::max<int>(amount, drift[i]->amount[x & 15][y & 15]);
            const float c = std::min(1.0f, amount / 100.0f);
            snow->tile[at] = uint8_t(c * 255 + 0.5f);
            sum += c;
        }
    snow->cover = land ? float(sum / land) : 0.0f;
    snow_ = snow;
}

void FarLand::update(Scene &scene, float eye_x, float eye_y, const FarSettings &s) {
    if (!s.on || !world || !world->world_data || world->world_data->world_width <= 0) {
        if (world_ || scene.far)
            clear(scene);
        return;
    }
    // a finished view goes to the scene, if the game's window has not moved since it was started
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::shared_ptr<FarView> v = pending_.get();
        last_ms_ = ms_out_->load();
        if (v && v->region_x == world->map.region_x && v->region_y == world->map.region_y) {
            last_trees_ = int(v->trees.size());
            last_levels_ = int(v->levels.size());
            last_cells_ = 0;
            for (const FarLevel &lv : v->levels)
                last_cells_ += lv.n * lv.n - (lv.hx1 - lv.hx0) * (lv.hy1 - lv.hy0);
            std::lock_guard<std::mutex> lock(scene.mutex);
            scene.far = std::move(v);
        } else {
            dirty_ = true;
        }
    }
    if (pending_.valid())
        return;   // one at a time

    df::world_data *wd = world->world_data;
    if (wd != world_id_ || wd->world_width != world_w_ || wd->world_height != world_h_ || !world_) {
        snapshotWorld(scene);
        world_id_ = wd;
        world_w_ = wd->world_width;
        world_h_ = wd->world_height;
        region_x_ = -1;
    }
    int32_t mx = 0, my = 0, mz = 0;
    Maps::getTileSize(mx, my, mz);
    if (world->map.region_x != region_x_ || world->map.region_y != region_y_ || world->map.region_z != region_z_ ||
        mx != map_w_ || my != map_h_) {
        readRegion();
        region_x_ = world->map.region_x;
        region_y_ = world->map.region_y;
        region_z_ = world->map.region_z;
        map_w_ = mx;
        map_h_ = my;
        dirty_ = true;
    }
    // The snow on the map comes and goes with the weather and the seasons, so
    // it is read again once the game's clock has moved on by some 100 ticks (a
    // few steps of walking; at once after a long wait; never while time stands still).
    const int64_t tick = (df::global::cur_year ? int64_t(*df::global::cur_year) : 0) * 403200 +
                         (df::global::cur_year_tick ? int64_t(*df::global::cur_year_tick) : 0);
    if (dirty_ || snow_tick_ < 0 || std::abs(tick - snow_tick_) >= 100) {
        snow_tick_ = tick;
        readSnow();
        if (std::abs(snow_->cover - built_snow_) > 0.03f)
            dirty_ = true;
    }
    const int sx = int(std::floor(eye_x / SQ)), sy = int(std::floor(eye_y / SQ));
    if (sx != built_sx_ || sy != built_sy_ || s.horizon != built_.horizon || s.trees != built_.trees ||
        s.view_distance != built_.view_distance)
        dirty_ = true;
    if (!dirty_)
        return;
    dirty_ = false;
    built_sx_ = sx;
    built_sy_ = sy;
    built_ = s;
    built_snow_ = snow_->cover;
    pending_ = std::async(std::launch::async, build, std::shared_ptr<const World>(world_), snow_, eye_x, eye_y, s,
                          ms_out_);
}

std::string FarLand::status() const {
    if (!world_)
        return "off";
    return "offset " + std::to_string(world_->offset) + " (from " + std::to_string(calib_n_) + " squares), sea " +
           std::to_string(world_->sea) + " (from " + std::to_string(sea_n_) + "), mid-level map for " +
           std::to_string(world_->details.size()) + " world tiles, " + std::to_string(world_->real.size()) +
           " real trees, snow on " + std::to_string(int(snow_->cover * 100 + 0.5f)) + "% of its ground, map read in " +
           std::to_string(read_ms_) + " ms; " + std::to_string(last_levels_) +
           " rings, " + std::to_string(last_cells_) + " cells, " + std::to_string(last_trees_) + " trees, built in " +
           std::to_string(last_ms_) + " ms";
}
#endif

} // namespace fpv
