// ---------------------------------------------------------------------------
// The game's own window art for the mod's panels (inventory, journal, map):
// the frame with its corner knots, tabs, buttons, scrollbars, the search box,
// + and -, all tiles of the INTERFACE_BITS page (8 x 12 each, drawn at the
// interface's cell size). Positions read from graphics_interface.txt
// (2026-11-15). The tiles are copied once on the simulation thread
// (load_game_art; page_image is not for the render thread) and painted from
// the copy; without them the panels fall back to plain lines.

struct GameArt {
    std::unordered_map<int, fpv::Image> tile;  // key y * 256 + x on INTERFACE_BITS
    fpv::Image button[9];  // the inventory's Done button: INTERFACE_BITS_2 columns 3..5, rows 0..2
    // the game's zoom buttons (INTERFACE_BITS_DISPLAY_TOGGLES ZOOM_IN_ON at 0,2 and
    // ZOOM_OUT_ON at 4,2, 2 x 2 cells each), cells left to right, top to bottom
    fpv::Image zoom_in[4], zoom_out[4];
    const fpv::Image *at(int x, int y) const {
        auto it = tile.find(y * 256 + x);
        return it == tile.end() || !it->second.ok() ? nullptr : &it->second;
    }
    bool ok() const { return at(0, 0) && at(4, 0) && at(16, 6); }
};

std::mutex art_mutex;
std::shared_ptr<const GameArt> game_art;
std::atomic<uint32_t> art_serial{0};

std::shared_ptr<const GameArt> get_game_art() {
    std::lock_guard<std::mutex> lock(art_mutex);
    return game_art;
}

// simulation thread, again whenever the game's pages got other textures
void load_game_art() {
    static uint32_t loaded_for = 0;
    const uint32_t e = page_epoch();
    if (e == loaded_for)
        return;
    loaded_for = e;
    auto art = std::make_shared<GameArt>();
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 46; ++x)
            if (std::shared_ptr<const fpv::Image> img = page_image("INTERFACE_BITS", x, y))
                art->tile[y * 256 + x] = *img;
    for (int k = 0; k < 9; ++k)
        if (std::shared_ptr<const fpv::Image> img = page_image("INTERFACE_BITS_2", 3 + k % 3, k / 3))
            art->button[k] = *img;
    for (int k = 0; k < 4; ++k) {
        if (std::shared_ptr<const fpv::Image> img = page_image("INTERFACE_BITS_DISPLAY_TOGGLES", k % 2, 2 + k / 2))
            art->zoom_in[k] = *img;
        if (std::shared_ptr<const fpv::Image> img = page_image("INTERFACE_BITS_DISPLAY_TOGGLES", 4 + k % 2, 2 + k / 2))
            art->zoom_out[k] = *img;
    }
    std::lock_guard<std::mutex> lock(art_mutex);
    game_art = art;
    ++art_serial;
}

const uint32_t ART_BG = fpv::rgb(28, 28, 28), ART_GOLD = fpv::rgb(255, 191, 1), ART_TAB_TEXT = fpv::rgb(40, 26, 0);
const uint32_t ART_HEADER = fpv::rgb(215, 155, 45), ART_ROW = fpv::rgb(78, 71, 78);

// one tile at (x, y), cw x ch, nearest, its see-through parts left alone;
// cut at `clip_w` / `clip_h` pixels if those are smaller
void art_tile(fpv::Canvas &cv, const fpv::Image *t, int x, int y, int cw, int ch, int clip_w = 1 << 20,
              int clip_h = 1 << 20) {
    if (!t)
        return;
    const int w = std::min(cw, clip_w), h = std::min(ch, clip_h);
    for (int yy = 0; yy < h; ++yy) {
        const int dy = y + yy;
        if (dy < 0 || dy >= cv.h)
            continue;
        const uint32_t *src = &t->px[size_t(yy * t->h / ch) * t->w];
        for (int xx = 0; xx < w; ++xx) {
            const int dx = x + xx;
            if (dx < 0 || dx >= cv.w)
                continue;
            const uint32_t c = src[xx * t->w / cw];
            const uint32_t a = fpv::px_a(c);
            if (a == 0)
                continue;
            uint32_t &d = cv.px[size_t(dy) * cv.w + dx];
            if (a >= 250) {
                d = c | 0xFF000000u;
            } else {
                const auto mix = [&](uint32_t s, uint32_t o) { return (s * a + o * (255 - a)) / 255; };
                d = fpv::rgb(mix(fpv::px_r(c), fpv::px_r(d)), mix(fpv::px_g(c), fpv::px_g(d)), mix(fpv::px_b(c), fpv::px_b(d)));
            }
        }
    }
}

// a block of tiles (tx, ty) .. (tx + n - 1, ty + m - 1)
void art_block(fpv::Canvas &cv, const GameArt &a, int tx, int ty, int n, int m, int x, int y, int cw, int ch) {
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
            art_tile(cv, a.at(tx + i, ty + j), x + i * cw, y + j * ch, cw, ch);
}

// What lies outside the art's gold lines is dark in the tiles (the game draws
// them over its own dark window): those pixels are made see-through, flooding
// in from the canvas's edges through dark ones. `band_x` / `band_y` keep the
// flood near the edges, so a gap in a line cannot empty the whole panel.
void clear_outside(fpv::Canvas &cv, int band_x, int band_y) {
    const int w = cv.w, h = cv.h;
    if (w <= 0 || h <= 0)
        return;
    const auto dark = [&](int x, int y) {
        const uint32_t c = cv.px[size_t(y) * w + x];
        return fpv::px_a(c) == 0 || std::max({fpv::px_r(c), fpv::px_g(c), fpv::px_b(c)}) <= 44;
    };
    const auto in_band = [&](int x, int y) { return x < band_x || y < band_y || x >= w - band_x || y >= h - band_y; };
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::vector<std::pair<int, int>> todo;
    const auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h || seen[size_t(y) * w + x] || !in_band(x, y) || !dark(x, y))
            return;
        seen[size_t(y) * w + x] = 1;
        todo.push_back({x, y});
    };
    for (int x = 0; x < w; ++x) {
        push(x, 0);
        push(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        push(0, y);
        push(w - 1, y);
    }
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        cv.px[size_t(y) * w + x] = 0;
        push(x + 1, y);
        push(x - 1, y);
        push(x, y + 1);
        push(x, y - 1);
    }
}

// The window: corners of 4 x 3 cells, the top and bottom edges repeated
// across (1 x 3), the sides down (4 x 1). The double line runs about one and a
// half cells in from the outside: what is inside starts 3 cells in.
void paint_game_frame(fpv::Canvas &cv, const GameArt *a, int w, int h, int cw, int ch) {
    cv.fill(0, 0, w, h, ART_BG);
    if (!a || !a->ok()) {
        cv.frame(cw, ch, w - 2 * cw, h - 2 * ch, ART_GOLD, 1);
        cv.frame(cw + 3, ch + 3, w - 2 * cw - 6, h - 2 * ch - 6, fpv::rgb(150, 100, 30), 1);
        return;
    }
    for (int y = 3 * ch; y < h - 3 * ch; y += ch) {
        const int clip = h - 3 * ch - y;
        for (int i = 0; i < 4; ++i) {
            art_tile(cv, a->at(i, 3), i * cw, y, cw, ch, cw, clip);
            art_tile(cv, a->at(5 + i, 3), w - 4 * cw + i * cw, y, cw, ch, cw, clip);
        }
    }
    for (int x = 4 * cw; x < w - 4 * cw; x += cw) {
        const int clip = w - 4 * cw - x;
        for (int j = 0; j < 3; ++j) {
            art_tile(cv, a->at(4, j), x, j * ch, cw, ch, clip, ch);
            art_tile(cv, a->at(4, 4 + j), x, h - 3 * ch + j * ch, cw, ch, clip, ch);
        }
    }
    art_block(cv, *a, 0, 0, 4, 3, 0, 0, cw, ch);
    art_block(cv, *a, 5, 0, 4, 3, w - 4 * cw, 0, cw, ch);
    art_block(cv, *a, 0, 4, 4, 3, 0, h - 3 * ch, cw, ch);
    art_block(cv, *a, 5, 4, 4, 3, w - 4 * cw, h - 3 * ch, cw, ch);
    clear_outside(cv, 4 * cw, 3 * ch);
}

// A tab: two cells of slope, the middle repeated under the label, two of
// slope; 3 rows (`small`: the 2-row SHORT_TAB). The label is dark on gold.
int game_tab_w(const std::string &label, int cw) { return (int(label.size()) + 4) * cw; }

void paint_game_tab(fpv::Canvas &cv, const GameArt *a, int x, int y, const std::string &label, bool on, bool hot,
                    bool small, int cw, int ch) {
    const int n = int(label.size());
    const int rows = small ? 2 : 3;
    const int tx = small ? (on ? 37 : 32) : (on ? 21 : 16), ty = small ? 8 : 6;
    if (a && a->at(tx, ty)) {
        for (int j = 0; j < rows; ++j) {
            art_tile(cv, a->at(tx, ty + j), x, y + j * ch, cw, ch);
            art_tile(cv, a->at(tx + 1, ty + j), x + cw, y + j * ch, cw, ch);
            for (int i = 0; i < n; ++i)
                art_tile(cv, a->at(tx + 2, ty + j), x + (2 + i) * cw, y + j * ch, cw, ch);
            art_tile(cv, a->at(tx + 3, ty + j), x + (2 + n) * cw, y + j * ch, cw, ch);
            art_tile(cv, a->at(tx + 4, ty + j), x + (3 + n) * cw, y + j * ch, cw, ch);
        }
    } else {
        cv.fill(x + cw, y + ch / 2, (n + 2) * cw, rows * ch - ch / 2, on ? ART_GOLD : fpv::rgb(97, 65, 81));
        cv.frame(x + cw, y + ch / 2, (n + 2) * cw, rows * ch - ch / 2, ART_GOLD, 1);
    }
    const int ty_text = y + (small ? ch / 2 : ch);
    panel_text(cv, x + 2 * cw, ty_text, label, on ? ART_TAB_TEXT : hot ? UI_YELLOW : UI_WHITE, cw, ch);
}

// A 3 x 3 tile picture stretched over a box: the corners at their size (or
// cut to half the box), the edges and middle repeated.
void art_nine(fpv::Canvas &cv, const GameArt &a, int tx, int ty, int x, int y, int w, int h, int cw, int ch) {
    const int ew = std::min(cw, w / 2), eh = std::min(ch, h / 2);
    // middle
    for (int yy = eh; yy < h - eh; yy += ch)
        for (int xx = ew; xx < w - ew; xx += cw)
            art_tile(cv, a.at(tx + 1, ty + 1), x + xx, y + yy, cw, ch, w - ew - xx, h - eh - yy);
    // edges
    for (int xx = ew; xx < w - ew; xx += cw) {
        art_tile(cv, a.at(tx + 1, ty), x + xx, y, cw, ch, w - ew - xx, eh);
        // the bottom edge: its lower part
        fpv::Canvas tmp;
        tmp.resize(cw, ch);
        art_tile(tmp, a.at(tx + 1, ty + 2), 0, 0, cw, ch);
        for (int j = 0; j < eh; ++j)
            for (int i = 0; i < std::min(cw, w - ew - xx); ++i) {
                const uint32_t c = tmp.px[size_t(ch - eh + j) * cw + i];
                if (fpv::px_a(c) && y + h - eh + j < cv.h && x + xx + i < cv.w)
                    cv.px[size_t(y + h - eh + j) * cv.w + x + xx + i] = c;
            }
    }
    for (int yy = eh; yy < h - eh; yy += ch) {
        art_tile(cv, a.at(tx, ty + 1), x, y + yy, cw, ch, ew, h - eh - yy);
        fpv::Canvas tmp;
        tmp.resize(cw, ch);
        art_tile(tmp, a.at(tx + 2, ty + 1), 0, 0, cw, ch);
        for (int j = 0; j < std::min(ch, h - eh - yy); ++j)
            for (int i = 0; i < ew; ++i) {
                const uint32_t c = tmp.px[size_t(j) * cw + cw - ew + i];
                if (fpv::px_a(c) && x + w - ew + i < cv.w && y + yy + j < cv.h)
                    cv.px[size_t(y + yy + j) * cv.w + x + w - ew + i] = c;
            }
    }
    // corners, each cut to its quarter of the box
    const auto corner = [&](int cx, int cy, int dx, int dy, bool right, bool bottom) {
        fpv::Canvas tmp;
        tmp.resize(cw, ch);
        art_tile(tmp, a.at(tx + cx, ty + cy), 0, 0, cw, ch);
        for (int j = 0; j < eh; ++j)
            for (int i = 0; i < ew; ++i) {
                const uint32_t c = tmp.px[size_t(bottom ? ch - eh + j : j) * cw + (right ? cw - ew + i : i)];
                const int px = dx + i, py = dy + j;
                if (fpv::px_a(c) && px >= 0 && py >= 0 && px < cv.w && py < cv.h)
                    cv.px[size_t(py) * cv.w + px] = c | 0xFF000000u;
            }
    };
    corner(0, 0, x, y, false, false);
    corner(2, 0, x + w - ew, y, true, false);
    corner(0, 2, x, y + h - eh, false, true);
    corner(2, 2, x + w - ew, y + h - eh, true, true);
}

// a button: the game's dark rectangle, its gold-knotted one when open or hovered
// a button in the art of the game's Done button (INTERFACE_BITS_2): its first
// and last column and row at their size, the middle ones repeated; brighter
// under the pointer
void paint_game_button(fpv::Canvas &cv, const GameArt *a, int x, int y, int w, int h, const std::string &label, bool hot,
                       bool on, int cw, int ch) {
    (void)on;
    const bool art = a && std::all_of(std::begin(a->button), std::end(a->button), [](const fpv::Image &t) { return t.ok(); });
    if (art) {
        const int cols = std::max(2, w / cw), rows = std::max(2, h / ch);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const int k = (r == 0 ? 0 : r == rows - 1 ? 2 : 1) * 3 + (c == 0 ? 0 : c == cols - 1 ? 2 : 1);
                art_tile(cv, &a->button[k], x + c * cw, y + r * ch, cw, ch);
            }
        if (hot)
            for (int yy = y; yy < std::min(cv.h, y + rows * ch); ++yy)
                for (int xx = x; xx < std::min(cv.w, x + cols * cw); ++xx) {
                    uint32_t &d = cv.px[size_t(yy) * cv.w + xx];
                    if (fpv::px_a(d))
                        d = fpv::rgb(std::min(255, int(fpv::px_r(d)) + 40), std::min(255, int(fpv::px_g(d)) + 40),
                                     std::min(255, int(fpv::px_b(d)) + 40));
                }
        w = cols * cw;
        h = rows * ch;
    } else {
        cv.fill(x, y, w, h, fpv::rgb(14, 15, 20));
        cv.frame(x, y, w, h, hot ? UI_YELLOW : INV_EDGE, 1);
    }
    panel_text(cv, x + (w - int(label.size()) * cw) / 2, y + (h - ch) / 2, label, hot ? UI_YELLOW : UI_WHITE, cw, ch);
}

// the game's zoom button (2 x 2 cells), brighter under the pointer
void paint_game_zoom(fpv::Canvas &cv, const GameArt *a, bool in, bool hot, int x, int y, int cw, int ch) {
    const fpv::Image *t = a ? (in ? a->zoom_in : a->zoom_out) : nullptr;
    if (!t || !t[0].ok()) {
        cv.fill(x, y, 2 * cw, 2 * ch, fpv::rgb(20, 18, 10));
        cv.frame(x, y, 2 * cw, 2 * ch, hot ? UI_YELLOW : ART_GOLD, 1);
        panel_text(cv, x + cw / 2, y + ch / 2, in ? "+" : "-", ART_GOLD, cw, ch);
        return;
    }
    for (int k = 0; k < 4; ++k)
        art_tile(cv, &t[k], x + (k % 2) * cw, y + (k / 2) * ch, cw, ch);
    if (hot)
        for (int yy = std::max(0, y); yy < std::min(cv.h, y + 2 * ch); ++yy)
            for (int xx = std::max(0, x); xx < std::min(cv.w, x + 2 * cw); ++xx) {
                uint32_t &d = cv.px[size_t(yy) * cv.w + xx];
                if (fpv::px_a(d))
                    d = fpv::rgb(std::min(255, int(fpv::px_r(d)) + 40), std::min(255, int(fpv::px_g(d)) + 40),
                                 std::min(255, int(fpv::px_b(d)) + 40));
            }
}

// the box the game shows an item's picture in (BUTTON_PICTURE_BOX, 3 x 3 at 0,10)
void paint_picture_box(fpv::Canvas &cv, const GameArt *a, int x, int y, int w, int h, int cw, int ch) {
    if (a && a->at(0, 10) && a->at(1, 11)) {
        art_nine(cv, *a, 0, 10, x, y, w, h, cw, ch);
        return;
    }
    cv.fill(x, y, w, h, fpv::rgb(73, 56, 74));
    cv.frame(x, y, w, h, fpv::rgb(74, 74, 74), 1);
}

// the game's + or - (3 x 3 cells of picture, the sign in the middle), fitted to a box
void paint_game_sign(fpv::Canvas &cv, const GameArt *a, bool plus, bool hot, int x, int y, int s, int cw, int ch) {
    if (a && a->at(plus ? 3 : 3, plus ? 7 : 10)) {
        const int tx = plus ? (hot ? 6 : 3) : (hot ? 6 : 3), ty = plus ? 7 : 10;
        fpv::Canvas tmp;
        tmp.resize(3 * cw, 3 * ch);
        art_block(tmp, *a, tx, ty, 3, 3, 0, 0, cw, ch);
        fpv::Image img;
        img.w = tmp.w;
        img.h = tmp.h;
        img.px = tmp.px;
        cv.blitFit(img, x, y, s, s);
        return;
    }
    cv.frame(x, y, s, s, hot ? UI_YELLOW : ART_GOLD, 1);
    panel_text(cv, x + (s - cw) / 2, y + (s - ch) / 2, plus ? "+" : "-", ART_GOLD, cw, ch);
}

// a vertical scrollbar of 2 cells across: arrows at the ends, the track
// between, the handle (top cap, blank, diamond in the middle, bottom cap)
void paint_game_scrollbar(fpv::Canvas &cv, const GameArt *a, int x, int y, int h, int at, int len, int cw, int ch) {
    if (!a || !a->at(0, 13)) {
        cv.fill(x + cw - 1, y + at, 3, len, fpv::rgb(150, 120, 60));
        return;
    }
    for (int yy = ch; yy < h - ch; yy += ch)
        for (int i = 0; i < 2; ++i)
            art_tile(cv, a->at(i, 14), x + i * cw, y + yy, cw, ch, cw, h - ch - yy);
    for (int i = 0; i < 2; ++i) {
        art_tile(cv, a->at(i, 13), x + i * cw, y, cw, ch);
        art_tile(cv, a->at(i, 15), x + i * cw, y + h - ch, cw, ch);
    }
    // the handle within the track
    const int ty0 = y + ch + at, th = std::max(2 * ch, len);
    for (int yy = 0; yy < th; yy += ch) {
        const int row = yy == 0 ? 15 : yy + ch >= th ? 18 : (yy / ch == (th / ch) / 2 ? 16 : 17);
        for (int i = 0; i < 2; ++i)
            art_tile(cv, a->at(6 + i, row), x + i * cw, ty0 + yy, cw, ch, cw, th - yy);
    }
}

// the search box: left end, the field repeated, the magnifier cap (6 x 3 cells,
// fitted to h)
void paint_game_filter(fpv::Canvas &cv, const GameArt *a, int x, int y, int w, int h, int cw, int ch) {
    if (!a || !a->at(0, 16)) {
        cv.fill(x, y, w, h, fpv::rgb(8, 8, 12));
        cv.frame(x, y, w, h, INV_EDGE, 1);
        return;
    }
    // drawn at full size into a strip, then fitted to the box's height
    const int cells = std::max(6, w / cw);
    fpv::Canvas tmp;
    tmp.resize(cells * cw, 3 * ch);
    for (int j = 0; j < 3; ++j) {
        art_tile(tmp, a->at(0, 16 + j), 0, j * ch, cw, ch);
        for (int i = 1; i < cells - 4; ++i)
            art_tile(tmp, a->at(1, 16 + j), i * cw, j * ch, cw, ch);
        for (int i = 0; i < 4; ++i)
            art_tile(tmp, a->at(2 + i, 16 + j), (cells - 4 + i) * cw, j * ch, cw, ch);
    }
    fpv::Image img;
    img.w = tmp.w;
    img.h = tmp.h;
    img.px = tmp.px;
    cv.blitFit(img, x, y, w, h);
}
