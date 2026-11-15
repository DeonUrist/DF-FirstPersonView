// Included inside dungeon_hook (functions only: a hook struct must not have
// data members). The tabs' switches between the panels, from the logic hook:
// keys that open the game's windows are never fed from a draw.

void panels_logic() {
    const int want = panel_switch.load();
    if (want < 0)
        return;
    df::unit *adv = World::getAdventurer();
    if (!adv || !game || !active) {
        panel_switch = -1;
        return;
    }
    auto &inv = game->main_interface.adventure.inventory;
    if (++panel_switch_tries > 300) {
        mouse_log.push_back("panels: gave up switching to " + std::string(PT_NAMES[want]));
        panel_switch = -1;
        return;
    }
    if (want == PT_INVENTORY) {
        // the game's inventory, opened with its own key once the game takes input
        if (inv.open) {
            panel_switch = -1;
            return;
        }
        if (plain_adventure_focus() && game_takes_input(adv) && panel_switch_tries % 15 == 1) {
            std::set<df::interface_key> keys{df::interface_key::A_INV_LOOK};
            INTERPOSE_NEXT(feed)(&keys);
        }
        return;
    }
    // to the journal or the map: the game's inventory closed first, a key every other frame
    if (inv.open) {
        inv_reopen_until = 0;
        if (panel_switch_tries % 2 == 1) {
            std::set<df::interface_key> keys{df::interface_key::LEAVESCREEN};
            INTERPOSE_NEXT(feed)(&keys);
        }
        return;
    }
    panel_switch = -1;
    jr_open(-1, JRef());
    if (want == PT_MAP) {
        jr.view = 1;
        jr_publish();
    }
}
