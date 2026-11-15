// The shapes of furniture, stairs and battlements: read from Wavefront OBJ
// files in media/models (made in Blender), so the player can change them. See
// media/models/README.txt for how a model is laid out.
//
// The renderers draw only upright blocks inside a tile, so every object of a
// model becomes one: an axis-aligned box, or an upright prism on the convex
// outline of the object seen from above (a stair's step, a round column).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "textures.h"

namespace fpv {

// shapes made of boxes; the frond shapes are worked out in code, all others come from a file
enum Shape {
    SH_TABLE, SH_STOOL, SH_BED, SH_CHEST, SH_CABINET, SH_COFFIN, SH_STATUE, SH_STAND, SH_RACK, SH_SLAB,
    SH_HATCH, SH_WELL, SH_PEDESTAL, SH_PILLAR, SH_FORT, SH_BOOKCASE, SH_FROND_HUB, SH_FROND_ARM,
    SH_SIGN, SH_ALTAR, SH_STAIRS,
    SH_STAIRS_BOTTOM,   // only a model: the lowest level of a stair (no stair below it); else SH_STAIRS
    SH_COUNT
};

// the file of each shape in media/models (without ".obj"); null: not from a file
extern const char *const MODEL_FILES[SH_COUNT];

constexpr int MODEL_CORNERS = 12;   // corners of a prism's outline, at most (ROUND_MAX)

// what a side, top or underside of a part is covered with (a material of the .mtl file)
struct ModelMat {
    enum Kind : uint8_t {
        None,      // not given (that side of the object has no faces)
        Own,       // the thing's own material, as the game makes it ("own")
        Picture,   // the game's picture of the thing ("picture")
        Paint,     // pattern in colour
        Image,     // the player's picture (map_Kd), in colour, or in the thing's colour when own_tint
    } kind = None;
    Pattern pattern = Pattern::Grit;
    uint32_t color = 0;     // Kd, 0 = white
    int image = -1;         // index into Models::images
    bool own_tint = false;  // an "own..." material with a picture: tinted in the thing's colour
};

// lines that only count in some cases (when=...)
enum : uint8_t { WHEN_ALWAYS, WHEN_SHUT, WHEN_OPEN, WHEN_WALL, WHEN_FREE };

struct ModelPart {
    // in tile coordinates (u east, v south, 0..1 is the tile) and tile widths up from the floor
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1, z0 = 0, z1 = 1;
    int corners = 0;                                  // 0: a box; else a prism on this outline
    float cu[MODEL_CORNERS] = {}, cv[MODEL_CORNERS] = {};
    ModelMat side, top, under;
    uint8_t face_sides = 0;   // FACE_* bits: sides covered with the game's picture
    uint8_t when = WHEN_ALWAYS;
    bool hang = false;        // its bottom reaches down by the depth of the well (a rope)
    bool fit = false;         // shrunk to the proportions of the game's picture (a sign's board)
};

struct Model {
    std::vector<ModelPart> parts;
    struct Value {
        float v[3] = {};
        uint8_t when = WHEN_ALWAYS;
    };
    std::vector<Value> surface;   // height things on it lie at
    std::vector<Value> stand;     // where a creature on it stands: u v height
};

struct ModelImage {
    uint32_t serial = 0;                 // unique to this picture as loaded this time
    std::array<uint32_t, TEXPIX> px{};   // 32 x 32, opaque
};

struct Models {
    std::string dir;                    // empty: only the built-in ones
    Model shape[SH_COUNT];
    std::vector<ModelImage> images;
    std::vector<std::string> problems;  // what could not be read, for the console
    int files = 0;                      // how many models were read from files
};

// Reads dir/models/*.obj (with their .mtl files and pictures). A shape whose
// file is missing, or has nothing that could be read, keeps its built-in model.
// Never fails.
std::shared_ptr<const Models> load_models(const std::string &dir);

// something that changes when any file in dir/models changes (cheap: file dates)
uint64_t models_stamp(const std::string &dir);

} // namespace fpv
