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
#include "spatter.h"
#include "textures.h"

namespace df {
struct unit;
}

namespace fpv {

enum : uint8_t {
    CELL_SKY = 1,       // open to the sky
    CELL_ROOFED = 2,    // something solid directly overhead
};

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

    bool operator==(const Cell &) const = default;
};

// sides of a block, for Box::face_sides
enum : uint8_t { FACE_N = 1, FACE_S = 2, FACE_W = 4, FACE_E = 8 };

// A rectangular block inside a tile: furniture and battlements are made of a
// few of these.
struct Box {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;   // its footprint inside the tile, 0..1 (u east, v south)
    float z0 = 0, z1 = 1;                   // bottom and top above the level's floor, in tile widths
    uint16_t side = 0, top = 0;             // textures
    uint16_t face = 0;                      // a picture on some of its sides (face_sides), stretched over them
    uint8_t face_sides = 0;                 // FACE_* bits
};

struct BoxSet {
    int n = 0;
    Box b[16];
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
    uint32_t ring = 0;        // a creature's colour for how it stands to you (enemy, neutral, companion)
    int8_t move_dx = 0, move_dy = 0;   // the tile a creature steps to next, relative to its own
};

struct CreatureTile {
    int32_t texpos = 0;                        // sprite the game drew on the tile
    std::shared_ptr<const Sprite> large;       // set for creatures drawn across several tiles
    float anchor_px = 16;
};

struct Level {
    std::vector<Cell> cells;                              // map_w * map_h
    std::vector<uint32_t> mats;                           // what each tile is made of (see Scene::matOf)
    std::vector<uint8_t> known;                           // tiles that have been read at least once
    std::vector<int32_t> ground_seen;                     // the game's ground picture last read on each tile
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
    TextureStore tex;
    std::vector<BoxSet> boxsets;                  // shapes that tiles refer to; [0] is unused
    // the shape of one kind of thing in the given textures (made once, then shared)
    // (picture: the game's drawing of the thing, for shapes that carry it on a side;
    // facing: for a cabinet, 0..3 = back to the north/south/west/east wall, 4 = free standing)
    // color/wood: the colour and kind of material of side, for shapes with parts in
    // shades of it (cabinet doors)
    uint16_t boxSet(int shape, uint16_t side, uint16_t picture, int facing = 0, uint32_t color = 0,
                    bool wood = false);
    uint64_t version = 0;                         // goes up whenever the scene changes
    // lowest and highest level with anything to draw, per map tile: the
    // renderer skips the empty levels above and below
    std::vector<int16_t> col_lo, col_hi;
    std::vector<uint8_t> col_box;                 // per map tile: some level has shapes standing in it

    // forget everything (new map, or texture numbers were reassigned)
    void reset();

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
    void buildUnits(int zlo, int zhi, const df::unit *self, uint32_t frame, bool outlines, bool melee_reach,
                    bool smooth_units);

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
    // the fronds of the palm crowns in and around a part of a level reach into
    // the open tiles next to them
    void spreadFronds(Level &L, int z, int x0, int y0, int x1, int y1);
    std::unordered_map<int32_t, uint32_t> unit_seen_;   // unit id -> frame it was last seen
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

// height of a slope at (u, v) inside its tile (u east, v south, both 0..1), in levels
inline float ramp_height(uint8_t ramp, float u, float v) {
    const float dip = (ramp & RAMP_VALLEY_U) ? std::abs(2 * u - 1) : (ramp & RAMP_VALLEY_V) ? std::abs(2 * v - 1) : 1.0f;
    const float nw = (ramp & RAMP_NW) ? 1.0f : 0.0f, ne = (ramp & RAMP_NE) ? 1.0f : 0.0f;
    const float sw = (ramp & RAMP_SW) ? 1.0f : 0.0f, se = (ramp & RAMP_SE) ? 1.0f : 0.0f;
    return std::min(dip, (nw * (1 - u) + ne * u) * (1 - v) + (sw * (1 - u) + se * u) * v);
}

} // namespace fpv
