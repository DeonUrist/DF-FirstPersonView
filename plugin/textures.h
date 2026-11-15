// Reads the game's own tile sprites out of memory and turns them into
// textures for the first-person renderer.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "common.h"

namespace fpv {

// an opaque 32x32 texture for wall faces and floors
struct Tex {
    uint32_t px[TEXPIX];
    bool stretch = false;   // one sprite per wall face instead of repeating like masonry
    float aspect = 1;       // width / height of the picture it shows (spriteCut)
};

// an image for a billboard; pixels with alpha 0 are transparent
struct Sprite {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    // bounding box of the opaque pixels (x1/y1 exclusive)
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

enum class Special { Unknown, Water, Magma, Roof };

// Pre-made surface patterns. A wall or floor is one of these, tinted with the
// colour of what it is made of.
// Grit: one flat tone with a fine speckle (furniture). Concrete: grey ceilings.
// Smooth: smoothed natural stone, plain with faint blotches. Slabs: a built floor
// of square slabs.
enum class Pattern : uint8_t { Blocks, Smooth, Rough, Soil, Planks, Bark, Leaves, Grass, Ice, Grit, Concrete, Logs, Slabs, Count };

// one piece of a billboard: a game texture placed at a tile offset
struct Piece {
    int32_t texpos;
    int dx, dy;
};

class TextureStore {
public:
    TextureStore();

    // all return a texture id; 0 means "nothing"
    uint16_t material(Pattern pattern, uint32_t color);   // a pattern in a colour
    bool isMaterial(uint16_t id) const { return id < is_material_.size() && is_material_[id]; }
    uint16_t flatWall(int32_t bg, int32_t tp);    // tile-filling building (door, floodgate)
    uint16_t masked(int32_t tp);                  // a sprite with holes you see through (open door frame)
    uint16_t spriteOn(uint32_t color, int32_t tp);   // a sprite over a plain colour
    // only the drawn part of a sprite (its empty margins cut off), over a plain colour
    uint16_t spriteCrop(uint32_t color, int32_t tp);
    // the same, its empty parts see-through (px 0); aspect is the drawn part's
    uint16_t spriteCut(int32_t tp);
    // only the picture in the middle of a sprite (the engraving on a slab): the
    // parts drawn in the sprite's main colour, and its rim, become plain stone
    uint16_t spriteEmblem(int32_t tp);
    // an engraved wall face: smoothed stone in a colour with a carved frame and,
    // carved in relief inside it, the picture tp (its shape only, not its colours)
    uint16_t engraved(uint32_t color, int32_t tp);
    // a painted front for a piece of furniture, in the colours of the game's
    // picture of it: 0 = a cabinet's two doors, 1 = a bookcase's shelves of books
    uint16_t furnitureFront(int32_t tp, int kind);
    // the same, painted in a colour (no picture seen yet)
    uint16_t furnitureFrontIn(uint32_t color, int kind);
    // a picture of the player's (a model's texture, already 32 x 32) multiplied by a
    // colour (0: as it is); serial tells the pictures apart
    uint16_t image(uint32_t serial, const uint32_t *px, uint32_t tint);
    uint16_t floor(int32_t bg);
    uint16_t floorOver(int32_t bg, int32_t over);  // a floor with something flat lying on it
    // a texture with a game picture laid over it (snow or blood on the ground)
    uint16_t over(uint16_t base, int32_t tp);
    // the same with holes in the picture where what lies under it shows through
    // (snow over mud). holes packs the hole density at the four corners (4 bits
    // each, 0..10: NW, NE, SW, SE) and the tile's place in the noise pattern
    // (x & 7 in bits 16..18, y & 7 in 19..21), so holes run on across tile
    // borders; 0 = no holes, as over()
    uint16_t overHoled(uint16_t base, int32_t tp, uint32_t holes);
    // A floor with the ground of the tiles around it reaching into it along an
    // uneven line (grass spreading onto bare soil). from[0..3]: the texture that
    // reaches in across the north, south, west, east side, from[4..7] at the NW,
    // NE, SW, SE corner only; 0 = nothing. Bit i of variants picks one of two
    // lines for side i. Every line is equally deep at a tile's corners, so tiles
    // side by side along one border join up whatever their lines.
    uint16_t fringed(uint16_t base, const uint16_t from[8], uint32_t variants);
    // the floor a fringed() texture was made from, else id itself
    uint16_t unfringed(uint16_t id) const {
        const auto it = unfringed_.find(id);
        return it == unfringed_.end() ? id : it->second;
    }
    // the texture something was laid over by over() (under every layer), else id itself
    uint16_t bare(uint16_t id) const {
        const auto it = bare_.find(id);
        return it == bare_.end() ? id : it->second;
    }
    // Water (or magma) seen from above, as the game draws it: its picture for the
    // depth (the LIQUIDS page), see-through, `opacity` times as solid as that
    // picture, with the game's light rim pieces laid over it (rim[8]: N, S, W, E,
    // NW, NE, SW, SE; 0: none). Along the sides and corners in `land` (bits in the
    // same order) an edge that thins out in a dither towards land. With waves (frames pictures of
    // the game's FLOWS pages, one per step of their animation) as many textures
    // with consecutive ids, one per frame; returns the first. Pixels hold their
    // alpha (0: nothing there).
    uint16_t water(int32_t depth_tp, const int32_t rim[8], uint8_t land, float opacity, const int32_t *waves,
                   int frames);
    uint16_t special(Special which);
    // id of a special texture if it has been made already, else 0 (safe from any thread
    // that holds the scene lock)
    uint16_t existing(Special which) const { return specials_[int(which)]; }

    const Tex &get(uint16_t id) const { return *texs_[id]; }
    size_t size() const { return texs_.size(); }

    // forget everything (texture numbers are reassigned when a world loads)
    void clear();
    // goes up whenever everything was forgotten: ids from before mean nothing now
    uint32_t generation() const { return generation_; }

    // copy a game texture into out[TEXPIX] (resampled to 32x32); false if unavailable
    static bool readSprite(int32_t texpos, uint32_t *out);
    // the same for a larger picture (a portrait): every pixel the mean of the area it stands for
    static bool readSmooth(int32_t texpos, uint32_t *out);
    // a game texture at its own size (RGBA, rows top to bottom); false if unavailable
    static bool readNative(int32_t texpos, std::vector<uint32_t> &out, int &w, int &h);

    // layer several game textures into one billboard image; null if nothing is visible
    static std::shared_ptr<Sprite> composite(const Piece *pieces, int count);

    // a heap of up to five game textures, each drawn small; null if nothing is visible
    // tints: 0, or a colour a grey picture (coins) is drawn in
    static std::shared_ptr<Sprite> pile(const int32_t *texpos, const uint32_t *tints, int count);
    // a grey game picture in a colour, as a billboard image
    static std::shared_ptr<Sprite> tinted(int32_t texpos, uint32_t color);
    // a billboard image turned a quarter turn clockwise (its top to the right)
    static std::shared_ptr<Sprite> rotated(const Sprite &src);
    // a plant with snow on it: its top part down to an uneven line (depth = share
    // of its height, 0..1) and the upward-facing edges of its upper half painted
    // in snow, keeping the picture's light and dark; seed varies the line
    static std::shared_ptr<Sprite> snowCapped(const Sprite &src, float depth, uint32_t seed);
    // the colour most of a game texture is drawn in; false if it has none
    bool mainColor(int32_t texpos, uint32_t &out);

    // Out of the adventurer's sight the game draws dark grey placeholder icons
    // (a dot for floor, a square for a wall or door, a silhouette for a
    // creature ...) instead of the real sprites. True if texpos is one of them.
    bool isMemoryIcon(int32_t texpos);

    // fraction of a game texture that is opaque, 0..1
    float coverage(int32_t texpos);

    // average colour of a game texture's opaque pixels; false if it has none
    bool averageColor(int32_t texpos, uint32_t &out);

private:
    uint16_t add(std::unique_ptr<Tex> tex, uint64_t key);
    uint16_t paintFront(uint32_t body, uint32_t dark, uint32_t light, int kind);
    uint16_t find(uint64_t key) const;

    std::vector<std::unique_ptr<Tex>> texs_;          // index 0 is unused
    std::unordered_map<uint64_t, uint16_t> index_;
    std::unordered_map<int32_t, bool> memory_icons_;
    std::unordered_map<int32_t, float> coverage_;
    std::unordered_map<int32_t, uint32_t> colors_;    // 0 = no colour
    std::vector<bool> is_material_;                   // per texture id
    std::unordered_map<uint16_t, uint16_t> bare_;     // over() texture -> its base
    std::unordered_map<uint64_t, uint16_t> holed_;    // overHoled() textures by base, holes, picture
    struct FringeHash {
        size_t operator()(const std::array<uint16_t, 10> &k) const {
            uint64_t h = 1469598103934665603ull;
            for (uint16_t v : k)
                h = (h ^ v) * 1099511628211ull;
            return size_t(h);
        }
    };
    // fringed() textures by base, what reaches in, lines
    std::unordered_map<std::array<uint16_t, 10>, uint16_t, FringeHash> fringed_;
    std::unordered_map<uint16_t, uint16_t> unfringed_;   // fringed() texture -> its floor
    std::unordered_map<uint64_t, uint16_t> water_;       // water() textures by what they were made from
    uint16_t specials_[4] = {0, 0, 0, 0};
    uint32_t generation_ = 0;
};

} // namespace fpv
