#include "scene.h"

#include <map>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cmath>

#include "TileTypes.h"

#include "modules/MapCache.h"
#include "modules/Buildings.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
#include "modules/Units.h"

#include "df/descriptor_color.h"
#include "df/global_objects.h"
#include "df/graphic.h"
#include "df/graphic_viewportst.h"
#include "df/material.h"
#include "df/matter_state.h"
#include "df/plant.h"
#include "df/plant_raw.h"
#include "df/pmd_tree_texture_infost.h"
#include "df/building.h"
#include "df/building_type.h"
#include "df/building_actual.h"
#include "df/buildingitemst.h"
#include "df/building_item_role_type.h"
#include "df/item.h"
#include "df/item_ammost.h"
#include "df/item_amuletst.h"
#include "df/item_armorst.h"
#include "df/item_braceletst.h"
#include "df/item_clothst.h"
#include "df/item_coinst.h"
#include "df/item_corpsest.h"
#include "df/item_corpsepiecest.h"
#include "df/item_remainsst.h"
#include "df/creature_raw.h"
#include "df/descriptor_shape.h"
#include "df/item_statue_graphics_infost.h"
#include "df/itemdef.h"
#include "df/caste_raw.h"
#include "df/caste_body_info.h"
#include "df/body_part_raw.h"
#include "df/body_part_raw_flags.h"
#include "df/creature_raw_graphics.h"
#include "df/creature_graphics_role.h"
#include "df/creature_small_texture_type.h"
#include "df/texture_handlerst.h"
#include "df/tile_pagest.h"
#include "df/item_bucket_graphics_infost.h"
#include "df/item_craft_graphics_infost.h"
#include "df/item_crownst.h"
#include "df/item_earringst.h"
#include "df/item_figurinest.h"
#include "df/item_glovesst.h"
#include "df/item_helmst.h"
#include "df/item_pantsst.h"
#include "df/item_ringst.h"
#include "df/item_scepterst.h"
#include "df/item_shieldst.h"
#include "df/item_shoesst.h"
#include "df/item_slabst.h"
#include "df/slab_engraving_type.h"
#include "df/item_toolst.h"
#include "df/item_toyst.h"
#include "df/item_trapcompst.h"
#include "df/item_weaponst.h"
#include "df/item_cloth_graphics_infost.h"
#include "df/itemdef_ammost.h"
#include "df/itemdef_armorst.h"
#include "df/itemdef_glovesst.h"
#include "df/itemdef_handlerst.h"
#include "df/itemdef_helmst.h"
#include "df/itemdef_pantsst.h"
#include "df/itemdef_shieldst.h"
#include "df/itemdef_shoesst.h"
#include "df/itemdef_toolst.h"
#include "df/itemdef_toyst.h"
#include "df/itemdef_trapcompst.h"
#include "df/itemdef_weaponst.h"
#include "df/map_block.h"
#include "df/material_flags.h"
#include "df/tile_building_occ.h"
#include "df/tile_designation.h"
#include "df/tile_liquid.h"
#include "df/tile_occupancy.h"
#include "df/tiletype.h"
#include "df/tiletype_material.h"
#include "df/tiletype_shape.h"
#include "df/tiletype_shape_basic.h"
#include "df/tiletype_special.h"
#include "df/unit.h"
#include "df/unit_action.h"
#include "df/unit_action_type.h"
#include "df/unit_relationship_type.h"
#include "df/world.h"

using namespace DFHack;
using df::global::gps;
using df::global::window_x;
using df::global::window_y;
using df::global::world;

namespace fpv {

namespace {

enum class TileClass { Wall, Fort, Floor, Open, Ramp, Canopy };

// height of raised surfaces, in sixteenths of a level
constexpr uint8_t FORT_HEIGHT = 3;     // a fortification is a low sill you look over ...
constexpr float FORT_PEG = 0.50f;      // ... with a block standing on each corner, this high (tile widths)

// palm fronds: how many tiles out from the crown they reach into, their length
// from the trunk's edge, and the height they start at (just under the top of the
// crown's level, in tile widths)
constexpr int FROND_REACH = 3;
constexpr float FROND_LEN = 3.0f;
constexpr float FROND_TOP = 1.12f;

// shapes made of boxes
enum Shape {
    SH_TABLE, SH_STOOL, SH_BED, SH_CHEST, SH_CABINET, SH_COFFIN, SH_STATUE, SH_STAND, SH_RACK, SH_SLAB,
    SH_HATCH, SH_WELL, SH_PEDESTAL, SH_PILLAR, SH_FORT, SH_BOOKCASE, SH_FROND_HUB, SH_FROND_ARM,
    SH_SIGN, SH_ALTAR
};

// which shape a piece of furniture gets; -1 = none (it stays a flat picture)
int furniture_shape(df::building_type type) {
    typedef df::building_type T;
    switch (type) {
    case T::Table: return SH_TABLE;
    case T::Chair: return SH_STOOL;
    case T::Bed: return SH_BED;
    case T::Box: return SH_CHEST;
    case T::Cabinet: return SH_CABINET;
    case T::Bookcase: return SH_BOOKCASE;
    case T::Coffin: return SH_COFFIN;
    case T::Statue: return SH_STATUE;
    case T::Armorstand: return SH_STAND;
    case T::Weaponrack: return SH_RACK;
    case T::Slab: return SH_SLAB;
    case T::Hatch: case T::GrateFloor: case T::BarsFloor: return SH_HATCH;
    case T::Well: return SH_WELL;
    case T::DisplayFurniture: return SH_PEDESTAL;
    case T::Support: return SH_PILLAR;
    case T::OfferingPlace: return SH_ALTAR;
    default: return -1;
    }
}

// a slab engraved as a shop or tavern sign (the game hangs these by doors)
bool is_shop_sign(df::building *bld) {
    auto *actual = virtual_cast<df::building_actual>(bld);
    if (!actual)
        return false;
    for (const df::buildingitemst *bi : actual->contained_items)
        if (auto *slab = bi ? virtual_cast<df::item_slabst>(bi->item) : nullptr) {
            typedef df::slab_engraving_type E;
            return slab->engraving_type >= E::FoodImportsSign && slab->engraving_type <= E::TavernSign;
        }
    return false;
}
constexpr uint8_t WATER_HEIGHT = 12;

constexpr uint32_t MAT_UNKNOWN = 0x7FFFFFFFu;   // not looked up yet
constexpr uint32_t MAT_TREE = 0x80000000u;      // | index of the plant kind

enum Use { USE_WALL, USE_FLOOR, USE_LEAVES, USE_FURNITURE };

bool is_tree(df::tiletype tt) {
    const df::tiletype_material m = tileMaterial(tt);
    return m == df::tiletype_material::TREE || m == df::tiletype_material::MUSHROOM;
}

TileClass classify(df::tiletype tt) {
    const df::tiletype_shape shape = tileShape(tt);
    if (shape == df::tiletype_shape::FORTIFICATION)
        return TileClass::Fort;
    if (is_tree(tt) && (shape == df::tiletype_shape::BRANCH || shape == df::tiletype_shape::TRUNK_BRANCH ||
                        shape == df::tiletype_shape::TWIG)) {
        // the bare twigs of a dead tree are not worth a box
        if (shape == df::tiletype_shape::TWIG && tileSpecial(tt) == df::tiletype_special::DEAD)
            return TileClass::Open;
        return TileClass::Canopy;
    }
    // the sloping top of a trunk is still trunk: a block of bark, not a slope
    if (shape == df::tiletype_shape::RAMP && tileMaterial(tt) == df::tiletype_material::TREE)
        return TileClass::Wall;
    const df::tiletype_shape_basic basic = tileShapeBasic(shape);
    if (basic == df::tiletype_shape_basic::Wall)
        return TileClass::Wall;
    if (basic == df::tiletype_shape_basic::Ramp)
        return TileClass::Ramp;
    if (basic == df::tiletype_shape_basic::Open || basic == df::tiletype_shape_basic::None)
        return TileClass::Open;
    return TileClass::Floor;
}

bool is_wall(int x, int y, int z) {
    const df::tiletype *tt = Maps::getTileType(x, y, z);
    return tt && classify(*tt) == TileClass::Wall;
}

// A slope rises towards the walls next to it: a corner is up if any of the
// three tiles touching that corner is a wall. The top of those walls is the
// floor of the level above, which the slope then meets.
uint8_t ramp_corners(int x, int y, int z) {
    const bool n = is_wall(x, y - 1, z), s = is_wall(x, y + 1, z);
    const bool w = is_wall(x - 1, y, z), e = is_wall(x + 1, y, z);
    uint8_t r = RAMP;
    if (n || w || is_wall(x - 1, y - 1, z)) r |= RAMP_NW;
    if (n || e || is_wall(x + 1, y - 1, z)) r |= RAMP_NE;
    if (s || w || is_wall(x - 1, y + 1, z)) r |= RAMP_SW;
    if (s || e || is_wall(x + 1, y + 1, z)) r |= RAMP_SE;
    // An edge that is up at both ends although no wall is beyond it (a door
    // in a wall line, a fortification, open ground: the walls beside it lift
    // its corners) would stand as a wall hiding what is there. The slope dips
    // in the middle instead, as a channel running through towards that side.
    const bool up_n = (r & RAMP_NW) && (r & RAMP_NE), up_s = (r & RAMP_SW) && (r & RAMP_SE);
    const bool up_w = (r & RAMP_NW) && (r & RAMP_SW), up_e = (r & RAMP_NE) && (r & RAMP_SE);
    if ((!n && up_n) || (!s && up_s))
        r |= RAMP_VALLEY_U;
    else if ((!w && up_w) || (!e && up_e))
        r |= RAMP_VALLEY_V;
    return r;
}

// What is over a tile: the sky, something solid, or (0) open space inside,
// such as the hole above a ramp, through which the levels above are seen.
uint8_t cover_flags(int x, int y, int z) {
    const df::tile_designation *des = Maps::getTileDesignation(x, y, z);
    if (!des)
        return 0;
    if (des->bits.outside)
        return CELL_SKY;
    const df::tiletype *above = Maps::getTileType(x, y, z + 1);
    if (above && classify(*above) == TileClass::Open)
        return 0;
    return CELL_ROOFED;
}

Pattern wall_pattern(df::tiletype tt, bool wood) {
    switch (tileMaterial(tt)) {
    case df::tiletype_material::CONSTRUCTION:
        return wood ? Pattern::Planks : Pattern::Blocks;
    case df::tiletype_material::SOIL:
    case df::tiletype_material::GRASS_LIGHT:
    case df::tiletype_material::GRASS_DARK:
    case df::tiletype_material::GRASS_DRY:
    case df::tiletype_material::GRASS_DEAD:
        return Pattern::Soil;
    case df::tiletype_material::FROZEN_LIQUID:
        return Pattern::Ice;
    case df::tiletype_material::TREE:
    case df::tiletype_material::MUSHROOM:
        return Pattern::Bark;
    default:
        return tileSpecial(tt) == df::tiletype_special::SMOOTH ? Pattern::Smooth : Pattern::Rough;
    }
}

Pattern floor_pattern(df::tiletype tt, bool wood) {
    switch (tileMaterial(tt)) {
    case df::tiletype_material::CONSTRUCTION:
        return wood ? Pattern::Planks : Pattern::Smooth;
    case df::tiletype_material::GRASS_LIGHT:
    case df::tiletype_material::GRASS_DARK:
    case df::tiletype_material::GRASS_DRY:
    case df::tiletype_material::GRASS_DEAD:
        return Pattern::Grass;
    case df::tiletype_material::SOIL:
        return Pattern::Soil;
    case df::tiletype_material::FROZEN_LIQUID:
        return Pattern::Ice;
    case df::tiletype_material::TREE:
    case df::tiletype_material::MUSHROOM:
        return Pattern::Bark;
    default:
        return tileSpecial(tt) == df::tiletype_special::SMOOTH ? Pattern::Smooth : Pattern::Rough;
    }
}

inline uint64_t color_key(int use, int tile_material, uint32_t mat) {
    return (uint64_t(use) << 56) | (uint64_t(uint16_t(tile_material)) << 32) | mat;
}

// The game's picture of an item as it lies about, and whether it is a piece of
// equipment (drawn bigger on a table than trinkets); texpos 0 if we know none.
struct ItemIcon {
    int32_t texpos = 0;
    bool big = false;
    bool tint = false;   // a grey picture the game draws in the colour of the item's material
};

// game pictures looked up once per world (Scene::reset forgets them: texture
// numbers change when a world loads); -1 = not looked up yet
int32_t coin_tex[5] = {-1, -1, -1, -1, -1};
int32_t bucket_tex = -1;

// a tile of one of the game's graphics pages, by the page's name
int32_t page_tile(const char *token, int x, int y) {
    if (!df::global::texture)
        return 0;
    for (const df::tile_pagest *page : df::global::texture->page) {
        if (!page || page->token != token)
            continue;
        const size_t i = size_t(y) * page->page_dim_x + x;
        return i < page->texpos.size() ? int32_t(page->texpos[i]) : 0;
    }
    return 0;
}

// Every statue picture the game has, plain (grey) and in two halves: of each
// creature and caste (the creatures_*_statues.png sheets, modded creatures
// too), of each shape, of each kind of item. A placed statue's upper half is
// drawn as a copy of one of these tinted in its material; the tint leaves its
// outline alone, so the outline tells which one it is, and so its lower half.
namespace {
uint64_t outline_key(const uint32_t *px) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < TEXPIX; i += 8) {
        uint8_t bits = 0;
        for (int k = 0; k < 8; ++k)
            if (px_a(px[i + k]) >= 128)
                bits |= uint8_t(1 << k);
        h = (h ^ bits) * 1099511628211ull;
    }
    return h;
}

struct StatueHalves {
    int32_t top = 0, bottom = 0;
};

const std::unordered_map<uint64_t, StatueHalves> &statue_index() {
    static std::unordered_map<uint64_t, StatueHalves> index;
    static const void *built_for = nullptr;
    static size_t built_n = 0;
    df::world *w = df::global::world;
    if (!w)
        return index;
    const void *key = w->raws.creatures.all.empty() ? nullptr : w->raws.creatures.all[0];
    const size_t n = w->raws.creatures.all.size() + w->raws.itemdefs.all.size() * 7 +
                     (df::global::texture ? df::global::texture->page.size() * 131 : 0);
    if (key == built_for && n == built_n)
        return index;
    built_for = key;
    built_n = n;
    index.clear();
    const auto add = [&](int32_t top, int32_t bottom) {
        if (top <= 0 || bottom <= 0)
            return;
        uint32_t raw[TEXPIX];
        if (!TextureStore::readSprite(top, raw))
            return;
        index.emplace(outline_key(raw), StatueHalves{top, bottom});
    };
    for (const df::creature_raw *cr : w->raws.creatures.all) {
        if (!cr)
            continue;
        for (const df::caste_raw *ca : cr->caste)
            if (ca)
                add(ca->statue_texpos[0], ca->statue_texpos[1]);
        add(cr->statue_texpos[0], cr->statue_texpos[1]);
    }
    for (const df::descriptor_shape *sh : w->raws.descriptors.shapes) {
        if (!sh)
            continue;
        add(sh->statue_tex_top, sh->statue_tex_bottom);
        for (size_t i = 0; i < sh->statue_adj_tex_top.size() && i < sh->statue_adj_tex_bottom.size(); ++i)
            add(sh->statue_adj_tex_top[i], sh->statue_adj_tex_bottom[i]);
    }
    for (const df::itemdef *d : w->raws.itemdefs.all)
        if (d)
            add(d->statue_texpos_top, d->statue_texpos_bottom);
    const size_t types = sizeof(w->raws.itemdefs.statue_texpos_top) / sizeof(w->raws.itemdefs.statue_texpos_top[0]);
    for (size_t i = 0; i < types; ++i)
        add(w->raws.itemdefs.statue_texpos_top[i], w->raws.itemdefs.statue_texpos_bottom[i]);
    for (const df::item_statue_graphics_infost *g : w->raws.itemdefs.statue_graphics_info)
        if (g)
            add(g->texpos_top, g->texpos_bottom);
    return index;
}
} // namespace

// The whole figure of a placed statue whose upper half the game drew as
// drawn_top: that half as drawn over the plain lower half of the same picture,
// coloured like it. Null if the picture is not one of the game's.
std::shared_ptr<const Sprite> statue_figure(int32_t drawn_top) {
    static std::unordered_map<int32_t, std::shared_ptr<const Sprite>> cache;
    static const void *cache_for = nullptr;
    df::world *w = df::global::world;
    const void *wkey = w && !w->raws.creatures.all.empty() ? w->raws.creatures.all[0] : nullptr;
    if (wkey != cache_for) {
        cache.clear();
        cache_for = wkey;
    }
    const auto hit = cache.find(drawn_top);
    if (hit != cache.end())
        return hit->second;
    std::shared_ptr<const Sprite> result;
    uint32_t top[TEXPIX], plain[TEXPIX], bottom[TEXPIX];
    if (TextureStore::readSprite(drawn_top, top)) {
        const auto &index = statue_index();
        const auto found = index.find(outline_key(top));
        if (found != index.end() && TextureStore::readSprite(found->second.top, plain) &&
            TextureStore::readSprite(found->second.bottom, bottom)) {
            // the tint: the drawn half over the plain one, channel by channel
            double sd[3] = {0, 0, 0}, sp[3] = {0, 0, 0};
            for (int i = 0; i < TEXPIX; ++i)
                if (px_a(top[i]) >= 128 && px_a(plain[i]) >= 128) {
                    sd[0] += px_r(top[i]);
                    sd[1] += px_g(top[i]);
                    sd[2] += px_b(top[i]);
                    sp[0] += px_r(plain[i]);
                    sp[1] += px_g(plain[i]);
                    sp[2] += px_b(plain[i]);
                }
            double f[3];
            for (int c = 0; c < 3; ++c)
                f[c] = sp[c] > 0 ? std::min(4.0, sd[c] / sp[c]) : 1.0;
            auto fig = std::make_shared<Sprite>();
            fig->w = TEX;
            fig->h = 2 * TEX;
            fig->px.assign(size_t(TEX) * 2 * TEX, 0);
            for (int i = 0; i < TEXPIX; ++i) {
                if (px_a(top[i]) >= 128)
                    fig->px[size_t(i)] = top[i] | 0xFF000000u;
                if (px_a(bottom[i]) >= 128) {
                    const uint32_t c = bottom[i];
                    fig->px[size_t(TEXPIX + i)] = rgb(std::min(255, int(px_r(c) * f[0] + 0.5)),
                                                      std::min(255, int(px_g(c) * f[1] + 0.5)),
                                                      std::min(255, int(px_b(c) * f[2] + 0.5)));
                }
            }
            int x0 = TEX, y0 = 2 * TEX, x1 = 0, y1 = 0;
            for (int y = 0; y < 2 * TEX; ++y)
                for (int x = 0; x < TEX; ++x)
                    if (fig->px[size_t(y) * TEX + x]) {
                        x0 = std::min(x0, x);
                        x1 = std::max(x1, x + 1);
                        y0 = std::min(y0, y);
                        y1 = std::max(y1, y + 1);
                    }
            fig->x0 = x0;
            fig->y0 = y0;
            fig->x1 = x1;
            fig->y1 = y1;
            if (!fig->empty())
                result = fig;
        }
    }
    cache.emplace(drawn_top, result);
    return result;
}

// The game's status bubbles over creatures (UNIT_STATUS page: hungry,
// stunned, unconscious ...). The mod draws its own, one set per creature over
// its head (status_bubble), so the game's are left out wherever they turn up.
bool is_status_tile(int32_t tp) {
    static std::vector<int32_t> tiles;
    static const void *tiles_for = nullptr;
    if (tp <= 0 || !df::global::texture)
        return false;
    const df::tile_pagest *page = nullptr;
    for (const df::tile_pagest *pg : df::global::texture->page)
        if (pg && pg->token == "UNIT_STATUS") {
            page = pg;
            break;
        }
    if (!page)
        return false;
    if (tiles_for != page || tiles.size() != page->texpos.size() + page->texpos_gs.size()) {
        tiles.clear();
        for (const auto *tps : {&page->texpos, &page->texpos_gs})
            for (const auto t : *tps)
                tiles.push_back(int32_t(t));
        tiles_for = page;
    }
    return std::find(tiles.begin(), tiles.end(), tp) != tiles.end();
}

// The rows of the game's UNIT_STATUS page (graphics_interface.txt)
enum StatusRow {
    ST_HUNGRY = 3, ST_THIRSTY = 4, ST_DROWSY = 5, ST_SLEEPING = 8, ST_WRESTLING = 23, ST_MINOR_INJURY = 24,
    ST_MAJOR_INJURY = 25, ST_PARALYZED = 26, ST_STUNNED = 27, ST_NAUSEA = 28, ST_WINDED = 29,
    ST_UNCONSCIOUS = 30, ST_FEVERED = 31, ST_PROJECTILE = 37, ST_GROUNDED = 38, ST_WEBBED = 39, ST_CLIMBING = 40,
};

// What a creature's bubble shows: its states, most pressing first, taking
// turns (one each 1.2 seconds). 0 if none.
int32_t status_bubble(df::unit *u, double now_s) {
    int rows[16];
    int n = 0;
    const auto add = [&](int row) {
        if (n < 16)
            rows[n++] = row;
    };
    if (u->counters.unconscious > 0)
        add(ST_UNCONSCIOUS);
    if (u->counters2.paralysis > 0)
        add(ST_PARALYZED);
    if (u->counters.stunned > 0)
        add(ST_STUNNED);
    if (u->counters.webbed > 0)
        add(ST_WEBBED);
    if (u->flags1.bits.projectile)
        add(ST_PROJECTILE);
    bool wrestling = false, climbing = false;
    for (const df::unit_action *a : u->actions) {
        if (!a)
            continue;
        if (a->type == df::unit_action_type::Climb || a->type == df::unit_action_type::HoldTerrain)
            climbing = true;
        if (a->type == df::unit_action_type::Attack &&
            a->data.attack.wrestle_info.wrestle_type != df::dungeon_wrestle_type::NONE)
            wrestling = true;
    }
    if (wrestling)
        add(ST_WRESTLING);
    if (climbing)
        add(ST_CLIMBING);
    if (u->counters.winded > 0)
        add(ST_WINDED);
    if (u->counters.nausea > 0)
        add(ST_NAUSEA);
    if (u->body.blood_max > 0 && u->body.blood_count < u->body.blood_max * 3 / 4)
        add(ST_MAJOR_INJURY);
    else if (!u->body.wounds.empty())
        add(ST_MINOR_INJURY);
    if (u->flags1.bits.on_ground && !u->counters.unconscious)
        add(ST_GROUNDED);
    if (u->counters2.fever > 0)
        add(ST_FEVERED);
    if (u->counters2.hunger_timer > 75000)
        add(ST_HUNGRY);
    if (u->counters2.thirst_timer > 50000)
        add(ST_THIRSTY);
    if (u->counters2.sleepiness_timer > 57600)
        add(ST_DROWSY);
    if (n == 0)
        return 0;
    const int pick = int(std::fmod(now_s / 1.2, double(n)));
    return page_tile("UNIT_STATUS", 0, rows[std::clamp(pick, 0, n - 1)]);
}

// The pedestal under a placed statue: the game's STATUES page has them in its
// first three rows (by material and quality); the figures are elsewhere.
bool statue_pedestal(int32_t tp) {
    if (tp <= 0 || !df::global::texture)
        return false;
    for (const df::tile_pagest *page : df::global::texture->page) {
        if (!page || page->token != "STATUES")
            continue;
        // (and their grey copies, drawn for what is out of sight)
        for (const auto *tps : {&page->texpos, &page->texpos_gs}) {
            const size_t rows = std::min<size_t>(3 * size_t(page->page_dim_x), tps->size());
            for (size_t i = 0; i < rows; ++i)
                if (int32_t((*tps)[i]) == tp)
                    return true;
        }
        return false;
    }
    return false;
}

ItemIcon item_icon(df::item *item) {
    if (!item || !world)
        return {};
    MaterialInfo mi(item);
    const bool wood = mi.isValid() && mi.material && mi.material->flags.is_set(df::material_flags::WOOD);
    const bool metal = mi.isValid() && mi.material && mi.material->flags.is_set(df::material_flags::IS_METAL);
    // trinkets: the game has a few drawings per kind, for metal and for wood
    const auto craft = [&](const std::vector<df::item_craft_graphics_infost *> &list) -> ItemIcon {
        const int want = wood ? int(df::item_craft_graphics_flag_material::WOOD)
                              : int(df::item_craft_graphics_flag_material::METAL);
        int32_t first = 0;
        for (const df::item_craft_graphics_infost *g : list) {
            if (!g || !g->texpos)
                continue;
            if (!first)
                first = g->texpos;
            if (int(g->flags.bits.material) == want)
                return {g->texpos, false};
        }
        return {first, false};
    };
    const auto &defs = world->raws.itemdefs;
    switch (item->getType()) {
    case df::item_type::WEAPON:
        if (auto def = virtual_cast<df::item_weaponst>(item)->subtype) {
            if (wood && def->texpos_item_wood) return {def->texpos_item_wood, true};
            if (def->texpos_item_default) return {def->texpos_item_default, true};
            if (def->texpos_item_material) return {def->texpos_item_material, true};
            return {def->texpos_item_wood, true};
        }
        break;
    case df::item_type::SHIELD:
        if (auto def = virtual_cast<df::item_shieldst>(item)->subtype)
            return {wood && def->texpos_item_wooden ? def->texpos_item_wooden
                                                    : (def->texpos_item ? def->texpos_item : def->texpos_item_wooden),
                    true};
        break;
    case df::item_type::TOOL:
        if (auto def = virtual_cast<df::item_toolst>(item)->subtype)
            return {def->texpos_item, true};
        break;
    case df::item_type::ARMOR:
        if (auto def = virtual_cast<df::item_armorst>(item)->subtype)
            return {def->texpos_item, true};
        break;
    case df::item_type::HELM:
        if (auto def = virtual_cast<df::item_helmst>(item)->subtype)
            return {def->texpos_item, true};
        break;
    case df::item_type::GLOVES:
        if (auto def = virtual_cast<df::item_glovesst>(item)->subtype)
            return {def->texpos_item, true};
        break;
    case df::item_type::SHOES:
        if (auto def = virtual_cast<df::item_shoesst>(item)->subtype)
            return {metal && def->texpos_item_metal ? def->texpos_item_metal : def->texpos_item, true};
        break;
    case df::item_type::PANTS:
        if (auto def = virtual_cast<df::item_pantsst>(item)->subtype)
            return {def->texpos_item, true};
        break;
    case df::item_type::AMMO:
        if (auto def = virtual_cast<df::item_ammost>(item)->subtype)
            return {wood && def->texpos_wood_ne ? def->texpos_wood_ne : def->texpos_default_ne, true};
        break;
    case df::item_type::TRAPCOMP:
        if (auto def = virtual_cast<df::item_trapcompst>(item)->subtype)
            return {def->texpos_item, true};
        break;
    case df::item_type::TOY:
        if (auto def = virtual_cast<df::item_toyst>(item)->subtype) {
            int32_t t = 0;
            if (wood) t = def->texpos_item_wood;
            else if (metal) t = def->texpos_item_metal;
            else if (mi.isValid() && mi.material && mi.material->flags.is_set(df::material_flags::IS_GLASS))
                t = def->texpos_item_glass;
            else if (mi.isValid() && mi.isInorganic()) t = def->texpos_item_stone;
            return {t ? t : def->texpos_item, false};
        }
        break;
    case df::item_type::FIGURINE: return craft(defs.figurine_graphics_info);
    case df::item_type::AMULET: return craft(defs.amulet_graphics_info);
    case df::item_type::SCEPTER: return craft(defs.scepter_graphics_info);
    case df::item_type::CROWN: return craft(defs.crown_graphics_info);
    case df::item_type::RING: return craft(defs.ring_graphics_info);
    case df::item_type::BRACELET: return craft(defs.bracelet_graphics_info);
    case df::item_type::EARRING: return craft(defs.earring_graphics_info);
    case df::item_type::COIN: {
        // one coin, or a heap the bigger the more there are
        const int n = item->getStackSize();
        const int col = n <= 1 ? 0 : n < 10 ? 1 : n < 50 ? 2 : n < 200 ? 3 : 4;
        if (coin_tex[col] < 0)
            coin_tex[col] = page_tile("ITEM_COINS", col, 0);
        ItemIcon icon{coin_tex[col], false, true};
        return icon;
    }
    case df::item_type::CLOTH:
        for (const df::item_cloth_graphics_infost *g : defs.cloth_graphics_info)
            if (g && g->texpos)
                return {g->texpos, false};
        break;
    default:
        break;
    }
    return {};
}

// The bloodied lumps of the game's BODYPARTS page (3 large, 3 small); -1 = not looked up yet
int32_t bodypart_tex[6] = {-1, -1, -1, -1, -1, -1};

bool is_body_item(df::item *item) {
    const df::item_type t = item->getType();
    return t == df::item_type::CORPSE || t == df::item_type::CORPSEPIECE || t == df::item_type::REMAINS;
}

// A corpse, a piece of one or vermin remains: the game's picture of it and how
// big to draw it. texpos 0 if none is known.
struct BodyIcon {
    int32_t texpos = 0;
    float scale = 0.5f;
    bool upside_down = false;   // the creature's standing picture, turned over (a dead animal on its back)
};

// how many legs it stands on
int stance_legs(const df::caste_raw *ca) {
    int n = 0;
    if (ca)
        for (const df::body_part_raw *part : ca->body_info.body_parts)
            if (part && part->flags.is_set(df::body_part_raw_flags::STANCE))
                ++n;
    return n;
}

// a picture turned upside down (half a turn)
std::shared_ptr<const Sprite> upside_down(int32_t texpos, const std::shared_ptr<const Sprite> &upright) {
    static std::unordered_map<int32_t, std::shared_ptr<const Sprite>> cache;
    if (!upright)
        return nullptr;
    if (texpos) {
        const auto found = cache.find(texpos);
        if (found != cache.end())
            return found->second;
    }
    std::shared_ptr<const Sprite> turned;
    if (const std::shared_ptr<Sprite> quarter = TextureStore::rotated(*upright))
        turned = TextureStore::rotated(*quarter);
    if (texpos) {
        if (cache.size() > 2000)
            cache.clear();
        cache.emplace(texpos, turned);
    }
    return turned;
}

BodyIcon body_icon(df::item *item) {
    BodyIcon out;
    if (!item || !world)
        return out;
    const auto creature = [&](int race) -> df::creature_raw * {
        return race >= 0 && size_t(race) < world->raws.creatures.all.size() ? world->raws.creatures.all[race] : nullptr;
    };
    // a size in body volume as a drawing scale (7000 is about a human)
    const auto size_scale = [](int size, float lo, float hi) {
        return size > 0 ? std::clamp(std::cbrt(float(size) / 7000.0f), lo, hi) : lo;
    };
    switch (item->getType()) {
    case df::item_type::CORPSE: {
        auto *c = virtual_cast<df::item_corpsest>(item);
        if (!c)
            break;
        // the game composes a picture of a layered creature's body (dwarves and other peoples)
        out.texpos = c->texpos[0][0];
        if (!out.texpos)
            if (df::creature_raw *cr = creature(c->race)) {
                df::caste_raw *ca = c->caste >= 0 && size_t(c->caste) < cr->caste.size() ? cr->caste[c->caste] : nullptr;
                const auto picture = [&](df::creature_graphics_role r) {
                    const int role = int(r);
                    int32_t tp = 0;
                    if (ca && ca->caste_graphics)
                        tp = ca->caste_graphics->creature_texture_texpos[0][role][0][0];
                    if (!tp && cr->graphics)
                        tp = cr->graphics->creature_texture_texpos[0][role][0][0];
                    return tp;
                };
                // an animal (not on two legs) lies on its back, legs up: its
                // standing picture upside down (the game's corpse picture is
                // only turned a quarter, onto its side)
                if (stance_legs(ca) != 2) {
                    out.texpos = picture(df::creature_graphics_role::DEFAULT);
                    out.upside_down = out.texpos != 0;
                }
                if (!out.texpos)
                    out.texpos = picture(df::creature_graphics_role::CORPSE);
            }
        out.scale = size_scale(c->body.size_info.size_cur, 0.4f, 1.7f);
        break;
    }
    case df::item_type::CORPSEPIECE: {
        auto *c = virtual_cast<df::item_corpsepiecest>(item);
        if (!c)
            break;
        const int size = c->body.size_info.size_cur;
        const int k = (size >= 1000 ? 0 : 3) + int(uint32_t(item->id) % 3);
        if (bodypart_tex[k] < 0)
            bodypart_tex[k] = page_tile("BODYPARTS", k % 3, k < 3 ? 21 : 22);
        out.texpos = bodypart_tex[k];
        out.scale = size_scale(size, 0.35f, 0.8f);
        break;
    }
    case df::item_type::REMAINS: {
        auto *r = virtual_cast<df::item_remainsst>(item);
        if (df::creature_raw *cr = r ? creature(r->race) : nullptr)
            if (cr->graphics)
                out.texpos = cr->graphics->creature_small_texpos[int(df::creature_small_texture_type::REMAINS)];
        out.scale = 0.35f;
        break;
    }
    default:
        break;
    }
    return out;
}

// How big a creature is drawn relative to its sprite. DF sprites are all one
// tile, whatever the creature; body volume tells them apart. 7000 is about a
// human.
float creature_scale(const df::unit *unit) {
    const int size = unit->body.size_info.size_cur;
    if (size <= 0)
        return 1.0f;
    return std::clamp(std::cbrt(float(size) / 7000.0f), 0.4f, 1.7f);
}

} // namespace

uint16_t Scene::boxSet(int shape, uint16_t side, uint16_t picture, int facing, uint32_t color, bool wood) {
    const uint64_t key = (uint64_t(facing) << 56) | (uint64_t(shape) << 40) | (uint64_t(side) << 20) | uint64_t(picture);
    const auto it = boxset_index_.find(key);
    if (it != boxset_index_.end())
        return it->second;
    if (boxsets.empty())
        boxsets.emplace_back();     // 0 means "none"
    if (boxsets.size() >= 65000)
        return 0;
    BoxSet set;
    // a block of the thing's own material, or of another texture
    const auto add = [&](float u0, float v0, float u1, float v1, float z0, float z1, uint16_t other = 0) {
        if (set.n >= 16)
            return;
        Box &b = set.b[set.n++];
        b.u0 = u0;
        b.v0 = v0;
        b.u1 = u1;
        b.v1 = v1;
        b.z0 = z0;
        b.z1 = z1;
        b.side = other ? other : side;
        b.top = b.side;
    };
    // four legs inside the corners of a square from a to 1-a
    const auto legs = [&](float a, float thick, float height) {
        const float b = 1 - a;
        add(a, a, a + thick, a + thick, 0, height);
        add(b - thick, a, b, a + thick, 0, height);
        add(a, b - thick, a + thick, b, 0, height);
        add(b - thick, b - thick, b, b, 0, height);
    };
    switch (shape) {
    case SH_TABLE:
        add(0.05f, 0.05f, 0.95f, 0.95f, 0.27f, 0.32f);
        legs(0.10f, 0.09f, 0.27f);
        set.surface = 0.32f;
        break;
    case SH_STOOL:
        add(0.28f, 0.28f, 0.72f, 0.72f, 0.15f, 0.19f);
        legs(0.31f, 0.06f, 0.15f);
        set.surface = 0.19f;
        break;
    case SH_BED: {
        // a frame on short legs, a sheet over it and a pillow at the head
        const uint16_t sheet = tex.material(Pattern::Grit, rgb(206, 198, 180));
        const uint16_t pillow = tex.material(Pattern::Grit, rgb(238, 236, 228));
        add(0.10f, 0.04f, 0.90f, 0.96f, 0.08f, 0.15f);
        add(0.13f, 0.10f, 0.87f, 0.93f, 0.15f, 0.19f, sheet);
        add(0.24f, 0.13f, 0.76f, 0.30f, 0.19f, 0.23f, pillow);
        add(0.10f, 0.04f, 0.90f, 0.10f, 0.15f, 0.36f);      // headboard
        legs(0.10f, 0.08f, 0.08f);
        set.surface = 0.19f;
        break;
    }
    case SH_CHEST:
        add(0.20f, 0.28f, 0.80f, 0.72f, 0, 0.26f);
        set.surface = 0.26f;
        break;
    case SH_CABINET:
    case SH_BOOKCASE:
        // a plain block with its front painted (doors, or shelves of books).
        // Made with its back to the north wall, then turned to its own wall.
        add(0.10f, 0.04f, 0.90f, 0.50f, 0, shape == SH_CABINET ? 0.74f : 0.82f);
        set.b[0].face = picture;
        set.b[0].face_sides = FACE_S;
        break;
    case SH_FROND_HUB:
        // the crown of a palm: its tile is trunk; the set has no blocks and only
        // marks where fronds start (and holds their leaf texture for spreadFronds)
        set.frond_hub = true;
        set.b[0].side = side;
        break;
    case SH_FROND_ARM: {
        // The part of a palm frond that lies in this tile. picture: the frond's
        // direction from the crown (0..7: N NE E SE S SW W NW) + 8 * (ring - 1),
        // where ring is how many tiles out from the crown this tile is. A frond
        // starts at the edge of the trunk's top, rises a little, then droops,
        // narrowing to a point FROND_LEN tiles out. Made of short thin blocks
        // (diagonal ones as a staircase), cut to this tile.
        static const int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1}, DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        set.frond_arm = true;
        const int d = picture & 7, ring = 1 + picture / 8;
        const float dx = float(DX[d]), dy = float(DY[d]);
        const bool diag = DX[d] != 0 && DY[d] != 0;
        const float unit = diag ? 0.7071f : 1.0f;              // tiles across per tile along
        const float start = diag ? 0.7071f : 0.5f;             // the trunk's edge, from its middle
        const float ox = dx * ring - 0.5f, oy = dy * ring - 0.5f;   // this tile's corner from the crown's middle
        const int pieces = diag ? 27 : 18;     // at most 15 in one tile
        for (int i = 0; i < pieces; ++i) {
            const float ta = float(i) / pieces, tb = float(i + 1) / pieces, t = 0.5f * (ta + tb);
            const float sa = (start + FROND_LEN * ta) * unit, sb = (start + FROND_LEN * tb) * unit;
            // half its width: widest a third of the way out, pointed at the tip
            const float hw = 0.0225f + 0.1125f * (t < 0.3f ? 0.6f + 0.4f * t / 0.3f : (1 - t) / 0.7f);
            // the tip ends just above the crown level's floor
            const float zc = FROND_TOP + 0.15f * t - 1.05f * t * t;
            const auto span = [&](float dir, float &lo, float &hi) {
                if (dir == 0) {
                    lo = -hw;
                    hi = hw;
                } else {
                    lo = std::min(dir * sa, dir * sb) - (diag ? hw : 0);
                    hi = std::max(dir * sa, dir * sb) + (diag ? hw : 0);
                }
            };
            float u0, u1, v0, v1;
            span(dx, u0, u1);
            span(dy, v0, v1);
            u0 = std::max(0.0f, u0 - ox);
            u1 = std::min(1.0f, u1 - ox);
            v0 = std::max(0.0f, v0 - oy);
            v1 = std::min(1.0f, v1 - oy);
            if (u1 - u0 > 0.004f && v1 - v0 > 0.004f)
                add(u0, v0, u1, v1, zc - 0.025f, zc + 0.025f);
        }
        break;
    }
    case SH_COFFIN:
        add(0.22f, 0.04f, 0.78f, 0.96f, 0, 0.20f);
        set.surface = 0.20f;
        break;
    case SH_STATUE:
        // a low podium; the game's picture of the statue stands on it
        add(0.20f, 0.20f, 0.80f, 0.80f, 0, 0.16f);
        set.surface = 0.16f;
        break;
    case SH_ALTAR: {
        // a long block with a red cloth over the middle of its top, hanging a
        // little down the front and back
        const uint16_t cloth = tex.material(Pattern::Grit, rgb(150, 28, 34));
        add(0.10f, 0.26f, 0.90f, 0.74f, 0, 0.25f);
        add(0.38f, 0.25f, 0.62f, 0.75f, 0.25f, 0.27f, cloth);
        add(0.38f, 0.24f, 0.62f, 0.26f, 0.16f, 0.27f, cloth);
        add(0.38f, 0.74f, 0.62f, 0.76f, 0.16f, 0.27f, cloth);
        set.surface = 0.27f;
        break;
    }
    case SH_SIGN: {
        // a board hanging under a bar that sticks out from the wall at about the
        // top of a door, at the corner of the tile; the game's picture on both flat
        // sides. Made against the north wall, then turned (facing & 7: wall to the
        // N, S, W, E, 4 = none: on a post); facing >> 3: how high the ground under
        // it is, in sixteenths of a level.
        const float up = float(facing >> 3) * LEVEL_H / 16, bar = 0.98f + up;
        add(0.07f, 0.00f, 0.11f, 0.52f, bar, bar + 0.04f);
        // the board is the game's picture cut to its drawn part, in its proportions;
        // where the picture is empty you see through it
        float bw = 0.42f, bh = 0.33f;
        if (picture) {
            const float aspect = std::clamp(tex.get(picture).aspect, 0.25f, 4.0f);
            bw = aspect >= 1 ? 0.42f : 0.42f * aspect;
            bh = aspect >= 1 ? 0.42f / aspect : 0.42f;
        }
        add(0.08f, 0.06f, 0.10f, 0.06f + bw, bar - 0.01f - bh, bar - 0.01f);
        set.b[set.n - 1].face = picture;
        set.b[set.n - 1].face_sides = FACE_E | FACE_W;
        if ((facing & 7) == 4)
            add(0.06f, 0.00f, 0.12f, 0.06f, up, bar + 0.04f);
        for (int i = 0; i < set.n; ++i) {
            Box &b = set.b[i];
            const float u0 = b.u0, u1 = b.u1, v0 = b.v0, v1 = b.v1;
            switch (facing & 7) {
            case 1: b.v0 = 1 - v1; b.v1 = 1 - v0; break;
            case 2: b.u0 = v0; b.u1 = v1; b.v0 = u0; b.v1 = u1; break;
            case 3: b.u0 = 1 - v1; b.u1 = 1 - v0; b.v0 = u0; b.v1 = u1; break;
            default: break;
            }
            if ((facing & 7) == 2 || (facing & 7) == 3)
                b.face_sides = b.face_sides ? FACE_N | FACE_S : 0;
        }
        break;
    }
    case SH_STAND:
        add(0.36f, 0.36f, 0.64f, 0.64f, 0, 0.05f);
        add(0.46f, 0.46f, 0.54f, 0.54f, 0.05f, 0.62f);
        add(0.24f, 0.46f, 0.76f, 0.54f, 0.46f, 0.52f);
        break;
    case SH_RACK:
        add(0.12f, 0.44f, 0.20f, 0.56f, 0, 0.56f);
        add(0.80f, 0.44f, 0.88f, 0.56f, 0, 0.56f);
        add(0.12f, 0.46f, 0.88f, 0.54f, 0.20f, 0.25f);
        add(0.12f, 0.46f, 0.88f, 0.54f, 0.48f, 0.53f);
        break;
    case SH_SLAB:
        // an upright stone with the game's drawing of it on both flat sides
        add(0.22f, 0.44f, 0.78f, 0.56f, 0, 0.56f);
        set.b[set.n - 1].face = picture;
        set.b[set.n - 1].face_sides = FACE_N | FACE_S;
        break;
    case SH_HATCH:
        add(0.06f, 0.06f, 0.94f, 0.94f, 0, 0.04f);
        break;
    case SH_WELL: {
        // a square rim, a post on two sides with a bar across, and a rope from
        // the bar down to the bucket (picture: how far down, in hundredths of a
        // tile width)
        const uint16_t rope = tex.material(Pattern::Smooth, rgb(156, 118, 70));
        const float down = picture / 100.0f;
        // the rim fills the whole tile so the shaft's edges stay hidden
        add(0.00f, 0.00f, 1.00f, 0.12f, 0, 0.30f);
        add(0.00f, 0.88f, 1.00f, 1.00f, 0, 0.30f);
        add(0.00f, 0.12f, 0.12f, 0.88f, 0, 0.30f);
        add(0.88f, 0.12f, 1.00f, 0.88f, 0, 0.30f);
        add(0.02f, 0.45f, 0.10f, 0.55f, 0.30f, 0.98f);
        add(0.90f, 0.45f, 0.98f, 0.55f, 0.30f, 0.98f);
        add(0.02f, 0.47f, 0.98f, 0.53f, 0.90f, 0.96f);
        add(0.49f, 0.49f, 0.51f, 0.51f, std::min(0.85f, 0.22f - down), 0.90f, rope);
        break;
    }
    case SH_PEDESTAL:
        add(0.30f, 0.30f, 0.70f, 0.70f, 0, 0.30f);
        add(0.24f, 0.24f, 0.76f, 0.76f, 0.30f, 0.35f);
        set.surface = 0.35f;
        break;
    case SH_PILLAR:
        add(0.32f, 0.32f, 0.68f, 0.68f, 0, LEVEL_H);
        break;
    case SH_FORT: {
        // a block on each corner of the sill: lined up, neighbours' blocks
        // join into the teeth of a battlement, whichever way the wall runs
        const float sill = FORT_HEIGHT * LEVEL_H / 16, w = 0.27f;
        add(0, 0, w, w, sill, FORT_PEG);
        add(1 - w, 0, 1, w, sill, FORT_PEG);
        add(0, 1 - w, w, 1, sill, FORT_PEG);
        add(1 - w, 1 - w, 1, 1, sill, FORT_PEG);
        break;
    }
    default:
        return 0;
    }
    if (shape == SH_CABINET || shape == SH_BOOKCASE) {
        // made with the back to the north wall: turn it to stand against its own
        // wall (facing: back to N, S, W, E), or out in the room (4)
        for (int i = 0; i < set.n; ++i) {
            Box &b = set.b[i];
            const float u0 = b.u0, u1 = b.u1, v0 = b.v0, v1 = b.v1;
            switch (facing) {
            case 1: b.v0 = 1 - v1; b.v1 = 1 - v0; b.face_sides = b.face_sides ? FACE_N : 0; break;
            case 2: b.u0 = v0; b.u1 = v1; b.v0 = u0; b.v1 = u1; b.face_sides = b.face_sides ? FACE_E : 0; break;
            case 3: b.u0 = 1 - v1; b.u1 = 1 - v0; b.v0 = u0; b.v1 = u1; b.face_sides = b.face_sides ? FACE_W : 0; break;
            case 4: b.v0 = v0 + 0.23f; b.v1 = v1 + 0.23f; break;
            default: break;
            }
        }
    }
    boxsets.push_back(set);
    const uint16_t id = uint16_t(boxsets.size() - 1);
    boxset_index_[key] = id;
    return id;
}

uint16_t Scene::leaf_texture(uint32_t mat) {
    if (!(mat & MAT_TREE) || !world)
        return 0;
    const size_t kind = mat & 0xFFFF;
    const auto &all = world->raws.plants.all;
    if (kind < all.size() && all[kind] && all[kind]->tree_texture_info) {
        const df::pmd_tree_texture_infost &t = *all[kind]->tree_texture_info;
        for (const int32_t tp : {t.texpos_tree_twigs_full[0], t.texpos_tree_twigs_full[1],
                                 t.texpos_tree_twigs_full[2], t.texpos_overleaves[0], t.texpos_tree_twigs[0]})
            if (tp > 0)
                if (const uint16_t id = tex.floor(tp))
                    return id;
    }
    // most trees have no leaves of their own: they use the standard ones, a
    // block of full leaf tiles on the game's TREES page. Looked up every time
    // (once per kind of tree until reset): texture numbers change when a world
    // loads, and a number kept from before points at some other picture.
    const int32_t standard = page_tile("TREES", 1, 14);
    return standard > 0 ? tex.floor(standard) : 0;
}

bool Scene::isTreeSprite(int32_t texpos) {
    if (tree_sprites_.empty() && df::global::texture) {
        for (const df::tile_pagest *page : df::global::texture->page)
            if (page && page->token.rfind("TREE", 0) == 0)
                for (const long tp : page->texpos)
                    tree_sprites_.insert(int32_t(tp));
        tree_sprites_.insert(-1);   // looked once, even if there were none
    }
    return texpos && tree_sprites_.count(texpos) != 0;
}

void Scene::reset() {
    levels.clear();
    boxsets.clear();
    boxset_index_.clear();
    leaf_tex_.clear();
    spatters_.reset();
    tree_sprites_.clear();
    ground_kind_.clear();
    std::fill(std::begin(coin_tex), std::end(coin_tex), -1);
    bucket_tex = -1;
    ground_look_.clear();
    front_kind_.clear();
    glide_.clear();
    units.clear();
    tex.clear();
    sprite_cache_.clear();
    pile_cache_.clear();
    tint_cache_.clear();
    prone_cache_.clear();
    prone_large_.clear();
    std::fill(std::begin(bodypart_tex), std::end(bodypart_tex), -1);
    unit_seen_.clear();
    learned_color_.clear();
    mat_info_.clear();
    col_lo.clear();
    col_hi.clear();
    col_box.clear();
    w = h = d = 0;
    ++version;
}

bool Scene::resize() {
    int32_t x = 0, y = 0, z = 0;
    Maps::getTileSize(x, y, z);
    if (x <= 0 || y <= 0 || z <= 0)
        return false;
    if (x != w || y != h || z != d) {
        reset();
        w = x;
        h = y;
        d = z;
        levels.resize(d);
        col_lo.assign(size_t(w) * h, int16_t(32767));
        col_hi.assign(size_t(w) * h, int16_t(-1));
        col_box.assign(size_t(w) * h, 0);
    }
    return true;
}

std::shared_ptr<const Sprite> Scene::staticSprite(int32_t texpos) {
    const auto it = sprite_cache_.find(texpos);
    if (it != sprite_cache_.end())
        return it->second;
    const Piece piece{texpos, 0, 0};
    std::shared_ptr<const Sprite> s = TextureStore::composite(&piece, 1);
    sprite_cache_[texpos] = s;
    return s;
}

std::shared_ptr<const Sprite> Scene::tintedSprite(int32_t texpos, uint32_t color) {
    const uint64_t key = (uint64_t(uint32_t(texpos)) << 32) | color;
    const auto it = tint_cache_.find(key);
    if (it != tint_cache_.end())
        return it->second;
    std::shared_ptr<const Sprite> s = TextureStore::tinted(texpos, color);
    tint_cache_[key] = s;
    return s;
}

const Scene::MatInfo &Scene::matInfo(uint32_t mat) {
    const auto it = mat_info_.find(mat);
    if (it != mat_info_.end())
        return it->second;
    MatInfo info;
    if (!(mat & MAT_TREE) || (mat >> 16) == 0xFFFFu) {
        const MaterialInfo mi(int16_t(mat >> 16), int32_t(int16_t(mat & 0xFFFF)));
        if (mi.isValid()) {
            info.wood = mi.isPlant();
            const int ci = mi.material->state_color[df::matter_state::Solid];
            const auto &colors = world->raws.descriptors.colors;
            if (ci >= 0 && size_t(ci) < colors.size() && colors[ci]) {
                // keep clear of pure black and of glaring colours
                const auto channel = [](float v) { return int(26 + 190 * std::clamp(v, 0.0f, 1.0f)); };
                info.color = rgb(channel(colors[ci]->red), channel(colors[ci]->green), channel(colors[ci]->blue));
            }
        }
    }
    return mat_info_[mat] = info;
}

void Scene::learn(uint64_t key, uint32_t color) {
    const auto it = learned_color_.find(key);
    if (it == learned_color_.end()) {
        learned_color_[key] = color;
        return;
    }
    // sprites of one material differ a little from tile to tile; only a real
    // change (autumn leaves, say) replaces what is known
    const uint32_t old = it->second;
    const int diff = std::abs(px_r(old) - px_r(color)) + std::abs(px_g(old) - px_g(color)) +
                     std::abs(px_b(old) - px_b(color));
    if (diff > 72)
        it->second = color;
}

bool Scene::groundFits(uint64_t kind, uint32_t color, bool confirmed) {
    GroundLook &k = ground_look_[kind];
    if (!k.fits) {
        k.color = color;
        k.fits = 1;
        return true;
    }
    const int diff = std::abs(px_r(k.color) - px_r(color)) + std::abs(px_g(k.color) - px_g(color)) +
                     std::abs(px_b(k.color) - px_b(color));
    if (diff <= 60) {
        ++k.fits;
        return true;
    }
    if (!confirmed)
        return false;
    // the same odd picture read twice on a tile is what is there; if that
    // happens more often than the usual look, the first look was the odd one
    if (++k.odd > k.fits) {
        k.color = color;
        k.fits = 1;
        k.odd = 0;
    }
    return true;
}

uint32_t Scene::colorFor(int use, int tile_material, uint32_t mat) {
    const auto it = learned_color_.find(color_key(use, tile_material, mat));
    if (it != learned_color_.end())
        return it->second;
    switch (df::tiletype_material(tile_material)) {
    case df::tiletype_material::GRASS_LIGHT: return rgb(92, 132, 56);
    case df::tiletype_material::GRASS_DARK: return rgb(64, 106, 46);
    case df::tiletype_material::GRASS_DRY: return rgb(152, 140, 76);
    case df::tiletype_material::GRASS_DEAD: return rgb(122, 100, 68);
    case df::tiletype_material::TREE:
    case df::tiletype_material::MUSHROOM:
        return use == USE_LEAVES ? rgb(62, 112, 48) : rgb(112, 84, 54);
    case df::tiletype_material::FROZEN_LIQUID: return rgb(172, 206, 230);
    default: break;
    }
    if (const uint32_t color = matInfo(mat).color)
        return color;
    return df::tiletype_material(tile_material) == df::tiletype_material::SOIL ? rgb(120, 94, 64)
                                                                               : rgb(122, 120, 126);
}

void Scene::readLevel(int z, const int *rect) {
    if (!gps || !gps->main_viewport || z < 0 || z >= d)
        return;
    const bool from_map = rect != nullptr;
    const df::graphic_viewportst *vp = gps->main_viewport;
    const int cols = from_map ? rect[2] : vp->dim_x, rows = from_map ? rect[3] : vp->dim_y;
    if (cols < 1 || rows < 1 || !vp->screentexpos_background || !vp->screentexpos)
        return;
    const int wx = from_map ? rect[0] : *window_x, wy = from_map ? rect[1] : *window_y;
    const int32_t blank = int32_t(gps->black_background_texpos[0]);

    if (!levels[z]) {
        levels[z] = std::make_unique<Level>();
        levels[z]->cells.resize(size_t(w) * h);
        levels[z]->mats.assign(size_t(w) * h, MAT_UNKNOWN);
        levels[z]->known.assign(size_t(w) * h, 0);
        levels[z]->ground_seen.assign(size_t(w) * h, 0);
    }
    Level &L = *levels[z];

    // Only the part of the level that the viewport covers is replaced; the
    // rest was read through other map windows and stays. What stood on the
    // tiles before is set aside: tiles that are out of sight now keep it.
    std::unordered_multimap<int32_t, Billboard> before;
    if (!from_map) {
        const int rx0 = wx, ry0 = wy, rx1 = wx + cols, ry1 = wy + rows;
        const auto inside = [&](int x, int y) { return x >= rx0 && x < rx1 && y >= ry0 && y < ry1; };
        for (Billboard &b : L.bills)
            if (inside(b.mx, b.my))
                before.emplace(b.my * w + b.mx, b);
        std::erase_if(L.bills, [&](const Billboard &b) { return inside(b.mx, b.my); });
        std::erase_if(L.creatures, [&](const auto &entry) { return inside(entry.first % w, entry.first / w); });
    }

    // sprites of things that rarely change are kept for a while, then re-read
    if (++sprite_cache_age_ > 400) {
        sprite_cache_age_ = 0;
        sprite_cache_.clear();
    }

    tex.special(Special::Unknown);
    tex.special(Special::Roof);   // the renderer needs it to exist

    // Out of the adventurer's sight the game draws placeholder icons instead of
    // the real sprites. They carry nothing worth using.
    const auto memory = [&](int32_t texpos) { return texpos != 0 && tex.isMemoryIcon(texpos); };
    const auto real = [&](int32_t texpos) { return texpos != 0 && texpos != blank && !tex.isMemoryIcon(texpos); };

    const auto place = [&](std::shared_ptr<const Sprite> s, int32_t texpos, int mx, int my, BillKind kind,
                           uint8_t order, float raise) -> Billboard * {
        if (!s)
            return nullptr;
        Billboard b;
        b.x = mx + 0.5f;
        b.y = my + 0.5f;
        b.base = (z + raise) * LEVEL_H;
        b.sprite = std::move(s);
        b.mx = int16_t(mx);
        b.my = int16_t(my);
        b.mz = int16_t(z);
        b.kind = kind;
        b.order = order;
        b.texpos = texpos;
        switch (kind) {
        case BillKind::Boulder: b.sink = 0.45f; break;         // boulders sit half in the ground
        case BillKind::Item: b.scale = 0.525f; break;          // things lying about are small
        case BillKind::Plant: b.scale = 0.5f; break;           // shrubs and saplings are low
        case BillKind::Flying: b.base += 0.45f * LEVEL_H; break;
        default: break;
        }
        L.bills.push_back(std::move(b));
        return &L.bills.back();
    };
    const auto add_bill = [&](int32_t texpos, int mx, int my, BillKind kind, uint8_t order, float raise) {
        if (real(texpos) && !is_status_tile(texpos))
            place(staticSprite(texpos), texpos, mx, my, kind, order, raise);
    };
    // a statue standing in its own tile, not on a slope
    const auto statue_at = [&](int mx, int my) {
        const df::tile_occupancy *o = Maps::getTileOccupancy(mx, my, z);
        if (!o || o->bits.building == df::tile_building_occ::None)
            return false;
        const df::tiletype *t = Maps::getTileType(mx, my, z);
        if (!t || classify(*t) == TileClass::Ramp)
            return false;
        df::building *sb = Buildings::findAtTile(df::coord(int16_t(mx), int16_t(my), int16_t(z)));
        return sb && sb->getType() == df::building_type::Statue;
    };
    // The game draws a statue two tiles tall: the bottom half on its tile, the
    // top half on the tile north of it. Put together, it stands as one figure.
    // The game's pedestal under it is left out: it stands on our podium.
    // A pedestal is a separate picture that fills its whole tile (the figures,
    // even big ones, cover less than 70%), so other art's pedestals are known too.
    const auto pedestal = [&](int32_t tp) { return real(tp) && statue_pedestal(tp); };
    // The game draws a placed statue two tiles tall: the figure's upper half
    // in the signpost layer of the tile to the north, its lower half on its
    // own tile already joined with the pedestal, as one picture. The figure is
    // put together from the upper half and the plain lower half of the same
    // picture (statue_figure) and stands on our podium. A picture that is not
    // one of the game's statues shows its upper half alone, raised clear of
    // the podium.
    constexpr float STATUE_RAISE = 0.25f;   // tile widths above the podium's top, upper half alone
    constexpr float FIGURE_RAISE = -0.01f;  // the whole figure: 0.15 tile widths above the ground (the podium's top is at 0.16)
    const auto add_statue = [&](int32_t b1, int32_t b2, int32_t top, int mx, int my, float raise) {
        (void)b1;
        (void)b2;
        if (!real(top))
            return;
        if (std::shared_ptr<const Sprite> whole = statue_figure(top))
            place(whole, top, mx, my, BillKind::Building, 1, raise + FIGURE_RAISE / LEVEL_H);
        else
            place(staticSprite(top), top, mx, my, BillKind::Building, 1, raise + STATUE_RAISE / LEVEL_H);
    };
    // A well: its shape, and the bucket hanging in its shaft over the water
    // (or the bottom) below.
    const auto well_here = [&](Cell &c, int mx, int my, uint16_t side) {
        float bottom = z * LEVEL_H;
        for (int zz = z - 1; zz >= std::max(0, z - 12); --zz) {
            const df::tile_designation *des = Maps::getTileDesignation(mx, my, zz);
            const df::tiletype *t = Maps::getTileType(mx, my, zz);
            if (des && des->bits.flow_size > 0) {
                bottom = zz * LEVEL_H + des->bits.flow_size / 7.0f * LEVEL_H * 0.75f;
                break;
            }
            if (!t || classify(*t) != TileClass::Open) {
                bottom = (t && classify(*t) == TileClass::Wall ? zz + 1 : zz) * LEVEL_H;
                break;
            }
            bottom = zz * LEVEL_H;
        }
        const float down = std::clamp(z * LEVEL_H - bottom, 0.0f, 600.0f);
        c.box = boxSet(SH_WELL, side, uint16_t(down * 100));
        int32_t &bucket = bucket_tex;
        if (bucket < 0) {
            bucket = 0;
            for (const df::item_bucket_graphics_infost *g : world->raws.itemdefs.bucket_graphics_info)
                if (g && g->texpos) {
                    bucket = g->texpos;
                    break;
                }
        }
        if (bucket && !from_map)
            if (Billboard *b = place(staticSprite(bucket), bucket, mx, my, BillKind::Item, 3, bottom / LEVEL_H - z + 0.02f))
                b->scale = 0.42f;
    };
    // a well over open space (its shaft) is not on a floor
    const auto well_in_open = [&](Cell &c, int mx, int my, int32_t picture) {
        const df::tile_occupancy *occ = Maps::getTileOccupancy(mx, my, z);
        if (!occ || occ->bits.building == df::tile_building_occ::None)
            return;
        df::building *bld = Buildings::findAtTile(df::coord(int16_t(mx), int16_t(my), int16_t(z)));
        if (!bld || bld->getType() != df::building_type::Well)
            return;
        const uint32_t bmat = (uint32_t(uint16_t(bld->mat_type)) << 16) | uint32_t(uint16_t(bld->mat_index));
        const uint64_t key = color_key(USE_FURNITURE, int(df::building_type::Well), bmat);
        uint32_t seen;
        if (real(picture) && tex.mainColor(picture, seen))
            learn(key, seen);
        uint32_t color = matInfo(bmat).color;
        const auto learned = learned_color_.find(key);
        if (learned != learned_color_.end())
            color = learned->second;
        well_here(c, mx, my, tex.material(Pattern::Grit, color ? color : rgb(128, 124, 120)));
    };
    // the colour of what an item is made of (coins are grey pictures drawn in it)
    const auto item_color = [&](df::item *it) -> uint32_t {
        const uint32_t m = (uint32_t(uint16_t(it->getMaterial())) << 16) | uint32_t(uint16_t(it->getMaterialIndex()));
        const uint32_t c = matInfo(m).color;
        return c ? c : rgb(200, 200, 200);
    };
    // what lies on a tile, as a heap of small pictures of a few of the things
    // in it; false if fewer than two of them have a picture
    const auto add_pile = [&](int mx, int my, float raise) -> bool {
        const df::map_block *block = Maps::getTileBlock(mx, my, z);
        if (!block)
            return false;
        int32_t icons[5];
        uint32_t tints[5];
        int n = 0;
        for (const int32_t id : block->items) {
            df::item *it = df::item::find(id);
            if (!it || it->pos.x != mx || it->pos.y != my || it->pos.z != z || !it->flags.bits.on_ground)
                continue;
            const ItemIcon icon = item_icon(it);
            const uint32_t tint = icon.tint ? item_color(it) : 0;
            bool dup = false;
            for (int i = 0; i < n; ++i)
                dup = dup || (icons[i] == icon.texpos && tints[i] == tint);
            if (!icon.texpos || dup)
                continue;
            icons[n] = icon.texpos;
            tints[n++] = tint;
            if (n == 5)
                break;
        }
        if (n < 2)
            return false;
        uint64_t key = 0xcbf29ce484222325ull;
        for (int i = 0; i < n; ++i)
            key = (key ^ (uint64_t(uint32_t(icons[i])) << 32 | tints[i])) * 0x100000001b3ull;
        auto found = pile_cache_.find(key);
        if (found == pile_cache_.end())
            found = pile_cache_.emplace(key, TextureStore::pile(icons, tints, n)).first;
        Billboard *b = place(found->second, icons[0], mx, my, BillKind::Item, 2, raise);
        if (b) {
            b->scale = 0.7f;
            b->anchor_px = found->second->w * 0.5f;
        }
        return b != nullptr;
    };
    // What lies on the ground of a tile. Every corpse, piece of one and vermin
    // remains is drawn on its own, so none is lost under the heap; they and the
    // heap of the other things stand in a small ring, like creatures sharing a tile.
    // item_tp: the game's picture of the tile's items.
    const auto add_items = [&](int mx, int my, float raise, int32_t item_tp) {
        constexpr size_t MAX_BODIES = 16;
        const df::map_block *block = Maps::getTileBlock(mx, my, z);
        std::vector<BodyIcon> bodies;
        df::item *other = nullptr;   // the first other thing with a picture of its own
        int others = 0;
        if (block)
            for (const int32_t id : block->items) {
                df::item *it = df::item::find(id);
                if (!it || it->pos.x != mx || it->pos.y != my || it->pos.z != z || !it->flags.bits.on_ground)
                    continue;
                if (!is_body_item(it)) {
                    ++others;
                    if (!other && item_icon(it).texpos)
                        other = it;
                } else if (bodies.size() < MAX_BODIES) {
                    bodies.push_back(body_icon(it));
                }
            }
        if (bodies.empty()) {
            if (!add_pile(mx, my, raise))
                add_bill(item_tp, mx, my, BillKind::Item, 2, raise);
            return;
        }
        // the only thing on the tile: the game's own picture is of it
        if (bodies.size() == 1 && !others && real(item_tp) && !bodies[0].upside_down)
            bodies[0].texpos = item_tp;
        const size_t first = L.bills.size();
        uint8_t order = 2;
        for (const BodyIcon &body : bodies)
            if (real(body.texpos)) {
                std::shared_ptr<const Sprite> sp = staticSprite(body.texpos);
                if (body.upside_down)
                    sp = upside_down(body.texpos, sp);
                if (Billboard *b = place(sp, body.texpos, mx, my, BillKind::Item, order++, raise))
                    b->scale = body.scale;
            }
        if (others && !add_pile(mx, my, raise)) {
            if (other) {
                const ItemIcon icon = item_icon(other);
                place(icon.tint ? tintedSprite(icon.texpos, item_color(other)) : staticSprite(icon.texpos),
                      icon.texpos, mx, my, BillKind::Item, order, raise);
            } else if (std::none_of(bodies.begin(), bodies.end(),
                                    [&](const BodyIcon &body) { return body.texpos == item_tp; })) {
                add_bill(item_tp, mx, my, BillKind::Item, order, raise);
            }
        }
        const size_t n = L.bills.size() - first;
        if (n > 1)
            for (size_t i = 0; i < n; ++i) {
                const float a = 6.2831853f * float(i) / float(n);
                L.bills[first + i].x = mx + 0.5f + 0.3f * std::cos(a);
                L.bills[first + i].y = my + 0.5f + 0.3f * std::sin(a);
            }
    };
    // what is laid out on a table: small trinkets in rows, equipment bigger
    const auto add_on_table = [&](df::building *bld, int mx, int my, float raise) {
        auto *actual = virtual_cast<df::building_actual>(bld);
        if (!actual)
            return;
        std::vector<std::pair<ItemIcon, uint32_t>> things;   // with the tint of a grey picture
        for (const df::buildingitemst *bi : actual->contained_items) {
            if (!bi || !bi->item || bi->use_mode != df::building_item_role_type::TEMP)
                continue;
            const ItemIcon icon = item_icon(bi->item);
            if (icon.texpos)
                things.push_back({icon, icon.tint ? item_color(bi->item) : 0u});
        }
        const int n = int(std::min<size_t>(things.size(), 16));
        if (!n)
            return;
        const int cols = int(std::ceil(std::sqrt(float(n)))), rows = (n + cols - 1) / cols;
        constexpr float A = 0.17f, B = 0.83f;
        for (int i = 0; i < n; ++i) {
            const ItemIcon &icon = things[i].first;
            Billboard *b = place(things[i].second ? tintedSprite(icon.texpos, things[i].second)
                                                  : staticSprite(icon.texpos),
                                 icon.texpos, mx, my, BillKind::Item, uint8_t(2 + i), raise);
            if (!b)
                continue;
            const int cx = i % cols, cy = i / cols;
            b->x = mx + A + (B - A) * (cx + 0.5f) / cols;
            b->y = my + A + (B - A) * (cy + 0.5f) / rows;
            const float cell = (B - A) / std::max(cols, rows);
            b->scale = icon.big ? std::clamp(cell * 2.2f, 0.32f, 0.5f) : std::clamp(cell * 1.5f, 0.2f, 0.3f);
        }
    };

    // what a tile is made of: (material type << 16 | index), or a kind of tree
    std::unique_ptr<MapExtras::MapCache> map_cache;
    const auto mat_of = [&](int ci, int mx, int my, df::tiletype tt) -> uint32_t {
        uint32_t &m = L.mats[ci];
        if (m != MAT_UNKNOWN)
            return m;
        if (is_tree(tt)) {
            const df::plant *plant = Maps::getPlantAtTile(mx, my, z);
            m = MAT_TREE | (plant ? uint32_t(uint16_t(plant->material)) : 0xFFFEu);
        } else {
            if (!map_cache)
                map_cache = std::make_unique<MapExtras::MapCache>();
            const t_matpair mat = map_cache->staticMaterialAt(DFCoord(mx, my, z));
            m = (uint32_t(uint16_t(mat.mat_type)) << 16) | uint32_t(uint16_t(mat.mat_index));
            if (m == MAT_UNKNOWN)
                m = 0xFFFFFFFFu;
        }
        return m;
    };

    for (int cx = 0; cx < cols; ++cx) {
        const int mx = wx + cx;
        if (mx < 0 || mx >= w)
            continue;
        for (int cy = 0; cy < rows; ++cy) {
            const int my = wy + cy;
            if (my < 0 || my >= h)
                continue;
            const int idx = cx * rows + cy;
            const int ci = my * w + mx;
            if (from_map && L.known[ci])
                continue;
            const Cell old = L.cells[ci];
            Cell c;
            bool keep_things = false;   // the tile is out of sight: keep what stood on it

            const df::tiletype *ttp = Maps::getTileType(mx, my, z);
            if (ttp) {
                const df::tiletype tt = *ttp;
                const df::tiletype_shape shape = tileShape(tt);
                const int tm = int(tileMaterial(tt));
                const TileClass cls = classify(tt);
                const int32_t bg = from_map ? 0 : vp->screentexpos_background[idx];
                const int32_t bg2 = from_map ? 0 : vp->screentexpos_background_two[idx];
                const int32_t creature = from_map ? 0 : vp->screentexpos[idx];

                if (real(creature)) {
                    // large creatures are drawn as several tile-sized pieces
                    Piece pieces[6];
                    int n = 0;
                    pieces[n++] = {creature, 0, 0};
                    if (cx > 0 && vp->screentexpos_left_creature[idx - rows])
                        pieces[n++] = {vp->screentexpos_left_creature[idx - rows], -1, 0};
                    if (cx < cols - 1 && vp->screentexpos_right_creature[idx + rows])
                        pieces[n++] = {vp->screentexpos_right_creature[idx + rows], 1, 0};
                    if (cy > 0) {
                        if (vp->screentexpos_up_creature[idx - 1])
                            pieces[n++] = {vp->screentexpos_up_creature[idx - 1], 0, -1};
                        if (cx > 0 && vp->screentexpos_upleft_creature[idx - rows - 1])
                            pieces[n++] = {vp->screentexpos_upleft_creature[idx - rows - 1], -1, -1};
                        if (cx < cols - 1 && vp->screentexpos_upright_creature[idx + rows - 1])
                            pieces[n++] = {vp->screentexpos_upright_creature[idx + rows - 1], 1, -1};
                    }
                    CreatureTile ct;
                    ct.texpos = creature;
                    if (n > 1) {
                        ct.large = TextureStore::composite(pieces, n);
                        int min_dx = 0;
                        for (int i = 0; i < n; ++i)
                            min_dx = std::min(min_dx, pieces[i].dx);
                        ct.anchor_px = float(-min_dx * TEX + TEX / 2);
                        // A creature drawn over several tiles stands with its middle
                        // over its own tile. Which of the tiles the game calls the
                        // creature's own differs (a two-wide picture hangs off to one
                        // side), and in this view that put it half a tile or more to
                        // the side of where it is.
                        if (ct.large)
                            ct.anchor_px = (ct.large->x0 + ct.large->x1) * 0.5f;
                    }
                    L.creatures[ci] = std::move(ct);
                }

                // A palm is a bare trunk ending in a sloping top, with no branches
                // or leaves around it: the game draws its fronds in that top's
                // picture. Here the top is trunk like the rest, with long fronds
                // reaching out from it into the tiles around (spreadFronds).
                bool crown = false;
                if (shape == df::tiletype_shape::RAMP && tileMaterial(tt) == df::tiletype_material::TREE &&
                    tileSpecial(tt) != df::tiletype_special::DEAD) {
                    crown = true;
                    for (int dy = -1; dy <= 1 && crown; ++dy)
                        for (int dx = -1; dx <= 1 && crown; ++dx) {
                            const df::tiletype *n = (dx || dy) ? Maps::getTileType(mx + dx, my + dy, z) : nullptr;
                            crown = !n || !is_tree(*n);
                        }
                }
                if (crown) {
                    const uint32_t mat = mat_of(ci, mx, my, tt);
                    uint16_t leaves = 0;
                    const auto known_leaves = leaf_tex_.find(mat);
                    if (known_leaves != leaf_tex_.end()) {
                        leaves = known_leaves->second;
                    } else {
                        leaves = leaf_texture(mat);
                        leaf_tex_[mat] = leaves;
                    }
                    if (!leaves)
                        leaves = tex.material(Pattern::Leaves, colorFor(USE_LEAVES, tm, mat));
                    c.solid = tex.material(wall_pattern(tt, matInfo(mat).wood), colorFor(USE_WALL, tm, mat));
                    c.box = boxSet(SH_FROND_HUB, leaves, 0);   // only marks the crown, for spreadFronds
                } else if (cls == TileClass::Open) {
                    c.flags = cover_flags(mx, my, z);
                    well_in_open(c, mx, my, from_map ? 0 : vp->screentexpos_building_one[idx]);
                } else if (cls == TileClass::Wall || cls == TileClass::Fort) {
                    const uint32_t mat = mat_of(ci, mx, my, tt);
                    // the wall's own edging, in plain view, shows its true colour
                    uint32_t seen;
                    if (real(bg2) && tex.averageColor(bg2, seen))
                        learn(color_key(USE_WALL, tm, mat), seen);
                    const uint16_t t = tex.material(wall_pattern(tt, matInfo(mat).wood), colorFor(USE_WALL, tm, mat));
                    if (cls == TileClass::Fort) {
                        // a low wall you look over
                        c.half = t;
                        c.top = t;
                        c.topq = FORT_HEIGHT;
                        c.box = boxSet(SH_FORT, t, 0);
                        c.flags = cover_flags(mx, my, z);
                    } else if (tileMaterial(tt) == df::tiletype_material::CONSTRUCTION && matInfo(mat).wood) {
                        // a full wall whose faces look like a row of upright logs
                        c.solid = tex.material(Pattern::Logs, colorFor(USE_WALL, tm, mat));
                    } else {
                        c.solid = t;
                    }
                } else if (cls == TileClass::Canopy) {
                    const uint32_t mat = mat_of(ci, mx, my, tt);
                    if (shape == df::tiletype_shape::TRUNK_BRANCH || tileSpecial(tt) == df::tiletype_special::DEAD) {
                        // a thick or bare branch: wood
                        c.solid = tex.material(Pattern::Bark, colorFor(USE_WALL, tm, mat));
                    } else {
                        // The game's own picture of this kind of tree's leaves, taken
                        // once from its raws. Not from the screen: while walking, the
                        // picture read for a level up in the trees can still be the
                        // one of the ground below (shrubs, trunks).
                        uint16_t leaves = 0;
                        const auto known_leaves = leaf_tex_.find(mat);
                        if (known_leaves != leaf_tex_.end()) {
                            leaves = known_leaves->second;
                        } else {
                            leaves = leaf_texture(mat);
                            leaf_tex_[mat] = leaves;
                        }
                        c.solid = leaves ? leaves : tex.material(Pattern::Leaves, colorFor(USE_LEAVES, tm, mat));
                    }
                } else {
                    const uint32_t mat = mat_of(ci, mx, my, tt);
                    const int32_t b1 = from_map ? 0 : vp->screentexpos_building_one[idx];
                    const int32_t b2 = from_map ? 0 : vp->screentexpos_building_two[idx];
                    const int32_t item = from_map ? 0 : vp->screentexpos_item[idx];
                    const df::tile_designation *des = Maps::getTileDesignation(mx, my, z);
                    const df::tile_occupancy *occ = Maps::getTileOccupancy(mx, my, z);
                    const auto occb = occ ? occ->bits.building : df::tile_building_occ::None;
                    const bool has_building = occb != df::tile_building_occ::None;
                    const bool drawn = !((bg == 0 || bg == blank) && !bg2 && !b1 && !b2 && !creature && !item);
                    const bool out_of_sight = !drawn || memory(bg) || memory(bg2) || memory(b1) || memory(b2) ||
                                              memory(item);
                    keep_things = out_of_sight;

                    // a building that fills the tile (closed door, floodgate, ...)
                    const int32_t filler = b1 ? b1 : b2;
                    bool doorway = false;
                    if (occb == df::tile_building_occ::Impassable || occb == df::tile_building_occ::Dynamic) {
                        if (!real(filler)) {
                            // remembered: as it was when last seen
                            if (old.frame) {
                                c.frame = old.frame;
                                doorway = true;
                            } else if (old.solid) {
                                c.solid = old.solid;
                            } else if (filler) {
                                c.solid = tex.flatWall(0, filler);
                            }
                        } else if (tex.coverage(filler) >= 0.6f) {
                            c.solid = tex.flatWall(real(bg) ? bg : 0, filler);
                        } else {
                            // an open door: its sprite is the frame with an opening
                            // you see and walk through
                            c.frame = tex.masked(filler);
                            doorway = true;
                        }
                    }

                    if (!c.solid) {
                        const int flow = des ? int(des->bits.flow_size) : 0;
                        // the game draws a tree's roots, shadow and dropped leaves over
                        // the ground near it: those are not the ground
                        const bool tree_bg = isTreeSprite(bg), tree_bg2 = isTreeSprite(bg2);
                        // (not on a tile with a building: what is drawn there belongs to it)
                        const bool flat_plant = real(bg2) && !tree_bg2 && cls == TileClass::Floor &&
                                                !has_building &&
                                                shape != df::tiletype_shape::BOULDER &&
                                                shape != df::tiletype_shape::SHRUB &&
                                                shape != df::tiletype_shape::SAPLING;
                        uint16_t ground = 0;
                        const uint64_t kind = (uint64_t(uint16_t(tt)) << 32) | mat;
                        if (real(bg) && !tree_bg) {
                            // a picture unlike this kind of ground is only taken once it
                            // has been read on this tile twice
                            uint32_t seen = 0;
                            const bool colored = tex.averageColor(bg, seen);
                            const bool confirmed = L.ground_seen[ci] == bg;
                            L.ground_seen[ci] = bg;
                            if (!colored || groundFits(kind, seen, confirmed)) {
                                // whatever grows or lies flat on the ground is part of its surface
                                ground = flat_plant ? tex.floorOver(bg, bg2) : tex.floor(bg);
                                if (ground && colored)
                                    learn(color_key(USE_FLOOR, tm, mat), seen);
                                if (ground && !flat_plant)
                                    ground_kind_[kind] = ground;
                            }
                        }
                        if (!ground) {
                            // as the same kind of ground looks where it has been seen
                            const auto known = ground_kind_.find(kind);
                            if (known != ground_kind_.end())
                                ground = known->second;
                        }
                        if (!ground) {
                            // what it looked like in view (without what lay on it)
                            const uint16_t was = tex.bare(old.ramp ? old.top : old.flr);
                            if (was && !tex.isMaterial(was))
                                ground = was;
                        }
                        if (!ground)   // not seen yet: the right kind of ground in the right colour
                            ground = tex.material(floor_pattern(tt, matInfo(mat).wood), colorFor(USE_FLOOR, tm, mat));

                        // snow, blood, mud, fallen leaves ... lying on it: in view as the
                        // game draws them, out of view as the game would from the map
                        int32_t spatter = 0;
                        if (!out_of_sight && vp->screentexpos_spatter) {
                            spatter = vp->screentexpos_spatter[idx];
                            if (real(spatter))
                                spatters_.learn(mx, my, z, spatter);
                            else
                                spatter = 0;
                        } else {
                            spatter = spatters_.fromMap(mx, my, z);
                        }
                        const uint16_t covered = tex.over(ground, spatter);

                        float raise = 0;
                        if (cls == TileClass::Ramp) {
                            c.ramp = ramp_corners(mx, my, z);
                            c.top = covered;
                            c.half = tex.material(wall_pattern(tt, matInfo(mat).wood), colorFor(USE_WALL, tm, mat));
                            raise = ramp_height(c.ramp, 0.5f, 0.5f);
                        } else if (flow >= 2) {
                            const bool magma = des->bits.liquid_type == df::tile_liquid::Magma;
                            const uint16_t liquid = tex.special(magma ? Special::Magma : Special::Water);
                            if (flow >= 4) {
                                c.top = liquid;
                                c.topq = WATER_HEIGHT;
                                c.flr = ground;
                            } else {
                                c.flr = liquid;
                            }
                        } else {
                            c.flr = covered;
                        }
                        c.flags = cover_flags(mx, my, z);

                        // furniture: a simple shape, in the colour the game shows it
                        // in, with the game's picture of it on top
                        df::building *bld = nullptr;
                        if (has_building && !doorway) {
                            bld = Buildings::findAtTile(df::coord(int16_t(mx), int16_t(my), int16_t(z)));
                            int shape = bld ? furniture_shape(bld->getType()) : -1;
                            // a shop sign hangs from the wall whatever the ground; other
                            // things on a slope stay pictures
                            if (shape == SH_SLAB && is_shop_sign(bld)) {
                                shape = SH_SIGN;
                            } else if (cls == TileClass::Ramp) {
                                shape = -1;
                                bld = nullptr;
                            }
                            if (shape >= 0) {
                                const uint32_t bmat = (uint32_t(uint16_t(bld->mat_type)) << 16) |
                                                      uint32_t(uint16_t(bld->mat_index));
                                const uint64_t key = color_key(USE_FURNITURE, int(bld->getType()), bmat);
                                const int32_t picture = real(b1) ? b1 : (real(b2) ? b2 : 0);
                                uint32_t seen;
                                // a cabinet takes the colour most of its picture is in
                                // (the average is darkened by its outlines)
                                const bool built = shape == SH_CABINET || shape == SH_BOOKCASE;
                                // (an altar's red and gold picture says little about its stone)
                                if (picture && shape != SH_ALTAR &&
                                    (built ? tex.mainColor(picture, seen) : tex.averageColor(picture, seen)))
                                    learn(key, seen);
                                uint32_t color = matInfo(bmat).color;
                                const auto learned = learned_color_.find(key);
                                if (learned != learned_color_.end())
                                    color = learned->second;
                                if (!color)
                                    color = rgb(124, 104, 84);
                                // cabinets, bookcases and wells are plain with a fine grit; a
                                // stone bed too, so its frame shows no slab joints
                                const bool plain = built || shape == SH_WELL || shape == SH_BED ||
                                                   shape == SH_STATUE || shape == SH_ALTAR;
                                uint16_t side = tex.material(
                                    plain && !(shape == SH_BED && matInfo(bmat).wood) ? Pattern::Grit
                                    : matInfo(bmat).wood                              ? Pattern::Planks
                                                                                      : Pattern::Smooth,
                                    color);
                                // a slab carries the engraving from the game's drawing on its
                                // flat sides; a cabinet or bookcase a front painted after it
                                const bool pictured = shape == SH_SLAB || shape == SH_SIGN || built;
                                uint16_t face = !pictured || !picture ? 0
                                                : shape == SH_SLAB  ? tex.spriteEmblem(picture)
                                                : shape == SH_SIGN  ? tex.spriteCut(picture)
                                                : tex.furnitureFront(picture, shape == SH_CABINET ? 0 : 1);
                                if (built) {
                                    // out of the adventurer's sight the game shows no picture:
                                    // as one of its kind looked, or painted in its colour
                                    const uint64_t fkey = (uint64_t(shape) << 32) | bmat;
                                    if (face)
                                        front_kind_[fkey] = face;
                                    else if (const auto f = front_kind_.find(fkey); f != front_kind_.end())
                                        face = f->second;
                                    else
                                        face = tex.furnitureFrontIn(color, shape == SH_CABINET ? 0 : 1);
                                }
                                int facing = 0;
                                if (built)
                                    facing = is_wall(mx, my - 1, z)   ? 0
                                             : is_wall(mx, my + 1, z) ? 1
                                             : is_wall(mx - 1, my, z) ? 2
                                             : is_wall(mx + 1, my, z) ? 3
                                                                      : 4;
                                if (shape == SH_SIGN) {
                                    // hung from a wall next to it; on a slope, as high above
                                    // the ground as on a floor
                                    const int wall = is_wall(mx, my - 1, z)   ? 0
                                                     : is_wall(mx, my + 1, z) ? 1
                                                     : is_wall(mx - 1, my, z) ? 2
                                                     : is_wall(mx + 1, my, z) ? 3
                                                                              : 4;
                                    float under = 0;
                                    if (c.ramp)
                                        for (const float v : {0.06f, 0.48f}) {
                                            const float u = 0.09f;
                                            const float ru = wall == 2 ? v : wall == 3 ? 1 - v : u;
                                            const float rv = wall == 2 || wall == 3 ? u : wall == 1 ? 1 - v : v;
                                            under = std::max(under, ramp_height(c.ramp, ru, rv));
                                        }
                                    facing = wall | (std::min(16, int(under * 16 + 0.5f)) << 3);
                                }
                                if (shape == SH_WELL)
                                    well_here(c, mx, my, side);
                                else if (pictured && !face && old.box)
                                    c.box = old.box;    // out of sight: as it looked when last seen
                                else
                                    c.box = boxSet(shape, side, face, facing, color, matInfo(bmat).wood);
                            }
                        }
                        // what lies on a table lies on its top
                        const float lift = c.box && c.box < boxsets.size() ? boxsets[c.box].surface / LEVEL_H : 0.0f;

                        // things standing on the tile; a sprite only counts where
                        // the map agrees that something of its kind is there
                        // a piece of furniture fills its tile: nothing grows or lies on
                        // the ground under it (a shop sign hangs high, leaving the ground)
                        const bool furnished = bld && c.box && !is_shop_sign(bld);
                        if (!out_of_sight) {
                            if (furnished) {
                            } else if (shape == df::tiletype_shape::BOULDER)
                                add_bill(bg2, mx, my, BillKind::Boulder, 0, raise);
                            else if (shape == df::tiletype_shape::SHRUB || shape == df::tiletype_shape::SAPLING)
                                add_bill(bg2, mx, my, BillKind::Plant, 0, raise);
                            else if (!flat_plant && !tree_bg2)
                                add_bill(bg2, mx, my, BillKind::Other, 0, raise);
                            if (!doorway && has_building && !c.box) {
                                add_bill(b1, mx, my, BillKind::Building, 1, raise);
                                add_bill(b2, mx, my, BillKind::Building, 6, raise);
                            }
                            if (bld && c.box && bld->getType() == df::building_type::Statue)
                                add_statue(b1, b2, cy > 0 ? vp->screentexpos_signpost[idx - 1] : 0, mx, my,
                                           raise + lift);
                            // (on a statue's tile the game may show the statue as an item
                            // too: its pedestal, which our podium replaces)
                            const bool statue_here = bld && c.box && bld->getType() == df::building_type::Statue;
                            if (occ && occ->bits.item)
                                add_items(mx, my, raise + lift, statue_here && pedestal(item) ? 0 : item);
                            if (bld && c.box && (bld->getType() == df::building_type::Table ||
                                                 bld->getType() == df::building_type::DisplayFurniture))
                                add_on_table(bld, mx, my, raise + lift);
                            add_bill(vp->screentexpos_vehicle[idx], mx, my, BillKind::Other, 3, raise);
                            add_bill(vp->screentexpos_vermin[idx], mx, my, BillKind::Item, 4, raise);
                            add_bill(vp->screentexpos_projectile[idx], mx, my, BillKind::Flying, 7, raise);
                            // the top half of a statue south of here is drawn on this tile:
                            // it stands with its bottom half on the statue's podium
                            if (real(vp->screentexpos_signpost[idx]) && !statue_at(mx, my + 1))
                                add_bill(vp->screentexpos_signpost[idx], mx, my, BillKind::Other, 8, raise);
                        }
                    } else {
                        keep_things = false;
                    }
                }
            }

            if (keep_things && !c.solid) {
                const auto range = before.equal_range(ci);
                for (auto it = range.first; it != range.second; ++it)
                    L.bills.push_back(it->second);
            }
            L.cells[ci] = c;
            L.known[ci] = 1;
            if (c.box)
                col_box[ci] = 1;
            if (c.solid || c.half || c.flr || c.top || c.frame || c.box || (c.flags & CELL_ROOFED)) {
                col_lo[ci] = std::min(col_lo[ci], int16_t(z));
                col_hi[ci] = std::max(col_hi[ci], int16_t(z));
            }
        }
    }
    spreadFronds(L, z, wx - FROND_REACH, wy - FROND_REACH, wx + cols + FROND_REACH, wy + rows + FROND_REACH);
    ++version;
}

void Scene::spreadFronds(Level &L, int z, int x0, int y0, int x1, int y1) {
    static const int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1}, DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    const auto is_arm = [&](uint16_t id) { return id < boxsets.size() && boxsets[id].frond_arm; };
    for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
            const Cell &hub = L.cells[size_t(y) * w + x];
            if (!hub.box || hub.box >= boxsets.size() || !boxsets[hub.box].frond_hub)
                continue;
            const uint16_t leaf = boxsets[hub.box].b[0].side;
            for (int d = 0; d < 8; ++d)
                for (int ring = 1; ring <= FROND_REACH; ++ring) {
                    const int nx = x + DX[d] * ring, ny = y + DY[d] * ring;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const size_t ni = size_t(ny) * w + nx;
                    Cell &c = L.cells[ni];
                    // only into open space (a frond already there is made again the same)
                    if (c.solid || c.half || c.ramp || c.top || c.frame ||
                        (c.box && boxsets[c.box].n && !is_arm(c.box)))
                        continue;
                    // the piece of the frond going out in direction d that lies this many tiles out
                    c.box = boxSet(SH_FROND_ARM, leaf, uint16_t(d + 8 * (ring - 1)));
                    col_box[ni] = 1;
                    col_lo[ni] = std::min(col_lo[ni], int16_t(z));
                    col_hi[ni] = std::max(col_hi[ni], int16_t(z));
                }
        }
}

void Scene::buildUnits(int zlo, int zhi, const df::unit *self, uint32_t frame, bool outlines, bool melee_reach,
                       bool smooth_units) {
    units.clear();
    if (!world)
        return;
    // a creature that was seen a moment ago stays visible while the part of
    // the map it walked into waits for its turn to be re-read
    static const CreatureTile remembered;
    constexpr uint32_t HOLD_FRAMES = 150;

    // time since the last call, for the glide of creatures between tiles
    const double now_s = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float dt = float(std::clamp(now_s - glide_time_, 0.0, 0.1));
    glide_time_ = now_s;

    // how many creatures share each tile, so they can stand side by side
    std::unordered_map<int64_t, int> counts, placed;
    const auto key_of = [&](const df::unit *u) {
        return (int64_t(u->pos.z) << 40) | (int64_t(u->pos.y) << 20) | int64_t(u->pos.x);
    };
    const auto visible = [&](const df::unit *u) -> const CreatureTile * {
        if (u == self || !Units::isActive(const_cast<df::unit *>(u)) || Units::isDead(const_cast<df::unit *>(u)))
            return nullptr;
        const int x = u->pos.x, y = u->pos.y, z = u->pos.z;
        if (z < zlo || z > zhi || x < 0 || y < 0 || x >= w || y >= h)
            return nullptr;
        const Level *L = level(z);
        if (!L)
            return nullptr;
        // only where the game itself drew a creature: that is what the player can see
        const auto it = L->creatures.find(y * w + x);
        if (it != L->creatures.end()) {
            unit_seen_[u->id] = frame;
            return &it->second;
        }
        const auto seen = unit_seen_.find(u->id);
        if (seen != unit_seen_.end() && frame - seen->second < HOLD_FRAMES && u->texpos[0][0])
            return &remembered;
        return nullptr;
    };

    for (const df::unit *u : world->units.active)
        if (visible(u))
            ++counts[key_of(u)];

    for (const df::unit *u : world->units.active) {
        const CreatureTile *ct = visible(u);
        if (!ct)
            continue;
        const int64_t key = key_of(u);
        const int n = counts[key];
        const int i = placed[key]++;

        Billboard b;
        b.kind = BillKind::Creature;
        b.unit_id = u->id;
        b.mx = int16_t(u->pos.x);
        b.my = int16_t(u->pos.y);
        b.mz = int16_t(u->pos.z);
        b.order = 5;
        {
            // how it stands to you: green a companion, red an enemy, white anyone else
            df::unit *unit = const_cast<df::unit *>(u);
            const int32_t me = self ? self->id : -1;
            if (u->relationship_ids[df::unit_relationship_type::GroupLeader] == me ||
                u->relationship_ids[df::unit_relationship_type::PetOwner] == me)
                b.ring = rgb(60, 230, 70);
            else if (Units::isDanger(unit) || Units::isInvader(unit) || Units::isOpposedToLife(unit) ||
                     Units::isCrazed(unit) || Units::isUndead(unit) ||
                     (Units::isWildlife(unit) && !Units::isTame(unit)) ||
                     u->relationship_ids[df::unit_relationship_type::LastAttacker] == me)
                b.ring = rgb(255, 50, 40);
            else
                b.ring = rgb(255, 255, 255);
        }
        // a strike or a hold only reaches the tiles around you
        const bool in_reach = !melee_reach ||
                              (self && std::abs(u->pos.x - self->pos.x) <= 1 && std::abs(u->pos.y - self->pos.y) <= 1 &&
                               std::abs(u->pos.z - self->pos.z) <= 1);
        if (outlines && in_reach)
            b.outline = b.ring;
        // where it is walking to next
        if (!u->path.path.x.empty() && !u->path.path.y.empty()) {
            const int dx = u->path.path.x[0] - u->pos.x, dy = u->path.path.y[0] - u->pos.y;
            if (std::abs(dx) <= 1 && std::abs(dy) <= 1) {
                b.move_dx = int8_t(dx);
                b.move_dy = int8_t(dy);
            }
        }

        const int32_t own = u->texpos[0][0];
        // what the game drew on the creature's tile may be one of its status bubbles
        const int32_t drawn = is_status_tile(ct->texpos) ? 0 : ct->texpos;
        if (ct->large && (i == 0 || own == ct->texpos)) {
            // drawn by the game across several tiles: already as big as it should be
            b.sprite = ct->large;
            b.anchor_px = ct->anchor_px;
        } else {
            const Piece piece{own ? own : drawn, 0, 0};
            b.sprite = TextureStore::composite(&piece, 1);
            b.scale = creature_scale(u);
        }
        if (!b.sprite)
            continue;

        // lying down: a creature on two legs lies on its side; one on four
        // legs or none sinks a quarter into the ground, unless it is dead:
        // then it lies on its back, legs up, on the ground
        if (u->flags1.bits.on_ground && Units::isDead(const_cast<df::unit *>(u))) {
            int stance = 0;
            if (u->body.body_plan)
                for (const df::body_part_raw *part : u->body.body_plan->body_parts)
                    if (part && part->flags.is_set(df::body_part_raw_flags::STANCE))
                        ++stance;
            std::shared_ptr<const Sprite> turned;
            if (stance == 2)
                turned = proneSprite(b.sprite == ct->large ? 0 : (own ? own : ct->texpos), b.sprite);
            else
                turned = upside_down(b.sprite == ct->large ? 0 : (own ? own : ct->texpos), b.sprite);
            if (turned) {
                b.sprite = turned;
                b.anchor_px = (turned->x0 + turned->x1) * 0.5f;
            }
        } else if (u->flags1.bits.on_ground) {
            int stance = 0;
            if (u->body.body_plan)
                for (const df::body_part_raw *part : u->body.body_plan->body_parts)
                    if (part && part->flags.is_set(df::body_part_raw_flags::STANCE))
                        ++stance;
            std::shared_ptr<const Sprite> turned;
            if (stance == 2)
                turned = proneSprite(b.sprite == ct->large ? 0 : (own ? own : ct->texpos), b.sprite);
            if (turned) {
                b.sprite = turned;
                b.anchor_px = (turned->x0 + turned->x1) * 0.5f;
            } else {
                b.sink = 0.25f;
            }
        }

        // several creatures on one tile stand in a small ring
        float ox = 0, oy = 0;
        if (n > 1) {
            const float a = 6.2831853f * float(i) / float(n);
            ox = 0.3f * std::cos(a);
            oy = 0.3f * std::sin(a);
        }
        b.x = u->pos.x + 0.5f + ox;
        b.y = u->pos.y + 0.5f + oy;
        b.base = (u->pos.z + groundRaise(u->pos.x, u->pos.y, u->pos.z)) * LEVEL_H;
        if (smooth_units) {
            // it is drawn on its way from where it was to where it is now
            Glide &g = glide_[u->id];
            const float dx = b.x - g.x, dy = b.y - g.y, dist = std::sqrt(dx * dx + dy * dy);
            if (g.frame != 0 && frame - g.frame <= 4 && dist < 2.6f && std::abs(b.base - g.base) < 1.6f * LEVEL_H) {
                const float step = std::max(4.5f, dist * 7.0f) * dt;
                if (dist > step) {
                    g.x += dx / dist * step;
                    g.y += dy / dist * step;
                } else {
                    g.x = b.x;
                    g.y = b.y;
                }
                const float climb = 4.0f * dt;
                g.base = std::abs(b.base - g.base) <= climb ? b.base : g.base + (b.base > g.base ? climb : -climb);
            } else {
                g.x = b.x;
                g.y = b.y;
                g.base = b.base;
            }
            g.frame = frame;
            b.x = g.x;
            b.y = g.y;
            b.base = g.base;
        }
        // its status bubble, over its head
        Billboard bubble;
        if (const int32_t tp = status_bubble(const_cast<df::unit *>(u), now_s); tp > 0 && b.sprite) {
            const Sprite &sp = *b.sprite;
            const float tall = float(sp.y1 - sp.y0) / TEX * THING_SCALE * b.scale * (1.0f - b.sink);
            bubble.sprite = staticSprite(tp);
            bubble.x = b.x;
            bubble.y = b.y;
            bubble.base = b.base + tall + 0.04f;
            bubble.scale = 0.5f;
            bubble.mx = b.mx;
            bubble.my = b.my;
            bubble.mz = b.mz;
            bubble.kind = BillKind::Other;
            bubble.order = 9;
            bubble.texpos = tp;
        }
        units.push_back(std::move(b));
        if (bubble.sprite)
            units.push_back(std::move(bubble));
    }
    if (frame % 512 == 0) {
        std::erase_if(unit_seen_, [&](const auto &entry) { return frame - entry.second > HOLD_FRAMES; });
        std::erase_if(glide_, [&](const auto &entry) { return frame - entry.second.frame > HOLD_FRAMES; });
        std::erase_if(prone_large_, [](const auto &entry) { return entry.second.from.expired(); });
    }
    ++version;
}

std::shared_ptr<const Sprite> Scene::proneSprite(int32_t texpos, const std::shared_ptr<const Sprite> &upright) {
    if (!upright)
        return nullptr;
    if (texpos) {
        auto found = prone_cache_.find(texpos);
        if (found == prone_cache_.end()) {
            if (prone_cache_.size() > 2000)
                prone_cache_.clear();
            found = prone_cache_.emplace(texpos, TextureStore::rotated(*upright)).first;
        }
        return found->second;
    }
    ProneLarge &p = prone_large_[upright.get()];
    if (p.from.lock() != upright) {
        p.from = upright;
        p.turned = TextureStore::rotated(*upright);
    }
    return p.turned;
}

float Scene::groundRaise(int x, int y, int z) const {
    const Level *L = level(z);
    if (!L || x < 0 || y < 0 || x >= w || y >= h)
        return 0;
    const Cell &c = L->cells[y * w + x];
    return c.ramp ? ramp_height(c.ramp, 0.5f, 0.5f) : 0.0f;
}

} // namespace fpv
