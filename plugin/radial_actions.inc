// The adventure's bottom row of buttons (read off the game's screen by texture,
// 2026-11-15), in the game's order: the action bar, the row right of the
// portrait, the companions, the corner. Icons are 4 x 3 tiles of
// INTERFACE_BITS_ADVENTURE unless named otherwise. Toggles (yield, weapon,
// stance, sneak) show their first state's picture. That the hand is A_GROUND
// and sneaking A_SNEAK are guesses.
const RadialAction RADIAL_ACTIONS[] = {
    {"interact", df::interface_key::A_GROUND, "Interact, pick up", "INTERFACE_BITS_ADVENTURE", 0, 0, 4, 3},
    {"search", df::interface_key::A_SEARCH, "Search", "INTERFACE_BITS_ADVENTURE", 4, 0, 4, 3},
    {"climb", df::interface_key::A_HOLD, "Climb", "INTERFACE_BITS_ADVENTURE", 12, 0, 4, 3},
    {"jump", df::interface_key::A_JUMP, "Jump", "INTERFACE_BITS_ADVENTURE", 16, 0, 4, 3},
    {"talk", df::interface_key::A_TALK, "Talk", "INTERFACE_BITS_ADVENTURE", 20, 0, 4, 3},
    {"perform", df::interface_key::A_PERFORM, "Perform", "INTERFACE_BITS_ADVENTURE", 24, 0, 4, 3},
    {"craft", df::interface_key::A_ACTION, "Craft, butcher", "INTERFACE_BITS_ADVENTURE", 8, 0, 4, 3},
    {"ability", df::interface_key::A_ABILITY, "Use an ability", "INTERFACE_BITS_ADVENTURE", 40, 0, 4, 3},
    {"strike", df::interface_key::A_ATTACK, "Strike", "INTERFACE_BITS_ADVENTURE", 28, 0, 4, 3},
    {"wrestle", df::interface_key::A_WRESTLE, "Wrestle", "INTERFACE_BITS_ADVENTURE", 32, 0, 4, 3},
    {"shoot", df::interface_key::A_SHOOT, "Shoot", "INTERFACE_BITS_ADVENTURE", 36, 0, 4, 3},
    {"yield", df::interface_key::A_YIELD, "Yield", "INTERFACE_BITS_ADVENTURE", 44, 0, 4, 3},
    {"weapon", df::interface_key::A_INV_DRAW_WEAPON, "Draw or put away a weapon", "INTERFACE_BITS_ADVENTURE", 40, 3, 4, 3},
    // standing / lying down: the picture follows, no ring (not an on and off)
    {"stance", df::interface_key::A_STANCE, "Stand up or lie down", "INTERFACE_BITS_ADVENTURE", 44, 3, 4, 3, RT_PRONE, 48, 3},
    {"status", df::interface_key::A_STATUS, "Status", "INTERFACE_BITS_ADVENTURE", 0, 3, 4, 3},
    {"journal", df::interface_key::A_LOG, "Journal", "INTERFACE_BITS_ADVENTURE", 4, 3, 4, 3},
    {"inventory", df::interface_key::A_INV_LOOK, "Inventory", "INTERFACE_BITS_ADVENTURE", 8, 3, 4, 3},
    {"movement", df::interface_key::A_MOVEMENT, "Movement options", "INTERFACE_BITS_ADVENTURE", 12, 3, 4, 3},
    // SNEAK_OFF, SNEAK_ON while sneaking (graphics_interface.txt)
    {"sneak", df::interface_key::A_SNEAK, "Sneak", "INTERFACE_BITS_ADVENTURE_TRAVEL", 23, 0, 4, 3, RT_SNEAK, 19, 0, true},
    // the game's tracks display (its button at the top right): TRACKS_OFF, TRACKS_ON while shown
    {"tracks", df::interface_key::A_DISPLAY_TRACKS, "Show or hide tracks", "INTERFACE_BITS_ADVENTURE_TRAVEL", 47, 0, 4, 3, RT_TRACKS, 43, 0, true},
    {"combat", df::interface_key::A_COMBAT, "Combat options", "INTERFACE_BITS_ADVENTURE", 16, 3, 4, 3},
    {"companions", df::interface_key::A_COMPANIONS, "Companions", "INTERFACE_BITS_ADVENTURE", 0, 8, 4, 3},
    {"recenter", df::interface_key::A_CENTER, "Centre on yourself", "INTERFACE_BITS_ADVENTURE", 16, 8, 4, 3},
    {"travel", df::interface_key::A_TRAVEL, "Travel", "INTERFACE_BITS_ADVENTURE", 20, 3, 4, 3},
    {"sleep", df::interface_key::A_SLEEP, "Sleep or wait", "INTERFACE_BITS_ADVENTURE", 24, 3, 4, 3},
};

// "..." has no picture of its own in the game: its settings gear (INTERFACE_BITS
// BUTTON_SETTINGS, 2 x 2 at 20,19) stands for it
std::shared_ptr<const fpv::Image> radial_more_picture() {
    std::shared_ptr<const fpv::Image> t[4];
    for (int k = 0; k < 4; ++k) {
        t[k] = page_image("INTERFACE_BITS", 20 + k % 2, 19 + k / 2);
        if (!t[k] || !t[k]->ok())
            return nullptr;
    }
    auto img = std::make_shared<fpv::Image>();
    const int tw = t[0]->w, th = t[0]->h;
    img->w = 2 * tw;
    img->h = 2 * th;
    img->px.assign(size_t(img->w) * img->h, 0);
    for (int k = 0; k < 4; ++k)
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x)
                img->px[size_t((k / 2) * th + y) * img->w + (k % 2) * tw + x] = t[k]->px[size_t(y) * t[k]->w + x];
    return img;
}
