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

enum { AIM_STRIKE, AIM_SHOOT, AIM_THROW, AIM_WRESTLE, AIM_NOCK, AIM_UNNOCK, AIM_INFO, AIM_INTERACT, AIM_WALK, AIM_COUNT };

// the kinds of cloud the game has, each a picture in media/sky that repeats across the sky
enum { CLOUD_CIRRUS, CLOUD_CUMULUS, CLOUD_CUMULUS_MANY, CLOUD_NIMBUS, CLOUD_ALTOSTRATUS, CLOUD_STRATUS, CLOUD_COUNT };
constexpr int MOON_PHASES = 8;   // moon_0 (new) .. moon_4 (full) .. moon_7
// the icons of the bars by the player's paperdoll (media/vitals/NAME.png), in
// the order of VitalKind in menu.inc, then drowning
constexpr int VITAL_ICONS = 18;

struct Media {
    std::string dir;
    Image cursor, crosshair, aim[AIM_COUNT], checkbox_on, checkbox_off, compass, compass_frame, wound_bruise,
        wound_cut;
    Image msg_icon[5];   // the message panel's buttons: combat, talk, other, hide, show
    Image minimap_arrow;             // the arrow on the minimap for the way you face; its top is forward
    Image compass_places;            // the castle on the button right of the compass (nearby places on or off)
    Image struck_hit, struck_miss;   // the arrow towards whoever struck at you: it wounded, it missed
    Image shot[7];       // the buttons at the pointer: quick and aimed when you shoot at a creature, throw at one, strike one; the last is Dodge
    // the sky (media/sky): the sun, the moon in each phase (0 new, 4 full), a row of square star
    // pictures, the clouds, a raindrop and a row of square snowflakes
    Image sun, moon[MOON_PHASES], stars, clouds[CLOUD_COUNT], rain, snow;
    Image vital_icon[VITAL_ICONS];   // the file's, or the built-in one where there is none
    Image button_camera;             // the camera at the top of the ring (Tab)
    std::vector<Doll> dolls;
    int files = 0;      // how many were found

    // the doll whose places fit these body part names best; null if none does.
    // slot_of gets, for each name, the index of its slot on that doll or -1.
    const Doll *bestDoll(const std::vector<std::string> &parts, std::vector<int> &slot_of) const;
};

// a PNG file as RGBA pixels; false if it cannot be read
bool load_png(const std::string &path, Image &out);

// reads the folder; never fails, missing files just stay empty
std::shared_ptr<const Media> load_media(const std::string &dir);

// ---------------------------------------------------------------------------

// the game's interface font, 8 x 12, one byte per row (bit 0 = leftmost)
struct Font {
    uint8_t rows[256][12];
    bool loaded = false;
};

// the game's font as the game has it: each character's picture at its own
// size, in colour and with its see-through edges (rows top to bottom)
struct GlyphFont {
    int w = 0, h = 0;                    // every glyph's size
    std::vector<uint32_t> glyph[256];    // empty: no picture for that character
    bool ok() const { return w > 0 && h > 0; }
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
    // the same with the game's glyphs, as the game draws text: each glyph
    // scaled to cw x ch (nearest), its colour times color, laid over what is there
    void text(const GlyphFont &font, int x, int y, const std::string &s, uint32_t color, int cw, int ch);
};

} // namespace fpv
