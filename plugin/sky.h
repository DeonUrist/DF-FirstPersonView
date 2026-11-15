// The sky over the first-person view and the weather in it.
//
// What the sky shows: a colour by time of day, stars turning with the night,
// the moon in its phase where the month puts it, the sun where the hour puts
// it, then the kinds of cloud the game knows as layers high above (each a
// picture repeated over a flat ceiling, so they shrink towards the horizon),
// and fog over all of it.
//
// The software renderer asks for the colour of each empty pixel (Sky::at).
// The graphics card renderer is given the same sky as triangles instead
// (Sky::mesh): a dome coloured at its corners, the sun, moon and stars as
// small pictures, the clouds as tiles of their pictures (farther out, larger
// tiles of smaller copies), all from one texture (SkyAtlas). Working out every
// pixel on the processor cost it 10 to 35 ms a frame; the triangles cost a
// fraction of a millisecond.
//
// Rain and snow are a few hundred drops in a box around the eye. A drop is
// only drawn where the game says the tile is open to the sky and nothing
// solid stands between it and the eye, so it does not rain indoors or through
// walls. The software renderer paints them into its frame, the graphics card
// renderer gets them as triangles too.
//
// Every picture comes from media/sky; a missing one is simply not drawn.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "media.h"
#include "render3d.h"

namespace fpv {

// What the sky shows: the game's weather where you are, or one picked to test
struct Weather {
    float hour = 12;                  // time of day, 0..24
    int day = 0;                      // day of the year, 0..335 (the sun stands higher in summer)
    float moon = 0.5f;                // where the moon is in its month: 0 new, 0.5 full
    bool rise_west = false;           // the world's sun rises in the west
    bool sun_north = false;           // the sun passes north of you at noon
    float cloud[CLOUD_COUNT] = {};    // how much of each kind of cloud, 0..1
    float fog = 0;                    // 0 none, 1/3 mist, 2/3 fog, 1 thick fog
    float rain = 0, snow = 0;         // how hard, 0..1
    uint32_t rain_color = 0;          // what falls is not water (blood rain): its colour; 0 = water
    uint32_t evil_color = 0;          // an evil cloud creeping nearby: its colour
    float evil = 0;                   // ... and how near it is, 0..1
    float wind_x = 1, wind_y = 0;     // where the clouds drift (map x east, y south), length 1
};

// a corner of a triangle for the graphics card renderer, laid out like SDL_Vertex:
// position in window pixels, colour, place in the SkyAtlas (0..1)
struct SkyVertex {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
};

struct SkyMesh {
    std::vector<SkyVertex> v;
    std::vector<int> i;
    void clear() {
        v.clear();
        i.clear();
    }
};

// every sky picture in one texture, for the graphics card renderer
struct SkyAtlas {
    struct Rect {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // 0..1 of the texture
        bool ok = false;
    };
    static constexpr int LEVELS = 4;   // a cloud picture as it is, and as 2x2, 4x4, 8x8 of its smaller copies

    int size = 0;                      // square, a power of two
    std::vector<uint32_t> px;          // 0xAABBGGRR, row by row
    uint32_t serial = 0;               // changes whenever it is made again
    Rect white, sun, moon[MOON_PHASES], rain;
    std::vector<Rect> stars, snow;     // one per picture in the row
    Rect cloud[CLOUD_COUNT][LEVELS];
};

class Sky {
public:
    // Before each frame. pixel_angle: the angle one pixel of the picture
    // covers in its middle (far clouds use smaller copies of their picture).
    void prepare(const Weather &w, const std::shared_ptr<const Media> &media, double seconds, float pixel_angle);

    // the colour (0xAABBGGRR, opaque) seen in a direction: x east, y south, z up; any length
    uint32_t at(float x, float y, float z) const;
    // the same for n pixels of one column of the software renderer, which
    // looks along (dirx, diry) with (rgtx, rgty) to its right (both level and
    // of length 1): pixel i looks along dir + rgt * a, up by t0 + i * step.
    // The sun and the moon come out round on its screen.
    void column(float dirx, float diry, float rgtx, float rgty, float a, float t0, float step, int n,
                uint32_t *out) const;

    // The sky as triangles for a W x H window seen through v. open: for each
    // of n_open slices of the compass (starting north, going clockwise),
    // whether the sky can be seen that way; null for all of them.
    void mesh(const View3D &v, float W, float H, const bool *open, int n_open, SkyMesh &out) const;
    const SkyAtlas &atlas() const { return atlas_; }
    const Media *media() const { return media_.get(); }

    uint32_t haze() const { return haze_; }        // what distant things fade into under the open sky
    float sightLimit() const { return sight_; }    // how far fog, rain or snow let one see clearly, in tiles
    float weatherHaze() const { return weather_haze_; }   // how thick their haze gets far off, 0..1
    bool ready() const { return media_ != nullptr; }

private:
    // a picture placed on the sky: the sun or the moon
    struct Body {
        float x = 0, y = 0, z = 1;       // its direction
        float rx = 1, ry = 0, rz = 0;    // right and up on its picture
        float ux = 0, uy = 0, uz = 1;
        float cosr = 1;                  // nothing of it lies farther from its middle than this
        const Image *img = nullptr;
        int phase = 0;                   // the moon's picture
        float alpha = 1;
    };
    // A body as the software renderer's screen shows it. That screen stands
    // upright whatever the pitch, so a picture lying on the sky is stretched
    // up and down the higher it stands; this keeps it round instead. a, t:
    // its middle on the screen (across, up, as in column()); r and u: its
    // right and up there, length 1; inv: radians of the picture per screen unit.
    struct Flat {
        bool on = false;
        float a = 0, t = 0;
        float ra = 1, rt = 0, ua = 0, ut = 1;
        float inv = 1;
    };
    static Flat flatten(const Body &b, float dirx, float diry, float rgtx, float rgty);
    // a picture and smaller copies of it, for far away
    struct Mip {
        int w = 0, h = 0;
        int wmask = -1, hmask = -1;      // w - 1 and h - 1 when they are powers of two
        std::vector<uint32_t> px;
    };
    struct Layer {
        int kind = 0;
        const std::vector<Mip> *mips = nullptr;
        float amount = 0;
        float scale = 1;                 // pixels of the picture per unit of the ceiling it lies on
        float off_x = 0, off_y = 0;      // how far the wind has moved it, in pixels
        float fade_lo = 0, fade_hi = 0;  // it thins out below this height over the horizon (sine of the angle)
        // by height over the horizon (z * Z_STEPS): how much shows, and which smaller copy is used
        static constexpr int Z_STEPS = 256;
        float alpha[Z_STEPS + 1];
        uint8_t level[Z_STEPS + 1];
    };
    // a star on its sphere
    struct Star {
        float x, y, z;                   // its direction among the stars
        uint16_t frame;
        float rate, phase;               // twinkling
    };

    void base(float x, float y, float z, float f, float &r, float &g, float &b) const;
    // a direction of length 1; fs, fm (may be null): the sun and moon flat on
    // the software renderer's screen, where this pixel is (pa, pt)
    uint32_t shade(float x, float y, float z, bool sun, bool moon, const Flat *fs = nullptr,
                   const Flat *fm = nullptr, float pa = 0, float pt = 0) const;
    void blendBody(const Body &b, float x, float y, float z, float &r, float &g, float &bl) const;
    void blendFlat(const Body &b, const Flat &f, float pa, float pt, float &r, float &g, float &bl) const;
    // u, v: radians right and up of the picture's middle
    static void blendAt(const Body &b, float u, float v, float &r, float &g, float &bl);
    void buildMips(const Media &m);
    void buildStars(const Media &m);
    void buildAtlas(const Media &m);
    float cloudAlpha(const Layer &L, float z) const;

    std::shared_ptr<const Media> media_;
    const Media *built_for_ = nullptr;
    std::vector<Mip> mips_[CLOUD_COUNT];
    std::vector<Star> stars_list_;
    SkyAtlas atlas_;

    Weather w_;
    double seconds_ = 0;
    float pixel_angle_ = 0.002f;
    Body sun_, moon_;
    float sunx_ = 0, suny_ = 0, sunz_ = 1;   // the sun's direction, even below the horizon
    float sunh_x_ = 1, sunh_y_ = 0;          // ... and towards it along the ground
    float glow_ = 0;                         // how strongly the sun glows
    float sun_reach_ = 0, moon_reach_ = 0;   // sine of the angle out to which they (and the glow) show
    float light_ = 1;                        // 0 night .. 1 day
    float stars_ = 0;                        // how well stars show
    float twilight_ = 0;
    float overcast_ = 0;
    float zen_[3] = {}, hor_[3] = {};        // the colours of the clear part of the sky
    float warm_[3] = {255, 140, 70};
    float cloud_light_[3] = {1, 1, 1};
    float star_rot_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   // turns a direction into the stars' sphere
    Layer layers_[CLOUD_COUNT];
    int nlayers_ = 0;
    float fog_veil_ = 0;
    float weather_haze_ = 0;
    float haze_rgb_[3] = {};
    uint32_t haze_ = 0xFF000000u;
    float sight_ = 1e9f;
};

// Falling rain and snow
class Precip {
public:
    // moves the drops on by dt seconds and keeps as many as the weather asks for
    // in a box around the eye (x, y in tiles, eye height in tile widths)
    void step(float dt, float x, float y, float eye, const Weather &w);
    // draws them into buf (W x H pixels, stored column by column, 0xAABBGGRR).
    // The caller holds scene.mutex. box (x0, y0, x1, y1) grows to hold what was painted.
    void draw(uint32_t *buf, int W, int H, const View3D &v, const Scene &scene, const Media &m, const Weather &w,
              int box[4]) const;
    // the same as triangles over a W x H window, with the pictures in the atlas
    void mesh(const View3D &v, float W, float H, const Scene &scene, const SkyAtlas &atlas, const Media &m,
              const Weather &w, SkyMesh &out) const;
    bool empty() const { return drops_.empty(); }

private:
    struct Drop {
        float x, y, z;
        uint32_t seed;
        bool snow;
    };
    // where a drop shows, if it does: the streak's two ends (rain) or the middle (snow), and its size
    struct Shown {
        float sx, sy, sx0, sy0, depth, fade;
    };
    bool shown(const Drop &d, const View3D &v, float W, float H, const Scene &scene, Shown &s) const;

    std::vector<Drop> drops_;
    uint32_t rng_ = 0x9E3779B9u;
    double t_ = 0;
    float wind_x_ = 0, wind_y_ = 0;
    uint32_t next();
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
};

} // namespace fpv
