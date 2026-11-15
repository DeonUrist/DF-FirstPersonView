// The land beyond what the game draws, out to the horizon.
//
// Adventure mode keeps only a block of the world around the adventurer; the
// rest exists as the world map: one entry per world tile (768 x 768 tiles:
// elevation, biome, vegetation, its region), and near the adventurer the
// mid-level map (one height and biome per 48 x 48 tiles) that the game builds
// its local maps from.
//
// The far land is made of blocks like the map: cells with flat tops at whole
// z-levels and sides down to their lower neighbours, so no gap opens between
// them. Inside the loaded map (beyond the draw distance) a cell takes the real
// ground's level there; outside, the mid-level map's height (or the world
// tile's, smoothed, with made-up relief), lined up with the loaded map by
// measuring its real ground. Cells are 4 tiles wide near the map, twice as wide
// in every ring outward.
//
// Trees are boxes: a trunk and the leafy part of each level, made from the
// game's own tree bodies (plant::tree_info): the real trees of the loaded map
// as they stand, and outside it trees of the kinds the region grows in that
// biome, shaped like real ones of their kind (or, with none loaded, after the
// kind's raws). Beyond the nearest few thousand trees a dense wood becomes a
// block of canopy as high as its trees.
//
// The work runs off the game's thread; the result replaces Scene::far whole.
#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "scene.h"

namespace fpv {

struct FarSettings {
    bool on = true;
    float horizon = 20000;      // tiles: how far the land reaches (and the haze over it)
    float trees = 1400;         // tiles: how far trees are drawn as trees (0: none)
    float view_distance = 48;   // tiles: the real map is drawn this far
};

class FarLand {
public:
    // Once per capture, on the game's thread, with scene.mutex not held. Takes
    // what is needed from the world when it changes, starts a new far view
    // when the adventurer has moved on or settings changed, and hands a
    // finished one to the scene.
    void update(Scene &scene, float eye_x, float eye_y, const FarSettings &s);
    // forget everything (no world, or the far land switched off)
    void clear(Scene &scene);
    std::string status() const;

    // what is kept of the world, shared with the worker that builds a view
    struct WorldTile {
        int16_t el = 100;
        int16_t temp = 0;           // its temperature, as the world map gives it
        uint8_t biome = 0;          // df::biome_type
        uint8_t veg = 0;            // 0..100
        int32_t region = -1;
        int16_t region_type = -1;   // df::world_region_type
        uint8_t align = 0;          // 0 neutral, 1 good, 2 evil
        bool lake = false, ocean = false;
    };
    struct Details {                // one world tile's mid-level map
        int16_t el[16][16];
        uint8_t biome[16][16];
        int32_t region[16][16];
        uint8_t align[16][16];
    };
    // a block of a tree: tiles from the corner of its trunk's tile, levels from its foot
    struct RelBox {
        int16_t x0, y0, x1, y1, z0, z1;
        bool leaf;
    };
    struct Shape {
        std::vector<RelBox> full;     // the trunk, and the leaves of each level in up to three bands
        std::vector<RelBox> simple;   // the trunk, and one block of leaves a level
        int height = 0;               // levels
        int reach = 0;                // tiles from the trunk
    };
    struct RealTree {
        int x, y, z;                  // its trunk's foot, in map tiles and levels
        int plant, shape;
    };
    // how much of the loaded map's ground lies under snow: on each tile
    // (0..255, full from a spatter of 100), and over all of it (0..1)
    struct Snow {
        float cover = 0;
        std::vector<uint8_t> tile;
    };
    struct World {
        int w = 0, h = 0;
        std::vector<WorldTile> tiles;
        std::unordered_map<int, Details> details;    // by y * w + x
        // the plant kinds of trees that grow in a biome of a region, by kindKey(),
        // the colour of their leaves seen from afar and how tall they stand (levels)
        std::unordered_map<uint32_t, std::vector<int>> kinds;
        std::unordered_map<uint32_t, uint32_t> canopy;
        std::unordered_map<uint32_t, float> canopy_h;
        std::unordered_map<int, FarTreeLook> looks;  // by plant
        std::vector<Shape> shapes;
        size_t made_up = 0;                          // shapes[0 .. made_up) are made from raws
        std::unordered_map<int, std::vector<int>> shapes_of;   // by plant: which shapes its trees take
        std::vector<RealTree> real;
        // the loaded map: the level of the ground (or the water's surface, the
        // level above water 4/7 deep or more) on each tile, its colours, and how
        // deep the water there is (the game's 1..7; 0: dry); -32768: none found
        std::vector<int16_t> surf;
        std::vector<uint32_t> surf_top, surf_side;
        std::vector<uint8_t> surf_water;
        float offset = 0;      // a local z-level is an elevation plus this
        // the elevation the sea's surface is at (its local level less the offset;
        // not a whole number where the sea is less than 7/7 deep)
        float sea = 100;
        // the sea and lakes from above, as the map shows them (see-through water over
        // what lies under it): the mean over the map's water, or a guess
        uint32_t sea_col = 0, lake_col = 0;
        int region_x = 0, region_y = 0;              // the game's window, in 48-tile squares
        int map_w = 0, map_h = 0;                    // its size, in tiles
        static uint32_t kindKey(int region, int biome, int align) {
            return (uint32_t(region + 1) << 10) | (uint32_t(biome & 0xFF) << 2) | uint32_t(align & 3);
        }
    };

private:
    std::shared_ptr<World> world_;
    std::shared_ptr<const Snow> snow_ = std::make_shared<Snow>();
    int64_t snow_tick_ = -1;   // the game's clock at the last read of the snow
    float built_snow_ = -1;
    const void *world_id_ = nullptr;
    int world_w_ = 0, world_h_ = 0;
    int region_x_ = -1, region_y_ = -1, region_z_ = -1, map_w_ = 0, map_h_ = 0;
    std::future<std::shared_ptr<FarView>> pending_;
    int built_sx_ = -1000000, built_sy_ = -1000000;   // the eye's 48-tile square when last started
    FarSettings built_;
    bool dirty_ = true;
    int calib_n_ = 0, sea_n_ = 0, read_ms_ = 0;
    int last_ms_ = 0, last_trees_ = 0, last_levels_ = 0, last_cells_ = 0;
    std::shared_ptr<std::atomic<int>> ms_out_ = std::make_shared<std::atomic<int>>(0);

    void snapshotWorld(Scene &scene);
    void readRegion();
    void readSnow();
};

} // namespace fpv
