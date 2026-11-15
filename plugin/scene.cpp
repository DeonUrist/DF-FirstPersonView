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
#include "df/plant_growth.h"
#include "df/pmd_tree_texture_infost.h"
#include "df/building.h"
#include "df/building_type.h"
#include "df/building_actual.h"
#include "df/building_hatchst.h"
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
#include "df/item_backpack_graphics_infost.h"
#include "df/item_quiver_graphics_infost.h"
#include "df/item_flask_graphics_infost.h"
#include "df/item_goblet_graphics_infost.h"
#include "df/item_box_graphics_infost.h"
#include "df/item_bag_graphics_infost.h"
#include "df/item_bin_graphics_infost.h"
#include "df/item_cage_graphics_infost.h"
#include "df/item_animal_trap_graphics_infost.h"
#include "df/item_chain_graphics_infost.h"
#include "df/item_splint_graphics_infost.h"
#include "df/item_crutch_graphics_infost.h"
#include "df/item_book_graphics_infost.h"
#include "df/item_egg_graphics_infost.h"
#include "df/item_instrument_graphics_infost.h"
#include "df/item_anvil_graphics_infost.h"
#include "df/item_mechanisms_graphics_infost.h"
#include "df/item_slab_graphics_infost.h"
#include "df/item_pipe_section_graphics_infost.h"
#include "df/item_totem_graphics_infost.h"
#include "df/item_skin_tanned_graphics_infost.h"
#include "df/item_sheet_graphics_infost.h"
#include "df/item_wood_graphics_infost.h"
#include "df/item_bar_graphics_infost.h"
#include "df/item_block_graphics_infost.h"
#include "df/item_gem_graphics_infost.h"
#include "df/item_thread_graphics_infost.h"
#include "df/item_liquid_graphics_infost.h"
#include "df/item_powder_graphics_infost.h"
#include "df/item_rock_graphics_infost.h"
#include "df/item_food_container_graphics_infost.h"
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
#include "df/flow_info.h"
#include "df/flow_type.h"
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
#include "df/activity_entry.h"
#include "df/activity_event.h"
#include "df/activity_event_conflictst.h"
#include "df/conflict_level.h"
#include "df/conflict_sidest.h"
#include "df/conflict_statest.h"
#include "df/unit.h"
#include "df/unit_action.h"
#include "df/unit_action_type.h"
#include "df/unit_relationship_type.h"
#include "df/world.h"
#include "df/art_image.h"
#include "df/art_image_chunk.h"
#include "df/art_image_element.h"
#include "df/art_image_element_creaturest.h"
#include "df/art_image_element_itemst.h"
#include "df/art_image_element_plantst.h"
#include "df/art_image_element_shapest.h"
#include "df/art_image_element_treest.h"
#include "df/art_image_element_type.h"
#include "df/engraving.h"
#include "df/init.h"
#include "modules/Items.h"

using namespace DFHack;
using df::global::gps;
using df::global::window_x;
using df::global::window_y;
using df::global::world;

namespace fpv {

namespace {

enum class TileClass { Wall, Fort, Floor, Open, Ramp, Canopy };

// height of raised surfaces, in sixteenths of a level
constexpr uint8_t FORT_HEIGHT = 3;     // a fortification is a low sill you look over (its blocks are a model)

// palm fronds: how many tiles out from the crown they reach into, their length
// from the trunk's edge, and the height they start at (just under the top of the
// crown's level, in tile widths)
constexpr int FROND_REACH = 3;
constexpr float FROND_LEN = 3.0f;
constexpr float FROND_TOP = 1.12f;

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

constexpr uint32_t MAT_UNKNOWN = 0x7FFFFFFFu;   // not looked up yet
constexpr uint32_t MAT_TREE = 0x80000000u;      // | index of the plant kind

enum Use { USE_WALL, USE_FLOOR, USE_LEAVES, USE_FURNITURE };

bool is_tree(df::tiletype tt) {
    const df::tiletype_material m = tileMaterial(tt);
    return m == df::tiletype_material::TREE || m == df::tiletype_material::MUSHROOM;
}

bool is_trunk(const df::tiletype *tt) {
    if (!tt || tileMaterial(*tt) != df::tiletype_material::TREE)
        return false;
    const df::tiletype_shape s = tileShape(*tt);
    return s == df::tiletype_shape::WALL || s == df::tiletype_shape::RAMP || s == df::tiletype_shape::TRUNK_BRANCH;
}

// Whether the trunk tile at x,y,z of a tree of kind mat carries leaves now:
// the tree's raws have a LEAVES growth hosted on the trunk (a palm's frond
// blades), in season, at this tile's height in the trunk.
bool has_frond_leaves(uint32_t mat, int x, int y, int z) {
    if (!(mat & MAT_TREE) || !world)
        return false;
    const size_t kind = mat & 0xFFFF;
    const auto &all = world->raws.plants.all;
    if (kind >= all.size() || !all[kind])
        return false;
    int below = -1, above = -1;   // trunk tiles under and over this one, counted when first needed
    const int32_t now = df::global::cur_year_tick ? *df::global::cur_year_tick : 0;
    for (const df::plant_growth *g : all[kind]->growths) {
        if (!g || g->id != "LEAVES" || !g->locations.bits.trunk)
            continue;
        if ((g->timing_1 >= 0 && now < g->timing_1) || (g->timing_2 >= 0 && now > g->timing_2))
            continue;
        if (below < 0) {
            below = 0;
            while (below < 64 && is_trunk(Maps::getTileType(x, y, z - below - 1)))
                ++below;
            above = 0;
            while (above < 64 && is_trunk(Maps::getTileType(x, y, z + above + 1)))
                ++above;
        }
        // 0 at the trunk's foot, 100 at its top
        const int perc = below + above ? below * 100 / (below + above) : 100;
        const int lo = g->trunk_height_perc_1 >= 0 ? g->trunk_height_perc_1 : 0;
        const int hi = g->trunk_height_perc_2 >= 0 ? g->trunk_height_perc_2 : 100;
        if (perc >= lo && perc <= hi)
            return true;
    }
    return false;
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

bool stair_up(df::tiletype tt) {
    const df::tiletype_shape s = tileShape(tt);
    return s == df::tiletype_shape::STAIR_UP || s == df::tiletype_shape::STAIR_UPDOWN;
}

bool stair_down(df::tiletype tt) {
    const df::tiletype_shape s = tileShape(tt);
    return s == df::tiletype_shape::STAIR_DOWN || s == df::tiletype_shape::STAIR_UPDOWN;
}

// Which kind of natural ground a tile is, for where two kinds meet: 0 if it takes
// no part (built, smoothed, farmed, stairs, not a floor); else the strength in the
// top bits (grass 3, soil and sand 2, stone and ice 1) over the kind. Of two
// tiles side by side, the higher class reaches into the lower one.
uint32_t ground_class(df::tiletype tt, uint32_t mat) {
    // driftwood lies on the soil or sand of the place, which is the tile's material
    if (tileMaterial(tt) == df::tiletype_material::DRIFTWOOD)
        tt = df::tiletype::SoilFloor1;
    if (classify(tt) != TileClass::Floor || stair_up(tt) || stair_down(tt))
        return 0;
    const df::tiletype_shape shape = tileShape(tt);
    if (shape != df::tiletype_shape::FLOOR && shape != df::tiletype_shape::PEBBLES &&
        shape != df::tiletype_shape::SHRUB && shape != df::tiletype_shape::SAPLING &&
        shape != df::tiletype_shape::BOULDER)
        return 0;
    const df::tiletype_special sp = tileSpecial(tt);
    if (sp == df::tiletype_special::SMOOTH || sp == df::tiletype_special::FURROWED)
        return 0;
    const df::tiletype_material tm = tileMaterial(tt);
    uint32_t strength = 0;
    bool by_mat = true;   // sand and loam, or two stones, differ by material
    switch (tm) {
    case df::tiletype_material::GRASS_LIGHT:
    case df::tiletype_material::GRASS_DARK:
    case df::tiletype_material::GRASS_DRY:
    case df::tiletype_material::GRASS_DEAD:
        strength = 3;
        by_mat = false;   // (its material is the soil under it)
        break;
    case df::tiletype_material::SOIL:
    case df::tiletype_material::ASHES:
        strength = 2;
        break;
    case df::tiletype_material::STONE:
    case df::tiletype_material::MINERAL:
    case df::tiletype_material::LAVA_STONE:
    case df::tiletype_material::FEATURE:
    case df::tiletype_material::FROZEN_LIQUID:
        strength = 1;
        break;
    default:
        return 0;
    }
    uint32_t kind = uint32_t(tm) << 20;
    if (by_mat) {
        uint32_t h = mat * 2654435761u;
        kind |= (h ^ (h >> 15)) & 0xFFFFF;
    }
    return strength << 28 | (kind & 0x0FFFFFFF) | 1;
}

// stairs here lead down to stairs that come up: the floor between them is open
bool stair_hole(int x, int y, int z) {
    const df::tiletype *here = Maps::getTileType(x, y, z);
    const df::tiletype *below = Maps::getTileType(x, y, z - 1);
    return here && below && stair_down(*here) && stair_up(*below);
}

bool is_wall(int x, int y, int z) {
    const df::tiletype *tt = Maps::getTileType(x, y, z);
    return tt && classify(*tt) == TileClass::Wall;
}

// A wall a slope leans on: one whose top can be stepped onto from the slope.
// A tree trunk or a cliff going on up is not one (the slope would lead
// nowhere and stand up into it).
// (any: also a wall with more wall over it, which no slope leads onto)
bool holds_slope(int x, int y, int z, bool any = false) {
    const df::tiletype *tt = Maps::getTileType(x, y, z);
    if (!tt || classify(*tt) != TileClass::Wall || tileMaterial(*tt) == df::tiletype_material::TREE)
        return false;
    if (any)
        return true;
    const df::tiletype *above = Maps::getTileType(x, y, z + 1);
    if (!above)
        return false;
    const TileClass a = classify(*above);
    return a != TileClass::Wall && a != TileClass::Canopy;
}

// Ground beside a slope that its side would stand up from as a bare face:
// neither a wall it leans on nor another slope that carries on.
bool open_beside(int x, int y, int z) {
    const df::tiletype *tt = Maps::getTileType(x, y, z);
    if (!tt)
        return false;
    const TileClass c = classify(*tt);
    return c != TileClass::Wall && c != TileClass::Ramp;
}

// A slope rises towards the walls next to it: a corner is up if any of the
// three tiles touching that corner is a wall it leans on. The top of those
// walls is the floor of the level above, which the slope then meets. Where no
// such wall touches it, it rises towards any wall beside it all the same (a
// cliff face it cannot lead up: the game still draws a slope there). A slope
// with no wall beside it at all lies flat.
uint8_t ramp_corners(int x, int y, int z) {
    uint8_t r = RAMP;
    for (int any = 0; any < 2 && r == RAMP; ++any) {
        const bool n = holds_slope(x, y - 1, z, any), s = holds_slope(x, y + 1, z, any);
        const bool w = holds_slope(x - 1, y, z, any), e = holds_slope(x + 1, y, z, any);
        if (n || w || holds_slope(x - 1, y - 1, z, any)) r |= RAMP_NW;
        if (n || e || holds_slope(x + 1, y - 1, z, any)) r |= RAMP_NE;
        if (s || w || holds_slope(x - 1, y + 1, z, any)) r |= RAMP_SW;
        if (s || e || holds_slope(x + 1, y + 1, z, any)) r |= RAMP_SE;
    }

    // A side up at one end only, with open ground beyond it, would stand there
    // as a bare upright triangle (the last slope of a row, one standing out
    // from a wall). That side is brought down to the ground instead: with
    // open ground on both sides the slope becomes a ridge, with one the
    // corner on it is dropped, so the slope still rises to the wall but its
    // edges run down to the ground.
    const auto uneven = [&](uint8_t a, uint8_t b) { return bool(r & a) != bool(r & b); };
    const bool on = open_beside(x, y - 1, z) && uneven(RAMP_NW, RAMP_NE);
    const bool os = open_beside(x, y + 1, z) && uneven(RAMP_SW, RAMP_SE);
    const bool ow = open_beside(x - 1, y, z) && uneven(RAMP_NW, RAMP_SW);
    const bool oe = open_beside(x + 1, y, z) && uneven(RAMP_NE, RAMP_SE);
    const auto same = [&](uint8_t a, uint8_t b) { return !uneven(a, b); };
    if (on && os && same(RAMP_NE, RAMP_SE) && same(RAMP_NW, RAMP_SW))
        return r | RAMP_RIDGE | RAMP_VALLEY_V;   // rising west or east, high along v = 0.5
    if (ow && oe && same(RAMP_NW, RAMP_NE) && same(RAMP_SW, RAMP_SE))
        return r | RAMP_RIDGE | RAMP_VALLEY_U;   // rising north or south, high along u = 0.5
    uint8_t drop = 0;
    if (on) drop |= RAMP_NW | RAMP_NE;
    if (os) drop |= RAMP_SW | RAMP_SE;
    if (ow) drop |= RAMP_NW | RAMP_SW;
    if (oe) drop |= RAMP_NE | RAMP_SE;
    r &= uint8_t(~drop);

    // An edge that is up at both ends with open ground beyond it (a door in a
    // wall line, a fortification, open ground: the walls beside it lift its
    // corners) would stand as a wall hiding what is there. The slope dips in
    // the middle instead, as a channel running through towards that side. Not
    // where a wall or another slope is beyond the edge: that slope carries on
    // at the edge's height (slopes in the notches of a cliff, side by side).
    const bool up_n = (r & RAMP_NW) && (r & RAMP_NE), up_s = (r & RAMP_SW) && (r & RAMP_SE);
    const bool up_w = (r & RAMP_NW) && (r & RAMP_SW), up_e = (r & RAMP_NE) && (r & RAMP_SE);
    if ((up_n && open_beside(x, y - 1, z)) || (up_s && open_beside(x, y + 1, z)))
        r |= RAMP_VALLEY_U;
    else if ((up_w && open_beside(x - 1, y, z)) || (up_e && open_beside(x + 1, y, z)))
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
    if (above && (classify(*above) == TileClass::Open || stair_hole(x, y, z + 1)))
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
        return wood ? Pattern::Planks : Pattern::Slabs;
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

// Engravings on walls, by tile. The tile itself does not tell (an engraved wall
// is a smooth wall); the game keeps them in a list of its own. Rebuilt when that
// list changes length.
struct WallEngraving {
    int32_t art_id;
    int16_t art_subid;
    uint8_t glyph;   // the picture's character in the game's text mode
};
std::unordered_map<uint64_t, WallEngraving> wall_engravings;
size_t wall_engravings_from = size_t(-1);
std::unordered_map<uint64_t, int32_t> engraving_pictures;   // by art_id << 16 | subid

inline uint64_t pos_key(int x, int y, int z) {
    return (uint64_t(uint16_t(z)) << 32) | (uint64_t(uint16_t(y)) << 16) | uint16_t(x);
}

// the game's picture for one thing shown in an engraving; 0 if none is known
int32_t element_picture(df::art_image_element *e) {
    if (!e || !world)
        return 0;
    const auto &raws = world->raws;
    switch (e->getType()) {
    case df::art_image_element_type::CREATURE: {
        auto *c = virtual_cast<df::art_image_element_creaturest>(e);
        if (!c || c->race < 0 || size_t(c->race) >= raws.creatures.all.size())
            return 0;
        const df::creature_raw *cr = raws.creatures.all[c->race];
        if (!cr)
            return 0;
        const df::caste_raw *ca = c->caste >= 0 && size_t(c->caste) < cr->caste.size() ? cr->caste[c->caste] : nullptr;
        const int role = int(df::creature_graphics_role::DEFAULT);
        if (ca && ca->caste_graphics && ca->caste_graphics->creature_texture_texpos[0][role][0][0] > 0)
            return ca->caste_graphics->creature_texture_texpos[0][role][0][0];
        if (cr->graphics && cr->graphics->creature_texture_texpos[0][role][0][0] > 0)
            return cr->graphics->creature_texture_texpos[0][role][0][0];
        // peoples are drawn in layers and have no single picture: their statue's upper half
        if (ca && ca->statue_texpos[0] > 0)
            return ca->statue_texpos[0];
        return cr->statue_texpos[0] > 0 ? cr->statue_texpos[0] : 0;
    }
    case df::art_image_element_type::PLANT:
    case df::art_image_element_type::TREE: {
        int32_t id = -1;
        if (auto *p = virtual_cast<df::art_image_element_plantst>(e))
            id = p->plant_id;
        else if (auto *t = virtual_cast<df::art_image_element_treest>(e))
            id = t->plant_id;
        if (id < 0 || size_t(id) >= raws.plants.all.size() || !raws.plants.all[id])
            return 0;
        // (which of these pictures is which is not known; the first one there is taken)
        for (const int32_t tp : raws.plants.all[id]->texpos)
            if (tp > 0)
                return tp;
        return 0;
    }
    case df::art_image_element_type::SHAPE: {
        auto *sh = virtual_cast<df::art_image_element_shapest>(e);
        if (!sh || sh->shape_id < 0 || size_t(sh->shape_id) >= raws.descriptors.shapes.size())
            return 0;
        const df::descriptor_shape *d = raws.descriptors.shapes[sh->shape_id];
        if (!d)
            return 0;
        return d->texpos_large_gem > 0 ? d->texpos_large_gem : (d->statue_tex_top > 0 ? d->statue_tex_top : 0);
    }
    case df::art_image_element_type::ITEM: {
        auto *it = virtual_cast<df::art_image_element_itemst>(e);
        if (!it || int(it->item_type) < 0)
            return 0;
        if (it->item_subtype >= 0)
            if (const df::itemdef *def = Items::getSubtypeDef(it->item_type, it->item_subtype))
                if (def->statue_texpos_top > 0)
                    return def->statue_texpos_top;
        const size_t ti = size_t(it->item_type);
        return ti < raws.itemdefs.statue_texpos_top.size() && raws.itemdefs.statue_texpos_top[ti] > 0
                   ? raws.itemdefs.statue_texpos_top[ti]
                   : 0;
    }
    default:
        return 0;
    }
}

// The picture to carve on an engraved wall at a tile: the main thing shown in
// the engraving (a creature before a tree or plant before an item before a
// shape), else the character the game shows for it in text mode. -1 if the wall
// is not engraved.
int32_t wall_engraving_picture(int x, int y, int z) {
    if (!world)
        return -1;
    const auto &all = world->event.engravings;
    if (all.size() != wall_engravings_from) {
        wall_engravings.clear();
        for (const df::engraving *e : all)
            if (e && !e->flags.bits.floor)
                wall_engravings[pos_key(e->pos.x, e->pos.y, e->pos.z)] = {e->art_id, e->art_subid, e->tile};
        wall_engravings_from = all.size();
    }
    const auto found = wall_engravings.find(pos_key(x, y, z));
    if (found == wall_engravings.end())
        return -1;
    const WallEngraving &we = found->second;
    const uint64_t art_key = (uint64_t(uint32_t(we.art_id)) << 16) | uint16_t(we.art_subid);
    const auto known = engraving_pictures.find(art_key);
    if (known != engraving_pictures.end())
        return known->second;
    int32_t picture = 0;
    // the image itself, if its part of the game's store of images is loaded
    for (const df::art_image_chunk *chunk : world->art_image_chunks.all) {
        if (!chunk || chunk->id != we.art_id)
            continue;
        if (we.art_subid >= 0 && size_t(we.art_subid) < chunk->images.size())
            if (const df::art_image *im = chunk->images[we.art_subid].art_image) {
                static const df::art_image_element_type order[] = {
                    df::art_image_element_type::CREATURE, df::art_image_element_type::TREE,
                    df::art_image_element_type::PLANT, df::art_image_element_type::ITEM,
                    df::art_image_element_type::SHAPE};
                for (const auto kind : order) {
                    for (df::art_image_element *el : im->elements)
                        if (el && el->getType() == kind)
                            if ((picture = element_picture(el)) > 0)
                                break;
                    if (picture > 0)
                        break;
                }
            }
        break;
    }
    if (picture <= 0 && df::global::init && we.glyph) {
        picture = int32_t(df::global::init->font.large_font_texpos[we.glyph]);
        if (picture <= 0)
            picture = int32_t(df::global::init->font.small_font_texpos[we.glyph]);
    }
    if (picture < 0)
        picture = 0;
    engraving_pictures[art_key] = picture;
    return picture;
}

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

// One of the game's track symbols (TRACKING_SYMBOLS page: footprints, broken
// vegetation ... by kind, strength, direction, yours or not), which the game
// draws on a tile while its tracks display is on. The page's texture numbers,
// kept per world (a new world gives new numbers).
bool is_track_symbol(int32_t tp) {
    static std::vector<int32_t> known;
    static int32_t known_first = -1;
    if (tp <= 0 || !df::global::texture)
        return false;
    for (const df::tile_pagest *page : df::global::texture->page) {
        if (!page || page->token != "TRACKING_SYMBOLS")
            continue;
        const int32_t first = page->texpos.empty() ? 0 : int32_t(page->texpos[0]);
        if (first != known_first) {
            known_first = first;
            known.clear();
            for (const auto *tps : {&page->texpos, &page->texpos_gs})
                for (const auto t : *tps)
                    if (int32_t(t) > 0)
                        known.push_back(int32_t(t));
            std::sort(known.begin(), known.end());
        }
        return std::binary_search(known.begin(), known.end(), tp);
    }
    return false;
}

// the track symbol the game drew on a tile of the viewport (0: none). Which of
// its layers it uses was not read from the game: the ones over the ground are
// looked through, and only a picture of the tracking page counts.
int32_t track_symbol_at(const df::graphic_viewportst *vp, int idx) {
    const int32_t *const layers[] = {vp->screentexpos_designation, vp->screentexpos_interface,
                                     vp->screentexpos_signpost,    vp->screentexpos_top_shadow,
                                     vp->screentexpos_high_flow,   vp->screentexpos_projectile,
                                     vp->screentexpos_building_two, vp->screentexpos_vermin,
                                     vp->screentexpos_item,         vp->screentexpos_spatter,
                                     vp->screentexpos_background_two};
    for (const int32_t *l : layers)
        if (l && is_track_symbol(l[idx]))
            return l[idx];
    return 0;
}

// The game's driftwood picture (DRIFTWOOD page, either colour set)
bool is_driftwood_picture(int32_t tp) {
    if (tp <= 0 || !df::global::texture)
        return false;
    for (const df::tile_pagest *page : df::global::texture->page)
        if (page && page->token == "DRIFTWOOD") {
            for (const auto *tps : {&page->texpos, &page->texpos_gs})
                for (const auto t : *tps)
                    if (int32_t(t) == tp)
                        return true;
            return false;
        }
    return false;
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
        if (!TextureStore::readSprite(top, raw) || std::none_of(raw, raw + TEXPIX, [](uint32_t p) { return px_a(p) >= 128; }))
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
    // The statues that show no one creature (a group, a duel, a battle, a tree
    // ...): the STATUES page has them below its pedestals, upper half over
    // lower half. The game's itemdefs.statue_graphics_info is not used: it
    // holds the pictures it put together for placed statues, and their lower
    // half has the pedestal painted in, which our podium replaces.
    if (df::global::texture)
        for (const df::tile_pagest *page : df::global::texture->page) {
            if (!page || page->token != "STATUES" || page->page_dim_x <= 0)
                continue;
            const size_t dx = size_t(page->page_dim_x), rows = page->texpos.size() / dx;
            for (size_t r = 3; r + 1 < rows; r += 2)
                for (size_t c = 0; c < dx; ++c)
                    add(int32_t(page->texpos[r * dx + c]), int32_t(page->texpos[(r + 1) * dx + c]));
            break;
        }
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

// The picture the game keeps for one creature (peoples, whose looks are put
// together from layers): up to three tiles wide and two high, since what it
// holds may reach into the tiles beside and above it. Which of them carry the
// body differs from creature to creature. Returns how many pieces there are;
// 0 for a creature drawn from its kind's plain picture.
int own_pieces(const df::unit *u, Piece out[6]) {
    int n = 0;
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 2; ++y)
            if (u->texpos_currently_in_use[x][y] && u->texpos[x][y] > 0)
                out[n++] = {u->texpos[x][y], x, y};
    return n;
}

// The picture the game's raws give a creature's kind and caste, laid out as
// own_pieces: for an animal on a shared tile not yet seen alone. 0 if none.
int raw_pieces(const df::unit *u, Piece out[6]) {
    if (!world || u->race < 0 || size_t(u->race) >= world->raws.creatures.all.size())
        return 0;
    const df::creature_raw *cr = world->raws.creatures.all[u->race];
    if (!cr)
        return 0;
    const df::caste_raw *ca = u->caste >= 0 && size_t(u->caste) < cr->caste.size() ? cr->caste[u->caste] : nullptr;
    const int def = int(df::creature_graphics_role::DEFAULT);
    const int pref = Units::isGhost(const_cast<df::unit *>(u)) ? int(df::creature_graphics_role::GHOST) : def;
    for (const df::creature_raw_graphics *g : {ca ? ca->caste_graphics : nullptr, cr->graphics}) {
        if (!g)
            continue;
        for (const int role : {pref, def}) {
            int n = 0;
            for (int x = 0; x < 3; ++x)
                for (int y = 0; y < 2; ++y)
                    if (g->creature_texture_texpos[0][role][x][y] > 0)
                        out[n++] = {g->creature_texture_texpos[0][role][x][y], x, y};
            if (n)
                return n;
        }
    }
    return 0;
}

// Whether a picture the game drew is one of those its raws hold for this
// creature's kind and caste (any role, profession or link): on a shared tile,
// or one where the game blinks between a creature and something else, that
// tells this creature's picture from the rest.
bool is_kind_picture(const df::unit *u, int32_t tp) {
    if (!world || tp <= 0 || u->race < 0 || size_t(u->race) >= world->raws.creatures.all.size())
        return false;
    static std::unordered_map<uint64_t, bool> cache;
    const uint64_t key = (uint64_t(uint32_t(u->race)) << 48) ^ (uint64_t(uint16_t(u->caste)) << 32) ^ uint32_t(tp);
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;
    if (cache.size() > 50000)
        cache.clear();
    const df::creature_raw *cr = world->raws.creatures.all[u->race];
    const df::caste_raw *ca =
        cr && u->caste >= 0 && size_t(u->caste) < cr->caste.size() ? cr->caste[u->caste] : nullptr;
    bool found = false;
    const auto scan = [&](const auto &arr) {
        const int32_t *p = reinterpret_cast<const int32_t *>(&arr);
        const size_t n = sizeof(arr) / sizeof(int32_t);
        if (!found)
            found = std::find(p, p + n, tp) != p + n;
    };
    for (const df::creature_raw_graphics *g : {ca ? ca->caste_graphics : nullptr, cr ? cr->graphics : nullptr})
        if (g) {
            scan(g->creature_texture_texpos);
            scan(g->profession_texpos);
            scan(g->entity_link_texpos);
            scan(g->site_link_texpos);
        }
    cache.emplace(key, found);
    return found;
}

} // namespace

// The first picture of a list of the game's item pictures (several per kind,
// by material or look); 0 if it has none.
template <typename List>
int32_t first_picture(const List &list) {
    for (const auto *g : list)
        if (g && g->texpos)
            return int32_t(g->texpos);
    return 0;
}

bool itemPicture(df::item *item, uint32_t *out) {
    if (!item || !world || !out)
        return false;
    ItemIcon icon = item_icon(item);
    MaterialInfo mi(item);
    const df::material *mat = mi.isValid() ? mi.material : nullptr;
    if (!icon.texpos) {
        const auto &d = world->raws.itemdefs;
        const bool metal = mat && mat->flags.is_set(df::material_flags::IS_METAL);
        const bool hard = mat && (metal || mat->flags.is_set(df::material_flags::WOOD) ||
                                  mat->flags.is_set(df::material_flags::IS_STONE) ||
                                  mat->flags.is_set(df::material_flags::IS_GLASS));
        using T = df::item_type;
        switch (item->getType()) {
        case T::BACKPACK: icon.texpos = first_picture(d.backpack_graphics_info); break;
        case T::QUIVER: icon.texpos = first_picture(d.quiver_graphics_info); break;
        case T::FLASK: icon.texpos = first_picture(d.flask_graphics_info); break;
        case T::GOBLET: icon.texpos = first_picture(d.goblet_graphics_info); break;
        // a box of cloth or leather is a bag
        case T::BOX: icon.texpos = hard ? first_picture(d.box_graphics_info) : first_picture(d.bag_graphics_info); break;
        case T::BARREL:
            icon.texpos = metal ? first_picture(d.metal_barrel_graphics_info) : first_picture(d.wood_barrel_graphics_info);
            break;
        case T::BUCKET: icon.texpos = first_picture(d.bucket_graphics_info); break;
        case T::BIN: icon.texpos = first_picture(d.bin_graphics_info); break;
        case T::CAGE: icon.texpos = first_picture(d.cage_graphics_info); break;
        case T::ANIMALTRAP: icon.texpos = first_picture(d.animal_trap_graphics_info); break;
        case T::CHAIN: icon.texpos = first_picture(d.chain_graphics_info); break;
        case T::SPLINT: icon.texpos = first_picture(d.splint_graphics_info); break;
        case T::CRUTCH: icon.texpos = first_picture(d.crutch_graphics_info); break;
        case T::BOOK: icon.texpos = first_picture(d.book_graphics_info); break;
        case T::EGG: icon.texpos = first_picture(d.egg_graphics_info); break;
        case T::INSTRUMENT: icon.texpos = first_picture(d.instrument_graphics_info); break;
        case T::ANVIL: icon.texpos = first_picture(d.anvil_graphics_info); break;
        case T::TRAPPARTS: icon.texpos = first_picture(d.mechanisms_graphics_info); break;
        case T::SLAB: icon.texpos = first_picture(d.slab_graphics_info); break;
        case T::PIPE_SECTION: icon.texpos = first_picture(d.pipe_section_graphics_info); break;
        case T::TOTEM: icon.texpos = first_picture(d.totem_graphics_info); break;
        case T::SKIN_TANNED: icon.texpos = first_picture(d.skin_tanned_graphics_info); break;
        case T::SHEET: icon.texpos = first_picture(d.sheet_graphics_info); break;
        case T::WOOD: icon.texpos = first_picture(d.wood_graphics_info); break;
        // grey pictures the game draws in the colour of the material
        case T::BAR: icon = {first_picture(d.bar_graphics_info), false, true}; break;
        case T::BLOCKS: icon = {first_picture(d.block_graphics_info), false, true}; break;
        case T::ROUGH:
        case T::SMALLGEM: icon = {first_picture(d.gem_graphics_info), false, true}; break;
        case T::THREAD: icon = {first_picture(d.thread_graphics_info), false, true}; break;
        case T::LIQUID_MISC: icon = {first_picture(d.liquid_graphics_info), false, true}; break;
        case T::POWDER_MISC: icon = {first_picture(d.powder_graphics_info), false, true}; break;
        case T::BOULDER:
        case T::ROCK: icon = {first_picture(d.rock_graphics_info), false, true}; break;
        default: break;
        }
    }
    if (!icon.texpos)
        return false;
    if (!icon.tint)
        return TextureStore::readSprite(icon.texpos, out);
    uint32_t color = rgb(200, 200, 200);
    if (mat) {
        const df::matter_state state =
            item->getType() == df::item_type::LIQUID_MISC ? df::matter_state::Liquid : df::matter_state::Solid;
        const int ci = mat->state_color[state];
        const auto &colors = world->raws.descriptors.colors;
        if (ci >= 0 && size_t(ci) < colors.size() && colors[ci])
            color = rgb(std::clamp(int(colors[ci]->red * 255), 0, 255), std::clamp(int(colors[ci]->green * 255), 0, 255),
                        std::clamp(int(colors[ci]->blue * 255), 0, 255));
    }
    const std::shared_ptr<Sprite> s = TextureStore::tinted(icon.texpos, color);
    if (!s || s->w != TEX || s->h != TEX)
        return false;
    std::copy(s->px.begin(), s->px.end(), out);
    return true;
}

uint16_t Scene::boxSet(int shape, uint16_t side, uint16_t picture, int facing, uint32_t color, bool wood) {
    const uint64_t key = (uint64_t(facing) << 56) | (uint64_t(shape) << 40) | (uint64_t(side) << 20) | uint64_t(picture);
    const auto it = boxset_index_.find(key);
    if (it != boxset_index_.end())
        return it->second;
    if (boxsets.empty()) {
        boxsets.emplace_back();     // 0 means "none"
        boxset_args_.push_back({-1, 0, 0, 0, 0, false});
    }
    if (boxsets.size() >= 65000)
        return 0;
    const BoxArgs args{shape, side, picture, facing, color, wood};
    boxsets.push_back(makeBoxSet(args));
    boxset_args_.push_back(args);
    const uint16_t id = uint16_t(boxsets.size() - 1);
    boxset_index_[key] = id;
    return id;
}

void Scene::setModels(std::shared_ptr<const Models> models) {
    models_ = std::move(models);
    remakeBoxSets();
}

void Scene::setEdgeWidth(float w) {
    if (w == edge_width_)
        return;
    edge_width_ = w;
    remakeBoxSets();
}

void Scene::remakeBoxSets() {
    // the tiles keep their numbers: each set is made again in its place
    for (size_t i = 1; i < boxsets.size() && i < boxset_args_.size(); ++i)
        boxsets[i] = makeBoxSet(boxset_args_[i]);
    ++version;
}

BoxSet Scene::makeBoxSet(const BoxArgs &a) {
    const int shape = a.shape, facing = a.facing;
    const uint16_t side = a.side, picture = a.picture;
    BoxSet set;
    switch (shape) {
    case SH_FROND_HUB:
        // the crown of a palm: its tile is trunk; the set has no blocks and only
        // marks where fronds start (and holds their leaf texture for spreadFronds)
        set.frond_hub = true;
        set.b[0].side = side;
        return set;
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
            if (u1 - u0 > 0.004f && v1 - v0 > 0.004f && set.n < BOX_MAX) {
                Box &b = set.b[set.n++];
                b.u0 = u0;
                b.v0 = v0;
                b.u1 = u1;
                b.v1 = v1;
                b.z0 = zc - 0.025f;
                b.z1 = zc + 0.025f;
                b.side = b.top = side;
            }
        }
        return set;
    }
    default:
        break;
    }
    if (shape < 0 || shape >= SH_COUNT || !MODEL_FILES[shape])
        return set;
    if (!models_)
        models_ = load_models("");
    // the case the thing is in, and how far its model is turned (quarter turns clockwise)
    bool shut = false, open = false, wall = false, free = false, below = false;
    int turns = 0;
    float down = 0;           // a well: how deep its shaft is
    uint16_t pic = picture;   // the game's picture of it, if it has one
    switch (shape) {
    case SH_CABINET:
    case SH_BOOKCASE:
    case SH_SIGN: {
        // facing: back to the N, S, W, E wall, 4 = free standing (a sign on its post)
        static const int TURNS[4] = {0, 2, 3, 1};
        const int f = facing & 7;
        free = f >= 4;
        wall = !free;
        turns = free ? 0 : TURNS[f];
        break;
    }
    case SH_STAIRS:
        // facing & 3: the side the first step is on (N, E, S, W); & 4: it goes on below
        turns = facing & 3;
        below = (facing & 4) != 0;
        break;
    case SH_HATCH:
        shut = (facing & 1) != 0;
        open = !shut;
        break;
    case SH_WELL:
        down = picture / 100.0f;
        pic = 0;
        break;
    default:
        break;
    }
    // the lowest level of a stair has a model of its own, if there is one
    const Model &m = shape == SH_STAIRS && !below && !models_->shape[SH_STAIRS_BOTTOM].parts.empty()
                         ? models_->shape[SH_STAIRS_BOTTOM]
                         : models_->shape[shape];
    const auto counts = [&](uint8_t when) {
        switch (when) {
        case WHEN_SHUT: return shut;
        case WHEN_OPEN: return open;
        case WHEN_WALL: return wall;
        case WHEN_FREE: return free;
        default: return true;
        }
    };
    const auto texture = [&](const ModelMat &mm, uint16_t usual) -> uint16_t {
        switch (mm.kind) {
        case ModelMat::Own: return side;
        case ModelMat::Picture: return pic ? pic : side;
        case ModelMat::Paint: return tex.material(mm.pattern, mm.color ? mm.color : rgb(255, 255, 255));
        case ModelMat::Image: {
            if (mm.image < 0 || size_t(mm.image) >= models_->images.size())
                return side;
            const ModelImage &img = models_->images[size_t(mm.image)];
            return tex.image(img.serial, img.px.data(), mm.own_tint ? a.color : mm.color);
        }
        default: return usual;
        }
    };
    for (const ModelPart &p : m.parts) {
        if (set.n >= BOX_MAX)
            break;
        if (!counts(p.when))
            continue;
        Box b;
        b.edge = edge_width_;
        b.side = texture(p.side, side);
        b.top = texture(p.top, b.side);
        b.under = texture(p.under, 0);
        if (p.face_sides && pic) {
            b.face = pic;
            b.face_sides = p.face_sides;
        }
        b.u0 = p.u0;
        b.v0 = p.v0;
        b.u1 = p.u1;
        b.v1 = p.v1;
        b.z0 = p.z0;
        b.z1 = p.z1;
        if (p.corners) {
            // an upright prism on this outline (its box around it is worked out below)
            b.round = uint8_t(std::min(p.corners, ROUND_MAX));
            for (int k = 0; k < b.round; ++k) {
                b.ru[k] = p.cu[k];
                b.rv[k] = p.cv[k];
            }
        } else if (p.fit && pic) {
            // the picture's proportions inside the box, along its longer side,
            // keeping its top and its west or north edge
            const float aspect = std::clamp(tex.get(pic).aspect, 0.25f, 4.0f);
            const bool along_u = b.u1 - b.u0 >= b.v1 - b.v0;
            const float w = along_u ? b.u1 - b.u0 : b.v1 - b.v0, h = b.z1 - b.z0;
            if (w > 0 && h > 0) {
                const bool wide = aspect >= w / h;
                (along_u ? b.u1 : b.v1) = (along_u ? b.u0 : b.v0) + (wide ? w : h * aspect);
                b.z0 = b.z1 - (wide ? w / aspect : h);
            }
        }
        if (p.hang)
            b.z0 = std::min(b.z1 - 0.05f, b.z0 - down);
        // a stair's lowest steps reach under the floor only where it goes on below
        if (shape == SH_STAIRS && !below)
            b.z0 = std::max(b.z0, 0.0f);
        if (b.z1 <= b.z0)
            continue;
        set.b[set.n++] = b;
    }
    for (const Model::Value &v : m.surface)
        if (counts(v.when))
            set.surface = v.v[0];
    for (const Model::Value &v : m.stand)
        if (counts(v.when)) {
            set.stand_u = v.v[0];
            set.stand_v = v.v[1];
            set.stand_h = v.v[2];
        }

    // turn it a quarter clockwise as seen from above (north to east), `turns` times
    for (int t = 0; t < turns; ++t) {
        for (int i = 0; i < set.n; ++i) {
            Box &b = set.b[i];
            const float u0 = b.u0, u1 = b.u1, v0 = b.v0, v1 = b.v1;
            b.u0 = 1 - v1;
            b.u1 = 1 - v0;
            b.v0 = u0;
            b.v1 = u1;
            for (int k = 0; k < b.round; ++k) {
                const float ru = b.ru[k];
                b.ru[k] = 1 - b.rv[k];
                b.rv[k] = ru;
            }
            const uint8_t f = b.face_sides;
            b.face_sides = uint8_t(((f & FACE_N) ? FACE_E : 0) | ((f & FACE_E) ? FACE_S : 0) |
                                   ((f & FACE_S) ? FACE_W : 0) | ((f & FACE_W) ? FACE_N : 0));
        }
        if (set.stand_u >= 0) {
            const float su = set.stand_u;
            set.stand_u = 1 - set.stand_v;
            set.stand_v = su;
        }
    }
    // a round piece's box around it and the outward normals of its sides
    for (int i = 0; i < set.n; ++i) {
        Box &b = set.b[i];
        const int n = b.round;
        if (!n)
            continue;
        float mu = 0, mv = 0;
        b.u0 = b.v0 = 1e9f;
        b.u1 = b.v1 = -1e9f;
        for (int k = 0; k < n; ++k) {
            mu += b.ru[k] / n;
            mv += b.rv[k] / n;
            b.u0 = std::min(b.u0, b.ru[k]);
            b.u1 = std::max(b.u1, b.ru[k]);
            b.v0 = std::min(b.v0, b.rv[k]);
            b.v1 = std::max(b.v1, b.rv[k]);
        }
        for (int k = 0; k < n; ++k) {
            const int j = k + 1 < n ? k + 1 : 0;
            float nu = b.rv[j] - b.rv[k], nv = b.ru[k] - b.ru[j];
            const float len = std::max(std::sqrt(nu * nu + nv * nv), 1e-6f);
            nu /= len;
            nv /= len;
            if (nu * (mu - b.ru[k]) + nv * (mv - b.rv[k]) > 0) {
                nu = -nu;
                nv = -nv;
            }
            b.rnu[k] = nu;
            b.rnv[k] = nv;
        }
        // a corner where the sides turn by more than about 40 degrees is a crease
        for (int k = 0; k < n; ++k) {
            const int prev = k > 0 ? k - 1 : n - 1;
            if (b.rnu[prev] * b.rnu[k] + b.rnv[prev] * b.rnv[k] < 0.77f)
                b.sharp |= uint16_t(1u << k);
        }
    }
    return set;
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

FarTreeLook Scene::farTreeLook(int plant) {
    FarTreeLook look;
    if (!world || plant < 0 || size_t(plant) >= world->raws.plants.all.size() || !world->raws.plants.all[plant])
        return look;
    const uint32_t mat = MAT_TREE | uint32_t(plant);
    const int tm = int(df::tiletype_material::TREE);
    // the average colour of the textures the map's own tree tiles are drawn with
    const auto average = [&](uint16_t id, uint32_t fallback) {
        if (!id)
            return fallback;
        const Tex &t = tex.get(id);
        uint32_t r = 0, g = 0, b = 0, n = 0;
        for (int i = 0; i < TEXPIX; ++i)
            if (px_a(t.px[i])) {
                r += px_r(t.px[i]);
                g += px_g(t.px[i]);
                b += px_b(t.px[i]);
                ++n;
            }
        return n ? rgb(int(r / n), int(g / n), int(b / n)) : fallback;
    };
    const uint32_t leaf_col = colorFor(USE_LEAVES, tm, mat);
    uint16_t leaves = leaf_texture(mat);
    if (!leaves)
        leaves = tex.material(Pattern::Leaves, leaf_col);
    look.leaves = average(leaves, leaf_col);
    const uint32_t bark_col = colorFor(USE_WALL, tm, mat);
    look.bark = average(tex.material(Pattern::Bark, bark_col), bark_col);
    return look;
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
    boxset_args_.clear();
    leaf_tex_.clear();
    spatters_.reset();
    tree_sprites_.clear();
    ground_kind_.clear();
    class_look_.clear();
    std::fill(std::begin(coin_tex), std::end(coin_tex), -1);
    bucket_tex = -1;
    wall_engravings.clear();
    wall_engravings_from = size_t(-1);
    engraving_pictures.clear();
    ground_look_.clear();
    front_kind_.clear();
    glide_.clear();
    units.clear();
    tex.clear();
    sprite_cache_.clear();
    pile_cache_.clear();
    tint_cache_.clear();
    snow_cache_.clear();
    prone_cache_.clear();
    prone_large_.clear();
    std::fill(std::begin(bodypart_tex), std::end(bodypart_tex), -1);
    unit_seen_.clear();
    learned_color_.clear();
    mat_info_.clear();
    col_lo.clear();
    col_hi.clear();
    col_box.clear();
    far.reset();
    w = h = d = 0;
    ++version;
}

void Scene::shift(int dx, int dy) {
    if (w <= 0 || h <= 0 || (dx == 0 && dy == 0))
        return;
    const size_t n = size_t(w) * h;
    // a grid of the map moved: what came from outside it gets `fill`
    const auto move = [&](auto &v, auto fill) {
        if (v.size() < n)
            return;
        std::remove_reference_t<decltype(v)> out(n, fill);
        for (int y = 0; y < h; ++y) {
            const int oy = y + dy;
            if (oy < 0 || oy >= h)
                continue;
            for (int x = 0; x < w; ++x) {
                const int ox = x + dx;
                if (ox >= 0 && ox < w)
                    out[size_t(y) * w + x] = v[size_t(oy) * w + ox];
            }
        }
        v.swap(out);
    };
    // A tile near the old edge was worked out with nothing beyond it (slopes,
    // fronds, borders between kinds of ground look at their neighbours): read
    // again from the map, now that its neighbours are there. It keeps its
    // look until then.
    constexpr int BAND = 2;
    const auto near_old_edge = [&](int x, int y) {
        const int ox = x + dx, oy = y + dy;
        return ox < BAND || oy < BAND || ox >= w - BAND || oy >= h - BAND;
    };
    const auto moved = [&](int &x, int &y) {
        x -= dx;
        y -= dy;
        return x >= 0 && y >= 0 && x < w && y < h;
    };

    for (auto &lp : levels) {
        if (!lp)
            continue;
        Level &L = *lp;
        move(L.cells, Cell{});
        move(L.mats, MAT_UNKNOWN);
        move(L.known, uint8_t(0));
        move(L.ground_seen, int32_t(0));
        move(L.ground_class, ~0u);
        if (L.known.size() >= n)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (near_old_edge(x, y))
                        L.known[size_t(y) * w + x] = 0;
        std::erase_if(L.bills, [&](Billboard &b) {
            int x = b.mx, y = b.my;
            if (!moved(x, y))
                return true;
            b.mx = int16_t(x);
            b.my = int16_t(y);
            b.x -= float(dx);
            b.y -= float(dy);
            return false;
        });
        std::unordered_map<int32_t, CreatureTile> creatures;
        for (auto &[ci, ct] : L.creatures) {
            int x = ci % w, y = ci / w;
            if (moved(x, y))
                creatures.emplace(y * w + x, std::move(ct));
        }
        L.creatures.swap(creatures);
    }
    move(col_lo, int16_t(32767));
    move(col_hi, int16_t(-1));
    move(col_box, uint8_t(0));
    for (Billboard &b : units) {
        b.x -= float(dx);
        b.y -= float(dy);
        b.mx = int16_t(b.mx - dx);
        b.my = int16_t(b.my - dy);
    }
    for (auto &[id, g] : glide_) {
        g.x -= float(dx);
        g.y -= float(dy);
    }
    if (far) {
        for (FarLevel &lv : far->levels) {
            lv.x0 -= float(dx);
            lv.y0 -= float(dy);
        }
        for (FarBox &b : far->boxes) {
            b.x0 -= float(dx);
            b.x1 -= float(dx);
            b.y0 -= float(dy);
            b.y1 -= float(dy);
        }
        for (FarTree &t : far->trees) {
            t.x -= float(dx);
            t.y -= float(dy);
        }
        far->tx0 -= dx;
        far->ty0 -= dy;
        far->region_x += dx / 48;
        far->region_y += dy / 48;
    }
    // the game reads the engravings of the new part in: looked up again
    wall_engravings.clear();
    wall_engravings_from = size_t(-1);
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

std::shared_ptr<const Sprite> Scene::snowySprite(int32_t texpos, int level, uint32_t variant) {
    const uint64_t key = (uint64_t(uint32_t(texpos)) << 32) | (uint32_t(level) << 8) | variant;
    const auto it = snow_cache_.find(key);
    if (it != snow_cache_.end())
        return it->second;
    std::shared_ptr<const Sprite> s;
    if (const std::shared_ptr<const Sprite> plain = staticSprite(texpos))
        // a light dusting caps the top fifth, deep snow nearly half the plant
        s = TextureStore::snowCapped(*plain, 0.12f + 0.065f * level, uint32_t(texpos) * 31u + variant);
    snow_cache_[key] = s;
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
        levels[z]->ground_class.assign(size_t(w) * h, ~0u);
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

    // How deep the water in a tile is, in sevenths of a level: 7 with more water
    // over it; 0 for dry ground, -1 for rock (or beyond the map: no shore there).
    // A brook's top is its own floor: the game draws it with its own pictures.
    const auto water_depth = [&](int x, int y, int zz) -> int {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return -1;
        const df::tiletype *t = Maps::getTileType(x, y, zz);
        if (!t)
            return -1;
        const TileClass k = classify(*t);
        if (k == TileClass::Wall || k == TileClass::Canopy)
            return -1;
        const df::tile_designation *d = Maps::getTileDesignation(x, y, zz);
        if (!d || !d->bits.flow_size || tileShape(*t) == df::tiletype_shape::BROOK_TOP)
            return 0;
        const df::tile_designation *up = Maps::getTileDesignation(x, y, zz + 1);
        return up && up->bits.flow_size ? 7 : int(d->bits.flow_size);
    };
    // Water (or magma) standing in a tile: a see-through block as deep as the
    // game's n/7, in the game's picture for that depth, with the game's light rim
    // and a dithered edge towards dry ground, and its waves.
    // One of the game's ocean waves on a tile (a flow of the kind OceanWave or
    // SeaFoam, which its wave makers send towards the coast): the way it
    // travels (dx, dy) and how dense it is; false if none.
    const auto wave_at = [&](int x, int y, int zz, int &dx, int &dy, int &density) {
        const df::map_block *blk = Maps::getTileBlock(x, y, zz);
        if (!blk)
            return false;
        for (const df::flow_info *fl : blk->flows) {
            if (!fl || fl->density <= 0 || fl->pos.x != x || fl->pos.y != y || fl->pos.z != zz ||
                (fl->type != df::flow_type::OceanWave && fl->type != df::flow_type::SeaFoam))
                continue;
            dx = fl->dest.x > x ? 1 : fl->dest.x < x ? -1 : 0;
            dy = fl->dest.y > y ? 1 : fl->dest.y < y ? -1 : 0;
            density = fl->density;
            return true;
        }
        return false;
    };
    // The pictures of a wave on a tile: the one the game drew there (its flow
    // layer), else the game's streaks (FLOWS pages) going the wave's way, 16
    // steps of their animation, the page by how dense it is. Returns how many.
    const auto wave_pics = [&](int x, int y, int zz, int32_t drawn, int32_t *out) -> int {
        int dx = 0, dy = 0, density = 0;
        if (!wave_at(x, y, zz, dx, dy, density))
            return 0;
        ++wave_tiles;
        if (real(drawn)) {
            ++wave_pictures;
            out[0] = drawn;
            return 1;
        }
        // the pages' rows: NW, NE, SE, SW, N, S, W, E
        const int row = dy < 0 ? (dx < 0 ? 0 : dx > 0 ? 1 : 4) : dy > 0 ? (dx < 0 ? 3 : dx > 0 ? 2 : 5)
                                                                         : (dx < 0 ? 6 : 7);
        static const char *PAGES[4] = {"FLOWS", "FLOWS2", "FLOWS3", "FLOWS4"};
        const char *page = PAGES[std::clamp((density - 1) * 4 / 100, 0, 3)];
        for (int f = 0; f < WAVE_FRAMES; ++f)
            out[f] = page_tile(page, f, row);
        return out[0] > 0 ? WAVE_FRAMES : 0;
    };
    int32_t drawn_flow = 0;   // the game's flow picture on the tile being read (0: none, or not in view)
    const auto water_in = [&](Cell &c, int x, int y, int zz, const df::tile_designation &des) {
        const bool magma = des.bits.liquid_type == df::tile_liquid::Magma;
        c.wet = uint8_t(des.bits.flow_size);
        // more water over it, or (full) a floor or rock right on top: no surface of its own
        const df::tile_designation *up = Maps::getTileDesignation(x, y, zz + 1);
        const df::tiletype *up_t = Maps::getTileType(x, y, zz + 1);
        const bool under = (up && up->bits.flow_size) ||
                           (c.wet == 7 && up_t && classify(*up_t) != TileClass::Open);
        const int depth = under ? 7 : c.wet;
        uint8_t land = 0;
        int32_t rim[8] = {};
        if (!under) {
            static constexpr int DX[8] = {0, 0, -1, 1, -1, 1, -1, 1};
            static constexpr int DY[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
            // the rim pieces: column and row on the LIQUIDS page (magma's three rows lower)
            static constexpr int RX[8] = {5, 5, 4, 6, 4, 6, 4, 6};
            static constexpr int RY[8] = {0, 2, 1, 1, 0, 0, 2, 2};
            int nd[8];
            for (int i = 0; i < 8; ++i)
                nd[i] = water_depth(x + DX[i], y + DY[i], zz);
            for (int i = 0; i < 4; ++i)
                if (nd[i] == 0)
                    land |= uint8_t(1 << i);
            // a corner: dry ground diagonally only, or (the rim's rounded corner) on both sides
            static constexpr int SIDES[4][2] = {{0, 2}, {0, 3}, {1, 2}, {1, 3}};
            for (int i = 0; i < 4; ++i) {
                const bool a = nd[SIDES[i][0]] == 0, b = nd[SIDES[i][1]] == 0;
                if (nd[4 + i] == 0 && !a && !b)
                    land |= uint8_t(16 << i);
                if ((nd[4 + i] == 0 && !a && !b) || (a && b))
                    rim[4 + i] = page_tile("LIQUIDS", RX[4 + i], RY[4 + i] + (magma ? 3 : 0));
            }
            for (int i = 0; i < 4; ++i)
                if (land & (1 << i))
                    rim[i] = page_tile("LIQUIDS", RX[i], RY[i] + (magma ? 3 : 0));
        }
        // the game's ocean waves on its surface
        int32_t waves[WAVE_FRAMES];
        const int frames = !magma && !under ? wave_pics(x, y, zz, drawn_flow, waves) : 0;
        c.water = tex.water(page_tile("LIQUIDS", magma ? 1 : 0, 7 - depth), rim, land,
                            magma ? 1.0f : WATER_OPACITY, frames ? waves : nullptr, frames);
        if (under)
            c.flags |= CELL_UNDER;
        if (frames > 1)
            c.flags |= CELL_WAVES;
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
            L.ground_class[ci] = ~0u;   // (the tile may have changed: worked out again when asked)

            const df::tiletype *ttp = Maps::getTileType(mx, my, z);
            if (ttp) {
                const df::tiletype tt = *ttp;
                const df::tiletype_shape shape = tileShape(tt);
                const int tm = int(tileMaterial(tt));
                const TileClass cls = classify(tt);
                const int32_t bg = from_map ? 0 : vp->screentexpos_background[idx];
                // (a track symbol in a layer is never a thing: track_symbol_at finds it)
                const auto no_track = [](int32_t tp) { return is_track_symbol(tp) ? 0 : tp; };
                const int32_t bg2 = from_map ? 0 : no_track(vp->screentexpos_background_two[idx]);
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

                // A palm's fronds are leaves that grow on its trunk (the game draws
                // them in the trunk top's picture). A tile that carries such leaves
                // is trunk like the rest, with long fronds reaching out from it into
                // the tiles around (spreadFronds).
                const bool crown = tileMaterial(tt) == df::tiletype_material::TREE &&
                                   tileSpecial(tt) != df::tiletype_special::DEAD &&
                                   has_frond_leaves(mat_of(ci, mx, my, tt), mx, my, z);
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
                        // an engraved wall shows what was carved into it
                        if (cls == TileClass::Wall && tileSpecial(tt) == df::tiletype_special::SMOOTH) {
                            const int32_t picture = wall_engraving_picture(mx, my, z);
                            if (picture >= 0)
                                c.solid = tex.engraved(colorFor(USE_WALL, tm, mat), picture);
                        }
                    }
                    if (cls == TileClass::Wall && tileMaterial(tt) == df::tiletype_material::CONSTRUCTION) {
                        // a built wall: a plain band where it stands on the ground and
                        // where it ends at the top
                        const auto built_wall = [](int x, int y, int zz) {
                            const df::tiletype *n = Maps::getTileType(x, y, zz);
                            return n && tileMaterial(*n) == df::tiletype_material::CONSTRUCTION &&
                                   classify(*n) == TileClass::Wall;
                        };
                        if (!built_wall(mx, my, z - 1))
                            c.flags |= CELL_FOOT;
                        if (!built_wall(mx, my, z + 1))
                            c.flags |= CELL_HEAD;
                        c.trim = tex.material(Pattern::Grit, colorFor(USE_WALL, tm, mat));
                    }
                } else if (cls == TileClass::Canopy) {
                    const uint32_t mat = mat_of(ci, mx, my, tt);
                    if (shape == df::tiletype_shape::TRUNK_BRANCH || tileSpecial(tt) == df::tiletype_special::DEAD) {
                        // a thick or bare branch: wood
                        c.solid = tex.material(Pattern::Bark, colorFor(USE_WALL, tm, mat));
                    } else {
                        // (the canopy's whole block is leaves: see below)
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
                    // leaves and branches look the same from below, not like a
                    // ceiling; snow on them covers the top fully, the sides in part
                    const uint16_t plain = c.solid;
                    c.half = plain;
                    int32_t snow_top = 0, snow_side = 0;
                    if (spatters_.snow(mx, my, z, snow_top, snow_side)) {
                        c.top = tex.over(plain, snow_top);
                        c.solid = tex.over(plain, snow_side);
                    }
                } else {
                    const uint32_t mat = mat_of(ci, mx, my, tt);
                    const int32_t b1 = from_map ? 0 : vp->screentexpos_building_one[idx];
                    const int32_t b2 = from_map ? 0 : vp->screentexpos_building_two[idx];
                    const int32_t item = from_map ? 0 : no_track(vp->screentexpos_item[idx]);
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
                    // A hatch lies in the floor: closed, its picture fills the tile like
                    // a closed door's, but taken as a block it would bury the walls
                    // around whoever stands on it.
                    bool hatch = false;
                    if (occb == df::tile_building_occ::Dynamic) {
                        df::building *hb =
                            Buildings::findAtTile(df::coord(int16_t(mx), int16_t(my), int16_t(z)));
                        hatch = hb && hb->getType() == df::building_type::Hatch;
                    }
                    if (!hatch &&
                        (occb == df::tile_building_occ::Impassable || occb == df::tile_building_occ::Dynamic)) {
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
                        // Driftwood lies on the soil or sand of the place (the tile's
                        // material): its picture is a thing lying on the tile, the
                        // tile plain ground of that soil
                        const bool driftwood = tm == int(df::tiletype_material::DRIFTWOOD);
                        const int32_t drift = !driftwood                   ? 0
                                              : is_driftwood_picture(bg2)  ? bg2
                                              : is_driftwood_picture(bg)   ? bg
                                              : real(bg2) && !tree_bg2     ? bg2
                                                                           : 0;
                        const int32_t gbg = drift && drift == bg ? 0 : bg;   // the ground's picture
                        const df::tiletype gtt = driftwood ? df::tiletype::SoilFloor1 : tt;
                        const int gtm = driftwood ? int(df::tiletype_material::SOIL) : tm;
                        const bool flat_plant = real(bg2) && !tree_bg2 && cls == TileClass::Floor &&
                                                !has_building && !driftwood &&
                                                shape != df::tiletype_shape::BOULDER &&
                                                shape != df::tiletype_shape::SHRUB &&
                                                shape != df::tiletype_shape::SAPLING;
                        uint16_t ground = 0;
                        bool pictured = false;   // ground is the game's picture of this tile
                        const uint64_t kind = (uint64_t(uint16_t(gtt)) << 32) | mat;
                        // (the game's picture of stairs is not a floor: they get a shape)
                        const bool stairs = cls == TileClass::Floor && (stair_up(tt) || stair_down(tt));
                        if (real(gbg) && !tree_bg && !stairs) {
                            // a picture unlike this kind of ground is only taken once it
                            // has been read on this tile twice
                            uint32_t seen = 0;
                            const bool colored = tex.averageColor(gbg, seen);
                            const bool confirmed = L.ground_seen[ci] == gbg;
                            L.ground_seen[ci] = gbg;
                            if (!colored || groundFits(kind, seen, confirmed)) {
                                // whatever grows or lies flat on the ground is part of its surface
                                ground = flat_plant ? tex.floorOver(gbg, bg2) : tex.floor(gbg);
                                pictured = ground != 0;
                                if (ground && colored)
                                    learn(color_key(USE_FLOOR, gtm, mat), seen);
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
                            const uint16_t was = tex.unfringed(tex.bare(old.ramp ? old.top : old.flr));
                            if (was && !tex.isMaterial(was))
                                ground = was;
                        }
                        if (!ground)   // not seen yet: the right kind of ground in the right colour
                            ground = tex.material(floor_pattern(gtt, matInfo(mat).wood), colorFor(USE_FLOOR, gtm, mat));

                        // stronger ground beside (grass beside soil) reaches in along an
                        // uneven line
                        const uint32_t own_class = ground_class(tt, mat);
                        L.ground_class[ci] = own_class;
                        if (own_class && ground && flow < 4) {
                            auto &look = class_look_[own_class];
                            if (!look.first || (pictured && !look.second))
                                look = {ground, pictured};
                            const auto class_at = [&](int x, int y) -> uint32_t {
                                if (x < 0 || y < 0 || x >= w || y >= h)
                                    return 0;
                                const int ni = y * w + x;
                                uint32_t &k = L.ground_class[ni];
                                if (k == ~0u) {
                                    const df::tiletype *nt = Maps::getTileType(x, y, z);
                                    k = nt ? ground_class(*nt, mat_of(ni, x, y, *nt)) : 0;
                                }
                                return k;
                            };
                            // N, S, W, E, then NW, NE, SW, SE
                            static constexpr int DX[8] = {0, 0, -1, 1, -1, 1, -1, 1};
                            static constexpr int DY[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
                            uint16_t from[8] = {};
                            for (int i = 0; i < 8; ++i) {
                                const uint32_t k = class_at(mx + DX[i], my + DY[i]);
                                if (k <= own_class)
                                    continue;
                                const auto nl = class_look_.find(k);
                                if (nl != class_look_.end())
                                    from[i] = nl->second.first;
                            }
                            // a corner only where neither side beside it already has a line
                            static constexpr int SIDES_OF[4][2] = {{0, 2}, {0, 3}, {1, 2}, {1, 3}};
                            for (int i = 0; i < 4; ++i)
                                if (from[SIDES_OF[i][0]] || from[SIDES_OF[i][1]])
                                    from[4 + i] = 0;
                            // each side's line by where that side lies, so it does not
                            // repeat along a long border
                            const auto edge_bit = [](int x, int y, uint32_t across) {
                                uint32_t hh = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ across * 83492791u;
                                hh = (hh ^ (hh >> 13)) * 1274126177u;
                                return (hh >> 16) & 1u;
                            };
                            const uint32_t variants = edge_bit(mx, my, 0) | edge_bit(mx, my + 1, 0) << 1 |
                                                      edge_bit(mx, my, 1) << 2 | edge_bit(mx + 1, my, 1) << 3;
                            ground = tex.fringed(ground, from, variants);
                        }

                        // snow, blood, mud, fallen leaves ... lying on it, each kind a
                        // layer: mud under snow under blood. In view the game's own
                        // picture is used for the kind it drew. Not on a slope: there the
                        // game's spatter layer left slopes bare, so the map decides.
                        int32_t seen = 0;
                        if (!out_of_sight && vp->screentexpos_spatter && cls != TileClass::Ramp) {
                            seen = no_track(vp->screentexpos_spatter[idx]);
                            if (real(seen))
                                spatters_.learn(mx, my, z, seen);
                            else
                                seen = 0;
                        }
                        Spatters::Layer layer[8];
                        const int layers =
                            spatters_.layers(mx, my, z, seen, cls == TileClass::Ramp, layer, 8);
                        uint16_t covered = ground;
                        for (int li = 0; li < layers; ++li)
                            covered = tex.overHoled(covered, layer[li].tp, layer[li].holes);
                        // tracks (the game's tracks display): over everything lying there
                        if (!from_map && !out_of_sight)
                            if (const int32_t track = track_symbol_at(vp, idx))
                                covered = tex.over(covered, track);

                        float raise = 0;
                        if (cls == TileClass::Ramp) {
                            c.ramp = ramp_corners(mx, my, z);
                            c.top = covered;
                            c.half = tex.material(wall_pattern(tt, matInfo(mat).wood), colorFor(USE_WALL, tm, mat));
                            // snow lies over the sides too, so a snowed slope does not
                            // stand out of the snow as bare earth
                            const df::tiletype *above = Maps::getTileType(mx, my, z + 1);
                            if (spatters_.snowLevel(mx, my, z) ||
                                (above && tileShape(*above) == df::tiletype_shape::RAMP_TOP &&
                                 spatters_.snowLevel(mx, my, z + 1)))
                                c.half = covered;
                            raise = ramp_height(c.ramp, 0.5f, 0.5f);
                        } else if (!(stairs && stair_hole(mx, my, z))) {
                            // (stairs that lead down have no floor: the steps below show)
                            c.flr = covered;
                        }
                        c.flags = cover_flags(mx, my, z);

                        // furniture: a simple shape, in the colour the game shows it
                        // in, with the game's picture of it on top
                        df::building *bld = nullptr;
                        bool stairwell_hatch = false;
                        if (has_building && !doorway) {
                            bld = Buildings::findAtTile(df::coord(int16_t(mx), int16_t(my), int16_t(z)));
                            int shape = bld ? furniture_shape(bld->getType()) : -1;
                            // a shop sign hangs from the wall whatever the ground; other
                            // things on a slope stay pictures
                            if (shape == SH_SLAB && is_shop_sign(bld)) {
                                shape = SH_SIGN;
                            } else if (shape == SH_HATCH && stairs && stair_up(tt)) {
                                // a hatch on stairs that also go up is not drawn: the flight
                                // standing in the tile would cut through it
                                shape = -1;
                                bld = nullptr;
                                stairwell_hatch = true;
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
                                // stone and metal furniture is one even grit (the slab joints of
                                // a smooth floor cut crosses and lines into small blocks); wooden
                                // furniture shows planks, except where it is plain all over
                                const bool plain = built || shape == SH_WELL || shape == SH_STATUE || shape == SH_ALTAR;
                                uint16_t side = tex.material(
                                    matInfo(bmat).wood && !plain ? Pattern::Planks : Pattern::Grit, color);
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
                                if (shape == SH_HATCH) {
                                    // shut, the lid carries the game's picture of it; out of
                                    // sight, as one of its kind looked shut. Grates and bars
                                    // in the floor do not open.
                                    const auto *hatch = virtual_cast<df::building_hatchst>(bld);
                                    const bool shut = !hatch || hatch->door_flags.bits.closed;
                                    facing = shut ? 1 : 0;
                                    if (shut) {
                                        const uint64_t fkey = (uint64_t(shape) << 32) | bmat;
                                        face = picture ? tex.flatWall(0, picture) : 0;
                                        if (face)
                                            front_kind_[fkey] = face;
                                        else if (const auto f = front_kind_.find(fkey); f != front_kind_.end())
                                            face = f->second;
                                        else if (bld->getType() == df::building_type::Hatch) {
                                            // Never seen yet. A hatch is mostly seen from below,
                                            // where the game shows nothing of the level above:
                                            // the game's own picture of a shut lid of its kind
                                            // of material (rows: wood, stone, metal, glass).
                                            const MaterialInfo mi(bld->mat_type, bld->mat_index);
                                            const auto has = [&](df::material_flags fl) {
                                                return mi.isValid() && mi.material && mi.material->flags.is_set(fl);
                                            };
                                            const int row = has(df::material_flags::WOOD)       ? 0
                                                            : has(df::material_flags::IS_METAL) ? 2
                                                            : has(df::material_flags::IS_GLASS) ? 3
                                                                                                : 1;
                                            if (const int32_t lid = page_tile("ITEM_HATCH_COVER", 1, row); lid > 0)
                                                face = tex.flatWall(0, lid);
                                        }
                                    }
                                }
                                if (built)
                                    facing = is_wall(mx, my - 1, z)   ? 0
                                             : is_wall(mx, my + 1, z) ? 1
                                             : is_wall(mx - 1, my, z) ? 2
                                             : is_wall(mx + 1, my, z) ? 3
                                                                      : 4;
                                if (shape == SH_SIGN) {
                                    // hung from a wall next to it, on a slope as on a floor:
                                    // it belongs to the door of its own level. A built wall
                                    // (the shop's) wins over the rock a slope rises against.
                                    static constexpr int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
                                    facing = 4;
                                    for (int d = 0; d < 4; ++d) {
                                        const int wx = mx + DX[d], wy = my + DY[d];
                                        if (!is_wall(wx, wy, z))
                                            continue;
                                        const df::tiletype *wt = Maps::getTileType(wx, wy, z);
                                        if (wt && tileMaterial(*wt) == df::tiletype_material::CONSTRUCTION) {
                                            facing = d;
                                            break;
                                        }
                                        if (facing == 4)
                                            facing = d;
                                    }
                                }
                                if (shape == SH_WELL)
                                    well_here(c, mx, my, side);
                                else if (pictured && !face && old.box)
                                    c.box = old.box;    // out of sight: as it looked when last seen
                                else
                                    c.box = boxSet(shape, side, face, facing, color, matInfo(bmat).wood);
                            }
                        }
                        if (stairs && stair_up(tt) && !c.box) {
                            // a spiral, entered from a side of the lowest tile of its
                            // stack that is not a wall (each level turns once, so all
                            // its levels start and end on that side)
                            int bottom = z;
                            for (int i = 0; i < 64 && stair_hole(mx, my, bottom); ++i)
                                --bottom;
                            const int entry = !is_wall(mx, my - 1, bottom)   ? 0
                                              : !is_wall(mx + 1, my, bottom) ? 1
                                              : !is_wall(mx, my + 1, bottom) ? 2
                                              : !is_wall(mx - 1, my, bottom) ? 3
                                                                             : 0;
                            const int facing = entry | (bottom < z ? 4 : 0);
                            c.box = boxSet(SH_STAIRS,
                                           tex.material(matInfo(mat).wood ? Pattern::Planks : Pattern::Grit,
                                                        colorFor(USE_FLOOR, tm, mat)),
                                           0, facing);
                        }
                        // what lies on a table lies on its top
                        const float lift = c.box && c.box < boxsets.size() ? boxsets[c.box].surface / LEVEL_H : 0.0f;

                        // things standing on the tile; a sprite only counts where
                        // the map agrees that something of its kind is there
                        // a piece of furniture fills its tile: nothing grows or lies on
                        // the ground under it (a shop sign hangs high, leaving the ground)
                        // (nor does a hatch: it lies in the floor and things lie on it)
                        const bool furnished = bld && c.box && !is_shop_sign(bld) &&
                                               furniture_shape(bld->getType()) != SH_HATCH;
                        // snow lying on the tile lies on a plant or boulder standing there too
                        const auto add_snowy = [&](BillKind kind, int32_t pic) {
                            const int snow = spatters_.snowLevel(mx, my, z);
                            if (snow > 0 && real(pic) && !is_status_tile(pic))
                                place(snowySprite(pic, snow, uint32_t(mx * 7 + my * 13) & 3u), pic, mx, my, kind, 0,
                                      raise);
                            else
                                add_bill(pic, mx, my, kind, 0, raise);
                        };
                        if (!out_of_sight) {
                            if (furnished) {
                            } else if (shape == df::tiletype_shape::BOULDER)
                                add_snowy(BillKind::Boulder, bg2);
                            else if (shape == df::tiletype_shape::SHRUB || shape == df::tiletype_shape::SAPLING)
                                add_snowy(BillKind::Plant, bg2);
                            else if (driftwood) {
                                if (drift)
                                    add_snowy(BillKind::Item, drift);
                            } else if (!flat_plant && !tree_bg2)
                                add_bill(bg2, mx, my, BillKind::Other, 0, raise);
                            if (!doorway && has_building && !c.box && !stairwell_hatch) {
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
            if (!c.solid && ttp && tileShape(*ttp) != df::tiletype_shape::BROOK_TOP) {
                drawn_flow = (!from_map && vp->screentexpos_high_flow) ? vp->screentexpos_high_flow[idx] : 0;
                const df::tile_designation *des = Maps::getTileDesignation(mx, my, z);
                if (des && des->bits.flow_size > 0) {
                    water_in(c, mx, my, z, *des);
                } else if (c.flr && !c.ramp) {
                    // a wave run up onto dry ground: drawn on it (only the game's picture)
                    int32_t waves[WAVE_FRAMES];
                    if (wave_pics(mx, my, z, drawn_flow, waves) == 1)
                        c.flr = tex.over(c.flr, waves[0]);
                }
            }
            L.cells[ci] = c;
            L.known[ci] = 1;
            if (c.box)
                col_box[ci] = 1;
            if (c.solid || c.half || c.flr || c.top || c.frame || c.box || c.water || (c.flags & CELL_ROOFED)) {
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

namespace {

// The level of the conflict a creature is in (df::conflict_level), -1 if it is
// in none. In a conflict you are part of too, it is the level between its side
// and yours; otherwise the highest its side has with any other. with_you_out
// is the level of its conflict with you alone, -1 if there is none.
int conflict_of(const df::unit *u, const df::unit *self, int &with_you_out) {
    int best = -1;
    with_you_out = -1;
    const auto on_side = [](const df::conflict_sidest *s, const df::unit *who) {
        if (std::find(s->unit_ids.begin(), s->unit_ids.end(), who->id) != s->unit_ids.end())
            return true;
        return who->hist_figure_id >= 0 &&
               std::find(s->histfig_ids.begin(), s->histfig_ids.end(), who->hist_figure_id) != s->histfig_ids.end();
    };
    for (const int32_t id : u->activities) {
        const df::activity_entry *act = df::activity_entry::find(id);
        if (!act)
            continue;
        for (df::activity_event *ev : act->events) {
            auto *c = ev ? virtual_cast<df::activity_event_conflictst>(ev) : nullptr;
            if (!c)
                continue;
            const df::conflict_sidest *mine = nullptr, *yours = nullptr;
            for (const df::conflict_sidest *s : c->sides) {
                if (!s)
                    continue;
                if (!mine && on_side(s, u))
                    mine = s;
                if (self && !yours && on_side(s, self))
                    yours = s;
            }
            if (!mine || mine == yours)
                continue;
            int with_you = -1, highest = -1;
            for (const df::conflict_statest *st : mine->enemies) {
                if (!st)
                    continue;
                const int lv = int(st->conflict_level);
                highest = std::max(highest, lv);
                if (yours && st->id == yours->id)
                    with_you = lv;
            }
            best = std::max(best, with_you >= 0 ? with_you : highest);
            with_you_out = std::max(with_you_out, with_you);
        }
    }
    return best;
}

// the colours the game writes the conflict levels in, from its own palette
uint32_t conflict_colour(int level) {
    int idx = 7;                                              // Encounter: grey
    switch (df::conflict_level(level)) {
    case df::conflict_level::Horseplay: idx = 10; break;      // light green
    case df::conflict_level::Training: idx = 15; break;       // white
    case df::conflict_level::Brawl: idx = 11; break;          // light cyan
    case df::conflict_level::Nonlethal: idx = 14; break;      // yellow
    case df::conflict_level::Lethal: idx = 12; break;         // light red
    case df::conflict_level::NoQuarter: idx = 13; break;      // light magenta
    default: break;
    }
    if (!gps)
        return rgb(200, 200, 200);
    return rgb(gps->uccolor[idx][0], gps->uccolor[idx][1], gps->uccolor[idx][2]);
}

}   // namespace

int32_t mountOf(const df::unit *u) {
    if (!u || !u->flags1.bits.rider)
        return -1;
    return u->relationship_ids[df::unit_relationship_type::RiderMount];
}

int conflictWithYou(const df::unit *u, const df::unit *self) {
    int with_you = -1;
    if (u && self)
        conflict_of(u, self, with_you);
    return with_you;
}

void Scene::buildUnits(int zlo, int zhi, const df::unit *self, uint32_t frame, bool outlines, bool melee_reach,
                       bool smooth_units, bool any_conflict) {
    units.clear();
    mount.reset();
    if (!world)
        return;
    // What you ride shares your tile and would fill the view from inside it:
    // it is left out here and its picture kept for the bottom of the screen.
    // Its own picture if it has one, else what the game drew for it while it
    // stood alone, else its kind's.
    const int32_t mount_id = mountOf(self);
    if (df::unit *m = mount_id >= 0 ? df::unit::find(mount_id) : nullptr) {
        Piece p[6];
        int n = own_pieces(m, p);
        if (!n) {
            if (const auto it = unit_look_.find(m->id); it != unit_look_.end()) {
                it->second.frame = frame;
                if (it->second.large)
                    mount = it->second.large;
                else if (it->second.texpos)
                    p[n++] = Piece{it->second.texpos, 0, 0};
            } else {
                n = raw_pieces(m, p);
            }
        }
        if (!mount && n)
            mount = TextureStore::composite(p, n);
    }
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
        if (u == self || u->id == mount_id || !Units::isActive(const_cast<df::unit *>(u)) || Units::isDead(const_cast<df::unit *>(u)))
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
        Piece held[6];
        if (seen != unit_seen_.end() && frame - seen->second < HOLD_FRAMES &&
            (own_pieces(u, held) || unit_look_.count(u->id) || raw_pieces(u, held)))
            return &remembered;
        return nullptr;
    };

    for (const df::unit *u : world->units.active)
        if (visible(u))
            ++counts[key_of(u)];
    // creatures on your own tile stand in a ring around you
    const int64_t self_key = self ? key_of(self) : -1;

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
            // its colour: the game's own for the conflict it is in, green a
            // companion, grey anyone who is in no conflict
            const int32_t me = self ? self->id : -1;
            int with_you = -1;
            // a conflict between others only counts when the player asked for it
            const int any = conflict_of(u, self, with_you);
            const int level = any_conflict ? any : with_you;
            b.fight = level >= 0;
            b.foe = int8_t(with_you);
            if (u->relationship_ids[df::unit_relationship_type::GroupLeader] == me ||
                u->relationship_ids[df::unit_relationship_type::PetOwner] == me)
                b.ring = rgb(60, 230, 70);
            else if (level >= 0)
                b.ring = conflict_colour(level);
            else
                b.ring = rgb(150, 150, 150);
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

        // Its own picture if the game keeps one for it. What the game drew on
        // its tile is only used for a creature without one: on a tile shared
        // by several the game shows them (and their status bubbles) in turn,
        // so that is not a picture of any one of them.
        Piece mine[6];
        int mine_n = own_pieces(u, mine);
        // what the game drew on the creature's tile may be one of its status bubbles
        const int32_t drawn = is_status_tile(ct->texpos) ? 0 : ct->texpos;
        // What the game drew on the tile, when it is one of this creature's
        // kind's pictures, is this creature (the game takes turns on a shared
        // tile): remembered for the frames it shows something else. A picture
        // not in the raws is only taken from a creature alone on its tile.
        // Otherwise the remembered picture, else its kind's plain one.
        const bool alone = n == 1 && key != self_key && ct != &remembered;
        const UnitLook *look = nullptr;
        if (!mine_n) {
            const bool ours = drawn && (is_kind_picture(u, drawn) ||
                                        (alone && !unit_look_.count(u->id) && !raw_pieces(u, mine)));
            if (ours) {
                UnitLook &l = unit_look_[u->id];
                l.texpos = drawn;
                l.large = ct->large;
                l.anchor_px = ct->anchor_px;
                l.frame = frame;
                look = &l;
            } else if (const auto it = unit_look_.find(u->id); it != unit_look_.end()) {
                it->second.frame = frame;
                look = &it->second;
            } else {
                mine_n = raw_pieces(u, mine);
            }
        }
        // the game picture a lying creature's turned copy is kept by (0: by sprite)
        int32_t own = mine_n ? mine[0].texpos : 0;
        if (mine_n) {
            b.sprite = TextureStore::composite(mine, mine_n);
            b.scale = creature_scale(u);
            // (what it holds out to one side moves its middle a little)
            if (b.sprite)
                b.anchor_px = (b.sprite->x0 + b.sprite->x1) * 0.5f;
        } else if (look && look->large) {
            // drawn by the game across several tiles: already as big as it should be
            b.sprite = look->large;
            b.anchor_px = look->anchor_px;
        } else if (look) {
            const Piece piece{look->texpos, 0, 0};
            b.sprite = TextureStore::composite(&piece, 1);
            b.scale = creature_scale(u);
            own = look->texpos;
        } else if (ct->large && drawn) {
            b.sprite = ct->large;
            b.anchor_px = ct->anchor_px;
        } else {
            const Piece piece{drawn, 0, 0};
            b.sprite = TextureStore::composite(&piece, 1);
            b.scale = creature_scale(u);
            own = drawn;
        }
        if (!b.sprite)
            continue;
        if (b.sprite == ct->large || (look && b.sprite == look->large))
            own = 0;

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
                turned = proneSprite(own, b.sprite);
            else
                turned = upside_down(own, b.sprite);
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
                turned = proneSprite(own, b.sprite);
            if (turned) {
                b.sprite = turned;
                b.anchor_px = (turned->x0 + turned->x1) * 0.5f;
            } else {
                b.sink = 0.25f;
            }
        }

        // several creatures on one tile stand in a small ring
        float ox = 0, oy = 0;
        if (key == self_key) {
            // around you, out past the nearest distance anything is drawn at
            const float a = 6.2831853f * (float(i) + 0.5f) / float(n);
            ox = 0.45f * std::cos(a);
            oy = 0.45f * std::sin(a);
        } else if (n > 1) {
            const float a = 6.2831853f * float(i) / float(n);
            ox = 0.3f * std::cos(a);
            oy = 0.3f * std::sin(a);
        }
        // on a spiral stair, on a step (the middle is its column)
        float lift = 0;
        if (const Level *L = level(u->pos.z); L && u->pos.x >= 0 && u->pos.y >= 0 && u->pos.x < w && u->pos.y < h) {
            const uint16_t id = L->cells[u->pos.y * w + u->pos.x].box;
            if (id && id < boxsets.size() && boxsets[id].stand_u >= 0) {
                const BoxSet &s = boxsets[id];
                if (n <= 1 && key != self_key) {
                    ox = s.stand_u - 0.5f;
                    oy = s.stand_v - 0.5f;
                }
                lift = s.stand_h;
            }
        }
        b.x = u->pos.x + 0.5f + ox;
        b.y = u->pos.y + 0.5f + oy;
        b.base = (u->pos.z + groundRaise(u->pos.x, u->pos.y, u->pos.z)) * LEVEL_H + lift;
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
        std::erase_if(unit_look_, [&](const auto &entry) { return frame - entry.second.frame > 20000; });
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
