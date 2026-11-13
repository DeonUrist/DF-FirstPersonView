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
    case Pattern::Smooth: {
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
    if (id != base)
        bare_[id] = base;
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
