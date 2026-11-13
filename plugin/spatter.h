// Snow, blood, mud, fallen leaves and the like lying on the ground: the game's
// "spatter" pictures.
//
// In view, the game draws one spatter picture per tile in the viewport's
// spatter layer and that is used as it is. Out of view the same choice is made
// here from the map: the spatter with the largest amount on the tile, a
// smaller or larger picture by that amount, and for a tile fully covered, the
// picture that joins up with fully covered neighbours of the same kind (snow
// with snow, blood with blood), as the game does on its top-down map.
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

    // the game drew picture tp as the spatter on a tile in view: remember which
    // kind of picture the material lying there is drawn with
    void learn(int x, int y, int z, int32_t tp);

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
