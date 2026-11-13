// A camera that really tilts, for the renderer that draws through the
// graphics card (gpu.h): where a point of the world lands on the screen, and
// what a ray through a pixel of the screen hits (for the mouse).
#pragma once

#include <cstdint>

#include "render.h"

namespace fpv {

// where the camera looks, for drawing and for putting things on the screen
struct View3D {
    float px = 0, py = 0, eye = 0;     // the eye
    float fx = 0, fy = 0, fz = 0;      // forward
    float rx = 0, ry = 0;              // right (always level)
    float ux = 0, uy = 0, uz = 0;      // up on the screen
    float S = 1;                       // pixels per unit at distance 1
    float cx = 0, cy = 0;              // the screen's middle (cy moved by the software renderer's pitch)

    // cam.tilt is the camera's real tilt in radians (0 for the software
    // renderer, which moves cy by cam.pitch instead)
    static View3D of(const Camera &cam, int W, int H);

    // a point of the world (map tiles, height in tile widths) on the screen;
    // false if it is behind the eye. depth: distance along the view
    bool project(float x, float y, float z, float &sx, float &sy, float &depth) const;
};

// What a ray through the point (sx, sy) of a W x H picture hits first: a tile
// of the map, or something standing on one. The caller holds scene.mutex.
Pick pick_ray(const Scene &scene, const Camera &cam, const RenderOptions &opt, int W, int H, float sx, float sy);

} // namespace fpv
