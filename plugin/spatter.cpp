#include "spatter.h"

#include <algorithm>
#include <string>

#include "modules/Maps.h"
#include "modules/Materials.h"

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

Spatters::Top Spatters::top(int x, int y, int z) {
    Top best;
    const df::map_block *block = Maps::getTileBlock(x, y, z);
    if (!block)
        return best;
    const int bx = x & 15, by = y & 15;
    for (df::block_square_event *ev : block->block_events) {
        if (auto *m = virtual_cast<df::block_square_event_material_spatterst>(ev)) {
            const int a = m->amount[bx][by];
            if (a > best.amount) {
                best.amount = a;
                best.key = (1ull << 63) | (uint64_t(uint8_t(m->mat_state)) << 48) |
                           (uint64_t(uint16_t(m->mat_type)) << 32) | uint32_t(m->mat_index);
                best.kind = kindOf(best.key, m->mat_type, m->mat_index, int(m->mat_state), -1, -1);
            }
        } else if (auto *it = virtual_cast<df::block_square_event_item_spatterst>(ev)) {
            const int a = it->amount[bx][by];
            if (a > best.amount) {
                best.amount = a;
                best.key = (1ull << 62) | (uint64_t(uint8_t(it->item_type)) << 48) |
                           (uint64_t(uint16_t(it->item_subtype)) << 32) | uint32_t(it->matindex);
                best.kind = kindOf(best.key, it->mattype, it->matindex, -1, int(it->item_type), it->item_subtype);
            }
        }
    }
    best.level = level_of(best.amount);
    return best;
}

int32_t Spatters::fromMap(int x, int y, int z) {
    loadPages();
    const Top t = top(x, y, z);
    if (!t.level || t.kind == NONE)
        return 0;
    const uint32_t h = tile_hash(x, y, z);
    if (scattered(t.kind))
        return picture(t.kind, int(h % 4), 4 - std::min(t.level, 4));
    if (t.level < 5)
        return picture(t.kind, 4 + int(h % 4), 4 - t.level);
    int sides = 0;
    const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    for (int i = 0; i < 4; ++i) {
        const Top n = top(x + dx[i], y + dy[i], z);
        if (n.level == 5 && n.kind == t.kind)
            sides |= 1 << i;
    }
    if (sides == 15) {
        const int8_t *v = FULL_INSIDE[h % 5];
        return picture(t.kind, v[0], v[1]);
    }
    return picture(t.kind, FULL[sides][0], FULL[sides][1]);
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
