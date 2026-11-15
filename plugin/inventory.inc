// ---------------------------------------------------------------------------
// The inventory panel. While the view is on, the game's inventory screen is
// replaced by a panel of our own, centred and in front of everything: on the
// left your own sprite, large, with a square for each body part that wears or
// holds things around it; in the rest what you carry, a square for each item,
// grouped by what it is in.
//
// It is a skin over the game's own inventory, which stays open unseen. A
// right (or plain left) click on an item clicks that item in the game's list;
// the game then has its list of what can be done with that item, which is
// shown here as a small menu, and a choice there clicks that row of the
// game's list. Dragging an item onto a body part or into a container opens
// its list the same way and picks the row that does it (wear, remove, put in).
// Keys go to the game's inventory as they would: a letter opens that item's
// list, Esc steps back.
//
// Included inside fpview.cpp's anonymous namespace, after menu.inc. What runs
// in the game's hooks (input, the clicks) is in inventory_hook.inc.

// what the game offers for an item in its list (adventure_option_inventory_item_option_flag)
constexpr uint32_t INV_CAN_DROP = 1u << 1, INV_CAN_WEAR = 1u << 2, INV_CAN_REMOVE = 1u << 3, INV_CAN_PUT = 1u << 4;
constexpr size_t LABEL_CHARS = 9;   // a body part's name under its square, at most, in the 8 x 12 font

struct InvItem {
    int32_t id = -1;
    std::string name;                          // as the game lists it
    std::shared_ptr<const fpv::Image> pic;     // 32 x 32; null: none known (its first letter stands in)
    uint32_t flags = 0;
    int count = 1;                             // how many in its stack
    bool ground = false;                       // lying on your tile, not carried
};

struct InvSlot {
    int bp = -1;
    std::string part, label;                   // "left hand", "l. hand"
    std::string label2;                        // a second line under the label ("held"); empty: none
    float ax = 0, ay = 0;                      // where on the body: -1..1 across as seen, 0..1 from the top
    int column = 0;                            // 0 left of the sprite, 1 right of it, 2 the row above it
    bool grasp = false, gone = false;
    // A grasping part has two squares: what is worn on it (a gauntlet) and,
    // under it, what it holds. This is the one for what it holds.
    bool held = false;
    std::vector<InvItem> items;
};

struct InvGroup {
    int32_t container = -1;                    // what they are in; -1: carried loose
    std::string title;
    std::vector<InvItem> items;
};

struct InvModel {
    std::shared_ptr<const fpv::Image> sprite;  // the creature's own picture, or a paperdoll
    std::vector<InvSlot> slots;
    std::vector<InvGroup> groups;
    std::string burden;                        // "Burden: 74 /75", as the game writes it
    // the game's red button (INTERFACE_BITS_2, columns 3..5, rows 0..2: corners,
    // edges and middle), for Done; null where it could not be read
    std::shared_ptr<const fpv::Image> button[9];
    // the game's scrollbar (INTERFACE_BITS): the bar's 2 x 3 tiles (arrow, track,
    // arrow), then the handle's top, middle, filler and bottom, 2 tiles each
    std::shared_ptr<const fpv::Image> scroll[14];
    std::vector<InvItem> ground;               // what lies on your tile
    bool popup_ground = false;                 // the small menu is ours, for a thing on the ground
    // the game's list for one item, or what it went on to: our small menu; empty: none
    int32_t popup_item = -1;
    std::vector<std::string> popup;
    int popup_split = -1;       // our own row in it, "Split stack"; -1: none
    // the dialog that splits a stack or shares two (InvSplit, copied to be drawn)
    bool split_on = false;
    std::string split_title, split_left, split_right;
    int split_lo = 0, split_hi = 0, split_value = 0, split_total = 0;
    std::string split_text;     // what is typed into its box
    size_t key = 0;                            // all of the above, to see a change

    const InvItem *item(int32_t id) const {
        for (const InvSlot &s : slots)
            for (const InvItem &it : s.items)
                if (it.id == id)
                    return &it;
        for (const InvGroup &g : groups)
            for (const InvItem &it : g.items)
                if (it.id == id)
                    return &it;
        for (const InvItem &it : ground)
            if (it.id == id)
                return &it;
        return nullptr;
    }
};

// The dialog to split a stack in two, or to share out two stacks of the same
// thing: one number, set with a slider or typed. Simulation thread.
struct InvSplit {
    bool on = false;
    int32_t a = -1;             // the stack split, or the one dragged
    int32_t b = -1;             // the one it was dragged onto; -1: a split
    int lo = 0, hi = 0;         // what value may be
    int value = 0;              // split: how many go to the new stack; share: how many stay in a
    int total = 0;              // split: a's count; share: both counts together
    std::string title, name_a, name_b, text;
    bool fresh = true;          // the next digit typed starts the number again
    int mode = 0;               // 0: split or share; 1: the game's "pick up how many?" answered here
} inv_split;
// the answer to the game's "pick up how many?" (mode 1): 0 none yet, 1 Okay, -1 Cancel
int inv_amount_answer = 0;
int inv_amount_value = 0;

// the menu of ours for a thing on the ground: its id; -1: none
int32_t inv_ground_menu = -1;

// What the game drew this frame, kept before its own inventory window is taken
// off the screen (inventory_grid_capture): the panel reads the window's rows,
// pictures and burden line from here. dimx 0: none this frame (the live screen
// is read then).
struct GridCopy {
    int dimx = 0, dimy = 0;
    std::vector<uint8_t> screen;      // 8 bytes a cell, as gps->screen
    std::vector<long> anchored;
    std::vector<uint32_t> flag;
    // the pictures anchored on it (flag 0), copied in the same moment: what
    // their texture numbers named when the game drew them
    std::unordered_map<long, std::shared_ptr<const fpv::Image>> pictures;
} inv_grid;
bool inv_window_erased = false;       // the game's inventory window was taken off this frame

bool grid_copied() {
    return gps && inv_grid.dimx == gps->dimx && inv_grid.dimy == gps->dimy && inv_grid.dimx > 0;
}
uint8_t inv_cell_char(int x, int y) {
    const size_t i = size_t(x) * gps->dimy + y;
    return grid_copied() ? inv_grid.screen[i * 8] : gps->screen[i * 8];
}
long inv_cell_anchored(int x, int y) {
    const size_t i = size_t(x) * gps->dimy + y;
    return grid_copied() ? inv_grid.anchored[i] : gps->screentexpos_anchored[i];
}
uint32_t inv_cell_flag(int x, int y) {
    const size_t i = size_t(x) * gps->dimy + y;
    return grid_copied() ? inv_grid.flag[i] : gps->screentexpos_flag[i];
}

std::mutex inv_mutex;
std::shared_ptr<const InvModel> inv_model;
std::atomic<bool> inv_up{false};           // the panel stands in for the game's inventory this frame
std::atomic<int> inv_scroll{0};            // pixels the list of what you carry is scrolled down
std::atomic<int32_t> inv_drag{-1};         // the item being dragged, drawn at the pointer

std::shared_ptr<const InvModel> get_inv() {
    std::lock_guard<std::mutex> lock(inv_mutex);
    return inv_model;
}

void set_inv(std::shared_ptr<const InvModel> m) {
    std::lock_guard<std::mutex> lock(inv_mutex);
    inv_model = std::move(m);
}

// A body part's name made short enough for its square: "left hand" -> "l. hand";
// if that is still too long, the first letters of its words: "upper left big
// sharp claw" -> "ULBSC".
std::string short_part(const std::string &name, size_t room) {
    static const std::pair<const char *, const char *> SHORT[] = {
        {"left", "l."}, {"right", "r."}, {"upper", "u."}, {"lower", "lo."}, {"front", "f."}, {"rear", "re."},
        {"middle", "m."}, {"first", "1st"}, {"second", "2nd"}, {"third", "3rd"}, {"fourth", "4th"}, {"fifth", "5th"}};
    std::vector<std::string> words;
    std::string w;
    for (const char ch : name + " ") {
        if (ch == ' ' || ch == '-' || ch == ',') {
            if (!w.empty())
                words.push_back(w);
            w.clear();
        } else {
            w += ch;
        }
    }
    std::string s;
    for (const std::string &word : words) {
        std::string put = word;
        for (const auto &p : SHORT)
            if (word == p.first)
                put = p.second;
        s += (s.empty() ? "" : " ") + put;
    }
    if (s.size() <= room)
        return s;
    std::string initials;
    for (const std::string &word : words)
        initials += char(std::toupper(uint8_t(word[0])));
    return initials.substr(0, room);
}

// ---------------------------------------------------------------------------
// the model, read from the game's open inventory (simulation thread)

std::unordered_map<int32_t, std::shared_ptr<const fpv::Image>> inv_pictures;   // by item id
int32_t inv_pictures_unit = -1;
// The picture the game itself draws beside each item in its inventory list
// (a texture it makes per kind of item), by item id, with the name the list
// gave it then (a changed name, such as a waterskin filled, is looked at
// again); img null: looked, none found. Read off the interface grid
// (inventory_hook.inc); preferred to ours. The pixels are kept, not the
// texture's number: the game hands freed numbers out again, and a number kept
// from an earlier draw or an earlier world named another picture by the time
// it was read (the thirst status icon in place of items, 2026-11-15).
//
// Nor is a picture taken from a single draw. The game makes these pictures on
// its main thread: in the first frame a row is drawn, its texture number can
// still hold what that number held before. A list page shown for one frame
// while paging for pictures was read just then, and the copy was kept for good
// (the thirst icon on the water in a waterskin after loading a save, again on
// 2026-11-15). A read is now only a candidate; it becomes the item's picture
// once the same number has shown the same pixels for ICON_STEADY_MS.
struct GameIcon {
    std::string name;                          // the list's name for the item when read
    std::shared_ptr<const fpv::Image> img;     // the game's picture, once settled
    bool done = false;                         // settled: img is it, or the game shows none
    std::shared_ptr<const fpv::Image> cand;    // the latest read, waiting to hold steady
    long cand_tex = 0;
    int64_t cand_since = 0;                    // 0: nothing read yet
    uint32_t seen = 0;                         // icon_frame when it was last read
};
std::unordered_map<int32_t, GameIcon> inv_game_icon;
uint32_t icon_frame = 0;                       // counts collect_game_icons' frames
constexpr int64_t ICON_STEADY_MS = 250;
constexpr int64_t ICON_WAIT_MS = 2000;         // no picture, or one that keeps changing: taken as it is then

int64_t icon_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool same_picture(const std::shared_ptr<const fpv::Image> &a, const std::shared_ptr<const fpv::Image> &b) {
    if (!a || !b)
        return !a && !b;
    return a == b || (a->w == b->w && a->h == b->h && a->px == b->px);
}

// one read of the picture the game drew for an item this frame
void note_game_icon(int32_t id, const std::string &name, long tex, std::shared_ptr<const fpv::Image> img) {
    GameIcon &g = inv_game_icon[id];
    if (g.name != name)
        g = GameIcon{name};   // filled, renamed: another picture is to be expected
    g.seen = icon_frame;
    const int64_t now = icon_ms();
    if (!g.cand_since || g.cand_tex != tex || !same_picture(g.cand, img)) {
        g.cand = std::move(img);
        g.cand_tex = tex;
        g.cand_since = now;
        return;
    }
    // no picture yet is waited out longer: the game may still be making it
    if (now - g.cand_since >= (g.cand ? ICON_STEADY_MS : ICON_WAIT_MS)) {
        g.img = g.cand;
        g.done = true;
    }
}

// Item ids and pictures belong to one world and one adventurer.
void inv_forget_pictures() {
    inv_pictures.clear();
    inv_game_icon.clear();
    inv_grid.pictures.clear();
}

std::shared_ptr<const fpv::Image> inv_picture(df::item *it) {
    const auto game_icon = inv_game_icon.find(it->id);
    if (game_icon != inv_game_icon.end() && game_icon->second.img)
        return game_icon->second.img;
    const auto found = inv_pictures.find(it->id);
    if (found != inv_pictures.end())
        return found->second;
    if (inv_pictures.size() > 800)
        inv_pictures.clear();
    std::shared_ptr<fpv::Image> img;
    uint32_t raw[fpv::TEXPIX];
    if (fpv::itemPicture(it, raw)) {
        img = std::make_shared<fpv::Image>();
        img->w = img->h = fpv::TEX;
        img->px.assign(raw, raw + fpv::TEXPIX);
    }
    inv_pictures[it->id] = img;
    return img;
}

std::string option_text(df::adventure_optionst *o, bool full) {
    std::string s;
    if (o) {
        if (full)
            o->getFullListName(&s);
        else
            o->getName(&s);
    }
    return s;
}

// the line of the game's inventory window that holds "Burden:"
std::string read_burden() {
    for (int y = 0; y < gps->dimy; ++y)
        for (int x = 0; x + 7 < gps->dimx; ++x) {
            if (inv_cell_char(x, y) != 'B')
                continue;
            std::string line;
            for (int xx = x; xx < gps->dimx; ++xx) {
                const uint8_t ch = inv_cell_char(xx, y);
                line += ch >= 32 ? char(ch) : ' ';
            }
            if (line.rfind("Burden:", 0) != 0)
                continue;
            const size_t end = line.find("   ");
            return rtrim(line.substr(0, end));
        }
    return "";
}

// The squares of the body parts, from the body plan: each part that wears or
// holds things (head, upper and lower body, grasping and standing parts) and
// any other part that has something on it. Where on the body each lies is
// made up from what it is and what it hangs from.
void inv_slots(df::unit *adv, const std::vector<bool> &holds, InvModel &m, std::vector<int> &slot_of,
               std::vector<int> &held_of) {
    const auto &parts = adv->body.body_plan->body_parts;
    using F = df::body_part_raw_flags;
    const size_t n = parts.size();
    slot_of.assign(n, -1);
    held_of.assign(n, -1);
    const auto flag = [&](size_t bp, F f) { return parts[bp] && parts[bp]->flags.is_set(f); };
    // the parts a head hangs from (a neck)
    std::vector<bool> to_head(n, false);
    for (size_t bp = 0; bp < n; ++bp)
        if (flag(bp, F::HEAD))
            for (int up = parts[bp]->con_part_id, hops = 0; up >= 0 && size_t(up) < n && hops < 8; ++hops) {
                if (!parts[up] || flag(size_t(up), F::UPPERBODY) || flag(size_t(up), F::LOWERBODY))
                    break;
                to_head[up] = true;
                up = parts[up]->con_part_id;
            }
    const auto base_y = [&](size_t bp) -> float {
        if (flag(bp, F::HEAD)) return 0.06f;
        if (to_head[bp]) return 0.17f;
        if (flag(bp, F::STANCE)) return 0.95f;
        if (flag(bp, F::GRASP)) return 0.58f;
        if (flag(bp, F::UPPERBODY)) return 0.32f;
        if (flag(bp, F::LOWERBODY)) return 0.56f;
        return -1;
    };
    for (size_t bp = 0; bp < n; ++bp) {
        const df::body_part_raw *p = parts[bp];
        if (!p)
            continue;
        const bool kind = flag(bp, F::HEAD) || flag(bp, F::UPPERBODY) || flag(bp, F::LOWERBODY) ||
                          flag(bp, F::GRASP) || flag(bp, F::STANCE);
        if (!kind && !holds[bp])
            continue;
        InvSlot s;
        s.bp = int(bp);
        s.part = !p->name_singular.empty() && p->name_singular[0] ? *p->name_singular[0] : p->token;
        s.label = short_part(s.part, LABEL_CHARS);
        s.grasp = flag(bp, F::GRASP);
        const auto &status = adv->body.components.body_part_status;
        s.gone = bp < status.size() && status[bp].bits.missing;
        // a part that is none of the kinds: below the nearest one it hangs from
        float y = base_y(bp);
        int up = p->con_part_id, hops = 1;
        while (y < 0 && up >= 0 && size_t(up) < n && hops < 10) {
            const float b = base_y(size_t(up));
            if (b >= 0)
                y = b + 0.1f * hops;
            up = parts[up] ? parts[up]->con_part_id : -1;
            ++hops;
        }
        s.ay = std::clamp(y < 0 ? 0.5f : y, 0.0f, 1.0f);
        const float out = s.grasp ? 0.85f : flag(bp, F::STANCE) ? 0.35f : 0.6f;
        // seen from the front: its right is on the left
        s.ax = flag(bp, F::LEFT) ? out : flag(bp, F::RIGHT) ? -out : 0.0f;
        s.column = flag(bp, F::HEAD) ? 2 : flag(bp, F::RIGHT) ? 0 : flag(bp, F::LEFT) ? 1 : -1;
        slot_of[bp] = int(m.slots.size());
        if (s.grasp) {
            // right after it (and kept so): the square for what it holds
            InvSlot h = s;
            h.held = true;
            h.label2 = "held";
            h.ay += 0.001f;
            m.slots.push_back(std::move(s));
            held_of[bp] = int(m.slots.size());
            m.slots.push_back(std::move(h));
        } else {
            m.slots.push_back(std::move(s));
        }
    }
    if (m.slots.empty() && n > 0 && parts[0]) {
        // nothing of the usual kinds (a blob): the body itself
        InvSlot s;
        s.bp = 0;
        s.part = !parts[0]->name_singular.empty() && parts[0]->name_singular[0] ? *parts[0]->name_singular[0]
                                                                              : parts[0]->token;
        s.label = short_part(s.part, LABEL_CHARS);
        s.ay = 0.4f;
        slot_of[0] = 0;
        m.slots.push_back(std::move(s));
    }
    // the middle parts go to the side with fewer, from the top down
    std::vector<size_t> middle;
    int count[2] = {0, 0};
    for (size_t i = 0; i < m.slots.size(); ++i) {
        if (m.slots[i].column < 0) {
            if (!m.slots[i].held)
                middle.push_back(i);
        } else if (m.slots[i].column < 2) {
            ++count[m.slots[i].column];
        }
    }
    std::stable_sort(middle.begin(), middle.end(), [&](size_t a, size_t b) { return m.slots[a].ay < m.slots[b].ay; });
    for (size_t i : middle) {
        const int side = count[1] < count[0] ? 1 : 0;
        m.slots[i].column = side;
        ++count[side];
        // a hand's held square stays on the side of its worn one
        if (i + 1 < m.slots.size() && m.slots[i + 1].held && m.slots[i + 1].bp == m.slots[i].bp) {
            m.slots[i + 1].column = side;
            ++count[side];
        }
    }
}

// The game's red button, as its own inventory window draws Done: a 3 x 3 block
// of tiles, columns 3..5 and rows 0..2 of its INTERFACE_BITS_2 page (read off
// the game's screen, 2026-11-14). Looked up again when the textures change.
std::shared_ptr<const fpv::Image> button_tiles[9];
int32_t button_first_tex = -1;   // page_epoch() of the copy

uint32_t page_epoch();

void read_button_tiles() {
    if (!df::global::texture)
        return;
    for (const df::tile_pagest *page : df::global::texture->page) {
        if (!page || page->token != "INTERFACE_BITS_2" || page->page_dim_x < 6 || page->page_dim_y < 3)
            continue;
        const int32_t first = int32_t(page_epoch());
        if (first == button_first_tex)
            return;
        button_first_tex = first;
        for (int k = 0; k < 9; ++k) {
            button_tiles[k] = nullptr;
            const size_t i = size_t(k / 3) * page->page_dim_x + 3 + k % 3;
            std::vector<uint32_t> px;
            int w = 0, h = 0;
            if (i < page->texpos.size() && fpv::TextureStore::readNative(int32_t(page->texpos[i]), px, w, h)) {
                auto img = std::make_shared<fpv::Image>();
                img->w = w;
                img->h = h;
                img->px.swap(px);
                button_tiles[k] = img;
            }
        }
        return;
    }
}

// A number that changes when the game has given its graphics pages other
// textures (simulation thread). The game hands the numbers of freed textures out
// again, so after a world is loaded a second time an old number can name
// another picture: copies kept by texture number, or once per save, went wrong
// then (ring icons and panel frames showing other tiles, 2026-11-15). Looked at
// again at most every quarter second: the first, middle and last texture of
// every page, by number and by where its picture lies in memory.
uint32_t page_epoch() {
    static uint32_t epoch = 1;
    static uint64_t stamp = 0;
    static std::chrono::steady_clock::time_point checked;
    const auto now = std::chrono::steady_clock::now();
    if (stamp && now - checked < std::chrono::milliseconds(250))
        return epoch;
    checked = now;
    uint64_t h = 1469598103934665603ull;
    const auto add = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    if (df::global::texture && enabler) {
        const auto &raws = enabler->textures.raws;
        for (const df::tile_pagest *page : df::global::texture->page) {
            if (!page || page->texpos.empty())
                continue;
            const size_t n = page->texpos.size();
            add(n);
            for (size_t i : {size_t(0), n / 2, n - 1}) {
                const int32_t tp = int32_t(page->texpos[i]);
                add(uint64_t(uint32_t(tp)));
                add(tp > 0 && size_t(tp) < raws.size() ? uint64_t(uintptr_t(raws[tp])) : 0);
            }
        }
    }
    h |= 1;
    if (h != stamp) {
        stamp = h;
        ++epoch;
    }
    return epoch;
}

// a tile of one of the game's graphics pages at its own size; cached by its
// texture number for as long as the pages keep their textures (page_epoch)
std::shared_ptr<const fpv::Image> page_image(const char *token, int x, int y) {
    static std::unordered_map<int32_t, std::shared_ptr<const fpv::Image>> cache;
    static uint32_t cache_epoch = 0;
    if (const uint32_t e = page_epoch(); e != cache_epoch) {
        cache_epoch = e;
        cache.clear();
    }
    if (!df::global::texture)
        return nullptr;
    for (const df::tile_pagest *page : df::global::texture->page) {
        if (!page || page->token != token || x >= page->page_dim_x || y >= page->page_dim_y)
            continue;
        const size_t i = size_t(y) * page->page_dim_x + x;
        if (i >= page->texpos.size())
            return nullptr;
        const int32_t tp = int32_t(page->texpos[i]);
        const auto found = cache.find(tp);
        if (found != cache.end())
            return found->second;
        std::shared_ptr<fpv::Image> img;
        std::vector<uint32_t> px;
        int w = 0, h = 0;
        if (fpv::TextureStore::readNative(tp, px, w, h)) {
            img = std::make_shared<fpv::Image>();
            img->w = w;
            img->h = h;
            img->px.swap(px);
        }
        cache[tp] = img;
        return img;
    }
    return nullptr;
}

// the dialog (InvSplit) into the model, to be drawn
void fill_dialog(InvModel &m) {
    m.split_on = inv_split.on;
    if (!inv_split.on)
        return;
    m.split_title = inv_split.title;
    m.split_lo = inv_split.lo;
    m.split_hi = inv_split.hi;
    m.split_value = inv_split.value;
    m.split_total = inv_split.total;
    m.split_text = inv_split.text;
    if (inv_split.mode == 1) {
        m.split_left = "left there: " + std::to_string(inv_split.total - inv_split.value);
        m.split_right = "picked up: " + std::to_string(inv_split.value);
    } else if (inv_split.b < 0) {
        m.split_left = inv_split.name_a + ": " + std::to_string(inv_split.total - inv_split.value);
        m.split_right = "new stack: " + std::to_string(inv_split.value);
    } else {
        m.split_left = inv_split.name_a + ": " + std::to_string(inv_split.value);
        m.split_right = inv_split.name_b + ": " + std::to_string(inv_split.total - inv_split.value);
    }
}

// simulation thread: the panel's contents for the game's open inventory
std::shared_ptr<InvModel> read_inventory(df::unit *adv) {
    auto &inv = game->main_interface.adventure.inventory;
    auto m = std::make_shared<InvModel>();
    if (!adv || !adv->body.body_plan)
        return m;
    if (inv_pictures_unit != adv->id) {
        inv_forget_pictures();
        inv_pictures_unit = adv->id;
    }
    // the creature's own picture
    // (of the six the game keeps, some are empty: in one adventure only texpos[1][1] had anything in it)
    {
        uint32_t raw[fpv::TEXPIX];
        int best = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j) {
                const int32_t tp = adv->texpos[i][j];
                if (tp <= 0 || !fpv::TextureStore::readSprite(tp, raw))
                    continue;
                int drawn = 0;
                for (const uint32_t c : raw)
                    drawn += fpv::px_a(c) >= 128;
                if (drawn > best) {
                    best = drawn;
                    auto img = std::make_shared<fpv::Image>();
                    img->w = img->h = fpv::TEX;
                    img->px.assign(raw, raw + fpv::TEXPIX);
                    m->sprite = img;
                }
            }
    }
    const auto &parts = adv->body.body_plan->body_parts;
    if (!m->sprite) {
        // no picture of it: the paperdoll that fits its body
        const std::shared_ptr<const fpv::Media> media = get_media();
        std::vector<std::string> names;
        for (df::body_part_raw *part : parts)
            names.push_back(part && !part->name_singular.empty() && part->name_singular[0] ? *part->name_singular[0]
                                                                                        : std::string());
        std::vector<int> slot_of;
        if (const fpv::Doll *doll = media ? media->bestDoll(names, slot_of) : nullptr)
            m->sprite = std::shared_ptr<const fpv::Image>(media, &doll->img);
    }

    // the game's list of what you carry, in its order; depth: how deep inside other things
    struct Entry {
        df::item *item;
        int depth;
        int bp;
        uint32_t flags;
        std::string name;
        bool held;      // in the part's grasp, not worn on it
    };
    std::vector<Entry> entries;
    const auto &main = inv.option[df::adventure_inventory_option_list_type::MAIN];
    for (df::adventure_optionst *o : main) {
        auto *io = o ? virtual_cast<df::adventure_option_inventory_itemst>(o) : nullptr;
        if (!io || !io->item)
            continue;
        using R = df::inv_item_role_type;
        const bool held = io->invitem && (io->invitem->mode == R::Weapon || io->invitem->mode == R::Hauled);
        entries.push_back({io->item, std::max(0, int(io->depth)),
                           io->invitem ? int(io->invitem->body_part_id) : -1, io->option_flag.whole,
                           option_text(io, false), held});
    }
    std::vector<bool> holds(parts.size(), false);
    for (const Entry &e : entries)
        if (e.depth == 0 && e.bp >= 0 && size_t(e.bp) < parts.size())
            holds[e.bp] = true;
    std::vector<int> slot_of, held_of;
    inv_slots(adv, holds, *m, slot_of, held_of);

    std::unordered_map<int32_t, size_t> group_of;   // container item id -> its group
    const auto group = [&](int32_t id, const std::string &title) -> InvGroup & {
        const auto at = group_of.find(id);
        if (at != group_of.end())
            return m->groups[at->second];
        group_of[id] = m->groups.size();
        InvGroup g;
        g.container = id;
        g.title = title;
        m->groups.push_back(std::move(g));
        return m->groups.back();
    };
    std::vector<const Entry *> above;   // the entry each depth is inside
    for (const Entry &e : entries) {
        above.resize(size_t(e.depth));
        InvItem it;
        it.id = e.item->id;
        it.name = e.name;
        it.pic = inv_picture(e.item);
        it.flags = e.flags;
        it.count = std::max(1, int(e.item->getStackSize()));
        if (e.depth == 0) {
            int s = e.bp >= 0 && size_t(e.bp) < slot_of.size() ? slot_of[e.bp] : -1;
            if (s >= 0 && e.held && held_of[e.bp] >= 0)
                s = held_of[e.bp];
            if (s >= 0)
                m->slots[s].items.push_back(it);
            else
                group(-1, "Carried").items.push_back(it);
        } else {
            const Entry *parent = above.empty() ? nullptr : above.back();
            if (parent)
                group(parent->item->id, parent->name).items.push_back(it);
            else
                group(-1, "Carried").items.push_back(it);
        }
        // a container gets its group even while empty, to drop things into
        if (Items::getCapacity(e.item) > 0)
            group(e.item->id, e.name);
        above.push_back(&e);
    }
    m->burden = read_burden();
    read_button_tiles();
    for (int k = 0; k < 9; ++k)
        m->button[k] = button_tiles[k];
    // the scrollbar, as graphics_interface.txt places it on INTERFACE_BITS
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 2; ++col)
            m->scroll[r * 2 + col] = page_image("INTERFACE_BITS", col, 13 + r);
    for (int r = 0; r < 4; ++r)
        for (int col = 0; col < 2; ++col)
            m->scroll[6 + r * 2 + col] = page_image("INTERFACE_BITS", 6 + col, 15 + r);

    // what lies on your own tile: what the game's pick-up (g) offers
    if (df::map_block *block = Maps::getTileBlock(adv->pos))
        for (const int32_t id : block->items) {
            df::item *it = df::item::find(id);
            if (!it || it->pos != adv->pos || !it->flags.bits.on_ground)
                continue;
            InvItem g;
            g.id = id;
            g.name = Items::getDescription(it, 0, true);
            g.pic = inv_picture(it);
            g.count = std::max(1, int(it->getStackSize()));
            g.ground = true;
            m->ground.push_back(g);
        }

    // the game's list for one item, or what it went on to
    using C = df::adventure_interface_inventory_context_type;
    if (inv.context != C::MAIN && inv.context != C::NONE) {
        for (df::adventure_optionst *o : inv.option_current)
            m->popup.push_back(option_text(o, true));
        if (inv.context_item) {
            m->popup_item = inv.context_item->id;
            // not the game's: split a stack that lies in a container
            if (inv.context == C::ONE_ITEM_FULL_LIST && inv.context_item->getStackSize() > 1 &&
                Items::getContainer(inv.context_item)) {
                m->popup_split = int(m->popup.size());
                m->popup.push_back("Split stack");
            }
        }
    }
    // our menu for a thing on the ground: pick it up, split it
    if (m->popup.empty() && inv_ground_menu >= 0) {
        for (const InvItem &g : m->ground)
            if (g.id == inv_ground_menu) {
                m->popup_item = g.id;
                m->popup_ground = true;
                m->popup.push_back("Pick up");
                if (g.count > 1) {
                    m->popup_split = 1;
                    m->popup.push_back("Split stack");
                }
            }
        if (!m->popup_ground)
            inv_ground_menu = -1;
    }
    fill_dialog(*m);

    // everything that is drawn, as one key
    std::string all = m->burden + "|" + std::to_string(button_first_tex) + "|" + std::to_string(m->popup_item) + "|" + m->split_title + m->split_left +
                      m->split_right + m->split_text + std::to_string(m->split_lo) + "/" + std::to_string(m->split_hi);
    for (const std::string &p : m->popup)
        all += "|" + p;
    for (const InvSlot &s : m->slots) {
        all += "#" + std::to_string(s.bp) + (s.gone ? "x" : "") + (s.held ? "h" : "");
        for (const InvItem &it : s.items)
            all += "," + std::to_string(it.id) + "x" + std::to_string(it.count) + it.name +
                   std::to_string(uintptr_t(it.pic.get()));
    }
    for (const InvGroup &g : m->groups) {
        all += "@" + std::to_string(g.container) + g.title;
        for (const InvItem &it : g.items)
            all += "," + std::to_string(it.id) + "x" + std::to_string(it.count) + it.name +
                   std::to_string(uintptr_t(it.pic.get()));
    }
    for (const InvItem &it : m->ground)
        all += "_" + std::to_string(it.id) + "x" + std::to_string(it.count) + std::to_string(uintptr_t(it.pic.get()));
    for (const auto &t : m->scroll)
        all += std::to_string(uintptr_t(t.get()));
    size_t h = std::hash<std::string>()(all);
    if (m->sprite)
        for (const uint32_t px : m->sprite->px)
            h = h * 1000003u ^ px;
    m->key = h;
    return m;
}

// ---------------------------------------------------------------------------
// where everything goes (window pixels), shared by drawing and clicking

enum { INV_NONE, INV_SLOT, INV_SLOT_ITEM, INV_GROUP, INV_ITEM, INV_ROW, INV_DONE, INV_POPUP, INV_PANEL,
       INV_DIALOG, INV_VALUE, INV_SLIDER, INV_OK, INV_CANCEL, INV_GROUND, INV_GROUND_ITEM,
       INV_VUP, INV_VDOWN, INV_VTRACK, INV_HLEFT, INV_HRIGHT, INV_HTRACK };

std::atomic<int> inv_hscroll{0};           // pixels the row of what lies on the ground is scrolled

struct InvBox {
    int kind = INV_NONE;
    int a = -1, b = -1;             // slot and item, group and item, row
    int x = 0, y = 0, w = 0, h = 0; // from the panel's top left
    bool inside(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct InvLayout {
    int x = 0, y = 0, w = 0, h = 0; // the panel, window pixels
    int cw = 8, ch = 12;
    int S = 32, Q = 44, gap = 6, col_w = 80;
    int sprite_x = 0, sprite_y = 0, sprite_s = 0;
    int list_x = 0, list_y = 0, list_w = 0, list_h = 0, content_h = 0, scroll = 0;
    int ground_x = 0, ground_y = 0, ground_w = 0, ground_h = 0, ground_items_y = 0, content_w = 0, hscroll = 0;
    InvBox vbar, hbar;                          // the scrollbars (kind INV_NONE: not needed)
    int title_y = 0, status_y = 0;
    std::vector<InvBox> boxes;                  // later ones lie over earlier ones
    std::vector<std::array<int, 4>> leads;      // a line from a body part's square to its place on the sprite
    InvBox pop;                                 // the small menu (kind INV_NONE: none)
    std::vector<InvBox> rows;                   // its rows
    InvBox dlg;                                 // the split/share dialog (kind INV_NONE: none)
    std::vector<InvBox> dlg_parts;              // its number box, slider, OK, Cancel

    bool in_list(int px, int py) const {
        return px >= list_x && py >= list_y && px < list_x + list_w && py < list_y + list_h;
    }
    bool in_ground(int px, int py) const {
        return px >= ground_x && py >= ground_y && px < ground_x + ground_w && py < ground_y + ground_h;
    }
    // what is at a window pixel: the menu first, then the topmost box
    InvBox at(int wx, int wy) const {
        const int px = wx - x, py = wy - y;
        // the dialog takes everything while it is up
        if (dlg.kind != INV_NONE) {
            for (const InvBox &b : dlg_parts)
                if (b.inside(px, py))
                    return b;
            return dlg;
        }
        if (pop.kind != INV_NONE) {
            for (const InvBox &r : rows)
                if (r.inside(px, py))
                    return r;
            if (pop.inside(px, py))
                return pop;
        }
        for (size_t i = boxes.size(); i-- > 0;) {
            const InvBox &b = boxes[i];
            if ((b.kind == INV_ITEM || b.kind == INV_GROUP) && !in_list(px, py))
                continue;
            if (b.kind == INV_GROUND_ITEM && (px < ground_x || px >= ground_x + ground_w))
                continue;
            if (b.inside(px, py))
                return b;
        }
        InvBox none;
        if (px >= 0 && py >= 0 && px < w && py < h)
            none.kind = INV_PANEL;
        return none;
    }
    const InvBox *box_of(const InvModel &m, int32_t id) const {
        for (const InvBox &b : boxes) {
            if (b.kind == INV_SLOT_ITEM && m.slots[b.a].items[b.b].id == id)
                return &b;
            if (b.kind == INV_ITEM && m.groups[b.a].items[b.b].id == id)
                return &b;
            if (b.kind == INV_GROUND_ITEM && m.ground[b.b].id == id)
                return &b;
        }
        return nullptr;
    }
};

// the item a box stands for; null if none
const InvItem *inv_item_at(const InvModel &m, const InvBox &b) {
    if (b.kind == INV_SLOT_ITEM && b.a >= 0 && size_t(b.a) < m.slots.size() && b.b >= 0 &&
        size_t(b.b) < m.slots[b.a].items.size())
        return &m.slots[b.a].items[b.b];
    if (b.kind == INV_ITEM && b.a >= 0 && size_t(b.a) < m.groups.size() && b.b >= 0 &&
        size_t(b.b) < m.groups[b.a].items.size())
        return &m.groups[b.a].items[b.b];
    if (b.kind == INV_GROUND_ITEM && b.b >= 0 && size_t(b.b) < m.ground.size())
        return &m.ground[b.b];
    return nullptr;
}

// Where the panel was last put (window pixels), and whether it is to stay
// there: while the game's inventory is closed for a moment (a pick-up, a
// change, reopening), the mod's interface around it is laid out otherwise.
std::atomic<int> inv_place_x{0}, inv_place_y{0}, inv_place_w{0}, inv_place_h{0};
std::atomic<bool> inv_place_kept{false};

// The room the panel may take, window pixels: the window less the mod's own
// interface (the bottom rows of buttons, the message list, the paperdoll, the
// compass, the minimap). For each of those in the way, the side of the room
// that keeps the most of it is moved past it.
void inv_free_area(const Cells &c, int &x0, int &y0, int &x1, int &y1) {
    x0 = c.ox + c.cw;
    y0 = c.oy + c.ch / 2;
    x1 = c.ox + c.dimx * c.cw - c.cw;
    y1 = c.oy + c.dimy * c.ch - c.ch / 2;
    // Only what stays put whatever screen is up: the row along the bottom (the
    // doll, the message list, the bars, the game's buttons), the compass, the
    // minimap's corner. What comes and goes (the passing bars, the message
    // list folded or hidden under a dialog, the minimap covered) is left out:
    // the game's inventory screen is not the plain one, and the panel moved a
    // little when it opened while those were taken into account.
    std::vector<SDL_Rect> busy;
    const int bottom_rows = std::max(2 * BUTTON_ROWS, BOTTOM_ROWS);
    busy.push_back({c.ox, c.oy + (c.dimy - bottom_rows) * c.ch, c.dimx * c.cw, bottom_rows * c.ch});
    if (compass_px1.load() > compass_px0.load())
        busy.push_back({compass_px0.load(), compass_py0.load(), compass_px1.load() - compass_px0.load() + 1,
                        compass_py1.load() - compass_py0.load() + 1});
    SDL_Rect mini;
    if (settings.minimap && corner_rect(mini))
        busy.push_back({mini.x - 4, mini.y - 4, mini.w + 10, mini.h + 8});
    const int gap = c.cw / 2;
    for (const SDL_Rect &r : busy) {
        if (r.w <= 0 || r.h <= 0 || r.x >= x1 || r.y >= y1 || r.x + r.w <= x0 || r.y + r.h <= y0)
            continue;
        const int cut[4][4] = {{x0, r.y + r.h + gap, x1, y1},
                               {x0, y0, x1, r.y - gap},
                               {r.x + r.w + gap, y0, x1, y1},
                               {x0, y0, r.x - gap, y1}};
        int best = -1;
        int64_t area = -1;
        for (int k = 0; k < 4; ++k) {
            const int w = cut[k][2] - cut[k][0], h = cut[k][3] - cut[k][1];
            if (w < 40 * c.cw || h < 20 * c.ch)
                continue;
            if (int64_t(w) * h > area) {
                area = int64_t(w) * h;
                best = k;
            }
        }
        if (best >= 0) {
            x0 = cut[best][0];
            y0 = cut[best][1];
            x1 = cut[best][2];
            y1 = cut[best][3];
        }
    }
}

// The room shared by the inventory, journal and map panels: one place for all
// three, so switching between them with the tabs above it keeps it still. The
// tabs stand on its top edge (PANEL_TABS_H above the body).
int panel_tabs_h(int ch) { return ch; }

bool panel_room(const Cells &c, int &x, int &y, int &w, int &h) {
    int fx0, fy0, fx1, fy1;
    inv_free_area(c, fx0, fy0, fx1, fy1);
    const int tabs = panel_tabs_h(c.ch);
    fy0 += tabs;
    w = std::min(fx1 - fx0, 150 * c.cw);
    h = std::min(fy1 - fy0, 56 * c.ch);
    if (w < 40 * c.cw || h < 16 * c.ch)
        return false;
    x = fx0 + (fx1 - fx0 - w) / 2;
    y = fy0 + (fy1 - fy0 - h) / 2;
    return true;
}

bool layout_inv(const InvModel &m, int scroll, InvLayout &L) {
    Cells c;
    if (!get_cells(c) || c.dimx < 60 || c.dimy < 24)
        return false;
    L.cw = c.cw;
    L.ch = c.ch;
    if (inv_place_kept && inv_place_w.load() > 0) {
        // while the game's inventory is closed for a moment, the panel stays put
        L.x = inv_place_x;
        L.y = inv_place_y;
        L.w = inv_place_w;
        L.h = inv_place_h;
    } else {
        if (!panel_room(c, L.x, L.y, L.w, L.h))
            return false;
        inv_place_x = L.x;
        inv_place_y = L.y;
        inv_place_w = L.w;
        inv_place_h = L.h;
    }
    // an item as large as the game draws a tile; a body part's square holds a few
    L.S = std::max(32, 4 * c.cw);
    L.Q = L.S + 12;
    L.gap = 6;
    const int pad = 3 * c.cw, lab_h = 12;  // inside the game's frame (gamestyle.inc)
    L.col_w = std::max(L.Q, int(LABEL_CHARS) * 8) + 8;
    L.title_y = 2 * c.ch + c.ch / 2 + 2 + c.ch / 2;
    const int top = 5 * c.ch + 4;
    L.status_y = L.h - 4 * c.ch - c.ch / 2;
    const int head_h = c.ch + 6;
    // along the bottom, all the way across: what lies on the ground, one row,
    // with room under it for its scrollbar
    L.ground_x = pad;
    L.ground_w = L.w - 2 * pad;
    L.ground_h = head_h + L.S + 6 + 2 * c.cw + 4;
    L.ground_y = L.status_y - c.ch / 2 - L.ground_h;
    L.ground_items_y = L.ground_y + head_h;
    const int bottom = L.ground_y - c.ch / 2;
    L.boxes.clear();
    L.leads.clear();
    L.rows.clear();
    L.pop = InvBox();
    L.dlg = InvBox();
    L.dlg_parts.clear();

    // the sprite: as large as fits, in whole multiples of its 32 pixels
    const int row_h = L.Q + lab_h + 6;
    const bool top_row = std::any_of(m.slots.begin(), m.slots.end(), [](const InvSlot &s) { return s.column == 2; });
    const int body_top = top + (top_row ? row_h : 0);
    int s = std::min(bottom - body_top, int(L.w * 0.42f) - 2 * L.col_w - 2 * L.gap);
    s = std::max(64, s / 32 * 32);
    L.sprite_s = s;
    L.sprite_x = pad + L.col_w + L.gap;
    L.sprite_y = body_top + std::max(0, (bottom - body_top - s) / 2);
    const int doll_w = 2 * L.col_w + s + 2 * L.gap;
    // a body part's place on the sprite
    const auto spot = [&](const InvSlot &sl, int &px, int &py) {
        px = L.sprite_x + int(s * (0.5f + sl.ax * 0.3f));
        py = L.sprite_y + int(s * (0.1f + sl.ay * 0.82f));
    };
    const auto add_slot = [&](size_t i, int x, int y) {
        const InvSlot &sl = m.slots[i];
        InvBox b;
        b.kind = INV_SLOT;
        b.a = int(i);
        b.x = x;
        b.y = y;
        b.w = b.h = L.Q;
        L.boxes.push_back(b);
        // what is in it: one fills it, more share it in a grid
        const int n = int(sl.items.size());
        const int k = n <= 1 ? 1 : n <= 4 ? 2 : n <= 9 ? 3 : 4;
        const int cell = (L.Q - 4) / k;
        for (int j = 0; j < n && j < k * k; ++j) {
            InvBox ib;
            ib.kind = INV_SLOT_ITEM;
            ib.a = int(i);
            ib.b = j;
            ib.x = x + 2 + (j % k) * cell;
            ib.y = y + 2 + (j / k) * cell;
            ib.w = ib.h = cell;
            L.boxes.push_back(ib);
        }
        int px, py;
        spot(sl, px, py);
        const int ex = sl.column == 0 ? x + L.Q : sl.column == 1 ? x : x + L.Q / 2;
        const int ey = sl.column == 2 ? y + L.Q + lab_h : y + L.Q / 2;
        L.leads.push_back({ex, ey, px, py});
    };
    // the two columns beside the sprite, each in the order of the body from the top
    for (int col = 0; col < 2; ++col) {
        std::vector<size_t> list;
        for (size_t i = 0; i < m.slots.size(); ++i)
            if (m.slots[i].column == col)
                list.push_back(i);
        std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) { return m.slots[a].ay < m.slots[b].ay; });
        std::vector<int> ys;
        for (size_t i : list) {
            int px, py;
            spot(m.slots[i], px, py);
            ys.push_back(py - L.Q / 2);
        }
        // a square's height with its label (two lines for a held one)
        const auto step = [&](size_t k) { return row_h + (m.slots[list[k]].label2.empty() ? 0 : lab_h); };
        // apart enough, pushed down, then back up from the bottom
        for (size_t k = 0; k < ys.size(); ++k)
            ys[k] = std::max(ys[k], k ? ys[k - 1] + step(k - 1) : body_top);
        for (size_t k = ys.size(); k-- > 0;)
            ys[k] = std::min(ys[k], k + 1 < ys.size() ? ys[k + 1] - step(k) : bottom - step(k) + 6);
        for (size_t k = 0; k < ys.size(); ++k)
            ys[k] = std::max(ys[k], k ? ys[k - 1] + step(k - 1) : top);
        const int x = col == 0 ? pad + (L.col_w - L.Q) / 2 : L.sprite_x + s + L.gap + (L.col_w - L.Q) / 2;
        for (size_t k = 0; k < list.size(); ++k)
            add_slot(list[k], x, ys[k]);
    }
    // the row above it (heads), across as they lie
    {
        std::vector<size_t> list;
        for (size_t i = 0; i < m.slots.size(); ++i)
            if (m.slots[i].column == 2)
                list.push_back(i);
        std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) { return m.slots[a].ax < m.slots[b].ax; });
        const int n = int(list.size());
        const int x0 = L.sprite_x + s / 2 - n * L.col_w / 2;
        for (int k = 0; k < n; ++k)
            add_slot(list[k], x0 + k * L.col_w + (L.col_w - L.Q) / 2, top);
    }

    // what you carry: a block for each container, a square for each thing in it
    L.list_x = pad + doll_w + pad;
    L.list_y = top;
    L.list_w = std::max(L.S, L.w - L.list_x - pad - 2 * c.cw - 8);
    L.list_h = bottom - top;
    const int per = std::max(1, (L.list_w + L.gap) / (L.S + L.gap));
    int y = 0;
    std::vector<InvBox> items;
    for (size_t g = 0; g < m.groups.size(); ++g) {
        const int n = int(m.groups[g].items.size());
        const int rows = std::max(1, (n + per - 1) / per);
        InvBox gb;
        gb.kind = INV_GROUP;
        gb.a = int(g);
        gb.x = L.list_x;
        gb.y = L.list_y + y;
        gb.w = L.list_w;
        gb.h = head_h + rows * (L.S + L.gap) + 2;
        L.boxes.push_back(gb);
        for (int j = 0; j < n; ++j) {
            InvBox ib;
            ib.kind = INV_ITEM;
            ib.a = int(g);
            ib.b = j;
            ib.x = L.list_x + 4 + (j % per) * (L.S + L.gap);
            ib.y = L.list_y + y + head_h + (j / per) * (L.S + L.gap);
            ib.w = ib.h = L.S;
            items.push_back(ib);
        }
        y += gb.h + c.ch / 2;
    }
    L.content_h = y;
    L.scroll = std::clamp(scroll, 0, std::max(0, L.content_h - L.list_h));
    for (InvBox &b : L.boxes)
        if (b.kind == INV_GROUP)
            b.y -= L.scroll;
    for (InvBox &b : items) {
        b.y -= L.scroll;
        L.boxes.push_back(b);
    }
    // the game's scrollbar beside it, when it is longer than its room
    L.vbar = InvBox();
    L.hbar = InvBox();
    if (L.content_h > L.list_h && L.list_h > 4 * c.ch) {
        L.vbar.kind = INV_VTRACK;
        L.vbar.x = L.list_x + L.list_w + 6;
        L.vbar.y = L.list_y;
        L.vbar.w = 2 * c.cw;
        L.vbar.h = L.list_h / c.ch * c.ch;
        InvBox up = L.vbar, down = L.vbar, track = L.vbar;
        up.kind = INV_VUP;
        up.h = c.ch;
        down.kind = INV_VDOWN;
        down.y = L.vbar.y + L.vbar.h - c.ch;
        down.h = c.ch;
        track.y += c.ch;
        track.h -= 2 * c.ch;
        L.boxes.push_back(track);
        L.boxes.push_back(up);
        L.boxes.push_back(down);
    }

    // what lies on the ground
    {
        InvBox gb;
        gb.kind = INV_GROUND;
        gb.x = L.ground_x;
        gb.y = L.ground_y;
        gb.w = L.ground_w;
        gb.h = L.ground_h;
        L.boxes.push_back(gb);
        const int n = int(m.ground.size());
        L.content_w = n * (L.S + L.gap) + 8;
        L.hscroll = std::clamp(inv_hscroll.load(), 0, std::max(0, L.content_w - L.ground_w));
        for (int j = 0; j < n; ++j) {
            InvBox ib;
            ib.kind = INV_GROUND_ITEM;
            ib.b = j;
            ib.x = L.ground_x + 4 + j * (L.S + L.gap) - L.hscroll;
            ib.y = L.ground_items_y;
            ib.w = ib.h = L.S;
            L.boxes.push_back(ib);
        }
        if (L.content_w > L.ground_w) {
            // the game has no scrollbar across: its own, turned a quarter
            L.hbar.kind = INV_HTRACK;
            L.hbar.x = L.ground_x;
            L.hbar.y = L.ground_items_y + L.S + 6;
            L.hbar.w = L.ground_w / c.ch * c.ch;
            L.hbar.h = 2 * c.cw;
            InvBox left = L.hbar, right = L.hbar, track = L.hbar;
            left.kind = INV_HLEFT;
            left.w = c.ch;
            right.kind = INV_HRIGHT;
            right.x = L.hbar.x + L.hbar.w - c.ch;
            right.w = c.ch;
            track.x += c.ch;
            track.w -= 2 * c.ch;
            L.boxes.push_back(track);
            L.boxes.push_back(left);
            L.boxes.push_back(right);
        }
    }

    InvBox done;
    done.kind = INV_DONE;
    done.w = 9 * c.cw;  // "Close", where the journal and the map have theirs; 3 rows, as the game's button art is
    done.h = 3 * c.ch;
    done.x = L.w - 3 * c.cw - done.w;
    done.y = 2 * c.ch + 2;
    L.boxes.push_back(done);

    // the game's list for one item: next to that item
    if (!m.popup.empty()) {
        size_t longest = 4;
        for (const std::string &p : m.popup)
            longest = std::max(longest, p.size());
        const int row = c.ch + 4;
        L.pop.kind = INV_POPUP;
        L.pop.w = std::min(int(longest + 3) * c.cw, L.w - 2 * pad);
        const int fit = std::max(1, (L.h - 2 * pad - row) / row);
        const int shown = std::min(int(m.popup.size()), fit);
        L.pop.h = shown * row + 12;
        int ax = L.w / 2 - L.pop.w / 2, ay = L.h / 2 - L.pop.h / 2;
        if (const InvBox *b = L.box_of(m, m.popup_item)) {
            ax = b->x + b->w + 8;
            if (ax + L.pop.w > L.w - pad / 2)
                ax = b->x - 8 - L.pop.w;
            ay = b->y;
        }
        L.pop.x = std::clamp(ax, pad / 2, std::max(pad / 2, L.w - pad / 2 - L.pop.w));
        L.pop.y = std::clamp(ay, pad / 2, std::max(pad / 2, L.h - pad / 2 - L.pop.h));
        for (int k = 0; k < shown; ++k) {
            InvBox r;
            r.kind = INV_ROW;
            r.a = k;
            r.x = L.pop.x + 4;
            r.y = L.pop.y + 6 + k * row;
            r.w = L.pop.w - 8;
            r.h = row;
            L.rows.push_back(r);
        }
    }

    // the split/share dialog, in the middle: title; the number and the slider;
    // the two stacks' counts; OK and Cancel
    if (m.split_on) {
        const int row = c.ch + 8;
        L.dlg.kind = INV_DIALOG;
        L.dlg.w = std::min(std::max(48 * c.cw, int(m.split_title.size() + 4) * c.cw), L.w - 2 * pad);
        L.dlg.h = 5 * row + 12;
        L.dlg.x = (L.w - L.dlg.w) / 2;
        L.dlg.y = (L.h - L.dlg.h) / 2;
        InvBox v;
        v.kind = INV_VALUE;
        v.x = L.dlg.x + c.cw;
        v.y = L.dlg.y + 6 + row;
        v.w = 7 * c.cw;
        v.h = row - 4;
        L.dlg_parts.push_back(v);
        InvBox sl;
        sl.kind = INV_SLIDER;
        sl.x = v.x + v.w + 2 * c.cw;
        sl.y = v.y;
        sl.w = L.dlg.x + L.dlg.w - c.cw - sl.x;
        sl.h = v.h;
        L.dlg_parts.push_back(sl);
        InvBox ok;
        ok.kind = INV_OK;
        ok.w = 8 * c.cw;
        ok.h = row - 4;
        ok.y = L.dlg.y + L.dlg.h - row - 4;
        ok.x = L.dlg.x + L.dlg.w / 2 - ok.w - c.cw;
        L.dlg_parts.push_back(ok);
        InvBox cancel = ok;
        cancel.kind = INV_CANCEL;
        cancel.w = 10 * c.cw;
        cancel.x = L.dlg.x + L.dlg.w / 2 + c.cw;
        L.dlg_parts.push_back(cancel);
    }
    return true;
}

// A scrollbar's handle along its track: start and length, in pixels along it.
// track: the room between the arrows; shown: what fits; total: all of it.
void scroll_handle(int track, int shown, int total, int scroll, int unit, int &at, int &len) {
    len = std::clamp(total > 0 ? track * shown / total : track, std::min(track, 2 * unit), track);
    len = std::max(unit, len / unit * unit);
    const int span = std::max(0, track - len), most = std::max(1, total - shown);
    at = span * std::clamp(scroll, 0, most) / most;
}

// the scroll for a pointer at `pos` along a track (the handle's middle under it)
int scroll_at(int track, int shown, int total, int pos, int unit) {
    int at, len;
    scroll_handle(track, shown, total, 0, unit, at, len);
    const int span = std::max(1, track - len);
    return std::clamp((pos - len / 2) * std::max(0, total - shown) / span, 0, std::max(0, total - shown));
}

// the slider's value at a pixel across it (panel pixels)
int slider_value(const InvModel &m, const InvBox &sl, int px) {
    if (m.split_hi <= m.split_lo || sl.w <= 8)
        return m.split_lo;
    const float f = std::clamp(float(px - sl.x - 4) / float(sl.w - 8), 0.0f, 1.0f);
    return m.split_lo + int(f * float(m.split_hi - m.split_lo) + 0.5f);
}

// ---------------------------------------------------------------------------
// drawing (render thread)

const uint32_t INV_BG = fpv::rgb(18, 20, 26), INV_SQUARE = fpv::rgb(30, 32, 42), INV_EDGE = fpv::rgb(70, 76, 92);
const uint32_t INV_GREEN = fpv::rgb(120, 230, 110);

void canvas_line(fpv::Canvas &cv, int x0, int y0, int x1, int y1, uint32_t color) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4000; ++guard) {
        cv.fill(x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// an item's picture in a square, or its first letter
void paint_item(fpv::Canvas &cv, const InvItem &it, int x, int y, int side, int cw, int ch) {
    if (it.pic && it.pic->ok()) {
        cv.blitFit(*it.pic, x, y, side, side);
    } else {
        const std::string letter(1, it.name.empty() ? '?' : char(std::toupper(uint8_t(it.name[0]))));
        const int lw = std::max(8, std::min(cw, side / 2)), lh = lw * 3 / 2;
        panel_text(cv, x + (side - lw) / 2, y + (side - lh) / 2, letter, UI_DIM, lw, lh);
    }
    // how many, in the bottom right corner, on a dark patch
    if (it.count > 1) {
        const std::string n = std::to_string(it.count);
        const int nw = side >= 28 ? 8 : 6, nh = side >= 28 ? 12 : 9;
        const int tw = int(n.size()) * nw;
        const int tx = x + side - tw - 1, ty = y + side - nh - 1;
        cv.fill(tx - 1, ty, tw + 2, nh + 1, fpv::rgb(8, 8, 10));
        panel_text(cv, tx, ty, n, UI_WHITE, nw, nh);
    }
}

// The game's weight sign (Γ, 226 in its code page) as a picture: media/weight.png,
// or this one built in
const char *const WEIGHT_ROWS[12] = {"....####....", "...#....#...", "...#....#...", "....####....",
                                     "...######...", "..########..", "..########..", ".##########.",
                                     ".##########.", "############", "############", "............"};
std::shared_ptr<const fpv::Image> weight_image;
std::string weight_dir = "?";

const fpv::Image &weight_picture() {
    const std::shared_ptr<const fpv::Media> media = get_media();
    const std::string dir = media ? media->dir : "";
    if (!weight_image || dir != weight_dir) {
        weight_dir = dir;
        auto img = std::make_shared<fpv::Image>();
        if (dir.empty() || !fpv::load_png(dir + "/weight.png", *img)) {
            img->w = img->h = 12;
            img->px.assign(144, 0);
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < 12; ++x)
                    if (WEIGHT_ROWS[y][x] == '#')
                        img->px[size_t(y) * 12 + x] = fpv::rgb(176, 180, 190);
        }
        weight_image = img;
    }
    return *weight_image;
}

void paint_burden(fpv::Canvas &cv, const std::string &burden, int x, int y, int cw, int ch) {
    std::string part;
    const auto flush = [&] {
        panel_text(cv, x, y, part, UI_DIM, cw, ch);
        x += int(part.size()) * cw;
        part.clear();
    };
    for (const char c : burden) {
        if (uint8_t(c) == 226) {
            flush();
            const fpv::Image &w = weight_picture();
            const int side = std::min(ch, cw * 2);
            if (w.ok())
                cv.blitFit(w, x + 1, y + (ch - side) / 2, side, side);
            x += side + 3;
        } else if (uint8_t(c) < 128) {
            part += c;
        }
    }
    flush();
}

// The game's scrollbar, upright, into a canvas of its own size (2 cells wide):
// arrow, track, arrow; the handle from `at` for `len` pixels along the track.
void paint_vbar(fpv::Canvas &cv, const InvModel &m, int w, int h, int at, int len, int cw, int ch) {
    cv.resize(w, h);
    const auto tile = [&](int k, int x, int y, uint32_t fallback) {
        if (m.scroll[k] && m.scroll[k]->ok())
            cv.blitFit(*m.scroll[k], x, y, cw, ch);
        else
            cv.fill(x, y, cw, ch, fallback);
    };
    const int cells = std::max(2, h / ch);
    for (int r = 0; r < cells; ++r) {
        const int row = r == 0 ? 0 : r == cells - 1 ? 2 : 1;
        for (int col = 0; col < 2; ++col)
            tile(row * 2 + col, col * cw, r * ch, row == 1 ? fpv::rgb(40, 34, 24) : fpv::rgb(150, 110, 40));
    }
    // the handle: its top, its middle, filler, its bottom
    const int n = std::max(1, len / ch), first = (ch + at) / ch;
    for (int i = 0; i < n && first + i < cells - 1; ++i) {
        const int part = n == 1 ? 1 : i == 0 ? 0 : i == n - 1 ? 3 : i == n / 2 ? 1 : 2;
        for (int col = 0; col < 2; ++col)
            tile(6 + part * 2 + col, col * cw, (first + i) * ch, fpv::rgb(200, 150, 60));
    }
}

// the same turned a quarter to the left, for a scrollbar across: its top end on the left
void put_turned(fpv::Canvas &dst, const fpv::Canvas &src, int x, int y) {
    // src is w x h (upright); dst gets h x w at x, y
    for (int ny = 0; ny < src.w; ++ny)
        for (int nx = 0; nx < src.h; ++nx) {
            const uint32_t c = src.px[size_t(nx) * src.w + (src.w - 1 - ny)];
            const int dx = x + nx, dy = y + ny;
            if (fpv::px_a(c) >= 128 && dx >= 0 && dy >= 0 && dx < dst.w && dy < dst.h)
                dst.px[size_t(dy) * dst.w + dx] = c | 0xFF000000u;
        }
}

void put_upright(fpv::Canvas &dst, const fpv::Canvas &src, int x, int y) {
    for (int yy = 0; yy < src.h; ++yy)
        for (int xx = 0; xx < src.w; ++xx) {
            const uint32_t c = src.px[size_t(yy) * src.w + xx];
            const int dx = x + xx, dy = y + yy;
            if (fpv::px_a(c) >= 128 && dx >= 0 && dy >= 0 && dx < dst.w && dy < dst.h)
                dst.px[size_t(dy) * dst.w + dx] = c | 0xFF000000u;
        }
}

// the game's window art (gamestyle.inc, included after this file)
struct GameArt;
std::shared_ptr<const GameArt> get_game_art();
void paint_game_frame(fpv::Canvas &cv, const GameArt *a, int w, int h, int cw, int ch);
void paint_game_button(fpv::Canvas &cv, const GameArt *a, int x, int y, int w, int h, const std::string &label, bool hot,
                       bool on, int cw, int ch);
void paint_picture_box(fpv::Canvas &cv, const GameArt *a, int x, int y, int w, int h, int cw, int ch);

void paint_inv(const InvModel &m, const InvLayout &L, const InvBox &hover, int32_t drag, fpv::Canvas &cv) {
    const int cw = L.cw, ch = L.ch;
    cv.resize(L.w, L.h);
    {
        const std::shared_ptr<const GameArt> art = get_game_art();
        paint_game_frame(cv, art.get(), L.w, L.h, cw, ch);
    }
    const int pad = 3 * cw;
    const std::shared_ptr<const GameArt> art_hold = get_game_art();
    if (!m.burden.empty())
        paint_burden(cv, m.burden, pad, L.title_y, cw, ch);
    const bool dragging = drag >= 0;
    const auto hot = [&](int kind, int a, int b) { return hover.kind == kind && hover.a == a && hover.b == b; };

    // the sprite, its body parts and the lines between
    if (m.sprite && m.sprite->ok()) {
        const fpv::Image &img = *m.sprite;
        const float f = std::min(float(L.sprite_s) / img.w, float(L.sprite_s) / img.h);
        const int dw = std::max(1, int(img.w * f)), dh = std::max(1, int(img.h * f));
        cv.blitFit(img, L.sprite_x + (L.sprite_s - dw) / 2, L.sprite_y + (L.sprite_s - dh) / 2, dw, dh);
    }
    for (const auto &l : L.leads)
        canvas_line(cv, l[0], l[1], l[2], l[3], fpv::rgb(96, 80, 44));
    for (const auto &l : L.leads)
        cv.fill(l[2] - 1, l[3] - 1, 3, 3, fpv::rgb(150, 120, 60));
    for (const InvBox &b : L.boxes) {
        if (b.kind != INV_SLOT)
            continue;
        const InvSlot &sl = m.slots[b.a];
        const bool target = dragging && hot(INV_SLOT, b.a, -1);
        paint_picture_box(cv, art_hold.get(), b.x, b.y, b.w, b.h, cw, ch);
        if (target || hot(INV_SLOT, b.a, -1))
            cv.frame(b.x, b.y, b.w, b.h, target ? INV_GREEN : UI_YELLOW, target ? 2 : 1);
        if (sl.gone)
            for (int i = 4; i < b.w - 4; ++i) {
                cv.fill(b.x + i, b.y + i, 2, 1, fpv::rgb(200, 50, 40));
                cv.fill(b.x + b.w - 1 - i, b.y + i, 2, 1, fpv::rgb(200, 50, 40));
            }
        const int lw = int(sl.label.size()) * 8;
        panel_text(cv, b.x + (b.w - lw) / 2, b.y + b.h + 2, sl.label, sl.gone ? UI_DIM : fpv::rgb(200, 190, 160), 8,
                12);
        if (!sl.label2.empty()) {
            const int lw2 = int(sl.label2.size()) * 8;
            panel_text(cv, b.x + (b.w - lw2) / 2, b.y + b.h + 2 + 12, sl.label2,
                       sl.gone ? UI_DIM : fpv::rgb(200, 190, 160), 8, 12);
        }
    }
    for (const InvBox &b : L.boxes) {
        if (b.kind != INV_SLOT_ITEM)
            continue;
        const InvItem &it = m.slots[b.a].items[b.b];
        if (hot(INV_SLOT_ITEM, b.a, b.b) && !dragging)
            cv.fill(b.x, b.y, b.w, b.h, fpv::rgb(60, 56, 30));
        if (it.id != drag)
            paint_item(cv, it, b.x + 1, b.y + 1, b.w - 2, cw, ch);
        if (hot(INV_SLOT_ITEM, b.a, b.b))
            cv.frame(b.x, b.y, b.w, b.h, dragging ? INV_GREEN : UI_YELLOW, 1);
    }

    // what you carry
    for (const InvBox &b : L.boxes) {
        if (b.kind != INV_GROUP)
            continue;
        const InvGroup &g = m.groups[b.a];
        const bool target = dragging && (hot(INV_GROUP, b.a, -1) || (hover.kind == INV_ITEM && hover.a == b.a));
        // only what lies wholly inside the list's room is drawn
        const int y0 = std::max(b.y, L.list_y), y1 = std::min(b.y + b.h, L.list_y + L.list_h);
        if (y1 <= y0)
            continue;
        cv.fill(b.x, y0, b.w, y1 - y0, fpv::rgb(24, 26, 34));
        if (target)
            cv.frame(b.x, y0, b.w, y1 - y0, INV_GREEN, 2);
        if (b.y >= L.list_y && b.y + ch + 4 <= L.list_y + L.list_h) {
            const size_t room = size_t(std::max(4, b.w / cw - 2));
            std::string title = g.title;
            if (g.items.empty())
                title += " (empty)";
            panel_text(cv, b.x + 4, b.y + 3, title.substr(0, room), fpv::rgb(220, 190, 120), cw, ch);
        }
    }
    for (const InvBox &b : L.boxes) {
        if (b.kind != INV_ITEM || b.y < L.list_y || b.y + b.h > L.list_y + L.list_h)
            continue;
        const InvItem &it = m.groups[b.a].items[b.b];
        const bool h = hot(INV_ITEM, b.a, b.b);
        paint_picture_box(cv, art_hold.get(), b.x, b.y, b.w, b.h, cw, ch);
        if (h)
            cv.frame(b.x, b.y, b.w, b.h, dragging ? INV_GREEN : UI_YELLOW, 1);
        if (it.id != drag)
            paint_item(cv, it, b.x + 2, b.y + 2, b.w - 4, cw, ch);
    }
    if (m.groups.empty())
        panel_text(cv, L.list_x, L.list_y, "You carry nothing else.", UI_DIM, cw, ch);
    if (L.vbar.kind != INV_NONE) {
        int at, len;
        scroll_handle(L.vbar.h - 2 * ch, L.list_h, L.content_h, L.scroll, ch, at, len);
        fpv::Canvas bar;
        paint_vbar(bar, m, L.vbar.w, L.vbar.h, at, len, cw, ch);
        put_upright(cv, bar, L.vbar.x, L.vbar.y);
    }

    // what lies on the ground: one row, scrolled across
    {
        const bool target = dragging && (hover.kind == INV_GROUND || hover.kind == INV_GROUND_ITEM);
        cv.fill(L.ground_x, L.ground_y, L.ground_w, L.ground_h, fpv::rgb(24, 26, 34));
        if (target)
            cv.frame(L.ground_x, L.ground_y, L.ground_w, L.ground_h, INV_GREEN, 2);
        panel_text(cv, L.ground_x + 4, L.ground_y + 3, m.ground.empty() ? "Nothing on the ground here" : "On the ground",
                   fpv::rgb(220, 190, 120), cw, ch);
        for (const InvBox &b : L.boxes) {
            if (b.kind != INV_GROUND_ITEM || b.x < L.ground_x || b.x + b.w > L.ground_x + L.ground_w)
                continue;
            const InvItem &it = m.ground[b.b];
            const bool h = hot(INV_GROUND_ITEM, -1, b.b);
            paint_picture_box(cv, art_hold.get(), b.x, b.y, b.w, b.h, cw, ch);
            if (h)
                cv.frame(b.x, b.y, b.w, b.h, dragging ? INV_GREEN : UI_YELLOW, 1);
            if (it.id != drag)
                paint_item(cv, it, b.x + 2, b.y + 2, b.w - 4, cw, ch);
        }
        if (L.hbar.kind != INV_NONE) {
            int at, len;
            scroll_handle(L.hbar.w - 2 * ch, L.ground_w, L.content_w, L.hscroll, ch, at, len);
            fpv::Canvas bar;
            paint_vbar(bar, m, L.hbar.h, L.hbar.w, at, len, cw, ch);
            put_turned(cv, bar, L.hbar.x, L.hbar.y);
        }
    }

    for (const InvBox &b : L.boxes) {
        if (b.kind != INV_DONE)
            continue;
        const std::shared_ptr<const GameArt> art = get_game_art();
        paint_game_button(cv, art.get(), b.x, b.y, b.w, b.h, "Close", hot(INV_DONE, -1, -1), false, cw, ch);
    }

    // the help at the bottom
    std::string line;
    if (dragging)
        line = "Drop it on a body part to wear or hold it, on a container to put it in, on the ground to drop it.";
    else if (m.split_on)
        line = "Drag the slider or type the number. Enter or OK to do it, Esc to cancel.";
    else if (!m.popup.empty())
        line = "Click what to do. Esc or a click elsewhere goes back.";
    else
        line = "Click or right-click an item for what you can do with it. Drag it onto a body part or into a container.";
    panel_text(cv, pad, L.status_y + ch / 2, line.substr(0, size_t(std::max(4, (L.w - 2 * pad) / cw))), UI_DIM, cw,
            ch);

    // the game's list for one item
    if (L.pop.kind != INV_NONE) {
        const InvBox &p = L.pop;
        cv.fill(p.x + 4, p.y + 4, p.w, p.h, fpv::rgb(4, 4, 6));
        cv.fill(p.x, p.y, p.w, p.h, fpv::rgb(14, 15, 20));
        cv.frame(p.x, p.y, p.w, p.h, UI_GOLD, 1);
        const size_t room = size_t(std::max(4, p.w / cw - 2));
        for (const InvBox &r : L.rows) {
            const bool h = hot(INV_ROW, r.a, -1);
            if (h)
                cv.fill(r.x, r.y, r.w, r.h, fpv::rgb(52, 56, 74));
            panel_text(cv, r.x + cw - 4, r.y + 2, m.popup[r.a].substr(0, room), h ? UI_YELLOW : UI_WHITE, cw, ch);
        }
    }

    // the split/share dialog
    if (L.dlg.kind != INV_NONE) {
        const InvBox &d = L.dlg;
        cv.fill(d.x + 4, d.y + 4, d.w, d.h, fpv::rgb(4, 4, 6));
        cv.fill(d.x, d.y, d.w, d.h, fpv::rgb(14, 15, 20));
        cv.frame(d.x, d.y, d.w, d.h, UI_GOLD, 2);
        const size_t room = size_t(std::max(4, d.w / cw - 2));
        panel_text(cv, d.x + cw, d.y + 8, m.split_title.substr(0, room), UI_WHITE, cw, ch);
        for (const InvBox &b : L.dlg_parts) {
            if (b.kind == INV_VALUE) {
                cv.fill(b.x, b.y, b.w, b.h, fpv::rgb(6, 6, 8));
                cv.frame(b.x, b.y, b.w, b.h, fpv::rgb(150, 150, 160), 1);
                const std::string t = m.split_text + "_";
                panel_text(cv, b.x + 4, b.y + (b.h - ch) / 2, t.substr(0, size_t(std::max(1, b.w / cw - 1))),
                           UI_YELLOW, cw, ch);
            } else if (b.kind == INV_SLIDER) {
                const int mid = b.y + b.h / 2;
                cv.fill(b.x + 4, mid - 1, b.w - 8, 3, fpv::rgb(90, 94, 110));
                const float f = m.split_hi > m.split_lo ? float(m.split_value - m.split_lo) /
                                                              float(m.split_hi - m.split_lo)
                                                        : 0.0f;
                const int hx = b.x + 4 + int(f * float(b.w - 8));
                cv.fill(hx - 3, b.y + 1, 7, b.h - 2, hot(INV_SLIDER, -1, -1) ? UI_YELLOW : UI_GOLD);
                // the ends: what the slider goes from and to
                const std::string lo = std::to_string(m.split_lo), hi = std::to_string(m.split_hi);
                panel_text(cv, b.x, b.y + b.h + 2, lo, UI_DIM, 8, 12);
                panel_text(cv, b.x + b.w - int(hi.size()) * 8, b.y + b.h + 2, hi, UI_DIM, 8, 12);
            } else {
                const bool h = hot(b.kind, -1, -1);
                cv.fill(b.x, b.y, b.w, b.h, h ? fpv::rgb(60, 56, 30) : fpv::rgb(28, 30, 38));
                cv.frame(b.x, b.y, b.w, b.h, h ? UI_YELLOW : fpv::rgb(96, 102, 118), 1);
                const std::string t = b.kind == INV_OK ? "OK" : "Cancel";
                panel_text(cv, b.x + (b.w - int(t.size()) * cw) / 2, b.y + (b.h - ch) / 2, t, h ? UI_YELLOW : UI_WHITE,
                           cw, ch);
            }
        }
        const int cy = d.y + 6 + 3 * (ch + 8) - 4;
        panel_text(cv, d.x + cw, cy, m.split_left.substr(0, room / 2), fpv::rgb(220, 190, 120), cw, ch);
        panel_text(cv, d.x + d.w - cw - int(std::min(m.split_right.size(), room / 2)) * cw, cy,
                   m.split_right.substr(0, room / 2), fpv::rgb(220, 190, 120), cw, ch);
    }
}

UiTexture tex_inv, tex_inv_tip, tex_inv_drag;
const InvModel *inv_drawn = nullptr;
std::string inv_drawn_state, inv_tip_text;
int32_t inv_drag_drawn = -1;

void destroy_inv_textures() {
    tex_inv.destroy();
    tex_inv_tip.destroy();
    tex_inv_drag.destroy();
    inv_drawn = nullptr;
    inv_drawn_state.clear();
    inv_tip_text.clear();
    inv_drag_drawn = -1;
}

// render thread, after the game's interface: the panel in front of everything
void draw_inventory(SDL_Renderer *sdl) {
    const std::shared_ptr<const InvModel> m = inv_up ? get_inv() : nullptr;
    if (!m || !ui_font_ready) {
        inv_drawn = nullptr;
        return;
    }
    InvLayout L;
    if (!layout_inv(*m, inv_scroll.load(), L))
        return;
    int px = -1, py = -1;
    if (debug_px.load() >= 0) {
        px = debug_px.load();
        py = debug_py.load();
    } else {
        sdl_get_mouse_state(&px, &py);
    }
    const InvBox hover = L.at(px, py);
    const int32_t drag = inv_drag.load();
    const std::string state = std::to_string(hover.kind) + "," + std::to_string(hover.a) + "," +
                              std::to_string(hover.b) + "," + std::to_string(drag) + "," + std::to_string(L.scroll) + "," +
                              std::to_string(L.hscroll) +
                              "," + std::to_string(L.w) + "," + std::to_string(L.h);
    if (m.get() != inv_drawn || state != inv_drawn_state) {
        fpv::Canvas cv;
        paint_inv(*m, L, hover, drag, cv);
        if (!tex_inv.upload(sdl, cv.px.data(), cv.w, cv.h))
            return;
        inv_drawn = m.get();
        inv_drawn_state = state;
    }
    tex_inv.draw(sdl, L.x, L.y);

    // the name of what the pointer is on, next to it
    std::string tip;
    if (drag < 0) {
        if (const InvItem *it = inv_item_at(*m, hover)) {
            tip = it->name;
            if (hover.kind == INV_SLOT_ITEM)
                tip += m->slots[hover.a].held ? " (held in " + m->slots[hover.a].part + ")"
                                              : " (" + m->slots[hover.a].part + ")";
        } else if (hover.kind == INV_SLOT) {
            const InvSlot &sl = m->slots[hover.a];
            tip = sl.part + (sl.held ? ", held" : "") + (sl.gone ? ": gone" : sl.items.empty() ? ": nothing" : "");
        }
    }
    if (!tip.empty()) {
        if (tip != inv_tip_text) {
            fpv::Canvas cv;
            cv.resize(int(tip.size()) * L.cw + 12, L.ch + 8);
            cv.fill(0, 0, cv.w, cv.h, fpv::rgb(12, 12, 16));
            cv.frame(0, 0, cv.w, cv.h, UI_GOLD, 1);
            panel_text(cv, 6, 4, tip, UI_WHITE, L.cw, L.ch);
            if (!tex_inv_tip.upload(sdl, cv.px.data(), cv.w, cv.h))
                return;
            inv_tip_text = tip;
        }
        Cells c;
        get_cells(c);
        const int right = c.ox + c.dimx * c.cw, low = c.oy + c.dimy * c.ch;
        int tx = px + 18, ty = py + 20;
        if (tx + tex_inv_tip.w > right)
            tx = px - 8 - tex_inv_tip.w;
        if (ty + tex_inv_tip.h > low)
            ty = py - 8 - tex_inv_tip.h;
        tex_inv_tip.draw(sdl, tx, ty);
    }

    // what is being dragged, at the pointer
    if (drag >= 0) {
        const InvItem *it = m->item(drag);
        if (it && (drag != inv_drag_drawn || !tex_inv_drag.tex)) {
            fpv::Canvas cv;
            cv.resize(L.S, L.S);
            cv.fill(0, 0, L.S, L.S, (fpv::rgb(30, 32, 42) & 0x00FFFFFFu) | 0xA0000000u);
            paint_item(cv, *it, 2, 2, L.S - 4, L.cw, L.ch);
            if (tex_inv_drag.upload(sdl, cv.px.data(), cv.w, cv.h))
                inv_drag_drawn = drag;
        }
        if (it && tex_inv_drag.tex) {
            if (sdl_set_texture_alpha_mod)
                sdl_set_texture_alpha_mod(tex_inv_drag.tex, 220);
            tex_inv_drag.draw(sdl, px - L.S / 2, py - L.S / 2);
        }
    }
    // a note (flash) over the panel, where it would be hidden under it
    draw_flash(sdl, px, py);
}

// ---------------------------------------------------------------------------
// what the hooks keep between frames (simulation thread); see inventory_hook.inc

// a press of the left button on the panel, until it is let go
struct InvPress {
    bool on = false;
    int x0 = 0, y0 = 0;
    InvBox hit;
    int32_t item = -1;          // the item it was on
    bool dragging = false;
} inv_press;

// What the panel has asked of the game's inventory, done over a few frames
// (a list of the game's takes no click in the frame it opened in).
enum { INV_OPEN = 1, INV_PICK, INV_DO, INV_BACK, INV_CLOSE, INV_PICKUP };
enum { WANT_WEAR = 1, WANT_REMOVE, WANT_PUT, WANT_DROP };
struct InvDrive {
    bool on = false;
    int kind = 0;
    int32_t item = -1;          // INV_OPEN, INV_DO: the item whose list to open
    int row = -1;               // INV_PICK: the row of the game's list
    std::string row_text;
    int want = 0;               // INV_DO: what to pick from the item's list
    int32_t container = -1;     // WANT_PUT: into what (-1: whatever the game offers first)
    int stage = 0, wait = 0, frames = 0;
    int tries = 0;              // pages turned looking for a row
} inv_drive;

// What to do with an item once something the game does first is done (taking
// it off, picking it up), when it shows in the inventory's list again.
struct InvThen {
    int32_t item = -1;
    int want = 0;
    int32_t container = -1;
    int64_t until = 0;
} inv_then;

// paging through the unseen list for the game's pictures (collect_game_icons):
// the scroll it started from; -1: not paging
int icon_page_from = -1;
int icon_page_rows = 0;     // how many of the list's items the game showed on its page

// The game closes its inventory after most things done from it. Until this
// time (ms), once the game takes input again on its plain screen, it is
// opened again (A_INV_LOOK), the panel staying up meanwhile. 0: not to be.
int64_t inv_reopen_until = 0;
uint32_t inv_reopen_from = 0;   // the drawn frame (inv_render_frame) of the action
uint32_t inv_reopen_tried = 0;  // the drawn frame the key was last sent

// drawn frames, counted by inventory_frame: inventory_logic acts once for each
uint32_t inv_render_frame = 0;

// A split or share confirmed in the dialog, done once the game's inventory is
// closed (its lists hold pointers to the items, and one may be removed).
struct InvApply {
    bool on = false;
    InvSplit what;
    int frames = 0;
    // or: a container strapped onto a body part over what is worn there (not the game's)
    int32_t strap_item = -1;    // -1: a split or share
    int strap_bp = -1;
    std::string strap_onto;     // the worn thing's name, for the note
} inv_apply;

void inv_request(int kind, int32_t item = -1, int want = 0, int32_t container = -1) {
    inv_drive = InvDrive();
    inv_drive.on = true;
    inv_drive.kind = kind;
    inv_drive.item = item;
    inv_drive.want = want;
    inv_drive.container = container;
}
