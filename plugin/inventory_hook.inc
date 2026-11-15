// The inventory panel's part in the game's hooks (inventory.inc has the rest).
// Included inside dungeon_hook: functions only. A data member here would lie
// past the end of the game's own object (see AGENTS.md); state is kept in
// inventory.inc's file-level variables.

// Click the row of the game's inventory list that shows this text, where the
// game has drawn it. skip: how many rows with the same text above it to pass.
// A row of the list starts with its key letter and a space.
bool click_inv_row_text(const std::string &text, int skip) {
    if (text.empty())
        return false;
    for (int strict = 1; strict >= 0; --strict) {
        int left = skip;
        for (int y = 0; y < gps->dimy; ++y) {
            std::string row(size_t(gps->dimx), ' ');
            for (int x = 0; x < gps->dimx; ++x) {
                const unsigned char ch = inv_cell_char(x, y);
                row[x] = ch >= 32 ? char(ch) : ' ';
            }
            for (size_t at = row.find(text); at != std::string::npos; at = row.find(text, at + 1)) {
                if (at > 0 && row[at - 1] != ' ')
                    continue;
                // the whole of it, not the start of a longer name
                const size_t end = at + text.size();
                if (end < row.size() && row[end] != ' ')
                    continue;
                if (strict) {
                    const char key = at >= 2 ? row[at - 2] : ' ';
                    if (!((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')) || (at >= 3 && row[at - 3] != ' '))
                        continue;
                }
                if (left-- > 0)
                    continue;
                click_cell(int(at) + 1, y);
                return true;
            }
        }
    }
    return false;
}

// The game's list never scrolled past its last page: the game may not expect
// that (a game crash while drawing its window was seen with the list scrolled
// to any entry, 2026-11-14; not proven to be the cause).
int32_t inv_scroll_for(size_t entries, int k) {
    const int page = std::max(1, icon_page_rows);
    return std::clamp(k, 0, std::max(0, int(entries) - page));
}

// Click entry k of the game's inventory list (its full text in a list for one
// item, its name in the list of everything) where the game drew it this frame.
// No extra drawing of the game's screen for it: such a draw, of the map and of
// the game's own window, could show for a moment before it is emptied again.
// 1: clicked. 0: not on the page; the list is scrolled to it, to be tried
// again next frame. -1: not to be found.
int click_inv_entry(const std::vector<df::adventure_optionst *> &list, int k, bool full, int tries) {
    if (k < 0 || size_t(k) >= list.size())
        return -1;
    auto &inv = game->main_interface.adventure.inventory;
    const std::string text = option_text(list[k], full);
    const int first = std::clamp(int(inv.scroll_position), 0, k);
    int skip = 0;
    for (int j = first; j < k; ++j)
        if (option_text(list[j], full) == text)
            ++skip;
    if (click_inv_row_text(text, skip)) {
        mouse_log.push_back("inventory: clicked '" + text + "'");
        return 1;
    }
    if (tries >= 2) {
        mouse_log.push_back("inventory: '" + text + "' not found on the game's page");
        return -1;
    }
    inv.scroll_position = inv_scroll_for(list.size(), tries == 0 ? k : k - 3);
    return 0;
}

// The pictures the game draws beside the items of its list: each a texture
// anchored over 5 x 3 cells (its top left cell has flag 0), the item's name on
// the row under its top, after the key letter. Matched to the list's items in
// order from the first one on the page. Returns how many items were on it.
int read_game_icons() {
    auto &inv = game->main_interface.adventure.inventory;
    const auto &list = inv.option_current;
    size_t next = size_t(std::clamp(int(inv.scroll_position), 0, int(list.size())));
    int rows = 0;
    for (int y = 0; y + 1 < gps->dimy; ++y)
        for (int x = 0; x < gps->dimx; ++x) {
            const int32_t tex = int32_t(inv_cell_anchored(x, y));
            if (!tex || inv_cell_flag(x, y) != 0)
                continue;
            // the name: on the next row, right of the picture
            std::string line;
            for (int xx = x + 5; xx < gps->dimx; ++xx) {
                const unsigned char ch = inv_cell_char(xx, y + 1);
                line += ch >= 32 ? char(ch) : ' ';
            }
            const size_t at = line.find_first_not_of(' ');
            if (at == std::string::npos)
                continue;
            line = line.substr(at);
            if (line.size() > 2 && line[1] == ' ')
                line = line.substr(2);      // the key letter
            line = rtrim(line.substr(0, line.find("  ")));
            for (size_t i = next; i < list.size(); ++i) {
                auto *io = list[i] ? virtual_cast<df::adventure_option_inventory_itemst>(list[i]) : nullptr;
                if (!io || !io->item)
                    continue;
                const std::string name = option_text(io, false);
                if (name.empty() || line.compare(0, name.size(), name) != 0)
                    continue;
                inv_game_icon[io->item->id] = tex;
                next = i + 1;
                ++rows;
                break;
            }
            break;   // one picture a row
        }
    return rows;
}

// While some item of the list has no picture from the game yet, the unseen
// list is paged through, a page a frame (never past its last page), and put
// back where it was.
void collect_game_icons() {
    auto &inv = game->main_interface.adventure.inventory;
    using C = df::adventure_interface_inventory_context_type;
    if (inv.context != C::MAIN || inv_drive.on || inv_press.on || inv_split.on)
        return;
    const int rows = read_game_icons();
    if (rows > 0)
        icon_page_rows = std::max(icon_page_rows, rows);
    const auto &list = inv.option_current;
    int missing = -1;
    for (size_t i = 0; i < list.size() && missing < 0; ++i) {
        auto *io = list[i] ? virtual_cast<df::adventure_option_inventory_itemst>(list[i]) : nullptr;
        if (io && io->item && !inv_game_icon.count(io->item->id))
            missing = int(i);
    }
    if (missing >= 0 && icon_page_rows > 0) {
        const int32_t to = inv_scroll_for(list.size(), missing);
        if (to > inv.scroll_position) {
            if (icon_page_from < 0)
                icon_page_from = inv.scroll_position;
            inv.scroll_position = to;
            return;
        }
    }
    // one the game did not show where it was looked for: 0 marks it looked at
    if (missing >= 0 && icon_page_rows > 0)
        if (auto *io = virtual_cast<df::adventure_option_inventory_itemst>(list[missing]))
            inv_game_icon[io->item->id] = 0;
    if (icon_page_from >= 0) {
        inv.scroll_position = icon_page_from;
        icon_page_from = -1;
    }
}

// ---------------------------------------------------------------------------
// splitting and sharing stacks (not something the game itself offers)

// a thing that comes in stacks
bool stackable(df::item *it) {
    using T = df::item_type;
    switch (it->getType()) {
    case T::AMMO: case T::COIN: case T::MEAT: case T::FISH: case T::FISH_RAW: case T::PLANT:
    case T::PLANT_GROWTH: case T::SEEDS: case T::CHEESE: case T::FOOD: case T::DRINK: case T::POWDER_MISC:
    case T::LIQUID_MISC: case T::GLOB: case T::BAR: case T::ROUGH: case T::SMALLGEM: case T::EGG:
    case T::THREAD: case T::VERMIN:
        return true;
    default:
        return it->getStackSize() > 1;
    }
}

// two stacks of exactly the same thing
bool same_kind(df::item *a, df::item *b) {
    if (!a || !b || a == b || a->getType() != b->getType() || a->getSubtype() != b->getSubtype() ||
        a->getMaterial() != b->getMaterial() || a->getMaterialIndex() != b->getMaterialIndex() ||
        a->getQuality() != b->getQuality() || a->getMaker() != b->getMaker() || a->getWear() != b->getWear() ||
        a->flags.bits.artifact || b->flags.bits.artifact || !stackable(a) || !stackable(b))
        return false;
    std::vector<df::item *> in;
    Items::getContainedItems(a, &in);
    if (!in.empty())
        return false;
    Items::getContainedItems(b, &in);
    return in.empty();
}

// room left in a container, in the game's volume units; -1: no limit known
int container_room(df::item *cont) {
    if (!cont)
        return -1;
    const int cap = Items::getCapacity(cont);
    if (cap <= 0)
        return -1;
    std::vector<df::item *> in;
    Items::getContainedItems(cont, &in);
    int used = 0;
    for (df::item *i : in)
        used += std::max(0, int(i->getVolume()));
    return std::max(0, cap - used);
}

int volume_each(df::item *it) {
    return std::max(1, int(it->getVolume()) / std::max(1, int(it->getStackSize())));
}

void open_split(const InvModel &m, int32_t id) {
    df::item *it = df::item::find(id);
    const InvItem *mi = m.item(id);
    if (!it || it->getStackSize() < 2 || (!Items::getContainer(it) && !it->flags.bits.on_ground)) {
        flash("Only a stack of several in a container or on the ground can be split");
        return;
    }
    inv_split = InvSplit();
    inv_split.on = true;
    inv_split.a = id;
    inv_split.total = it->getStackSize();
    inv_split.lo = 1;
    inv_split.hi = inv_split.total - 1;
    inv_split.value = inv_split.total / 2;
    inv_split.text = std::to_string(inv_split.value);
    inv_split.name_a = "this stack";
    inv_split.title = "Split " + (mi ? mi->name : std::string("the stack"));
}

void open_share(const InvModel &m, int32_t a_id, int32_t b_id) {
    df::item *a = df::item::find(a_id), *b = df::item::find(b_id);
    if (!same_kind(a, b))
        return;
    const int sa = a->getStackSize(), sb = b->getStackSize(), total = sa + sb;
    int lo = 0, hi = total;
    // what has to move from one container into the other has to fit there
    df::item *ca = Items::getContainer(a), *cb = Items::getContainer(b);
    if (ca != cb) {
        const int each = volume_each(a);
        const int room_a = container_room(ca), room_b = container_room(cb);
        if (room_a >= 0)
            hi = std::min(hi, sa + room_a / each);
        if (room_b >= 0)
            lo = std::max(lo, sa - room_b / each);
    }
    if (lo >= hi) {
        flash("No room to move any");
        return;
    }
    const auto where = [&](df::item *c) {
        const InvItem *ci = c ? m.item(c->id) : nullptr;
        return ci ? "in " + ci->name : std::string("held");
    };
    inv_split = InvSplit();
    inv_split.on = true;
    inv_split.a = a_id;
    inv_split.b = b_id;
    inv_split.total = total;
    inv_split.lo = lo;
    inv_split.hi = hi;
    inv_split.value = std::clamp(sa, lo, hi);
    inv_split.text = std::to_string(inv_split.value);
    const InvItem *mi = m.item(a_id);
    inv_split.title = "Share out " + (mi ? mi->name : std::string("the stacks"));
    inv_split.name_a = ca == cb ? "dragged" : where(ca);
    inv_split.name_b = ca == cb ? "other" : where(cb);
}

void set_split_value(int v, bool from_text) {
    inv_split.value = std::clamp(v, inv_split.lo, inv_split.hi);
    if (!from_text) {
        inv_split.text = std::to_string(inv_split.value);
        inv_split.fresh = true;
    }
}

// the dialog's OK: done once the game's inventory is closed (see InvApply)
void confirm_split() {
    if (!inv_split.on)
        return;
    set_split_value(inv_split.text.empty() ? inv_split.value : std::atoi(inv_split.text.c_str()), false);
    if (inv_split.mode == 1) {
        // the game's own question: answered by the pick-up's next step
        inv_amount_value = inv_split.value;
        inv_amount_answer = 1;
        inv_split.on = false;
        return;
    }
    inv_apply = InvApply();
    inv_apply.on = true;
    inv_apply.what = inv_split;
    inv_split.on = false;
}

// simulation thread, with the game's inventory closed
void apply_split(const InvSplit &w) {
    df::item *a = df::item::find(w.a);
    if (!a)
        return;
    if (w.b < 0) {
        // a new stack of `value` beside the old one, in the same container
        if (a->getStackSize() != w.total || w.value < 1 || w.value >= w.total)
            return;
        df::item *cont = Items::getContainer(a);
        df::item *fresh = a->splitStack(w.value, true);
        if (!fresh)
            return;
        fresh->categorize(true);
        if (cont && Items::getContainer(fresh) != cont)
            Items::moveToContainer(fresh, cont);
        mouse_log.push_back("inventory: split " + std::to_string(w.value) + " off item " + std::to_string(w.a));
        return;
    }
    df::item *b = df::item::find(w.b);
    if (!same_kind(a, b) || a->getStackSize() + b->getStackSize() != w.total)
        return;
    const int va = std::clamp(w.value, 0, w.total), vb = w.total - va;
    if (va == 0) {
        b->setStackSize(w.total);
        Items::remove(a);
    } else if (vb == 0) {
        a->setStackSize(w.total);
        Items::remove(b);
    } else {
        a->setStackSize(va);
        b->setStackSize(vb);
    }
    mouse_log.push_back("inventory: shared " + std::to_string(va) + " / " + std::to_string(vb));
}

// The game straps a pouch (a container it does not count as wearable) over the
// clothes of the upper body when it makes an adventurer, but has no way to do
// it again once it is taken off. The mod does: a container dropped on a body
// part is strapped there, the way the game keeps it ("flask" mode on that part),
// if something to strap it to is worn there: a garment of the body (armour,
// shirt, coat, trousers ...). Returns false if this is not such a case.
bool try_strap(df::unit *adv, const InvSlot &slot, const InvItem &it) {
    df::item *item = df::item::find(it.id);
    if (!adv || !item || Items::getCapacity(item) <= 0 || (it.flags & INV_CAN_WEAR))
        return false;
    for (const df::unit_inventory_item *inv : adv->inventory)
        if (inv && inv->item == item && inv->mode == df::inv_item_role_type::Flask)
            return false;   // strapped on already
    df::item *onto = nullptr;
    for (const df::unit_inventory_item *inv : adv->inventory) {
        if (!inv || !inv->item || inv->mode != df::inv_item_role_type::Worn || inv->body_part_id != slot.bp)
            continue;
        const df::item_type t = inv->item->getType();
        if (t == df::item_type::ARMOR || t == df::item_type::PANTS) {
            onto = inv->item;
            break;
        }
    }
    if (!onto) {
        flash("Nothing worn there to strap it to");
        return true;
    }
    inv_apply = InvApply();
    inv_apply.on = true;
    inv_apply.strap_item = it.id;
    inv_apply.strap_bp = slot.bp;
    inv_apply.strap_onto = Items::getDescription(onto, 0, false);
    return true;
}

// simulation thread, with the game's inventory closed
void apply_strap(df::unit *adv, const InvApply &a) {
    df::item *item = df::item::find(a.strap_item);
    if (!adv || !item)
        return;
    if (Items::moveToInventory(item, adv, df::inv_item_role_type::Flask, a.strap_bp)) {
        flash("Strapped to the " + a.strap_onto);
        mouse_log.push_back("inventory: strapped item " + std::to_string(a.strap_item) + " to body part " +
                            std::to_string(a.strap_bp));
    } else {
        flash("Could not strap it on");
    }
}

void cancel_split() {
    if (inv_split.on && inv_split.mode == 1)
        inv_amount_answer = -1;
    inv_split.on = false;
}

// the inventory to be opened again once the game is done with what was asked
void reopen_after_action() {
    inv_reopen_until = now_ms() + 20000;
    inv_reopen_from = inv_render_frame;
    inv_reopen_tried = 0;
}

// ---------------------------------------------------------------------------

// whether the panel may stand in for the game's inventory (or wait for it) now
bool inventory_usable() {
    return active && showing && settings.inventory_panel && game && gps && World::getAdventurer() &&
           ui_font_ready && !game->main_interface.view_sheets.open &&
           Gui::getCurViewscreen(true) == static_cast<df::viewscreen *>(this);
}

// Right after the game's last draw of the frame (capture): what it drew is
// copied for the panel to read, and its inventory window is taken off the
// screen, so that the bottom of the screen is laid out as on the plain screen
// (bottom_is_plain sees the window reach down there otherwise) and nothing of
// it shows beside the panel. The window: the block of background pictures
// around its "Done" (read off the game's screen, 2026-11-14).
void inventory_grid_capture() {
    inv_grid.dimx = 0;
    inv_window_erased = false;
    if (!inventory_usable())
        return;
    auto &ai = game->main_interface.adventure;
    const bool picking = inv_drive.on && inv_drive.kind == INV_PICKUP && ai.option_list.open;
    if (!ai.inventory.open && !picking)
        return;
    const size_t cells = size_t(gps->dimx) * gps->dimy;
    inv_grid.dimx = gps->dimx;
    inv_grid.dimy = gps->dimy;
    inv_grid.screen.assign(gps->screen, gps->screen + cells * 8);
    inv_grid.anchored.assign(gps->screentexpos_anchored, gps->screentexpos_anchored + cells);
    inv_grid.flag.assign(gps->screentexpos_flag, gps->screentexpos_flag + cells);
    if (!ai.inventory.open)
        return;
    const auto lower = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < gps->dimx && y < gps->dimy && gps->screentexpos_lower[x * gps->dimy + y] != 0;
    };
    int dx = -1, dy = -1;
    for (int y = 0; y < gps->dimy && dx < 0; ++y)
        for (int x = 0; x + 3 < gps->dimx; ++x)
            if (inv_cell_char(x, y) == 'D' && inv_cell_char(x + 1, y) == 'o' && inv_cell_char(x + 2, y) == 'n' &&
                inv_cell_char(x + 3, y) == 'e') {
                dx = x;
                dy = y;
                break;
            }
    if (dx < 0)
        return;
    int x1 = dx + 3;
    while (lower(x1 + 1, dy))
        ++x1;
    int y0 = dy;
    while (lower(x1, y0 - 1))
        --y0;
    int x0 = x1;
    while (lower(x0 - 1, y0))
        --x0;
    int y1 = y0;
    while (lower(x1, y1 + 1))
        ++y1;
    if (x1 - x0 < 20 || y1 - y0 < 10)
        return;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y) {
            const int idx = x * gps->dimy + y;
            std::memset(&gps->screen[idx * 8], 0, 8);
            gps->screentexpos[idx] = 0;
            gps->screentexpos_lower[idx] = 0;
            gps->screentexpos_anchored[idx] = 0;
            gps->screentexpos_flag[idx] = 0;
        }
    inv_window_erased = true;
}

// pick up a thing from the ground through the game's own list (g), then
// (once the inventory is open again) do `want` with it
void ground_get(int32_t id, int want, int32_t container) {
    inv_ground_menu = -1;
    inv_request(INV_PICKUP, id);
    inv_then = InvThen();
    if (want) {
        inv_then.item = id;
        inv_then.want = want;
        inv_then.container = container;
    }
}

// what was asked for after something the game does first (taking off,
// picking up): done once the item is in the inventory's list
void inventory_then() {
    if (inv_then.item < 0 || inv_drive.on || inv_apply.on)
        return;
    if (!inv_then.until)
        inv_then.until = now_ms() + 10000;
    if (now_ms() > inv_then.until) {
        inv_then = InvThen();
        return;
    }
    auto &inv = game->main_interface.adventure.inventory;
    if (!inv.open || inv.context != df::adventure_interface_inventory_context_type::MAIN)
        return;
    for (df::adventure_optionst *o : inv.option_current) {
        auto *io = o ? virtual_cast<df::adventure_option_inventory_itemst>(o) : nullptr;
        if (!io || !io->item || io->item->id != inv_then.item)
            continue;
        // ready only when the game offers it (worn things are not put away)
        const uint32_t need = inv_then.want == WANT_PUT ? INV_CAN_PUT : inv_then.want == WANT_DROP ? INV_CAN_DROP
                              : inv_then.want == WANT_WEAR ? INV_CAN_WEAR : 0;
        if (need && !(io->option_flag.whole & need))
            return;
        inv_request(INV_DO, inv_then.item, inv_then.want, inv_then.container);
        inv_then = InvThen();
        return;
    }
}

// a click (or right click) on the panel
void inv_click(const InvModel &m, const InvBox &hit, bool right) {
    if (m.split_on) {
        if (hit.kind == INV_OK && !right)
            confirm_split();
        else if (hit.kind == INV_CANCEL || right)
            cancel_split();
        return;
    }
    InvLayout L;
    if ((hit.kind == INV_VUP || hit.kind == INV_VDOWN || hit.kind == INV_HLEFT || hit.kind == INV_HRIGHT) &&
        layout_inv(m, inv_scroll.load(), L)) {
        const int step = L.S + L.gap;
        if (hit.kind == INV_VUP || hit.kind == INV_VDOWN)
            inv_scroll = std::clamp(L.scroll + (hit.kind == INV_VUP ? -step : step), 0,
                                    std::max(0, L.content_h - L.list_h));
        else
            inv_hscroll = std::clamp(L.hscroll + (hit.kind == INV_HLEFT ? -step : step), 0,
                                     std::max(0, L.content_w - L.ground_w));
        return;
    }
    if (const InvItem *it = inv_item_at(m, hit)) {
        // a second click on the item whose list is open puts the list away
        if (!m.popup.empty() && m.popup_item == it->id) {
            if (m.popup_ground)
                inv_ground_menu = -1;
            else
                inv_request(INV_BACK);
        } else if (it->ground) {
            // ours: the game's list for the ground is not part of its inventory
            if (!m.popup.empty() && !m.popup_ground)
                inv_request(INV_BACK);
            inv_ground_menu = it->id;
        } else {
            inv_ground_menu = -1;
            inv_request(INV_OPEN, it->id);
        }
        return;
    }
    if (hit.kind == INV_ROW && hit.a >= 0 && hit.a == m.popup_split) {
        if (m.popup_ground)
            inv_ground_menu = -1;
        else
            inv_request(INV_BACK);
        open_split(m, m.popup_item);
        return;
    }
    if (hit.kind == INV_ROW && m.popup_ground) {
        ground_get(m.popup_item, 0, -1);
        return;
    }
    if (hit.kind == INV_ROW && hit.a >= 0 && size_t(hit.a) < m.popup.size()) {
        inv_request(INV_PICK);
        inv_drive.row = hit.a;
        inv_drive.row_text = m.popup[hit.a];
        return;
    }
    if (hit.kind == INV_DONE && !right) {
        inv_reopen_until = 0;
        inv_request(INV_CLOSE);
        return;
    }
    if (hit.kind != INV_POPUP && !m.popup.empty()) {
        if (m.popup_ground)
            inv_ground_menu = -1;
        else
            inv_request(INV_BACK);
    }
}

// an item let go of somewhere on the panel
void inv_drop(const InvModel &m, int32_t id, const InvBox &target) {
    const InvItem *it = m.item(id);
    if (!it)
        return;
    // onto a stack of the same thing: share the two out
    if (const InvItem *onto = inv_item_at(m, target))
        if (onto->id != id && same_kind(df::item::find(id), df::item::find(onto->id))) {
            open_share(m, id, onto->id);
            return;
        }
    const auto in = [&](const std::vector<InvItem> &items) {
        return std::any_of(items.begin(), items.end(), [&](const InvItem &x) { return x.id == id; });
    };
    if (target.kind == INV_GROUND || target.kind == INV_GROUND_ITEM) {
        // dropped (what is worn is taken off first)
        if (it->ground)
            return;
        if (it->flags & INV_CAN_DROP) {
            inv_request(INV_DO, id, WANT_DROP);
        } else if (it->flags & INV_CAN_REMOVE) {
            inv_request(INV_DO, id, WANT_REMOVE);
            inv_then = InvThen();
            inv_then.item = id;
            inv_then.want = WANT_DROP;
        } else {
            flash("Can't drop that");
        }
        return;
    }
    if (it->ground) {
        // picked up first; then worn, or put in the container it was dropped on
        if (target.kind == INV_SLOT || target.kind == INV_SLOT_ITEM)
            ground_get(id, m.slots[target.a].grasp ? 0 : WANT_WEAR, -1);
        else if (target.kind == INV_GROUP || target.kind == INV_ITEM)
            ground_get(id, m.groups[target.a].container >= 0 ? WANT_PUT : 0, m.groups[target.a].container);
        return;
    }
    if (target.kind == INV_SLOT || target.kind == INV_SLOT_ITEM) {
        const InvSlot &slot = m.slots[target.a];
        if (in(slot.items))
            return;
        // into a hand: out of where it is (taken off, or out of its container)
        if (slot.grasp) {
            if (it->flags & INV_CAN_REMOVE)
                inv_request(INV_DO, id, WANT_REMOVE);
            else
                flash("Can't take that into a hand");
            return;
        }
        if (it->flags & INV_CAN_WEAR)
            inv_request(INV_DO, id, WANT_WEAR);
        else if (!try_strap(World::getAdventurer(), slot, *it))
            flash("Can't wear that");
        return;
    }
    if (target.kind == INV_GROUP || target.kind == INV_ITEM) {
        const InvGroup &g = m.groups[target.a];
        if (in(g.items) || g.container == id)
            return;
        // into a container; what is worn is taken off first (the game puts away only what is in hand)
        if (g.container >= 0 && (it->flags & INV_CAN_PUT)) {
            inv_request(INV_DO, id, WANT_PUT, g.container);
        } else if (it->flags & INV_CAN_REMOVE) {
            // taken off (into a hand), then put in
            inv_request(INV_DO, id, WANT_REMOVE);
            if (g.container >= 0) {
                inv_then = InvThen();
                inv_then.item = id;
                inv_then.want = WANT_PUT;
                inv_then.container = g.container;
            }
        } else {
            flash("Can't put that there");
        }
    }
}

// Input while the panel is up; true if it was dealt with here. Mouse buttons
// are the panel's. Keys go on to the game's (unseen) inventory, as they would,
// except while the split dialog is up, which takes them all.
bool inventory_input(std::set<df::interface_key> *input) {
    if (!inv_up)
        return false;
    using namespace df::enums::interface_key;
    int px = -1, py = -1;
    pointer_pixels(px, py);
    const std::shared_ptr<const InvModel> m = get_inv();
    InvLayout L;
    const bool laid = m && layout_inv(*m, inv_scroll.load(), L);
    if (enabler->mouse_rbut) {
        enabler->mouse_rbut = 0;
        inv_press.on = false;
        inv_drag = -1;
        if (laid && !inv_apply.on)
            inv_click(*m, L.at(px, py), true);
        return true;
    }
    if (enabler->mouse_lbut) {
        enabler->mouse_lbut = 0;
        // the tabs over the panel: to the journal or the map (the game's inventory is closed first)
        {
            Cells c;
            const int leaf = laid && get_cells(c) ? leaf_tab_at(px, py, L.x, L.y, c.cw, c.ch) : -1;
            if (leaf == PT_JOURNAL || leaf == PT_MAP) {
                enabler->mouse_lbut_down = 0;
                panel_switch = leaf;
                panel_switch_tries = 0;
                return true;
            }
            if (leaf == PT_INVENTORY)
                return true;
        }
        if (!inv_press.on && laid && !inv_apply.on) {
            inv_press = InvPress();
            inv_press.on = true;
            inv_press.x0 = px;
            inv_press.y0 = py;
            inv_press.hit = L.at(px, py);
            if (const InvItem *it = inv_item_at(*m, inv_press.hit))
                inv_press.item = it->id;
            if (inv_press.hit.kind == INV_SLIDER && m->split_on)
                set_split_value(slider_value(*m, inv_press.hit, px - L.x), false);
        }
        return true;
    }
    if (inv_split.on) {
        if (input) {
            if (input->count(LEAVESCREEN)) {
                cancel_split();
            } else if (input->count(SELECT)) {
                confirm_split();
            } else if (input->count(STRING_A000) || input->count(STRING_A008)) {
                if (!inv_split.text.empty())
                    inv_split.text.pop_back();
                inv_split.fresh = false;
                if (!inv_split.text.empty())
                    set_split_value(std::atoi(inv_split.text.c_str()), true);
            } else {
                for (int d = 0; d < 10; ++d)
                    if (input->count(df::interface_key(int(STRING_A048) + d))) {
                        if (inv_split.fresh)
                            inv_split.text.clear();
                        inv_split.fresh = false;
                        if (inv_split.text.size() < 7)
                            inv_split.text += char('0' + d);
                        set_split_value(std::atoi(inv_split.text.c_str()), true);
                    }
            }
        }
        return true;
    }
    if (inv_press.dragging && input && input->count(LEAVESCREEN)) {
        inv_press.on = false;
        inv_drag = -1;
        return true;
    }
    // leaving the inventory yourself: it is not to be opened again
    if (input && (input->count(LEAVESCREEN) || input->count(A_INV_LOOK)) &&
        game->main_interface.adventure.inventory.context ==
            df::adventure_interface_inventory_context_type::MAIN)
        inv_reopen_until = 0;
    // keys wait while the panel's own clicks or changes are being made
    return inv_drive.on || inv_apply.on;
}

// What is about to be clicked in the game's list: whether the inventory is to
// be opened again after it (not after a look at the item, naming it, or
// throwing it, which go to screens of their own). Asked before the click: the
// game frees its lists when it acts.
bool reopens_after(df::adventure_optionst *o) {
    return o && !virtual_cast<df::adventure_option_view_itemst>(o) &&
           !virtual_cast<df::adventure_item_interact_give_namest>(o) && !virtual_cast<df::adventure_option_throw_itemst>(o);
}

// one step of what the panel asked of the game's inventory
void inventory_drive() {
    InvDrive &d = inv_drive;
    if (!d.on)
        return;
    auto &inv = game->main_interface.adventure.inventory;
    using C = df::adventure_interface_inventory_context_type;
    if (++d.frames > 90) {
        mouse_log.push_back("inventory: gave up at stage " + std::to_string(d.stage));
        d.on = false;
        return;
    }
    if (d.wait > 0) {
        --d.wait;
        return;
    }
    const auto leave = [&] {
        std::set<df::interface_key> keys{df::interface_key::LEAVESCREEN};
        INTERPOSE_NEXT(feed)(&keys);
    };
    switch (d.kind) {
    case INV_CLOSE:
        if (!inv.open) {
            d.on = false;
            return;
        }
        leave();
        d.wait = 1;
        return;
    case INV_BACK:
        if (inv.context != C::MAIN)
            leave();
        d.on = false;
        return;
    case INV_PICK: {
        if (inv.context == C::MAIN || inv.context == C::NONE) {
            d.on = false;
            return;
        }
        const auto &list = inv.option_current;
        int k = d.row;
        if (k < 0 || size_t(k) >= list.size() || option_text(list[k], true) != d.row_text) {
            k = -1;
            for (size_t i = 0; i < list.size() && k < 0; ++i)
                if (option_text(list[i], true) == d.row_text)
                    k = int(i);
        }
        const bool reopen = k >= 0 && reopens_after(list[k]);
        const int done = k < 0 ? -1 : click_inv_entry(list, k, true, d.stage++);
        if (done == 1 && reopen)
            reopen_after_action();
        if (done != 0)
            d.on = false;
        return;
    }
    case INV_OPEN:
    case INV_DO: {
        if (d.stage == 0) {
            // from the list of everything: back to it first
            if (inv.context != C::MAIN) {
                leave();
                d.wait = 1;
                return;
            }
            const auto &list = inv.option_current;
            int k = -1;
            for (size_t i = 0; i < list.size() && k < 0; ++i) {
                auto *io = list[i] ? virtual_cast<df::adventure_option_inventory_itemst>(list[i]) : nullptr;
                if (io && io->item && io->item->id == d.item)
                    k = int(i);
            }
            const int done = k < 0 ? -1 : click_inv_entry(list, k, false, d.tries++);
            if (done < 0)
                d.on = false;
            if (done == 1) {
                d.stage = 1;
                d.tries = 0;
                d.wait = 1;
            }
            return;
        }
        if (inv.context != C::ONE_ITEM_FULL_LIST || !inv.context_item || inv.context_item->id != d.item)
            return;     // not there yet
        if (d.kind == INV_OPEN) {
            d.on = false;
            return;
        }
        const auto &list = inv.option_current;
        int k = -1, any_put = -1;
        for (size_t i = 0; i < list.size() && k < 0; ++i) {
            df::adventure_optionst *o = list[i];
            if (!o)
                continue;
            if (d.want == WANT_WEAR && virtual_cast<df::adventure_option_wear_itemst>(o))
                k = int(i);
            else if (d.want == WANT_REMOVE && virtual_cast<df::adventure_option_remove_itemst>(o))
                k = int(i);
            else if (d.want == WANT_DROP && virtual_cast<df::adventure_option_drop_itemst>(o))
                k = int(i);
            else if (d.want == WANT_PUT) {
                auto *put = virtual_cast<df::adventure_environment_place_in_it_containerst>(o);
                df::item *into = put ? put->getContainerItem() : nullptr;
                if (into && any_put < 0)
                    any_put = int(i);
                if (into && into->id == d.container)
                    k = int(i);
            }
        }
        if (k < 0 && d.want == WANT_PUT && d.container < 0)
            k = any_put;
        if (k < 0) {
            d.on = false;
            flash(d.want == WANT_WEAR ? "The game offers no way to wear that"
                  : d.want == WANT_REMOVE ? "The game offers no way to take that out"
                  : d.want == WANT_DROP   ? "The game offers no way to drop that"
                                          : "The game offers no way to put that there");
            inv_then = InvThen();
            leave();
            return;
        }
        const bool reopen = reopens_after(list[k]);
        const int done = click_inv_entry(list, k, true, d.tries++);
        if (done == 1 && reopen)
            reopen_after_action();
        if (done != 0)
            d.on = false;
        return;
    }
    case INV_PICKUP: {
        // out of the inventory, open the game's pick-up list (g), click the thing's "Get ..."
        auto &ol = game->main_interface.adventure.option_list;
        if (d.stage >= 2) {
            // after "Get ...": done, or the game asks how many of the stack
            d.frames = 0;   // waits as long as the question is open
            if (!ol.open) {
                d.on = false;
                return;
            }
            if (!ol.doing_pickup_amount) {
                if (++d.tries > 60)
                    d.on = false;
                return;
            }
            if (d.stage == 2) {
                inv_split = InvSplit();
                inv_split.on = true;
                inv_split.mode = 1;
                inv_split.a = d.item;
                inv_split.total = std::max(1, int(ol.pickup_amount_max));
                inv_split.lo = std::min(1, inv_split.total);
                inv_split.hi = inv_split.total;
                inv_split.value = std::clamp(int(ol.number_amount), inv_split.lo, inv_split.hi);
                inv_split.text = std::to_string(inv_split.value);
                inv_split.title = "Pick up how many?";
                inv_amount_answer = 0;
                d.stage = 3;
                return;
            }
            if (inv_amount_answer == 0) {
                if (inv_split.on)
                    return;
                inv_amount_answer = -1;
            }
            const int answer = inv_amount_answer;
            inv_amount_answer = 0;
            d.on = false;
            if (answer > 0) {
                ol.number_amount = std::clamp(inv_amount_value, 0, int(ol.pickup_amount_max));
                if (!click_inv_row_text("Okay", 0)) {
                    std::set<df::interface_key> keys{df::interface_key::SELECT};
                    INTERPOSE_NEXT(feed)(&keys);
                }
                mouse_log.push_back("inventory: picked up " + std::to_string(ol.number_amount));
            } else {
                inv_then = InvThen();
                leave();
            }
            return;
        }
        if (d.stage == 0) {
            if (inv.open) {
                leave();
                d.wait = 1;
                return;
            }
            if (!ol.open) {
                std::set<df::interface_key> keys{df::interface_key::A_GROUND};
                INTERPOSE_NEXT(feed)(&keys);
            }
            d.stage = 1;
            d.wait = 1;
            return;
        }
        if (!ol.open || ol.context != df::adventure_interface_option_list_context_type::GROUND)
            return;     // not there yet
        int k = -1;
        for (size_t i = 0; i < ol.option.size() && k < 0; ++i) {
            df::adventure_optionst *o = ol.option[i];
            df::item *it = o ? o->getPickupItem() : nullptr;
            if (it && it->id == d.item && virtual_cast<df::adventure_environment_pick_up_ground_itemst>(o))
                k = int(i);
        }
        if (k < 0) {
            d.on = false;
            inv_then = InvThen();
            flash("The game offers no way to pick that up");
            leave();
            return;
        }
        const std::string text = option_text(ol.option[k], true);
        const int first = std::clamp(int(ol.scroll_position), 0, k);
        int skip = 0;
        for (int j = first; j < k; ++j)
            if (option_text(ol.option[j], true) == text)
                ++skip;
        if (click_inv_row_text(text, skip)) {
            mouse_log.push_back("inventory: picked up with '" + text + "'");
            d.stage = 2;
            d.tries = 0;
            d.wait = 1;
            reopen_after_action();
            return;
        }
        if (d.tries++ >= 2) {
            d.on = false;
            inv_then = InvThen();
            leave();
            return;
        }
        ol.scroll_position = std::clamp(k - 2, 0, std::max(0, int(ol.option.size()) - 8));
        return;
    }
    default:
        d.on = false;
        return;
    }
}

// Render: does the panel stay up while the game's inventory is closed (a
// change waiting to be made, a pick-up going on, the inventory to be opened
// again after something done from it)?
// A window of the game other than its inventory is open (aiming, a sheet, a
// conversation, a list ...): what was done led there, and the inventory is not
// to be opened over it. Empty: none.
std::string other_window() {
    const auto &ai = game->main_interface.adventure;
    const bool picking = inv_drive.on && inv_drive.kind == INV_PICKUP;
    if (game->main_interface.view_sheets.open) return "a sheet";
    if (ai.option_list.open && !picking) return "a list";
    if (ai.aim_projectile.open) return "aiming";
    if (ai.attack.open) return "attack";
    if (ai.conversation.open) return "conversation";
    if (ai.look.open) return "look";
    if (ai.jump.open) return "jump";
    if (ai.perform.open) return "perform";
    if (ai.companions.open) return "companions";
    if (ai.sleep.open) return "sleep";
    if (ai.barter.open) return "barter";
    return "";
}

bool inventory_waiting() {
    if (inv_apply.on || (inv_drive.on && inv_drive.kind == INV_PICKUP))
        return true;
    if (!inv_reopen_until || now_ms() > inv_reopen_until)
        return false;
    // something else came up: not waited for
    return other_window().empty();
}

// What the panel asks of the game: done from the game's logic step, never from
// inside its drawing. A key that opens a window of the game (the inventory, its
// pick-up list) makes it build pictures, which needs the main thread; from
// inside the drawing, which the main thread waits for, the game hung for good
// (2026-11-14: reopening the inventory after a drink). Once per drawn frame,
// so that every step meets what the game drew since the one before.
void inventory_logic() {
    static uint32_t seen = 0;
    if (!active || !inv_up || seen == inv_render_frame || !game || !gps)
        return;
    seen = inv_render_frame;
    df::unit *adv = World::getAdventurer();
    if (!adv)
        return;
    auto &inv = game->main_interface.adventure.inventory;
    inventory_drive();
    if (inv_drive.on)
        return;
    if (inv_apply.on) {
        if (++inv_apply.frames > 120) {
            inv_apply.on = false;
            return;
        }
        if (inv.open) {
            // out of the inventory, a step a frame (from a list for one item, two)
            std::set<df::interface_key> keys{df::interface_key::LEAVESCREEN};
            INTERPOSE_NEXT(feed)(&keys);
            return;
        }
        if (inv_apply.strap_item >= 0)
            apply_strap(adv, inv_apply);
        else
            apply_split(inv_apply.what);
        inv_apply.on = false;
        reopen_after_action();
        return;
    }
    if (!inv_reopen_until)
        return;
    if (inv.open) {
        // open again (or never closed): done, once the game has had time to act
        if (inv_render_frame - inv_reopen_from > 30 || inv_reopen_tried)
            inv_reopen_until = 0;
        return;
    }
    if (now_ms() > inv_reopen_until) {
        mouse_log.push_back("inventory: reopen: gave up after 20 s");
        inv_reopen_until = 0;
        return;
    }
    const std::string other = other_window();
    if (!other.empty()) {
        mouse_log.push_back("inventory: reopen: not over " + other);
        inv_reopen_until = 0;
        return;
    }
    if (game_takes_input(adv) && (!inv_reopen_tried || inv_render_frame - inv_reopen_tried >= 15)) {
        std::set<df::interface_key> keys{df::interface_key::A_INV_LOOK};
        INTERPOSE_NEXT(feed)(&keys);
        inv_reopen_tried = std::max<uint32_t>(1, inv_render_frame);
        const std::vector<std::string> focus = Gui::getCurFocus(true);
        mouse_log.push_back("inventory: reopen: key sent (" + (focus.empty() ? std::string() : focus[0]) + ")" +
                            (game->main_interface.adventure.inventory.open ? " open" : " still closed"));
    }
}

// Once a frame, after the game has drawn itself: whether the panel stands in
// for the game's inventory, what it shows, and what the mouse did on it.
void inventory_frame() {
    df::unit *adv = World::getAdventurer();
    const bool usable = active && showing && settings.inventory_panel && game && gps && adv && ui_font_ready &&
                        !game->main_interface.view_sheets.open &&
                        Gui::getCurViewscreen(true) == static_cast<df::viewscreen *>(this);
    const bool want = usable && game->main_interface.adventure.inventory.open && !inv_apply.on;
    static int wheel_seen = 0;
    const int wheel_now = wheel_total.load();
    const int turned = wheel_now - wheel_seen;
    wheel_seen = wheel_now;
    const bool hold = !want && usable && inv_up && inventory_waiting();
    inv_place_kept = hold;
    if (hold && !inv_split.on) {
        // the last picture of the panel stays while it waits for the game
        ++inv_render_frame;
        inv_press.on = false;
        inv_drag = -1;
        return;
    }
    if (!want && !hold && inv_up && inv_reopen_until)
        mouse_log.push_back(std::string("inventory: panel gone while reopening: ") +
                            (!usable ? "panel not usable here" : other_window().empty() ? "time" : other_window()));
    if (!want && !hold) {
        if (inv_up) {
            inv_up = false;
            inv_press.on = false;
            inv_drag = -1;
            inv_drive.on = false;
            inv_split.on = false;
            inv_scroll = 0;
        }
        inv_apply.on = false;
        inv_reopen_until = 0;
        icon_page_from = -1;
        inv_ground_menu = -1;
        return;
    }
    if (hold) {
        // the game's inventory is closed (its lists not to be read): the last
        // picture, with the dialog as it is now
        const std::shared_ptr<const InvModel> old = get_inv();
        if (!old)
            return;
        auto fresh = std::make_shared<InvModel>(*old);
        fresh->popup.clear();
        fresh->popup_item = -1;
        fresh->popup_split = -1;
        fresh->popup_ground = false;
        fill_dialog(*fresh);
        fresh->key = std::hash<std::string>()(fresh->split_title + fresh->split_left + fresh->split_right +
                                              fresh->split_text) ^ (old->key * 31u);
        if (fresh->key != old->key)
            set_inv(fresh);
    } else {
        std::shared_ptr<InvModel> fresh = read_inventory(adv);
        const std::shared_ptr<const InvModel> old = get_inv();
        if (!old || old->key != fresh->key)
            set_inv(fresh);
    }
    inv_up = true;
    const std::shared_ptr<const InvModel> m = get_inv();
    InvLayout L;
    if (!m || !layout_inv(*m, inv_scroll.load(), L))
        return;
    int px = -1, py = -1;
    pointer_pixels(px, py);
    // the wheel scrolls the list of what you carry
    if (turned && !m->split_on && L.in_list(px - L.x, py - L.y)) {
        inv_scroll = std::clamp(L.scroll - turned * (L.S + L.gap), 0, std::max(0, L.content_h - L.list_h));
        layout_inv(*m, inv_scroll.load(), L);
    } else if (turned && !m->split_on && L.in_ground(px - L.x, py - L.y)) {
        inv_hscroll = std::clamp(L.hscroll - turned * (L.S + L.gap), 0, std::max(0, L.content_w - L.ground_w));
        layout_inv(*m, inv_scroll.load(), L);
    } else {
        inv_scroll = L.scroll;
        inv_hscroll = L.hscroll;
    }
    // a scrollbar's track held: the handle follows the pointer
    if (inv_press.on && inv_press.hit.kind == INV_VTRACK && L.vbar.kind != INV_NONE) {
        inv_scroll = scroll_at(L.vbar.h - 2 * L.ch, L.list_h, L.content_h, py - L.y - L.vbar.y - L.ch, L.ch);
        layout_inv(*m, inv_scroll.load(), L);
    }
    if (inv_press.on && inv_press.hit.kind == INV_HTRACK && L.hbar.kind != INV_NONE) {
        inv_hscroll = scroll_at(L.hbar.w - 2 * L.ch, L.ground_w, L.content_w, px - L.x - L.hbar.x - L.ch, L.ch);
        layout_inv(*m, inv_scroll.load(), L);
    }
    // the left button: a click, or a drag once it has moved a little
    if (inv_press.on) {
        const bool down = debug_px.load() >= 0 || !sdl_get_mouse_state
                              ? enabler->mouse_lbut_down != 0
                              : (sdl_get_mouse_state(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        // the slider follows the pointer while held
        if (inv_press.hit.kind == INV_SLIDER && inv_split.on) {
            for (const InvBox &b : L.dlg_parts)
                if (b.kind == INV_SLIDER)
                    set_split_value(slider_value(*m, b, px - L.x), false);
        }
        if (down) {
            if (!inv_press.dragging && inv_press.item >= 0 && !inv_split.on &&
                std::abs(px - inv_press.x0) + std::abs(py - inv_press.y0) > 6) {
                inv_press.dragging = true;
                inv_drag = inv_press.item;
            }
        } else {
            inv_press.on = false;
            if (inv_press.dragging) {
                inv_drag = -1;
                inv_drop(*m, inv_press.item, L.at(px, py));
            } else if ((!inv_drive.on || inv_split.on) && inv_press.hit.kind != INV_SLIDER) {
                // by the item pressed on, wherever the panel has it now
                InvBox hit = inv_press.hit;
                if (inv_press.item >= 0 && !inv_split.on) {
                    const InvBox *b = L.box_of(*m, inv_press.item);
                    hit = b ? *b : InvBox();
                }
                inv_click(*m, hit, false);
            }
        }
    }
    ++inv_render_frame;
    if (hold)
        return;
    inventory_then();
    collect_game_icons();
    inv_window_erased = false;
    // The text of the game's own window under the panel is emptied, so that
    // none of it can show through. Only the characters: the picture layers are
    // left alone, since the game's drawing (on the other thread) can meet a
    // picture half taken away (a game crash in its drawing was seen while
    // they were emptied too, 2026-11-14; not proven to be the cause).
    Cells c;
    if (get_cells(c)) {
        const int x0 = std::max(0, (L.x - c.ox) / c.cw), x1 = std::min(c.dimx - 1, (L.x + L.w - 1 - c.ox) / c.cw);
        const int y0 = std::max(0, (L.y - c.oy) / c.ch), y1 = std::min(c.dimy - 1, (L.y + L.h - 1 - c.oy) / c.ch);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                gps->screen[(x * gps->dimy + y) * 8] = 0;
    }
}
