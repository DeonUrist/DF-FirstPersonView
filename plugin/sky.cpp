#include "sky.h"

#include <algorithm>
#include <cmath>

namespace fpv {

namespace {

constexpr float PI = 3.14159265f;
constexpr float ART = 0.0065f;       // the angle one pixel of the sun and moon pictures covers
constexpr float STAR_ART = 0.0035f;  // ... and of the star pictures
constexpr int STAR_GRID = 40;        // star places along each side of a face of the cube the stars lie on
constexpr uint32_t STAR_CHANCE = 330;   // in 1024: how many of those places hold a star
constexpr int MAX_MIPS = SkyAtlas::LEVELS;
constexpr float CLOUD_FAR = 80;      // clouds end this far out on their ceiling (its height is 1): 0.7 degrees up

// the box of falling drops around the eye, in tile widths
constexpr float BOX_R = 7.0f, BOX_LO = -4.0f, BOX_HI = 5.0f;
constexpr int MAX_RAIN = 700, MAX_SNOW = 450;
constexpr float RAIN_SPEED = 10.0f, SNOW_SPEED = 0.9f;   // tile widths a second: snow falls far slower
constexpr float RAIN_WIND = 1.2f, SNOW_WIND = 0.35f;
constexpr float STREAK = 0.022f;     // a raindrop is drawn as the way it fell in this many seconds

inline float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
inline float mixf(float a, float b, float t) {
    return a + (b - a) * t;
}
inline float smooth(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
// std::floor is a call; this is not
inline int ifloor(float v) {
    const int i = int(v);
    return i - (v < float(i) ? 1 : 0);
}
inline int wrap(int i, int n, int mask) {
    if (mask >= 0)
        return i & mask;
    i %= n;
    return i < 0 ? i + n : i;
}

inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x8DA6B343u ^ b * 0xD8163841u ^ c * 0xCB1AB31Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

inline uint8_t byte(float v) {
    return uint8_t(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}
inline uint32_t pack(float r, float g, float b) {
    return 0xFF000000u | (uint32_t(byte(b)) << 16) | (uint32_t(byte(g)) << 8) | uint32_t(byte(r));
}

// The direction of the sun or the moon on its way over the sky: it comes up
// on one side (east, or west in a world whose sun rises there), stands
// highest at el_max over the south (the north south of the equator) and goes
// down on the other side. a: 0 rising, pi/2 highest, pi setting; below the
// horizon outside that.
void body_dir(float a, float el_max, const Weather &w, float &x, float &y, float &z) {
    const float ex = w.rise_west ? -1.0f : 1.0f;
    const float sy = w.sun_north ? -1.0f : 1.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    x = ca * ex;
    y = sa * std::cos(el_max) * sy;
    z = sa * std::sin(el_max);
}

// mix a colour into a pixel that may itself be see-through (the frame lying
// over a world drawn by the graphics card)
inline void blend_px(uint32_t &dst, float r, float g, float b, float a) {
    if (a <= 0)
        return;
    a = std::min(a, 1.0f);
    const float da = px_a(dst) / 255.0f;
    const float oa = a + da * (1 - a);
    const float ka = a / oa, kd = da * (1 - a) / oa;
    dst = (pack(r * ka + px_r(dst) * kd, g * ka + px_g(dst) * kd, b * ka + px_b(dst) * kd) & 0x00FFFFFFu) |
          (uint32_t(oa * 255.0f + 0.5f) << 24);
}

const Cell *cell_at(const Scene &s, int tx, int ty, int tz) {
    if (tx < 0 || ty < 0 || tx >= s.w || ty >= s.h)
        return nullptr;
    const Level *L = s.level(tz);
    if (!L || L->cells.size() < size_t(s.w) * s.h)
        return nullptr;
    return &L->cells[size_t(ty) * s.w + tx];
}

// a drop can be at this point: the tile is under the open sky and not solid
bool open_air(const Scene &s, float x, float y, float z) {
    const Cell *c = cell_at(s, ifloor(x), ifloor(y), ifloor(z / LEVEL_H));
    return c && (c->flags & CELL_SKY) && !c->solid;
}

// nothing solid, and no floor or roof, lies between the eye and the point
bool clear_line(const Scene &s, float x0, float y0, float z0, float x1, float y1, float z1) {
    const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int n = std::max(1, int(dist / 0.4f) + 1);
    int pk = ifloor(z0 / LEVEL_H);
    for (int i = 1; i < n; ++i) {
        const float t = float(i) / n;
        const float x = x0 + dx * t, y = y0 + dy * t, z = z0 + dz * t;
        const int tx = ifloor(x), ty = ifloor(y), tz = ifloor(z / LEVEL_H);
        if (const Cell *c = cell_at(s, tx, ty, tz))
            if (c->solid)
                return false;
        if (tz != pk)
            if (const Cell *c = cell_at(s, tx, ty, std::max(tz, pk)))
                if (c->flr || c->solid)
                    return false;
        pk = tz;
    }
    return true;
}

// ---------------------------------------------------------------------------
// building triangles for the graphics card

// a corner in the camera's space (x right, y up, z forward)
struct CV {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

inline CV lerp(const CV &p, const CV &q, float k) {
    return CV{p.x + (q.x - p.x) * k, p.y + (q.y - p.y) * k, p.z + (q.z - p.z) * k, p.u + (q.u - p.u) * k,
              p.v + (q.v - p.v) * k, p.r + (q.r - p.r) * k, p.g + (q.g - p.g) * k, p.b + (q.b - p.b) * k,
              p.a + (q.a - p.a) * k};
}

struct Emit {
    const View3D &v;
    float W, H;
    SkyMesh &m;
    static constexpr float NEAR = 0.01f;

    // a direction or a point relative to the eye (x east, y south, z up) into the camera's space
    void cam(float x, float y, float z, CV &o) const {
        o.x = x * v.rx + y * v.ry;
        o.y = x * v.ux + y * v.uy + z * v.uz;
        o.z = x * v.fx + y * v.fy + z * v.fz;
    }

    // a convex polygon of up to 8 corners: cut at the near plane, put on the screen
    void poly(const CV *p, int n) {
        CV c[12];
        int k = 0;
        for (int i = 0; i < n && k < 10; ++i) {
            const CV &a = p[i], &b = p[(i + 1) % n];
            const bool ina = a.z >= NEAR, inb = b.z >= NEAR;
            if (ina)
                c[k++] = a;
            if (ina != inb) {
                const CV &s = ina ? a : b, &t = ina ? b : a;
                c[k++] = lerp(s, t, (NEAR - s.z) / (t.z - s.z));
            }
        }
        if (k < 3)
            return;
        SkyVertex out[12];
        bool left = true, right = true, above = true, below = true;
        for (int i = 0; i < k; ++i) {
            const float s = v.S / c[i].z;
            SkyVertex &o = out[i];
            o.x = v.cx + c[i].x * s;
            o.y = v.cy - c[i].y * s;
            o.r = byte(c[i].r);
            o.g = byte(c[i].g);
            o.b = byte(c[i].b);
            o.a = byte(c[i].a);
            o.u = c[i].u;
            o.v = c[i].v;
            left = left && o.x < 0;
            right = right && o.x > W;
            above = above && o.y < 0;
            below = below && o.y > H;
        }
        if (left || right || above || below)
            return;
        const int base = int(m.v.size());
        m.v.insert(m.v.end(), out, out + k);
        for (int i = 1; i + 1 < k; ++i) {
            m.i.push_back(base);
            m.i.push_back(base + i);
            m.i.push_back(base + i + 1);
        }
    }

    void quad(const CV &a, const CV &b, const CV &c, const CV &d) {
        const CV q[4] = {a, b, c, d};
        poly(q, 4);
    }
};

// which slice of the compass a direction falls in
inline int sector_of(float x, float y, int n) {
    float az = std::atan2(x, -y);
    if (az < 0)
        az += 2 * PI;
    return std::min(n - 1, int(az / (2 * PI) * n));
}

} // namespace

// ---------------------------------------------------------------------------
// the sky

void Sky::buildMips(const Media &m) {
    for (int k = 0; k < CLOUD_COUNT; ++k) {
        std::vector<Mip> &v = mips_[k];
        v.clear();
        const Image &img = m.clouds[k];
        if (!img.ok())
            continue;
        Mip m0;
        m0.w = img.w;
        m0.h = img.h;
        m0.px = img.px;
        v.push_back(std::move(m0));
        // each smaller copy averages 2 x 2 pixels of the one before (weighted by how opaque they are)
        while (int(v.size()) < MAX_MIPS && v.back().w >= 2 && v.back().h >= 2) {
            const Mip &a = v.back();
            Mip b;
            b.w = a.w / 2;
            b.h = a.h / 2;
            b.px.resize(size_t(b.w) * b.h);
            for (int y = 0; y < b.h; ++y)
                for (int x = 0; x < b.w; ++x) {
                    float r = 0, g = 0, bl = 0, al = 0;
                    for (int j = 0; j < 2; ++j)
                        for (int i = 0; i < 2; ++i) {
                            const uint32_t c = a.px[size_t(y * 2 + j) * a.w + x * 2 + i];
                            const float ca = px_a(c);
                            r += px_r(c) * ca;
                            g += px_g(c) * ca;
                            bl += px_b(c) * ca;
                            al += ca;
                        }
                    const uint32_t col = al > 0 ? pack(r / al, g / al, bl / al) : 0;
                    b.px[size_t(y) * b.w + x] = (col & 0x00FFFFFFu) | (uint32_t(al / 4 + 0.5f) << 24);
                }
            v.push_back(std::move(b));
        }
        for (Mip &mp : v) {
            mp.wmask = (mp.w & (mp.w - 1)) == 0 ? mp.w - 1 : -1;
            mp.hmask = (mp.h & (mp.h - 1)) == 0 ? mp.h - 1 : -1;
        }
    }
}

// the stars: one in some places of a grid over the faces of a cube
void Sky::buildStars(const Media &m) {
    stars_list_.clear();
    const Image &st = m.stars;
    if (!st.ok())
        return;
    const int fs = st.h;
    const int frames = std::max(1, st.w / std::max(1, fs));
    const float cell = 2.0f / STAR_GRID;
    for (uint32_t face = 0; face < 6; ++face)
        for (int ci = 0; ci < STAR_GRID; ++ci)
            for (int cj = 0; cj < STAR_GRID; ++cj) {
                const uint32_t h = hash3(face, uint32_t(ci), uint32_t(cj));
                if ((h & 1023) >= STAR_CHANCE)
                    continue;
                const float u = -1 + (ci + 0.25f + 0.5f * ((h >> 10) & 255) / 255.0f) * cell;
                const float v = -1 + (cj + 0.25f + 0.5f * ((h >> 18) & 255) / 255.0f) * cell;
                float x, y, z;
                const float sgn = (face & 1) ? -1.0f : 1.0f;
                if (face < 2) {
                    x = sgn;
                    y = u;
                    z = v;
                } else if (face < 4) {
                    x = u;
                    y = sgn;
                    z = v;
                } else {
                    x = u;
                    y = v;
                    z = sgn;
                }
                const float l = 1 / std::sqrt(x * x + y * y + z * z);
                const float pick = ((h >> 26) & 63) / 63.0f;
                Star s;
                s.x = x * l;
                s.y = y * l;
                s.z = z * l;
                s.frame = uint16_t(std::min(frames - 1, int(frames * pick * pick * pick)));
                s.rate = 1.3f + (h & 7) * 0.4f;
                s.phase = float((h >> 4) & 63);
                stars_list_.push_back(s);
            }
}

// Everything the graphics card renderer draws of the sky in one texture.
// Each picture has a pixel of room around it: see-through, or for the
// clouds, which repeat, the picture's other side.
void Sky::buildAtlas(const Media &m) {
    struct Item {
        int w = 0, h = 0;
        bool wrap = false;
        std::vector<uint32_t> px;
        SkyAtlas::Rect *out = nullptr;
    };
    SkyAtlas &A = atlas_;
    A.stars.assign(m.stars.ok() ? std::max(1, m.stars.w / std::max(1, m.stars.h)) : 0, SkyAtlas::Rect{});
    A.snow.assign(m.snow.ok() ? std::max(1, m.snow.w / std::max(1, m.snow.h)) : 0, SkyAtlas::Rect{});
    A.white = A.sun = A.rain = SkyAtlas::Rect{};
    for (auto &r : A.moon)
        r = SkyAtlas::Rect{};
    for (auto &k : A.cloud)
        for (auto &r : k)
            r = SkyAtlas::Rect{};

    std::vector<Item> items;
    const auto part = [&](const Image &img, int x0, int y0, int w, int h, SkyAtlas::Rect *out) {
        if (!img.ok() || w <= 0 || h <= 0 || x0 + w > img.w || y0 + h > img.h)
            return;
        Item it;
        it.w = w;
        it.h = h;
        it.out = out;
        it.px.resize(size_t(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                it.px[size_t(y) * w + x] = img.px[size_t(y0 + y) * img.w + x0 + x];
        items.push_back(std::move(it));
    };
    {
        Item it;
        it.w = it.h = 4;
        it.px.assign(16, 0xFFFFFFFFu);
        it.out = &A.white;
        items.push_back(std::move(it));
    }
    part(m.sun, 0, 0, m.sun.w, m.sun.h, &A.sun);
    for (int i = 0; i < MOON_PHASES; ++i)
        part(m.moon[i], 0, 0, m.moon[i].w, m.moon[i].h, &A.moon[i]);
    part(m.rain, 0, 0, m.rain.w, m.rain.h, &A.rain);
    for (size_t i = 0; i < A.stars.size(); ++i)
        part(m.stars, int(i) * m.stars.h, 0, m.stars.h, m.stars.h, &A.stars[i]);
    for (size_t i = 0; i < A.snow.size(); ++i)
        part(m.snow, int(i) * m.snow.h, 0, m.snow.h, m.snow.h, &A.snow[i]);
    // a cloud at each distance: level L holds 2^L x 2^L of its picture's L-th smaller copy
    for (int k = 0; k < CLOUD_COUNT; ++k)
        for (int L = 0; L < int(mips_[k].size()); ++L) {
            const Mip &mp = mips_[k][L];
            const int n = 1 << L;
            Item it;
            it.w = mp.w * n;
            it.h = mp.h * n;
            it.wrap = true;
            it.out = &A.cloud[k][L];
            it.px.resize(size_t(it.w) * it.h);
            for (int y = 0; y < it.h; ++y)
                for (int x = 0; x < it.w; ++x)
                    it.px[size_t(y) * it.w + x] = mp.px[size_t(y % mp.h) * mp.w + x % mp.w];
            items.push_back(std::move(it));
        }

    // shelves, tallest first, in the smallest square that holds everything
    std::vector<int> order(items.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = int(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return items[a].h > items[b].h; });
    std::vector<int> px(items.size()), py(items.size());
    int size = 256;
    for (;; size *= 2) {
        int x = 0, y = 0, shelf = 0;
        bool fits = true;
        for (const int i : order) {
            const int w = items[i].w + 2, h = items[i].h + 2;
            if (w > size) {
                fits = false;
                px[i] = -1;
                continue;
            }
            if (x + w > size) {
                x = 0;
                y += shelf;
                shelf = 0;
            }
            if (y + h > size) {
                fits = false;
                px[i] = -1;
                continue;
            }
            px[i] = x;
            py[i] = y;
            x += w;
            shelf = std::max(shelf, h);
        }
        if (fits || size >= 4096)
            break;
    }
    A.size = size;
    A.px.assign(size_t(size) * size, 0);
    const float inv = 1.0f / size;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item &it = items[i];
        if (px[i] < 0)
            continue;
        for (int y = -1; y <= it.h; ++y)
            for (int x = -1; x <= it.w; ++x) {
                uint32_t c = 0;
                if (it.wrap)
                    c = it.px[size_t((y + it.h) % it.h) * it.w + (x + it.w) % it.w];
                else if (x >= 0 && y >= 0 && x < it.w && y < it.h)
                    c = it.px[size_t(y) * it.w + x];
                A.px[size_t(py[i] + 1 + y) * size + px[i] + 1 + x] = c;
            }
        it.out->x0 = (px[i] + 1) * inv;
        it.out->y0 = (py[i] + 1) * inv;
        it.out->x1 = (px[i] + 1 + it.w) * inv;
        it.out->y1 = (py[i] + 1 + it.h) * inv;
        it.out->ok = true;
    }
    ++A.serial;
}

void Sky::prepare(const Weather &w, const std::shared_ptr<const Media> &media, double seconds, float pixel_angle) {
    media_ = media;
    w_ = w;
    seconds_ = seconds;
    pixel_angle_ = pixel_angle;
    if (!media)
        return;
    if (built_for_ != media.get()) {
        buildMips(*media);
        buildStars(*media);
        buildAtlas(*media);
        built_for_ = media.get();
    }

    // the sun: higher at noon in summer
    const float year = 2 * PI * (w.day - 42) / 336.0f;
    const float sun_el = (52 + 22 * std::sin(year)) * PI / 180;
    const float moon_el = (52 - 22 * std::sin(year)) * PI / 180;
    float sx, sy, sz;
    body_dir((w.hour - 6) / 12 * PI, sun_el, w, sx, sy, sz);
    sunx_ = sx;
    suny_ = sy;
    sunz_ = sz;
    {
        const float hl = std::sqrt(sx * sx + sy * sy);
        sunh_x_ = hl > 1e-4f ? sx / hl : 1;
        sunh_y_ = hl > 1e-4f ? sy / hl : 0;
    }
    light_ = smooth(-0.2f, 0.2f, sz);
    stars_ = clamp01((-sz - 0.05f) / 0.2f);
    twilight_ = clamp01(1 - std::abs(sz) / 0.25f);

    const float *c = w.cloud;
    overcast_ = clamp01(std::max({c[CLOUD_STRATUS], c[CLOUD_NIMBUS] * 0.9f, c[CLOUD_ALTOSTRATUS] * 0.45f,
                                  c[CLOUD_CUMULUS_MANY] * 0.25f, c[CLOUD_CUMULUS] * 0.1f, w.fog * 0.6f, w.rain * 0.8f,
                                  w.snow * 0.7f}));
    const float dark = clamp01(c[CLOUD_NIMBUS] * 0.4f + w.rain * 0.15f + w.snow * 0.05f);
    glow_ = 0.5f * (1 - overcast_) * smooth(-0.1f, 0.05f, sz);

    // colours of the sky: clear by day and by night, grey under clouds
    static const float DZ[3] = {52, 104, 188}, DH[3] = {158, 196, 226};
    static const float NZ[3] = {4, 6, 18}, NH[3] = {18, 22, 44};
    static const float HD[3] = {144, 174, 202}, HN[3] = {8, 9, 12};
    static const float CN[3] = {0.13f, 0.14f, 0.19f}, CW[3] = {1.0f, 0.66f, 0.5f};
    const float grey = 0.12f + 0.88f * light_;
    const float og[3] = {120 * grey, 124 * grey, 132 * grey};
    const float fogc[3] = {178 * grey, 182 * grey, 188 * grey};
    float evil[3] = {0, 0, 0};
    const float evil_k = (w.evil_color && w.evil > 0) ? clamp01(w.evil) : 0.0f;
    if (evil_k > 0) {
        const float l = 0.3f + 0.7f * light_;
        evil[0] = px_r(w.evil_color) * l;
        evil[1] = px_g(w.evil_color) * l;
        evil[2] = px_b(w.evil_color) * l;
    }
    for (int i = 0; i < 3; ++i) {
        zen_[i] = mixf(mixf(NZ[i], DZ[i], light_), og[i] * 0.85f, overcast_) * (1 - dark);
        hor_[i] = mixf(mixf(NH[i], DH[i], light_), og[i], overcast_) * (1 - dark);
        float h = mixf(HN[i], HD[i], light_);
        h = mixf(h, og[i], overcast_ * 0.8f);
        h = mixf(h, fogc[i], clamp01(w.fog * 1.2f));
        h *= 1 - dark * 0.8f;
        if (evil_k > 0) {
            h = mixf(h, evil[i], evil_k * 0.6f);
            zen_[i] = mixf(zen_[i], evil[i], evil_k * 0.4f);
            hor_[i] = mixf(hor_[i], evil[i], evil_k * 0.5f);
        }
        haze_rgb_[i] = h;
        cloud_light_[i] = mixf(CN[i], 1.0f, light_) * (1 - dark * 0.5f);
        cloud_light_[i] = mixf(cloud_light_[i], CW[i], twilight_ * 0.6f * light_ * (1 - overcast_ * 0.5f));
        if (evil_k > 0) {
            // A creeping cloud: the clouds are of its stuff, dark and in its colour,
            // fully so from a third of the way in.
            cloud_light_[i] = mixf(cloud_light_[i], evil[i] / 255.0f * 0.5f, clamp01(evil_k * 3));
        }
    }
    haze_ = pack(haze_rgb_[0], haze_rgb_[1], haze_rgb_[2]);
    fog_veil_ = clamp01(w.fog);

    // how far one sees
    sight_ = 1e9f;
    if (w.fog > 0.005f)
        sight_ = std::min(sight_, 10.0f / std::pow(clamp01(w.fog), 1.2f));
    if (w.rain > 0.005f)
        sight_ = std::min(sight_, 40.0f / std::sqrt(clamp01(w.rain)));
    if (w.snow > 0.005f)
        sight_ = std::min(sight_, 30.0f / std::sqrt(clamp01(w.snow)));
    if (evil_k > 0.005f)
        sight_ = std::min(sight_, 30.0f / std::pow(evil_k, 0.7f));
    // how thick the weather's haze gets far off: each kind lets some through
    {
        float clear = 1;
        if (w.fog > 0.005f)
            clear *= 1 - (0.3f + 0.65f * clamp01(w.fog));   // mist 0.52, fog 0.73, thick fog 0.95
        clear *= 1 - 0.45f * clamp01(w.rain);
        clear *= 1 - 0.6f * clamp01(w.snow);
        clear *= 1 - 0.7f * evil_k;
        weather_haze_ = 1 - clear;
    }

    // the sun's picture, upright: right is level, up is away from the horizon
    sun_ = Body{};
    sun_.x = sx;
    sun_.y = sy;
    sun_.z = sz;
    {
        float rx = -sy, ry = sx;
        const float l = std::sqrt(rx * rx + ry * ry);
        if (l > 1e-4f) {
            rx /= l;
            ry /= l;
        } else {
            rx = 1;
            ry = 0;
        }
        sun_.rx = rx;
        sun_.ry = ry;
        sun_.rz = 0;
        sun_.ux = -sz * ry;
        sun_.uy = sz * rx;
        sun_.uz = sx * ry - sy * rx;
    }
    sun_.img = media->sun.ok() ? &media->sun : nullptr;
    if (sun_.img)
        sun_.cosr = std::cos(ART * std::max(sun_.img->w, sun_.img->h) * 0.75f);

    // The moon lags behind the sun by its month: new beside the sun, full
    // across the sky from it. Its lit side is turned towards the sun.
    float mx, my, mz;
    body_dir((w.hour - 6 - w.moon * 24) / 12 * PI, moon_el, w, mx, my, mz);
    const float mf = w.moon - std::floor(w.moon);
    const int phase = int(std::floor(mf * MOON_PHASES + 0.5f)) % MOON_PHASES;
    moon_ = Body{};
    moon_.x = mx;
    moon_.y = my;
    moon_.z = mz;
    moon_.phase = phase;
    {
        const float d = sx * mx + sy * my + sz * mz;
        float rx = sx - mx * d, ry = sy - my * d, rz = sz - mz * d;
        float l = std::sqrt(rx * rx + ry * ry + rz * rz);
        if (l < 1e-3f) {
            rx = -my;
            ry = mx;
            rz = 0;
            l = std::sqrt(rx * rx + ry * ry);
        }
        if (l < 1e-4f) {
            rx = 1;
            ry = rz = 0;
            l = 1;
        }
        // the pictures are lit from the right while it waxes, from the left while it wanes
        const float k = (phase > MOON_PHASES / 2 ? -1.0f : 1.0f) / l;
        rx *= k;
        ry *= k;
        rz *= k;
        moon_.rx = rx;
        moon_.ry = ry;
        moon_.rz = rz;
        moon_.ux = my * rz - mz * ry;
        moon_.uy = mz * rx - mx * rz;
        moon_.uz = mx * ry - my * rx;
    }
    moon_.img = media->moon[phase].ok() ? &media->moon[phase] : nullptr;
    if (moon_.img)
        moon_.cosr = std::cos(ART * std::max(moon_.img->w, moon_.img->h) * 0.75f);
    moon_.alpha = 0.45f + 0.55f * (1 - light_);
    moon_reach_ = std::sqrt(std::max(0.0f, 1 - moon_.cosr * moon_.cosr)) + 0.01f;
    sun_reach_ = std::sqrt(std::max(0.0f, 1 - sun_.cosr * sun_.cosr)) + 0.01f;
    if (glow_ > 0.003f)
        sun_reach_ = std::max(sun_reach_, 0.45f);   // the glow reaches out to cos 0.9

    // the stars turn once a day around a pole above the north (south) horizon,
    // and a little more with the year, so the seasons have their own stars
    {
        const float pl = 40 * PI / 180;
        const float px = 0, py = (w.sun_north ? 1.0f : -1.0f) * std::cos(pl), pz = std::sin(pl);
        const float th = 2 * PI * (w.hour / 24.0f + w.day / 336.0f);
        const float ct = std::cos(th), st = std::sin(th), ic = 1 - ct;
        float *M = star_rot_;
        M[0] = ct + px * px * ic;
        M[1] = px * py * ic - pz * st;
        M[2] = px * pz * ic + py * st;
        M[3] = py * px * ic + pz * st;
        M[4] = ct + py * py * ic;
        M[5] = py * pz * ic - px * st;
        M[6] = pz * px * ic - py * st;
        M[7] = pz * py * ic + px * st;
        M[8] = ct + pz * pz * ic;
    }

    // the clouds, farthest (highest) first
    struct Kind {
        int k;
        float scale, drift, fade_lo, fade_hi, start;
    };
    static const Kind KINDS[] = {
        {CLOUD_CIRRUS, 150, 1.2f, 0.03f, 0.2f, 0},
        {CLOUD_ALTOSTRATUS, 110, 0.8f, 0.0f, 0.1f, 37},
        {CLOUD_CUMULUS, 85, 2.0f, 0.02f, 0.15f, 71},
        {CLOUD_CUMULUS_MANY, 85, 2.0f, 0.02f, 0.15f, 143},
        {CLOUD_NIMBUS, 70, 2.6f, 0.0f, 0.1f, 19},
        {CLOUD_STRATUS, 60, 0.6f, -0.02f, 0.05f, 97},
    };
    nlayers_ = 0;
    for (const Kind &kd : KINDS) {
        const float amount = clamp01(c[kd.k]);
        if (amount < 0.005f || mips_[kd.k].empty())
            continue;
        Layer &L = layers_[nlayers_++];
        L.kind = kd.k;
        L.mips = &mips_[kd.k];
        L.amount = amount;
        L.scale = kd.scale;
        L.fade_lo = kd.fade_lo;
        L.fade_hi = kd.fade_hi;
        const Mip &m0 = mips_[kd.k][0];
        // drifting with the wind: the picture moves the other way under the sky
        const double dist = seconds * kd.drift;
        L.off_x = float(std::fmod(kd.start - dist * w.wind_x, double(m0.w)));
        L.off_y = float(std::fmod(kd.start * 0.6 - dist * w.wind_y, double(m0.h)));
        if (L.off_x < 0)
            L.off_x += m0.w;
        if (L.off_y < 0)
            L.off_y += m0.h;
        // A pixel of the screen covers more of the picture the lower it looks:
        // there the smaller copy whose pixels are about as large as the screen's.
        for (int i = 0; i <= Layer::Z_STEPS; ++i) {
            const float z = std::max(float(i) / Layer::Z_STEPS, 0.002f);
            L.alpha[i] = cloudAlpha(L, z);
            const float foot = pixel_angle * L.scale / (z * z);
            const int lv = foot < 1.5f ? 0 : foot < 3 ? 1 : foot < 6 ? 2 : 3;
            L.level[i] = uint8_t(std::min(lv, int(L.mips->size()) - 1));
        }
    }
}

// how much of a cloud layer shows at a height over the horizon (sine of the
// angle): it thins out low down, where the sky behind is the colour of the haze
float Sky::cloudAlpha(const Layer &L, float z) const {
    const float far = (1 - clamp01(z * (1 / 0.3f))) * 0.55f;
    return L.amount * smooth(L.fade_lo, L.fade_hi, z) * (1 - far);
}

// the plain sky: from the horizon's colour to the zenith's (f: 0 at the
// horizon, 1 straight up), red on the sun's side at dawn and dusk
void Sky::base(float x, float y, float z, float f, float &r, float &g, float &b) const {
    (void)z;
    r = mixf(hor_[0], zen_[0], f);
    g = mixf(hor_[1], zen_[1], f);
    b = mixf(hor_[2], zen_[2], f);
    if (twilight_ > 0.01f && overcast_ < 0.99f) {
        const float hl2 = x * x + y * y;
        if (hl2 > 1e-6f) {
            float k = std::max(0.0f, (x * sunh_x_ + y * sunh_y_) / std::sqrt(hl2));
            k *= k;
            const float wgt = twilight_ * (1 - overcast_) * k * (1 - f) * 0.75f;
            r = mixf(r, warm_[0], wgt);
            g = mixf(g, warm_[1], wgt);
            b = mixf(b, warm_[2], wgt);
        }
    }
}

void Sky::blendBody(const Body &b, float x, float y, float z, float &r, float &g, float &bl) const {
    const float c = x * b.x + y * b.y + z * b.z;
    if (c < b.cosr)
        return;
    const float ic = 1 / c;
    blendAt(b, (x * b.rx + y * b.ry + z * b.rz) * ic, (x * b.ux + y * b.uy + z * b.uz) * ic, r, g, bl);
}

void Sky::blendFlat(const Body &b, const Flat &f, float pa, float pt, float &r, float &g, float &bl) const {
    const float da = pa - f.a, dt = pt - f.t;
    blendAt(b, (da * f.ra + dt * f.rt) * f.inv, (da * f.ua + dt * f.ut) * f.inv, r, g, bl);
}

Sky::Flat Sky::flatten(const Body &b, float dirx, float diry, float rgtx, float rgty) {
    Flat o;
    const float f = b.x * dirx + b.y * diry;
    if (!b.img || b.z <= -0.15f || f < 0.02f)
        return o;
    const float inv_f = 1 / f;
    o.a = (b.x * rgtx + b.y * rgty) * inv_f;
    o.t = b.z * inv_f;
    // how far its middle moves on the screen as the direction turns along the picture's right or up
    const auto d = [&](float tx, float ty, float tz, float &da, float &dt) {
        const float td = tx * dirx + ty * diry;
        da = (tx * rgtx + ty * rgty - o.a * td) * inv_f;
        dt = (tz - o.t * td) * inv_f;
    };
    float ra, rt, ua, ut;
    d(b.rx, b.ry, b.rz, ra, rt);
    d(b.ux, b.uy, b.uz, ua, ut);
    const float lr = std::sqrt(ra * ra + rt * rt), lu = std::sqrt(ua * ua + ut * ut);
    // the smaller of the two: the size it has across, not the stretched one
    const float k = std::min(lr, lu);
    if (k < 1e-6f)
        return o;
    o.ra = ra / lr;
    o.rt = rt / lr;
    o.ua = -o.rt;
    o.ut = o.ra;
    if (o.ua * ua + o.ut * ut < 0) {
        o.ua = -o.ua;
        o.ut = -o.ut;
    }
    o.inv = 1 / k;
    o.on = true;
    return o;
}

void Sky::blendAt(const Body &b, float u, float v, float &r, float &g, float &bl) {
    const Image &im = *b.img;
    const int px = ifloor(u * (1 / ART) + im.w * 0.5f), py = ifloor(im.h * 0.5f - v * (1 / ART));
    if (px < 0 || py < 0 || px >= im.w || py >= im.h)
        return;
    const uint32_t p = im.px[size_t(py) * im.w + px];
    const float a = px_a(p) * (1 / 255.0f) * b.alpha;
    if (a <= 0)
        return;
    r = mixf(r, float(px_r(p)), a);
    g = mixf(g, float(px_g(p)), a);
    bl = mixf(bl, float(px_b(p)), a);
}

#ifdef _MSC_VER
__forceinline
#else
inline __attribute__((always_inline))
#endif
uint32_t Sky::shade(float x, float y, float z, bool sun, bool moon, const Flat *fs, const Flat *fm, float pa,
                    float pt) const {
    const float up = z > 0 ? z : 0.0f;
    float down = 1 - up;
    down *= down;
    const float f = 1 - down * down;   // 0 at the horizon, 1 straight up
    float r, g, b;
    base(x, y, z, f, r, g, b);

    // stars, on a sphere of their own that turns through the night
    if (stars_ > 0.01f && z > 0 && overcast_ < 0.98f && media_->stars.ok()) {
        const float *M = star_rot_;
        const float tx = M[0] * x + M[1] * y + M[2] * z;
        const float ty = M[3] * x + M[4] * y + M[5] * z;
        const float tz = M[6] * x + M[7] * y + M[8] * z;
        const float ax = std::abs(tx), ay = std::abs(ty), az = std::abs(tz);
        uint32_t face;
        float u, v, m;
        if (ax >= ay && ax >= az) {
            face = tx > 0 ? 0 : 1;
            m = ax;
            u = ty;
            v = tz;
        } else if (ay >= az) {
            face = ty > 0 ? 2 : 3;
            m = ay;
            u = tx;
            v = tz;
        } else {
            face = tz > 0 ? 4 : 5;
            m = az;
            u = tx;
            v = ty;
        }
        const float im = 1 / m;
        u *= im;
        v *= im;
        const int ci = std::clamp(int((u + 1) * 0.5f * STAR_GRID), 0, STAR_GRID - 1);
        const int cj = std::clamp(int((v + 1) * 0.5f * STAR_GRID), 0, STAR_GRID - 1);
        const uint32_t h = hash3(face, uint32_t(ci), uint32_t(cj));
        if ((h & 1023) < STAR_CHANCE) {
            const Image &st = media_->stars;
            const int fs = st.h;
            const int frames = std::max(1, st.w / std::max(1, fs));
            const float cell = 2.0f / STAR_GRID;
            const float uc = -1 + (ci + 0.25f + 0.5f * ((h >> 10) & 255) / 255.0f) * cell;
            const float vc = -1 + (cj + 0.25f + 0.5f * ((h >> 18) & 255) / 255.0f) * cell;
            const int px = ifloor((u - uc) * (1 / STAR_ART) + fs * 0.5f);
            const int py = ifloor(fs * 0.5f - (v - vc) * (1 / STAR_ART));
            if (px >= 0 && py >= 0 && px < fs && py < fs) {
                // most stars are the small ones at the start of the row
                const float pick = ((h >> 26) & 63) / 63.0f;
                const int fr = std::min(frames - 1, int(frames * pick * pick * pick));
                const uint32_t sc = st.px[size_t(py) * st.w + fr * fs + px];
                const float twinkle =
                    0.7f + 0.3f * std::sin(float(seconds_) * (1.3f + (h & 7) * 0.4f) + float((h >> 4) & 63));
                const float a = px_a(sc) / 255.0f * stars_ * (1 - overcast_) * clamp01(z * 6) * twinkle;
                r = mixf(r, float(px_r(sc)), a);
                g = mixf(g, float(px_g(sc)), a);
                b = mixf(b, float(px_b(sc)), a);
            }
        }
    }

    if (fm) {
        if (fm->on)
            blendFlat(moon_, *fm, pa, pt, r, g, b);
    } else if (moon)
        blendBody(moon_, x, y, z, r, g, b);
    if (fs) {
        if (sun && glow_ > 0.003f) {
            const float c = x * sunx_ + y * suny_ + z * sunz_;
            if (c > 0.9f) {
                float t = (c - 0.9f) * 10;
                t *= t;
                t *= t * t;
                const float a = t * glow_;
                r = mixf(r, 255, a);
                g = mixf(g, 235, a);
                b = mixf(b, 190, a);
            }
        }
        if (fs->on)
            blendFlat(sun_, *fs, pa, pt, r, g, b);
    } else if (sun) {
        // a glow around the sun
        if (glow_ > 0.003f) {
            const float c = x * sunx_ + y * suny_ + z * sunz_;
            if (c > 0.9f) {
                float t = (c - 0.9f) * 10;
                t *= t;
                t *= t * t;
                const float a = t * glow_;
                r = mixf(r, 255, a);
                g = mixf(g, 235, a);
                b = mixf(b, 190, a);
            }
        }
        if (sun_.img)
            blendBody(sun_, x, y, z, r, g, b);
    }

    // clouds: each kind is a picture repeated over a flat ceiling high up
    if (nlayers_ > 0 && z > 0.004f) {
        const float iz = 1 / z;
        const float px = x * iz, py = y * iz;          // where the line of sight meets the ceiling
        const int zi = int(z * Layer::Z_STEPS);
        for (int i = 0; i < nlayers_; ++i) {
            const Layer &L = layers_[i];
            const float la = L.alpha[zi];
            if (la <= 0.003f)
                continue;
            const int lv = L.level[zi];
            const Mip &mp = (*L.mips)[lv];
            const float k = 1.0f / float(1 << lv);
            const int iu = wrap(ifloor((px * L.scale + L.off_x) * k), mp.w, mp.wmask);
            const int iv = wrap(ifloor((py * L.scale + L.off_y) * k), mp.h, mp.hmask);
            const uint32_t cc = mp.px[size_t(iv) * mp.w + iu];
            if (cc < 0x01000000u)
                continue;
            const float a = px_a(cc) * (1 / 255.0f) * la;
            if (a <= 0.003f)
                continue;
            r = mixf(r, px_r(cc) * cloud_light_[0], a);
            g = mixf(g, px_g(cc) * cloud_light_[1], a);
            b = mixf(b, px_b(cc) * cloud_light_[2], a);
        }
    }

    if (fog_veil_ > 0.001f) {
        const float k = clamp01(fog_veil_ * (0.75f + 0.25f * (1 - f)) * 1.05f);
        r = mixf(r, haze_rgb_[0], k);
        g = mixf(g, haze_rgb_[1], k);
        b = mixf(b, haze_rgb_[2], k);
    }
    // every step mixed colours of 0..255, so no clamping is needed
    return 0xFF000000u | (uint32_t(std::min(int(b + 0.5f), 255)) << 16) |
           (uint32_t(std::min(int(g + 0.5f), 255)) << 8) | uint32_t(std::min(int(r + 0.5f), 255));
}

uint32_t Sky::at(float x, float y, float z) const {
    const float len2 = x * x + y * y + z * z;
    if (len2 < 1e-12f || !media_)
        return haze_;
    const float il = 1.0f / std::sqrt(len2);
    return shade(x * il, y * il, z * il, sunz_ > -0.15f, moon_.img && moon_.z > -0.15f);
}

void Sky::column(float dirx, float diry, float rgtx, float rgty, float a, float t0, float step, int n,
                 uint32_t *out) const {
    if (!media_) {
        std::fill(out, out + n, haze_);
        return;
    }
    const float x = dirx + rgtx * a, y = diry + rgty * a;
    // A column's directions all lie in one upright plane through the eye: the
    // sun's glow is only looked for if it comes near enough to it, the sun
    // and moon pictures if they reach across the screen to this column.
    const float h2 = x * x + y * y;
    bool sun = sunz_ > -0.15f && glow_ > 0.003f;
    if (sun && h2 > 1e-12f) {
        const float ih = 1 / std::sqrt(h2);
        sun = std::abs(sunx_ * (-y * ih) + suny_ * (x * ih)) < sun_reach_;
    }
    const auto flat = [&](const Body &b) {
        Flat f = flatten(b, dirx, diry, rgtx, rgty);
        if (f.on && std::abs(a - f.a) * f.inv > 0.5f * ART * std::hypot(float(b.img->w), float(b.img->h)))
            f.on = false;
        return f;
    };
    const Flat fs = flat(sun_), fm = flat(moon_);
    for (int i = 0; i < n; ++i) {
        const float t = t0 + i * step;
        const float il = 1 / std::sqrt(h2 + t * t);
        out[i] = shade(x * il, y * il, t * il, sun, false, &fs, &fm, a, t);
    }
}

void Sky::mesh(const View3D &v, float W, float H, const bool *open, int n_open, SkyMesh &out) const {
    out.clear();
    if (!media_ || atlas_.size <= 0 || !atlas_.white.ok)
        return;
    Emit e{v, W, H, out};
    const float wu = (atlas_.white.x0 + atlas_.white.x1) * 0.5f, wv = (atlas_.white.y0 + atlas_.white.y1) * 0.5f;
    bool all_open = true;
    if (open)
        for (int s = 0; s < n_open; ++s)
            all_open = all_open && open[s];
    const auto is_open = [&](float x, float y) {
        return all_open || open[sector_of(x, y, n_open)];
    };

    // the dome, coloured at its corners; the fog over everything uses the same corners
    static const float EL[] = {-6, -1, 0, 2, 5, 9, 14, 20, 28, 38, 50, 64, 78, 90};
    constexpr int NE = int(sizeof(EL) / sizeof(EL[0]));
    constexpr int AZ = 64;
    CV grid[NE][AZ + 1];
    float veil[NE];
    for (int j = 0; j < NE; ++j) {
        const float se = std::sin(EL[j] * PI / 180), ce = std::cos(EL[j] * PI / 180);
        const float up = std::max(se, 0.0f);
        float down = 1 - up;
        down *= down;
        const float f = 1 - down * down;
        veil[j] = clamp01(fog_veil_ * (0.75f + 0.25f * (1 - f)) * 1.05f);
        for (int i = 0; i <= AZ; ++i) {
            const float az = i * 2 * PI / AZ;
            const float x = std::sin(az) * ce, y = -std::cos(az) * ce;
            CV &c = grid[j][i];
            e.cam(x, y, se, c);
            base(x, y, se, f, c.r, c.g, c.b);
            c.a = 255;
            c.u = wu;
            c.v = wv;
        }
    }
    for (int i = 0; i < AZ; ++i) {
        if (!all_open && !open[i * n_open / AZ])
            continue;
        for (int j = 0; j + 1 < NE; ++j)
            e.quad(grid[j + 1][i], grid[j + 1][i + 1], grid[j][i + 1], grid[j][i]);
    }

    // a picture standing on the sky around a direction: x and y are its pixels,
    // right and up across it, `cut` x `cut` pieces so it keeps its shape
    const auto picture = [&](float bx, float by, float bz, float rx, float ry, float rz, float ux, float uy, float uz,
                             float sw, float sh, const SkyAtlas::Rect &rc, float alpha, int cut) {
        CV p[9][9];
        for (int j = 0; j <= cut; ++j)
            for (int i = 0; i <= cut; ++i) {
                const float a = (float(i) / cut - 0.5f) * sw, b = (0.5f - float(j) / cut) * sh;
                CV &c = p[j][i];
                e.cam(bx + rx * a + ux * b, by + ry * a + uy * b, bz + rz * a + uz * b, c);
                c.r = c.g = c.b = 255;
                c.a = alpha * 255;
                c.u = mixf(rc.x0, rc.x1, float(i) / cut);
                c.v = mixf(rc.y0, rc.y1, float(j) / cut);
            }
        for (int j = 0; j < cut; ++j)
            for (int i = 0; i < cut; ++i)
                e.quad(p[j][i], p[j][i + 1], p[j + 1][i + 1], p[j + 1][i]);
    };

    // stars
    if (stars_ > 0.01f && overcast_ < 0.98f && !atlas_.stars.empty()) {
        const float *M = star_rot_;
        const float k = stars_ * (1 - overcast_);
        for (const Star &s : stars_list_) {
            // back from the stars' sphere: the turn's transpose
            const float x = M[0] * s.x + M[3] * s.y + M[6] * s.z;
            const float y = M[1] * s.x + M[4] * s.y + M[7] * s.z;
            const float z = M[2] * s.x + M[5] * s.y + M[8] * s.z;
            if (z <= 0)
                continue;
            const float depth = x * v.fx + y * v.fy + z * v.fz;
            if (depth < 0.05f || !is_open(x, y))
                continue;
            const SkyAtlas::Rect &rc = atlas_.stars[std::min<size_t>(s.frame, atlas_.stars.size() - 1)];
            if (!rc.ok)
                continue;
            const float twinkle = 0.7f + 0.3f * std::sin(float(seconds_) * s.rate + s.phase);
            const float a = k * clamp01(z * 6) * twinkle;
            float rx = -y, ry = x;
            const float l = std::sqrt(rx * rx + ry * ry);
            if (l < 1e-4f)
                continue;
            rx /= l;
            ry /= l;
            const float sz = media_->stars.h * STAR_ART;
            picture(x, y, z, rx, ry, 0, -z * ry, z * rx, x * ry - y * rx, sz, sz, rc, a, 1);
        }
    }

    // The sun or the moon: a picture facing the eye, flat on the screen, so it
    // stays round wherever it stands in the view. Its size is the one it has
    // across the line from the screen's middle (perspective would stretch it
    // along that line towards the edges).
    const auto facing = [&](const Body &b, const SkyAtlas::Rect &rc, float alpha) {
        CV c, r, u;
        e.cam(b.x, b.y, b.z, c);
        if (c.z < 0.02f)
            return;
        e.cam(b.rx, b.ry, b.rz, r);
        e.cam(b.ux, b.uy, b.uz, u);
        // where the picture's right and up point on the screen around its middle
        float sx = r.x * c.z - c.x * r.z, sy = r.y * c.z - c.y * r.z;
        const float l = std::sqrt(sx * sx + sy * sy);
        if (l < 1e-6f) {
            sx = 1;
            sy = 0;
        } else {
            sx /= l;
            sy /= l;
        }
        float ux = -sy, uy = sx;
        if (ux * (u.x * c.z - c.x * u.z) + uy * (u.y * c.z - c.y * u.z) < 0) {
            ux = -ux;
            uy = -uy;
        }
        const float hw = 0.5f * b.img->w * ART, hh = 0.5f * b.img->h * ART;
        CV p[4];
        static const float CA[4] = {-1, 1, 1, -1}, CB[4] = {1, 1, -1, -1};
        for (int k = 0; k < 4; ++k) {
            const float a = CA[k] * hw, bb = CB[k] * hh;
            p[k].x = c.x + sx * a + ux * bb;
            p[k].y = c.y + sy * a + uy * bb;
            p[k].z = c.z;
            p[k].r = p[k].g = p[k].b = 255;
            p[k].a = alpha * 255;
            p[k].u = CA[k] < 0 ? rc.x0 : rc.x1;
            p[k].v = CB[k] > 0 ? rc.y0 : rc.y1;
        }
        e.quad(p[0], p[1], p[2], p[3]);
    };

    // the moon, the sun's glow, the sun
    if (moon_.img && moon_.z > -0.15f && atlas_.moon[moon_.phase].ok && is_open(moon_.x, moon_.y))
        facing(moon_, atlas_.moon[moon_.phase], moon_.alpha);
    if (sunz_ > -0.15f && is_open(sunx_, suny_)) {
        if (glow_ > 0.003f) {
            static const float RING[] = {1.0f, 0.997f, 0.99f, 0.98f, 0.965f, 0.945f, 0.92f, 0.9f};
            constexpr int NR = int(sizeof(RING) / sizeof(RING[0]));
            constexpr int ARC = 24;
            CV ring[NR][ARC + 1];
            for (int j = 0; j < NR; ++j) {
                float t = (RING[j] - 0.9f) * 10;
                t *= t;
                t *= t * t;
                const float s = std::sqrt(std::max(0.0f, 1 - RING[j] * RING[j]));
                for (int i = 0; i <= ARC; ++i) {
                    const float a = i * 2 * PI / ARC, ca = std::cos(a) * s, sa = std::sin(a) * s;
                    CV &c = ring[j][i];
                    e.cam(sunx_ * RING[j] + sun_.rx * ca + sun_.ux * sa, suny_ * RING[j] + sun_.ry * ca + sun_.uy * sa,
                          sunz_ * RING[j] + sun_.rz * ca + sun_.uz * sa, c);
                    c.r = 255;
                    c.g = 235;
                    c.b = 190;
                    c.a = t * glow_ * 255;
                    c.u = wu;
                    c.v = wv;
                }
            }
            for (int j = 0; j + 1 < NR; ++j)
                for (int i = 0; i < ARC; ++i)
                    e.quad(ring[j][i], ring[j][i + 1], ring[j + 1][i + 1], ring[j + 1][i]);
        }
        if (sun_.img && atlas_.sun.ok)
            facing(sun_, atlas_.sun, 1);
    }

    // Clouds: tiles of the picture over a ceiling at height 1. Near overhead
    // one tile per picture; farther out, where a pixel of the screen covers
    // several of the picture's, tiles of 2x2, 4x4, 8x8 pictures in the
    // smaller copies, so there are few tiles and nothing flickers.
    for (int li = 0; li < nlayers_; ++li) {
        const Layer &L = layers_[li];
        const Mip &m0 = (*L.mips)[0];
        const int levels = std::min<int>(int(L.mips->size()), SkyAtlas::LEVELS);
        const float pa_s = pixel_angle_ * L.scale;
        float reach[SkyAtlas::LEVELS];
        const float far_r =
            L.fade_lo > 0.005f ? std::min(CLOUD_FAR, std::sqrt(1 - L.fade_lo * L.fade_lo) / L.fade_lo) : CLOUD_FAR;
        for (int l = 0; l < levels; ++l)
            reach[l] = l + 1 < levels ? std::sqrt(std::max(0.0f, 1.5f * float(1 << l) / pa_s - 1)) : far_r;
        // the part of the ceiling each level covers, in the picture's pixels, on that level's grid;
        // each box also lies on the grid of the level above it, so the levels fit together
        struct Box {
            int x0, y0, x1, y1;   // grid cells [x0, x1) x [y0, y1)
        } box[SkyAtlas::LEVELS];
        for (int l = levels - 1; l >= 0; --l) {
            const int up = l + 1 < levels ? l + 1 : l;   // the grid it is rounded to
            const float cw = float(m0.w << up), ch = float(m0.h << up);
            const float r = std::min(reach[l], far_r);
            int gx0 = ifloor((L.off_x - r * L.scale) / cw), gx1 = ifloor((L.off_x + r * L.scale) / cw) + 1;
            int gy0 = ifloor((L.off_y - r * L.scale) / ch), gy1 = ifloor((L.off_y + r * L.scale) / ch) + 1;
            if (up != l) {
                // inside the box of the level above
                gx0 = std::max(gx0, box[up].x0);
                gy0 = std::max(gy0, box[up].y0);
                gx1 = std::min(gx1, box[up].x1);
                gy1 = std::min(gy1, box[up].y1);
            }
            // in this level's own cells
            const int k = 1 << (up - l);
            box[l] = Box{gx0 * k, gy0 * k, gx1 * k, gy1 * k};
            if (box[l].x1 < box[l].x0)
                box[l].x1 = box[l].x0;
            if (box[l].y1 < box[l].y0)
                box[l].y1 = box[l].y0;
        }
        const float lr = cloud_light_[0] * 255, lg = cloud_light_[1] * 255, lb = cloud_light_[2] * 255;
        for (int l = 0; l < levels; ++l) {
            const SkyAtlas::Rect &rc = atlas_.cloud[L.kind][l];
            if (!rc.ok)
                continue;
            const float cw = float(m0.w << l), ch = float(m0.h << l);
            // the inner box, on this level's grid: covered by the levels below
            Box inner{0, 0, 0, 0};
            if (l > 0)
                inner = Box{box[l - 1].x0 >> 1, box[l - 1].y0 >> 1, box[l - 1].x1 >> 1, box[l - 1].y1 >> 1};
            for (int gy = box[l].y0; gy < box[l].y1; ++gy)
                for (int gx = box[l].x0; gx < box[l].x1; ++gx) {
                    if (l > 0 && gx >= inner.x0 && gx < inner.x1 && gy >= inner.y0 && gy < inner.y1)
                        continue;
                    // the cell on the ceiling, relative to the eye
                    const float x0 = (gx * cw - L.off_x) / L.scale, x1 = ((gx + 1) * cw - L.off_x) / L.scale;
                    const float y0 = (gy * ch - L.off_y) / L.scale, y1 = ((gy + 1) * ch - L.off_y) / L.scale;
                    const float nx = std::clamp(0.0f, x0, x1), ny = std::clamp(0.0f, y0, y1);
                    const float rn = std::sqrt(nx * nx + ny * ny);
                    if (rn > far_r || cloudAlpha(L, 1 / std::sqrt(1 + rn * rn)) <= 0.002f)
                        continue;
                    const float span = std::max(x1 - x0, y1 - y0);
                    const int n = std::clamp(int(std::ceil(span / (0.4f * (1 + rn)))), 1, 8);
                    CV p[9][9];
                    for (int j = 0; j <= n; ++j)
                        for (int i = 0; i <= n; ++i) {
                            const float tx = float(i) / n, ty = float(j) / n;
                            const float qx = mixf(x0, x1, tx), qy = mixf(y0, y1, ty);
                            CV &c = p[j][i];
                            e.cam(qx, qy, 1, c);
                            c.r = lr;
                            c.g = lg;
                            c.b = lb;
                            c.a = 255 * cloudAlpha(L, 1 / std::sqrt(1 + qx * qx + qy * qy));
                            c.u = mixf(rc.x0, rc.x1, tx);
                            c.v = mixf(rc.y0, rc.y1, ty);
                        }
                    for (int j = 0; j < n; ++j)
                        for (int i = 0; i < n; ++i) {
                            if (!all_open) {
                                const float qx = mixf(x0, x1, (i + 0.5f) / n), qy = mixf(y0, y1, (j + 0.5f) / n);
                                if (!is_open(qx, qy))
                                    continue;
                            }
                            if (p[j][i].a <= 0 && p[j][i + 1].a <= 0 && p[j + 1][i].a <= 0 && p[j + 1][i + 1].a <= 0)
                                continue;
                            e.quad(p[j][i], p[j][i + 1], p[j + 1][i + 1], p[j + 1][i]);
                        }
                }
        }
    }

    // fog over all of it
    if (fog_veil_ > 0.001f) {
        for (int j = 0; j < NE; ++j)
            for (int i = 0; i <= AZ; ++i) {
                CV &c = grid[j][i];
                c.r = haze_rgb_[0];
                c.g = haze_rgb_[1];
                c.b = haze_rgb_[2];
                c.a = veil[j] * 255;
            }
        for (int i = 0; i < AZ; ++i) {
            if (!all_open && !open[i * n_open / AZ])
                continue;
            for (int j = 0; j + 1 < NE; ++j)
                e.quad(grid[j + 1][i], grid[j + 1][i + 1], grid[j][i + 1], grid[j][i]);
        }
    }
}

// ---------------------------------------------------------------------------
// rain and snow

uint32_t Precip::next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void Precip::step(float dt, float x, float y, float eye, const Weather &w) {
    t_ += dt;
    wind_x_ = w.wind_x;
    wind_y_ = w.wind_y;
    const int want_rain = int(MAX_RAIN * clamp01(w.rain)), want_snow = int(MAX_SNOW * clamp01(w.snow));
    int have_rain = 0, have_snow = 0;
    for (const Drop &d : drops_)
        ++(d.snow ? have_snow : have_rain);
    if (have_rain > want_rain || have_snow > want_snow) {
        int cut_rain = have_rain - want_rain, cut_snow = have_snow - want_snow;
        drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                    [&](const Drop &d) {
                                        int &cut = d.snow ? cut_snow : cut_rain;
                                        if (cut <= 0)
                                            return false;
                                        --cut;
                                        return true;
                                    }),
                     drops_.end());
        have_rain = std::min(have_rain, want_rain);
        have_snow = std::min(have_snow, want_snow);
    }
    const auto spawn = [&](bool snow) {
        Drop d;
        d.x = x + (unit() * 2 - 1) * BOX_R;
        d.y = y + (unit() * 2 - 1) * BOX_R;
        d.z = eye + BOX_LO + unit() * (BOX_HI - BOX_LO);
        d.seed = next();
        d.snow = snow;
        drops_.push_back(d);
    };
    for (; have_rain < want_rain; ++have_rain)
        spawn(false);
    for (; have_snow < want_snow; ++have_snow)
        spawn(true);

    const auto wrap_box = [](float v) { return v - 2 * BOX_R * std::floor((v + BOX_R) / (2 * BOX_R)); };
    const float t = float(std::fmod(t_, 1000.0));
    for (Drop &d : drops_) {
        if (d.snow) {
            // flakes sway as they drift down
            const float ph = t * (0.8f + (d.seed & 15) * 0.05f) + ((d.seed >> 4) & 255) * 0.1f;
            d.x += (wind_x_ * SNOW_WIND + std::sin(ph) * 0.25f) * dt;
            d.y += (wind_y_ * SNOW_WIND + std::cos(ph * 0.7f) * 0.25f) * dt;
            d.z -= SNOW_SPEED * (0.8f + ((d.seed >> 12) & 15) * 0.03f) * dt;
        } else {
            d.x += wind_x_ * RAIN_WIND * dt;
            d.y += wind_y_ * RAIN_WIND * dt;
            d.z -= RAIN_SPEED * (0.9f + ((d.seed >> 12) & 15) * 0.015f) * dt;
        }
        if (d.z < eye + BOX_LO) {
            // fallen through the box: again from the top, somewhere else
            d.z += BOX_HI - BOX_LO;
            d.x = x + (unit() * 2 - 1) * BOX_R;
            d.y = y + (unit() * 2 - 1) * BOX_R;
        } else if (d.z > eye + BOX_HI) {
            d.z -= BOX_HI - BOX_LO;
        }
        // the box goes where the eye goes
        d.x = x + wrap_box(d.x - x);
        d.y = y + wrap_box(d.y - y);
    }
}

bool Precip::shown(const Drop &d, const View3D &v, float W, float H, const Scene &scene, Shown &s) const {
    if (!v.project(d.x, d.y, d.z, s.sx, s.sy, s.depth) || s.depth < 0.5f)
        return false;
    if (s.sx < -60 || s.sx > W + 60 || s.sy < -60 || s.sy > H + 400)
        return false;
    if (!open_air(scene, d.x, d.y, d.z) || !clear_line(scene, v.px, v.py, v.eye, d.x, d.y, d.z))
        return false;
    s.fade = std::clamp(1.25f - s.depth / (BOX_R + 1.0f), 0.15f, 1.0f);
    if (!d.snow) {
        // a raindrop: a streak from where it was a moment ago to where it is
        float d0;
        if (!v.project(d.x - wind_x_ * RAIN_WIND * STREAK, d.y - wind_y_ * RAIN_WIND * STREAK,
                       d.z + RAIN_SPEED * STREAK, s.sx0, s.sy0, d0))
            return false;
    }
    return true;
}

void Precip::draw(uint32_t *buf, int W, int H, const View3D &v, const Scene &scene, const Media &m, const Weather &w,
                  int box[4]) const {
    if (drops_.empty() || W <= 0 || H <= 0)
        return;
    const float ui = std::max(1.0f, H / 540.0f);
    const auto grow = [&](int x0, int y0, int x1, int y1) {
        if (box[2] <= box[0] || box[3] <= box[1]) {
            box[0] = x0;
            box[1] = y0;
            box[2] = x1;
            box[3] = y1;
        } else {
            box[0] = std::min(box[0], x0);
            box[1] = std::min(box[1], y0);
            box[2] = std::max(box[2], x1);
            box[3] = std::max(box[3], y1);
        }
    };
    const float tr = w.rain_color ? px_r(w.rain_color) / 255.0f : 1.0f;
    const float tg = w.rain_color ? px_g(w.rain_color) / 255.0f : 1.0f;
    const float tb = w.rain_color ? px_b(w.rain_color) / 255.0f : 1.0f;
    for (const Drop &d : drops_) {
        const Image &art = d.snow ? m.snow : m.rain;
        if (!art.ok())
            continue;
        Shown s;
        if (!shown(d, v, float(W), float(H), scene, s))
            continue;

        if (d.snow) {
            const int fs = art.h;
            const int frames = std::max(1, art.w / std::max(1, fs));
            const int fr = int(d.seed % uint32_t(frames));
            const int size = std::clamp(int(0.05f * v.S / s.depth + 0.5f), 1, int(10 * ui));
            const int x0 = ifloor(s.sx - size * 0.5f), y0 = ifloor(s.sy - size * 0.5f);
            for (int ox = 0; ox < size; ++ox) {
                const int xx = x0 + ox;
                if (xx < 0 || xx >= W)
                    continue;
                const int ac = fr * fs + std::min(fs - 1, ox * fs / size);
                for (int oy = 0; oy < size; ++oy) {
                    const int yy = y0 + oy;
                    if (yy < 0 || yy >= H)
                        continue;
                    const uint32_t c = art.px[size_t(std::min(fs - 1, oy * fs / size)) * art.w + ac];
                    const float a = px_a(c) / 255.0f * s.fade * 0.95f;
                    if (a > 0.01f)
                        blend_px(buf[size_t(xx) * H + yy], px_r(c), px_g(c), px_b(c), a);
                }
            }
            grow(x0, y0, x0 + size, y0 + size);
            continue;
        }

        const float len = std::hypot(s.sx - s.sx0, s.sy - s.sy0);
        const int n = std::clamp(int(len), 2, 400);
        const int wi = std::max(1, int(art.w * ui * std::clamp(1.4f / s.depth, 0.3f, 1.0f) + 0.5f));
        int bx0 = W, by0 = H, bx1 = 0, by1 = 0;
        for (int i = 0; i < n; ++i) {
            const float t = (i + 0.5f) / n;
            const float px = s.sx0 + (s.sx - s.sx0) * t;
            const int yy = ifloor(s.sy0 + (s.sy - s.sy0) * t);
            if (yy < 0 || yy >= H)
                continue;
            const int ar = std::min(art.h - 1, int(t * art.h));
            const int x0 = ifloor(px - wi * 0.5f);
            for (int k = 0; k < wi; ++k) {
                const int xx = x0 + k;
                if (xx < 0 || xx >= W)
                    continue;
                const uint32_t c = art.px[size_t(ar) * art.w + std::min(art.w - 1, k * art.w / wi)];
                const float a = px_a(c) / 255.0f * s.fade * 0.8f;
                if (a <= 0.01f)
                    continue;
                // not water: the drop in the colour of what falls
                blend_px(buf[size_t(xx) * H + yy], px_r(c) * tr, px_g(c) * tg, px_b(c) * tb, a);
                bx0 = std::min(bx0, xx);
                bx1 = std::max(bx1, xx + 1);
                by0 = std::min(by0, yy);
                by1 = std::max(by1, yy + 1);
            }
        }
        if (bx1 > bx0)
            grow(bx0, by0, bx1, by1);
    }
}

void Precip::mesh(const View3D &v, float W, float H, const Scene &scene, const SkyAtlas &atlas, const Media &m,
                  const Weather &w, SkyMesh &out) const {
    out.clear();
    if (drops_.empty() || W <= 0 || H <= 0)
        return;
    const float ui = std::max(1.0f, H / 540.0f);
    const uint8_t tr = w.rain_color ? uint8_t(px_r(w.rain_color)) : 255;
    const uint8_t tg = w.rain_color ? uint8_t(px_g(w.rain_color)) : 255;
    const uint8_t tb = w.rain_color ? uint8_t(px_b(w.rain_color)) : 255;
    const auto put = [&](const float *xs, const float *ys, const SkyAtlas::Rect &rc, uint8_t r, uint8_t g, uint8_t b,
                         float a) {
        // corners: top left, top right, bottom right, bottom left of the picture
        const int base = int(out.v.size());
        const float us[4] = {rc.x0, rc.x1, rc.x1, rc.x0}, vs[4] = {rc.y0, rc.y0, rc.y1, rc.y1};
        for (int i = 0; i < 4; ++i)
            out.v.push_back(SkyVertex{xs[i], ys[i], r, g, b, byte(a * 255), us[i], vs[i]});
        static const int IDX[6] = {0, 1, 2, 0, 2, 3};
        for (const int i : IDX)
            out.i.push_back(base + i);
    };
    for (const Drop &d : drops_) {
        const Image &art = d.snow ? m.snow : m.rain;
        if (!art.ok())
            continue;
        Shown s;
        if (!shown(d, v, W, H, scene, s))
            continue;
        if (d.snow) {
            if (atlas.snow.empty())
                continue;
            const SkyAtlas::Rect &rc = atlas.snow[d.seed % uint32_t(atlas.snow.size())];
            if (!rc.ok)
                continue;
            const float size = std::clamp(std::floor(0.05f * v.S / s.depth + 0.5f), 1.0f, std::floor(10 * ui));
            const float x0 = std::floor(s.sx - size * 0.5f), y0 = std::floor(s.sy - size * 0.5f);
            const float xs[4] = {x0, x0 + size, x0 + size, x0}, ys[4] = {y0, y0, y0 + size, y0 + size};
            put(xs, ys, rc, 255, 255, 255, s.fade * 0.95f);
            continue;
        }
        if (!atlas.rain.ok)
            continue;
        const float wi = std::max(1.0f, std::floor(art.w * ui * std::clamp(1.4f / s.depth, 0.3f, 1.0f) + 0.5f));
        float ex = s.sx - s.sx0, ey = s.sy - s.sy0;
        const float len = std::sqrt(ex * ex + ey * ey);
        if (len < 0.5f) {
            ex = 0;
            ey = 1;
        } else {
            ex /= len;
            ey /= len;
        }
        // across the streak, half its width each way
        const float ox = -ey * wi * 0.5f, oy = ex * wi * 0.5f;
        const float xs[4] = {s.sx0 + ox, s.sx0 - ox, s.sx - ox, s.sx + ox};
        const float ys[4] = {s.sy0 + oy, s.sy0 - oy, s.sy - oy, s.sy + oy};
        put(xs, ys, atlas.rain, tr, tg, tb, s.fade * 0.8f);
    }
}

} // namespace fpv
