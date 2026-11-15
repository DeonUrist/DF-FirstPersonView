#include "textures.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <SDL_surface.h>

#include "df/enabler.h"
#include "df/global_objects.h"

using df::global::enabler;

namespace fpv {

namespace {

inline bool opaque(uint32_t c) { return px_a(c) >= 128; }

int mask_shift(uint32_t mask) {
    if (!mask)
        return -1;
    int s = 0;
    while (!(mask & 1)) {
        mask >>= 1;
        ++s;
    }
    return s;
}

// average colour of the opaque pixels; false if there are none
bool average_color(const uint32_t *px, uint32_t &out) {
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (int i = 0; i < TEXPIX; ++i) {
        const uint32_t c = px[i];
        if (opaque(c)) {
            r += px_r(c);
            g += px_g(c);
            b += px_b(c);
            ++n;
        }
    }
    if (!n)
        return false;
    out = rgb(int(r / n), int(g / n), int(b / n));
    return true;
}

void fill(Tex &t, uint32_t color) {
    for (int i = 0; i < TEXPIX; ++i)
        t.px[i] = color;
}

// ---------------------------------------------------------------------------
// patterns: brightness maps (128 = the colour itself) that repeat every tile

inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// smooth noise in 0..1 that wraps around at 32 pixels; cell is the feature size
float noise(int x, int y, int cell, uint32_t seed) {
    const int n = TEX / cell;
    const int x0 = x / cell, y0 = y / cell;
    const float fx = float(x % cell) / cell, fy = float(y % cell) / cell;
    const auto at = [&](int cx, int cy) { return float(hash2(cx % n, cy % n, seed) & 1023) / 1023.0f; };
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const float bot = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return top + (bot - top) * sy;
}

uint8_t pattern_pixel(Pattern p, int x, int y) {
    const float grain = float(hash2(x, y, 7) & 255) / 255.0f;   // per-pixel speckle
    float l = 128;
    switch (p) {
    case Pattern::Blocks: {
        // courses of blocks, every other course shifted by half a block
        const int course = y / 8, in_y = y % 8;
        const int sx = (x + (course % 2) * 8) % TEX, in_x = sx % 16;
        const float tone = float(hash2(sx / 16, course, 11) & 255) / 255.0f;
        l = 118 + 26 * tone + 8 * grain;
        if (in_y == 0 || in_x == 0) l = 74;                      // mortar
        else if (in_y == 1 || in_x == 1) l += 14;                // lit edge
        else if (in_y == 7 || in_x == 15) l -= 16;               // shaded edge
        break;
    }
    case Pattern::Smooth:
        l = 120 + 10 * noise(x, y, 16, 21) + 5 * noise(x, y, 4, 22) + 4 * grain;
        break;
    case Pattern::Slabs: {
        const int in_x = x % 16, in_y = y % 16;
        l = 124 + 10 * noise(x, y, 8, 21) + 5 * grain;
        if (in_y == 0 || in_x == 0) l = 92;
        else if (in_y == 1 || in_x == 1) l += 10;
        break;
    }
    case Pattern::Rough:
        l = 92 + 46 * noise(x, y, 8, 31) + 26 * noise(x, y, 4, 32) + 14 * grain;
        if (noise(x, y, 4, 33) > 0.82f) l -= 26;                 // cracks and hollows
        break;
    case Pattern::Soil:
        l = 104 + 30 * noise(x, y, 8, 41) + 22 * grain;
        if ((hash2(x, y, 42) & 31) == 0) l += 30;                // pebbles
        if ((hash2(x, y, 43) & 31) == 0) l -= 30;
        break;
    case Pattern::Planks: {
        const int plank = y / 8, in_y = y % 8;
        const float tone = float(hash2(plank, 0, 51) & 255) / 255.0f;
        l = 112 + 24 * tone + 14 * noise(x, plank * 8 + in_y / 2, 16, 52) + 6 * grain;
        if (in_y == 0) l = 70;
        else if (in_y == 1) l += 10;
        if ((x + plank * 11) % TEX == 0 && in_y != 0) l = 84;    // board ends
        break;
    }
    case Pattern::Bark: {
        // vertical furrows that wander a little on the way down
        const int wx = (x + int(2.0f * noise(x, y, 8, 61) + 0.5f)) & 31;
        const float streak = float(hash2(wx, 0, 62) & 255) / 255.0f;
        l = 96 + 52 * streak + 10 * grain;
        if (streak < 0.22f) l -= 22;
        break;
    }
    case Pattern::Leaves: {
        const float a = noise(x, y, 4, 71), b = noise(x, y, 8, 72);
        l = 78 + 70 * a + 30 * b + 14 * grain;
        if (a < 0.28f) l = 52 + 20 * b;                          // deep shade between the leaves
        break;
    }
    case Pattern::Grass:
        l = 106 + 26 * noise(x, y, 8, 81) + 30 * grain;
        if ((hash2(x, y / 2, 82) & 7) == 0) l += 18;             // blades
        break;
    case Pattern::Ice:
        l = 122 + 14 * noise(x, y, 16, 91) + 4 * grain;
        if (((x + 2 * y) & 31) == 5) l += 22;
        break;
    case Pattern::Grit:
        l = 122 + 12 * float(hash2(x, y, 101) & 255) / 255.0f;
        break;
    case Pattern::Concrete:
        // 2x2 pixel speckle over faint blotches
        l = 118 + 6 * noise(x, y, 8, 111) + 10 * float(hash2(x / 2, y / 2, 112) & 255) / 255.0f + 3 * grain;
        if ((hash2(x / 2, y / 2, 113) & 63) == 0) l -= 10;       // small pits
        break;
    case Pattern::Logs: {
        // four upright logs side by side: each lit in the middle and dark at its
        // sides so it looks round, a dark seam between them, bark grain running down
        const int log = x / 8;
        const float s = (float(x % 8) + 0.5f) / 8;            // across the log, 0..1
        const float round = std::sin(3.14159265f * s);
        const float tone = float(hash2(log, 0, 121) & 255) / 255.0f;
        const int wx = (x + int(1.5f * noise(x, y, 8, 122) + 0.5f)) & 31;
        const float streak = float(hash2(wx, 0, 123) & 255) / 255.0f;
        l = 52 + 88 * round + 12 * tone + 22 * streak + 8 * grain;
        if (streak < 0.18f) l -= 16;                             // furrows in the bark
        if (x % 8 == 0) l = 44;                                  // seam between logs
        break;
    }
    default:
        break;
    }
    return uint8_t(std::clamp(int(l + 0.5f), 0, 255));
}

const uint8_t *pattern_map(Pattern p) {
    static uint8_t maps[int(Pattern::Count)][TEXPIX];
    static bool made = false;
    if (!made) {
        for (int i = 0; i < int(Pattern::Count); ++i)
            for (int y = 0; y < TEX; ++y)
                for (int x = 0; x < TEX; ++x)
                    maps[i][y * TEX + x] = pattern_pixel(Pattern(i), x, y);
        made = true;
    }
    return maps[int(p)];
}

// copy opaque pixels, replace the rest with the average colour
bool flatten(const uint32_t *px, Tex &out) {
    uint32_t avg;
    if (!average_color(px, avg))
        return false;
    for (int i = 0; i < TEXPIX; ++i)
        out.px[i] = opaque(px[i]) ? (px[i] | 0xFF000000u) : avg;
    return true;
}

// alpha-blend a 32x32 sprite over part of a canvas
void blend(uint32_t *dst, int dst_stride, const uint32_t *src) {
    for (int y = 0; y < TEX; ++y) {
        uint32_t *d = dst + y * dst_stride;
        const uint32_t *s = src + y * TEX;
        for (int x = 0; x < TEX; ++x) {
            const uint32_t sc = s[x];
            const int sa = px_a(sc);
            if (sa >= 250) {
                d[x] = sc;
            } else if (sa > 8) {
                const uint32_t dc = d[x];
                const int da = px_a(dc);
                if (da < 8) {
                    d[x] = sc;
                } else {
                    const int ia = 255 - sa;
                    const int r = (px_r(sc) * sa + px_r(dc) * ia) / 255;
                    const int g = (px_g(sc) * sa + px_g(dc) * ia) / 255;
                    const int b = (px_b(sc) * sa + px_b(dc) * ia) / 255;
                    d[x] = (uint32_t(std::max(da, sa)) << 24) | (b << 16) | (g << 8) | r;
                }
            }
        }
    }
}

void water_pixels(Tex &t) {
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const float wave = std::sin((x + 3 * std::sin(y * 0.55f)) * 0.6f) + std::sin(y * 0.9f + x * 0.25f);
            const float l = 0.5f + 0.12f * wave;
            t.px[y * TEX + x] = rgb(int(30 + 40 * l), int(90 + 70 * l), int(150 + 90 * l));
        }
}

void magma_pixels(Tex &t) {
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const float n = std::sin(x * 0.55f + std::sin(y * 0.4f) * 2.2f) *
                            std::sin(y * 0.6f + std::sin(x * 0.35f) * 1.9f);
            const float l = 0.5f + 0.5f * n;
            t.px[y * TEX + x] = l < 0.25f ? rgb(90, 24, 10)
                                          : rgb(int(200 + 55 * l), int(60 + 150 * l * l), int(10 + 30 * l));
        }
}

enum : uint64_t {
    KIND_WALL = 1ull << 56,
    KIND_BARK = 2ull << 56,
    KIND_FLAT = 3ull << 56,
    KIND_FLOOR = 4ull << 56,
    KIND_MASK = 5ull << 56,
    KIND_MATERIAL = 6ull << 56,
    KIND_FLOOR_OVER = 7ull << 56,
    KIND_SPRITE_ON = 8ull << 56,
    KIND_SPRITE_CROP = 9ull << 56,
    KIND_EMBLEM = 11ull << 56,
    KIND_FRONT = 12ull << 56,
    KIND_SPRITE_CUT = 13ull << 56,
    KIND_OVER = 14ull << 56,
    KIND_ENGRAVED = 15ull << 56,
    KIND_IMAGE = 16ull << 56,
};

inline uint64_t key_of(uint64_t kind, int32_t a, int32_t b) {
    return kind | (uint64_t(uint32_t(b) & 0x0FFFFFFF) << 28) | (uint32_t(a) & 0x0FFFFFFF);
}

} // namespace

TextureStore::TextureStore() {
    texs_.emplace_back(nullptr);
}

void TextureStore::clear() {
    ++generation_;
    texs_.clear();
    texs_.emplace_back(nullptr);
    index_.clear();
    memory_icons_.clear();
    coverage_.clear();
    colors_.clear();
    is_material_.clear();
    bare_.clear();
    holed_.clear();
    fringed_.clear();
    unfringed_.clear();
    water_.clear();
    for (auto &s : specials_)
        s = 0;
}

uint16_t TextureStore::find(uint64_t key) const {
    const auto it = index_.find(key);
    return it == index_.end() ? 0 : it->second;
}

uint16_t TextureStore::add(std::unique_ptr<Tex> tex, uint64_t key) {
    if (texs_.size() >= 65535)
        return specials_[0];   // table full: fall back to the "unknown" texture
    texs_.push_back(std::move(tex));
    const uint16_t id = uint16_t(texs_.size() - 1);
    if (key)
        index_[key] = id;
    return id;
}

bool TextureStore::readSprite(int32_t texpos, uint32_t *out) {
    if (!enabler || texpos <= 0 || size_t(texpos) >= enabler->textures.raws.size())
        return false;
    auto surf = static_cast<SDL_Surface *>(enabler->textures.raws[texpos]);
    if (!surf || !surf->pixels || !surf->format || surf->w < 1 || surf->h < 1 ||
        surf->w > 512 || surf->h > 512 || surf->format->BytesPerPixel != 4)
        return false;
    const SDL_PixelFormat *fmt = surf->format;
    const int rs = mask_shift(fmt->Rmask), gs = mask_shift(fmt->Gmask), bs = mask_shift(fmt->Bmask);
    const int as = mask_shift(fmt->Amask);
    if (rs < 0 || gs < 0 || bs < 0)
        return false;
    const bool native = rs == 0 && gs == 8 && bs == 16 && as == 24;
    const auto base = static_cast<const uint8_t *>(surf->pixels);

    if (native && surf->w == TEX && surf->h == TEX) {
        for (int y = 0; y < TEX; ++y)
            std::memcpy(out + y * TEX, base + y * surf->pitch, TEX * 4);
        return true;
    }
    // other sizes are resampled (nearest); other layouts are converted
    for (int y = 0; y < TEX; ++y) {
        const auto row = reinterpret_cast<const uint32_t *>(base + ((y * surf->h) / TEX) * surf->pitch);
        for (int x = 0; x < TEX; ++x) {
            uint32_t v = row[(x * surf->w) / TEX];
            if (!native) {
                const uint32_t a = as >= 0 ? ((v >> as) & 255) : 255;
                v = (a << 24) | (((v >> bs) & 255) << 16) | (((v >> gs) & 255) << 8) | ((v >> rs) & 255);
            }
            out[y * TEX + x] = v;
        }
    }
    return true;
}

bool TextureStore::readNative(int32_t texpos, std::vector<uint32_t> &out, int &w, int &h) {
    if (!enabler || texpos <= 0 || size_t(texpos) >= enabler->textures.raws.size())
        return false;
    auto surf = static_cast<SDL_Surface *>(enabler->textures.raws[texpos]);
    if (!surf || !surf->pixels || !surf->format || surf->w < 1 || surf->h < 1 || surf->w > 512 ||
        surf->h > 512 || surf->format->BytesPerPixel != 4)
        return false;
    const SDL_PixelFormat *fmt = surf->format;
    const int rs = mask_shift(fmt->Rmask), gs = mask_shift(fmt->Gmask), bs = mask_shift(fmt->Bmask);
    const int as = mask_shift(fmt->Amask);
    if (rs < 0 || gs < 0 || bs < 0)
        return false;
    w = surf->w;
    h = surf->h;
    out.resize(size_t(w) * h);
    const auto base = static_cast<const uint8_t *>(surf->pixels);
    for (int y = 0; y < h; ++y) {
        const auto row = reinterpret_cast<const uint32_t *>(base + y * surf->pitch);
        for (int x = 0; x < w; ++x) {
            const uint32_t v = row[x];
            const uint32_t a = as >= 0 ? ((v >> as) & 255) : 255;
            out[size_t(y) * w + x] = (a << 24) | (((v >> bs) & 255) << 16) | (((v >> gs) & 255) << 8) | ((v >> rs) & 255);
        }
    }
    return true;
}

bool TextureStore::readSmooth(int32_t texpos, uint32_t *out) {
    if (!enabler || texpos <= 0 || size_t(texpos) >= enabler->textures.raws.size())
        return false;
    auto surf = static_cast<SDL_Surface *>(enabler->textures.raws[texpos]);
    if (!surf || !surf->pixels || !surf->format || surf->w <= TEX || surf->h <= TEX || surf->w > 512 ||
        surf->h > 512 || surf->format->BytesPerPixel != 4)
        return readSprite(texpos, out);
    const SDL_PixelFormat *fmt = surf->format;
    const int rs = mask_shift(fmt->Rmask), gs = mask_shift(fmt->Gmask), bs = mask_shift(fmt->Bmask);
    const int as = mask_shift(fmt->Amask);
    if (rs < 0 || gs < 0 || bs < 0)
        return false;
    const auto base = static_cast<const uint8_t *>(surf->pixels);
    for (int y = 0; y < TEX; ++y) {
        const int sy0 = y * surf->h / TEX, sy1 = std::max(sy0 + 1, (y + 1) * surf->h / TEX);
        for (int x = 0; x < TEX; ++x) {
            const int sx0 = x * surf->w / TEX, sx1 = std::max(sx0 + 1, (x + 1) * surf->w / TEX);
            // colours count by how solid they are, so see-through pixels do not darken an edge
            uint32_t r = 0, g = 0, b = 0, a = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const auto row = reinterpret_cast<const uint32_t *>(base + sy * surf->pitch);
                for (int sx = sx0; sx < sx1; ++sx) {
                    const uint32_t v = row[sx];
                    const uint32_t pa = as >= 0 ? ((v >> as) & 255) : 255;
                    r += ((v >> rs) & 255) * pa;
                    g += ((v >> gs) & 255) * pa;
                    b += ((v >> bs) & 255) * pa;
                    a += pa;
                }
            }
            const uint32_t count = uint32_t((sy1 - sy0) * (sx1 - sx0));
            out[y * TEX + x] = a ? ((a / count) << 24) | ((b / a) << 16) | ((g / a) << 8) | (r / a) : 0;
        }
    }
    return true;
}

bool TextureStore::isMemoryIcon(int32_t texpos) {
    if (!texpos)
        return false;
    const auto it = memory_icons_.find(texpos);
    if (it != memory_icons_.end())
        return it->second;
    // the placeholders are dark and colourless throughout; real sprites are not
    uint32_t raw[TEXPIX];
    bool icon = false;
    if (readSprite(texpos, raw)) {
        int seen = 0;
        icon = true;
        for (int i = 0; i < TEXPIX && icon; ++i) {
            const uint32_t c = raw[i];
            if (px_a(c) < 128)
                continue;
            ++seen;
            const int hi = std::max({px_r(c), px_g(c), px_b(c)});
            const int lo = std::min({px_r(c), px_g(c), px_b(c)});
            if (hi > 84 || hi - lo > 18)
                icon = false;
        }
        if (!seen)
            icon = false;
    }
    memory_icons_[texpos] = icon;
    return icon;
}

float TextureStore::coverage(int32_t texpos) {
    if (!texpos)
        return 0;
    const auto it = coverage_.find(texpos);
    if (it != coverage_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    float c = 0;
    if (readSprite(texpos, raw)) {
        int n = 0;
        for (int i = 0; i < TEXPIX; ++i)
            if (px_a(raw[i]) >= 128)
                ++n;
        c = float(n) / TEXPIX;
    }
    coverage_[texpos] = c;
    return c;
}

uint16_t TextureStore::special(Special which) {
    const int i = int(which);
    if (specials_[i])
        return specials_[i];
    auto t = std::make_unique<Tex>();
    switch (which) {
    case Special::Unknown: fill(*t, rgb(14, 13, 16)); break;
    case Special::Water: water_pixels(*t); break;
    case Special::Magma: magma_pixels(*t); break;
    case Special::Roof: {
        // every ceiling: plain grey concrete, whatever the floor above is made of
        const uint8_t *map = pattern_map(Pattern::Concrete);
        for (int i = 0; i < TEXPIX; ++i) {
            const int l = map[i];
            t->px[i] = rgb(132 * l / 128, 130 * l / 128, 128 * l / 128);
        }
        break;
    }
    }
    specials_[i] = add(std::move(t), 0);
    return specials_[i];
}

uint16_t TextureStore::image(uint32_t serial, const uint32_t *px, uint32_t tint) {
    const uint64_t key = key_of(KIND_IMAGE, int32_t(serial), int32_t(tint & 0x00FFFFFFu));
    if (const uint16_t id = find(key))
        return id;
    auto t = std::make_unique<Tex>();
    const int tr = tint ? px_r(tint) : 255, tg = tint ? px_g(tint) : 255, tb = tint ? px_b(tint) : 255;
    for (int i = 0; i < TEXPIX; ++i)
        t->px[i] = rgb(px_r(px[i]) * tr / 255, px_g(px[i]) * tg / 255, px_b(px[i]) * tb / 255);
    return add(std::move(t), key);
}

uint16_t TextureStore::material(Pattern pattern, uint32_t color) {
    // colours are rounded a little so that near-identical ones share a texture
    const int r = px_r(color) & ~3, g = px_g(color) & ~3, b = px_b(color) & ~3;
    const uint64_t key = key_of(KIND_MATERIAL, (r << 16) | (g << 8) | b, int32_t(pattern));
    if (const uint16_t id = find(key))
        return id;
    auto t = std::make_unique<Tex>();
    const uint8_t *map = pattern_map(pattern);
    for (int i = 0; i < TEXPIX; ++i) {
        const int l = map[i];
        t->px[i] = rgb(std::min(255, r * l / 128), std::min(255, g * l / 128), std::min(255, b * l / 128));
    }
    const uint16_t id = add(std::move(t), key);
    if (is_material_.size() <= id)
        is_material_.resize(size_t(id) + 1, false);
    is_material_[id] = true;
    return id;
}

bool TextureStore::averageColor(int32_t texpos, uint32_t &out) {
    if (!texpos)
        return false;
    const auto it = colors_.find(texpos);
    if (it != colors_.end()) {
        out = it->second;
        return out != 0;
    }
    uint32_t raw[TEXPIX];
    uint32_t avg = 0;
    if (!readSprite(texpos, raw) || !average_color(raw, avg))
        avg = 0;
    colors_[texpos] = avg;
    out = avg;
    return avg != 0;
}

uint16_t TextureStore::flatWall(int32_t bg, int32_t tp) {
    const uint64_t key = key_of(KIND_FLAT, bg, tp);
    if (const uint16_t id = find(key))
        return id;
    auto t = std::make_unique<Tex>();
    uint32_t raw[TEXPIX];
    if (!(bg && readSprite(bg, raw) && flatten(raw, *t)))
        fill(*t, rgb(52, 48, 56));
    if (tp && readSprite(tp, raw)) {
        blend(t->px, TEX, raw);
        for (int i = 0; i < TEXPIX; ++i)
            t->px[i] |= 0xFF000000u;
    }
    t->stretch = true;
    return add(std::move(t), key);
}

uint16_t TextureStore::spriteOn(uint32_t color, int32_t tp) {
    const uint64_t key = key_of(KIND_SPRITE_ON, int32_t(color & 0x00FFFFFFu), tp);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!tp || !readSprite(tp, raw)) {
        index_[key] = 0;
        return 0;
    }
    auto t = std::make_unique<Tex>();
    fill(*t, color | 0xFF000000u);
    blend(t->px, TEX, raw);
    for (int i = 0; i < TEXPIX; ++i)
        t->px[i] |= 0xFF000000u;
    t->stretch = true;
    return add(std::move(t), key);
}

uint16_t TextureStore::spriteCrop(uint32_t color, int32_t tp) {
    const uint64_t key = key_of(KIND_SPRITE_CROP, int32_t(color & 0x00FFFFFFu), tp);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!tp || !readSprite(tp, raw)) {
        index_[key] = 0;
        return 0;
    }
    int x0 = TEX, y0 = TEX, x1 = 0, y1 = 0;
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x)
            if (opaque(raw[y * TEX + x])) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x + 1);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y + 1);
            }
    if (x1 <= x0 || y1 <= y0) {
        index_[key] = 0;
        return 0;
    }
    auto t = std::make_unique<Tex>();
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const uint32_t c = raw[(y0 + y * (y1 - y0) / TEX) * TEX + x0 + x * (x1 - x0) / TEX];
            t->px[y * TEX + x] = opaque(c) ? (c | 0xFF000000u) : (color | 0xFF000000u);
        }
    t->stretch = true;
    return add(std::move(t), key);
}

uint16_t TextureStore::spriteCut(int32_t tp) {
    const uint64_t key = key_of(KIND_SPRITE_CUT, tp, 0);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!tp || !readSprite(tp, raw)) {
        index_[key] = 0;
        return 0;
    }
    // the game's sprites cast a drop shadow down and to the right in half-transparent
    // dark pixels; on a board hung in the air it is no part of the picture
    const auto solid = [](uint32_t c) { return px_a(c) >= 192; };
    int x0 = TEX, y0 = TEX, x1 = 0, y1 = 0;
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x)
            if (solid(raw[y * TEX + x])) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x + 1);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y + 1);
            }
    if (x1 <= x0 || y1 <= y0) {
        index_[key] = 0;
        return 0;
    }
    auto t = std::make_unique<Tex>();
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const uint32_t c = raw[(y0 + y * (y1 - y0) / TEX) * TEX + x0 + x * (x1 - x0) / TEX];
            t->px[y * TEX + x] = solid(c) ? (c | 0xFF000000u) : 0;   // 0 = see through
        }
    t->stretch = true;
    t->aspect = float(x1 - x0) / float(y1 - y0);
    return add(std::move(t), key);
}

uint16_t TextureStore::spriteEmblem(int32_t tp) {
    const uint64_t key = key_of(KIND_EMBLEM, tp, 0);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!tp || !readSprite(tp, raw)) {
        index_[key] = 0;
        return 0;
    }
    int x0 = TEX, y0 = TEX, x1 = 0, y1 = 0;
    std::unordered_map<uint32_t, int> counts;
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const uint32_t c = raw[y * TEX + x];
            if (!opaque(c))
                continue;
            ++counts[c | 0xFF000000u];
            x0 = std::min(x0, x);
            x1 = std::max(x1, x + 1);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y + 1);
        }
    if (x1 <= x0 || y1 <= y0) {
        index_[key] = 0;
        return 0;
    }
    uint32_t body = 0;
    int body_n = 0;
    for (const auto &[c, n] : counts)
        if (n > body_n) {
            body = c;
            body_n = n;
        }
    const auto near_body = [&](uint32_t c) {
        return std::abs(px_r(c) - px_r(body)) + std::abs(px_g(c) - px_g(body)) + std::abs(px_b(c) - px_b(body)) < 36;
    };
    // the picture: what differs from the stone, inside the slab's rim
    const int w = x1 - x0, h = y1 - y0;
    const int ix0 = x0 + w * 3 / 16, ix1 = x1 - w * 3 / 16, iy0 = y0 + h / 4, iy1 = y1 - h / 8;
    int px0 = TEX, py0 = TEX, px1 = 0, py1 = 0;
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const uint32_t c = raw[y * TEX + x];
            if (opaque(c) && !near_body(c)) {
                px0 = std::min(px0, x);
                px1 = std::max(px1, x + 1);
                py0 = std::min(py0, y);
                py1 = std::max(py1, y + 1);
            }
        }
    auto t = std::make_unique<Tex>();
    if (px1 <= px0 || py1 <= py0) {
        // no picture found: the inside of the slab
        px0 = ix0; px1 = ix1; py0 = iy0; py1 = iy1;
    }
    // the picture fills the whole face
    const int pw = std::max(1, px1 - px0), ph = std::max(1, py1 - py0);
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const uint32_t c = raw[(py0 + y * ph / TEX) * TEX + px0 + x * pw / TEX];
            t->px[y * TEX + x] = opaque(c) ? (c | 0xFF000000u) : body;
        }
    t->stretch = true;
    return add(std::move(t), key);
}

uint16_t TextureStore::engraved(uint32_t color, int32_t tp) {
    const int r = px_r(color) & ~3, g = px_g(color) & ~3, b = px_b(color) & ~3;
    const uint64_t key = key_of(KIND_ENGRAVED, (r << 16) | (g << 8) | b, tp);
    if (const uint16_t id = find(key))
        return id;
    // brightness, 128 = the stone's own colour; light comes from the upper left
    float l[TEXPIX];
    const uint8_t *smooth = pattern_map(Pattern::Smooth);
    for (int i = 0; i < TEXPIX; ++i)
        l[i] = smooth[i];
    // a groove all round, two pixels in from the edge of the face
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const int d = std::min(std::min(x, y), std::min(TEX - 1 - x, TEX - 1 - y));
            float &v = l[y * TEX + x];
            if (d == 1) v += 8;
            else if (d == 2) v = 84;
            else if (d == 3) v -= 10;
        }
    // the picture, carved in relief: only its outline and its light and dark
    // are kept, so a creature's or an item's sprite reads as a carving in the stone
    uint32_t raw[TEXPIX];
    if (tp && readSprite(tp, raw)) {
        int x0 = TEX, y0 = TEX, x1 = 0, y1 = 0;
        float lo = 1, hi = 0;
        const auto lum = [](uint32_t c) { return (0.3f * px_r(c) + 0.59f * px_g(c) + 0.11f * px_b(c)) / 255.0f; };
        for (int y = 0; y < TEX; ++y)
            for (int x = 0; x < TEX; ++x) {
                const uint32_t c = raw[y * TEX + x];
                if (!opaque(c))
                    continue;
                x0 = std::min(x0, x);
                x1 = std::max(x1, x + 1);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y + 1);
                lo = std::min(lo, lum(c));
                hi = std::max(hi, lum(c));
            }
        if (x1 > x0 && y1 > y0) {
            // as large as fits inside the groove, keeping its proportions
            const int in0 = 5, in = TEX - 2 * in0;
            const int sw = x1 - x0, sh = y1 - y0;
            const float scale = std::min(float(in) / sw, float(in) / sh);
            const int dw = std::max(1, int(sw * scale + 0.5f)), dh = std::max(1, int(sh * scale + 0.5f));
            const int ox = in0 + (in - dw) / 2, oy = in0 + (in - dh) / 2;
            bool mask[TEXPIX] = {};
            float shade[TEXPIX] = {};
            for (int y = 0; y < dh; ++y)
                for (int x = 0; x < dw; ++x) {
                    const int sx = x0 + std::min(sw - 1, int((x + 0.5f) / scale));
                    const int sy = y0 + std::min(sh - 1, int((y + 0.5f) / scale));
                    const uint32_t c = raw[sy * TEX + sx];
                    if (!opaque(c))
                        continue;
                    const int i = (oy + y) * TEX + ox + x;
                    mask[i] = true;
                    shade[i] = hi > lo + 0.05f ? (lum(c) - lo) / (hi - lo) : 0.5f;
                }
            const auto in_mask = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < TEX && y < TEX && mask[y * TEX + x];
            };
            for (int y = 4; y < TEX - 4; ++y)
                for (int x = 4; x < TEX - 4; ++x) {
                    const int i = y * TEX + x;
                    float &v = l[i];
                    if (mask[i]) {
                        // the raised figure: its own light and dark, toned down
                        v = 104 + 36 * shade[i] + 0.15f * (smooth[i] - 120);
                        if (!in_mask(x - 1, y) || !in_mask(x, y - 1)) v += 16;        // lit rim
                        else if (!in_mask(x + 1, y) || !in_mask(x, y + 1)) v -= 14;   // shaded rim
                    } else {
                        // the ground cut back around the figure, in its shadow below right
                        v -= 8;
                        if (in_mask(x - 1, y) || in_mask(x, y - 1) || in_mask(x - 1, y - 1)) v -= 16;
                    }
                }
        }
    }
    auto t = std::make_unique<Tex>();
    for (int i = 0; i < TEXPIX; ++i) {
        const float v = std::clamp(l[i], 0.0f, 255.0f);
        t->px[i] = rgb(std::min(255, int(r * v / 128)), std::min(255, int(g * v / 128)), std::min(255, int(b * v / 128)));
    }
    t->stretch = true;
    return add(std::move(t), key);
}

uint16_t TextureStore::furnitureFront(int32_t tp, int kind) {
    const uint64_t key = key_of(KIND_FRONT, tp, kind);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!tp || !readSprite(tp, raw)) {
        index_[key] = 0;
        return 0;
    }
    // the picture's main colour, and the darkest and lightest it uses a fair amount of
    std::unordered_map<uint32_t, int> counts;
    for (int i = 0; i < TEXPIX; ++i)
        if (opaque(raw[i]))
            ++counts[raw[i] | 0xFF000000u];
    const auto lum = [](uint32_t c) { return px_r(c) * 3 + px_g(c) * 6 + px_b(c); };
    uint32_t body = 0, dark = 0, light = 0;
    int body_n = 0, dark_l = 1 << 30, light_l = -1;
    for (const auto &[c, n] : counts) {
        if (n > body_n) {
            body = c;
            body_n = n;
        }
        if (n >= 6 && lum(c) < dark_l) {
            dark = c;
            dark_l = lum(c);
        }
        if (n >= 6 && lum(c) > light_l) {
            light = c;
            light_l = lum(c);
        }
    }
    if (!body_n) {
        index_[key] = 0;
        return 0;
    }
    if (!dark || dark == body)
        dark = 0;
    if (!light || light == body)
        light = 0;
    const uint16_t id = paintFront(body, dark, light, kind);
    index_[key] = id;
    return id;
}

uint16_t TextureStore::furnitureFrontIn(uint32_t color, int kind) {
    return paintFront(color | 0xFF000000u, 0, 0, kind);
}

uint16_t TextureStore::paintFront(uint32_t body, uint32_t dark, uint32_t light, int kind) {
    const uint64_t key = key_of(KIND_FRONT, int32_t(body & 0x00FFFFFFu), int32_t(((dark & 0xFFFFFF) * 31 + (light & 0xFFFFFF)) & 0x3FFFFFF) * 2 + kind) ^ (1ull << 55);
    const auto found = index_.find(key);
    if (found != index_.end())
        return found->second;
    const auto scale = [](uint32_t c, int pct) {
        return rgb(std::min(255, px_r(c) * pct / 100), std::min(255, px_g(c) * pct / 100),
                   std::min(255, px_b(c) * pct / 100));
    };
    if (!dark)
        dark = scale(body, 55);
    if (!light)
        light = scale(body, 130);
    auto t = std::make_unique<Tex>();
    uint32_t *px = t->px;
    const auto rect = [&](int x0, int y0, int x1, int y1, uint32_t c) {
        for (int y = std::max(0, y0); y < std::min(TEX, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(TEX, x1); ++x)
                px[y * TEX + x] = c;
    };
    rect(0, 0, TEX, TEX, body);
    if (kind == 0) {
        // two doors: each a panel with a dark edge, and a handle beside the gap
        // between them; the wood has a fine grit like the cabinet's sides
        rect(0, 0, TEX, 1, dark);
        rect(0, TEX - 1, TEX, TEX, dark);
        rect(0, 0, 1, TEX, dark);
        rect(TEX - 1, 0, TEX, TEX, dark);
        for (int d = 0; d < 2; ++d) {
            const int x0 = d ? 16 : 3, x1 = d ? 29 : 16;
            rect(x0, 3, x1, 29, dark);
            rect(x0 + 1, 4, x1 - 1, 28, scale(body, 92));
        }
        for (int i = 0; i < TEXPIX; ++i)
            px[i] = scale(px[i], 95 + int(hash2(i % TEX, i / TEX, 101) % 10));
        rect(12, 14, 14, 18, light);
        rect(18, 14, 20, 18, light);
    } else {
        // a frame with three shelves, each with a row of books
        rect(0, 0, TEX, TEX, dark);
        rect(1, 1, TEX - 1, TEX - 1, body);
        static const uint32_t BOOK[6] = {rgb(140, 52, 40), rgb(60, 82, 130), rgb(70, 112, 58),
                                         rgb(150, 120, 60), rgb(110, 60, 110), rgb(176, 160, 132)};
        int seed = 7;
        for (int s = 0; s < 3; ++s) {
            const int top = 3 + s * 10, bottom = top + 8;
            rect(3, top, TEX - 3, bottom, scale(dark, 80));
            for (int x = 3; x < TEX - 3;) {
                seed = (seed * 1103 + 12345) & 0x7fff;
                const int w = 2 + seed % 2, h = 5 + (seed >> 3) % 3;
                rect(x, bottom - h, std::min(x + w, TEX - 3), bottom, BOOK[(seed >> 5) % 6]);
                x += w + ((seed >> 7) % 5 == 0 ? 1 : 0);
            }
            rect(2, bottom, TEX - 2, bottom + 1, light);
        }
    }
    t->stretch = true;
    return add(std::move(t), key);
}

uint16_t TextureStore::masked(int32_t tp) {
    const uint64_t key = key_of(KIND_MASK, tp, 0);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!tp || !readSprite(tp, raw)) {
        index_[key] = 0;
        return 0;
    }
    auto t = std::make_unique<Tex>();
    for (int i = 0; i < TEXPIX; ++i)
        t->px[i] = px_a(raw[i]) >= 128 ? (raw[i] | 0xFF000000u) : 0;   // 0 = see through
    t->stretch = true;
    return add(std::move(t), key);
}

uint16_t TextureStore::floor(int32_t bg) {
    if (!bg)
        return 0;
    const uint64_t key = key_of(KIND_FLOOR, bg, 0);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    auto t = std::make_unique<Tex>();
    if (!readSprite(bg, raw) || !flatten(raw, *t)) {
        index_[key] = 0;   // remember that this sprite has nothing to show
        return 0;
    }
    return add(std::move(t), key);
}

uint16_t TextureStore::floorOver(int32_t bg, int32_t over) {
    const uint64_t key = key_of(KIND_FLOOR_OVER, bg, over);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    auto t = std::make_unique<Tex>();
    if (!bg || !readSprite(bg, raw) || !flatten(raw, *t)) {
        index_[key] = 0;
        return 0;
    }
    if (over && readSprite(over, raw)) {
        blend(t->px, TEX, raw);
        for (int i = 0; i < TEXPIX; ++i)
            t->px[i] |= 0xFF000000u;
    }
    return add(std::move(t), key);
}

uint16_t TextureStore::over(uint16_t base, int32_t tp) {
    if (!base || !tp || base >= texs_.size() || !texs_[base])
        return base;
    const uint64_t key = key_of(KIND_OVER, base, tp);
    const auto it = index_.find(key);
    if (it != index_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!readSprite(tp, raw)) {
        index_[key] = base;
        return base;
    }
    auto t = std::make_unique<Tex>(*texs_[base]);
    blend(t->px, TEX, raw);
    for (int i = 0; i < TEXPIX; ++i)
        t->px[i] |= 0xFF000000u;
    const uint16_t id = add(std::move(t), key);
    // (layers laid over layers: bare() is the texture under all of them)
    if (id != base)
        bare_[id] = bare(base);
    return id;
}

uint16_t TextureStore::water(int32_t depth_tp, const int32_t rim[8], uint8_t land, float opacity,
                             const int32_t *waves, int frames) {
    if (!waves || frames < 1)
        frames = 1;
    uint64_t key = 1469598103934665603ull;
    const auto mixin = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
    mixin(uint32_t(depth_tp));
    mixin(land);
    mixin(uint32_t(opacity * 1000.0f + 0.5f));
    for (int i = 0; i < 8; ++i)
        mixin(uint32_t(rim[i]));
    mixin(uint32_t(frames));
    if (waves)
        mixin(uint32_t(waves[0]));
    if (const auto it = water_.find(key); it != water_.end())
        return it->second;
    if (texs_.size() + frames >= 65535)
        return specials_[0];

    // the water itself: colour and how solid each pixel is (0..1)
    float cr[TEXPIX], cg[TEXPIX], cb[TEXPIX], ca[TEXPIX];
    {
        uint32_t raw[TEXPIX];
        if (!readSprite(depth_tp, raw)) {
            Tex t;
            water_pixels(t);
            std::copy(t.px, t.px + TEXPIX, raw);
        }
        for (int i = 0; i < TEXPIX; ++i) {
            cr[i] = float(px_r(raw[i]));
            cg[i] = float(px_g(raw[i]));
            cb[i] = float(px_b(raw[i]));
            ca[i] = px_a(raw[i]) / 255.0f * opacity;
        }
    }
    // a picture laid over it, as the game would lay it over the water
    const auto lay = [&](const uint32_t *src, float *r, float *g, float *b, float *a) {
        for (int i = 0; i < TEXPIX; ++i) {
            const float sa = px_a(src[i]) / 255.0f;
            if (sa <= 0)
                continue;
            const float na = sa + a[i] * (1 - sa);
            r[i] = (px_r(src[i]) * sa + r[i] * a[i] * (1 - sa)) / na;
            g[i] = (px_g(src[i]) * sa + g[i] * a[i] * (1 - sa)) / na;
            b[i] = (px_b(src[i]) * sa + b[i] * a[i] * (1 - sa)) / na;
            a[i] = na;
        }
    };
    uint32_t pic[TEXPIX];
    for (int i = 0; i < 8; ++i)
        if (rim[i] > 0 && readSprite(rim[i], pic))
            lay(pic, cr, cg, cb, ca);

    // Towards land the edge thins out in an ordered dither: fewer and fewer
    // pixels of water the nearer the edge. A 4 x 4 pattern divides the tile, so
    // the dither runs on unbroken from tile to tile.
    static const int BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    constexpr float DITHER = 7;   // pixels
    float keep[TEXPIX];
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            float d = 1e9f;
            if (land & 1) d = std::min(d, y + 0.5f);
            if (land & 2) d = std::min(d, TEX - y - 0.5f);
            if (land & 4) d = std::min(d, x + 0.5f);
            if (land & 8) d = std::min(d, TEX - x - 0.5f);
            const auto corner = [&](int bit, float cx, float cy) {
                if (land & bit)
                    d = std::min(d, std::hypot(x + 0.5f - cx, y + 0.5f - cy));
            };
            corner(16, 0, 0);
            corner(32, float(TEX), 0);
            corner(64, 0, float(TEX));
            corner(128, float(TEX), float(TEX));
            keep[y * TEX + x] = d >= DITHER || BAYER[y & 3][x & 3] < int(d / DITHER * 16.0f) ? 1.0f : 0.0f;
        }

    uint16_t first = 0;
    for (int f = 0; f < frames; ++f) {
        float r[TEXPIX], g[TEXPIX], b[TEXPIX], a[TEXPIX];
        std::copy(cr, cr + TEXPIX, r);
        std::copy(cg, cg + TEXPIX, g);
        std::copy(cb, cb + TEXPIX, b);
        std::copy(ca, ca + TEXPIX, a);
        if (waves && waves[f] > 0 && readSprite(waves[f], pic))
            lay(pic, r, g, b, a);
        auto t = std::make_unique<Tex>();
        for (int i = 0; i < TEXPIX; ++i) {
            const int al = int(a[i] * keep[i] * 255.0f + 0.5f);
            t->px[i] = al <= 0 ? 0u
                               : (uint32_t(std::min(al, 255)) << 24) | (uint32_t(std::clamp(int(b[i] + 0.5f), 0, 255)) << 16) |
                                     (uint32_t(std::clamp(int(g[i] + 0.5f), 0, 255)) << 8) |
                                     uint32_t(std::clamp(int(r[i] + 0.5f), 0, 255));
        }
        const uint16_t id = add(std::move(t), 0);
        if (f == 0)
            first = id;
    }
    water_[key] = first;
    return first;
}

namespace {
// Smooth noise over the ground in texture pixels (32 a tile), repeating every
// 8 tiles: the same everywhere a tile's holes are worked out, so they meet up
// at tile borders. 0..1, mostly 0.25..0.75.
float hole_noise(int px, int py) {
    constexpr int PERIOD = 8 * TEX;
    const auto lattice = [](int i, int j, int cells) {
        i = ((i % cells) + cells) % cells;
        j = ((j % cells) + cells) % cells;
        uint32_t h = uint32_t(i) * 374761393u + uint32_t(j) * 668265263u + uint32_t(cells) * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return float((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
    };
    const auto octave = [&](int cell) {
        const int cells = PERIOD / cell;
        const float fx = (px + 0.5f) / cell, fy = (py + 0.5f) / cell;
        const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
        float tx = fx - ix, ty = fy - iy;
        tx = tx * tx * (3 - 2 * tx);
        ty = ty * ty * (3 - 2 * ty);
        const float a = lattice(ix, iy, cells), b = lattice(ix + 1, iy, cells);
        const float c = lattice(ix, iy + 1, cells), d = lattice(ix + 1, iy + 1, cells);
        return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
    };
    return (octave(8) + 0.45f * octave(4) + 0.2f * octave(2)) / 1.65f;
}
} // namespace

uint16_t TextureStore::overHoled(uint16_t base, int32_t tp, uint32_t holes) {
    if (!(holes & 0xFFFF))
        return over(base, tp);
    if (!base || !tp || base >= texs_.size() || !texs_[base])
        return base;
    const uint64_t key = (uint64_t(base) << 48) | (uint64_t(holes & 0x3FFFFF) << 26) | (uint32_t(tp) & 0x3FFFFFF);
    const auto it = holed_.find(key);
    if (it != holed_.end())
        return it->second;
    uint32_t raw[TEXPIX];
    if (!readSprite(tp, raw)) {
        holed_[key] = base;
        return base;
    }
    const float nw = float(holes & 15) / 10, ne = float((holes >> 4) & 15) / 10;
    const float sw = float((holes >> 8) & 15) / 10, se = float((holes >> 12) & 15) / 10;
    const int ox = int((holes >> 16) & 7) * TEX, oy = int((holes >> 19) & 7) * TEX;
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            const float u = (x + 0.5f) / TEX, v = (y + 0.5f) / TEX;
            const float f = (nw + (ne - nw) * u) + ((sw + (se - sw) * u) - (nw + (ne - nw) * u)) * v;
            if (f <= 0)
                continue;
            // the more there is under it, the more of the picture is open; a
            // soft rim so the holes do not look cut out
            const float t = 0.6f * f;
            const float keep = std::clamp((hole_noise(ox + x, oy + y) - t) / 0.05f, 0.0f, 1.0f);
            uint32_t &p = raw[y * TEX + x];
            p = (p & 0x00FFFFFFu) | (uint32_t(px_a(p) * keep + 0.5f) << 24);
        }
    auto t = std::make_unique<Tex>(*texs_[base]);
    blend(t->px, TEX, raw);
    for (int i = 0; i < TEXPIX; ++i)
        t->px[i] |= 0xFF000000u;
    const uint16_t id = add(std::move(t), 0);
    if (id != base)
        bare_[id] = bare(base);
    holed_[key] = id;
    return id;
}

namespace {
uint32_t fringe_hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 374761393u + b * 668265263u + c * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// how far the ground beside reaches in across a side, at t pixels along it:
// FRINGE_END at both ends, so neighbouring tiles' lines meet whatever their seed
constexpr float FRINGE_END = 4.0f;
float fringe_depth(float t, uint32_t seed) {
    const auto noise = [&](int cell) {
        const float f = t / cell;
        const int i = int(std::floor(f));
        float k = f - i;
        k = k * k * (3 - 2 * k);
        const float a = float(fringe_hash(uint32_t(i), seed, uint32_t(cell)) & 0xFFFF) / 65535.0f;
        const float b = float(fringe_hash(uint32_t(i + 1), seed, uint32_t(cell)) & 0xFFFF) / 65535.0f;
        return a + (b - a) * k;
    };
    const float n = (noise(8) + 0.5f * noise(4) + 0.25f * noise(2)) / 1.75f;   // 0..1
    const float rise = std::sin(3.14159265f * t / TEX);
    return FRINGE_END + rise * (n * 1.5f - 0.45f) * 7.0f;
}
} // namespace

uint16_t TextureStore::fringed(uint16_t base, const uint16_t from[8], uint32_t variants) {
    if (!base || base >= texs_.size() || !texs_[base])
        return base;
    std::array<uint16_t, 10> key;
    key[0] = base;
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        key[1 + i] = from[i] < texs_.size() && texs_[from[i]] && from[i] != base ? from[i] : 0;
        any |= key[1 + i] != 0;
    }
    if (!any)
        return base;
    key[9] = uint16_t(variants & 15);
    const auto it = fringed_.find(key);
    if (it != fringed_.end())
        return it->second;

    auto t = std::make_unique<Tex>(*texs_[base]);
    // which texture each pixel takes (0: its own), and the pixels just outside a
    // line, darkened a little like the lip of grass over the ground beside it
    uint16_t take[TEXPIX] = {};
    bool lip[TEXPIX] = {};
    for (int side = 0; side < 4; ++side) {
        const uint16_t src = key[1 + side];
        if (!src)
            continue;
        const uint32_t seed = uint32_t(side) * 2 + ((variants >> side) & 1);
        for (int y = 0; y < TEX; ++y)
            for (int x = 0; x < TEX; ++x) {
                // distance in from the side, and how far along it
                const float dist = side == 0 ? y + 0.5f : side == 1 ? TEX - y - 0.5f
                                 : side == 2 ? x + 0.5f : TEX - x - 0.5f;
                if (dist > 14)
                    continue;
                const float along = side < 2 ? x + 0.5f : y + 0.5f;
                const float depth = fringe_depth(along, seed);
                const int i = y * TEX + x;
                if (dist < depth) {
                    take[i] = src;
                    continue;
                }
                // a few loose tufts beyond the line
                const float beyond = dist - depth;
                if (beyond < 2.5f && float(fringe_hash(uint32_t(x), uint32_t(y), seed + 17) & 0xFF) / 255.0f <
                                         0.3f * (1 - beyond / 2.5f)) {
                    take[i] = src;
                    continue;
                }
                if (beyond < 1.2f)
                    lip[i] = true;
            }
    }
    for (int corner = 0; corner < 4; ++corner) {
        const uint16_t src = key[5 + corner];
        if (!src)
            continue;
        const float cx = (corner & 1) ? TEX : 0, cy = (corner & 2) ? TEX : 0;
        for (int y = 0; y < TEX; ++y)
            for (int x = 0; x < TEX; ++x) {
                const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                const float r = std::sqrt(dx * dx + dy * dy);
                const int i = y * TEX + x;
                if (r < FRINGE_END + 1)
                    take[i] = src;
                else if (r < FRINGE_END + 2.2f)
                    lip[i] = true;
            }
    }
    for (int i = 0; i < TEXPIX; ++i) {
        if (take[i]) {
            t->px[i] = texs_[take[i]]->px[i] | 0xFF000000u;
        } else if (lip[i]) {
            const uint32_t p = t->px[i];
            const auto dk = [](int v) { return uint32_t(v * 205 / 256); };
            t->px[i] = (p & 0xFF000000u) | (dk(px_b(p)) << 16) | (dk(px_g(p)) << 8) | dk(px_r(p));
        }
    }
    const uint16_t id = add(std::move(t), 0);
    if (id != base)
        unfringed_[id] = base;
    fringed_[key] = id;
    return id;
}

namespace {
// a grey picture times a colour (light grey keeps the colour, white goes paler)
void tint_pixels(uint32_t *px, uint32_t color) {
    for (int i = 0; i < TEXPIX; ++i) {
        const uint32_t c = px[i];
        if (!px_a(c))
            continue;
        const int l = (px_r(c) + px_g(c) + px_b(c)) / 3;
        const auto ch = [&](int v) { return std::min(255, v * l / 190); };
        px[i] = (c & 0xFF000000u) | (uint32_t(ch(px_b(color))) << 16) | (uint32_t(ch(px_g(color))) << 8) |
                uint32_t(ch(px_r(color)));
    }
}
} // namespace

std::shared_ptr<Sprite> TextureStore::tinted(int32_t texpos, uint32_t color) {
    uint32_t raw[TEXPIX];
    if (!readSprite(texpos, raw))
        return nullptr;
    tint_pixels(raw, color);
    auto s = std::make_shared<Sprite>();
    s->w = s->h = TEX;
    s->px.assign(raw, raw + TEXPIX);
    int bx0 = TEX, by0 = TEX, bx1 = 0, by1 = 0;
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x) {
            uint32_t &c = s->px[size_t(y) * TEX + x];
            if (px_a(c) >= 150) {
                c |= 0xFF000000u;
                bx0 = std::min(bx0, x);
                bx1 = std::max(bx1, x + 1);
                by0 = std::min(by0, y);
                by1 = std::max(by1, y + 1);
            } else {
                c = 0;
            }
        }
    s->x0 = bx0; s->y0 = by0; s->x1 = bx1; s->y1 = by1;
    if (s->empty())
        return nullptr;
    return s;
}

bool TextureStore::mainColor(int32_t texpos, uint32_t &out) {
    uint32_t raw[TEXPIX];
    if (!texpos || !readSprite(texpos, raw))
        return false;
    std::unordered_map<uint32_t, int> counts;
    for (int i = 0; i < TEXPIX; ++i)
        if (opaque(raw[i]))
            ++counts[raw[i] | 0xFF000000u];
    int best = 0;
    for (const auto &[c, n] : counts)
        if (n > best) {
            best = n;
            out = c;
        }
    return best > 0;
}

std::shared_ptr<Sprite> TextureStore::pile(const int32_t *texpos, const uint32_t *tints, int count) {
    // where each small picture goes (top left), back of the heap first
    constexpr int SMALL = 18, W = 36, H = 32;
    static const int SLOTS[6][5][2] = {
        {},
        {{9, 14}},
        {{2, 12}, {16, 14}},
        {{9, 4}, {0, 14}, {18, 14}},
        {{2, 4}, {16, 4}, {0, 14}, {18, 14}},
        {{2, 2}, {16, 2}, {0, 13}, {18, 13}, {9, 14}},
    };
    uint32_t raws[5][TEXPIX];
    int n = 0;
    for (int i = 0; i < count && n < 5; ++i)
        if (readSprite(texpos[i], raws[n])) {
            if (tints && tints[i])
                tint_pixels(raws[n], tints[i]);
            ++n;
        }
    if (!n)
        return nullptr;
    auto s = std::make_shared<Sprite>();
    s->w = W;
    s->h = H;
    s->px.assign(size_t(W) * H, 0);
    for (int k = 0; k < n; ++k) {
        const int ox = SLOTS[n][k][0], oy = SLOTS[n][k][1];
        for (int y = 0; y < SMALL; ++y)
            for (int x = 0; x < SMALL; ++x) {
                const uint32_t c = raws[k][(y * TEX / SMALL) * TEX + x * TEX / SMALL];
                const int dx = ox + x, dy = oy + y;
                if (px_a(c) >= 150 && dx < W && dy < H)
                    s->px[size_t(dy) * W + dx] = c | 0xFF000000u;
            }
    }
    int bx0 = W, by0 = H, bx1 = 0, by1 = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (s->px[size_t(y) * W + x]) {
                bx0 = std::min(bx0, x);
                bx1 = std::max(bx1, x + 1);
                by0 = std::min(by0, y);
                by1 = std::max(by1, y + 1);
            }
    s->x0 = bx0; s->y0 = by0; s->x1 = bx1; s->y1 = by1;
    if (s->empty())
        return nullptr;
    return s;
}

std::shared_ptr<Sprite> TextureStore::rotated(const Sprite &src) {
    if (src.empty())
        return nullptr;
    auto s = std::make_shared<Sprite>();
    s->w = src.h;
    s->h = src.w;
    s->px.assign(size_t(s->w) * s->h, 0);
    // source (x, y) lands on (h - 1 - y, x)
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
            s->px[size_t(x) * s->w + (src.h - 1 - y)] = src.px[size_t(y) * src.w + x];
    s->x0 = src.h - src.y1;
    s->x1 = src.h - src.y0;
    s->y0 = src.x0;
    s->y1 = src.x1;
    return s;
}

std::shared_ptr<Sprite> TextureStore::snowCapped(const Sprite &src, float depth, uint32_t seed) {
    if (src.empty())
        return nullptr;
    auto s = std::make_shared<Sprite>(src);
    const int H = src.y1 - src.y0;
    const float unit = std::max(1.0f, src.h / 32.0f);   // one pixel of the game's 32 pixel picture
    const auto at = [&](int x, int y) -> uint32_t & { return s->px[size_t(y) * s->w + x]; };
    const auto solid = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < src.w && y < src.h && px_a(src.px[size_t(y) * src.w + x]) >= 128;
    };
    const auto hash = [](uint32_t v) {
        v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16;
        return v;
    };
    // The snow's lower edge: a line across the picture that rises and falls by a
    // pixel or two from one point to the next (a point every 4 pixels), so the
    // plant looks snowed on rather than cut in two colours.
    const float step = 4 * unit, amp = std::max(unit, H * 0.09f);
    const float base = src.y0 + std::max(2 * unit, H * depth);
    const auto bump = [&](int i) { return (float(hash(seed * 977u + uint32_t(i)) & 255) / 255.0f * 2 - 1) * amp; };
    // the upward-facing edges catch snow too, but only in the upper part
    const float edge_limit = src.y0 + H * std::min(0.85f, depth + 0.3f);
    const int edge_px = std::max(1, int(unit + 0.5f));

    const auto paint = [&](uint32_t &c) {
        const int lum = (px_r(c) * 3 + px_g(c) * 6 + px_b(c)) / 10;
        // the picture's shading shows through; its dark outline stays a shadowed grey
        const float f = lum < 48 ? 0.58f : 0.74f + 0.26f * std::min(1.0f, lum / 200.0f);
        c = rgb(int(234 * f), int(240 * f), std::min(255, int(250 * f + (1 - f) * 40)));
    };
    for (int x = src.x0; x < src.x1; ++x) {
        const float t = x / step;
        const int i = int(std::floor(t));
        float u = t - i;
        u = u * u * (3 - 2 * u);
        const float line = base + bump(i) + (bump(i + 1) - bump(i)) * u;
        int exposed = 0;   // pixels left to paint below an upward-facing edge
        for (int y = src.y0; y < src.y1; ++y) {
            if (!solid(x, y)) {
                exposed = 0;
                continue;
            }
            if (!solid(x, y - 1) && y < edge_limit)
                exposed = edge_px;
            // at the line itself every other pixel, a ragged edge
            const bool under = y < int(line) || (y == int(line) && ((x / edge_px + y / edge_px) & 1));
            if (under || exposed > 0)
                paint(at(x, y));
            if (exposed > 0)
                --exposed;
        }
    }
    return s;
}

std::shared_ptr<Sprite> TextureStore::composite(const Piece *pieces, int count) {
    uint32_t raws[16][TEXPIX];
    int used[16];
    int n = 0;
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    for (int i = 0; i < count && n < 16; ++i) {
        uint32_t avg;
        if (readSprite(pieces[i].texpos, raws[n]) && average_color(raws[n], avg)) {
            used[n++] = i;
            x0 = std::min(x0, pieces[i].dx);
            x1 = std::max(x1, pieces[i].dx);
            y0 = std::min(y0, pieces[i].dy);
            y1 = std::max(y1, pieces[i].dy);
        }
    }
    if (!n)
        return nullptr;
    auto s = std::make_shared<Sprite>();
    s->w = (x1 - x0 + 1) * TEX;
    s->h = (y1 - y0 + 1) * TEX;
    s->px.assign(size_t(s->w) * s->h, 0);
    for (int k = 0; k < n; ++k) {
        const Piece &p = pieces[used[k]];
        blend(&s->px[size_t(p.dy - y0) * TEX * s->w + size_t(p.dx - x0) * TEX], s->w, raws[k]);
    }
    // every pixel is either fully opaque or transparent; find the opaque box
    int bx0 = s->w, by0 = s->h, bx1 = 0, by1 = 0;
    for (int y = 0; y < s->h; ++y)
        for (int x = 0; x < s->w; ++x) {
            uint32_t &c = s->px[size_t(y) * s->w + x];
            if (px_a(c) >= 150) {
                c |= 0xFF000000u;
                bx0 = std::min(bx0, x);
                bx1 = std::max(bx1, x + 1);
                by0 = std::min(by0, y);
                by1 = std::max(by1, y + 1);
            } else {
                c = 0;
            }
        }
    s->x0 = bx0; s->y0 = by0; s->x1 = bx1; s->y1 = by1;
    if (s->empty())
        return nullptr;
    return s;
}

} // namespace fpv
