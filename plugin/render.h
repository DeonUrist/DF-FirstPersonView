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

struct RenderOptions {
    int zlo = 0, zhi = -1;       // z-levels to draw
    float view_distance = 24;    // tiles at which things have faded into haze
    float daylight = 1;          // 0 = night, 1 = day
    uint32_t fog = 0xFF0C0908;   // colour distant things fade into
    Pick highlight;              // tile or creature under the pointer, drawn brighter
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
        int16_t tx, ty, tz; // map tile it belongs to (-1 for sky)
    };
    static constexpr int MAX_SPANS = 64;

    struct Frame;
    void renderColumns(const Frame &f, int x0, int x1);
    void renderBillboards(const Frame &f, int x0, int x1);
    template <class MaskedFn>
    void draw_ring(const Frame &f, const Billboard &b, float bill_depth, int x0, int x1, MaskedFn &draw_masked_until);

    WorkerPool pool_;
    std::vector<Span> spans_;          // W * MAX_SPANS
    std::vector<uint8_t> span_count_;  // W
    std::vector<float> row_inv_;       // H
    std::vector<uint32_t> sky_row_;    // H

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
