// Snow, blood, mud, fallen leaves and the like lying on the ground: the game's
// "spatter" pictures.
//
// The game draws only one spatter picture per tile (the kind with the largest
// amount). Here every kind on a tile is a layer of its own, in a fixed order
// from the ground up: mud and dust, then snow, then blood and everything else.
// Each layer is a smaller or larger picture by its amount, and for a tile fully
// covered, the picture that joins up with neighbours fully covered by the same
// kind (snow with snow, blood with blood), as the game does on its top-down
// map. In view the game's own picture stands in for the layer of its kind.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fpv {

class Spatters {
public:
    // forget everything (texture numbers change when a world loads)
    void reset();

    // the game's picture for the spatter on a map tile, worked out from the
    // map; 0 if nothing lies there
    int32_t fromMap(int x, int y, int z);

    // one picture of what lies on a tile; holes: see TextureStore::overHoled
    struct Layer {
        int32_t tp = 0;
        uint32_t holes = 0;
    };
    // every spatter on a map tile as pictures, from the ground up; seen = the
    // picture the game drew there (0 if none or out of view), used for the
    // layer of its kind. slope: the tile is a ramp (what lies on it may be kept
    // on the open tile above, and it joins up with the tops of walls beside
    // it). Snow over mud or dust gets holes where they show through. Returns
    // how many were written to out (at most max).
    int layers(int x, int y, int z, int32_t seen, bool slope, Layer *out, int max);

    // the game drew picture tp as the spatter on a tile in view: remember which
    // kind of picture the material lying there is drawn with
    void learn(int x, int y, int z, int32_t tp);

    // Snow lying on a map tile, for a block that shows it on more than its top
    // (a tree's leaves): top = the picture the game would draw on the tile from
    // above, side = a partly covered picture for its sides. False if the
    // tile has no snow on it.
    bool snow(int x, int y, int z, int32_t &top, int32_t &side);
    // how much snow lies on a map tile: 0 none, 1..4 partly covered, 5 fully
    int snowLevel(int x, int y, int z);

    enum Kind : int8_t {
        NONE = -1, SNOW, BLOOD_RED, BLOOD_CYAN, BLOOD_MAGENTA, BLOOD_GOO, BLOOD_ICHOR, DUST, MUD, VOMIT,
        WATER, MAGMA, SLIME, LEAVES, FRUIT, FRUIT_SMALL, FRUIT_LARGE, COUNT
    };

private:
    struct Top {
        uint64_t key = 0;   // what lies there (material and state, or kind of item)
        int amount = 0;
        int level = 0;      // 0 nothing, 1..4 partly covered, 5 fully covered
        Kind kind = NONE;
    };
    Top top(int x, int y, int z);
    // what lies on a tile, one entry per kind with its largest amount
    int kinds(int x, int y, int z, Top *out, int max);
    // how much of one kind lies on a tile (level 0..5)
    int levelOf(int x, int y, int z, Kind kind);
    // the picture of a kind at a level on a tile, joined up with its neighbours
    // (slope: the tile is a ramp)
    int32_t pictureAt(int x, int y, int z, Kind kind, int level, bool slope = false);
    // how much of a kind lies at a neighbour's surface where it meets a tile:
    // a slope and the open tile over it are one surface, and beside a slope
    // the top of a wall is too
    int levelBeside(int x, int y, int z, Kind kind, bool from_slope);
    // the holes of snow on a tile over the mud and dust around it, or 0
    uint32_t holesAt(int x, int y, int z);
    Kind kindOf(uint64_t key, int16_t mat_type, int32_t mat_index, int state, int item_type, int item_subtype);
    int32_t picture(Kind kind, int col, int row);
    void loadPages();

    bool loaded_ = false;
    std::vector<int32_t> pages_[COUNT];   // texpos by tile of each kind's graphics page
    int page_w_[COUNT] = {};
    std::unordered_map<int32_t, Kind> kind_of_picture_;
    std::unordered_map<uint64_t, Kind> kind_of_key_;   // learned from the screen or guessed
};

} // namespace fpv
