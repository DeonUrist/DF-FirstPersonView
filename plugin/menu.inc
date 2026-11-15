// Part of fpview.cpp (included inside its anonymous namespace).
//
// Intents: what a left click in the view does (look at, use or talk to, walk,
// strike, shoot or nock, unnock, throw, wrestle). R goes to the next one.
// Combat menus: the game's "aim for" and "attack with" lists, drawn again as
// a paperdoll and compact lists. The game's own dialog stays open underneath
// (emptied of its cells, so it is not seen) and does the actual work: what is
// clicked here is clicked there.

// ---------------------------------------------------------------------------
// intents

enum Intent {
    INTENT_STRIKE, INTENT_SHOOT, INTENT_THROW, INTENT_WRESTLE, INTENT_UNNOCK,
    INTENT_INFO, INTENT_INTERACT, INTENT_WALK, INTENT_COUNT
};
const char *const INTENT_NAMES[INTENT_COUNT] = {"Strike", "Shoot", "Throw", "Wrestle", "Unnock",
                                                "Info",   "Interact", "Walk"};
// the order R goes through them
const int INTENT_ORDER[INTENT_COUNT] = {INTENT_INFO,  INTENT_INTERACT, INTENT_WALK,  INTENT_STRIKE,
                                        INTENT_SHOOT, INTENT_UNNOCK,   INTENT_THROW, INTENT_WRESTLE};
const char *const INTENT_FILE = "dfhack-config/fpview-intent.txt";

std::atomic<int> intent{INTENT_WALK};     // remembered between sessions
std::atomic<bool> can_shoot{false};       // a ranged weapon is in hand

// the intents that attack: creatures get outlines, a click needs a target
bool combat_intent(int i) {
    return i == INTENT_STRIKE || i == INTENT_SHOOT || i == INTENT_THROW || i == INTENT_WRESTLE || i == INTENT_UNNOCK;
}

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

// Shooting needs something to shoot with, and taking the arrow off needs one
// on the string. Shoot without an arrow on the string means: nock one.
int effective_intent() {
    int i = intent.load();
    if (i == INTENT_UNNOCK && !arrow_nocked)
        i = INTENT_SHOOT;
    if ((i == INTENT_SHOOT || i == INTENT_UNNOCK) && !can_shoot)
        i = INTENT_STRIKE;
    return i;
}

// Shoot with nothing on the string
bool nock_intent() {
    return effective_intent() == INTENT_SHOOT && !arrow_nocked;
}

// what the current intent is called
std::string intent_label() {
    const int i = effective_intent();
    const bool unnock = i == INTENT_UNNOCK, nock = i == INTENT_SHOOT && !arrow_nocked;
    if ((unnock || nock) && weapon_loads) {
        // a crossbow is loaded, not strung
        std::lock_guard<std::mutex> lock(ammo_mutex);
        return (unnock ? "Unload " : "Load ") + ammo_word;
    }
    if (unnock)
        return "Unnock arrow";
    if (nock)
        return "Nock arrow";
    return INTENT_NAMES[i];
}

// the crosshair's picture for it
int aim_mark() {
    const int i = effective_intent();
    if (i == INTENT_INFO)
        return fpv::AIM_INFO;
    if (i == INTENT_INTERACT)
        return fpv::AIM_INTERACT;
    if (i == INTENT_WALK)
        return fpv::AIM_WALK;
    if (i == INTENT_UNNOCK)
        return fpv::AIM_UNNOCK;
    if (i == INTENT_SHOOT && !arrow_nocked)
        return fpv::AIM_NOCK;
    return i == INTENT_SHOOT ? fpv::AIM_SHOOT : i == INTENT_THROW ? fpv::AIM_THROW
         : i == INTENT_WRESTLE ? fpv::AIM_WRESTLE : fpv::AIM_STRIKE;
}

void load_intent() {
    std::ifstream in(INTENT_FILE);
    std::string word;
    if (in >> word)
        for (int i = 0; i < INTENT_COUNT; ++i)
            if (word == INTENT_NAMES[i])
                intent = i;
}

// Unnock is not remembered between sessions: it only exists while an arrow is
// on the string.
void set_intent(int next, bool tell = true) {
    intent = next;
    std::ofstream out(INTENT_FILE);
    out << INTENT_NAMES[next == INTENT_UNNOCK ? int(INTENT_SHOOT) : next] << "\n";
    if (tell)
        flash(intent_label());
}

void cycle_intent() {
    int at = 0;
    const int now = effective_intent();
    for (int i = 0; i < INTENT_COUNT; ++i)
        if (INTENT_ORDER[i] == now)
            at = i;
    int next = now;
    for (int i = 0; i < INTENT_COUNT; ++i) {
        at = (at + 1) % INTENT_COUNT;
        next = INTENT_ORDER[at];
        if (next == INTENT_SHOOT && !can_shoot)
            continue;
        if (next == INTENT_UNNOCK && !(can_shoot && arrow_nocked))
            continue;
        break;
    }
    set_intent(next);
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

// The game's own font, both of its sizes, as its pictures are (colour and
// see-through edges kept): the message panel's text is drawn with it, so that
// it looks like the game's text. Written once, before glyphs_ready is set.
fpv::GlyphFont game_glyphs[2];   // the small font, the large one
std::atomic<bool> glyphs_ready{false};

void load_game_glyphs() {
    if (glyphs_ready || !init)
        return;
    int found = 0;
    for (int f = 0; f < 2; ++f) {
        fpv::GlyphFont &font = game_glyphs[f];
        for (int ch = 0; ch < 256; ++ch) {
            const int32_t texpos =
                int32_t(f == 0 ? init->font.small_font_texpos[ch] : init->font.large_font_texpos[ch]);
            std::vector<uint32_t> px;
            int w = 0, h = 0;
            if (!texpos || !fpv::TextureStore::readNative(texpos, px, w, h))
                continue;
            if (!font.ok()) {
                font.w = w;
                font.h = h;
            }
            if (w != font.w || h != font.h)
                continue;
            // magenta is the font picture's see-through colour
            for (uint32_t &c : px)
                if (fpv::px_r(c) > 200 && fpv::px_b(c) > 200 && fpv::px_g(c) < 80)
                    c = 0;
            font.glyph[ch].swap(px);
            ++found;
        }
    }
    if (found > 64)
        glyphs_ready = true;
}

// render thread: the size of the game's font that suits cells of cw x ch
// pixels: the largest that is not taller than a cell, else the smallest
const fpv::GlyphFont *glyphs_for(int ch) {
    if (!glyphs_ready)
        return nullptr;
    const fpv::GlyphFont *best = nullptr;
    for (const fpv::GlyphFont &f : game_glyphs) {
        if (!f.ok())
            continue;
        if (!best || (f.h <= ch && (best->h > ch || f.h > best->h)) || (best->h > ch && f.h < best->h))
            best = &f;
    }
    return best;
}

// one line of the panel's text in the game's font, or in the plain copy of it
void panel_text(fpv::Canvas &cv, int x, int y, const std::string &s, uint32_t color, int cw, int ch) {
    if (const fpv::GlyphFont *g = glyphs_for(ch))
        cv.text(*g, x, y, s, color, cw, ch);
    else
        cv.text(ui_font, x, y, s, color, cw, ch);
}

// simulation thread: copy the game's interface font
void load_ui_font() {
    load_game_glyphs();
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
// Wounds in words: the hint under the pointer over a hurt body part (the
// adventurer's paperdoll, the body parts of the aiming menu). Built from the
// wound layers' flags and the body part's status, in the spirit of the game's
// Health tab: the part named in the colour the game gives it (red for lost
// function, yellow for damage, brown for the skin only, cyan for an organ that
// no longer works), what the whole part has, then each hurt tissue. The
// game's own wording is not read; the words here are taken from the flags'
// names (df.unit.xml), so they may differ from what the Health tab says.

struct TipLine {
    std::string text;
    uint32_t color = 0;
    bool operator==(const TipLine &o) const { return text == o.text && color == o.color; }
};

enum { HURT_NONE, HURT_BRUISE, HURT_CUT, HURT_GONE };

const uint32_t TIP_RED = fpv::rgb(255, 92, 80), TIP_YELLOW = fpv::rgb(255, 225, 90),
               TIP_BROWN = fpv::rgb(210, 150, 70), TIP_CYAN = fpv::rgb(90, 220, 230),
               TIP_GREY = fpv::rgb(160, 160, 166), TIP_WHITE = fpv::rgb(235, 235, 235),
               TIP_GREEN = fpv::rgb(150, 220, 110);

// how a layer of a wound marks the doll: open (a cut), blunt (a bruise), or not at all
uint8_t layer_hurt(const df::unit_wound_layerst *layer) {
    if (!layer)
        return HURT_NONE;
    const auto &f = layer->flags1.bits;
    if (f.cut || f.edged_damage || f.major_artery || f.guts_spilled || layer->bleeding > 0)
        return HURT_CUT;
    if (f.smashed || f.smashed_apart || f.tendon_bruised || f.tendon_strained || f.tendon_torn || f.ligament_bruised ||
        f.ligament_sprained || f.ligament_torn || layer->strain > 0 || layer->pain > 0 || layer->swelling > 0)
        return HURT_BRUISE;
    return HURT_NONE;
}

df::body_part_raw *body_part_of(df::unit *u, int bp) {
    if (!u || !u->body.body_plan || bp < 0 || size_t(bp) >= u->body.body_plan->body_parts.size())
        return nullptr;
    return u->body.body_plan->body_parts[bp];
}

std::string part_name(df::unit *u, int bp) {
    df::body_part_raw *part = body_part_of(u, bp);
    return part && !part->name_singular.empty() && part->name_singular[0] ? *part->name_singular[0] : std::string();
}

std::string lower_text(std::string s) {
    for (char &c : s)
        c = c == '_' ? ' ' : char(std::tolower(uint8_t(c)));
    return s;
}

// the body part of the unit with this name ("left hand"), -1 if none
int find_part(df::unit *u, const std::string &name) {
    if (!u || !u->body.body_plan || name.empty())
        return -1;
    const std::string want = lower_text(name);
    const auto &parts = u->body.body_plan->body_parts;
    for (size_t i = 0; i < parts.size(); ++i)
        if (lower_text(part_name(u, int(i))) == want)
            return int(i);
    return -1;
}

bool part_missing(df::unit *u, int bp) {
    const auto &status = u->body.components.body_part_status;
    return bp >= 0 && size_t(bp) < status.size() && status[bp].bits.missing;
}

int tip_rank(uint32_t c) {
    return c == TIP_RED ? 3 : c == TIP_YELLOW ? 2 : c == TIP_BROWN ? 1 : 0;
}

// simulation thread: the hint's lines for these body parts of the unit (parts
// with nothing wrong are left out), and the worst mark among them (HURT_*)
std::vector<TipLine> wound_tip(df::unit *u, const std::vector<int> &bps, uint8_t *worst = nullptr) {
    std::vector<TipLine> out;
    if (worst)
        *worst = HURT_NONE;
    if (!u || !u->body.body_plan)
        return out;
    const auto &status = u->body.components.body_part_status;
    const auto join = [](const std::vector<std::string> &w) {
        std::string s;
        for (const std::string &x : w)
            s += (s.empty() ? "" : ", ") + x;
        return s;
    };
    const auto capital = [](std::string s) {
        if (!s.empty())
            s[0] = char(std::toupper(uint8_t(s[0])));
        return s;
    };
    for (int bp : bps) {
        df::body_part_raw *part = body_part_of(u, bp);
        if (!part)
            continue;
        const std::string name = capital(part_name(u, bp));
        if (part_missing(u, bp)) {
            out.push_back({name + ": gone", TIP_RED});
            if (worst)
                *worst = HURT_GONE;
            continue;
        }
        df::body_part_status st;
        st.whole = size_t(bp) < status.size() ? status[bp].whole : 0;
        // what the whole part has
        std::vector<std::string> whole;
        uint32_t whole_color = TIP_GREEN;
        const auto bad = [&](const char *w, uint32_t c) {
            whole.push_back(w);
            if (tip_rank(c) > tip_rank(whole_color))
                whole_color = c;
        };
        if (st.bits.mangled)
            bad("mangled", TIP_RED);
        if (st.bits.on_fire)
            bad("on fire", TIP_RED);
        if (st.bits.spilled_guts)
            bad("guts spilled", TIP_RED);
        if (st.bits.motor_nerve_severed)
            bad("motor nerve severed", TIP_RED);
        if (st.bits.sensory_nerve_severed)
            bad("sensory nerve severed", TIP_YELLOW);
        // each tissue's words, in the order of the part's layers
        std::map<int, std::vector<std::string>> layers;
        std::map<int, uint32_t> layer_color;
        std::map<int, bool> layer_strain;
        int bleeding = 0, swelling = 0;
        bool infected = false, sutured = false, stuck = false, severed = false;
        uint8_t hurt = HURT_NONE;
        for (df::unit_wound *wound : u->body.wounds) {
            if (!wound)
                continue;
            bool here = false;
            for (df::unit_wound_layerst *layer : wound->parts) {
                if (!layer || layer->body_part_id != bp)
                    continue;
                here = true;
                hurt = std::max(hurt, layer_hurt(layer));
                bleeding += layer->bleeding;
                swelling += layer->swelling;
                const int li = layer->layer_idx;
                auto &w = layers[li];
                uint32_t &col = layer_color.emplace(li, TIP_GREY).first->second;
                const auto add = [&](const char *word, uint32_t c) {
                    if (std::find(w.begin(), w.end(), word) != w.end())
                        return;
                    w.push_back(word);
                    if (tip_rank(c) > tip_rank(col))
                        col = c;
                };
                const auto &f = layer->flags1.bits;
                // broken through: red; bent out of shape: yellow; bruised: brown
                if (f.cut) add("cut through", TIP_RED);
                if (f.edged_damage) add("cut", TIP_YELLOW);
                if (f.smashed) add("smashed open", TIP_RED);
                if (f.smashed_apart) add("crushed", TIP_YELLOW);
                if (f.edged_shake1) add("torn through", TIP_RED);
                if (f.edged_shake2) add("torn", TIP_YELLOW);
                if (f.broken) add("broken", TIP_RED);
                if (f.gouged) add("gouged", TIP_YELLOW);
                if (f.blunt_shake1) add("twisted apart", TIP_RED);
                if (f.blunt_shake2) add("twisted", TIP_YELLOW);
                if (f.joint_bend1) add("snapped", TIP_RED);
                if (f.joint_bend2) add("bent", TIP_YELLOW);
                if (f.compound_fracture) add("compound fracture", TIP_RED);
                if (f.overlapping_fracture) add("overlapping fracture", TIP_YELLOW);
                if (layer->flags2.bits.needs_setting) add("needs setting", TIP_YELLOW);
                if (f.major_artery) add("major artery opened", TIP_RED);
                else if (f.artery) add("artery opened", TIP_RED);
                if (f.tendon_torn) add("tendon torn", TIP_RED);
                else if (f.tendon_strained) add("tendon strained", TIP_YELLOW);
                else if (f.tendon_bruised) add("tendon bruised", TIP_BROWN);
                if (f.ligament_torn) add("ligament torn", TIP_RED);
                else if (f.ligament_sprained) add("ligament sprained", TIP_YELLOW);
                else if (f.ligament_bruised) add("ligament bruised", TIP_BROWN);
                if (f.scar_cut || f.scar_smashed || f.scar_edged_shake1 || f.scar_broken || f.scar_blunt_shake1 ||
                    f.scar_joint_bend1)
                    add("scarred", TIP_GREY);
                if (layer->strain > 0)
                    layer_strain[li] = true;
            }
            if (here) {
                infected = infected || wound->flags.bits.infection;
                sutured = sutured || wound->flags.bits.sutured;
                stuck = stuck || wound->flags.bits.stuck_weapon;
                severed = severed || wound->flags.bits.severed_part;
            }
        }
        if (severed)
            bad("severed", TIP_RED);
        if (bleeding > 0)
            bad("bleeding", TIP_RED);
        if (swelling > 0)
            bad("swollen", TIP_BROWN);
        if (infected)
            bad("infected", TIP_YELLOW);
        if (stuck)
            bad("something stuck in it", TIP_YELLOW);
        if (sutured)
            whole.push_back("sutured");
        if (st.bits.has_splint)
            whole.push_back("splinted");
        if (st.bits.has_bandage)
            whole.push_back("bandaged");
        if (st.bits.has_plaster_cast)
            whole.push_back("in a cast");
        // strain alone: bruised (soft tissue) or dented (bone, shell)
        for (auto &kv : layer_strain) {
            auto &w = layers[kv.first];
            bool only_scars = true;
            for (const std::string &x : w)
                only_scars = only_scars && x == "scarred";
            if (!only_scars)
                continue;
            const df::body_part_layer_raw *lr =
                kv.first >= 0 && size_t(kv.first) < part->layers.size() ? part->layers[kv.first] : nullptr;
            const std::string tissue = lr ? lr->layer_name : std::string();
            const bool hard = tissue.find("BONE") != std::string::npos || tissue.find("SHELL") != std::string::npos ||
                              tissue.find("CHITIN") != std::string::npos;
            w.insert(w.begin(), hard ? "dented" : "bruised");
            if (tip_rank(layer_color[kv.first]) < 1)
                layer_color[kv.first] = TIP_BROWN;
        }
        bool any = !whole.empty();
        for (const auto &kv : layers)
            any = any || !kv.second.empty();
        if (!any)
            continue;
        uint32_t head = TIP_WHITE;
        if (st.bits.bone_loss || st.bits.muscle_loss)
            head = TIP_RED;
        else if (st.bits.organ_loss)
            head = TIP_CYAN;
        else if (st.bits.bone_damage || st.bits.muscle_damage || st.bits.organ_damage)
            head = TIP_YELLOW;
        else if (st.bits.skin_damage)
            head = TIP_BROWN;
        out.push_back({name, head});
        if (!whole.empty())
            out.push_back({"  " + join(whole), whole_color});
        for (const auto &kv : layers) {
            if (kv.second.empty())
                continue;
            const df::body_part_layer_raw *lr =
                kv.first >= 0 && size_t(kv.first) < part->layers.size() ? part->layers[kv.first] : nullptr;
            const std::string tissue = lr ? lower_text(lr->layer_name) : std::string("tissue");
            out.push_back({"  " + tissue + ": " + join(kv.second), layer_color[kv.first]});
        }
        if (worst && hurt > *worst)
            *worst = hurt;
    }
    return out;
}

// a hurt part's mark on a doll: the cut or bruise picture, a cross for a missing part
void paint_hurt_mark(fpv::Canvas &cv, const fpv::Media *media, int x, int y, uint8_t hurt) {
    if (hurt == HURT_GONE) {
        // a missing part: crossed out
        for (int i = -4; i <= 4; ++i) {
            cv.fill(x + i - 1, y + i, 3, 1, fpv::rgb(255, 60, 50));
            cv.fill(x - i - 1, y + i, 3, 1, fpv::rgb(255, 60, 50));
        }
        return;
    }
    const fpv::Image *icon = media ? (hurt == HURT_BRUISE ? &media->wound_bruise : &media->wound_cut) : nullptr;
    if (icon && icon->ok())
        cv.blit(*icon, x - icon->w / 2, y - icon->h / 2, 1);
    else
        cv.fill(x - 3, y - 3, 7, 7, hurt == HURT_BRUISE ? fpv::rgb(170, 90, 210) : fpv::rgb(235, 40, 36));
}

// ---------------------------------------------------------------------------
// the menu: what it holds

struct MenuEntry {
    std::string name, d1, d2;       // "left hand", "Easier strike", "can't quite connect"
    uint32_t cn = 0, c1 = 0, c2 = 0;
    std::string mark;               // what the game puts at the end of the row: "+", "!"
    int slot = -1;                  // its place on the paperdoll; -1 = in the list
    uint8_t hurt = HURT_NONE;       // the target's body part: how it is marked (HURT_*)
    bool gone = false;              // the part is missing: it cannot be aimed for
    std::vector<TipLine> tip;       // its wounds, for the hint under the pointer
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
    panel_text(cv, 2 * cw, ch, m.title, UI_WHITE, cw, ch);
    if (m.kind == 1 && m.doll)
        cv.blit(m.doll->img, L.doll_x, L.doll_y, L.doll_scale);

    const auto codes = [&](const MenuEntry &e, int x, int y) {
        panel_text(cv, x, y, std::string(1, code_letter(e.d1)), e.c1, cw, ch);
        panel_text(cv, x + cw, y, std::string(1, code_letter(e.d2)), e.c2, cw, ch);
        if (!e.mark.empty())
            panel_text(cv, x + 2 * cw + 2, y, e.mark, fpv::rgb(19, 253, 101), cw, ch);
    };
    for (size_t i = 0; i < L.items.size(); ++i) {
        const MenuItem &it = L.items[i];
        const bool hot = int(i) == hover;
        if (it.kind == ITEM_DOLL) {
            const MenuEntry &e = m.entries[it.index];
            if (e.gone) {
                // nothing to hit there: no box, the word in its place
                panel_text(cv, it.x + (it.w - 4 * cw) / 2, it.y + 2, "gone", TIP_RED, cw, ch);
                continue;
            }
            cv.fill(it.x, it.y, it.w, it.h, hot ? fpv::rgb(60, 56, 30) : fpv::rgb(10, 10, 14));
            cv.frame(it.x, it.y, it.w, it.h, hot ? UI_YELLOW : fpv::rgb(96, 102, 118), 1);
            codes(e, it.x + 4, it.y + 2);
            // the part's wound, as the player's own doll marks it, just above its box
            if (e.hurt == HURT_BRUISE || e.hurt == HURT_CUT) {
                const fpv::Image &icon = e.hurt == HURT_BRUISE ? m.media->wound_bruise : m.media->wound_cut;
                const int ih = icon.ok() ? icon.h : 7;
                paint_hurt_mark(cv, m.media.get(), it.x + it.w / 2, it.y - 1 - (ih + 1) / 2, e.hurt);
            }
        } else if (it.kind == ITEM_LIST) {
            const MenuEntry &e = m.entries[it.index];
            if (hot && !e.gone)
                cv.fill(it.x - 2, it.y, it.w + 4, it.h, fpv::rgb(52, 56, 74));
            const int room = std::max(4, it.w / cw - 5);
            const bool hopeless = e.gone || e.d1.rfind("Impossible", 0) == 0;
            panel_text(cv, it.x, it.y, e.name.substr(0, size_t(room)), hopeless ? UI_DIM : e.cn, cw, ch);
            if (e.gone)
                panel_text(cv, it.x + it.w - 5 * cw, it.y, "gone", TIP_RED, cw, ch);
            else
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
            panel_text(cv, it.x + side + cw, it.y, o.label.substr(0, 16), o.on ? UI_YELLOW : UI_WHITE, cw, ch);
        } else {
            panel_text(cv, it.x, it.y, "Cancel (Esc)", hot ? UI_YELLOW : UI_DIM, cw, ch);
        }
    }

    // what the pointer is on, in full
    const int sx = 2 * cw;
    int x = sx;
    const auto put = [&](const std::string &s, uint32_t color, int y) {
        panel_text(cv, x, y, s, color, cw, ch);
        x += int(s.size()) * cw;
    };
    if (hover >= 0 && hover < int(L.items.size()) && L.items[hover].kind <= ITEM_LIST &&
        m.entries[L.items[hover].index].gone) {
        const MenuEntry &e = m.entries[L.items[hover].index];
        put(e.name, UI_DIM, L.status_y);
        put(": gone", TIP_RED, L.status_y);
    } else if (hover >= 0 && hover < int(L.items.size()) && L.items[hover].kind <= ITEM_LIST) {
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
            if (e.gone)
                continue;
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
UiTexture tex_shot[7];                      // the buttons at the pointer: quick and aimed for a shot, a throw, a strike; Dodge
constexpr int MAX_MARKS = 16;               // places on the compass at most
UiTexture tex_marks[MAX_MARKS];             // their labels
std::string tex_mark_keys[MAX_MARKS];       // what each label says
std::atomic<bool> compass_picture{false};   // the compass is drawn from the media folder
struct BodyModel;
UiTexture tex_body;                         // the adventurer's own paperdoll
UiTexture tex_bars;                         // and the square of bars beside the message list
std::string tex_bars_key;
UiTexture tex_tip;                          // the hint over a hurt body part
std::string tex_tip_key;
const BodyModel *body_drawn = nullptr;
bool body_drawn_compact = false;
uint32_t ui_media_serial = 0xFFFFFFFFu;
uint32_t label_serial = 0;
std::chrono::steady_clock::time_point label_until;
const MenuModel *menu_drawn = nullptr;
int menu_drawn_hover = -2, menu_drawn_w = 0, menu_drawn_h = 0;

void destroy_passing_bars();

void destroy_ui_textures() {
    tex_crosshair.destroy();
    for (UiTexture &t : tex_aim)
        t.destroy();
    tex_label.destroy();
    tex_menu.destroy();
    tex_compass.destroy();
    tex_compass_frame.destroy();
    for (UiTexture &t : tex_shot)
        t.destroy();
    for (int i = 0; i < MAX_MARKS; ++i) {
        tex_marks[i].destroy();
        tex_mark_keys[i].clear();
    }
    tex_body.destroy();
    body_drawn = nullptr;
    destroy_passing_bars();
    tex_bars.destroy();
    tex_bars_key.clear();
    tex_tip.destroy();
    tex_tip_key.clear();
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
    for (UiTexture &t : tex_shot)
        t.destroy();
    compass_picture = false;
    if (!m)
        return;
    for (int i = 0; i < 7; ++i)
        tex_shot[i].upload(sdl, m->shot[i]);
    compass_picture = tex_compass.upload(sdl, m->compass) && tex_compass_frame.upload(sdl, m->compass_frame);
    tex_crosshair.upload(sdl, m->crosshair);
    for (int i = 0; i < fpv::AIM_COUNT; ++i)
        tex_aim[i].upload(sdl, m->aim[i]);
}

// ---------------------------------------------------------------------------
// the adventurer's own paperdoll, in place of the portrait: marks where they
// are hurt, and bars for what wears them down

constexpr int BODY_COLS = 12, BODY_ROWS = 8;    // the portrait's place, in interface cells from the bottom left
constexpr int DOLL_ROWS = 7;                    // the doll's own height above its bars, in interface cells
constexpr int BOTTOM_ROWS = 12;                 // the row along the bottom (doll, message list, bars), in interface cells

// The bars. Under the doll, always there: the six the adventurer lives with.
// Above it, while they last: the passing states, the newest nearest the doll.
// Every bar grows as things get worse. What each counter means, its maximum and
// its stage marks are the game's own, read from the code of Dwarf Fortress
// 53.17 (see "Vitals" in AGENTS.md); adventure mode's numbers, which differ
// from fortress mode's.
enum VitalKind : uint8_t {
    V_BLEEDOUT, V_EXHAUSTION, V_PAIN, V_HUNGER, V_THIRST, V_SLEEP,
    V_STARVING, V_SUFFOCATION, V_UNCONSCIOUS, V_STUNNED, V_WEBBED, V_PARALYSIS, V_NUMBNESS, V_FEVER,
    V_NAUSEA, V_DIZZINESS, V_INFECTION,
    V_COUNT
};

struct VitalLook {
    uint32_t fill, track;
};

const VitalLook VITAL_LOOKS[V_COUNT] = {
    {fpv::rgb(226, 38, 34), fpv::rgb(64, 14, 16)},     // bleedout
    {fpv::rgb(224, 160, 32), fpv::rgb(64, 46, 10)},    // exhaustion
    {fpv::rgb(192, 60, 200), fpv::rgb(54, 18, 58)},    // pain
    {fpv::rgb(200, 120, 50), fpv::rgb(58, 34, 14)},    // hunger
    {fpv::rgb(60, 140, 230), fpv::rgb(16, 40, 66)},    // thirst
    {fpv::rgb(140, 130, 200), fpv::rgb(40, 36, 58)},   // sleepiness
    {fpv::rgb(150, 90, 30), fpv::rgb(44, 26, 8)},      // starving
    {fpv::rgb(64, 208, 224), fpv::rgb(16, 58, 64)},    // suffocation
    {fpv::rgb(150, 150, 150), fpv::rgb(40, 40, 40)},   // unconscious
    {fpv::rgb(240, 230, 140), fpv::rgb(66, 62, 36)},   // stunned
    {fpv::rgb(200, 200, 200), fpv::rgb(56, 56, 56)},   // webbed
    {fpv::rgb(80, 112, 160), fpv::rgb(22, 30, 46)},    // paralysis
    {fpv::rgb(160, 192, 224), fpv::rgb(44, 54, 64)},   // numbness
    {fpv::rgb(240, 96, 48), fpv::rgb(68, 26, 12)},     // fever
    {fpv::rgb(140, 200, 60), fpv::rgb(38, 56, 16)},    // nausea
    {fpv::rgb(60, 180, 160), fpv::rgb(16, 50, 44)},    // dizziness
    {fpv::rgb(154, 154, 48), fpv::rgb(44, 44, 12)},    // infection
};

struct Vital {
    uint8_t kind = 0;
    uint8_t icon = 0;               // its picture (fpv::Media::vital_icon): the kind, or V_COUNT for drowning
    int32_t value = 0, max = 1;
    std::string name, stage;
    int32_t marks[5] = {};
    int n_marks = 0;

    float frac() const {
        return max > 0 ? std::clamp(float(value) / float(max), 0.0f, 1.0f) : 0.0f;
    }
    // what its picture depends on
    bool looks_like(const Vital &o) const {
        return kind == o.kind && icon == o.icon && int(frac() * 1000) == int(o.frac() * 1000);
    }
};

struct BodyModel {
    std::shared_ptr<const fpv::Media> media;
    const fpv::Doll *doll = nullptr;
    std::vector<uint8_t> hurt;      // per place on the doll
    std::vector<std::vector<TipLine>> tips;   // per place on the doll: the wounds of the parts it stands for
    std::vector<Vital> fixed;       // in the bars' panel
    std::vector<Vital> passing;     // above it, the newest first
};

std::mutex body_mutex;
std::shared_ptr<const BodyModel> body_model;
std::atomic<bool> body_shown{false};

std::shared_ptr<const BodyModel> get_body() {
    std::lock_guard<std::mutex> lock(body_mutex);
    return body_model;
}

std::string with_commas(int64_t v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = int(s.size()) - 3; i > 0; i -= 3)
        s.insert(size_t(i), ",");
    return v < 0 ? "-" + s : s;
}

// simulation thread: the bars for this creature, in the order of VitalKind
// (the passing ones not yet ordered by when they came)
void read_vitals(df::unit *u, std::vector<Vital> &fixed, std::vector<Vital> &passing) {
    using cf = df::caste_raw_flags;
    const auto caste = [&](cf f) { return Units::casteFlagSet(u->race, u->caste, f); };
    const auto add = [](std::vector<Vital> &to, uint8_t kind, const char *name, int32_t value, int32_t max,
                        std::string stage, std::initializer_list<int32_t> marks) {
        Vital v;
        v.kind = kind;
        v.icon = kind;
        v.name = name;
        v.value = std::max(0, value);
        v.max = std::max(1, max);
        v.stage = std::move(stage);
        for (int32_t m : marks)
            if (v.n_marks < 5 && m > 0 && m < v.max)
                v.marks[v.n_marks++] = m;
        to.push_back(std::move(v));
    };
    const auto &c = u->counters;
    const auto &c2 = u->counters2;

    // Death at no blood left; losing blood never knocks out. The unit sheet's
    // moderate and severe blood loss: less than a half, less than a quarter left.
    if (u->body.blood_max > 0) {
        const int32_t lost = u->body.blood_max - u->body.blood_count, mx = u->body.blood_max;
        add(fixed, V_BLEEDOUT, "Bleedout", lost, mx,
            lost * 4 > mx * 3 ? "Severe blood loss" : lost * 2 > mx ? "Moderate blood loss" : "",
            {mx / 2, mx * 3 / 4});
    }
    // passes out at 6000; collapses at 4000; can't keep sprinting from 3800
    {
        const int32_t e = c2.exhaustion;
        add(fixed, V_EXHAUSTION, "Exhaustion", e, 6000,
            e >= 6000 ? "Exhausted: passes out" : e >= 4000 ? "Over-exerted: collapses"
            : e >= 3800 ? "Tired: can't sprint" : e >= 2000 ? "Tired" : "",
            {2000, 3800, 4000});
    }
    // The total is capped at 200. From 100 a failed roll on discipline,
    // willpower and focus each tick knocks out ("gives in to pain").
    if (!caste(cf::NOPAIN) && !Units::isUndead(u) && !Units::isGhost(u)) {
        const int32_t p = c.pain;
        add(fixed, V_PAIN, "Pain", p, 200,
            p >= 100 ? "Extreme pain: may pass out" : p >= 50 ? "Moderate pain" : p > 0 ? "Slight pain" : "",
            {50, 100});
    }
    // Only the adventurer gets hungry, thirsty and sleepy; one a tick (86400 a day).
    // Hunger past 5184000 burns fat, and kills once the fat is gone.
    const int32_t hunger = c2.hunger_timer;
    if (!caste(cf::NO_EAT))
        add(fixed, V_HUNGER, "Hunger", hunger, 5184000,
            hunger >= 5184000 ? "Starving: burning fat" : hunger >= 2592000 ? "Starving"
            : hunger >= 1209600 ? "Very hungry" : hunger >= 57600 ? "Hungry" : "",
            {57600, 172800, 1209600, 2592000});
    // kills at 864000
    if (!caste(cf::NO_DRINK)) {
        const int32_t t = c2.thirst_timer;
        add(fixed, V_THIRST, "Thirst", t, 864000,
            t >= 345600 ? "Dehydrated" : t >= 57600 ? "Thirsty" : "",
            {57600, 115200, 172800, 345600});
    }
    // never kills or forces sleep in adventure mode; it only lowers skills
    if (!caste(cf::NO_SLEEP)) {
        const int32_t s = c2.sleepiness_timer;
        add(fixed, V_SLEEP, "Sleepiness", s, 864000,
            s >= 864000 ? "Slumberous" : s >= 345600 ? "Very drowsy" : s >= 115200 ? "Drowsy" : "",
            {115200, 172800, 259200, 345600});
    }

    // starving: the fat there was when it began to burn, against what is left
    static int32_t starve_unit = -1, starve_fat = 0;
    if (!caste(cf::NO_EAT) && hunger >= 5184000) {
        if (starve_unit != u->id || c2.stored_fat > starve_fat) {
            starve_unit = u->id;
            starve_fat = std::max(1, c2.stored_fat);
        }
        add(passing, V_STARVING, "Starving", starve_fat - c2.stored_fat, starve_fat,
            "Fat left: " + with_commas(c2.stored_fat), {});
    } else {
        starve_unit = -1;
    }
    // +1 a tick while winded; death at 200 + (toughness + endurance) / 4
    if (c.winded > 0 || c.suffocation > 0) {
        const int32_t mx = 200 + (Units::getPhysicalAttrValue(u, df::physical_attribute_type::TOUGHNESS) +
                                  Units::getPhysicalAttrValue(u, df::physical_attribute_type::ENDURANCE)) / 4;
        const bool drowning = u->flags1.bits.drowning;
        add(passing, V_SUFFOCATION, drowning ? "Drowning" : "Suffocation", c.suffocation, mx,
            drowning ? "Drowning" : "Winded", {});
        if (drowning)
            passing.back().icon = V_COUNT;
    }
    // countdowns in ticks: a knock-out is 350..499, a blow stuns for 50..99
    if (c.unconscious > 0)
        add(passing, V_UNCONSCIOUS, "Unconscious", c.unconscious, std::max<int32_t>(500, c.unconscious),
            "Ticks left", {});
    if (c.stunned > 0)
        add(passing, V_STUNNED, "Stunned", c.stunned, std::max<int32_t>(100, c.stunned), "Ticks left", {});
    // each web adds 50..99; stuck from 10
    if (c.webbed > 0)
        add(passing, V_WEBBED, "Webbed", c.webbed, std::max<int32_t>(100, c.webbed),
            c.webbed >= 10 ? "Stuck in a web" : "Partially webbed", {10});
    // the strongest of the wounds and syndromes; full effect from 100
    if (c2.paralysis > 0)
        add(passing, V_PARALYSIS, "Paralysis", c2.paralysis, 100,
            c2.paralysis >= 100 ? "Paralyzed" : c2.paralysis >= 50 ? "Partially paralyzed" : "Sluggish", {50});
    if (c2.numbness > 0)
        add(passing, V_NUMBNESS, "Numbness", c2.numbness, 100,
            c2.numbness >= 100 ? "Completely numb: no pain" : c2.numbness >= 50 ? "Partially numb" : "Slightly numb",
            {50});
    if (c2.fever > 0)
        add(passing, V_FEVER, "Fever", c2.fever, 100,
            c2.fever >= 100 ? "Serious fever" : c2.fever >= 50 ? "Moderate fever" : "Slight fever", {50});
    // the sum over wounds; a syndrome sets 200 in adventure mode
    if (c.nausea > 0)
        add(passing, V_NAUSEA, "Nausea", c.nausea, std::max<int32_t>(200, c.nausea), "Nauseous", {});
    if (c.dizziness > 0)
        add(passing, V_DIZZINESS, "Dizziness", c.dizziness, std::max<int32_t>(200, c.dizziness), "Dizzy", {});
    // from 300 blood drains instead of coming back
    if (u->body.infection_level > 0)
        add(passing, V_INFECTION, "Infection", u->body.infection_level, 300,
            u->body.infection_level >= 300 ? "Blood draining" : "", {});
}

// the passing bars in the order they stack: one that came is put nearest the
// doll, the others keep their order
void order_passing(std::vector<Vital> &passing) {
    static std::vector<uint8_t> order;   // kinds, nearest the doll first
    std::vector<uint8_t> now;
    for (const Vital &v : passing)
        now.push_back(v.kind);
    std::vector<uint8_t> next;
    for (uint8_t k : now)
        if (std::find(order.begin(), order.end(), k) == order.end())
            next.push_back(k);
    for (uint8_t k : order)
        if (std::find(now.begin(), now.end(), k) != now.end())
            next.push_back(k);
    order = next;
    std::stable_sort(passing.begin(), passing.end(), [&](const Vital &a, const Vital &b) {
        return std::find(order.begin(), order.end(), a.kind) < std::find(order.begin(), order.end(), b.kind);
    });
}

// Where the panels go, in window pixels; shared by drawing, the hover text
// and what is kept free of the game's interface. Along the bottom, as tall as
// the message list (BOTTOM_ROWS): the doll's square on the left, the bars'
// square on the right, the message list between them; the passing bars stack
// above the bars' square. compact: the portrait's own place with only the
// bleedout bar under the doll (on the game's other screens, or with the
// setting status_bars off).
struct BodyLayout {
    int x = 0, y = 0, w = 0, h = 0;     // the doll's panel
    int doll_y = 0, doll_h = 0;         // the doll's room, from the panel's top
    int bx = 0, by = 0, bw = 0, bh = 0; // the bars' panel (bw 0: the bars are under the doll, in its panel)
    int bar_x = 0, bar_w = 0, bar_h = 0, step = 0, bars_y = 0;   // the rows of bars (icon and bar), in the panel holding them
    int rows = BODY_ROWS;               // interface rows the panels cover, from the bottom
    int cols = BODY_COLS;               // interface columns the doll's panel covers, from the left
    int bar_cols = 0;                   // and the bars' panel, from the right
    int pass_h = 0, pass_step = 0;      // a passing bar's box above the bars

    int holder_x() const { return bw > 0 ? bx : x; }
    int holder_y() const { return bw > 0 ? by : y; }
    // the row of bar i, window pixels
    SDL_Rect bar_box(int i) const { return SDL_Rect{holder_x() + bar_x, holder_y() + bars_y + i * step, bar_w, step}; }
    // the box of the passing bar in this slot (0: nearest the bars), window pixels
    SDL_Rect passing_box(int slot) const {
        return SDL_Rect{holder_x(), holder_y() - (slot + 1) * pass_step, bw > 0 ? bw : w, pass_h};
    }
};

BodyLayout body_layout(const Cells &c, int bars, bool compact) {
    BodyLayout L;
    L.bar_h = c.ch;
    L.step = L.bar_h + 3;
    L.bar_x = 6;
    L.doll_y = 5;
    if (compact) {
        L.w = BODY_COLS * c.cw - 2;
        L.bar_w = L.w - 12;
        L.h = BODY_ROWS * c.ch - 2;
        L.doll_h = std::max(8, L.h - 5 - 4 - bars * L.step - 2);
        L.bars_y = 5 + L.doll_h + 4;
        L.x = c.ox + 1;
        L.y = c.oy + c.dimy * c.ch - 1 - L.h;
        L.rows = BODY_ROWS;
        L.cols = BODY_COLS;
        L.pass_h = L.bar_h + 6;
        L.pass_step = L.pass_h + 2;
        return L;
    }
    // two squares as tall as the message list
    L.h = BOTTOM_ROWS * c.ch - 2;
    L.w = std::max(L.h, 8 * c.cw);
    L.x = c.ox + 1;
    L.y = c.oy + c.dimy * c.ch - 1 - L.h;
    L.doll_h = L.h - 10;
    L.rows = BOTTOM_ROWS;
    L.cols = (L.w + 1 + c.cw - 1) / c.cw;
    if (bars > 0) {
        L.bw = L.bh = L.h;
        L.bx = c.ox + c.dimx * c.cw - 1 - L.bw;
        L.by = L.y;
        L.bar_cols = (L.bw + 1 + c.cw - 1) / c.cw;
        // the bars spread over the square's height, each as tall as a line of text at most
        const int room = L.bh - 12;
        L.step = std::max(6, room / bars);
        L.bar_h = std::clamp(L.step - 5, 4, c.ch);
        L.bar_w = L.bw - 12;
        L.bars_y = 6 + (room - bars * L.step) / 2 + (L.step - L.bar_h) / 2;
    }
    L.pass_h = L.bar_h + 6;
    L.pass_step = L.pass_h + 2;
    return L;
}

// the interface cells the doll, the bars and the passing bars cover (over_interface, hide_portrait, the message list)
void publish_doll_rows(const BodyModel *m) {
    Cells c;
    if (!m || !get_cells(c)) {
        doll_rows = BODY_ROWS;
        doll_cols = BODY_COLS;
        bars_cols = 0;
        passing_rows = 0;
        return;
    }
    const bool compact = !settings.status_bars;
    const BodyLayout L = body_layout(c, compact ? 1 : int(m->fixed.size()), compact);
    doll_rows = L.rows;
    doll_cols = L.cols;
    bars_cols = L.bar_cols;
    const int n = compact ? 0 : int(m->passing.size());
    passing_rows = n > 0 ? (n * L.pass_step + c.ch - 1) / c.ch : 0;
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
    m->tips.assign(m->doll->slots.size(), {});
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
        for (df::unit_wound_layerst *layer : wound->parts)
            if (layer)
                mark(layer->body_part_id, layer_hurt(layer));
    }
    const auto &status = adv->body.components.body_part_status;
    for (size_t bp = 0; bp < parts.size() && bp < status.size(); ++bp)
        if (status[bp].bits.missing && slot_of[bp] >= 0)
            m->hurt[slot_of[bp]] = HURT_GONE;
    // the hover hint of each place: the wounds of every part it stands for
    {
        std::vector<std::vector<int>> parts_of(m->doll->slots.size());
        for (size_t bp = 0; bp < parts.size(); ++bp) {
            const int slot = place(int(bp));
            if (slot >= 0)
                parts_of[slot].push_back(int(bp));
        }
        for (size_t s = 0; s < parts_of.size(); ++s)
            m->tips[s] = wound_tip(adv, parts_of[s]);
    }
    read_vitals(adv, m->fixed, m->passing);
    order_passing(m->passing);
    const auto same = [](const std::vector<Vital> &a, const std::vector<Vital> &b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!a[i].looks_like(b[i]))
                return false;
        return true;
    };
    publish_doll_rows(m.get());
    std::lock_guard<std::mutex> lock(body_mutex);
    // only a new picture when something changed
    if (!body_model || body_model->doll != m->doll || body_model->hurt != m->hurt || body_model->tips != m->tips ||
        !same(body_model->fixed, m->fixed) || !same(body_model->passing, m->passing) ||
        body_model->media != m->media)
        body_model = m;
    body_shown = true;
}

// simulation thread: the hover text for the bar at these window pixels ("" if none)
std::string vital_hint(df::unit *adv, int px, int py) {
    const std::shared_ptr<const BodyModel> m = get_body();
    Cells c;
    if (!adv || !m || !settings.status_bars || !get_cells(c))
        return "";
    const BodyLayout L = body_layout(c, int(m->fixed.size()), false);
    const auto inside = [&](int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; };
    int kind = -1;
    for (size_t i = 0; i < m->fixed.size() && kind < 0; ++i) {
        const SDL_Rect r = L.bar_box(int(i));
        if (inside(r.x, r.y, r.w, r.h))
            kind = m->fixed[i].kind;
    }
    for (size_t i = 0; i < m->passing.size() && kind < 0; ++i) {
        const SDL_Rect r = L.passing_box(int(i));
        if (inside(r.x, r.y, r.w, r.h + 2))
            kind = m->passing[i].kind;
    }
    if (kind < 0)
        return "";
    // the numbers as they are now, not as last drawn
    std::vector<Vital> fixed, passing;
    read_vitals(adv, fixed, passing);
    for (const auto *list : {&fixed, &passing})
        for (const Vital &v : *list)
            if (v.kind == kind)
                return v.name + ": " + with_commas(v.value) + " / " + with_commas(v.max) +
                       (v.stage.empty() ? "" : "  (" + v.stage + ")");
    return "";
}

// simulation thread: nothing of the game is drawn where the doll and its bars
// go. full: the plain adventure screen, where nothing but the portrait, the
// game's status icons and its buttons lie there: the doll's square and a few
// rows over it (where the game puts its icons for hunger, thirst and the like,
// which the bars replace), and the bars' square with the passing bars over it.
// Otherwise only the portrait's own place, so that a dialog of the game keeps
// its cells.
void hide_portrait(bool full) {
    if (!body_shown || !gps)
        return;
    const auto blank = [](int x0, int x1, int rows) {
        for (int cx = std::max(0, x0); cx < x1 && cx < gps->dimx; ++cx)
            for (int cy = std::max(0, gps->dimy - rows); cy < gps->dimy; ++cy) {
                const int idx = cx * gps->dimy + cy;
                std::memset(&gps->screen[idx * 8], 0, 8);
                gps->screentexpos[idx] = 0;
                gps->screentexpos_lower[idx] = 0;
                gps->screentexpos_anchored[idx] = 0;
                gps->screentexpos_flag[idx] = 0;
            }
    };
    if (!full || !settings.status_bars) {
        blank(0, BODY_COLS, BODY_ROWS);
        return;
    }
    blank(0, doll_cols.load(), doll_rows.load() + 4);
    if (bars_cols.load() > 0)
        blank(gps->dimx - bars_cols.load(), gps->dimx, doll_rows.load() + passing_rows.load());
}

// simulation thread: is it the game's portrait that is drawn in that place? It
// is a picture; a panel of some other screen lying there has words in it.
bool portrait_drawn() {
    if (!gps)
        return false;
    int pictures = 0;
    for (int cx = 0; cx < BODY_COLS && cx < gps->dimx; ++cx)
        for (int cy = std::max(0, gps->dimy - BODY_ROWS); cy < gps->dimy; ++cy) {
            const int idx = cx * gps->dimy + cy;
            if (gps->screentexpos[idx] || gps->screentexpos_lower[idx] || gps->screentexpos_anchored[idx])
                ++pictures;
            else if (gps->screen[idx * 8] > ' ')
                return false;
        }
    return pictures > 0;
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

// what a journal entry is (journal.inc); a compass mark can stand for one
enum { JK_NONE, JK_QUEST, JK_SITE, JK_PLACE, JK_PERSON, JK_CIV, JK_REGION, JK_ARTIFACT, JK_RUMOR, JK_BEAST };

struct CompassMark {
    float bearing = 0;          // radians clockwise from north
    std::string what, dist;     // the two small lines under the compass
    int32_t site = -1;          // the world site it was taken for
    uint32_t color = 0;
    uint8_t glyph = 0;          // the game's symbol for it; 0 for none
    int jkind = JK_NONE;        // a journal entry tracked on the compass
    int32_t jid = -1, jsub = -1;
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

CellRect nearby_area;   // simulation thread: where the list was last found

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
    nearby_area = area;
    return out;
}

// Other screens of the adventure (inventory, journal ...) show the game's
// list too. There it is only taken off, and only where it was last found on
// the plain screen and still looks like the list (rows ending in a
// direction): a panel of that screen may lie in the same corner.
void hide_nearby_again() {
    if (!gps || nearby_area.empty())
        return;
    const int x0 = std::max(0, nearby_area.x0), x1 = std::min(nearby_area.x1, gps->dimx - 1);
    const int y0 = std::max(0, nearby_area.y0), y1 = std::min(nearby_area.y1, gps->dimy - 1);
    int rows = 0, entries = 0;
    for (int y = y0; y <= y1; ++y) {
        std::string word, last;
        for (int x = x0; x <= x1 + 1; ++x) {
            const uint8_t ch = x <= x1 ? grid_char(x, y) : 0;
            if (ch && ch != ' ') {
                word += char(ch);
            } else if (!word.empty()) {
                last = word;
                word.clear();
            }
        }
        if (last.empty())
            continue;
        ++rows;
        for (int d = 0; d < 16; ++d)
            if (last == DIR16[d]) {
                ++entries;
                break;
            }
    }
    if (entries > 0 && entries * 2 >= rows)
        blank_rect(nearby_area, CellRect());
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
    return t < 10000 ? std::to_string(t) : std::to_string((t + 500) / 1000) + "k";
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
            m.what = (e.text.empty() ? "" : e.text + " ") + it->second;
            m.dist = site_kind(p.site->type) + " " + distance_text(p.dist);
            m.site = p.site->id;
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
            same = a.what == b.what && a.dist == b.dist && a.site == b.site && a.color == b.color && a.glyph == b.glyph &&
                   a.jkind == b.jkind && a.jid == b.jid && a.jsub == b.jsub &&
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
    int x0 = tip_x0.load(), y0 = tip_y0.load(), x1 = tip_x1.load(), y1 = tip_y1.load();
    const int dimx = gps->dimx, dimy = gps->dimy;
    const int to_y = 0;
    struct Saved {
        uint8_t s[8], st[8];
        long t, l, a, ax, ay, tt, tl, ta, tax, tay;
        uint32_t f, tf;
    };
    // The box as it last was, for frames where the game draws none (while
    // walking, between turns): it stays up for a moment instead of blinking.
    static std::vector<Saved> kept;
    static int kept_w = 0, kept_h = 0, kept_dimx = 0, kept_dimy = 0, missing = 0;
    constexpr int KEEP_FRAMES = 30;
    const bool top = gps->top_in_use && gps->screen_top;
    const bool found = !(x1 < x0 || y1 < y0 || x0 < 0 || y0 < 0 || x1 >= dimx || y1 >= dimy) &&
                       x1 - x0 + 1 <= dimx - 2 && to_y + (y1 - y0 + 1) <= dimy;
    std::vector<Saved> cells;
    if (!found) {
        if (++missing > KEEP_FRAMES || kept.empty() || kept_dimx != dimx || kept_dimy != dimy ||
            !frame_pointer.over_view || !settings.game_tips)
            return;
        cells = kept;
        x0 = 0;
        y0 = to_y;
        x1 = kept_w - 1;
        y1 = to_y + kept_h - 1;
    }
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (found)
        cells.resize(size_t(w) * h);
    for (int x = 0; x < w && found; ++x)
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
    if (found) {
        kept = cells;
        kept_w = w;
        kept_h = h;
        kept_dimx = dimx;
        kept_dimy = dimy;
        missing = 0;
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

void add_tracked_marks(std::vector<CompassMark> &marks);  // journal.inc

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
        std::vector<CompassMark> marks = place_marks(entries);
        add_tracked_marks(marks);
        publish_marks(std::move(marks));
    }
    move_tip_to_corner();
}

// a row: the bar's icon on the left, as high as the bar, then the bar: its
// track, the part filled and the game's stages as lines across it
void paint_vital(fpv::Canvas &cv, int x, int by, int row_w, int bh, const Vital &v, const fpv::Media *media) {
    const VitalLook &look = VITAL_LOOKS[v.kind < V_COUNT ? v.kind : 0];
    if (media && v.icon < fpv::VITAL_ICONS && media->vital_icon[v.icon].ok())
        cv.blitFit(media->vital_icon[v.icon], x, by, bh, bh);
    const int bx = x + bh + 3, bw = std::max(8, row_w - bh - 3);
    const auto bar = [&](int width, uint32_t color) {
        for (int y = 0; y < bh; ++y) {
            // corners cut to round the ends
            const int edge = std::min(y, bh - 1 - y);
            const int cut = edge == 0 ? 2 : edge == 1 ? 1 : 0;
            if (width > 2 * cut)
                cv.fill(bx + cut, by + y, width - 2 * cut, 1, color);
        }
    };
    bar(bw, look.track);
    const int full = v.value > 0 ? std::clamp(int(bw * v.frac() + 0.5f), 5, bw) : 0;
    if (full > 0)
        bar(full, look.fill);
    for (int i = 0; i < v.n_marks; ++i) {
        const int mx = bx + int(int64_t(bw) * v.marks[i] / v.max);
        cv.fill(mx, by, 1, bh, mx < bx + full ? fpv::rgb(24, 18, 12) : fpv::rgb(235, 225, 190));
    }
}

// The passing bars, each a picture of its own over the view above the panel.
// One that comes takes the place nearest the panel and fades in while the
// others slide up; one that goes fades where it is and those above it come down.
struct PassingBar {
    uint8_t kind = 0;
    float y = 0, alpha = 0;     // pixels above the panel's top to the bar's top; 0..1
    bool live = false;
    std::string key;            // what its picture shows
    UiTexture tex;
};
std::vector<PassingBar> passing_bars;
std::chrono::steady_clock::time_point passing_time;

void destroy_passing_bars() {
    for (PassingBar &b : passing_bars)
        b.tex.destroy();
    passing_bars.clear();
}

void draw_passing(SDL_Renderer *sdl, const BodyModel *m, const BodyLayout &L, const Cells &c) {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - passing_time).count(), 0.0f, 0.1f);
    passing_time = now;
    for (PassingBar &b : passing_bars)
        b.live = false;
    for (size_t i = 0; m && i < m->passing.size(); ++i) {
        const Vital &v = m->passing[i];
        const float target = float((int(i) + 1) * L.pass_step);
        auto it = std::find_if(passing_bars.begin(), passing_bars.end(),
                               [&](const PassingBar &b) { return b.kind == v.kind; });
        if (it == passing_bars.end()) {
            passing_bars.emplace_back();
            it = std::prev(passing_bars.end());
            it->kind = v.kind;
            it->y = target;
        }
        PassingBar &b = *it;
        b.live = true;
        b.y += (target - b.y) * std::min(1.0f, dt * 10);
        b.alpha = std::min(1.0f, b.alpha + dt * 5);
        const SDL_Rect box = L.passing_box(0);
        const std::string key = std::to_string(v.icon) + "|" + std::to_string(int(v.frac() * 1000)) + "|" +
                                std::to_string(box.w) + "x" + std::to_string(L.pass_h) + "|" +
                                std::to_string(reinterpret_cast<uintptr_t>(m->media.get()));
        if (key != b.key || !b.tex.tex) {
            fpv::Canvas cv;
            cv.resize(box.w, L.pass_h);
            cv.fill(0, 0, box.w, L.pass_h, fpv::rgb(18, 20, 26));
            cv.frame(0, 0, box.w, L.pass_h, fpv::rgb(70, 52, 20), 1);
            paint_vital(cv, L.bar_x, 3, L.bar_w, L.bar_h, v, m->media.get());
            if (b.tex.upload(sdl, cv.px.data(), cv.w, cv.h))
                b.key = key;
        }
    }
    for (PassingBar &b : passing_bars)
        if (!b.live)
            b.alpha = sdl_set_texture_alpha_mod ? b.alpha - dt * 5 : 0.0f;
    for (size_t i = passing_bars.size(); i-- > 0;)
        if (!passing_bars[i].live && passing_bars[i].alpha <= 0) {
            passing_bars[i].tex.destroy();
            passing_bars.erase(passing_bars.begin() + std::ptrdiff_t(i));
        }
    for (PassingBar &b : passing_bars) {
        if (!b.tex.tex)
            continue;
        if (sdl_set_texture_alpha_mod)
            sdl_set_texture_alpha_mod(b.tex.tex, uint8_t(std::clamp(b.alpha, 0.0f, 1.0f) * 255));
        b.tex.draw(sdl, L.holder_x(), L.holder_y() - int(b.y + 0.5f));
    }
}

// Where the doll was last drawn (render thread): for the hint over a hurt part.
struct DollDrawn {
    bool ok = false;
    int x = 0, y = 0, w = 0, h = 0;     // the doll's picture, window pixels
    float scale = 1;
} doll_drawn;


// render thread. compact: the portrait's own place with only the bleedout bar
// (the game's other screens, where its panels may lie above the portrait).
void draw_body(SDL_Renderer *sdl, bool compact) {
    const std::shared_ptr<const BodyModel> m = get_body();
    Cells c;
    if (!m || !m->doll || !get_cells(c)) {
        body_drawn = nullptr;
        doll_drawn.ok = false;
        destroy_passing_bars();
        return;
    }
    compact = compact || !settings.status_bars;
    std::vector<const Vital *> bars;
    for (const Vital &v : m->fixed)
        if (!compact || v.kind == V_BLEEDOUT)
            bars.push_back(&v);
    const BodyLayout L = body_layout(c, int(bars.size()), compact);
    const int w = L.w, h = L.h;
    // the doll, as large as fits in its room
    const fpv::Image &img = m->doll->img;
    const int room_w = w - 10, room_h = L.doll_h;
    const float scale = std::min(float(room_w) / img.w, float(room_h) / img.h);
    const int dw = std::max(1, int(img.w * scale)), dh = std::max(1, int(img.h * scale));
    const int dx = (w - dw) / 2, dy = L.doll_y + (room_h - dh) / 2;
    if (m.get() != body_drawn || tex_body.w != w || tex_body.h != h || body_drawn_compact != compact) {
        fpv::Canvas cv;
        cv.resize(w, h);
        cv.fill(0, 0, w, h, fpv::rgb(18, 20, 26));
        cv.frame(0, 0, w, h, UI_GOLD, 2);
        cv.frame(2, 2, w - 4, h - 4, fpv::rgb(70, 52, 20), 1);
        if (L.bw == 0)
            for (size_t i = 0; i < bars.size(); ++i)
                paint_vital(cv, L.bar_x, L.bars_y + int(i) * L.step, L.bar_w, L.bar_h, *bars[i], m->media.get());
        cv.blitFit(img, dx, dy, dw, dh);
        for (size_t s = 0; s < m->hurt.size() && s < m->doll->slots.size(); ++s)
            if (m->hurt[s])
                paint_hurt_mark(cv, m->media.get(), dx + int(m->doll->slots[s].x * scale),
                                dy + int(m->doll->slots[s].y * scale), m->hurt[s]);
        if (!tex_body.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        body_drawn = m.get();
        body_drawn_compact = compact;
        tex_bars_key.clear();
    }
    tex_body.draw(sdl, L.x, L.y);
    doll_drawn = DollDrawn{true, L.x + dx, L.y + dy, dw, dh, scale};
    if (L.bw > 0) {
        const std::string key = std::to_string(reinterpret_cast<uintptr_t>(m.get())) + "|" + std::to_string(L.bw) + "x" +
                                std::to_string(L.bh);
        if (key != tex_bars_key || !tex_bars.tex) {
            fpv::Canvas cv;
            cv.resize(L.bw, L.bh);
            cv.fill(0, 0, L.bw, L.bh, fpv::rgb(18, 20, 26));
            cv.frame(0, 0, L.bw, L.bh, UI_GOLD, 2);
            cv.frame(2, 2, L.bw - 4, L.bh - 4, fpv::rgb(70, 52, 20), 1);
            for (size_t i = 0; i < bars.size(); ++i)
                paint_vital(cv, L.bar_x, L.bars_y + int(i) * L.step, L.bar_w, L.bar_h, *bars[i], m->media.get());
            if (tex_bars.upload(sdl, cv.px.data(), cv.w, cv.h))
                tex_bars_key = key;
        }
        tex_bars.draw(sdl, L.bx, L.by);
    }
    draw_passing(sdl, compact ? nullptr : m.get(), L, c);
}

// ---------------------------------------------------------------------------
// The hint under the pointer over a hurt body part: on the adventurer's doll,
// and in the aiming menu (render thread, over the whole interface).

void draw_tip_lines(SDL_Renderer *sdl, const std::vector<TipLine> &lines, int px, int py) {
    Cells c;
    if (lines.empty() || !ui_font_ready || !get_cells(c))
        return;
    const int cw = c.cw, ch = c.ch;
    std::string key = std::to_string(cw) + "x" + std::to_string(ch);
    size_t longest = 0;
    for (const TipLine &l : lines) {
        key += "|" + l.text + "#" + std::to_string(l.color);
        longest = std::max(longest, l.text.size());
    }
    if (key != tex_tip_key || !tex_tip.tex) {
        const int W = int(longest) * cw + 12, H = int(lines.size()) * ch + 8;
        fpv::Canvas cv;
        cv.resize(W, H);
        cv.fill(0, 0, W, H, fpv::rgb(12, 12, 16));
        cv.frame(0, 0, W, H, UI_GOLD, 1);
        for (size_t i = 0; i < lines.size(); ++i)
            panel_text(cv, 6, 4 + int(i) * ch, lines[i].text, lines[i].color, cw, ch);
        if (!tex_tip.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        tex_tip_key = key;
    }
    // under the pointer, kept inside the window (above it where there is no room below)
    const int right = c.ox + c.dimx * cw, bottom = c.oy + c.dimy * ch;
    int x = px + 14, y = py + 18;
    if (x + tex_tip.w > right)
        x = std::max(c.ox, right - tex_tip.w);
    if (y + tex_tip.h > bottom)
        y = std::max(c.oy, py - 6 - tex_tip.h);
    tex_tip.draw(sdl, x, y);
}

void draw_hover_tips(SDL_Renderer *sdl) {
    const bool doll_ok = doll_drawn.ok;
    doll_drawn.ok = false;      // drawn again by the next frame's draw_body, or not there
    int px = -1, py = -1;
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    if (px < 0 || py < 0)
        return;
    if (const std::shared_ptr<const MenuModel> m = get_menu()) {
        MenuLayout L;
        if (m->kind == 1 && layout_menu(*m, L) && L.contains(px, py)) {
            const int hit = L.at(px, py);
            if (hit >= 0 && (L.items[hit].kind == ITEM_DOLL || L.items[hit].kind == ITEM_LIST)) {
                const MenuEntry &e = m->entries[L.items[hit].index];
                draw_tip_lines(sdl, e.tip, px, py);
            }
        }
        return;
    }
    const std::shared_ptr<const BodyModel> b = get_body();
    if (!doll_ok || !b || !b->doll || b->tips.size() < b->doll->slots.size())
        return;
    const DollDrawn &d = doll_drawn;
    const int reach = std::max(10, d.w / 5);
    if (px < d.x - reach || py < d.y - reach || px >= d.x + d.w + reach || py >= d.y + d.h + reach)
        return;
    // the nearest place with something wrong
    int best = -1;
    int64_t best_d = int64_t(reach) * reach;
    for (size_t s = 0; s < b->doll->slots.size(); ++s) {
        if (b->tips[s].empty())
            continue;
        const int sx = d.x + int(b->doll->slots[s].x * d.scale), sy = d.y + int(b->doll->slots[s].y * d.scale);
        const int64_t dd = int64_t(px - sx) * (px - sx) + int64_t(py - sy) * (py - sy);
        if (dd <= best_d) {
            best_d = dd;
            best = int(s);
        }
    }
    if (best >= 0)
        draw_tip_lines(sdl, b->tips[best], px, py);
}

// The compass at the top: a strip holding the whole circle, N in its first
// eighth, slid so that where the camera looks is under the frame's marker.
// It is drawn 2 2/3 times as wide as the frame picture (short of the
// minimap), the frame stretched between its ends and its middle, and the
// strip 1 1/3 times as wide, so that most of the circle shows. The places from the
// game's list of nearby places sit on it where they lie, each named in two
// small lines under it (what it is, how far); one behind you is held at the
// nearer end.
constexpr int COMPASS_NUM = 4, COMPASS_DEN = 3;   // how much wider the strip is drawn: 4/3

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
    // both lines in the middle of the label
    int x = (cv.w - int(first) * cw) / 2;
    if (m.glyph) {
        cv.text(ui_font, x, 2, std::string(1, char(m.glyph)), color, cw, ch);
        x += 2 * cw;
    }
    cv.text(ui_font, x, 2, m.what, UI_WHITE, cw, ch);
    if (!m.dist.empty())
        cv.text(ui_font, (cv.w - int(m.dist.size()) * cw) / 2, 2 + ch, m.dist, UI_DIM, cw, ch);
    tex_marks[i].upload(sdl, cv.px.data(), cv.w, cv.h);
}

// where the labels of the places were drawn last (window pixels), for clicks
struct MarkSpot {
    SDL_Rect r;
    int32_t site;
    int jkind = JK_NONE;
    int32_t jid = -1, jsub = -1;
};
std::mutex mark_spots_mutex;
std::vector<MarkSpot> mark_spots;

// The castle button right of the compass: the game's nearby places on the
// compass, on or off (tracked journal entries stay). Kept in a file of its own
// so it survives the game being closed.
const char *const PLACES_FILE = "dfhack-config/fpview-compass-places.txt";
std::atomic<bool> compass_places{true};
std::atomic<int> places_btn_x{-1}, places_btn_y{0}, places_btn_s{0};  // window pixels, where it was drawn

void load_compass_places() {
    std::ifstream f(PLACES_FILE);
    int v = 1;
    if (f >> v)
        compass_places = v != 0;
}

void toggle_compass_places() {
    compass_places = !compass_places.load();
    std::ofstream f(PLACES_FILE, std::ios::trunc);
    f << (compass_places.load() ? 1 : 0) << '\n';
}

bool places_button_at(int px, int py) {
    const int x = places_btn_x.load(), y = places_btn_y.load(), s = places_btn_s.load();
    return settings.compass && x >= 0 && px >= x && py >= y && px < x + s && py < y + s;
}

// the built-in castle (16 x 16): '#' wall, '+' lit edge, '.' see-through
const char *const CASTLE_ROWS[16] = {
    "................",
    ".#.#........#.#.",
    ".###...#.#..###.",
    ".###...###..###.",
    ".###...###..###.",
    ".#######+########",
    ".##+##+###+##+##",
    ".###############",
    ".####.......####",
    ".####..###..####",
    ".####.#####.####",
    ".####.#####.####",
    ".####.#####.####",
    ".####.#####.####",
    ".###############",
    "................",
};

// the game's own castle (WORLD_MAP_DETAILS SITE_CASTLE), read once on the simulation thread
std::mutex castle_mutex;
std::shared_ptr<const fpv::Image> castle_game_icon;
bool castle_game_icon_tried = false;
std::atomic<uint32_t> castle_serial{0};
void set_castle_game_icon(std::shared_ptr<const fpv::Image> img) {
    std::lock_guard<std::mutex> lock(castle_mutex);
    castle_game_icon = std::move(img);
    ++castle_serial;
}

fpv::Image castle_picture(const fpv::Media *media) {
    if (media && media->compass_places.ok())
        return media->compass_places;
    {
        std::lock_guard<std::mutex> lock(castle_mutex);
        if (castle_game_icon && castle_game_icon->ok())
            return *castle_game_icon;
    }
    fpv::Image img;
    img.w = img.h = 16;
    img.px.assign(256, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16 && CASTLE_ROWS[y][x]; ++x) {
            const char c = CASTLE_ROWS[y][x];
            if (c == '#')
                img.px[y * 16 + x] = fpv::rgb(206, 170, 96);
            else if (c == '+')
                img.px[y * 16 + x] = fpv::rgb(250, 226, 150);
        }
    return img;
}

UiTexture tex_places_btn;
uint32_t places_btn_serial = 0xFFFFFFFFu;
int places_btn_drawn = -1;

void draw_places_button(SDL_Renderer *sdl, int x, int y, int s) {
    const uint32_t serial = media_serial.load() * 31u + castle_serial.load();
    const int state = compass_places.load() ? 1 : 0;
    if (serial != places_btn_serial || state != places_btn_drawn || tex_places_btn.w != s) {
        const std::shared_ptr<const fpv::Media> m = get_media();
        const fpv::Image icon = castle_picture(m.get());
        fpv::Canvas cv;
        cv.resize(s, s);
        cv.fill(0, 0, s, s, 0xD0000000u | (fpv::rgb(16, 14, 12) & 0x00FFFFFFu));
        cv.frame(0, 0, s, s, state ? UI_GOLD : fpv::rgb(110, 90, 50), 1);
        cv.frame(2, 2, s - 4, s - 4, state ? fpv::rgb(120, 84, 24) : fpv::rgb(50, 42, 30), 1);
        const int pad = std::max(4, s / 6);
        fpv::Image shown = icon;
        if (!state)
            for (uint32_t &c : shown.px)
                if (fpv::px_a(c))
                    c = (c & 0xFF000000u) | (fpv::rgb(fpv::px_r(c) / 3 + 30, fpv::px_g(c) / 3 + 30, fpv::px_b(c) / 3 + 30) & 0x00FFFFFFu);
        cv.blitFit(shown, pad, pad, s - 2 * pad, s - 2 * pad);
        if (!state)  // struck through while off
            for (int i = pad; i < s - pad; ++i)
                cv.fill(i, s - 1 - i, 2, 2, fpv::rgb(170, 60, 40));
        tex_places_btn.upload(sdl, cv.px.data(), cv.w, cv.h);
        places_btn_serial = serial;
        places_btn_drawn = state;
    }
    tex_places_btn.draw(sdl, x, y);
    places_btn_x = x;
    places_btn_y = y;
    places_btn_s = s;
}

// the world site whose label is at this pixel, or -1
int32_t mark_site_at(int px, int py) {
    if (!settings.compass || !settings.compass_markers)
        return -1;
    std::lock_guard<std::mutex> lock(mark_spots_mutex);
    for (const MarkSpot &s : mark_spots)
        if (px >= s.r.x && px < s.r.x + s.r.w && py >= s.r.y && py < s.r.y + s.r.h)
            return s.site;
    return -1;
}

// the journal entry whose label is at this pixel (a place's label: its site)
bool mark_entry_at(int px, int py, int &kind, int32_t &id, int32_t &sub) {
    if (!settings.compass || !settings.compass_markers)
        return false;
    std::lock_guard<std::mutex> lock(mark_spots_mutex);
    for (const MarkSpot &s : mark_spots)
        if (px >= s.r.x && px < s.r.x + s.r.w && py >= s.r.y && py < s.r.y + s.r.h) {
            kind = s.jkind != JK_NONE ? s.jkind : JK_SITE;
            id = s.jkind != JK_NONE ? s.jid : s.site;
            sub = s.jkind != JK_NONE ? s.jsub : -1;
            return true;
        }
    return false;
}

// A click on a label opens the game's journal (the key for it was sent); once
// that screen is up, it is turned to its list of sites and the place is
// selected there. A place the adventurer has not heard of is not in that
// list: then the journal just stays open.
std::atomic<int32_t> journal_site{-1};
int journal_tries = 0;

void journal_step() {
    const int32_t id = journal_site.load();
    if (id < 0)
        return;
    if (--journal_tries < 0) {
        journal_site = -1;
        return;
    }
    auto log = strict_virtual_cast<df::viewscreen_adventure_logst>(Gui::getCurViewscreen(true));
    if (!log)
        return;
    journal_site = -1;
    if (log->mode != df::adventure_log_mode::SITES) {
        std::set<df::interface_key> keys{df::interface_key::A_LOG_SITES};
        log->feed(&keys);
    }
    for (size_t i = 0; i < log->site_entry.size(); ++i) {
        const df::site_entryst *e = log->site_entry[i];
        if (!e || !e->st || e->st->id != id)
            continue;
        log->selected = int32_t(i);
        log->scroll_position_sites = std::max(0, int32_t(i) - 2);
        log->site_text_scroll = 0;
        break;
    }
}

void draw_compass(SDL_Renderer *sdl, int out_w, float angle) {
    compass_px1 = -1;
    places_btn_x = -1;
    std::vector<MarkSpot> spots;
    struct PutSpots {
        std::vector<MarkSpot> &v;
        ~PutSpots() {
            std::lock_guard<std::mutex> lock(mark_spots_mutex);
            mark_spots.swap(v);
        }
    } put_spots{spots};
    if (!tex_compass.tex || !tex_compass_frame.tex)
        return;
    const int fw = tex_compass_frame.w, fh = tex_compass_frame.h;
    const int sw0 = tex_compass.w, sh = tex_compass.h, sw = sw0 * COMPASS_NUM / COMPASS_DEN;
    int room = out_w - 16;
    SDL_Rect mini;
    if (minimap_rect(mini))
        room = std::min(room, 2 * (mini.x - 12) - out_w);
    int W = std::max(fw, std::min(2 * fw * COMPASS_NUM / COMPASS_DEN, room));
    int win_w = std::min(W - 8, sw);
    win_w -= win_w % COMPASS_NUM;
    W = std::max(fw, win_w + 8);
    const int win_h = std::min(fh - 8, sh);
    const int x0 = out_w / 2 - W / 2, y0 = 2;
    const int wx = x0 + (W - win_w) / 2, wy = y0 + (fh - win_h) / 2;
    float turn = angle / 6.2831853f;
    turn -= std::floor(turn);
    // strip pixel under the marker, then the left edge of what shows (in the picture's own pixels)
    int left = int(std::floor(turn * sw0 + sw0 / 16.0f - win_w * COMPASS_DEN / (2.0f * COMPASS_NUM)));
    left = ((left % sw0) + sw0) % sw0;
    const int src_w = win_w / COMPASS_NUM * COMPASS_DEN;
    int done = 0;
    while (done < src_w) {
        const int from = (left + done) % sw0;
        const int n = std::min(src_w - done, sw0 - from);
        const SDL_Rect src = {from, 0, n, win_h};
        const int d0 = done * COMPASS_NUM / COMPASS_DEN, d1 = (done + n) * COMPASS_NUM / COMPASS_DEN;
        const SDL_Rect dst = {wx + d0, wy, d1 - d0, win_h};
        sdl_render_copy(sdl, tex_compass.tex, &src, &dst);
        done += n;
    }
    draw_frame_wide(sdl, x0, y0, W);
    int bottom = y0 + fh;
    draw_places_button(sdl, x0 + W + 4, y0, std::max(16, fh));

    const std::shared_ptr<const std::vector<CompassMark>> marks = get_marks();
    if (marks && settings.compass_markers && ui_font_ready) {
        struct Placed {
            int x, i;
            bool held;
        };
        std::vector<Placed> placed;
        const int centre = wx + win_w / 2, half = win_w / 2 - 4;
        for (int i = 0; i < int(marks->size()) && i < MAX_MARKS; ++i) {
            const CompassMark &m = (*marks)[i];
            if (!compass_places && m.jkind == JK_NONE)
                continue;
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
                if (m.site >= 0 || m.jkind != JK_NONE)
                    spots.push_back({SDL_Rect{lx, ly, t.w, t.h}, m.site, m.jkind, m.jid, m.jsub});
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

// ---------------------------------------------------------------------------
// Shooting or throwing at a creature: the game asks how (quickly, or aiming
// carefully) in a list that also names everyone else on that tile. Two buttons
// at the pointer stand in for it, for the creature that was clicked: Quick
// shot (throw) just above the pointer, Aimed just below. The game's list stays open
// underneath, emptied of its cells (see drive_ranged, shot_input).
// Striking a creature is asked the same way: the game's list for it has
// "Attack" (a strike at once) and "Combat" (which goes on to where to hit).
// While someone is striking at you a third button, Dodge, is under those two:
// it goes into the game's "Combat" entry and picks dodging there.

struct ShotChoice {
    std::atomic<bool> on{false};
    std::atomic<int> kind{0};        // 0 a shot, 1 a throw, 2 a strike: the buttons say so
    std::atomic<bool> dodge{false};  // a third button, Dodge, is offered
    std::atomic<bool> quick{true};   // the Quick button is offered (a strike may only have "Combat")
    std::atomic<int> px{0}, py{0};   // the pointer when the shot was asked for, in window pixels
} shot_choice;

const int SHOT_DODGE = 6;       // the Dodge button's picture and label
const char *const SHOT_LABELS[7] = {"Quick shot",   "Aimed shot",   "Quick throw", "Aimed throw",
                                    "Quick strike", "Aimed strike", "Dodge"};

// which picture button i has: 0 is quick, 1 aimed, 2 dodge
int shot_picture(int i) {
    return i == 2 ? SHOT_DODGE : i + shot_choice.kind.load() * 2;
}

// where the buttons are, in window pixels: shared by drawing and clicking.
// Returns how many there are (0: none are up).
int shot_buttons(SDL_Rect out[3]) {
    Cells c;
    if (!shot_choice.on || !get_cells(c))
        return 0;
    const std::shared_ptr<const fpv::Media> m = get_media();
    const int gap = std::max(4, c.ch / 2);
    const int px = shot_choice.px.load(), py = shot_choice.py.load();
    const int count = shot_choice.dodge ? 3 : 2;
    for (int i = 0; i < count; ++i) {
        // a picture is made for interface cells of 8 x 12 and grows with them
        int w = 12 * c.cw, h = 2 * c.ch;
        const int k = shot_picture(i);
        if (m && m->shot[k].ok()) {
            w = m->shot[k].w * c.cw / 8;
            h = m->shot[k].h * c.ch / 12;
        }
        const int x = std::clamp(px - w / 2, c.ox, std::max(c.ox, c.ox + c.dimx * c.cw - w));
        // quick above the pointer, aimed below it, dodge below that
        const int y = i == 0 ? py - gap - h : i == 1 ? py + gap : out[1].y + out[1].h + gap;
        out[i] = {x, y, w, h};
    }
    if (!shot_choice.quick)
        out[0] = {px, py, 0, 0};    // not offered: kept in place for the others' layout, but never drawn or hit
    return count;
}

// 0 quick, 1 aimed, 2 dodge, -1 none
int shot_button_at(int px, int py) {
    SDL_Rect r[3];
    const int count = shot_buttons(r);
    for (int i = 0; i < count; ++i)
        if (px >= r[i].x && px < r[i].x + r[i].w && py >= r[i].y && py < r[i].y + r[i].h)
            return i;
    return -1;
}

void draw_shot_buttons(SDL_Renderer *sdl) {
    SDL_Rect r[3];
    const int count = shot_buttons(r);
    if (!count)
        return;
    int px = -1, py = -1;
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    const int hover = shot_button_at(px, py);
    for (int i = 0; i < count; ++i) {
        if (r[i].w <= 0)
            continue;
        const int k = shot_picture(i);
        if (!tex_shot[k].tex && ui_font_ready) {
            // no picture in the media folder: a plain framed label
            const std::string text = SHOT_LABELS[k];
            fpv::Canvas cv;
            cv.resize(96, 24);
            cv.fill(0, 0, cv.w, cv.h, fpv::rgb(38, 34, 30));
            cv.frame(0, 0, cv.w, cv.h, fpv::rgb(16, 14, 20), 1);
            cv.frame(1, 1, cv.w - 2, cv.h - 2, UI_GOLD, 1);
            cv.text(ui_font, (cv.w - int(text.size()) * 8) / 2, 6, text, UI_WHITE, 8, 12);
            tex_shot[k].upload(sdl, cv.px.data(), cv.w, cv.h);
        }
        if (!tex_shot[k].tex)
            continue;
        sdl_render_copy(sdl, tex_shot[k].tex, nullptr, &r[i]);
        if (i == hover) {
            // the one the pointer is on is lit
            Uint8 cr = 0, cg = 0, cb = 0, ca = 255;
            sdl_get_render_draw_color(sdl, &cr, &cg, &cb, &ca);
            SDL_BlendMode blend_was = SDL_BLENDMODE_NONE;
            sdl_get_render_draw_blend_mode(sdl, &blend_was);
            sdl_set_render_draw_blend_mode(sdl, SDL_BLENDMODE_BLEND);
            sdl_set_render_draw_color(sdl, 255, 255, 255, 60);
            sdl_render_fill_rect(sdl, &r[i]);
            sdl_set_render_draw_blend_mode(sdl, blend_was);
            sdl_set_render_draw_color(sdl, cr, cg, cb, ca);
        }
    }
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
            // the creature aimed at: each listed part's wounds, and whether it is there at all
            df::unit *target = ranged ? game->main_interface.adventure.aim_projectile.target_unit
                                      : (atk.attack_unit ? atk.attack_unit : atk.confirm_unit);
            for (MenuEntry &e : m->entries) {
                const int bp = find_part(target, e.name);
                if (bp < 0)
                    continue;
                e.gone = part_missing(target, bp);
                e.tip = wound_tip(target, {bp}, &e.hurt);
            }
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

// the message panel's button pictures when media has none (same as tools/make_media.py)
const char *const MSG_ICON_ROWS[5][12] = {
    {"............##..", "...........###..", "..........###...", ".........###....", "........###.....", ".......###......", "..oo..###.......", "...oo###........", "....ooo.........", "...ooooo........", "..oo...oo.......", ".oo............."},
    {"................", "..###########...", ".#...........#..", ".#.##.###.##.#..", ".#...........#..", ".#.####.###..#..", ".#...........#..", "..####..######..", ".....#.#........", ".....##.........", ".....#..........", "................"},
    {"................", "..############..", "..#..........#..", "..#..........#..", "..############..", "..#....oo....#..", "..#....oo....#..", "..#..........#..", "..#..........#..", "..############..", "................", "................"},
    {"................", "......####......", "......####......", "......####......", "......####......", "..############..", "...##########...", "....########....", ".....######.....", "......####......", ".......##.......", "................"},
    {"................", ".......##.......", "......####......", ".....######.....", "....########....", "...##########...", "..############..", "......####......", "......####......", "......####......", "......####......", "................"},
};

// ---------------------------------------------------------------------------
// The message list in the bottom right corner: every message of the
// adventure, newest at the bottom, on a see-through background. Drawn here
// in pixels (an interface cell always has a solid background). Buttons at its
// top right show or hide combat, talk and everything else, and fold it away:
// then only the newest message shows, with a button to unfold it.

enum { MSG_COMBAT, MSG_TALK, MSG_OTHER };
constexpr int MSG_ICON_COLS = 3;   // cells before a message's text: a two-cell icon and a gap

struct MsgLine {
    std::string text;
    uint32_t color;
    int kind = -1;    // MSG_COMBAT/TALK/OTHER on a message's first line, -1 on the lines after it
    int entry = 0;    // which message the line belongs to (lines of one message share it)
};
struct MsgModel {
    std::vector<MsgLine> lines;    // shown kinds, wrapped to the panel
    std::vector<MsgLine> latest;   // the newest shown message, wrapped for the folded panel
    int width = 0;                 // characters a line holds
};
std::mutex msg_mutex;
std::shared_ptr<const MsgModel> msg_model;
std::atomic<bool> msg_show[3] = {true, true, true};
std::atomic<bool> msg_folded{false};
std::atomic<int> msg_top{0};          // first line shown
std::atomic<bool> msg_follow{true};   // kept at the newest
std::atomic<int> msg_page{8};         // lines that fit
std::atomic<int> msg_shown_version{0};
std::atomic<bool> adv_sneaking{false};   // the adventurer sneaks: the speed at the top of the list goes purple
std::atomic<bool> adv_prone{false};      // the adventurer lies down (the ring's stance picture)

constexpr int MSG_KEPT = 200;   // messages listed at most
constexpr int MSG_ROWS = BOTTOM_ROWS;    // the panel's height, in interface cells

// the panel's left and right ends, window pixels: all the room between the
// doll's square and the bars' square (and right of the game's action bar,
// where that is moved down beside the portrait without the ring)
bool msg_span(const Cells &c, int &x0, int &x1) {
    x0 = c.ox + c.cw;
    x1 = c.ox + c.dimx * c.cw - c.cw;
    if (body_shown)
        if (const std::shared_ptr<const BodyModel> m = get_body()) {
            const bool compact = !settings.status_bars;
            const BodyLayout L = body_layout(c, compact ? 1 : int(m->fixed.size()), compact);
            x0 = L.x + L.w + 3;
            if (L.bw > 0)
                x1 = L.bx - 3;
        }
    const int bar_right = moved_bar.live.load() ? moved_bar.x1.load() : -1;
    if (bar_right > 0 && bar_right < c.dimx - 24)
        x0 = std::max(x0, c.ox + (bar_right + 1) * c.cw);
    return x1 - x0 >= 20 * c.cw;
}

std::vector<std::string> msg_wrap(const std::string &text, int width) {
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ')
            ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ')
            ++j;
        std::string word = text.substr(i, j - i);
        i = j;
        if (word.empty())
            break;
        while (int(word.size()) > width) {
            if (!line.empty()) {
                out.push_back(line);
                line.clear();
            }
            out.push_back(word.substr(0, size_t(width)));
            word = word.substr(size_t(width));
        }
        if (line.empty())
            line = word;
        else if (int(line.size() + 1 + word.size()) <= width)
            line += " " + word;
        else {
            out.push_back(line);
            line = word;
        }
    }
    if (!line.empty() || out.empty())
        out.push_back(line);
    return out;
}

// A message the game puts in a box with an "Okay" button: its kind of
// announcement is set to be shown that way, or it was in such a box this
// session (dismiss_popups). In the list it is talk, in gold.
bool msg_boxed(const df::report *r, const std::string &whole) {
    const df::d_init *init_d = df::global::d_init;
    const int type = int(r->type);
    if (init_d && type >= 0 &&
        type < int(sizeof(init_d->announcements.flags) / sizeof(init_d->announcements.flags[0])) &&
        init_d->announcements.flags[type].bits.DO_MEGA)
        return true;
    const std::string text = squeeze_spaces(whole);
    if (text.size() < 8)
        return false;
    for (const std::string &p : popup_texts) {
        const size_t n = std::min({p.size(), text.size(), size_t(48)});
        if (n >= 8 && p.compare(0, n, text, 0, n) == 0)
            return true;
    }
    return false;
}

// what kind of message a report is: talk has a speaker, combat is a combat
// report or one in a creature's combat (sparring, hunting) log
int msg_kind(const df::report *r, const std::unordered_set<int32_t> &fight_ids) {
    if (r->speaker_id >= 0)
        return MSG_TALK;
    const std::string type = ENUM_KEY_STR(announcement_type, r->type);
    // fighting that is not called COMBAT_ (falling over, a grip let go, rage gone ...)
    static const std::unordered_set<std::string> fight_types = {
        "ADV_CREATURE_DEATH", "FALL_OVER", "STAND_UP", "CANNOT_STAND", "BREAK_GRIP", "NO_BREAK_GRIP",
        "LOSE_EMOTION", "MOVED_OUT_OF_RANGE", "YOU_YIELDED", "YOU_UNYIELDED", "NO_INV_TO_THROW", "NO_INV_TO_FIRE",
        "UNIT_PROJECTILE_SLAM", "UNIT_PROJECTILE_SLAM_INTO_UNIT", "UNIT_PROJECTILE_SLAM_BLOW_APART", "PARALYZED",
        "OVERCOME_PARALYSIS", "NOT_STUNNED", "PAIN_KO", "CAUGHT_IN_WEB", "FREE_FROM_WEB", "SHOOT_WEB", "TRIGGER_WEB",
        "CAUGHT_IN_FLAMES", "FLAME_HIT", "DODGE_FLYING_OBJECT", "GHOST_ATTACK", "UNDEAD_ATTACK", "COUGH_BLOOD",
        "VOMIT_BLOOD", "VERMIN_BITE", "YOU_FEED_ON_SUCKEE", "BANDIT_GRAB_ITEM", "BLOCK_FIRE"};
    if (type.rfind("COMBAT_", 0) == 0 || type.rfind("AMBUSH_", 0) == 0 || fight_types.count(type) ||
        fight_ids.count(r->id))
        return MSG_COMBAT;
    if (type.find("CONVERSATION") != std::string::npos)
        return MSG_TALK;
    return MSG_OTHER;
}

// simulation thread, each frame: the lines again when anything they depend on changed
void update_messages() {
    static int seen_count = -1, seen_last = -2, seen_repeat = -1, seen_width = -1, seen_filter = -1;
    static int seen_popups = -1;
    if (!world || !gps)
        return;
    Cells c;
    int x0, x1;
    if (!get_cells(c) || !msg_span(c, x0, x1))
        return;
    const int width = (x1 - x0) / c.cw - 4;   // a margin, and the scrollbar
    const auto &list = world->status.announcements;
    const int count = int(list.size());
    const int last = count ? list[count - 1]->id : -1;
    const int repeat = count ? list[count - 1]->repeat_count : 0;
    const int filter = (msg_show[0] ? 1 : 0) | (msg_show[1] ? 2 : 0) | (msg_show[2] ? 4 : 0);
    if (count == seen_count && last == seen_last && repeat == seen_repeat && width == seen_width &&
        filter == seen_filter && popup_serial == seen_popups)
        return;
    seen_popups = popup_serial;
    const bool news = last != seen_last || repeat != seen_repeat;
    seen_count = count;
    seen_last = last;
    seen_repeat = repeat;
    seen_width = width;
    seen_filter = filter;

    std::unordered_set<int32_t> fight_ids;
    for (const df::unit *u : world->units.active)
        if (u)
            for (int k = 0; k < 3; ++k)
                for (const int32_t id : u->reports.log[k])
                    fight_ids.insert(id);

    struct Entry {
        std::string text;
        uint32_t color;
        int kind;
        const df::report *first;
    };
    std::vector<Entry> entries;
    for (int i = std::max(0, count - MSG_KEPT); i < count; ++i) {
        const df::report *r = list[i];
        if (!r)
            continue;
        std::string text = r->text;   // in the game's own characters, as its font has them
        if (r->repeat_count > 0)
            text += " x" + std::to_string(r->repeat_count + 1);
        if (r->flags.bits.continuation && !entries.empty()) {
            entries.back().text += " " + text;
            continue;
        }
        int fg = (r->color & 7) + (r->bright ? 8 : 0);
        if (fg == 0)
            fg = 8;
        const uint32_t color = fpv::rgb(gps->uccolor[fg][0], gps->uccolor[fg][1], gps->uccolor[fg][2]);
        entries.push_back(Entry{text, color, msg_kind(r, fight_ids), r});
    }
    // once a message is whole (its further lines joined on)
    for (Entry &e : entries)
        if (e.kind == MSG_OTHER && msg_boxed(e.first, e.text)) {
            e.kind = MSG_TALK;
            e.color = UI_GOLD;
        }
    auto m = std::make_shared<MsgModel>();
    m->width = width;
    const Entry *newest = nullptr;
    int entry = 0;
    for (const Entry &e : entries) {
        if (!msg_show[e.kind])
            continue;
        newest = &e;
        // room for the kind's icon in front of the text
        bool first = true;
        for (const std::string &l : msg_wrap(e.text, std::max(10, width - MSG_ICON_COLS))) {
            m->lines.push_back(MsgLine{l, e.color, first ? e.kind : -1, entry});
            first = false;
        }
        ++entry;
    }
    if (newest) {
        std::vector<std::string> parts = msg_wrap(newest->text, std::max(10, width - 4));
        if (parts.size() > 3) {
            parts.resize(3);
            parts[2] += "...";
        }
        for (const std::string &l : parts)
            m->latest.push_back(MsgLine{l, newest->color});
    }
    std::lock_guard<std::mutex> lock(msg_mutex);
    msg_model = m;
    if (news)
        msg_follow = true;
}

// where everything is, window pixels (from the last drawing)
struct MsgRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool has(int px, int py) const { return w > 0 && px >= x && py >= y && px < x + w && py < y + h; }
};
std::mutex msg_rect_mutex;
MsgRect msg_area, msg_buttons[5], msg_track, msg_thumb;   // buttons: combat, talk, other, hide, show
// while the scrollbar's handle is held: where in it it was taken (pixels from its top), else -1
std::atomic<int> msg_drag{-1};
UiTexture tex_messages;

// a filled rectangle with its corners rounded off by r pixels
void round_fill(fpv::Canvas &cv, int x, int y, int w, int h, int r, uint32_t color) {
    r = std::min({r, w / 2, h / 2});
    for (int yy = 0; yy < h; ++yy) {
        const int d = std::min(yy, h - 1 - yy);
        int inset = 0;
        if (d < r) {
            const float dy = float(r) - float(d) - 0.5f;
            inset = r - int(std::sqrt(std::max(0.0f, float(r * r) - dy * dy)) + 0.5f);
        }
        cv.fill(x + inset, y + yy, w - 2 * inset, 1, color);
    }
}
std::shared_ptr<const fpv::Image> msg_fallback[5];

const fpv::Image &msg_icon(const fpv::Media *media, int i) {
    if (media && media->msg_icon[i].ok())
        return media->msg_icon[i];
    if (!msg_fallback[i]) {
        auto img = std::make_shared<fpv::Image>();
        img->w = 16;
        img->h = 12;
        img->px.assign(16 * 12, 0);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 16; ++x) {
                const char c = MSG_ICON_ROWS[i][y][x];
                if (c == '#')
                    img->px[size_t(y) * 16 + x] = fpv::rgb(255, 255, 255);
                else if (c == 'o')
                    img->px[size_t(y) * 16 + x] = fpv::rgb(255, 214, 90);
            }
        msg_fallback[i] = img;
    }
    return *msg_fallback[i];
}

// true if the pointer is on something of the panel
bool in_messages(int px, int py) {
    std::lock_guard<std::mutex> lock(msg_rect_mutex);
    if (msg_area.has(px, py))
        return true;
    for (const MsgRect &b : msg_buttons)
        if (b.has(px, py))
            return true;
    return false;
}

void msg_scroll_to(int top, int total) {
    const int page = std::max(1, msg_page.load());
    const int max_top = std::max(0, total - page);
    top = std::clamp(top, 0, max_top);
    msg_top = top;
    msg_follow = top >= max_top;
}

// simulation thread: a left click at a window pixel; true if it was ours
bool messages_click(int px, int py) {
    MsgRect area, buttons[5], track, thumb;
    {
        std::lock_guard<std::mutex> lock(msg_rect_mutex);
        area = msg_area;
        for (int i = 0; i < 5; ++i)
            buttons[i] = msg_buttons[i];
        track = msg_track;
        thumb = msg_thumb;
    }
    for (int i = 0; i < 3; ++i)
        if (buttons[i].has(px, py)) {
            msg_show[i] = !msg_show[i];
            return true;
        }
    if (buttons[3].has(px, py)) {
        msg_folded = true;
        return true;
    }
    if (buttons[4].has(px, py)) {
        msg_folded = false;
        msg_follow = true;
        return true;
    }
    // the handle is dragged; the render thread follows the pointer until the button is let go
    if (thumb.has(px, py)) {
        msg_drag = py - thumb.y;
        return true;
    }
    if (track.has(px, py)) {
        std::shared_ptr<const MsgModel> m;
        {
            std::lock_guard<std::mutex> lock(msg_mutex);
            m = msg_model;
        }
        const int total = m ? int(m->lines.size()) : 0;
        const int page = std::max(1, msg_page.load());
        // a click above the middle of the track pages up, below it down
        const int dir = py < track.y + track.h / 2 ? -1 : 1;
        msg_scroll_to(msg_top + dir * page, total);
        return true;
    }
    return area.has(px, py);
}

// the date as the game's own date line gives it: "15th Granite, 125"
std::string date_text() {
    static const char *const MONTHS[12] = {"Granite", "Slate", "Felsite", "Hematite", "Malachite", "Galena",
                                           "Limestone", "Sandstone", "Timber", "Moonstone", "Opal", "Obsidian"};
    if (!cur_year || !cur_year_tick)
        return "";
    const int days = std::max(0, *cur_year_tick) / 1200;
    const int day = days % 28 + 1, month = days / 28 % 12;
    const char *th = day % 10 == 1 && day != 11 ? "st" : day % 10 == 2 && day != 12 ? "nd"
                     : day % 10 == 3 && day != 13 ? "rd" : "th";
    return std::to_string(day) + th + " " + MONTHS[month] + ", " + std::to_string(*cur_year);
}

// render thread
void draw_messages(SDL_Renderer *sdl) {
    static int wheel_seen = 0;
    const auto clear = [] {
        std::lock_guard<std::mutex> lock(msg_rect_mutex);
        msg_area = MsgRect();
        for (MsgRect &b : msg_buttons)
            b = MsgRect();
        msg_track = MsgRect();
        msg_thumb = MsgRect();
        msg_drag = -1;
    };
    std::shared_ptr<const MsgModel> m;
    {
        std::lock_guard<std::mutex> lock(msg_mutex);
        m = msg_model;
    }
    Cells c;
    int left = 0, right = 0;
    // only on the plain adventure screen, where the game's corner is hidden
    if (!m || !ui_font_ready || (!corner_live && !msg_ok) || !get_cells(c) || !msg_span(c, left, right)) {
        clear();
        return;
    }
    const std::shared_ptr<const fpv::Media> media = get_media();
    const int cw = c.cw, ch = c.ch;
    const int bw = 2 * cw + 6, bh = ch + 6;   // a button: a two-cell picture and its border
    const uint32_t BG = 0xA0000000u | (fpv::rgb(8, 9, 14) & 0x00FFFFFFu);
    const uint32_t ON = UI_YELLOW, OFF = fpv::rgb(60, 110, 210);
    const auto button = [&](fpv::Canvas &cv, int x, int y, int icon, bool on) {
        cv.fill(x, y, bw, bh, 0xD0000000u | (fpv::rgb(14, 16, 24) & 0x00FFFFFFu));
        cv.frame(x, y, bw, bh, on ? ON : OFF, 2);
        cv.blitFit(msg_icon(media.get(), icon), x + 3, y + 3, bw - 6, bh - 6);
    };
    int mx = -1, my = -1;
    sdl_get_mouse_state(&mx, &my);
    const int bottom = c.oy + c.dimy * ch - 1;

    if (msg_folded) {
        // the newest message alone, and the button that brings the list back
        int tw = 0;
        for (const MsgLine &l : m->latest)
            tw = std::max(tw, int(l.text.size()) * cw);
        const int text_w = tw > 0 ? tw + 12 : 0, text_h = int(m->latest.size()) * ch + 8;
        const int W = text_w + 6 + bw, H = std::max(bh, text_h);
        fpv::Canvas cv;
        cv.resize(W, H);
        if (text_w > 0) {
            cv.fill(0, H - text_h, text_w, text_h, BG);
            for (size_t i = 0; i < m->latest.size(); ++i)
                panel_text(cv, 6, H - text_h + 4 + int(i) * ch, m->latest[i].text, m->latest[i].color, cw, ch);
        }
        button(cv, W - bw, H - bh, 4, false);
        tex_messages.upload(sdl, cv.px.data(), cv.w, cv.h);
        const int x = right - W, y = bottom - H - 2;
        tex_messages.draw(sdl, x, y);
        std::lock_guard<std::mutex> lock(msg_rect_mutex);
        msg_area = MsgRect();
        for (MsgRect &b : msg_buttons)
            b = MsgRect();
        msg_track = MsgRect();
        msg_thumb = MsgRect();
        msg_drag = -1;
        msg_buttons[4] = MsgRect{x + W - bw, y + H - bh, bw, bh};
        return;
    }

    // as tall as the doll's and the bars' squares, level with them
    const int X = left, Y = bottom - (MSG_ROWS * ch - 2);
    const int W = right - X, H = bottom - Y;
    const int text_y = bh + 6, rows = std::max(1, (H - text_y - 4) / ch);
    msg_page = rows;
    const int total = int(m->lines.size());

    // the mouse wheel over it looks back
    const int wheel = wheel_total.load();
    if (wheel != wheel_seen) {
        if (mx >= X && my >= Y && mx < X + W && my < Y + H)
            msg_scroll_to(msg_top - 2 * (wheel - wheel_seen), total);
        wheel_seen = wheel;
    }
    const int track_x = W - 2 * cw + 2, track_w = cw - 2, track_h = rows * ch;
    const int thumb_h = total > rows ? std::max(ch, track_h * rows / total) : 0;
    // the handle held: the list follows the pointer
    if (msg_drag.load() >= 0) {
        const bool down = debug_px.load() >= 0 || !sdl_get_mouse_state
                              ? enabler->mouse_lbut_down != 0
                              : (sdl_get_mouse_state(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        if (!down || total <= rows) {
            msg_drag = -1;
        } else {
            const int span = std::max(1, track_h - thumb_h);
            const int pos = std::clamp(my - Y - text_y - msg_drag.load(), 0, span);
            msg_scroll_to((pos * (total - rows) + span / 2) / span, total);
        }
    }
    if (msg_follow)
        msg_top = std::max(0, total - rows);
    const int top = std::clamp(msg_top.load(), 0, std::max(0, total - rows));

    fpv::Canvas cv;
    cv.resize(W, H);
    cv.fill(0, 0, W, H, BG);
    cv.frame(0, 0, W, H, UI_GOLD, 1);
    // each message on a rounded box of its own, its kind's icon before its first line
    {
        const uint32_t BOX = 0xC8000000u | (fpv::rgb(30, 33, 46) & 0x00FFFFFFu);
        const int box_x = cw / 2, box_w = track_x - 4 - box_x;
        const int shown = std::min(rows, total - top);
        for (int r = 0; r < shown;) {
            const int entry = m->lines[size_t(top + r)].entry;
            int n = 1;
            while (r + n < shown && m->lines[size_t(top + r + n)].entry == entry)
                ++n;
            // a pixel off top and bottom leaves a gap between neighbouring messages
            round_fill(cv, box_x, text_y + r * ch + 1, box_w, n * ch - 2, std::max(2, ch / 4), BOX);
            r += n;
        }
        for (int r = 0; r < shown; ++r) {
            const MsgLine &l = m->lines[size_t(top + r)];
            const int y = text_y + r * ch;
            if (l.kind >= 0)
                cv.blitFit(msg_icon(media.get(), l.kind), cw, y, 2 * cw, ch);
            panel_text(cv, cw + MSG_ICON_COLS * cw - cw / 2, y, l.text, l.color, cw, ch);
        }
    }
    // the scrollbar
    cv.fill(track_x, text_y, track_w, track_h, 0x80000000u | (fpv::rgb(60, 64, 80) & 0x00FFFFFFu));
    int thumb_y = 0;
    if (total > rows) {
        thumb_y = text_y + (track_h - thumb_h) * top / std::max(1, total - rows);
        cv.fill(track_x, thumb_y, track_w, thumb_h,
                msg_drag.load() >= 0 ? fpv::rgb(220, 224, 236) : fpv::rgb(170, 176, 196));
    }
    // the buttons, right to left: fold, everything else, talk, combat
    int bx[4];
    for (int i = 0; i < 4; ++i)
        bx[i] = W - 4 - (4 - i) * bw - (3 - i) * 3;
    for (int i = 0; i < 3; ++i)
        button(cv, bx[i], 3, i, msg_show[i]);
    button(cv, bx[3], 3, 3, false);
    // top left, where no message is: the date and what the game's speed
    // display said, each in a thin frame, as far as there is room
    {
        std::string speed;
        {
            std::lock_guard<std::mutex> lock(speed_mutex);
            speed = speed_text;
        }
        int x = 4;
        const std::string shown[2] = {date_text(), speed};
        for (int k = 0; k < 2; ++k) {
            const std::string &s = shown[k];
            const int w = int(s.size()) * cw + 10;
            if (s.empty() || x + w > bx[0] - 6)
                continue;
            cv.fill(x, 3, w, bh, 0xD0000000u | (fpv::rgb(14, 16, 24) & 0x00FFFFFFu));
            cv.frame(x, 3, w, bh, fpv::rgb(150, 112, 44), 1);
            panel_text(cv, x + 5, 6, s, k == 1 && adv_sneaking.load() ? fpv::rgb(196, 120, 255) : UI_WHITE, cw, ch);
            x += w + 4;
        }
    }
    tex_messages.upload(sdl, cv.px.data(), cv.w, cv.h);
    tex_messages.draw(sdl, X, Y);

    std::lock_guard<std::mutex> lock(msg_rect_mutex);
    msg_area = MsgRect{X, Y, W, H};
    for (int i = 0; i < 4; ++i)
        msg_buttons[i] = MsgRect{X + bx[i], Y + 3, bw, bh};
    msg_buttons[4] = MsgRect();
    msg_track = MsgRect{X + track_x - 2, Y + text_y, track_w + 4, track_h};
    msg_thumb = total > rows ? MsgRect{X + track_x - 2, Y + thumb_y, track_w + 4, thumb_h} : MsgRect();
}
