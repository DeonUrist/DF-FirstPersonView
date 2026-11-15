// ---------------------------------------------------------------------------
// The tabs over the shared panel area: Inventory, Journal, Map, in the game's
// tab art (gamestyle.inc). They stand on the top edge of the panel's body
// (panel_room keeps panel_tabs_h free above it); their lower two rows lie over
// the body's top edge, the tab's gold line on the frame's. Drawn on the render thread after the panel; clicks are found
// with the same geometry on the simulation thread.

enum { PT_INVENTORY, PT_JOURNAL, PT_MAP, PT_COUNT };
const char *const PT_NAMES[PT_COUNT] = {"Inventory", "Journal", "Map"};

struct LeafTab {
    int x = 0, y = 0, w = 0, h = 0;
};

// the tabs' boxes, window pixels, for a body whose top left is (x, y)
std::array<LeafTab, PT_COUNT> leaf_tabs(int x, int y, int cw, int ch) {
    std::array<LeafTab, PT_COUNT> t;
    int at = x + 3 * cw;
    for (int i = 0; i < PT_COUNT; ++i) {
        t[i].w = game_tab_w(PT_NAMES[i], cw);
        t[i].h = 3 * ch;
        t[i].x = at;
        t[i].y = y - ch;
        at += t[i].w;
    }
    return t;
}

int leaf_tab_at(int px, int py, int x, int y, int cw, int ch) {
    const auto t = leaf_tabs(x, y, cw, ch);
    for (int i = 0; i < PT_COUNT; ++i)
        if (px >= t[i].x + cw && px < t[i].x + t[i].w - cw && py >= t[i].y && py < t[i].y + 2 * ch)
            return i;
    return -1;
}

UiTexture tex_leaves;
std::string tex_leaves_key;

void draw_leaf_tabs(SDL_Renderer *sdl, int x, int y, int active) {
    Cells c;
    if (!get_cells(c))
        return;
    const auto t = leaf_tabs(x, y, c.cw, c.ch);
    int px = -1, py = -1;
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    const int hot = leaf_tab_at(px, py, x, y, c.cw, c.ch);
    const int w = t[PT_COUNT - 1].x + t[PT_COUNT - 1].w - t[0].x, h = t[0].h;
    const std::string key = std::to_string(active) + "," + std::to_string(hot) + "," + std::to_string(c.cw) + "," +
                            std::to_string(c.ch) + "," + std::to_string(art_serial.load());
    if (key != tex_leaves_key || !tex_leaves.tex) {
        const std::shared_ptr<const GameArt> art = get_game_art();
        fpv::Canvas cv;
        cv.resize(w, h);
        for (int i = 0; i < PT_COUNT; ++i)
            paint_game_tab(cv, art.get(), t[i].x - t[0].x, 0, PT_NAMES[i], i == active, i == hot, false, c.cw, c.ch);
        clear_outside(cv, w, h);
        if (!tex_leaves.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        tex_leaves_key = key;
    }
    tex_leaves.draw(sdl, t[0].x, t[0].y);
}

void destroy_leaf_textures() {
    tex_leaves.destroy();
    tex_leaves_key.clear();
}

// Which panel the tabs asked for (PT_*); carried out from the logic hook
// (panels_logic), since the game's inventory is opened and closed through the game.
std::atomic<int> panel_switch{-1};
int panel_switch_tries = 0;
