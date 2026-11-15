#include "spatter.h"

#include <algorithm>
#include <string>

#include "modules/Maps.h"
#include "modules/Materials.h"
#include "TileTypes.h"

#include "df/block_square_event.h"
#include "df/block_square_event_item_spatterst.h"
#include "df/block_square_event_material_spatterst.h"
#include "df/descriptor_color.h"
#include "df/global_objects.h"
#include "df/item_type.h"
#include "df/map_block.h"
#include "df/material.h"
#include "df/matter_state.h"
#include "df/plant_growth.h"
#include "df/plant_raw.h"
#include "df/texture_handlerst.h"
#include "df/tile_pagest.h"
#include "df/world.h"

using namespace DFHack;

namespace fpv {

namespace {

// the graphics page of each kind, as named in the game's graphics_spatters.txt
const char *const PAGE[Spatters::COUNT] = {
    "SNOW", "BLOOD_RED", "BLOOD_CYAN", "BLOOD_MAGENTA", "BLOOD_GOO", "BLOOD_ICHOR", "DUST", "MUD", "VOMIT",
    "WATER_SPATTER", "MAGMA_SPATTER", "SLIME", "LEAVES", "FRUIT", "FRUIT_SMALL", "FRUIT_LARGE",
};

// kinds that only come as scattered bits, never as a cover (a 4 x 4 page)
bool scattered(Spatters::Kind k) {
    return k == Spatters::LEAVES || k == Spatters::FRUIT || k == Spatters::FRUIT_SMALL ||
           k == Spatters::FRUIT_LARGE;
}

// how much of the tile a spatter covers, as the game decides it (measured in
// game: below 25 the smallest bits, 100 and more the whole tile)
int level_of(int amount) {
    return amount <= 0 ? 0 : amount < 25 ? 1 : amount < 35 ? 2 : amount < 50 ? 3 : amount < 100 ? 4 : 5;
}

// The tiles of a fully covered tile on a 36-tile page, by which sides join a
// neighbour that is fully covered by the same kind (bits: 1 N, 2 S, 4 W, 8 E).
// All sides joined has five pictures to vary between.
const int8_t FULL[16][2] = {
    {1, 0}, {0, 1}, {1, 1}, {2, 0}, {2, 1}, {0, 2}, {2, 2}, {0, 3},
    {3, 1}, {1, 2}, {3, 2}, {1, 3}, {3, 0}, {2, 3}, {3, 3}, {0, 0},
};
const int8_t FULL_INSIDE[5][2] = {{0, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}};

// The game varies the pictures between tiles; this picks a variant that stays
// the same for a tile.
uint32_t tile_hash(int x, int y, int z) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

bool has(const std::string &s, const char *part) {
    return s.find(part) != std::string::npos;
}

} // namespace

void Spatters::reset() {
    loaded_ = false;
    for (auto &p : pages_)
        p.clear();
    kind_of_picture_.clear();
    kind_of_key_.clear();
}

void Spatters::loadPages() {
    if (loaded_ || !df::global::texture)
        return;
    loaded_ = true;
    for (const df::tile_pagest *page : df::global::texture->page) {
        if (!page)
            continue;
        for (int k = 0; k < COUNT; ++k) {
            if (page->token != PAGE[k])
                continue;
            pages_[k].assign(page->texpos.begin(), page->texpos.end());
            page_w_[k] = page->page_dim_x;
            for (const long tp : page->texpos)
                if (tp > 0)
                    kind_of_picture_[int32_t(tp)] = Kind(k);
        }
    }
}

int32_t Spatters::picture(Kind kind, int col, int row) {
    if (kind < 0 || kind >= COUNT)
        return 0;
    const size_t i = size_t(row) * page_w_[kind] + col;
    return i < pages_[kind].size() ? pages_[kind][i] : 0;
}

Spatters::Kind Spatters::kindOf(uint64_t key, int16_t mat_type, int32_t mat_index, int state, int item_type,
                                int item_subtype) {
    const auto known = kind_of_key_.find(key);
    if (known != kind_of_key_.end())
        return known->second;
    // a guess until the game is seen drawing it
    Kind k = NONE;
    const bool liquid = state == int(df::matter_state::Liquid);
    if (item_type >= 0) {
        if (df::item_type(item_type) == df::item_type::PLANT_GROWTH && df::global::world) {
            const auto &plants = df::global::world->raws.plants.all;
            const df::plant_raw *plant = mat_index >= 0 && size_t(mat_index) < plants.size() ? plants[mat_index] : nullptr;
            const df::plant_growth *growth =
                plant && item_subtype >= 0 && size_t(item_subtype) < plant->growths.size() ? plant->growths[item_subtype]
                                                                                            : nullptr;
            k = growth && growth->id == "LEAVES" ? LEAVES : FRUIT;
        }
    } else if (mat_type == 6) {   // water: snow unless it is liquid
        k = liquid ? WATER : SNOW;
    } else if (mat_type == 12) {
        k = MUD;
    } else if (mat_type == 13) {
        k = VOMIT;
    } else if (mat_type == 9) {   // ash: the game draws it with the pale ichor picture
        k = BLOOD_ICHOR;
    } else if (mat_type == 0) {   // stone: molten, or dust
        k = liquid ? MAGMA : DUST;
    } else {
        const MaterialInfo mi(mat_type, mat_index);
        if (!liquid || !mi.material) {
            k = DUST;
        } else if (has(mi.material->id, "SLIME")) {
            k = SLIME;
        } else if (!has(mi.material->id, "BLOOD") && !has(mi.material->id, "ICHOR") &&
                   !has(mi.material->id, "GOO")) {
            k = WATER;   // pus, oil, milk ... (the game draws pus as water)
        } else {
            // blood by its colour
            float r = 0.6f, g = 0, b = 0.1f;
            const int16_t ci = state >= 0 && state < 6 ? mi.material->state_color[state] : -1;
            const auto &colors = df::global::world->raws.descriptors.colors;
            if (ci >= 0 && size_t(ci) < colors.size() && colors[ci]) {
                r = colors[ci]->red;
                g = colors[ci]->green;
                b = colors[ci]->blue;
            }
            k = std::max({r, g, b}) < 0.2f   ? BLOOD_GOO
                : std::min({r, g, b}) > 0.6f ? BLOOD_ICHOR
                : r > g && b > 0.5f * r      ? BLOOD_MAGENTA
                : r > g && r > b             ? BLOOD_RED
                : b >= r && g >= r           ? BLOOD_CYAN
                                             : BLOOD_RED;
        }
    }
    kind_of_key_[key] = k;
    return k;
}

int Spatters::kinds(int x, int y, int z, Top *out, int max) {
    const df::map_block *block = Maps::getTileBlock(x, y, z);
    if (!block)
        return 0;
    int n = 0;
    const int bx = x & 15, by = y & 15;
    const auto add = [&](uint64_t key, int amount, auto kind_of) {
        if (amount <= 0)
            return;
        const Kind k = kind_of();
        for (int i = 0; i < n; ++i)
            if (out[i].kind == k) {
                if (amount > out[i].amount) {
                    out[i].amount = amount;
                    out[i].key = key;
                }
                return;
            }
        if (n < max)
            out[n++] = Top{key, amount, 0, k};
    };
    for (df::block_square_event *ev : block->block_events) {
        if (auto *m = virtual_cast<df::block_square_event_material_spatterst>(ev)) {
            const uint64_t key = (1ull << 63) | (uint64_t(uint8_t(m->mat_state)) << 48) |
                                 (uint64_t(uint16_t(m->mat_type)) << 32) | uint32_t(m->mat_index);
            add(key, m->amount[bx][by],
                [&] { return kindOf(key, m->mat_type, m->mat_index, int(m->mat_state), -1, -1); });
        } else if (auto *it = virtual_cast<df::block_square_event_item_spatterst>(ev)) {
            const uint64_t key = (1ull << 62) | (uint64_t(uint8_t(it->item_type)) << 48) |
                                 (uint64_t(uint16_t(it->item_subtype)) << 32) | uint32_t(it->matindex);
            add(key, it->amount[bx][by],
                [&] { return kindOf(key, it->mattype, it->matindex, -1, int(it->item_type), it->item_subtype); });
        }
    }
    for (int i = 0; i < n; ++i)
        out[i].level = level_of(out[i].amount);
    return n;
}

Spatters::Top Spatters::top(int x, int y, int z) {
    Top all[16];
    const int n = kinds(x, y, z, all, 16);
    Top best;
    for (int i = 0; i < n; ++i)
        if (all[i].amount > best.amount)
            best = all[i];
    return best;
}

int Spatters::levelOf(int x, int y, int z, Kind kind) {
    Top all[16];
    const int n = kinds(x, y, z, all, 16);
    for (int i = 0; i < n; ++i)
        if (all[i].kind == kind)
            return all[i].level;
    return 0;
}

int Spatters::levelBeside(int x, int y, int z, Kind kind, bool from_slope) {
    if (const int l = levelOf(x, y, z, kind))
        return l;
    const df::tiletype *tt = Maps::getTileType(x, y, z);
    if (!tt)
        return 0;
    const df::tiletype_shape shape = tileShape(*tt);
    const df::tiletype_shape_basic basic = tileShapeBasic(shape);
    if (basic == df::tiletype_shape_basic::Ramp) {
        // (a slope's snow may be kept on the open tile over it)
        const df::tiletype *above = Maps::getTileType(x, y, z + 1);
        return above && tileShape(*above) == df::tiletype_shape::RAMP_TOP ? levelOf(x, y, z + 1, kind) : 0;
    }
    if (shape == df::tiletype_shape::RAMP_TOP)
        return levelOf(x, y, z - 1, kind);   // the slope below is this tile's surface
    if (from_slope && basic == df::tiletype_shape_basic::Wall)
        return levelOf(x, y, z + 1, kind);   // a slope runs up onto the wall's top
    return 0;
}

uint32_t Spatters::holesAt(int x, int y, int z) {
    // mud or dust on the tiles around, 0..5 each
    int mud[3][3];
    bool any = false;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            Top all[16];
            const int n = kinds(x + i - 1, y + j - 1, z, all, 16);
            int m = 0;
            for (int k = 0; k < n; ++k)
                if (all[k].kind == MUD || all[k].kind == DUST)
                    m = std::max(m, all[k].level);
            mud[j][i] = m;
            any |= m > 0;
        }
    if (!any)
        return 0;
    // each corner by the four tiles that meet there, so neighbours agree on it
    // and the holes thin out across the border instead of stopping at it
    const auto corner = [&](int cx, int cy) {
        const int sum = mud[cy][cx] + mud[cy][cx + 1] + mud[cy + 1][cx] + mud[cy + 1][cx + 1];   // 0..20
        return uint32_t((sum + 1) / 2);
    };
    const uint32_t c = corner(0, 0) | corner(1, 0) << 4 | corner(0, 1) << 8 | corner(1, 1) << 12;
    if (!c)
        return 0;
    return c | uint32_t(x & 7) << 16 | uint32_t(y & 7) << 19;
}

int32_t Spatters::pictureAt(int x, int y, int z, Kind kind, int level, bool slope) {
    if (!level || kind == NONE)
        return 0;
    // (the kind in the hash: layers of different kinds on one tile vary apart)
    const uint32_t h = tile_hash(x, y, z) ^ (uint32_t(kind) * 0x9e3779b9u);
    if (scattered(kind))
        return picture(kind, int(h % 4), 4 - std::min(level, 4));
    if (level < 5)
        return picture(kind, 4 + int(h % 4), 4 - level);
    int sides = 0;
    const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    for (int i = 0; i < 4; ++i)
        if (levelBeside(x + dx[i], y + dy[i], z, kind, slope) == 5)
            sides |= 1 << i;
    if (sides == 15) {
        const int8_t *v = FULL_INSIDE[h % 5];
        return picture(kind, v[0], v[1]);
    }
    return picture(kind, FULL[sides][0], FULL[sides][1]);
}

int32_t Spatters::fromMap(int x, int y, int z) {
    loadPages();
    const Top t = top(x, y, z);
    return pictureAt(x, y, z, t.kind, t.level);
}

int Spatters::layers(int x, int y, int z, int32_t seen, bool slope, Layer *out, int max) {
    loadPages();
    Top all[16];
    int n = kinds(x, y, z, all, 16);
    int from_z = z;
    if (!n && slope) {
        // what lies on a slope may be kept on the open tile over it
        const df::tiletype *above = Maps::getTileType(x, y, z + 1);
        if (above && tileShape(*above) == df::tiletype_shape::RAMP_TOP) {
            n = kinds(x, y, z + 1, all, 16);
            from_z = z + 1;
        }
    }
    // mud and dust lie on the ground, snow falls on them, blood and the rest
    // land on the snow
    const auto rank = [](Kind k) { return k == MUD || k == DUST ? 0 : k == SNOW ? 1 : 2; };
    std::sort(all, all + n, [&](const Top &a, const Top &b) {
        return rank(a.kind) != rank(b.kind) ? rank(a.kind) < rank(b.kind) : a.amount < b.amount;
    });
    const auto seen_kind = seen ? kind_of_picture_.find(seen) : kind_of_picture_.end();
    bool used_seen = seen_kind == kind_of_picture_.end();
    int count = 0;
    for (int i = 0; i < n && count < max; ++i) {
        if (!all[i].level || all[i].kind == NONE)
            continue;
        Layer l;
        if (!used_seen && all[i].kind == seen_kind->second) {
            // a full cover is joined here instead: the game joins it only with
            // neighbours where that kind is on top, so snow next to mud and snow
            // would get an edge
            l.tp = all[i].level < 5 ? seen : pictureAt(x, y, z, all[i].kind, all[i].level, slope);
            used_seen = true;
        } else {
            l.tp = pictureAt(x, y, z, all[i].kind, all[i].level, slope);
        }
        if (all[i].kind == SNOW)
            l.holes = holesAt(x, y, from_z);
        if (l.tp)
            out[count++] = l;
    }
    // the game shows something the map does not explain: keep it on top
    if (!used_seen && count < max)
        out[count++] = Layer{seen, 0};
    return count;
}

bool Spatters::snow(int x, int y, int z, int32_t &top_tp, int32_t &side_tp) {
    loadPages();
    // (snow lies over mud and dust, so it shows whichever there is more of)
    const int level = levelOf(x, y, z, SNOW);
    if (!level)
        return false;
    top_tp = pictureAt(x, y, z, SNOW, level);
    // the sides catch less than the top: two steps less cover, never joined up
    const int side_level = std::max(1, level - 2);
    side_tp = picture(SNOW, 4 + int(tile_hash(x, y, z) % 4), 4 - side_level);
    return top_tp != 0;
}

int Spatters::snowLevel(int x, int y, int z) {
    loadPages();
    return std::min(levelOf(x, y, z, SNOW), 5);
}

void Spatters::learn(int x, int y, int z, int32_t tp) {
    loadPages();
    const auto k = kind_of_picture_.find(tp);
    if (k == kind_of_picture_.end())
        return;
    const Top t = top(x, y, z);
    if (t.key)
        kind_of_key_[t.key] = k->second;
}

} // namespace fpv
