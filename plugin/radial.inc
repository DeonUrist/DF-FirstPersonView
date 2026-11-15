// ---------------------------------------------------------------------------
// The radial menu (Tab): the adventure's actions as round buttons in a ring
// around the pointer, each with the game's own icon, in place of the game's
// row of buttons at the bottom. At the bottom of the ring "..." opens a panel
// in which each action is put on or taken off the ring (kept in
// dfhack-config/fpview-radial.txt). A click on an action sends its key to the
// game from the feed hook, as if the key had been pressed. At the top, always
// there, the mod's camera (back to the normal view) and timer (time passes by
// itself; the wheel over it sets how fast), which are not on the bottom row
// while the ring is in use.

// what a toggle's picture follows (radial_toggle_state)
enum { RT_NONE, RT_SNEAK, RT_TRACKS, RT_PRONE };

struct RadialAction {
    const char *id;           // kept in the settings file
    df::interface_key key;
    const char *label;
    const char *page;         // the icon: a block of tiles of a graphics page
    int x, y, w, h;
    int state = RT_NONE;      // a toggle: which state of the game switches its picture
    int ax = -1, ay = -1;     // the picture for that state on (same page and size)
    bool lit = false;         // and while on, the button has the gold ring (the game outlines it)
};

// filled in radial_actions.inc (the game's bottom row, in its order)
#include "radial_actions.inc"
constexpr int RADIAL_COUNT = int(sizeof(RADIAL_ACTIONS) / sizeof(RADIAL_ACTIONS[0]));

const char *const RADIAL_FILE = "dfhack-config/fpview-radial.txt";

// simulation thread
struct RadialState {
    bool loaded = false;
    std::vector<bool> shown;   // per action: on the ring
} radial_cfg;

std::atomic<bool> radial_open{false}, radial_config_open{false};
std::atomic<int> radial_cx{0}, radial_cy{0};   // the ring's middle, window pixels
std::atomic<uint32_t> radial_serial{1};        // changes with what the ring holds

std::mutex radial_mutex;
std::vector<int> radial_ring;                  // the actions on the ring, in order (render thread reads a copy)
std::vector<bool> radial_shown_copy;

void radial_publish() {
    std::vector<int> ring;
    for (int i = 0; i < RADIAL_COUNT; ++i)
        if (i < int(radial_cfg.shown.size()) && radial_cfg.shown[i])
            ring.push_back(i);
    std::lock_guard<std::mutex> lock(radial_mutex);
    radial_ring = ring;
    radial_shown_copy = radial_cfg.shown;
    ++radial_serial;
}

void radial_load() {
    if (radial_cfg.loaded)
        return;
    radial_cfg.loaded = true;
    radial_cfg.shown.assign(RADIAL_COUNT, true);
    std::ifstream f(RADIAL_FILE);
    std::string id;
    int on;
    while (f >> id >> on)
        for (int i = 0; i < RADIAL_COUNT; ++i)
            if (id == RADIAL_ACTIONS[i].id)
                radial_cfg.shown[i] = on != 0;
    radial_publish();
}

void radial_save() {
    std::ofstream f(RADIAL_FILE, std::ios::trunc);
    for (int i = 0; i < RADIAL_COUNT; ++i)
        f << RADIAL_ACTIONS[i].id << " " << (radial_cfg.shown[i] ? 1 : 0) << "\n";
}

// The icons, copied on the simulation thread (again when page_epoch changes).
std::mutex radial_icon_mutex;
std::vector<std::shared_ptr<const fpv::Image>> radial_icons;   // per action, null if not readable
std::vector<std::shared_ptr<const fpv::Image>> radial_icons_on;   // a toggle's picture while on
std::shared_ptr<const fpv::Image> radial_more_icon;

// whether a toggle on the ring is on now (render thread; the flags are read as they are)
bool radial_toggle_on(int a) {
    if (a < 0 || a >= RADIAL_COUNT)
        return false;
    switch (RADIAL_ACTIONS[a].state) {
    case RT_SNEAK: return adv_sneaking.load();
    case RT_TRACKS: return adventure && adventure->tracks_flags.bits.display_latest;
    case RT_PRONE: return adv_prone.load();
    default: return false;
    }
}

// every toggle's state as bits, for the ring's picture to be made again when one changes
uint32_t radial_toggles() {
    uint32_t bits = 0;
    for (int a = 0; a < RADIAL_COUNT && a < 32; ++a)
        if (radial_toggle_on(a))
            bits |= 1u << a;
    return bits;
}

// The game's button pictures are whole buttons: a flat dark background
// (28,28,28), on the lower rows a blue-grey frame (102,132,163) inside it, and
// on the top row a gold line along the page's top (255,191,1) (read from
// interface_bits_adventure.png). On the ring only the picture itself is wanted:
// those are made see-through where they join the edge of the block, so the
// picture's own dark and blue parts (inside its black outline) stay.
void strip_button_art(fpv::Image &img) {
    if (!img.ok())
        return;
    const auto near = [](uint32_t p, int r, int g, int b) {
        return fpv::px_a(p) > 0 && std::abs(int(fpv::px_r(p)) - r) <= 6 && std::abs(int(fpv::px_g(p)) - g) <= 6 &&
               std::abs(int(fpv::px_b(p)) - b) <= 6;
    };
    const int w = img.w, h = img.h;
    const auto chrome = [&](int x, int y) {
        const uint32_t p = img.px[size_t(y) * w + x];
        if (fpv::px_a(p) == 0)
            return true;
        if (near(p, 28, 28, 28) || near(p, 102, 132, 163))
            return true;
        return y < std::max(1, h / 36 + 1) && near(p, 255, 191, 1);   // the gold line: the block's top pixels only
    };
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::vector<int> stack;
    const auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h || seen[size_t(y) * w + x] || !chrome(x, y))
            return;
        seen[size_t(y) * w + x] = 1;
        stack.push_back(y * w + x);
    };
    for (int x = 0; x < w; ++x) {
        push(x, 0);
        push(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        push(0, y);
        push(w - 1, y);
    }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        img.px[size_t(i)] = 0;
        const int x = i % w, y = i / w;
        push(x - 1, y);
        push(x + 1, y);
        push(x, y - 1);
        push(x, y + 1);
    }
}

void radial_load_icons() {
    static uint32_t loaded_for = 0;
    const uint32_t e = page_epoch();
    if (e == loaded_for)
        return;
    loaded_for = e;
    // a block of tiles side by side in one picture, the button's chrome taken off
    const auto block = [](const char *page, int bx, int by, int bw, int bh) -> std::shared_ptr<const fpv::Image> {
        std::vector<std::shared_ptr<const fpv::Image>> t;
        int tw = 0, th = 0;
        for (int j = 0; j < bh; ++j)
            for (int k = 0; k < bw; ++k) {
                std::shared_ptr<const fpv::Image> img = page_image(page, bx + k, by + j);
                if (!img || !img->ok())
                    return nullptr;
                tw = img->w;
                th = img->h;
                t.push_back(img);
            }
        auto img = std::make_shared<fpv::Image>();
        img->w = tw * bw;
        img->h = th * bh;
        img->px.assign(size_t(img->w) * img->h, 0);
        for (int j = 0; j < bh; ++j)
            for (int k = 0; k < bw; ++k) {
                const fpv::Image &s = *t[size_t(j) * bw + k];
                for (int y = 0; y < th; ++y)
                    for (int x = 0; x < tw; ++x)
                        img->px[size_t(j * th + y) * img->w + k * tw + x] = s.px[size_t(y) * s.w + x];
            }
        strip_button_art(*img);
        return img;
    };
    std::vector<std::shared_ptr<const fpv::Image>> icons(RADIAL_COUNT), icons_on(RADIAL_COUNT);
    for (int i = 0; i < RADIAL_COUNT; ++i) {
        const RadialAction &a = RADIAL_ACTIONS[i];
        if (!a.page || a.w <= 0 || a.h <= 0)
            continue;
        icons[i] = block(a.page, a.x, a.y, a.w, a.h);
        if (a.ax >= 0)
            icons_on[i] = block(a.page, a.ax, a.ay, a.w, a.h);
    }
    std::shared_ptr<const fpv::Image> more = radial_more_picture();
    if (more) {
        auto copy = std::make_shared<fpv::Image>(*more);
        strip_button_art(*copy);
        more = copy;
    }
    std::lock_guard<std::mutex> lock(radial_icon_mutex);
    radial_icons = icons;
    radial_icons_on = icons_on;
    radial_more_icon = more;
    ++radial_serial;
}

// ---------------------------------------------------------------------------
// geometry, shared by drawing and clicking

enum { RADIAL_MORE = -1, RADIAL_CAMERA = -2, RADIAL_TIMER = -3 };

struct RadialSlot {
    int action = RADIAL_MORE;   // an action, or RADIAL_MORE ("..."), RADIAL_CAMERA, RADIAL_TIMER
    int x = 0, y = 0;  // its middle
};

int radial_button_d(int ch) { return std::max(36, 3 * ch + 4); }

// the ring's slots: "..." at the bottom, the camera and the timer side by side
// at the top, the actions clockwise from the bottom round to the top and on
std::vector<RadialSlot> radial_slots(const std::vector<int> &ring, int cx, int cy, int ch) {
    std::vector<RadialSlot> s;
    const int n = int(ring.size()) + 3;
    const int top = (n - 1) / 2;     // with an odd count, it and the next lie evenly either side of the top
    std::vector<int> order(size_t(n), RADIAL_MORE);
    order[size_t(top)] = RADIAL_CAMERA;
    order[size_t(top) + 1] = RADIAL_TIMER;
    for (int i = 1, k = 0; i < n; ++i)
        if (i != top && i != top + 1)
            order[size_t(i)] = ring[size_t(k++)];
    const int d = radial_button_d(ch);
    const double r = std::max(2.2 * d, n * (d + 8) / (2 * 3.14159265));
    for (int i = 0; i < n; ++i) {
        // slot 0 at the bottom ("..."), the others clockwise from there
        const double a = 3.14159265 / 2 + 2 * 3.14159265 * i / n;
        RadialSlot k;
        k.action = order[size_t(i)];
        k.x = cx + int(std::lround(std::cos(a) * r));
        k.y = cy + int(std::lround(std::sin(a) * r));
        s.push_back(k);
    }
    return s;
}

int radial_slot_at(const std::vector<RadialSlot> &s, int px, int py, int ch) {
    const int d = radial_button_d(ch);
    for (size_t i = 0; i < s.size(); ++i) {
        const int dx = px - s[i].x, dy = py - s[i].y;
        if (dx * dx + dy * dy <= d * d / 4)
            return int(i);
    }
    return -2;
}

// the settings panel: one row per action (icon, check box, name), two columns
struct RadialPanel {
    int x = 0, y = 0, w = 0, h = 0, row = 0, col_w = 0, rows = 0;
    int close_x = 0, close_y = 0, close_w = 0, close_h = 0;
};

bool radial_panel(RadialPanel &p) {
    Cells c;
    if (!get_cells(c))
        return false;
    p.row = std::max(24, 2 * c.ch);
    p.rows = (RADIAL_COUNT + 1) / 2;
    p.col_w = 30 * c.cw;
    p.w = 2 * p.col_w + 8 * c.cw;
    p.h = 3 * c.ch + 3 * c.ch + p.rows * p.row + 5 * c.ch;
    const int sw = c.dimx * c.cw, sh = c.dimy * c.ch;
    p.x = c.ox + (sw - p.w) / 2;
    p.y = c.oy + std::max(0, (sh - p.h) / 2);
    p.close_w = 9 * c.cw;
    p.close_h = 3 * c.ch;
    p.close_x = p.w - 3 * c.cw - p.close_w;
    p.close_y = 2 * c.ch + 2;
    return true;
}

// which action's row (or -2 Close, -1 nothing) is at a window pixel
int radial_panel_at(const RadialPanel &p, int px, int py, int cw, int ch) {
    const int x = px - p.x, y = py - p.y;
    if (x >= p.close_x && y >= p.close_y && x < p.close_x + p.close_w && y < p.close_y + p.close_h)
        return -2;
    const int top = 6 * ch;
    if (y < top || x < 4 * cw)
        return -1;
    const int r = (y - top) / p.row, col = (x - 4 * cw) / p.col_w;
    if (col > 1 || r >= p.rows)
        return -1;
    const int i = col * p.rows + r;
    return i < RADIAL_COUNT ? i : -1;
}

// ---------------------------------------------------------------------------
// drawing (render thread)

UiTexture tex_radial, tex_radial_panel, tex_radial_tip;
std::string tex_radial_key, tex_radial_panel_key, tex_radial_tip_text;

// a round button: dark disc, gold ring (lighter when hovered), the icon inside
void paint_round_button(fpv::Canvas &cv, int cx, int cy, int d, const fpv::Image *icon, bool hot, bool dim) {
    const double r = d / 2.0;
    for (int y = int(cy - r) - 1; y <= int(cy + r) + 1; ++y)
        for (int x = int(cx - r) - 1; x <= int(cx + r) + 1; ++x) {
            if (x < 0 || y < 0 || x >= cv.w || y >= cv.h)
                continue;
            const double dist = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            if (dist > r + 0.5)
                continue;
            uint32_t c;
            if (dist > r - 3)
                c = hot ? UI_YELLOW : ART_GOLD;
            else if (dist > r - 5)
                c = fpv::rgb(120, 84, 24);
            else
                c = hot ? fpv::rgb(70, 54, 30) : fpv::rgb(36, 32, 38);
            const double edge = std::clamp(r + 0.5 - dist, 0.0, 1.0);
            const uint32_t a = uint32_t(edge * 255);
            cv.px[size_t(y) * cv.w + x] = (c & 0x00FFFFFFu) | (a << 24);
        }
    if (icon && icon->ok()) {
        const int s = int(d * 0.62);
        const float f = std::min(float(s) / icon->w, float(s) / icon->h);
        const int w = std::max(1, int(icon->w * f)), h = std::max(1, int(icon->h * f));
        cv.blitFit(*icon, cx - w / 2, cy - h / 2, w, h);
    }
    if (dim)
        for (int y = int(cy - r); y <= int(cy + r); ++y)
            for (int x = int(cx - r); x <= int(cx + r); ++x)
                if (x >= 0 && y >= 0 && x < cv.w && y < cv.h) {
                    uint32_t &p = cv.px[size_t(y) * cv.w + x];
                    p = (p & 0xFF000000u) | (fpv::rgb(fpv::px_r(p) / 2, fpv::px_g(p) / 2, fpv::px_b(p) / 2) & 0x00FFFFFFu);
                }
}

void draw_tip_at(SDL_Renderer *sdl, const std::string &tip, int x, int y, int cw, int ch) {
    if (tip.empty())
        return;
    if (tip != tex_radial_tip_text || !tex_radial_tip.tex) {
        fpv::Canvas cv;
        cv.resize(int(tip.size()) * cw + 12, ch + 8);
        cv.fill(0, 0, cv.w, cv.h, fpv::rgb(12, 12, 16));
        cv.frame(0, 0, cv.w, cv.h, UI_GOLD, 1);
        panel_text(cv, 6, 4, tip, UI_WHITE, cw, ch);
        if (!tex_radial_tip.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        tex_radial_tip_text = tip;
    }
    tex_radial_tip.draw(sdl, x - tex_radial_tip.w / 2, y);
}

void draw_radial(SDL_Renderer *sdl) {
    if (!radial_open && !radial_config_open)
        return;
    Cells c;
    if (!get_cells(c))
        return;
    std::vector<int> ring;
    std::vector<bool> shown;
    {
        std::lock_guard<std::mutex> lock(radial_mutex);
        ring = radial_ring;
        shown = radial_shown_copy;
    }
    std::vector<std::shared_ptr<const fpv::Image>> icons, icons_on;
    std::shared_ptr<const fpv::Image> more;
    {
        std::lock_guard<std::mutex> lock(radial_icon_mutex);
        icons = radial_icons;
        icons_on = radial_icons_on;
        more = radial_more_icon;
    }
    // a toggle on: its "on" picture where the game has one
    const auto icon_of = [&](int a) -> const fpv::Image * {
        if (a < 0 || a >= int(icons.size()))
            return nullptr;
        if (radial_toggle_on(a) && a < int(icons_on.size()) && icons_on[a])
            return icons_on[a].get();
        return icons[a] ? icons[a].get() : nullptr;
    };
    int px = -1, py = -1;
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    if (radial_config_open) {
        RadialPanel p;
        if (!radial_panel(p))
            return;
        const int hot = radial_panel_at(p, px, py, c.cw, c.ch);
        std::string key = std::to_string(hot) + "," + std::to_string(radial_serial.load()) + "," +
                          std::to_string(art_serial.load()) + "," + std::to_string(p.w);
        if (key != tex_radial_panel_key || !tex_radial_panel.tex) {
            const std::shared_ptr<const GameArt> art = get_game_art();
            fpv::Canvas cv;
            cv.resize(p.w, p.h);
            paint_game_frame(cv, art.get(), p.w, p.h, c.cw, c.ch);
            panel_text(cv, 4 * c.cw, 3 * c.ch, "Actions on the ring (Tab)", UI_WHITE, c.cw, c.ch);
            paint_game_button(cv, art.get(), p.close_x, p.close_y, p.close_w, p.close_h, "Close", hot == -2, false, c.cw,
                              c.ch);
            for (int i = 0; i < RADIAL_COUNT; ++i) {
                const int col = i / p.rows, r = i % p.rows;
                const int x = 4 * c.cw + col * p.col_w, y = 6 * c.ch + r * p.row;
                const bool on = i < int(shown.size()) && shown[i];
                if (hot == i)
                    cv.fill(x - 4, y, p.col_w - 4, p.row - 2, fpv::rgb(60, 50, 34));
                paint_track(cv, x, y + (p.row - c.ch) / 2, c.ch, true, on ? ART_GOLD : 0, hot == i);
                if (const fpv::Image *ic = icon_of(i))
                    cv.blitFit(*ic, x + c.ch + c.cw, y + 1, p.row - 2, p.row - 2);
                panel_text(cv, x + c.ch + 2 * c.cw + p.row, y + (p.row - c.ch) / 2, RADIAL_ACTIONS[i].label,
                           on ? UI_WHITE : UI_DIM, c.cw, c.ch);
            }
            panel_text(cv, 4 * c.cw, p.h - 4 * c.ch, "Click an action to put it on the ring or take it off.", UI_DIM,
                       c.cw, c.ch);
            if (!tex_radial_panel.upload(sdl, cv.px.data(), cv.w, cv.h))
                return;
            tex_radial_panel_key = key;
        }
        tex_radial_panel.draw(sdl, p.x, p.y);
        return;
    }
    const int cx = radial_cx.load(), cy = radial_cy.load();
    const std::vector<RadialSlot> slots = radial_slots(ring, cx, cy, c.ch);
    const int hot = radial_slot_at(slots, px, py, c.ch);
    const int d = radial_button_d(c.ch);
    int x0 = cx, y0 = cy, x1 = cx, y1 = cy;
    for (const RadialSlot &s : slots) {
        x0 = std::min(x0, s.x - d);
        y0 = std::min(y0, s.y - d);
        x1 = std::max(x1, s.x + d);
        y1 = std::max(y1, s.y + d);
    }
    const bool timing = realtime.load();
    const int rate = std::clamp(settings.realtime_rate.load(), 1, 99);
    const std::shared_ptr<const fpv::Media> media = get_media();
    const std::string key = std::to_string(hot) + "," + std::to_string(radial_serial.load()) + "," +
                            std::to_string(x1 - x0) + "," + std::to_string(y1 - y0) + "," + std::to_string(cx) + "," +
                            std::to_string(cy) + "," + std::to_string(timing) + "," + std::to_string(rate) + "," +
                            std::to_string(radial_toggles()) + "," +
                            std::to_string(reinterpret_cast<uintptr_t>(media.get()));
    if (key != tex_radial_key || !tex_radial.tex) {
        fpv::Canvas cv;
        cv.resize(x1 - x0, y1 - y0);
        for (size_t i = 0; i < slots.size(); ++i) {
            const RadialSlot &s = slots[i];
            const fpv::Image *icon = s.action >= 0 ? icon_of(s.action) : more.get();
            if (s.action == RADIAL_CAMERA)
                icon = media ? &media->button_camera : nullptr;
            else if (s.action == RADIAL_TIMER)
                icon = nullptr;     // the timer is its number alone
            const bool lit = s.action >= 0 && RADIAL_ACTIONS[s.action].lit && radial_toggle_on(s.action);
            paint_round_button(cv, s.x - x0, s.y - y0, d, icon,
                               hot == int(i) || lit || (s.action == RADIAL_TIMER && timing), false);
            const bool pictured = icon && icon->ok();
            if (s.action == RADIAL_MORE && !pictured)
                panel_text(cv, s.x - x0 - 3 * c.cw / 2, s.y - y0 - c.ch / 2, "...", UI_WHITE, c.cw, c.ch);
            if (s.action == RADIAL_CAMERA && !pictured)
                panel_text(cv, s.x - x0 - 3 * c.cw / 2, s.y - y0 - c.ch / 2, "Cam", UI_WHITE, c.cw, c.ch);
            if (s.action == RADIAL_TIMER) {
                // the game ticks a second, in the button's middle; yellow while time runs
                const std::string n = std::to_string(rate);
                panel_text(cv, s.x - x0 - int(n.size()) * c.cw / 2, s.y - y0 - c.ch / 2, n,
                           timing ? UI_YELLOW : UI_WHITE, c.cw, c.ch);
            }
        }
        if (!tex_radial.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        tex_radial_key = key;
    }
    tex_radial.draw(sdl, x0, y0);
    // the name of the action under the pointer, in the ring's middle
    if (hot >= 0) {
        const int a = slots[hot].action;
        const std::string tip = a == RADIAL_CAMERA ? std::string("Back to the normal view")
                                : a == RADIAL_TIMER
                                    ? std::string(timing ? "Stop time passing by itself" : "Let time pass by itself") +
                                          " (" + std::to_string(rate) + " ticks a second; wheel: faster, slower)"
                                : a < 0 ? std::string("Choose the actions on the ring")
                                        : std::string(RADIAL_ACTIONS[a].label);
        draw_tip_at(sdl, tip, cx, cy - c.ch / 2 - 4, c.cw, c.ch);
    }
}

void destroy_radial_textures() {
    tex_radial.destroy();
    tex_radial_panel.destroy();
    tex_radial_tip.destroy();
    tex_radial_key.clear();
    tex_radial_panel_key.clear();
    tex_radial_tip_text.clear();
}

// ---------------------------------------------------------------------------
// input (simulation thread, feed hook): the key to send, or NONE

bool is_tab(const std::set<df::interface_key> *input) {
    return input && (input->count(df::interface_key::CHANGETAB) || input->count(df::interface_key::STRING_A009));
}

// the camera on the ring: the view is turned off from the logic hook (as the
// command does), not from inside the game's input handling
std::atomic<bool> radial_view_off{false};

// the timer's number one wheel notch further: by 1 up to 10, then by 5 (as the timer button did)
int radial_next_rate(int rate, int dir) {
    if (dir > 0)
        rate = rate < 10 ? rate + 1 : rate + 5;
    else
        rate = rate > 10 ? std::max(10, rate - 5) : rate - 1;
    return std::clamp(rate, 1, 99);
}

// Simulation thread, each frame: the wheel over the timer on the ring sets how
// fast time passes. The settings are the Lua script's: the new number is handed
// to it in the global fpview_realtime_rate, which it stores.
void radial_wheel() {
    static int seen = 0;
    const int total = wheel_total.load();
    const int turned = total - seen;
    seen = total;
    if (!turned || !radial_open)
        return;
    Cells c;
    int px = -1, py = -1;
    if (!get_cells(c))
        return;
    // where the pointer really is: the game's own idea of it is hidden while it draws
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    std::vector<int> ring;
    {
        std::lock_guard<std::mutex> lock(radial_mutex);
        ring = radial_ring;
    }
    const std::vector<RadialSlot> slots = radial_slots(ring, radial_cx.load(), radial_cy.load(), c.ch);
    const int hit = radial_slot_at(slots, px, py, c.ch);
    if (hit < 0 || slots[hit].action != RADIAL_TIMER)
        return;
    int rate = std::clamp(settings.realtime_rate.load(), 1, 99);
    for (int n = std::abs(turned); n > 0; --n)
        rate = radial_next_rate(rate, turned > 0 ? 1 : -1);
    if (rate == settings.realtime_rate.load())
        return;
    settings.realtime_rate = rate;
    if (lua_State *L = Core::getInstance().getLuaState(true)) {
        lua_pushinteger(L, rate);
        lua_setglobal(L, "fpview_realtime_rate");
    }
}

int radial_feed_wheel = 0;   // the wheel's count at the last input while the ring was up

// 0: not ours; 1: taken; 2: taken, send `send` to the game
int radial_input(std::set<df::interface_key> *input, df::interface_key &send) {
    using namespace df::enums::interface_key;
    radial_load();
    Cells c;
    if (!get_cells(c))
        return 0;
    int px = -1, py = -1;
    pointer_pixels(px, py);
    if (radial_config_open) {
        if (enabler->mouse_lbut) {
            enabler->mouse_lbut = 0;
            enabler->mouse_lbut_down = 0;
            RadialPanel p;
            if (radial_panel(p)) {
                const int hit = radial_panel_at(p, px, py, c.cw, c.ch);
                if (hit == -2) {
                    radial_config_open = false;
                } else if (hit >= 0) {
                    radial_cfg.shown[hit] = !radial_cfg.shown[hit];
                    radial_save();
                    radial_publish();
                }
            }
            return 1;
        }
        if (enabler->mouse_rbut || (input && (input->count(LEAVESCREEN) || is_tab(input)))) {
            enabler->mouse_rbut = 0;
            enabler->mouse_rbut_down = 0;
            radial_config_open = false;
            return 1;
        }
        return 1;
    }
    if (!radial_open) {
        if (settings.radial_menu && is_tab(input) && plain_adventure_focus() && !mouse_buttons_active()) {
            radial_load_icons();
            // around the pointer, or the view's middle while mouse look holds the pointer
            if (look_on || px < 0) {
                px = c.ox + c.dimx * c.cw / 2;
                py = c.oy + c.dimy * c.ch / 2;
            }
            radial_cx = px;
            radial_cy = py;
            radial_open = true;
            radial_feed_wheel = wheel_total.load();
            ++radial_serial;
            return 1;
        }
        return 0;
    }
    std::vector<int> ring;
    {
        std::lock_guard<std::mutex> lock(radial_mutex);
        ring = radial_ring;
    }
    if (enabler->mouse_lbut) {
        enabler->mouse_lbut = 0;
        enabler->mouse_lbut_down = 0;
        const std::vector<RadialSlot> slots = radial_slots(ring, radial_cx.load(), radial_cy.load(), c.ch);
        const int hit = radial_slot_at(slots, px, py, c.ch);
        radial_open = false;
        if (hit >= 0 && slots[hit].action == RADIAL_CAMERA) {
            radial_view_off = true;
            return 1;
        }
        if (hit >= 0 && slots[hit].action == RADIAL_TIMER) {
            realtime = !realtime && active && showing;
            return 1;
        }
        if (hit >= 0 && slots[hit].action < 0) {
            radial_config_open = true;
            return 1;
        }
        if (hit >= 0) {
            send = RADIAL_ACTIONS[slots[hit].action].key;
            // the journal: the mod's panel, as the key itself gives
            if (send == A_LOG && settings.journal_panel) {
                jr_open(-1, JRef());
                return 1;
            }
            return 2;
        }
        return 1;
    }
    if (enabler->mouse_rbut || (input && (input->count(LEAVESCREEN) || is_tab(input)))) {
        enabler->mouse_rbut = 0;
        enabler->mouse_rbut_down = 0;
        radial_open = false;
        return 1;
    }
    // the wheel is the timer's (radial_wheel), and does not close the ring:
    // whatever keys the game made of a notch turned since the last input
    {
        const int total = wheel_total.load();
        const bool turned = total != radial_feed_wheel;
        radial_feed_wheel = total;
        static const df::interface_key WHEEL_KEYS[] = {
            CONTEXT_SCROLL_UP, CONTEXT_SCROLL_DOWN, CONTEXT_SCROLL_PAGEUP, CONTEXT_SCROLL_PAGEDOWN,
            SECONDSCROLL_UP, SECONDSCROLL_DOWN, SECONDSCROLL_PAGEUP, SECONDSCROLL_PAGEDOWN,
            STANDARDSCROLL_UP, STANDARDSCROLL_DOWN, STANDARDSCROLL_PAGEUP, STANDARDSCROLL_PAGEDOWN,
            ADVENTURE_LIST_SCROLL_UP, ADVENTURE_LIST_SCROLL_DOWN, ADVENTURE_LIST_SCROLL_PAGEUP,
            ADVENTURE_LIST_SCROLL_PAGEDOWN, ZOOM_IN, ZOOM_OUT};
        bool wheel_key = false;
        for (df::interface_key k : WHEEL_KEYS)
            wheel_key = wheel_key || (input && input->count(k));
        if (wheel_key || (turned && input && !enabler->mouse_lbut && !enabler->mouse_rbut))
            return 1;
    }
    // other keys go on to the game, and the ring closes
    if (input && !input->empty())
        radial_open = false;
    return 0;
}
