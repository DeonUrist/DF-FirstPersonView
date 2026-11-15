// What the adventurer can see, as light on the world (setting "Visibility
// gradient"). The game marks every tile it can see from the adventurer in
// adventure mode (its line of sight, which already shrinks in the dark and is
// short indoors). Tiles it cannot see are black; seen tiles darken towards them
// over a few tiles, measured through open ground, so a room fades out where the
// view ends rather than at a wall; under a roof everything is a little darker.
#pragma once

#include <cstdint>
#include <vector>

#include "common.h"

namespace fpv {

constexpr float SIGHT_INDOORS = 0.8f;   // light on a seen tile not open to the sky
constexpr float SIGHT_FADE = 5.0f;      // tiles over which the seen fades into the unseen

struct SightMap {
    bool on = false;
    // the part of the map read: x0..x0+w, y0..y0+h, levels zlo..zlo+nz
    int x0 = 0, y0 = 0, w = 0, h = 0, zlo = 0, nz = 0;
    std::vector<uint8_t> light;   // w * h * nz: 0..255
    std::vector<uint8_t> has;     // 1: light says something there (not unseen rock or air)
    std::vector<uint8_t> col;     // w * h: the most light on any level of the column
    std::vector<uint8_t> col_has;

    // the light at a point (map tiles; z in tile widths), 0..1; 1 when off
    float at(float x, float y, float z) const;
    // the light of the column, for the land beyond the map: the edge of the part read
    float column(float x, float y) const;

private:
    float level(float x, float y, int z) const;   // -1: nothing known there
};

// Fill `out` from the game's map around (ex, ey), radius tiles each way, levels zlo..zhi.
// `day` (0 night .. 1 day): how much the sun lights tiles open to the sky, which
// then fade to the unseen only as much as it is dark.
void readSight(SightMap &out, int ex, int ey, int zlo, int zhi, int radius, float day);

} // namespace fpv
