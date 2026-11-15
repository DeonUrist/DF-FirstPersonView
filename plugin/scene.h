// The multi-level scene the first-person renderer draws.
//
// The shape of the world comes from the map itself: every wall, floor, slope
// and tree is there whether or not the adventurer has it in view. Walls and
// trees are drawn with pre-made patterns tinted in the colour of what they
// are made of; that colour is learned from the game's own sprites the first
// time a material is seen and guessed from the material before that.
//
// Everything else comes from the map viewport, where the game stores, per
// layer (ground, plants, items, creatures, ...), the sprite it draws on each
// visible tile. Scene::readLevel() reads those for the z-level the viewport
// currently shows. The plugin points the viewport at neighbouring levels and
// map windows in turn, so the whole neighbourhood ends up in the cache.
#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common.h"
#include "models.h"
#include "spatter.h"
#include "textures.h"

namespace df {
struct unit;
struct item;
}

namespace fpv {

enum : uint8_t {
    CELL_SKY = 1,       // open to the sky
    CELL_ROOFED = 2,    // something solid directly overhead
    // a built wall's plain band (Cell::trim) along its foot (nothing built
    // below it) and along its head (nothing built above it)
    CELL_FOOT = 4,
    CELL_HEAD = 8,
    // water: more water above, so this tile is full and has no surface of its own
    CELL_UNDER = 16,
    // water: Cell::water is the first of WAVE_FRAMES textures, one per step of the waves' animation
    CELL_WAVES = 32,
};

constexpr int WAVE_FRAMES = 16;      // the game's FLOWS pages: 16 pictures a direction
constexpr float WATER_OPACITY = 0.85f;   // water lets 15% of what lies under it show through

constexpr float TRIM_H = 0.18f;   // height of a built wall's foot or head band, in tile widths

enum : uint8_t {
    RAMP = 16,          // the tile is a slope
    RAMP_NW = 1,        // corners of the slope that are up at the next level
    RAMP_NE = 2,
    RAMP_SW = 4,
    RAMP_SE = 8,
    // a channel through the slope, so that what lies beyond an edge raised by
    // the walls beside it (a door in a wall line) can be seen over the dip
    RAMP_VALLEY_U = 32,   // running north-south: low along u = 0.5
    RAMP_VALLEY_V = 64,   // running west-east: low along v = 0.5
    // with one of the two above: a ridge instead of a channel, high along that
    // line and down to the ground at both sides (a slope standing out from a
    // wall with open ground on both sides of it)
    RAMP_RIDGE = 128,
};

// one map tile on one z-level; texture ids index Scene::tex, 0 = none
struct Cell {
    uint16_t solid = 0;   // full-height block
    uint16_t half = 0;    // sides of a low block (fortification) or of a slope
    uint16_t flr = 0;     // floor surface
    uint16_t top = 0;     // raised surface (top of a low block, slope, water)
    uint16_t frame = 0;   // see-through face on the tile's sides (open door frame)
    uint8_t topq = 0;     // height of the raised surface in sixteenths of a level
    uint8_t flags = 0;
    uint8_t ramp = 0;     // RAMP plus the corners that are up, or 0
    uint16_t box = 0;     // shapes standing inside the tile: index into Scene::boxsets, 0 = none
    uint16_t trim = 0;    // plain texture of a built wall's bands (CELL_FOOT, CELL_HEAD)
    // Water (or magma) in the tile: a see-through block standing on its floor
    // (or slope), its top the surface (TextureStore::water; with CELL_WAVES the
    // first of a run of frames). 0 = none.
    uint16_t water = 0;
    uint8_t wet = 0;      // how deep it is, in sevenths of a level (the game's 1..7)
    // With a full-height block (solid), top and half are its top and underside
    // when they differ from its sides (a tree's leaves: the same leaves below
    // rather than the grey of a ceiling, snow on top); 0 = as usual.

    bool operator==(const Cell &) const = default;
};

// sides of a block, for Box::face_sides
enum : uint8_t { FACE_N = 1, FACE_S = 2, FACE_W = 4, FACE_E = 8 };

constexpr int ROUND_MAX = 12;   // corners of a round piece's outline, at most
constexpr int BOX_MAX = 24;     // blocks in one BoxSet, at most

// A rectangular block inside a tile: furniture and battlements are made of a
// few of these.
struct Box {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;   // its footprint inside the tile, 0..1 (u east, v south)
    float z0 = 0, z1 = 1;                   // bottom and top above the level's floor, in tile widths
    uint16_t side = 0, top = 0;             // textures
    uint16_t under = 0;                     // its underside, if not side
    uint16_t face = 0;                      // a picture on some of its sides (face_sides), stretched over them
    uint8_t face_sides = 0;                 // FACE_* bits
    // A round piece instead (a step of a spiral stair, its column): an upright
    // prism on a convex outline of `round` corners (arcs as straight pieces),
    // u0..v1 being only the box around it. 0: a plain block.
    uint8_t round = 0;
    float ru[ROUND_MAX], rv[ROUND_MAX];     // corners, in tile coordinates, going round it
    float rnu[ROUND_MAX], rnv[ROUND_MAX];   // outward normal of the side from corner i to i + 1
    // A dark line along the edges of each of its faces, this wide (tile widths;
    // 0: none), for the look of the game's outlined pixel art. On a round piece
    // only the corners in `sharp` (bit i: corner i) get an upright line, so a
    // round column is not striped.
    float edge = 0;
    uint16_t sharp = 0;
};

// how dark the line along a block's edges is: its face's colour times this
constexpr float EDGE_DARK = 0.45f;

// Where the line o + t d (tile coordinates, o relative to the tile's corner)
// is inside a round piece: false if nowhere. (enter_nu, enter_nv): the outward
// normal of the side it comes in through, (0, 0) if it starts inside.
inline bool roundClip(const Box &b, float ou, float ov, float du, float dv, float &tin, float &tout,
                      float &enter_nu, float &enter_nv) {
    tin = -1e30f;
    tout = 1e30f;
    enter_nu = enter_nv = 0;
    for (int i = 0; i < b.round; ++i) {
        // inside: n . (o + t d - corner) <= 0
        const float num = b.rnu[i] * (b.ru[i] - ou) + b.rnv[i] * (b.rv[i] - ov);
        const float den = b.rnu[i] * du + b.rnv[i] * dv;
        if (den > -1e-9f && den < 1e-9f) {
            if (num < 0)
                return false;
            continue;
        }
        const float t = num / den;
        if (den < 0) {
            if (t > tin) {
                tin = t;
                enter_nu = b.rnu[i];
                enter_nv = b.rnv[i];
            }
        } else if (t < tout) {
            tout = t;
        }
        if (tout <= tin)
            return false;
    }
    return tin < tout;
}

struct BoxSet {
    int n = 0;
    Box b[BOX_MAX];
    // where a creature on the tile stands (in tile coordinates and tile widths
    // above the floor), if not in the middle: on the first step of a spiral stair
    float stand_u = -1, stand_v = 0, stand_h = 0;
    float surface = 0;      // height at which things put on it lie (a table top), in tile widths
    bool frond_hub = false; // the crown of a palm: its fronds reach into the tiles around
    bool frond_arm = false; // part of a frond reaching in from a palm crown nearby
};

enum class BillKind : uint8_t { Creature, Item, Plant, Boulder, Building, Flying, Other };

// something that stands on a tile and always faces the camera
struct Billboard {
    float x = 0, y = 0;       // map position of its centre, in tiles
    float base = 0;           // height of the ground under it, in tile widths
    float scale = 1;          // size multiplier
    float sink = 0;           // fraction of its height below the ground
    std::shared_ptr<const Sprite> sprite;
    float anchor_px = 16;     // x of the tile centre inside the sprite, in pixels
    int32_t unit_id = -1;
    int16_t mx = 0, my = 0, mz = 0;
    BillKind kind = BillKind::Other;
    uint8_t order = 0;        // draw order among things on the same tile
    int32_t texpos = 0;       // game sprite it was made from (0 for creatures)
    uint32_t outline = 0;     // colour of a line drawn around the sprite, 0 = none
    uint32_t ring = 0;        // a creature's colour: its conflict's level, companion, or grey for none
    bool fight = false;       // the creature is in a conflict: the disc under it is always drawn
    int8_t foe = -1;          // level of the conflict it is in with you (df::conflict_level), -1 = none
    int8_t move_dx = 0, move_dy = 0;   // the tile a creature steps to next, relative to its own
};

struct CreatureTile {
    int32_t texpos = 0;                        // sprite the game drew on the tile
    std::shared_ptr<const Sprite> large;       // set for creatures drawn across several tiles
    float anchor_px = 16;
};

// The land beyond what the game has loaded, out to the horizon (farland.h),
// made of blocks like the map itself: flat-topped cells at whole z-levels,
// with sides down to their lower neighbours, and trees of boxes.

// a block: x and y in map tiles, z in tile widths
struct FarBox {
    float x0, y0, z0, x1, y1, z1;
    uint32_t top, side;        // colours, before light and haze
    uint8_t snow = 0;          // how thickly snow specks lie on its faces (0..255), as on the map's snowy leaves
    bool leaf = false;         // leaves (not a trunk) of a tree
};

// One ring of cells: n x n of them, `step` tiles wide, the first with its
// corner at (x0, y0); the cells in [hx0, hx1) x [hy0, hy1) are the finer ring's.
struct FarLevel {
    int n = 0;
    float x0 = 0, y0 = 0, step = 4;
    std::vector<float> h;              // top of each cell, in tile widths
    std::vector<float> low;            // 4 a cell (north, south, west, east): how far down that side shows
    std::vector<uint32_t> top, side;   // colours of its top and of its sides
    std::vector<uint8_t> wet;          // 1: the cell is water (the sea or a lake)
    int hx0 = 0, hx1 = 0, hy0 = 0, hy1 = 0;
    bool hole(int i, int j) const { return i >= hx0 && i < hx1 && j >= hy0 && j < hy1; }
};

struct FarTree {
    float x, y;                        // middle of its trunk, in map tiles
    float reach, height;               // how far it spreads (tiles) and stands (tile widths)
    uint32_t full, n_full;             // its blocks in FarView::boxes, as seen near
    uint32_t simple, n_simple;         // ... and far: one block a level
};

struct FarView {
    std::vector<FarLevel> levels;      // finest first
    std::vector<FarBox> boxes;
    std::vector<FarTree> trees;
    // for the software renderer: the top of the trees on each tile of a square
    // tw x tw tiles from (tx0, ty0), and its colour (no tree: below everything)
    int tx0 = 0, ty0 = 0, tw = 0;
    std::vector<float> tree_top;
    std::vector<uint32_t> tree_col;
    std::vector<uint8_t> tree_snow;
    float top = 0;                     // the highest top of all, in tile widths
    int region_x = 0, region_y = 0;    // the game's window on the world it was made for
    bool empty() const { return levels.empty(); }
    // the cell that holds a point: ring and place; false outside them all
    bool cellAt(float x, float y, int &k, int &i, int &j) const;
    // The top of the land (or a tree) at a point, its colour, the colour of its
    // sides, and a number that differs between neighbouring cells; false outside.
    bool sample(float x, float y, float &h, uint32_t &top_col, uint32_t &side_col, int &id) const;
};

// the colours of a kind of tree seen from afar: its leaves and its bark
struct FarTreeLook {
    uint32_t leaves = 0, bark = 0;
};

struct Level {
    std::vector<Cell> cells;                              // map_w * map_h
    std::vector<uint32_t> mats;                           // what each tile is made of (see Scene::matOf)
    std::vector<uint8_t> known;                           // tiles that have been read at least once
    std::vector<int32_t> ground_seen;                     // the game's ground picture last read on each tile
    std::vector<uint32_t> ground_class;                   // see ground_class in scene.cpp (~0u: not worked out)
    std::vector<Billboard> bills;                         // everything except creatures
    std::unordered_map<int32_t, CreatureTile> creatures;  // tiles where the game drew a creature
};

class Scene {
public:
    // guards everything below: the simulation thread writes while it captures,
    // the render thread reads while it draws
    std::mutex mutex;

    int w = 0, h = 0, d = 0;
    std::vector<std::unique_ptr<Level>> levels;   // indexed by z
    std::vector<Billboard> units;                 // creatures, rebuilt every capture
    // The creature you ride is not among them: its picture is drawn at the
    // bottom of the screen instead (null when you ride nothing or it has none).
    std::shared_ptr<const Sprite> mount;
    TextureStore tex;
    std::vector<BoxSet> boxsets;                  // shapes that tiles refer to; [0] is unused
    // the shape of one kind of thing in the given textures (made once, then shared)
    // (picture: the game's drawing of the thing, for shapes that carry it on a side;
    // facing: for a cabinet, 0..3 = back to the north/south/west/east wall, 4 = free standing)
    // color/wood: the colour and kind of material of side, for shapes with parts in
    // shades of it (cabinet doors)
    uint16_t boxSet(int shape, uint16_t side, uint16_t picture, int facing = 0, uint32_t color = 0,
                    bool wood = false);
    // use these models (media/models) from now on: every shape made so far is made again
    void setModels(std::shared_ptr<const Models> models);
    // the width of the lines along the edges of models, in tile widths (0: none)
    void setEdgeWidth(float w);
    float edgeWidth() const { return edge_width_; }
    uint64_t version = 0;                         // goes up whenever the scene changes
    // tiles read with one of the game's ocean waves on them, and how many of
    // those had the game's own picture of it in the map (the rest: streaks made up)
    uint32_t wave_tiles = 0, wave_pictures = 0;
    // lowest and highest level with anything to draw, per map tile: the
    // renderer skips the empty levels above and below
    std::vector<int16_t> col_lo, col_hi;
    std::vector<uint8_t> col_box;                 // per map tile: some level has shapes standing in it

    // forget everything (new map, or texture numbers were reassigned)
    void reset();

    // The game moved its loaded part of the world: the tile that was at
    // (x + dx, y + dy) is at (x, y) now. What is still on the map is kept where
    // it now is; the part newly loaded is left unread, and so is a band along
    // the old edge, read without its neighbours beyond it.
    void shift(int dx, int dy);

    // make room for the current map; returns false if no map is loaded
    bool resize();

    // Read the z-level the map viewport currently shows. With a rect
    // {x, y, width, height} the viewport is left alone and the shape of the
    // tiles in that part of the map that have not been read yet is taken
    // straight from the map (walls, floors, slopes, trees: no sprites needed).
    void readLevel(int z, const int *rect = nullptr);

    // turn the creatures on the captured levels into billboards. `frame` is a
    // running counter used to keep creatures visible for a moment after they
    // walk into a part of the map that has not been re-read yet.
    // With outlines, every creature gets a line around it that tells how an
    // attack on it would go: red for an enemy, white for someone the game
    // would ask you about first, green for a companion.
    // With smooth_units they glide from tile to tile instead of jumping.
    // With melee_reach, only creatures you could strike from where you stand are outlined.
    // any_conflict: conflicts between others count too (disc and colour); otherwise only those with you.
    void buildUnits(int zlo, int zhi, const df::unit *self, uint32_t frame, bool outlines, bool melee_reach,
                    bool smooth_units, bool any_conflict);

    // the land beyond the loaded map (null: none); replaced whole, never changed in place but by shift()
    std::shared_ptr<FarView> far;

    // the colours of a kind of tree (index into the plant raws), as the map draws it
    FarTreeLook farTreeLook(int plant);

    const Level *level(int z) const {
        return (z >= 0 && z < int(levels.size())) ? levels[z].get() : nullptr;
    }

    // how far the ground at the middle of this tile is above the level's
    // floor, in levels (0 on flat ground, up to 1 on a slope)
    float groundRaise(int x, int y, int z) const;

private:
    std::shared_ptr<const Sprite> staticSprite(int32_t texpos);
    std::unordered_map<int32_t, std::shared_ptr<const Sprite>> sprite_cache_;
    int sprite_cache_age_ = 0;
    std::unordered_map<uint64_t, std::shared_ptr<const Sprite>> pile_cache_;   // heaps of things, by what is in them
    std::unordered_map<uint64_t, std::shared_ptr<const Sprite>> tint_cache_;   // grey game pictures in a colour
    std::shared_ptr<const Sprite> tintedSprite(int32_t texpos, uint32_t color);
    // a plant's picture with snow on it, by picture, amount of snow (1..5) and
    // one of a few variants of the snow's edge
    std::unordered_map<uint64_t, std::shared_ptr<const Sprite>> snow_cache_;
    std::shared_ptr<const Sprite> snowySprite(int32_t texpos, int level, uint32_t variant);
    // the fronds of the palm crowns in and around a part of a level reach into
    // the open tiles next to them
    void spreadFronds(Level &L, int z, int x0, int y0, int x1, int y1);
    std::unordered_map<int32_t, uint32_t> unit_seen_;   // unit id -> frame it was last seen
    // What the game drew for a creature without a picture of its own (animals)
    // while it stood alone on its tile. On a tile shared by several the game
    // shows them and their status bubbles in turn, so what it draws there is
    // not a picture of any one of them.
    struct UnitLook {
        int32_t texpos = 0;
        std::shared_ptr<const Sprite> large;
        float anchor_px = 0;
        uint32_t frame = 0;
    };
    std::unordered_map<int32_t, UnitLook> unit_look_;
    // creatures lying on their side: by game picture, and for creatures drawn
    // across several tiles by their picture (kept while that picture lives)
    std::unordered_map<int32_t, std::shared_ptr<const Sprite>> prone_cache_;
    struct ProneLarge {
        std::weak_ptr<const Sprite> from;
        std::shared_ptr<const Sprite> turned;
    };
    std::unordered_map<const Sprite *, ProneLarge> prone_large_;
    std::shared_ptr<const Sprite> proneSprite(int32_t texpos, const std::shared_ptr<const Sprite> &upright);
    std::unordered_map<uint64_t, uint16_t> boxset_index_;
    struct BoxArgs {
        int shape;
        uint16_t side, picture;
        int facing;
        uint32_t color;
        bool wood;
    };
    std::vector<BoxArgs> boxset_args_;            // what each of boxsets was made from
    std::shared_ptr<const Models> models_;        // null until first needed: the built-in ones
    float edge_width_ = 1.0f / 32;                // one pixel of the game's 32-pixel tiles
    void remakeBoxSets();
    BoxSet makeBoxSet(const BoxArgs &a);
    Spatters spatters_;                                 // snow, blood, leaves ... on the ground
    std::unordered_map<uint32_t, uint16_t> leaf_tex_;   // kind of tree -> texture of its leaves (0: none known)
    uint16_t leaf_texture(uint32_t mat);
    // pictures from the game's tree graphics (roots, shadows and leaves the game
    // draws over the ground near a tree): never the ground itself
    std::unordered_set<int32_t> tree_sprites_;
    bool isTreeSprite(int32_t texpos);
    // the look of each kind of ground (tile type and material) as last seen in
    // plain view, for tiles of that kind that have not been seen themselves
    std::unordered_map<uint64_t, uint16_t> ground_kind_;
    // Natural ground (grass, soil, sand, stone ...) reaches into the floor beside it
    // along an uneven line instead of stopping at the tile's edge (ground_class in
    // scene.cpp). The look of each class of ground where it reaches into a tile,
    // and whether it was taken from the game's picture.
    std::unordered_map<uint32_t, std::pair<uint16_t, bool>> class_look_;
    // The colour each kind of ground has been seen in. A picture far from it is
    // only taken for a tile once the same picture is read there a second time:
    // a single read that went wrong cannot repaint the ground.
    struct GroundLook {
        uint32_t color = 0;
        uint32_t fits = 0, odd = 0;
    };
    std::unordered_map<uint64_t, GroundLook> ground_look_;
    // true if a picture of this colour may be the ground for a tile of this kind
    bool groundFits(uint64_t kind, uint32_t color, bool confirmed);
    // the painted front of each kind of cabinet or bookcase (shape and material),
    // once its picture has been seen
    std::unordered_map<uint64_t, uint16_t> front_kind_;
    // where each creature is drawn right now: it glides from tile to tile
    struct Glide {
        float x = 0, y = 0, base = 0;
        uint32_t frame = 0;
    };
    std::unordered_map<int32_t, Glide> glide_;
    double glide_time_ = 0;

    // colour of a kind of surface made of a material: as seen on the game's
    // own sprites, or failing that the material's colour
    std::unordered_map<uint64_t, uint32_t> learned_color_;
    struct MatInfo {
        uint32_t color = 0;   // 0 = unknown
        bool wood = false;
    };
    std::unordered_map<uint32_t, MatInfo> mat_info_;
    const MatInfo &matInfo(uint32_t mat);
    void learn(uint64_t key, uint32_t color);
    uint32_t colorFor(int use, int tile_material, uint32_t mat);
};

// the level of the conflict a creature is in with you (df::conflict_level), -1 if none
int conflictWithYou(const df::unit *u, const df::unit *self);

// the id of the creature this one rides, -1 if none
int32_t mountOf(const df::unit *u);

// The game's picture of an item, 32 x 32 into out[TEXPIX]: as it lies about,
// in the colour of its material where the game draws it grey (the inventory
// panel). False if no picture is known.
bool itemPicture(df::item *item, uint32_t *out);

// how high the water in a tile stands above the level's floor, in levels (the game's n/7)
inline float water_height(const Cell &c) {
    return !c.water ? 0.0f : (c.flags & CELL_UNDER) ? 1.0f : c.wet / 7.0f;
}

// the texture of a water surface in this frame of the waves' animation
inline uint16_t water_frame(const Cell &c, int frame) {
    return (c.flags & CELL_WAVES) ? uint16_t(c.water + (frame & (WAVE_FRAMES - 1))) : c.water;
}

// height of a slope at (u, v) inside its tile (u east, v south, both 0..1), in levels
inline float ramp_height(uint8_t ramp, float u, float v) {
    float dip = (ramp & RAMP_VALLEY_U) ? std::abs(2 * u - 1) : (ramp & RAMP_VALLEY_V) ? std::abs(2 * v - 1) : 1.0f;
    if ((ramp & RAMP_RIDGE) && (ramp & (RAMP_VALLEY_U | RAMP_VALLEY_V)))
        dip = 1 - dip;
    const float nw = (ramp & RAMP_NW) ? 1.0f : 0.0f, ne = (ramp & RAMP_NE) ? 1.0f : 0.0f;
    const float sw = (ramp & RAMP_SW) ? 1.0f : 0.0f, se = (ramp & RAMP_SE) ? 1.0f : 0.0f;
    return std::min(dip, (nw * (1 - u) + ne * u) * (1 - v) + (sw * (1 - u) + se * u) * v);
}

} // namespace fpv
