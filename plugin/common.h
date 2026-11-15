// Shared constants and small helpers for the fpview plugin.
#pragma once

#include <cstdint>

namespace fpv {

constexpr int TEX = 32;              // DF map sprites are 32x32
constexpr int TEXPIX = TEX * TEX;

// A tile is roomy: several creatures can stand on one side by side.
constexpr float LEVEL_H = 1.2f;      // height of one z-level, in tile widths
constexpr float EYE = 0.52f;         // eye height above the floor, in tile widths
constexpr float EYE_PRONE = 0.14f;   // ... while lying on the ground
constexpr float EYE_RIDING = 0.37f;  // raised by this much while riding: a table top (media/models/table.obj)
constexpr float THING_SCALE = 0.7f;  // size of a 32 pixel sprite, in tile widths

// pixels are 0xAABBGGRR (SDL_PIXELFORMAT_RGBA32 on little-endian), the layout
// DF's own sprite surfaces use
inline uint32_t rgb(int r, int g, int b) {
    return 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}
inline int px_r(uint32_t c) { return c & 255; }
inline int px_g(uint32_t c) { return (c >> 8) & 255; }
inline int px_b(uint32_t c) { return (c >> 16) & 255; }
inline int px_a(uint32_t c) { return c >> 24; }

} // namespace fpv
