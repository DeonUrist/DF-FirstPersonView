// Software renderer for a world made of stacked tiles.
//
// For every screen column a ray walks across the map, tile by tile, front to
// back. Each map column it crosses contributes vertical faces (where the ray
// enters a solid block) and horizontal surfaces (floors seen from above,
// ceilings seen from below) on any captured z-level. A short list of
// still-empty pixel ranges per screen column makes nearer things hide farther
// ones. Billboards are drawn last, clipped against the depth of every span
// that was drawn. Columns are independent, so they are spread over threads.
#pragma once

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "scene.h"

namespace fpv {

struct Camera {
    float x = 0, y = 0;     // map position, in tiles
    float eye = 0;          // eye height, in tile widths (z * LEVEL_H + EYE)
    float ang = 0;          // heading in radians, 0 = north, clockwise
    float fov = 80;         // horizontal field of view in degrees
    float pitch = 0;        // vertical shift of the horizon, in pixels
    float tilt = 0;         // real tilt up (+) or down (-), in radians: the graphics card renderer (gpu.h) only
};

// what is under a pixel, for mouse picking
struct Pick {
    int16_t x = -1, y = -1, z = -1;   // map tile
    int32_t unit_id = -1;
    int8_t kind = -1;                 // BillKind of the billboard under the pixel, -1 for terrain
    int32_t texpos = 0;               // game sprite of that billboard
    bool valid() const { return x >= 0; }
};

class Sky;
class Precip;
struct Weather;

// How much haze lies over something, 0..1. t is its distance as a fraction of
// the fog distance (what is drawn ends at 1), fog_distance that distance in
// tiles; the weather's haze comes over it.
inline float haze_at(float t, float fog_distance, float weather_haze, float weather_reach) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float a = 0.97f * t * std::sqrt(std::sqrt(t));
    if (weather_haze > 0) {
        const float w = weather_haze * (1 - std::exp(-t * fog_distance / (weather_reach > 1 ? weather_reach : 1)));
        a = 1 - (1 - a) * (1 - w);
    }
    return a;
}

struct RenderOptions {
    int zlo = 0, zhi = -1;       // z-levels to draw
    float view_distance = 24;    // tiles: the map is drawn this far
    // tiles at which things have faded into haze; 0: the view distance. With the
    // far land on it is the horizon's distance, and the map itself is barely hazed.
    float fog_distance = 0;
    bool far_land = false;       // draw Scene::far beyond the map
    float daylight = 1;          // 0 = night, 1 = day
    uint32_t fog = 0xFF0C0908;   // colour distant things fade into
    // Fog, rain, snow or an evil cloud: a haze of their own over everything, as
    // thick as weather_haze (0..1) and reaching that over some weather_reach
    // tiles. It never shortens what is drawn: the land stays, faintly.
    float weather_haze = 0;
    float weather_reach = 30;
    Pick highlight;              // tile or creature under the pointer, drawn brighter
    const Sky *sky = nullptr;    // the sky (sky.h); without it a plain blue
    // rain and snow: the graphics card renderer draws them over the world (the software one leaves them to the caller)
    const Precip *precip = nullptr;
    const Weather *weather = nullptr;
    int wave_frame = 0;          // the step of the waves' animation (Cell::water, CELL_WAVES)
    bool sight = false;          // darken by what the adventurer sees (Scene::sight, sight.h)
};

class WorkerPool {
public:
    explicit WorkerPool(int threads);
    ~WorkerPool();
    // runs job(worker_index) on every worker and on the calling thread, and waits
    void run(const std::function<void(int)> &job);
    int size() const { return int(threads_.size()) + 1; }

private:
    void loop(int index);
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    const std::function<void(int)> *job_ = nullptr;
    uint64_t generation_ = 0;
    int pending_ = 0;
    bool stop_ = false;
};

class Renderer {
public:
    Renderer();

    // Draws into buf, which holds W*H pixels stored column by column:
    // pixel (x, y) is buf[x * H + y]. The caller must hold scene.mutex.
    void render(const Scene &scene, const Camera &cam, const RenderOptions &opt, int W, int H,
                uint32_t *buf);

    // what the last rendered frame shows at pixel (x, y)
    Pick pick(int x, int y) const;

    // a pixel of the last rendered frame that shows a creature (the one
    // covering the most columns); false if no creature is in view
    bool findCreature(int &x, int &y) const;

    int threads() const { return pool_.size(); }

    // swap red and blue in every pixel (the texture wants ARGB, we draw ABGR)
    void swapRedBlue(uint32_t *buf, size_t count);

private:
    struct Span {
        int16_t a, b;       // rows [a, b)
        float depth;
        int16_t tx, ty, tz; // map tile it belongs to (-1 for sky, -2 for the far land)
        float da, db;       // distance at its first and last row (for the light: Scene::sight)
    };
    static constexpr int MAX_SPANS = 64;

    struct Frame;
    void renderColumns(const Frame &f, int x0, int x1);
    void renderBillboards(const Frame &f, int x0, int x1);
    template <class MaskedFn>
    void draw_ring(const Frame &f, const Billboard &b, float bill_depth, int x0, int x1, bool with_arrow,
                   MaskedFn &draw_masked_until);

    WorkerPool pool_;
    std::vector<Span> spans_;          // W * MAX_SPANS
    std::vector<uint8_t> span_count_;  // W
    std::vector<float> row_inv_;       // H
    std::vector<uint32_t> sky_row_;    // H
    std::vector<float> far_depth_;     // W * H, column by column: depth of the far land drawn there
    // Water is see-through: where a ray crosses its surface the rows stay open for
    // what lies under it, and the water's own colour (with its alpha) is kept here,
    // for the nearest water on each pixel, to be laid over the column once it is done.
    // Picking goes through it, to the bottom and what swims there.
    std::vector<uint32_t> veil_;       // W * H, column by column; 0 = no water
    std::vector<float> veil_d_;        // W * H: how far away that water is

    // picking: per pixel column, the billboard spans drawn on top
    struct BillHit {
        int16_t a, b;
        int32_t unit_id;
        int16_t tx, ty, tz;
        int8_t kind;
        int32_t texpos;
    };

    // a see-through face (door frame) crossed by a column's ray; drawn with
    // the billboards so that things behind it show through the opening
    struct Masked {
        float depth, top, bot;
        uint16_t tex;
        uint8_t texx;
        int16_t tx, ty, tz;
        int16_t shade;
        int16_t keep;       // light on it, 0..256 (Scene::sight)
    };
    static constexpr int MAX_MASKED = 6;
    std::vector<Masked> masked_;        // W * MAX_MASKED, near to far
    std::vector<uint8_t> masked_count_; // W
    static constexpr int MAX_HITS = 24;
    std::vector<BillHit> hits_;        // W * MAX_HITS
    std::vector<uint8_t> hit_count_;   // W
    int last_w_ = 0, last_h_ = 0;
};

} // namespace fpv
