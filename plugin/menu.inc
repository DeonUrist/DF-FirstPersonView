// Part of fpview.cpp (included inside its anonymous namespace).
//
// Intents: what a Ctrl-click does (strike, shoot, throw, wrestle).
// Combat menus: the game's "aim for" and "attack with" lists, drawn again as
// a paperdoll and compact lists. The game's own dialog stays open underneath
// (emptied of its cells, so it is not seen) and does the actual work: what is
// clicked here is clicked there.

// ---------------------------------------------------------------------------
// intents

enum Intent { INTENT_STRIKE, INTENT_SHOOT, INTENT_THROW, INTENT_WRESTLE, INTENT_COUNT };
const char *const INTENT_NAMES[INTENT_COUNT] = {"Strike", "Shoot", "Throw", "Wrestle"};
const char *const INTENT_FILE = "dfhack-config/fpview-intent.txt";

std::atomic<int> intent{INTENT_STRIKE};   // remembered between sessions
std::atomic<bool> can_shoot{false};       // a ranged weapon is in hand
std::atomic<bool> swallow_keys{false};    // Ctrl+R is down: that key press is ours, not the game's

// a few words shown briefly above the crosshair
std::mutex flash_mutex;
std::string flash_text;
std::atomic<uint32_t> flash_serial{0};

void flash(const std::string &text) {
    {
        std::lock_guard<std::mutex> lock(flash_mutex);
        flash_text = text;
    }
    ++flash_serial;
}

// shooting needs something to shoot with
int effective_intent() {
    const int i = intent.load();
    return (i == INTENT_SHOOT && !can_shoot) ? int(INTENT_STRIKE) : i;
}

void load_intent() {
    std::ifstream in(INTENT_FILE);
    std::string word;
    if (in >> word)
        for (int i = 0; i < INTENT_COUNT; ++i)
            if (word == INTENT_NAMES[i])
                intent = i;
}

void cycle_intent() {
    int next = intent.load();
    for (int i = 0; i < INTENT_COUNT; ++i) {
        next = (next + 1) % INTENT_COUNT;
        if (next != INTENT_SHOOT || can_shoot)
            break;
    }
    intent = next;
    std::ofstream out(INTENT_FILE);
    out << INTENT_NAMES[next] << "\n";
    flash(INTENT_NAMES[next]);
}

// ---------------------------------------------------------------------------
// art and font

std::mutex media_mutex;
std::shared_ptr<const fpv::Media> media_ptr;
std::atomic<uint32_t> media_serial{0};

std::shared_ptr<const fpv::Media> get_media() {
    std::lock_guard<std::mutex> lock(media_mutex);
    return media_ptr;
}

int set_media(const std::string &dir) {
    std::shared_ptr<const fpv::Media> m = fpv::load_media(dir);
    std::lock_guard<std::mutex> lock(media_mutex);
    media_ptr = m;
    ++media_serial;
    return m->files;
}

fpv::Font ui_font;
std::atomic<bool> ui_font_ready{false};

// simulation thread: copy the game's interface font
void load_ui_font() {
    if (ui_font_ready || !init)
        return;
    int found = 0;
    for (int ch = 0; ch < 256; ++ch) {
        std::memset(ui_font.rows[ch], 0, 12);
        int32_t texpos = int32_t(init->font.small_font_texpos[ch]);
        if (!texpos)
            texpos = int32_t(init->font.large_font_texpos[ch]);
        uint32_t raw[fpv::TEXPIX];
        if (!texpos || !fpv::TextureStore::readSprite(texpos, raw))
            continue;
        ++found;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 8; ++x) {
                const uint32_t c = raw[(y * fpv::TEX / 12 + 1) * fpv::TEX + x * 4 + 2];
                const int r = fpv::px_r(c), g = fpv::px_g(c), b = fpv::px_b(c);
                if (fpv::px_a(c) >= 128 && !(r > 200 && b > 200 && g < 80) && std::max({r, g, b}) > 90)
                    ui_font.rows[ch][y] |= uint8_t(1u << x);
            }
    }
    if (found > 64)
        ui_font_ready = true;
}

// ---------------------------------------------------------------------------
// the menu: what it holds

struct MenuEntry {
    std::string name, d1, d2;       // "left hand", "Easier strike", "can't quite connect"
    uint32_t cn = 0, c1 = 0, c2 = 0;
    std::string mark;               // what the game puts at the end of the row: "+", "!"
    int slot = -1;                  // its place on the paperdoll; -1 = in the list
};

struct MenuOption {
    std::string label;              // "Quick attack"
    bool on = false;
};

struct MenuModel {
    int kind = 0;                   // 1 = aim for a body part, 2 = attack with
    std::string title;
    std::vector<MenuEntry> entries;
    std::vector<MenuOption> options;
    std::shared_ptr<const fpv::Media> media;
    const fpv::Doll *doll = nullptr;
    int bottom_row = 0;             // interface row the game's dialog ends on
    uint32_t flags = 0;
};

std::mutex menu_mutex;
std::shared_ptr<const MenuModel> menu_model;    // null while there is no menu of ours

std::shared_ptr<const MenuModel> get_menu() {
    std::lock_guard<std::mutex> lock(menu_mutex);
    return menu_model;
}

void set_menu(std::shared_ptr<const MenuModel> m) {
    std::lock_guard<std::mutex> lock(menu_mutex);
    menu_model = std::move(m);
}

// "Tricky strike" -> T, "fairly solid" -> F, "can't quite connect" -> Q, "can only graze" -> G
char code_letter(const std::string &text) {
    std::string t = text;
    if (t.rfind("can't ", 0) == 0)
        t.erase(0, 6);
    else if (t.rfind("can only ", 0) == 0)
        t.erase(0, 9);
    if (t.empty())
        return ' ';
    const char c = t[0];
    return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
}

// ---------------------------------------------------------------------------
// the menu: where everything goes (window pixels), shared by drawing and clicking

struct Cells {
    int ox = 0, oy = 0, cw = 8, ch = 12, dimx = 0, dimy = 0;
};

bool get_cells(Cells &c) {
    if (!gps || !enabler || !enabler->renderer)
        return false;
    const auto r = static_cast<df::renderer_2d_base *>(enabler->renderer);
    if (r->dispx_z <= 0 || r->dispy_z <= 0)
        return false;
    c.ox = r->origin_x;
    c.oy = r->origin_y;
    c.cw = r->dispx_z;
    c.ch = r->dispy_z;
    c.dimx = gps->dimx;
    c.dimy = gps->dimy;
    return true;
}

enum { ITEM_DOLL, ITEM_LIST, ITEM_OPTION, ITEM_CANCEL };

struct MenuItem {
    int kind = 0, index = 0;
    int x = 0, y = 0, w = 0, h = 0;     // relative to the panel
};

struct MenuLayout {
    int x = 0, y = 0, w = 0, h = 0;     // the panel, in window pixels
    int cw = 8, ch = 12;
    int doll_x = 0, doll_y = 0, doll_scale = 1;
    int status_y = 0;
    std::vector<MenuItem> items;

    int at(int px, int py) const {
        px -= x;
        py -= y;
        for (size_t i = 0; i < items.size(); ++i) {
            const MenuItem &it = items[i];
            if (px >= it.x && py >= it.y && px < it.x + it.w && py < it.y + it.h)
                return int(i);
        }
        return -1;
    }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

bool layout_menu(const MenuModel &m, MenuLayout &L) {
    Cells c;
    if (!get_cells(c) || c.dimx < 70)
        return false;
    L.cw = c.cw;
    L.ch = c.ch;
    L.items.clear();
    const int n = int(m.entries.size());
    const int body_top = 3;                         // rows: edge, title, gap
    const int widest = c.dimx - 6;
    int body_rows = 3;
    int PW = 0;                                     // panel width in cells
    if (m.kind == 1) {
        const int max_rows = std::max(8, std::min(27, c.dimy - 16));
        const fpv::Doll *doll = m.doll;
        int doll_cols = 0, doll_h = 0;
        if (doll) {
            L.doll_scale = std::max(1, std::min((max_rows * c.ch) / doll->img.h, (44 * c.cw) / doll->img.w));
            doll_h = doll->img.h * L.doll_scale;
            doll_cols = (doll->img.w * L.doll_scale + c.cw - 1) / c.cw + 3;
            body_rows = (doll_h + c.ch - 1) / c.ch;
        }
        std::vector<int> listed;
        size_t longest = 8;
        for (int i = 0; i < n; ++i)
            if (!doll || m.entries[i].slot < 0) {
                listed.push_back(i);
                longest = std::max(longest, m.entries[i].name.size());
            }
        const int col_w = std::clamp(int(longest) + 9, 22, 38);
        const int count = int(listed.size());
        const int rows_cap = std::clamp(doll ? body_rows : 12, 8, max_rows);
        int cols = std::max(1, (count + rows_cap - 1) / rows_cap);
        cols = std::min(cols, std::max(1, (widest - 4 - doll_cols) / col_w));
        const int rows = std::max(1, std::min((count + cols - 1) / cols, max_rows));
        if (count == 0)
            cols = 0;
        body_rows = std::max(body_rows, std::max(rows, 3));
        PW = 4 + doll_cols + cols * col_w;
        L.doll_x = 2 * c.cw;
        L.doll_y = body_top * c.ch + (body_rows * c.ch - doll_h) / 2;
        if (doll)
            for (int i = 0; i < n; ++i) {
                const MenuEntry &e = m.entries[i];
                if (e.slot < 0 || e.slot >= int(doll->slots.size()))
                    continue;
                MenuItem it;
                it.kind = ITEM_DOLL;
                it.index = i;
                it.w = 2 * c.cw + 8 + int(e.mark.size()) * c.cw;
                it.h = c.ch + 4;
                it.x = L.doll_x + doll->slots[e.slot].x * L.doll_scale - it.w / 2;
                it.y = L.doll_y + doll->slots[e.slot].y * L.doll_scale - it.h / 2;
                L.items.push_back(it);
            }
        for (int k = 0; k < count; ++k) {
            const int col = k / rows, row = k % rows;
            if (col >= cols)
                break;      // more parts than fit: the rest cannot be picked here
            MenuItem it;
            it.kind = ITEM_LIST;
            it.index = listed[k];
            it.x = (2 + doll_cols + col * col_w) * c.cw;
            it.y = (body_top + row) * c.ch;
            it.w = (col_w - 2) * c.cw;
            it.h = c.ch;
            L.items.push_back(it);
        }
    } else {
        size_t longest = 8;
        for (const MenuEntry &e : m.entries)
            longest = std::max(longest, e.name.size());
        const int col_w = std::clamp(int(longest) + 9, 24, 42);
        const int cols = n > 8 ? 2 : 1;
        const int rows = std::max(1, (n + cols - 1) / cols);
        body_rows = std::max(rows, 3);
        PW = 4 + cols * col_w + (m.options.empty() ? 0 : 42);
        for (int i = 0; i < n; ++i) {
            MenuItem it;
            it.kind = ITEM_LIST;
            it.index = i;
            it.x = (2 + (i / rows) * col_w) * c.cw;
            it.y = (body_top + i % rows) * c.ch;
            it.w = (col_w - 2) * c.cw;
            it.h = c.ch;
            L.items.push_back(it);
        }
        // the check boxes: two columns of three
        for (size_t k = 0; k < m.options.size(); ++k) {
            MenuItem it;
            it.kind = ITEM_OPTION;
            it.index = int(k);
            it.x = (2 + cols * col_w + 2 + int(k / 3) * 20) * c.cw;
            it.y = (body_top + int(k % 3)) * c.ch;
            it.w = 19 * c.cw;
            it.h = c.ch;
            L.items.push_back(it);
        }
    }
    PW = std::clamp(std::max(PW, int(m.title.size()) + 20), 40, widest);
    const int PH = body_top + body_rows + 1 + 3 + 1;    // gap, three lines of help, edge
    L.w = PW * c.cw;
    L.h = PH * c.ch;
    L.x = c.ox + ((c.dimx - PW) / 2) * c.cw;
    const int bottom = std::clamp(m.bottom_row, std::min(PH + 2, c.dimy - 1), c.dimy - 1);
    L.y = c.oy + (bottom + 1 - PH) * c.ch;
    L.status_y = (body_top + body_rows + 1) * c.ch;
    MenuItem cancel;
    cancel.kind = ITEM_CANCEL;
    cancel.x = (PW - 2 - 12) * c.cw;
    cancel.y = c.ch;
    cancel.w = 12 * c.cw;
    cancel.h = c.ch;
    L.items.push_back(cancel);
    return true;
}

// ---------------------------------------------------------------------------
// the menu: drawing (render thread)

const uint32_t UI_WHITE = fpv::rgb(255, 255, 255), UI_DIM = fpv::rgb(130, 134, 146);
const uint32_t UI_GOLD = fpv::rgb(255, 170, 30), UI_YELLOW = fpv::rgb(255, 225, 90);

void paint_menu(const MenuModel &m, const MenuLayout &L, int hover, fpv::Canvas &cv) {
    const int cw = L.cw, ch = L.ch;
    cv.resize(L.w, L.h);
    cv.fill(0, 0, L.w, L.h, fpv::rgb(18, 20, 26));
    cv.frame(0, 0, L.w, L.h, UI_GOLD, 2);
    cv.frame(3, 3, L.w - 6, L.h - 6, fpv::rgb(70, 52, 20), 1);
    cv.text(ui_font, 2 * cw, ch, m.title, UI_WHITE, cw, ch);
    if (m.kind == 1 && m.doll)
        cv.blit(m.doll->img, L.doll_x, L.doll_y, L.doll_scale);

    const auto codes = [&](const MenuEntry &e, int x, int y) {
        cv.text(ui_font, x, y, std::string(1, code_letter(e.d1)), e.c1, cw, ch);
        cv.text(ui_font, x + cw, y, std::string(1, code_letter(e.d2)), e.c2, cw, ch);
        if (!e.mark.empty())
            cv.text(ui_font, x + 2 * cw + 2, y, e.mark, fpv::rgb(19, 253, 101), cw, ch);
    };
    for (size_t i = 0; i < L.items.size(); ++i) {
        const MenuItem &it = L.items[i];
        const bool hot = int(i) == hover;
        if (it.kind == ITEM_DOLL) {
            const MenuEntry &e = m.entries[it.index];
            cv.fill(it.x, it.y, it.w, it.h, hot ? fpv::rgb(60, 56, 30) : fpv::rgb(10, 10, 14));
            cv.frame(it.x, it.y, it.w, it.h, hot ? UI_YELLOW : fpv::rgb(96, 102, 118), 1);
            codes(e, it.x + 4, it.y + 2);
        } else if (it.kind == ITEM_LIST) {
            const MenuEntry &e = m.entries[it.index];
            if (hot)
                cv.fill(it.x - 2, it.y, it.w + 4, it.h, fpv::rgb(52, 56, 74));
            const int room = std::max(4, it.w / cw - 5);
            const bool hopeless = e.d1.rfind("Impossible", 0) == 0;
            cv.text(ui_font, it.x, it.y, e.name.substr(0, size_t(room)), hopeless ? UI_DIM : e.cn, cw, ch);
            codes(e, it.x + it.w - 5 * cw, it.y);
        } else if (it.kind == ITEM_OPTION) {
            const MenuOption &o = m.options[it.index];
            if (hot)
                cv.fill(it.x - 2, it.y, it.w + 4, it.h, fpv::rgb(52, 56, 74));
            const fpv::Image &box = o.on ? m.media->checkbox_on : m.media->checkbox_off;
            const int side = std::min(ch, 2 * cw);
            if (box.ok()) {
                cv.blitFit(box, it.x, it.y + (ch - side) / 2, side, side);
            } else {
                cv.frame(it.x, it.y + (ch - side) / 2, side, side, UI_WHITE, 1);
                if (o.on)
                    cv.fill(it.x + 3, it.y + (ch - side) / 2 + 3, side - 6, side - 6, fpv::rgb(120, 240, 110));
            }
            cv.text(ui_font, it.x + side + cw, it.y, o.label.substr(0, 16), o.on ? UI_YELLOW : UI_WHITE, cw, ch);
        } else {
            cv.text(ui_font, it.x, it.y, "Cancel (Esc)", hot ? UI_YELLOW : UI_DIM, cw, ch);
        }
    }

    // what the pointer is on, in full
    const int sx = 2 * cw;
    int x = sx;
    const auto put = [&](const std::string &s, uint32_t color, int y) {
        cv.text(ui_font, x, y, s, color, cw, ch);
        x += int(s.size()) * cw;
    };
    if (hover >= 0 && hover < int(L.items.size()) && L.items[hover].kind <= ITEM_LIST) {
        const MenuEntry &e = m.entries[L.items[hover].index];
        put(e.name, e.cn, L.status_y);
        if (!e.d1.empty()) {
            put(": ", UI_WHITE, L.status_y);
            put(e.d1, e.c1, L.status_y);
        }
        if (!e.d2.empty()) {
            put(", ", UI_WHITE, L.status_y);
            put(e.d2, e.c2, L.status_y);
        }
    } else {
        put(m.kind == 1 ? "Click a body part to aim for it." : "Click what to attack with.", UI_DIM, L.status_y);
    }
    // what the letters stand for: one line for each of the two
    for (int which = 0; which < 2; ++which) {
        x = sx;
        const int y = L.status_y + (1 + which) * ch;
        std::vector<std::string> seen;
        for (const MenuEntry &e : m.entries) {
            std::string word = which == 0 ? e.d1.substr(0, e.d1.find(' ')) : e.d2;
            if (word.empty() || std::find(seen.begin(), seen.end(), word) != seen.end())
                continue;
            if (x + int(word.size() + 4) * cw > L.w - 2 * cw)
                break;
            seen.push_back(word);
            const uint32_t color = which == 0 ? e.c1 : e.c2;
            put(std::string(1, code_letter(word)) + " ", color, y);
            put(word + "  ", color, y);
        }
    }
}

struct UiTexture {
    SDL_Texture *tex = nullptr;
    int w = 0, h = 0;

    void destroy() {
        if (tex && sdl_destroy_texture)
            sdl_destroy_texture(tex);
        tex = nullptr;
        w = h = 0;
    }
    bool upload(SDL_Renderer *sdl, const uint32_t *px, int w_, int h_) {
        if (w_ < 1 || h_ < 1)
            return false;
        if (!tex || w != w_ || h != h_) {
            destroy();
            tex = sdl_create_texture(sdl, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w_, h_);
            if (!tex)
                return false;
            sdl_set_texture_blend_mode(tex, SDL_BLENDMODE_BLEND);
            sdl_set_texture_scale_mode(tex, SDL_ScaleModeNearest);
            w = w_;
            h = h_;
        }
        sdl_update_texture(tex, nullptr, px, w * 4);
        return true;
    }
    bool upload(SDL_Renderer *sdl, const fpv::Image &img) {
        return img.ok() && upload(sdl, img.px.data(), img.w, img.h);
    }
    void draw(SDL_Renderer *sdl, int x, int y) const {
        if (!tex)
            return;
        const SDL_Rect dst = {x, y, w, h};
        sdl_render_copy(sdl, tex, nullptr, &dst);
    }
};

UiTexture tex_crosshair, tex_aim[fpv::AIM_COUNT], tex_label, tex_menu, tex_compass, tex_compass_frame;
constexpr int MAX_MARKS = 16;               // places on the compass at most
UiTexture tex_marks[MAX_MARKS];             // their labels
std::string tex_mark_keys[MAX_MARKS];       // what each label says
std::atomic<bool> compass_picture{false};   // the compass is drawn from the media folder
struct BodyModel;
UiTexture tex_body;                         // the adventurer's own paperdoll
const BodyModel *body_drawn = nullptr;
uint32_t ui_media_serial = 0xFFFFFFFFu;
uint32_t label_serial = 0;
std::chrono::steady_clock::time_point label_until;
const MenuModel *menu_drawn = nullptr;
int menu_drawn_hover = -2, menu_drawn_w = 0, menu_drawn_h = 0;

void destroy_ui_textures() {
    tex_crosshair.destroy();
    for (UiTexture &t : tex_aim)
        t.destroy();
    tex_label.destroy();
    tex_menu.destroy();
    tex_compass.destroy();
    tex_compass_frame.destroy();
    for (int i = 0; i < MAX_MARKS; ++i) {
        tex_marks[i].destroy();
        tex_mark_keys[i].clear();
    }
    tex_body.destroy();
    body_drawn = nullptr;
    ui_media_serial = 0xFFFFFFFFu;
    menu_drawn = nullptr;
}

// render thread: pictures from the media folder as textures
void sync_ui_textures(SDL_Renderer *sdl) {
    const uint32_t serial = media_serial.load();
    if (serial == ui_media_serial)
        return;
    ui_media_serial = serial;
    const std::shared_ptr<const fpv::Media> m = get_media();
    tex_crosshair.destroy();
    for (UiTexture &t : tex_aim)
        t.destroy();
    tex_compass.destroy();
    tex_compass_frame.destroy();
    compass_picture = false;
    if (!m)
        return;
    compass_picture = tex_compass.upload(sdl, m->compass) && tex_compass_frame.upload(sdl, m->compass_frame);
    tex_crosshair.upload(sdl, m->crosshair);
    for (int i = 0; i < fpv::AIM_COUNT; ++i)
        tex_aim[i].upload(sdl, m->aim[i]);
}

// ---------------------------------------------------------------------------
// the adventurer's own paperdoll, in place of the portrait: marks where they
// are hurt, and a bar for how much blood they have left

constexpr int BODY_COLS = 12, BODY_ROWS = 8;    // the portrait's place, in interface cells from the bottom left

enum { HURT_NONE, HURT_BRUISE, HURT_CUT, HURT_GONE };

struct BodyModel {
    std::shared_ptr<const fpv::Media> media;
    const fpv::Doll *doll = nullptr;
    std::vector<uint8_t> hurt;      // per place on the doll
    float blood = 1;
};

std::mutex body_mutex;
std::shared_ptr<const BodyModel> body_model;
std::atomic<bool> body_shown{false};

std::shared_ptr<const BodyModel> get_body() {
    std::lock_guard<std::mutex> lock(body_mutex);
    return body_model;
}

// simulation thread
void update_body(df::unit *adv) {
    const std::shared_ptr<const fpv::Media> media = get_media();
    if (!adv || !media || media->dolls.empty() || !adv->body.body_plan || !settings.body_doll) {
        std::lock_guard<std::mutex> lock(body_mutex);
        body_model = nullptr;
        body_shown = false;
        return;
    }
    const auto &parts = adv->body.body_plan->body_parts;
    std::vector<std::string> names;
    for (df::body_part_raw *part : parts)
        names.push_back(part && !part->name_singular.empty() && part->name_singular[0] ? *part->name_singular[0]
                                                                                    : std::string());
    auto m = std::make_shared<BodyModel>();
    m->media = media;
    std::vector<int> slot_of;
    m->doll = media->bestDoll(names, slot_of);
    if (!m->doll) {
        std::lock_guard<std::mutex> lock(body_mutex);
        body_model = nullptr;
        body_shown = false;
        return;
    }
    m->hurt.assign(m->doll->slots.size(), HURT_NONE);
    // a small part counts for the large one it is attached to
    const auto place = [&](int bp) {
        for (int hops = 0; hops < 12 && bp >= 0 && size_t(bp) < parts.size(); ++hops) {
            if (slot_of[bp] >= 0)
                return slot_of[bp];
            bp = parts[bp] ? parts[bp]->con_part_id : -1;
        }
        return -1;
    };
    const auto mark = [&](int bp, uint8_t how) {
        const int slot = place(bp);
        if (slot >= 0 && how > m->hurt[slot])
            m->hurt[slot] = how;
    };
    for (df::unit_wound *wound : adv->body.wounds) {
        if (!wound)
            continue;
        for (df::unit_wound_layerst *layer : wound->parts) {
            if (!layer)
                continue;
            const auto &f = layer->flags1.bits;
            const bool open = f.cut || f.edged_damage || f.major_artery || f.guts_spilled || layer->bleeding > 0;
            const bool blunt = f.smashed || f.smashed_apart || f.tendon_bruised || f.tendon_strained || f.tendon_torn ||
                               f.ligament_bruised || f.ligament_sprained || f.ligament_torn || layer->strain > 0 ||
                               layer->pain > 0 || layer->swelling > 0;
            if (open)
                mark(layer->body_part_id, HURT_CUT);
            else if (blunt)
                mark(layer->body_part_id, HURT_BRUISE);
        }
    }
    const auto &status = adv->body.components.body_part_status;
    for (size_t bp = 0; bp < parts.size() && bp < status.size(); ++bp)
        if (status[bp].bits.missing && slot_of[bp] >= 0)
            m->hurt[slot_of[bp]] = HURT_GONE;
    m->blood = adv->body.blood_max > 0
                   ? std::clamp(float(adv->body.blood_count) / float(adv->body.blood_max), 0.0f, 1.0f)
                   : 1.0f;
    std::lock_guard<std::mutex> lock(body_mutex);
    // only a new picture when something changed
    if (!body_model || body_model->doll != m->doll || body_model->hurt != m->hurt ||
        std::abs(body_model->blood - m->blood) > 0.004f || body_model->media != m->media)
        body_model = m;
    body_shown = true;
}

// simulation thread: the game's portrait is not drawn where our doll goes
void hide_portrait() {
    if (!body_shown || !gps)
        return;
    for (int cx = 0; cx < BODY_COLS && cx < gps->dimx; ++cx)
        for (int cy = std::max(0, gps->dimy - BODY_ROWS); cy < gps->dimy; ++cy) {
            const int idx = cx * gps->dimy + cy;
            std::memset(&gps->screen[idx * 8], 0, 8);
            gps->screentexpos[idx] = 0;
            gps->screentexpos_lower[idx] = 0;
            gps->screentexpos_anchored[idx] = 0;
            gps->screentexpos_flag[idx] = 0;
        }
}

// ---------------------------------------------------------------------------
// The top of the plain adventure screen (simulation thread): the game's list
// of nearby places in the top left goes onto the compass, the game's box
// about the looked-at tile moves into that corner, and the box the game
// shows new messages and conversation lines in is not drawn (the message
// list has them).

const char *const DIR16[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                               "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
constexpr float PI_F = 3.14159265f;

struct CompassMark {
    float bearing = 0;          // radians clockwise from north
    std::string what, dist;     // the two small lines under the compass
    uint32_t color = 0;
    uint8_t glyph = 0;          // the game's symbol for it; 0 for none
};

std::mutex marks_mutex;
std::shared_ptr<const std::vector<CompassMark>> marks_model;

std::shared_ptr<const std::vector<CompassMark>> get_marks() {
    std::lock_guard<std::mutex> lock(marks_mutex);
    return marks_model;
}

struct CellRect {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool empty() const { return x1 < x0 || y1 < y0; }
    bool has(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    void add(int x, int y) {
        if (empty()) {
            x0 = x1 = x;
            y0 = y1 = y;
            return;
        }
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    }
    void add(const CellRect &r) {
        if (r.empty())
            return;
        add(r.x0, r.y0);
        add(r.x1, r.y1);
    }
};

uint8_t grid_char(int x, int y) {
    return gps->screen[(size_t(x) * gps->dimy + y) * 8];
}
uint32_t grid_color(int x, int y) {
    const uint8_t *c = &gps->screen[(size_t(x) * gps->dimy + y) * 8];
    return fpv::rgb(c[1], c[2], c[3]);
}

void blank_cell(size_t idx) {
    std::memset(&gps->screen[idx * 8], 0, 8);
    gps->screentexpos[idx] = 0;
    gps->screentexpos_lower[idx] = 0;
    gps->screentexpos_anchored[idx] = 0;
    gps->screentexpos_flag[idx] = 0;
    if (gps->top_in_use && gps->screen_top) {
        std::memset(&gps->screen_top[idx * 8], 0, 8);
        gps->screentexpos_top[idx] = 0;
        gps->screentexpos_top_lower[idx] = 0;
        gps->screentexpos_top_anchored[idx] = 0;
        gps->screentexpos_top_flag[idx] = 0;
    }
}

void blank_rect(const CellRect &r, const CellRect &keep) {
    for (int x = std::max(0, r.x0); x <= r.x1 && x < gps->dimx; ++x)
        for (int y = std::max(0, r.y0); y <= r.y1 && y < gps->dimy; ++y)
            if (!keep.has(x, y))
                blank_cell(size_t(x) * gps->dimy + y);
}

// the drawn cells joined to (x, y) within limit, as a rectangle; empty if
// there are more than max_cells of them (then it is not one small panel)
CellRect drawn_area(int x, int y, const CellRect &limit, const CellRect &skip, int max_cells) {
    CellRect out;
    const int dimy = gps->dimy;
    if (!limit.has(x, y) || skip.has(x, y) || !grid_cell_drawn(size_t(x) * dimy + y))
        return out;
    std::vector<uint8_t> seen(size_t(gps->dimx) * dimy, 0);
    std::vector<int> todo{x * dimy + y};
    seen[size_t(x) * dimy + y] = 1;
    int count = 0;
    while (!todo.empty()) {
        const int idx = todo.back();
        todo.pop_back();
        if (++count > max_cells)
            return CellRect();
        const int cx = idx / dimy, cy = idx % dimy;
        out.add(cx, cy);
        const int nx[4] = {cx - 1, cx + 1, cx, cx}, ny[4] = {cy, cy, cy - 1, cy + 1};
        for (int i = 0; i < 4; ++i) {
            if (!limit.has(nx[i], ny[i]) || skip.has(nx[i], ny[i]) || nx[i] < 0 || ny[i] < 0 ||
                nx[i] >= gps->dimx || ny[i] >= dimy)
                continue;
            const size_t n = size_t(nx[i]) * dimy + ny[i];
            if (seen[n] || !grid_cell_drawn(n))
                continue;
            seen[n] = 1;
            todo.push_back(int(n));
        }
    }
    return out;
}

// The box at the top of the screen with the latest messages (in a
// conversation, with the speaker's portrait). It is found by the text of a
// message it shows. While the game has more to show than fits it waits for
// the player, so then the box stays.
void hide_message_box(const CellRect &keep) {
    if (!world || world->status.temp_flag.bits.adv_have_more)
        return;
    const int dimx = gps->dimx, dimy = gps->dimy;
    const CellRect top{0, 0, dimx - 1, dimy / 2};
    std::vector<std::string> needles;
    const auto add = [&](const std::vector<df::adv_announcementst *> &list) {
        for (const df::adv_announcementst *a : list) {
            if (!a)
                continue;
            needles.push_back(a->str);
            if (a->parent)
                needles.push_back(a->parent->text);
        }
    };
    add(world->status.adv_announcement);
    // the lines of a conversation once its menu has closed
    if (game && !game->main_interface.adventure.conversation.open)
        add(game->main_interface.adventure.conversation.adv_announcement);
    for (std::string needle : needles) {
        const size_t first = needle.find_first_not_of(' ');
        if (first == std::string::npos)
            continue;
        needle = needle.substr(first, 12);
        if (needle.size() < 4)
            continue;
        const int n = int(needle.size());
        for (int y = 0; y <= top.y1; ++y)
            for (int x = 0; x + n <= dimx; ++x) {
                int i = 0;
                while (i < n && grid_char(x + i, y) == uint8_t(needle[i]))
                    ++i;
                if (i < n || keep.has(x, y))
                    continue;
                const CellRect box = drawn_area(x, y, top, keep, dimx * dimy / 3);
                if (box.empty() || box.x1 - box.x0 > dimx - 4)
                    continue;
                blank_rect(box, keep);
                return;
            }
    }
}

// one entry of the game's list of nearby places
struct NearbyEntry {
    int dir = 0;                // index into DIR16
    std::string text;           // any words before the direction
    uint8_t glyph = 0;
    uint32_t color = 0;
};

// The list in the top left: per entry a symbol, maybe a word, and a
// direction ("SSW"). Read, then emptied.
std::vector<NearbyEntry> take_nearby_list(const CellRect &keep) {
    std::vector<NearbyEntry> out;
    const int dimy = gps->dimy, cols = std::min(gps->dimx, 32), rows = dimy / 2;
    const auto letter = [&](int x, int y) {
        if (keep.has(x, y))
            return uint8_t(0);
        const uint8_t ch = grid_char(x, y);
        return ch == ' ' ? uint8_t(0) : ch;
    };
    std::vector<std::pair<int, int>> starts;    // a cell of each entry, to find the panel from
    CellRect area;
    for (int y = 0; y < rows; ++y) {
        int start = -1;
        std::vector<std::string> words;
        for (int x = 0; x < cols;) {
            if (!letter(x, y)) {
                ++x;
                continue;
            }
            std::string word;
            int end = x;
            while (end < cols && letter(end, y))
                word += char(letter(end++, y));
            if (start < 0)
                start = x;
            int dir = -1;
            for (int d = 0; d < 16; ++d)
                if (word == DIR16[d])
                    dir = d;
            if (dir < 0) {
                words.push_back(word);
                x = end;
                continue;
            }
            NearbyEntry e;
            e.dir = dir;
            e.color = grid_color(start, y);
            size_t w = 0;
            // a lone symbol first is the entry's picture
            if (!words.empty() && words[0].size() == 1 && !std::isalnum(uint8_t(words[0][0]))) {
                e.glyph = uint8_t(words[0][0]);
                w = 1;
            }
            for (; w < words.size(); ++w)
                e.text += (e.text.empty() ? "" : " ") + words[w];
            out.push_back(e);
            starts.emplace_back(start, y);
            // a symbol drawn as a picture leaves no letter: take the drawn cells before the entry too
            int from = start;
            while (from > 0 && !keep.has(from - 1, y) && grid_cell_drawn(size_t(from - 1) * dimy + y) &&
                   start - from < 3)
                --from;
            area.add(from, y);
            area.add(end - 1, y);
            start = -1;
            words.clear();
            x = end;
        }
    }
    if (out.empty())
        return out;
    // the whole panel, its frame included
    const CellRect limit{0, 0, std::min(gps->dimx - 1, 40), rows};
    for (const auto &s : starts)
        area.add(drawn_area(s.first, s.second, limit, keep, 40 * rows));
    blank_rect(area, keep);
    return out;
}

std::string site_kind(df::world_site_type t) {
    using T = df::world_site_type;
    switch (t) {
    case T::PlayerFortress: case T::Fortress: return "fortress";
    case T::DarkFortress: return "dark fortress";
    case T::Cave: return "cave";
    case T::MountainHalls: return "mountain halls";
    case T::ForestRetreat: return "forest retreat";
    case T::Town: return "town";
    case T::ImportantLocation: return "location";
    case T::LairShrine: return "lair";
    case T::Camp: return "camp";
    case T::Monument: return "monument";
    default: return "site";
    }
}

std::string distance_text(float tiles) {
    const int t = int(tiles + 0.5f);
    return t < 10000 ? std::to_string(t) + " tiles" : std::to_string((t + 500) / 1000) + "k tiles";
}

// What the entries are and exactly where: the game's list says only roughly
// which way. Each entry is taken for the nearest world site that lies that
// way (or one direction next to it); one that matches none keeps the
// direction it was given.
std::vector<CompassMark> place_marks(const std::vector<NearbyEntry> &entries) {
    struct Place {
        float bearing, dist;
        const df::world_site *site;
    };
    static std::unordered_map<int32_t, std::string> names;
    std::vector<Place> places;
    df::unit *adv = World::getAdventurer();
    if (adv && world && world->world_data) {
        const double gx = world->map.region_x * 48.0 + adv->pos.x + 0.5;
        const double gy = world->map.region_y * 48.0 + adv->pos.y + 0.5;
        for (const df::world_site *s : world->world_data->sites) {
            if (!s)
                continue;
            const double sx0 = s->global_min_x * 48.0, sx1 = (s->global_max_x + 1) * 48.0;
            const double sy0 = s->global_min_y * 48.0, sy1 = (s->global_max_y + 1) * 48.0;
            // the site you are in has no direction
            if (gx >= sx0 && gx < sx1 && gy >= sy0 && gy < sy1)
                continue;
            const double dx = (sx0 + sx1) / 2 - gx, dy = (sy0 + sy1) / 2 - gy;
            places.push_back({float(std::atan2(dx, -dy)), float(std::sqrt(dx * dx + dy * dy)), s});
        }
    }
    std::vector<bool> used(places.size(), false);
    std::vector<CompassMark> marks;
    for (const NearbyEntry &e : entries) {
        CompassMark m;
        m.glyph = e.glyph;
        m.color = e.color;
        m.bearing = e.dir * PI_F / 8;
        int best = -1;
        for (int slack = 0; slack <= 1 && best < 0; ++slack)
            for (size_t i = 0; i < places.size(); ++i) {
                if (used[i])
                    continue;
                const int sector = int(std::lround(places[i].bearing / (PI_F / 8))) & 15;
                if (std::min((sector - e.dir) & 15, (e.dir - sector) & 15) > slack)
                    continue;
                if (best < 0 || places[i].dist < places[best].dist)
                    best = int(i);
            }
        if (best >= 0) {
            const Place &p = places[best];
            used[best] = true;
            m.bearing = p.bearing;
            auto it = names.find(p.site->id);
            if (it == names.end())
                it = names.emplace(p.site->id, Translation::translateName(&p.site->name, false)).first;
            m.what = (e.text.empty() ? "" : e.text + " ") + it->second + " (" + site_kind(p.site->type) + ")";
            m.dist = distance_text(p.dist);
        } else {
            m.what = e.text.empty() ? std::string(DIR16[e.dir]) : e.text;
        }
        marks.push_back(m);
        if (int(marks.size()) >= MAX_MARKS)
            break;
    }
    return marks;
}

void publish_marks(std::vector<CompassMark> marks) {
    std::lock_guard<std::mutex> lock(marks_mutex);
    if (marks_model && marks_model->size() == marks.size()) {
        bool same = true;
        for (size_t i = 0; i < marks.size() && same; ++i) {
            const CompassMark &a = (*marks_model)[i], &b = marks[i];
            same = a.what == b.what && a.dist == b.dist && a.color == b.color && a.glyph == b.glyph &&
                   std::abs(a.bearing - b.bearing) < 0.001f;
        }
        if (same)
            return;
    }
    if (!marks_model && marks.empty())
        return;
    marks_model = marks.empty() ? nullptr : std::make_shared<const std::vector<CompassMark>>(std::move(marks));
}

// The game's box about the looked-at tile (as measured by tip_measure) into
// the top left corner. It is copied whole, every layer of every cell.
void move_tip_to_corner() {
    const int x0 = tip_x0.load(), y0 = tip_y0.load(), x1 = tip_x1.load(), y1 = tip_y1.load();
    const int dimx = gps->dimx, dimy = gps->dimy;
    const int to_y = 0;
    if (x1 < x0 || y1 < y0 || x0 < 0 || y0 < 0 || x1 >= dimx || y1 >= dimy)
        return;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w > dimx - 2 || to_y + h > dimy)
        return;
    struct Saved {
        uint8_t s[8], st[8];
        long t, l, a, ax, ay, tt, tl, ta, tax, tay;
        uint32_t f, tf;
    };
    const bool top = gps->top_in_use && gps->screen_top;
    std::vector<Saved> cells(size_t(w) * h);
    for (int x = 0; x < w; ++x)
        for (int y = 0; y < h; ++y) {
            const size_t i = size_t(x0 + x) * dimy + (y0 + y);
            Saved &c = cells[size_t(x) * h + y];
            std::memcpy(c.s, &gps->screen[i * 8], 8);
            c.t = gps->screentexpos[i];
            c.l = gps->screentexpos_lower[i];
            c.a = gps->screentexpos_anchored[i];
            c.ax = gps->screentexpos_anchored_x[i];
            c.ay = gps->screentexpos_anchored_y[i];
            c.f = gps->screentexpos_flag[i];
            if (top) {
                std::memcpy(c.st, &gps->screen_top[i * 8], 8);
                c.tt = gps->screentexpos_top[i];
                c.tl = gps->screentexpos_top_lower[i];
                c.ta = gps->screentexpos_top_anchored[i];
                c.tax = gps->screentexpos_top_anchored_x[i];
                c.tay = gps->screentexpos_top_anchored_y[i];
                c.tf = gps->screentexpos_top_flag[i];
            }
            blank_cell(i);
        }
    for (int x = 0; x < w; ++x)
        for (int y = 0; y < h; ++y) {
            const size_t i = size_t(x) * dimy + (to_y + y);
            const Saved &c = cells[size_t(x) * h + y];
            std::memcpy(&gps->screen[i * 8], c.s, 8);
            gps->screentexpos[i] = c.t;
            gps->screentexpos_lower[i] = c.l;
            gps->screentexpos_anchored[i] = c.a;
            gps->screentexpos_anchored_x[i] = c.ax;
            gps->screentexpos_anchored_y[i] = c.ay;
            gps->screentexpos_flag[i] = c.f;
            if (top) {
                std::memcpy(&gps->screen_top[i * 8], c.st, 8);
                gps->screentexpos_top[i] = c.tt;
                gps->screentexpos_top_lower[i] = c.tl;
                gps->screentexpos_top_anchored[i] = c.ta;
                gps->screentexpos_top_anchored_x[i] = c.tax;
                gps->screentexpos_top_anchored_y[i] = c.tay;
                gps->screentexpos_top_flag[i] = c.tf;
            }
        }
    tip_x1 = -1;
    tip_x0 = 0;
    tip_y0 = to_y;
    tip_y1 = to_y + h - 1;
    tip_x1 = w - 1;
}

// The game's box about the looked-at tile, found by its look: a framed
// rectangle (a full row of drawn cells on top and bottom, a full column on
// each side) at least 6 wide and 3 high in the upper part of the screen. The
// game decides about the box before drawing, so drawing without the pointer
// (tip_measure) does not always leave it out. The one nearest the pointer
// column, if there are several; not one already in the left edge (the list of
// nearby places).
void find_tip_box(int pointer_cx) {
    const int dimx = gps->dimx, dimy = gps->dimy;
    const auto on = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < dimx && y < dimy && grid_cell_drawn(size_t(x) * dimy + y);
    };
    int best = -1, bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    for (int y = 0; y < dimy / 2; ++y)
        for (int x = 0; x < dimx; ++x) {
            if (!on(x, y) || on(x - 1, y) || on(x, y - 1))
                continue;
            int x1 = x;
            while (on(x1 + 1, y))
                ++x1;
            int y1 = y;
            while (on(x, y1 + 1))
                ++y1;
            // the game's list of nearby places sits in the corner already
            if (x1 - x + 1 < 6 || y1 - y + 1 < 3 || x == 0)
                continue;
            bool closed = true;
            for (int i = x; i <= x1 && closed; ++i)
                closed = on(i, y1);
            for (int j = y; j <= y1 && closed; ++j)
                closed = on(x1, j);
            if (!closed)
                continue;
            const int d = pointer_cx < x ? x - pointer_cx : (pointer_cx > x1 ? pointer_cx - x1 : 0);
            if (best < 0 || d < best) {
                best = d;
                bx0 = x;
                by0 = y;
                bx1 = x1;
                by1 = y1;
            }
            x = x1;
        }
    if (best < 0)
        return;
    tip_x1 = -1;
    tip_x0 = bx0;
    tip_y0 = by0;
    tip_y1 = by1;
    tip_x1 = bx1;
}

void tidy_top(bool plain) {
    static int empty_frames = 0;
    if (!plain || !gps || !world || gps->dimx < 40 || gps->dimy < 20)
        return;
    if (frame_pointer.over_view && settings.game_tips) {
        int pcx = gps->dimx / 2, pcy = 0;
        interface_cell(frame_pointer.px, frame_pointer.py, pcx, pcy);
        find_tip_box(pcx);
    }
    CellRect tip;
    if (tip_x1.load() >= tip_x0.load())
        tip = CellRect{tip_x0.load(), tip_y0.load(), tip_x1.load(), tip_y1.load()};
    hide_message_box(tip);
    const std::vector<NearbyEntry> entries = take_nearby_list(tip);
    // a frame without the list now and then does not make the marks blink
    if (!entries.empty() || ++empty_frames > 20) {
        empty_frames = 0;
        publish_marks(place_marks(entries));
    }
    move_tip_to_corner();
}

// render thread
void draw_body(SDL_Renderer *sdl) {
    const std::shared_ptr<const BodyModel> m = get_body();
    Cells c;
    if (!m || !m->doll || !get_cells(c)) {
        body_drawn = nullptr;
        return;
    }
    const int w = BODY_COLS * c.cw - 2, h = BODY_ROWS * c.ch - 2;
    if (m.get() != body_drawn || tex_body.w != w || tex_body.h != h) {
        fpv::Canvas cv;
        cv.resize(w, h);
        cv.fill(0, 0, w, h, fpv::rgb(18, 20, 26));
        cv.frame(0, 0, w, h, UI_GOLD, 2);
        cv.frame(2, 2, w - 4, h - 4, fpv::rgb(70, 52, 20), 1);
        // blood: a bar with rounded ends along the bottom
        const int bh = std::max(6, c.ch / 2), bx = 6, bw = w - 12, by = h - 5 - bh;
        const auto bar = [&](int width, uint32_t color) {
            for (int y = 0; y < bh; ++y) {
                // corners cut to round the ends
                const int edge = std::min(y, bh - 1 - y);
                const int cut = edge == 0 ? 2 : edge == 1 ? 1 : 0;
                if (width > 2 * cut)
                    cv.fill(bx + cut, by + y, width - 2 * cut, 1, color);
            }
        };
        bar(bw, fpv::rgb(64, 14, 16));
        const int full = int(bw * m->blood + 0.5f);
        if (full > 0)
            bar(std::max(full, 5), fpv::rgb(226, 38, 34));
        // the doll, as large as fits above the bar
        const fpv::Image &img = m->doll->img;
        const int room_w = w - 10, room_h = by - 8;
        const float scale = std::min(float(room_w) / img.w, float(room_h) / img.h);
        const int dw = std::max(1, int(img.w * scale)), dh = std::max(1, int(img.h * scale));
        const int dx = (w - dw) / 2, dy = 5 + (room_h - dh) / 2;
        cv.blitFit(img, dx, dy, dw, dh);
        for (size_t s = 0; s < m->hurt.size() && s < m->doll->slots.size(); ++s) {
            if (!m->hurt[s])
                continue;
            const int x = dx + int(m->doll->slots[s].x * scale), y = dy + int(m->doll->slots[s].y * scale);
            const fpv::Image &icon = m->hurt[s] == HURT_BRUISE ? m->media->wound_bruise : m->media->wound_cut;
            if (m->hurt[s] == HURT_GONE) {
                // a missing part: crossed out
                for (int i = -4; i <= 4; ++i) {
                    cv.fill(x + i - 1, y + i, 3, 1, fpv::rgb(255, 60, 50));
                    cv.fill(x - i - 1, y + i, 3, 1, fpv::rgb(255, 60, 50));
                }
            } else if (icon.ok()) {
                cv.blit(icon, x - icon.w / 2, y - icon.h / 2, 1);
            } else {
                cv.fill(x - 3, y - 3, 7, 7,
                        m->hurt[s] == HURT_BRUISE ? fpv::rgb(170, 90, 210) : fpv::rgb(235, 40, 36));
            }
        }
        if (!tex_body.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        body_drawn = m.get();
    }
    tex_body.draw(sdl, c.ox + 1, c.oy + (c.dimy - BODY_ROWS) * c.ch + 1);
}

// The compass at the top: a strip holding the whole circle, N in its first
// eighth, slid so that where the camera looks is under the frame's marker.
// It is drawn four times as wide as the frame picture (short of the
// minimap), the frame stretched between its ends and its middle, and the
// strip twice as wide, so that most of the circle shows. The places from the
// game's list of nearby places sit on it where they lie, each named in two
// small lines under it (what it is, how far); one behind you is held at the
// nearer end.
constexpr int COMPASS_STRETCH = 2;

// the frame picture drawn wider: its ends and its middle (with the marker)
// as they are, the plain parts between them stretched
void draw_frame_wide(SDL_Renderer *sdl, int x0, int y0, int W) {
    const int fw = tex_compass_frame.w, fh = tex_compass_frame.h;
    if (W <= fw || fw < 40) {
        tex_compass_frame.draw(sdl, x0, y0);
        return;
    }
    const int cap = 8, mid = fw / 2, c = 8, dmid = W / 2;
    const int src[6] = {0, cap, mid - c, mid + c, fw - cap, fw};
    const int dst[6] = {0, cap, dmid - c, dmid + c, W - cap, W};
    for (int i = 0; i < 5; ++i) {
        const SDL_Rect s = {src[i], 0, src[i + 1] - src[i], fh};
        const SDL_Rect d = {x0 + dst[i], y0, dst[i + 1] - dst[i], fh};
        if (s.w > 0 && d.w > 0)
            sdl_render_copy(sdl, tex_compass_frame.tex, &s, &d);
    }
}

void make_mark_texture(SDL_Renderer *sdl, int i, const CompassMark &m) {
    const int cw = 8, ch = 12;
    const size_t first = m.what.size() + (m.glyph ? 2 : 0);
    const int chars = int(std::max(first, m.dist.size()));
    const int lines = m.dist.empty() ? 1 : 2;
    const uint32_t color = m.color ? m.color : UI_GOLD;
    fpv::Canvas cv;
    cv.resize(chars * cw + 6, lines * ch + 4);
    cv.fill(0, 0, cv.w, cv.h, 0xB0000000u | (fpv::rgb(12, 12, 16) & 0x00FFFFFFu));
    cv.frame(0, 0, cv.w, cv.h, color, 1);
    int x = 3;
    if (m.glyph) {
        cv.text(ui_font, x, 2, std::string(1, char(m.glyph)), color, cw, ch);
        x += 2 * cw;
    }
    cv.text(ui_font, x, 2, m.what, UI_WHITE, cw, ch);
    if (!m.dist.empty())
        cv.text(ui_font, 3, 2 + ch, m.dist, UI_DIM, cw, ch);
    tex_marks[i].upload(sdl, cv.px.data(), cv.w, cv.h);
}

void draw_compass(SDL_Renderer *sdl, int out_w, float angle) {
    compass_px1 = -1;
    if (!tex_compass.tex || !tex_compass_frame.tex)
        return;
    const int fw = tex_compass_frame.w, fh = tex_compass_frame.h;
    const int sw0 = tex_compass.w, sh = tex_compass.h, sw = sw0 * COMPASS_STRETCH;
    int room = out_w - 16;
    SDL_Rect mini;
    if (minimap_rect(mini))
        room = std::min(room, 2 * (mini.x - 12) - out_w);
    int W = std::max(fw, std::min(4 * fw, room));
    int win_w = std::min(W - 8, sw);
    win_w -= win_w % COMPASS_STRETCH;
    W = std::max(fw, win_w + 8);
    const int win_h = std::min(fh - 8, sh);
    const int x0 = out_w / 2 - W / 2, y0 = 2;
    const int wx = x0 + (W - win_w) / 2, wy = y0 + (fh - win_h) / 2;
    float turn = angle / 6.2831853f;
    turn -= std::floor(turn);
    // strip pixel under the marker, then the left edge of what shows (in the picture's own pixels)
    int left = int(std::floor(turn * sw0 + sw0 / 16.0f - win_w / (2.0f * COMPASS_STRETCH)));
    left = ((left % sw0) + sw0) % sw0;
    const int src_w = win_w / COMPASS_STRETCH;
    int done = 0;
    while (done < src_w) {
        const int from = (left + done) % sw0;
        const int n = std::min(src_w - done, sw0 - from);
        const SDL_Rect src = {from, 0, n, win_h};
        const SDL_Rect dst = {wx + done * COMPASS_STRETCH, wy, n * COMPASS_STRETCH, win_h};
        sdl_render_copy(sdl, tex_compass.tex, &src, &dst);
        done += n;
    }
    draw_frame_wide(sdl, x0, y0, W);
    int bottom = y0 + fh;

    const std::shared_ptr<const std::vector<CompassMark>> marks = get_marks();
    if (marks && ui_font_ready) {
        struct Placed {
            int x, i;
            bool held;
        };
        std::vector<Placed> placed;
        const int centre = wx + win_w / 2, half = win_w / 2 - 4;
        for (int i = 0; i < int(marks->size()) && i < MAX_MARKS; ++i) {
            const CompassMark &m = (*marks)[i];
            float rel = m.bearing - angle;
            rel -= 6.2831853f * std::floor((rel + PI_F) / 6.2831853f);
            int x = centre + int(rel * float(sw) / 6.2831853f);
            const bool held = std::abs(x - centre) > half;
            if (held)
                x = x < centre ? centre - half : centre + half;
            placed.push_back({x, i, held});
            const std::string key =
                m.what + "|" + m.dist + "|" + std::to_string(m.color) + "|" + std::to_string(int(m.glyph));
            if (key != tex_mark_keys[i] || !tex_marks[i].tex) {
                tex_mark_keys[i] = key;
                make_mark_texture(sdl, i, m);
            }
        }
        Uint8 r0 = 0, g0 = 0, b0 = 0, a0 = 0;
        sdl_get_render_draw_color(sdl, &r0, &g0, &b0, &a0);
        std::sort(placed.begin(), placed.end(), [](const Placed &a, const Placed &b) { return a.x < b.x; });
        constexpr int LEVELS = 3;
        int level_end[LEVELS] = {-100000, -100000, -100000};
        for (const Placed &p : placed) {
            const CompassMark &m = (*marks)[p.i];
            const uint32_t c = m.color ? m.color : UI_GOLD;
            const int div = p.held ? 2 : 1;
            // a pip on the lower edge of the frame, dimmer while held at an end
            sdl_set_render_draw_color(sdl, 0, 0, 0, 255);
            const SDL_Rect outer = {p.x - 4, y0 + fh - 7, 9, 9};
            sdl_render_fill_rect(sdl, &outer);
            sdl_set_render_draw_color(sdl, Uint8(fpv::px_r(c) / div), Uint8(fpv::px_g(c) / div),
                                      Uint8(fpv::px_b(c) / div), 255);
            const SDL_Rect inner = {p.x - 3, y0 + fh - 6, 7, 7};
            sdl_render_fill_rect(sdl, &inner);
            // its label under the compass, on the first row where it fits
            const UiTexture &t = tex_marks[p.i];
            if (!t.tex)
                continue;
            const int lx = std::clamp(p.x - t.w / 2, x0, std::max(x0, x0 + W - t.w));
            for (int l = 0; l < LEVELS; ++l) {
                if (lx <= level_end[l] + 2)
                    continue;
                level_end[l] = lx + t.w;
                const int ly = y0 + fh + 3 + l * (t.h + 2);
                t.draw(sdl, lx, ly);
                bottom = std::max(bottom, ly + t.h);
                break;
            }
        }
        sdl_set_render_draw_color(sdl, r0, g0, b0, a0);
    }
    compass_px0 = x0;
    compass_py0 = y0;
    compass_py1 = bottom;
    compass_px1 = x0 + W - 1;
}

// the few words above the crosshair (or the pointer), for a moment
void draw_flash(SDL_Renderer *sdl, int cx, int cy) {
    const uint32_t serial = flash_serial.load();
    const auto now = std::chrono::steady_clock::now();
    if (serial != label_serial && ui_font_ready) {
        label_serial = serial;
        std::string text;
        {
            std::lock_guard<std::mutex> lock(flash_mutex);
            text = flash_text;
        }
        Cells c;
        if (!get_cells(c) || text.empty())
            return;
        fpv::Canvas cv;
        cv.resize(int(text.size()) * c.cw + 16, c.ch + 10);
        cv.fill(0, 0, cv.w, cv.h, 0xC0000000u | (fpv::rgb(12, 12, 16) & 0x00FFFFFFu));
        cv.frame(0, 0, cv.w, cv.h, UI_GOLD, 1);
        cv.text(ui_font, 8, 5, text, UI_WHITE, c.cw, c.ch);
        tex_label.upload(sdl, cv.px.data(), cv.w, cv.h);
        label_until = now + std::chrono::milliseconds(1400);
    }
    if (now < label_until)
        tex_label.draw(sdl, cx - tex_label.w / 2, cy - 34 - tex_label.h);
}

// true if the menu is up (and was drawn)
bool draw_menu(SDL_Renderer *sdl) {
    const std::shared_ptr<const MenuModel> m = get_menu();
    if (!m || !ui_font_ready) {
        menu_drawn = nullptr;
        return false;
    }
    MenuLayout L;
    if (!layout_menu(*m, L))
        return false;
    int px = -1, py = -1;
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    const int hover = L.at(px, py);
    if (m.get() != menu_drawn || hover != menu_drawn_hover || L.w != menu_drawn_w || L.h != menu_drawn_h) {
        fpv::Canvas cv;
        paint_menu(*m, L, hover, cv);
        if (!tex_menu.upload(sdl, cv.px.data(), cv.w, cv.h))
            return false;
        menu_drawn = m.get();
        menu_drawn_hover = hover;
        menu_drawn_w = L.w;
        menu_drawn_h = L.h;
    }
    tex_menu.draw(sdl, L.x, L.y);
    return true;
}

// ---------------------------------------------------------------------------
// the menu: reading the game's dialog off the interface grid (simulation thread)

struct ScreenRow {
    int x = 0, y = 0;               // where its hotkey letter is
    std::string name, d1, d2;
    uint32_t cn = 0, c1 = 0, c2 = 0;
    std::string mark;
    bool same(const ScreenRow &o) const { return name == o.name && d1 == o.d1 && d2 == o.d2; }
};

inline uint8_t cell_char(int x, int y) {
    return gps->screen[(x * gps->dimy + y) * 8];
}
inline uint32_t cell_color(int x, int y) {
    const uint8_t *c = &gps->screen[(x * gps->dimy + y) * 8];
    return fpv::rgb(c[1], c[2], c[3]);
}
inline bool cell_used(int x, int y) {
    const int idx = x * gps->dimy + y;
    return gps->screen[idx * 8] || gps->screentexpos[idx] || gps->screentexpos_lower[idx] ||
           gps->screentexpos_anchored[idx] || gps->screentexpos_flag[idx];
}
inline bool cell_blank(int x, int y) {
    if (x < 0 || x >= gps->dimx)
        return true;
    const uint8_t ch = cell_char(x, y);
    return ch == 0 || ch == 32;
}

// a list row starts with its key: one green lowercase letter standing alone
bool is_hotkey(int x, int y) {
    const uint8_t ch = cell_char(x, y);
    if (ch < 'a' || ch > 'z')
        return false;
    const uint8_t *c = &gps->screen[(x * gps->dimy + y) * 8];
    if (!(c[2] > 200 && c[1] < 90 && c[3] < 170))
        return false;
    if (!cell_blank(x - 1, y) || !cell_blank(x + 1, y) || x + 2 >= gps->dimx)
        return false;
    const uint8_t next = cell_char(x + 2, y);
    return next > 32 && next < 127;
}

std::string rtrim(std::string s) {
    const size_t last = s.find_last_not_of(' ');
    s.erase(last == std::string::npos ? 0 : last + 1);
    return s;
}

// the rows the dialog shows right now, and the line above them
void read_dialog(std::vector<ScreenRow> &rows, std::string &title, int &title_x, int &title_y) {
    rows.clear();
    title.clear();
    title_x = title_y = -1;
    for (int y = 1; y < gps->dimy; ++y)
        for (int x = 1; x < gps->dimx - 3; ++x) {
            if (!is_hotkey(x, y))
                continue;
            ScreenRow r;
            r.x = x;
            r.y = y;
            std::string line;
            for (int xx = x + 2; xx < gps->dimx; ++xx) {
                const uint8_t ch = cell_char(xx, y);
                line += (ch > 32 && ch < 127) ? char(ch) : ' ';
            }
            const size_t gap = line.find("  ");
            r.name = rtrim(line.substr(0, gap));
            r.cn = cell_color(x + 2, y);
            const size_t d = gap == std::string::npos ? gap : line.find_first_not_of(' ', gap);
            if (d != std::string::npos) {
                std::string rest = rtrim(line.substr(d));
                // marks stand apart at the far end of the row
                std::string marks;
                const size_t tail = rest.find("  ");
                if (tail != std::string::npos) {
                    marks = rest.substr(tail);
                    rest.erase(tail);
                } else if (rest.find_first_not_of("+!* ") == std::string::npos) {
                    marks = rest;
                    rest.clear();
                }
                for (const char ch : marks)
                    if (ch != ' ' && r.mark.size() < 2)
                        r.mark += ch;
                if (!rest.empty()) {
                    const size_t comma = rest.find(", ");
                    r.d1 = rest.substr(0, comma);
                    r.c1 = cell_color(x + 2 + int(d), y);
                    if (comma != std::string::npos) {
                        r.d2 = rest.substr(comma + 2);
                        r.c2 = cell_color(x + 2 + int(d) + int(comma) + 2, y);
                    }
                }
            }
            rows.push_back(r);
            break;
        }
    if (rows.empty())
        return;
    for (int y = rows[0].y - 1; y >= std::max(0, rows[0].y - 6) && title_y < 0; --y)
        for (int x = std::max(0, rows[0].x - 8); x < gps->dimx; ++x) {
            const uint8_t ch = cell_char(x, y);
            if (ch <= 32 || ch >= 127)
                continue;
            std::string line;
            for (int xx = x; xx < gps->dimx; ++xx) {
                const uint8_t v = cell_char(xx, y);
                line += (v > 32 && v < 127) ? char(v) : ' ';
            }
            title = rtrim(line.substr(0, line.find("  ")));
            title_x = x;
            title_y = y;
            break;
        }
}

// the options of the "attack with" list are sentences: "Quick attack: faster to ..."
bool is_option_row(const ScreenRow &r) {
    return r.d1.empty() && r.name.find(": ") != std::string::npos;
}

struct MenuState {
    bool on = false;
    int kind = 0;
    std::vector<ScreenRow> first_page;      // what the dialog showed when the menu was made
    std::string title;
    uint32_t flags = 0;
    bool ranged = false;                    // the list belongs to the aiming of a shot or throw
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;   // the dialog, in interface cells
} menu_state;

void blank_dialog() {
    if (!gps || menu_state.x1 < menu_state.x0)
        return;
    for (int x = std::max(0, menu_state.x0); x <= menu_state.x1 && x < gps->dimx; ++x)
        for (int y = std::max(0, menu_state.y0); y <= menu_state.y1 && y < gps->dimy; ++y) {
            const int idx = x * gps->dimy + y;
            std::memset(&gps->screen[idx * 8], 0, 8);
            gps->screentexpos[idx] = 0;
            gps->screentexpos_lower[idx] = 0;
            gps->screentexpos_anchored[idx] = 0;
            gps->screentexpos_flag[idx] = 0;
        }
}

void close_menu() {
    if (!menu_state.on)
        return;
    menu_state.on = false;
    menu_state.x1 = -1;
    set_menu(nullptr);
    menu_px1 = -1;
}

// the scroll position of the game's list that our menu stands in for
int32_t &menu_scroll() {
    auto &atk = game->main_interface.adventure.attack;
    if (menu_state.ranged)
        return game->main_interface.adventure.aim_projectile.scroll_position_aim_target;
    return atk.mode == df::adventure_interface_attack_mode_type::AIM_TARGET ? atk.scroll_position_aim_target
                                                                           : atk.scroll_position_aim_attack;
}

// which of our menus the game's state calls for: 0 none, 1 body parts, 2 attacks
int menu_kind_now(bool &ranged) {
    ranged = false;
    auto &atk = game->main_interface.adventure.attack;
    using Mode = df::adventure_interface_attack_mode_type;
    if (atk.open)
        return atk.mode == Mode::AIM_TARGET ? 1 : atk.mode == Mode::AIM_ATTACK ? 2 : 0;
    // aiming a shot carefully: the aiming screen lists the body parts itself
    if (game->main_interface.adventure.aim_projectile.open && !game->main_interface.adventure.option_list.open) {
        ranged = true;
        return 1;
    }
    return 0;
}

uint32_t option_bit(const std::string &label) {
    df::aim_attack_flag f;
    f.whole = 0;
    if (label.rfind("Quick", 0) == 0) f.bits.quick = 1;
    else if (label.rfind("Heavy", 0) == 0) f.bits.heavy = 1;
    else if (label.rfind("Wild", 0) == 0) f.bits.wild = 1;
    else if (label.rfind("Precise", 0) == 0) f.bits.precise = 1;
    else if (label.rfind("Charge", 0) == 0) f.bits.charge = 1;
    else if (label.rfind("Multi", 0) == 0) f.bits.multi = 1;
    return f.whole;
}

// Called once per frame after the game has drawn itself. redraw() makes the
// game draw again (after the list was scrolled); tidy() empties the hidden
// top-down map that such a draw puts back.
void update_menu(const std::function<void()> &redraw, const std::function<void()> &tidy) {
    if (!game || !gps) {
        close_menu();
        return;
    }
    auto &atk = game->main_interface.adventure.attack;
    bool ranged = false;
    const int kind = (active && showing && settings.combat_menus && ui_font_ready) ? menu_kind_now(ranged) : 0;
    if (!kind) {
        close_menu();
        return;
    }
    std::vector<ScreenRow> page;
    std::string title;
    int tx = -1, ty = -1;
    read_dialog(page, title, tx, ty);
    // The aiming screen is mostly just a prompt; it is a list of body parts
    // only when its rows say how hard each is to hit.
    int rated = 0;
    for (const ScreenRow &r : page)
        if (!r.d1.empty())
            ++rated;
    if (page.empty() || ty < 0 || (ranged && rated < 2)) {
        close_menu();       // not a list we know: leave the game's dialog alone
        return;
    }
    const uint32_t flags = ranged ? 0 : atk.aim_attack_flag.whole;
    bool fresh = !menu_state.on || menu_state.kind != kind || menu_state.ranged != ranged ||
                 menu_state.title != title || menu_state.flags != flags ||
                 menu_state.first_page.size() != page.size();
    for (size_t i = 0; i < page.size() && !fresh; ++i)
        fresh = !page[i].same(menu_state.first_page[i]);
    menu_state.ranged = ranged;
    if (fresh) {
        // the dialog's own rectangle: a solid block of cells around its title
        int x0 = tx, x1 = tx, y0 = ty, y1 = ty;
        while (x0 > 0 && cell_used(x0 - 1, ty)) --x0;
        while (x1 + 1 < gps->dimx && cell_used(x1 + 1, ty)) ++x1;
        while (y0 > 0 && cell_used(x0, y0 - 1)) --y0;
        while (y1 + 1 < gps->dimy && cell_used(x0, y1 + 1)) ++y1;

        // every row of the list: scroll through it
        std::vector<ScreenRow> all, options;
        const auto take = [&](const std::vector<ScreenRow> &rows) {
            std::vector<ScreenRow> list;
            for (const ScreenRow &r : rows) {
                if (kind == 2 && is_option_row(r)) {
                    if (options.size() < 6 && std::none_of(options.begin(), options.end(),
                                                           [&](const ScreenRow &o) { return o.name == r.name; }))
                        options.push_back(r);
                } else {
                    list.push_back(r);
                }
            }
            // rows already seen at the end of what we have are skipped
            size_t overlap = std::min(all.size(), list.size());
            for (; overlap > 0; --overlap) {
                bool match = true;
                for (size_t i = 0; i < overlap && match; ++i)
                    match = all[all.size() - overlap + i].same(list[i]);
                if (match)
                    break;
            }
            const size_t before = all.size();
            all.insert(all.end(), list.begin() + overlap, list.end());
            return all.size() > before;
        };
        take(page);
        int32_t &scroll = menu_scroll();
        bool scrolled = false;
        for (int guard = 0; guard < 60 && all.size() < 400; ++guard) {
            scroll = int32_t(all.size());
            scrolled = true;
            redraw();
            std::vector<ScreenRow> more;
            std::string t;
            int a, b;
            read_dialog(more, t, a, b);
            if (!take(more))
                break;
        }
        if (scrolled) {
            scroll = 0;
            redraw();
            tidy();
        }

        auto m = std::make_shared<MenuModel>();
        m->kind = kind;
        m->title = title;
        m->bottom_row = y1;
        m->flags = flags;
        m->media = get_media();
        if (!m->media)
            m->media = std::make_shared<fpv::Media>();
        std::vector<std::string> names;
        for (const ScreenRow &r : all) {
            MenuEntry e;
            e.name = r.name;
            e.d1 = r.d1;
            e.d2 = r.d2;
            e.cn = r.cn;
            e.c1 = r.c1;
            e.c2 = r.c2;
            e.mark = r.mark;
            m->entries.push_back(e);
            names.push_back(r.name);
        }
        if (kind == 1) {
            std::vector<int> slot_of;
            m->doll = m->media->bestDoll(names, slot_of);
            for (size_t i = 0; i < m->entries.size(); ++i)
                m->entries[i].slot = slot_of[i];
        }
        for (const ScreenRow &r : options) {
            MenuOption o;
            o.label = r.name.substr(0, r.name.find(": "));
            o.on = (atk.aim_attack_flag.whole & option_bit(o.label)) != 0;
            m->options.push_back(o);
        }
        menu_state.on = true;
        menu_state.kind = kind;
        menu_state.first_page = page;
        menu_state.title = title;
        menu_state.flags = flags;
        menu_state.x0 = x0;
        menu_state.y0 = y0;
        menu_state.x1 = x1;
        menu_state.y1 = y1;
        set_menu(m);
    }
    blank_dialog();
    const std::shared_ptr<const MenuModel> m = get_menu();
    MenuLayout L;
    if (m && layout_menu(*m, L)) {
        menu_px0 = L.x;
        menu_py0 = L.y;
        menu_py1 = L.y + L.h - 1;
        menu_px1 = L.x + L.w - 1;
    }
}
