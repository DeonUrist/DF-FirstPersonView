// Art loaded from the mod's media folder, and a small pixel canvas for the
// menus that are drawn with it.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common.h"

namespace fpv {

// pixels row by row, 0xAABBGGRR like everything else here
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    bool ok() const { return w > 0 && h > 0; }
};

// a place on a paperdoll: the body part names that go there
struct DollSlot {
    std::vector<std::string> names;
    int x = 0, y = 0;
};

struct Doll {
    std::string name;
    Image img;
    std::vector<DollSlot> slots;
};

enum { AIM_STRIKE, AIM_SHOOT, AIM_THROW, AIM_WRESTLE, AIM_COUNT };

struct Media {
    std::string dir;
    Image cursor, crosshair, aim[AIM_COUNT], checkbox_on, checkbox_off, compass, compass_frame, wound_bruise,
        wound_cut;
    std::vector<Doll> dolls;
    int files = 0;      // how many were found

    // the doll whose places fit these body part names best; null if none does.
    // slot_of gets, for each name, the index of its slot on that doll or -1.
    const Doll *bestDoll(const std::vector<std::string> &parts, std::vector<int> &slot_of) const;
};

// reads the folder; never fails, missing files just stay empty
std::shared_ptr<const Media> load_media(const std::string &dir);

// ---------------------------------------------------------------------------

// the game's interface font, 8 x 12, one byte per row (bit 0 = leftmost)
struct Font {
    uint8_t rows[256][12];
    bool loaded = false;
};

struct Canvas {
    int w = 0, h = 0;
    std::vector<uint32_t> px;

    void resize(int w_, int h_);    // and clear to transparent
    void fill(int x, int y, int w_, int h_, uint32_t color);
    void frame(int x, int y, int w_, int h_, uint32_t color, int thick = 1);
    // nearest-neighbour, alpha as a mask (>= 128 is drawn)
    void blit(const Image &img, int x, int y, int scale);
    void blitFit(const Image &img, int x, int y, int w_, int h_);
    // one line of text; each character takes cw x ch pixels
    void text(const Font &font, int x, int y, const std::string &s, uint32_t color, int cw, int ch);
};

} // namespace fpv
