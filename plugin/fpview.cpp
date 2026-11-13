// First-person view for adventure mode: native renderer plugin.
//
// Two hooks do the work:
//
//  * viewscreen_dungeonmodest::render (simulation thread). After the game has
//    filled the map viewport, the scene is captured from it. To see more than
//    one z-level the game is asked to draw neighbouring levels as well, the
//    same thing scrolling levels with the mouse wheel does.
//
//  * renderer_2d_base::update_all (render thread). This is the stage that
//    draws the interface. Just before it runs, the scene is drawn into a
//    texture and copied over the map, so the interface lands on top.
//
// The camera is animated on the render thread, so turning and walking are
// smooth regardless of how often the game itself updates.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <SDL_events.h>
#include <SDL_keyboard.h>
#include <SDL_mouse.h>
#include <SDL_render.h>

#include "Core.h"
#include "Debug.h"
#include "LuaTools.h"
#include "MemAccess.h"
#include "PluginManager.h"
#include "TileTypes.h"
#include "VTableInterpose.h"

#include "modules/Buildings.h"
#include "modules/DFSDL.h"
#include "modules/Gui.h"
#include "modules/Items.h"
#include "modules/MapCache.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
#include "modules/Screen.h"
#include "modules/Units.h"
#include "modules/World.h"

#include "df/adventure_interface_attack_mode_type.h"
#include "df/adventure_interface_attackst.h"
#include "df/adventure_interface_option_listst.h"
#include "df/adventure_movement_shoot_tilest.h"
#include "df/adventure_movement_throw_item_at_creaturest.h"
#include "df/adventure_movement_throw_item_at_tilest.h"
#include "df/aim_attack_flag.h"
#include "df/attack_move_choice_type.h"
#include "df/adventure_movement_attack_creaturest.h"
#include "df/adventure_movement_combat_menu_creaturest.h"
#include "df/adventure_movement_shoot_creaturest.h"
#include "df/adventure_optionst.h"
#include "df/adventurest.h"
#include "df/adventure_game_loop_type.h"
#include "df/body_part_raw.h"
#include "df/body_part_raw_flags.h"
#include "df/caste_body_info.h"
#include "df/gamest.h"
#include "df/inv_item_role_type.h"
#include "df/item_shieldst.h"
#include "df/item_toolst.h"
#include "df/item_weaponst.h"
#include "df/itemdef_shieldst.h"
#include "df/itemdef_toolst.h"
#include "df/itemdef_weaponst.h"
#include "df/job_skill.h"
#include "df/unit_inventory_item.h"
#include "df/building.h"
#include "df/building_doorst.h"
#include "df/building_type.h"
#include "df/creature_raw.h"
#include "df/announcement_handlerst.h"
#include "df/announcements.h"
#include "df/d_init.h"
#include "df/report.h"
#include "df/announcement_type.h"
#include "df/init.h"
#include "df/tile_building_occ.h"
#include "df/tile_occupancy.h"
#include "df/tiletype.h"
#include "df/tiletype_material.h"
#include "df/tiletype_shape.h"
#include "df/tiletype_special.h"
#include "df/world.h"
#include "df/adv_announcementst.h"
#include "df/world_data.h"
#include "df/world_site.h"
#include "modules/Translation.h"
#include "df/item.h"
#include "df/map_block.h"
#include "df/plant.h"
#include "df/plant_raw.h"
#include "df/enabler.h"
#include "df/global_objects.h"
#include "df/graphic.h"
#include "df/graphic_viewportst.h"
#include "df/interface_key.h"
#include "df/renderer_2d_base.h"
#include "df/tile_designation.h"
#include "df/tile_liquid.h"
#include "df/tiletype_shape_basic.h"
#include "df/ui_advmode_menu.h"
#include "df/unit.h"
#include "df/unit_action.h"
#include "df/unit_action_type.h"
#include "df/unit_wound.h"
#include "df/unit_wound_layerst.h"
#include "df/viewport_spatter_flag.h"
#include "df/game_type.h"
#include "df/viewscreen_dungeonmodest.h"

#include "media.h"
#include "render.h"
#include "scene.h"

using std::string;
using std::vector;

using namespace DFHack;

DFHACK_PLUGIN("fpview");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(adventure);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(game);
REQUIRE_GLOBAL(init);
REQUIRE_GLOBAL(world);
REQUIRE_GLOBAL(window_x);
REQUIRE_GLOBAL(window_y);
REQUIRE_GLOBAL(window_z);

namespace DFHack {
    DBG_DECLARE(fpview, log, DebugCategory::LINFO);
}

namespace {

using fpv::Camera;
using fpv::EYE;
using fpv::EYE_PRONE;
using fpv::LEVEL_H;

// ---------------------------------------------------------------------------
// SDL lives in the game's process; bind the functions we need at runtime

decltype(&SDL_CreateTexture) sdl_create_texture = nullptr;
decltype(&SDL_DestroyTexture) sdl_destroy_texture = nullptr;
decltype(&SDL_UpdateTexture) sdl_update_texture = nullptr;
decltype(&SDL_RenderCopyEx) sdl_render_copy_ex = nullptr;
decltype(&SDL_SetTextureBlendMode) sdl_set_texture_blend_mode = nullptr;
decltype(&SDL_SetTextureScaleMode) sdl_set_texture_scale_mode = nullptr;
decltype(&SDL_GetRendererOutputSize) sdl_get_renderer_output_size = nullptr;
decltype(&SDL_SetRenderTarget) sdl_set_render_target = nullptr;
decltype(&SDL_RenderClear) sdl_render_clear = nullptr;
decltype(&SDL_RenderCopy) sdl_render_copy = nullptr;
decltype(&SDL_RenderDrawRect) sdl_render_draw_rect = nullptr;
decltype(&SDL_SetRenderDrawColor) sdl_set_render_draw_color = nullptr;
decltype(&SDL_SetRenderDrawBlendMode) sdl_set_render_draw_blend_mode = nullptr;
decltype(&SDL_GetRenderDrawBlendMode) sdl_get_render_draw_blend_mode = nullptr;
decltype(&SDL_GetRenderDrawColor) sdl_get_render_draw_color = nullptr;
decltype(&SDL_RenderFillRect) sdl_render_fill_rect = nullptr;
decltype(&SDL_GetMouseState) sdl_get_mouse_state = nullptr;
decltype(&SDL_WarpMouseInWindow) sdl_warp_mouse_in_window = nullptr;
decltype(&SDL_GetModState) sdl_get_mod_state = nullptr;
decltype(&SDL_CreateRGBSurfaceWithFormat) sdl_create_rgb_surface_with_format = nullptr;
decltype(&SDL_FreeSurface) sdl_free_surface = nullptr;
decltype(&SDL_CreateColorCursor) sdl_create_color_cursor = nullptr;
decltype(&SDL_SetCursor) sdl_set_cursor = nullptr;
decltype(&SDL_GetDefaultCursor) sdl_get_default_cursor = nullptr;
decltype(&SDL_GetKeyboardFocus) sdl_get_keyboard_focus = nullptr;
decltype(&SDL_GetKeyboardState) sdl_get_keyboard_state = nullptr;
decltype(&SDL_AddEventWatch) sdl_add_event_watch = nullptr;   // optional: only the mouse wheel needs them
decltype(&SDL_DelEventWatch) sdl_del_event_watch = nullptr;
bool sdl_bound = false;

bool bind_sdl(color_ostream &out) {
    if (sdl_bound)
        return true;
    DFLibrary *handle = DFSDL::obtain_library_handle();
#define BIND(name, target)                                                              \
    target = reinterpret_cast<decltype(target)>(LookupPlugin(handle, #name));           \
    if (!target) {                                                                      \
        out.printerr("fpview: SDL2 function unavailable: " #name "\n");                 \
        return false;                                                                   \
    }
    BIND(SDL_CreateTexture, sdl_create_texture);
    BIND(SDL_DestroyTexture, sdl_destroy_texture);
    BIND(SDL_UpdateTexture, sdl_update_texture);
    BIND(SDL_RenderCopyEx, sdl_render_copy_ex);
    BIND(SDL_SetTextureBlendMode, sdl_set_texture_blend_mode);
    BIND(SDL_SetTextureScaleMode, sdl_set_texture_scale_mode);
    BIND(SDL_GetRendererOutputSize, sdl_get_renderer_output_size);
    BIND(SDL_SetRenderTarget, sdl_set_render_target);
    BIND(SDL_RenderClear, sdl_render_clear);
    BIND(SDL_RenderCopy, sdl_render_copy);
    BIND(SDL_RenderDrawRect, sdl_render_draw_rect);
    BIND(SDL_SetRenderDrawColor, sdl_set_render_draw_color);
    BIND(SDL_SetRenderDrawBlendMode, sdl_set_render_draw_blend_mode);
    BIND(SDL_GetRenderDrawBlendMode, sdl_get_render_draw_blend_mode);
    BIND(SDL_GetRenderDrawColor, sdl_get_render_draw_color);
    BIND(SDL_RenderFillRect, sdl_render_fill_rect);
    BIND(SDL_GetMouseState, sdl_get_mouse_state);
    BIND(SDL_WarpMouseInWindow, sdl_warp_mouse_in_window);
    BIND(SDL_GetModState, sdl_get_mod_state);
    BIND(SDL_CreateRGBSurfaceWithFormat, sdl_create_rgb_surface_with_format);
    BIND(SDL_FreeSurface, sdl_free_surface);
    BIND(SDL_CreateColorCursor, sdl_create_color_cursor);
    BIND(SDL_SetCursor, sdl_set_cursor);
    BIND(SDL_GetDefaultCursor, sdl_get_default_cursor);
    BIND(SDL_GetKeyboardFocus, sdl_get_keyboard_focus);
    BIND(SDL_GetKeyboardState, sdl_get_keyboard_state);
#undef BIND
    sdl_add_event_watch = reinterpret_cast<decltype(sdl_add_event_watch)>(LookupPlugin(handle, "SDL_AddEventWatch"));
    sdl_del_event_watch = reinterpret_cast<decltype(sdl_del_event_watch)>(LookupPlugin(handle, "SDL_DelEventWatch"));
    sdl_bound = true;
    return true;
}

// ---------------------------------------------------------------------------
// settings (set from the console or by the mod's Lua script)

struct Settings {
    std::atomic<float> fov{100};
    std::atomic<float> resolution{1.0f};      // fraction of the window resolution
    std::atomic<float> view_distance{48};
    std::atomic<int> range{2};                // map windows read in each direction (1 = just the middle one)
    std::atomic<int> levels_up{24};
    std::atomic<bool> minimap{true};
    std::atomic<bool> minimap_ascii{false};
    std::atomic<int> minimap_tiles{11};       // tiles across the minimap (its zoom)
    std::atomic<bool> wasd{true};             // w a s d walk and strafe
    std::atomic<bool> mouse_look{true};       // the mouse turns the camera
    std::atomic<bool> combat_menus{true};     // paperdoll and compact lists instead of the game's attack lists
    std::atomic<bool> game_tips{true};        // the game's own hover box, for the tile the pointer is on
    std::atomic<bool> body_doll{true};        // the adventurer's paperdoll in place of the portrait
    std::atomic<int> levels_down{8};
    std::atomic<bool> relative_controls{true};
    std::atomic<bool> smooth{true};
    std::atomic<bool> compass{true};
    std::atomic<int> realtime_rate{5};        // game ticks per second while time runs by itself
} settings;

// ---------------------------------------------------------------------------
// shared state

std::atomic<bool> active{false};        // the user wants the view on
std::atomic<bool> showing{false};       // ... and the game is in a state where it can be shown
std::atomic<int64_t> last_capture_ms{0}; // when the adventure screen last drew itself
std::atomic<int> heading{0};            // 0..7, N NE E SE S SW W NW
std::atomic<bool> realtime{false};      // the timer button is on: time passes by itself
bool space_stopped = false;             // Space turned the timer off; that press does nothing else

fpv::Scene scene;
std::unique_ptr<fpv::Renderer> renderer;

// where the camera wants to be; written by the simulation thread
std::mutex target_mutex;
struct Target {
    float x = 0, y = 0, eye = 0;
    int zlo = 0, zhi = -1;
    float daylight = 1;
    uint32_t fog = 0xFF0C0908;
    bool valid = false;
    uint32_t snap = 0;          // goes up whenever the camera must jump
} target;

// capture bookkeeping (simulation thread only)
bool full_capture = true;
int rr_index = 0;
int last_cam_z = -100000;
int last_ax = -1, last_ay = -1, last_az = -1;
uint32_t capture_frame = 0;
int warmup = 0;                // frames left in which extra map windows are read quickly
int pre_x = -100000, pre_y = 0, pre_z = 0, pre_next = 0;   // where and how far the map itself has been read

// testing aid: look from somewhere other than the adventurer
std::atomic<bool> free_camera{false};
std::atomic<int> free_x{0}, free_y{0}, free_z{0};
bool zoom_saved = false;       // the hidden map is zoomed out; saved_zoom is the player's own zoom
int saved_zoom = 0;

// What the mod's interface buttons (Lua) need to know. Kept in a Lua global
// rather than asked for through a call into the plugin, which could wait
// forever on a plugin that is just being loaded or unloaded.
int published_state = -1;

void publish_state(bool view, bool minimap) {
    const bool timer = view && realtime;
    const int state = (view ? 1 : 0) | (minimap ? 2 : 0) | (timer ? 4 : 0);
    if (state == published_state)
        return;
    published_state = state;
    lua_State *L = Core::getInstance().getLuaState(true);
    if (!L)
        return;
    lua_pushboolean(L, view);
    lua_setglobal(L, "fpview_view_shown");
    lua_pushboolean(L, minimap);
    lua_setglobal(L, "fpview_minimap_shown");
    lua_pushboolean(L, timer);
    lua_setglobal(L, "fpview_realtime_on");
}

// The mouse wheel, counted from SDL's own events: with Alt held (which frees
// the pointer from mouse look) the game makes no key of the wheel, so the
// mod's buttons would never hear of it. The count (up is positive) goes to
// Lua as fpview_wheel.
std::atomic<int> wheel_total{0};
bool wheel_watching = false;

int SDLCALL wheel_watch(void *, SDL_Event *event) {
    if (event && event->type == SDL_MOUSEWHEEL) {
        const int y = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        wheel_total += y;
    }
    return 1;
}

void watch_wheel(bool on) {
    if (on && !wheel_watching && sdl_add_event_watch) {
        sdl_add_event_watch(wheel_watch, nullptr);
        wheel_watching = true;
    } else if (!on && wheel_watching && sdl_del_event_watch) {
        sdl_del_event_watch(wheel_watch, nullptr);
        wheel_watching = false;
    }
}

void publish_wheel() {
    static int published = 0;
    static bool any = false;
    if (!wheel_watching)
        return;
    const int total = wheel_total.load();
    if (any && total == published)
        return;
    lua_State *L = Core::getInstance().getLuaState(true);
    if (!L)
        return;
    lua_pushinteger(L, total);
    lua_setglobal(L, "fpview_wheel");
    published = total;
    any = true;
}

// mouse look: the render thread turns the camera and keeps the pointer in the
// middle of the window; the simulation thread says when that is allowed
std::atomic<bool> look_ok{false};         // plain adventure screen, nothing open that needs the pointer
std::atomic<bool> look_on{false};         // the pointer is being held in the middle right now
std::atomic<bool> ctrl_held{false};       // Ctrl is down: a click attacks
std::atomic<bool> shift_held{false};      // Shift is down
std::atomic<bool> space_down{false};      // the space bar is down
std::atomic<bool> debug_ctrl{false};      // testing aid: as if Ctrl were held
std::atomic<bool> debug_look{false};      // testing aid: draw the crosshair even without the mouse held
std::atomic<bool> attack_busy{false};     // an attack started with Ctrl-click is being carried through

// what the adventurer holds, drawn at the bottom of the view
struct HeldItem {
    bool on = false;
    int32_t texpos = 0;
    uint32_t px[fpv::TEXPIX];
};
std::mutex held_mutex;
HeldItem held[2];                         // 0 = left hand, 1 = right hand

// testing aids
std::atomic<bool> debug_noblank{false};   // leave the hidden top-down map in the viewport
std::atomic<bool> debug_center{true};     // aim clicks at the middle of the map tile
std::vector<std::string> mouse_log;       // what happened to recent mouse clicks

// statistics
std::atomic<uint32_t> stat_render_us{0}, stat_upload_us{0}, stat_capture_us{0};
std::atomic<uint32_t> stat_reads_skipped{0};   // map reads not used: the game drew some other place
std::atomic<uint64_t> stat_frames{0};
std::atomic<int> stat_w{0}, stat_h{0};

const char *DIR_NAMES[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
const int DIR_DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int DIR_DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

bool default_adventure_ui() {
    if (!adventure)
        return false;
    // also while aiming a throw or a shot: the target is clicked in the view
    return adventure->menu == df::ui_advmode_menu::Default || adventure->menu == df::ui_advmode_menu::ThrowAim ||
           adventure->menu == df::ui_advmode_menu::Fire;
}

// The plain adventure screen with nothing open on it. An adventure and the
// arena (after taking control of a creature) name that screen differently.
bool plain_adventure_focus() {
    const std::vector<std::string> focus = Gui::getCurFocus(true);
    if (focus.empty())
        return false;
    for (const std::string &f : focus)
        if (f != "dungeonmode/Default" && f != "dungeonmode/ADVENTURE_ARENA")
            return false;
    return true;
}

float daylight_now() {
    // 1200 ticks per day; fade around dawn (5-7h) and dusk (18-20h)
    const float hour = float(*cur_year_tick % 1200) / 50.0f;
    const auto ramp = [](float a, float b, float x) { return std::clamp((x - a) / (b - a), 0.0f, 1.0f); };
    return std::min(ramp(5, 7, hour), 1 - ramp(18, 20, hour));
}

// ---------------------------------------------------------------------------
// pointer: which tile or creature of the 3D view is the mouse on?

std::atomic<int> view_out_w{0}, view_out_h{0};   // window size the last frame was drawn for
std::atomic<int> debug_px{-1}, debug_py{-1};     // stand-in pointer for testing, in window pixels

// pointer position in window pixels; false if the mouse is outside the window
bool pointer_pixels(int &px, int &py) {
    const int dx = debug_px.load();
    if (dx >= 0) {
        px = dx;
        py = debug_py.load();
        return true;
    }
    if (!gps || gps->precise_mouse_x < 0 || gps->precise_mouse_y < 0)
        return false;
    px = gps->precise_mouse_x;
    py = gps->precise_mouse_y;
    return true;
}

// interface grid cell under a window pixel; false if there is none
bool interface_cell(int px, int py, int &cx, int &cy) {
    if (!gps || !enabler || !enabler->renderer)
        return false;
    const auto r = static_cast<df::renderer_2d_base *>(enabler->renderer);
    if (r->dispx_z <= 0 || r->dispy_z <= 0)
        return false;
    cx = (px - r->origin_x) / r->dispx_z;
    cy = (py - r->origin_y) / r->dispy_z;
    return px >= r->origin_x && py >= r->origin_y && cx >= 0 && cy >= 0 && cx < gps->dimx && cy < gps->dimy;
}

// true if the interface has drawn something at this window pixel, in which
// case the pointer belongs to the interface and not to the 3D view
int hint_x0 = 0, hint_y0 = 0, hint_x1 = -1, hint_y1 = -1;   // hover box painted last frame
std::atomic<int> tip_x0{0}, tip_y0{0}, tip_x1{-1}, tip_y1{-1};   // the game's hover box drawn last frame, in cells
std::atomic<int> menu_px0{0}, menu_py0{0}, menu_px1{-1}, menu_py1{-1};   // our attack menu, in window pixels
std::atomic<bool> portrait_ours{false};  // the adventurer's paperdoll is drawn where the portrait was
std::atomic<int> compass_px0{0}, compass_py0{0}, compass_px1{-1}, compass_py1{-1};   // compass and its labels, window pixels

bool in_menu_panel(int px, int py) {
    return menu_px1.load() >= menu_px0.load() && px >= menu_px0.load() && px <= menu_px1.load() &&
           py >= menu_py0.load() && py <= menu_py1.load();
}

// The game's buttons in the bottom right corner (centre on yourself, build,
// travel, sleep) are hidden while the view shows: the mod's message list takes
// that corner, and its own travel and sleep buttons sit beside the speed
// display. Where they are is read each frame from what the game drew there.
std::atomic<int> corner_x0{-1}, corner_y0{-1};   // top left interface cell of the hidden corner
std::atomic<bool> corner_live{false};           // hidden this frame (the plain adventure screen)

bool in_corner(int px, int py) {
    int cx, cy;
    if (!corner_live || !interface_cell(px, py, cx, cy))
        return false;
    const int x0 = corner_x0.load(), y0 = corner_y0.load();
    return x0 >= 0 && y0 >= 0 && cx >= x0 && cy >= y0;
}

// While the view shows, the game's buttons at the bottom are rearranged
// (move_bottom_buttons). The row of buttons right of the portrait and the
// speed display ("Walk") go up into the row of "View your companions", right
// after the mod's camera and timer (from column BUTTONS_LEFT; the mod's travel
// and sleep buttons follow them). The action bar (hand, look, talk ...),
// normally centred, takes their place at the bottom right of the portrait
// (from column BAR_LEFT). What the game drew is moved; the game still takes
// the buttons to be where it drew them, so the pointer over a moved block is
// handed to the game at the same spot of the original (bar_source).
constexpr int BAR_LEFT = 12;       // right of the portrait (BODY_COLS in menu.inc)
constexpr int BUTTONS_LEFT = 27;   // right of the timer (TimeButton in first-person.lua: x=21, 7 cells wide)
constexpr int BUTTON_ROWS = 3;     // a row of the game's 4 x 3 buttons

struct MovedBlock {
    std::atomic<bool> live{false};
    std::atomic<int> x0{0}, x1{-1}, y0{0}, y1{-1};   // where it is shown, in interface cells
    std::atomic<int> dx{0}, dy{0};                   // where the game drew it, from there
};
MovedBlock moved_bar, moved_buttons;

// where the moved bar ends, for the message panel to start after it (nil: not moved)
int published_bar = -2;
void publish_bar() {
    const int right = moved_bar.live.load() ? moved_bar.x1.load() : -1;
    if (right == published_bar)
        return;
    lua_State *L = Core::getInstance().getLuaState(true);
    if (!L)
        return;
    published_bar = right;
    if (right >= 0)
        lua_pushinteger(L, right);
    else
        lua_pushnil(L);
    lua_setglobal(L, "fpview_bar_right");
}

// if this window pixel is on a moved block: the same spot on what the game drew
bool bar_source(int px, int py, int &opx, int &opy) {
    int cx, cy;
    if (!interface_cell(px, py, cx, cy))
        return false;
    const auto r = static_cast<df::renderer_2d_base *>(enabler->renderer);
    for (const MovedBlock *b : {&moved_bar, &moved_buttons}) {
        if (!b->live.load() || cx < b->x0.load() || cx > b->x1.load() || cy < b->y0.load() || cy > b->y1.load())
            continue;
        opx = px + b->dx.load() * r->dispx_z;
        opy = py + b->dy.load() * r->dispy_z;
        return true;
    }
    return false;
}

bool in_messages(int px, int py);   // the message panel (menu.inc)

bool over_interface(int px, int py) {
    int cx, cy;
    if (!interface_cell(px, py, cx, cy))
        return true;
    if (in_menu_panel(px, py) || in_messages(px, py))
        return true;
    // the portrait's place still belongs to the interface while our doll is drawn there
    if (portrait_ours && cx < 12 && cy >= gps->dimy - 8)
        return true;
    // what we painted ourselves (hover box, compass) is part of the view
    if (cx >= hint_x0 && cx <= hint_x1 && cy >= hint_y0 && cy <= hint_y1)
        return false;
    // So is the game's hover box for the looked-at tile. A long one reaches
    // down to the pointer; taken for interface, the pointer would be handed
    // back to the game, the box would go, and come back the next frame.
    if (cx >= tip_x0.load() && cx <= tip_x1.load() && cy >= tip_y0.load() && cy <= tip_y1.load())
        return false;
    if (cy == 0 && settings.compass && std::abs(cx - gps->dimx / 2) <= 12)
        return false;
    if (settings.compass && px >= compass_px0.load() && px <= compass_px1.load() && py >= compass_py0.load() &&
        py <= compass_py1.load())
        return false;
    const int idx = cx * gps->dimy + cy;
    if (gps->screentexpos[idx] || gps->screentexpos_lower[idx] || gps->screentexpos_anchored[idx] ||
        gps->screentexpos_flag[idx])
        return true;
    if (gps->screen[idx * 8] != 0)
        return true;
    if (gps->top_in_use && gps->screen_top &&
        (gps->screen_top[idx * 8] != 0 || gps->screentexpos_top[idx] || gps->screentexpos_top_lower[idx] ||
         gps->screentexpos_top_anchored[idx]))
        return true;
    return false;
}

// has the interface drawn anything in this cell?
bool grid_cell_drawn(size_t idx) {
    if (gps->screen[idx * 8] || gps->screentexpos[idx] || gps->screentexpos_lower[idx] ||
        gps->screentexpos_anchored[idx] || gps->screentexpos_flag[idx])
        return true;
    return gps->top_in_use && gps->screen_top &&
           (gps->screen_top[idx * 8] || gps->screentexpos_top[idx] || gps->screentexpos_top_lower[idx] ||
            gps->screentexpos_top_anchored[idx]);
}

// empty the interface grid; the game's next draw puts its interface back
void wipe_grid() {
    const size_t cells = size_t(gps->dimx) * gps->dimy;
    std::memset(gps->screen, 0, cells * 8);
    std::memset(gps->screentexpos, 0, cells * sizeof(*gps->screentexpos));
    std::memset(gps->screentexpos_lower, 0, cells * sizeof(*gps->screentexpos_lower));
    std::memset(gps->screentexpos_anchored, 0, cells * sizeof(*gps->screentexpos_anchored));
    std::memset(gps->screentexpos_flag, 0, cells * sizeof(*gps->screentexpos_flag));
}

// The game's hover box is told from the rest of the interface by drawing the
// frame once without it (tip_baseline) and then with it (tip_measure).
std::vector<uint8_t> tip_base;   // simulation thread: the cells drawn without a hover box

void tip_none() {
    tip_x1 = -1;
}

void tip_baseline() {
    const size_t cells = size_t(std::max(0, gps->dimx)) * size_t(std::max(0, gps->dimy));
    tip_base.resize(cells);
    for (size_t i = 0; i < cells; ++i)
        tip_base[i] = grid_cell_drawn(i);
}

// The box is the group of new cells nearest the pointer cell (mcx, mcy),
// cells up to two apart counted as one group (blank cells inside the box).
// Anything else the second draw added elsewhere is not part of it.
void tip_measure(int mcx, int mcy) {
    const int dimx = gps->dimx, dimy = gps->dimy;
    if (tip_base.size() != size_t(std::max(0, dimx)) * size_t(std::max(0, dimy))) {
        tip_none();
        return;
    }
    std::vector<uint8_t> fresh(tip_base.size(), 0);
    int seed = -1, seed_d = 1 << 30;
    for (int x = 0; x < dimx; ++x)
        for (int y = 0; y < dimy; ++y) {
            const size_t idx = size_t(x) * dimy + y;
            if (tip_base[idx] || !grid_cell_drawn(idx))
                continue;
            fresh[idx] = 1;
            const int d = std::max(std::abs(x - mcx), std::abs(y - mcy));
            if (d < seed_d) {
                seed_d = d;
                seed = int(idx);
            }
        }
    if (seed < 0) {
        tip_none();
        return;
    }
    int x0 = dimx, y0 = dimy, x1 = -1, y1 = -1;
    std::vector<int> todo{seed};
    fresh[size_t(seed)] = 2;
    while (!todo.empty()) {
        const int idx = todo.back();
        todo.pop_back();
        const int x = idx / dimy, y = idx % dimy;
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
        for (int nx = std::max(0, x - 2); nx <= std::min(dimx - 1, x + 2); ++nx)
            for (int ny = std::max(0, y - 2); ny <= std::min(dimy - 1, y + 2); ++ny) {
                const size_t n = size_t(nx) * dimy + ny;
                if (fresh[n] == 1) {
                    fresh[n] = 2;
                    todo.push_back(int(n));
                }
            }
    }
    tip_x1 = -1;
    tip_x0 = x0;
    tip_y0 = y0;
    tip_y1 = y1;
    tip_x1 = x1;
}

// what the last drawn frame shows at a window pixel
fpv::Pick pick_pixels(int px, int py) {
    fpv::Pick p;
    const int ow = view_out_w.load(), oh = view_out_h.load();
    if (ow <= 0 || oh <= 0 || px < 0 || py < 0 || px >= ow || py >= oh)
        return p;
    std::lock_guard<std::mutex> lock(scene.mutex);
    if (!renderer)
        return p;
    return renderer->pick(int(int64_t(px) * stat_w.load() / ow), int(int64_t(py) * stat_h.load() / oh));
}

// ---------------------------------------------------------------------------
// minimap: a small copy of the normal top-down map in the top right corner

// tiles per side: an odd number, so that the adventurer is in the middle
int mini_tiles() {
    return std::clamp(settings.minimap_tiles.load(), 5, 61) | 1;
}
constexpr int MINI_COLS = 28;           // width in interface columns; as tall as it is wide
std::atomic<int> mini_n{0};             // tiles per side parked for the renderer, 0 = none
std::atomic<int> mini_self{0};          // the adventurer's tile in the taken minimap: column << 8 | row

// where the minimap goes, in window pixels; false if it is not shown. It is
// placed on the interface grid, like the game's own panels, so that it keeps
// its place next to them at any window size.
bool minimap_rect(SDL_Rect &rect) {
    if (!settings.minimap || !gps || !enabler || !enabler->renderer)
        return false;
    const auto r = static_cast<df::renderer_2d_base *>(enabler->renderer);
    if (r->dispx_z <= 0 || r->dispy_z <= 0 || gps->dimx < MINI_COLS + 8)
        return false;
    const int side = MINI_COLS * r->dispx_z;
    // against the right edge, below the date line, which is two interface rows tall
    rect = {r->origin_x + (gps->dimx - MINI_COLS) * r->dispx_z - 3, r->origin_y + 2 * r->dispy_z + 4, side, side};
    return true;
}

bool in_minimap(int px, int py) {
    SDL_Rect r;
    if (!minimap_rect(r))
        return false;
    return px >= r.x - 4 && py >= r.y - 4 && px < r.x + r.w + 6 && py < r.y + r.h + 4;
}

std::string replace_word(std::string text, const std::string &word, const std::string &with) {
    const size_t at = text.find(word);
    if (at != std::string::npos)
        text.replace(at, word.size(), with);
    return text;
}

// what the ground, wall or tree at a tile is, e.g. "kaolinite wall"
std::string describe_terrain(const df::coord &pos) {
    const df::tiletype *ttp = Maps::getTileType(pos);
    if (!ttp)
        return "";
    const df::tiletype tt = *ttp;
    if (const df::tile_designation *des = Maps::getTileDesignation(pos))
        if (des->bits.flow_size >= 2 && tileShapeBasic(tileShape(tt)) != df::tiletype_shape_basic::Wall)
            return des->bits.liquid_type == df::tile_liquid::Magma ? "magma" : "water";
    const char *raw_caption = ENUM_ATTR(tiletype, caption, tt);
    std::string caption = raw_caption ? raw_caption : "";
    // constructed walls carry letters for their shape (e.g. "wall LUD")
    const size_t last_space = caption.rfind(' ');
    if (last_space != std::string::npos && last_space + 1 < caption.size() &&
        caption.find_first_not_of("LRUD", last_space + 1) == std::string::npos)
        caption.erase(last_space);
    if (caption.empty())
        return "";
    const df::tiletype_material tm = tileMaterial(tt);
    if (tm == df::tiletype_material::TREE || tm == df::tiletype_material::MUSHROOM) {
        if (df::plant *plant = Maps::getPlantAtTile(pos.x, pos.y, pos.z))
            if (df::plant_raw *raw = df::plant_raw::find(plant->material))
                return caption.find("tree") != std::string::npos ? replace_word(caption, "tree", raw->name)
                                                                 : raw->name + " " + caption;
        return caption;
    }
    if (tm == df::tiletype_material::STONE || tm == df::tiletype_material::MINERAL ||
        tm == df::tiletype_material::LAVA_STONE || tm == df::tiletype_material::FEATURE ||
        tm == df::tiletype_material::SOIL || tm == df::tiletype_material::CONSTRUCTION) {
        MapExtras::MapCache cache;
        const MaterialInfo mi(cache.staticMaterialAt(pos));
        const std::string name = mi.isValid() ? mi.toString() : "";
        if (!name.empty()) {
            if (caption.find("stone") != std::string::npos)
                return replace_word(caption, "stone", name);
            if (caption.find("soil") != std::string::npos)
                return replace_word(caption, "soil", name);
            if (caption.find("constructed ") != std::string::npos)
                return name + " " + replace_word(caption, "constructed ", "");
            return name + " " + caption;
        }
    }
    return caption;
}

// name of exactly the thing a pick points at
std::string describe(const fpv::Pick &p) {
    if (!p.valid())
        return "";
    if (p.unit_id >= 0) {
        df::unit *unit = df::unit::find(p.unit_id);
        return unit ? Units::getReadableName(unit) : "";
    }
    const df::coord pos(p.x, p.y, p.z);
    switch (p.kind < 0 ? fpv::BillKind::Other : fpv::BillKind(p.kind)) {
    case fpv::BillKind::Building:
        if (df::building *bld = Buildings::findAtTile(pos))
            return Buildings::getName(bld);
        break;
    case fpv::BillKind::Item:
        if (df::map_block *block = Maps::getTileBlock(pos))
            for (int32_t id : block->items) {
                df::item *item = df::item::find(id);
                if (item && item->pos == pos && item->flags.bits.on_ground)
                    return Items::getReadableDescription(item);
            }
        break;
    case fpv::BillKind::Plant:
        if (df::plant *plant = Maps::getPlantAtTile(pos.x, pos.y, pos.z))
            if (df::plant_raw *raw = df::plant_raw::find(plant->material))
                return raw->name;
        break;
    default:
        break;
    }
    return describe_terrain(pos);
}

// The game works out which map tile the mouse is on from the mouse position
// and the origin of the map window, as if the top-down map were still on
// screen. While the pointer is over the 3D view this moves the window so that
// the answer is the tile the pointer is on in 3D, for the duration of one call
// into the game. If the pointer is on nothing (sky), the mouse is hidden from
// the game instead. Interface elements are unaffected: the mouse stays where
// it really is.
struct PointerRedirect {
    enum State { NONE, SHIFTED, HIDDEN, ON_BAR } state = NONE;
    int32_t save_wx = 0, save_wy = 0, save_wz = 0, set_wx = 0, set_wy = 0, set_wz = 0;
    int32_t save_mx = 0, save_my = 0, save_px = 0, save_py = 0;
    int32_t set_mx = 0, set_my = 0, set_px = 0, set_py = 0;
    bool mouse_changed = false;
    fpv::Pick picked;

    void set_mouse(int32_t mx, int32_t my, int32_t px, int32_t py) {
        save_mx = gps->mouse_x;
        save_my = gps->mouse_y;
        save_px = gps->precise_mouse_x;
        save_py = gps->precise_mouse_y;
        gps->mouse_x = set_mx = mx;
        gps->mouse_y = set_my = my;
        gps->precise_mouse_x = set_px = px;
        gps->precise_mouse_y = set_py = py;
        mouse_changed = true;
    }

    // hide_from_game: keep the game from reacting to the pointer at all (used
    // when we show our own hover text for what the pointer is on)
    explicit PointerRedirect(bool hide_from_game = false) {
        if (!active || !showing || !gps)
            return;
        int px, py;
        if (!pointer_pixels(px, py))
            return;
        if (in_minimap(px, py) || in_corner(px, py)) {
            // the buttons that normally sit there are hidden; do not press them
            set_mouse(-1, -1, -1, -1);
            state = HIDDEN;
            return;
        }
        int bar_px, bar_py;
        if (bar_source(px, py, bar_px, bar_py)) {
            // the moved buttons: the game gets the pointer on its own
            int cx = 0, cy = 0;
            interface_cell(bar_px, bar_py, cx, cy);
            set_mouse(cx, cy, bar_px, bar_py);
            state = ON_BAR;
            return;
        }
        if (over_interface(px, py))
            return;
        const fpv::Pick p = pick_pixels(px, py);
        picked = p;
        const int tile_px = gps->viewport_zoom_factor / 4;
        if (p.valid() && hide_from_game) {
            set_mouse(-1, -1, -1, -1);
            state = HIDDEN;
        } else if (p.valid() && tile_px > 0) {
            save_wx = *window_x;
            save_wy = *window_y;
            save_wz = *window_z;
            *window_x = set_wx = p.x - px / tile_px;
            *window_y = set_wy = p.y - py / tile_px;
            *window_z = set_wz = p.z;
            state = SHIFTED;
            // The game finds the map tile from the pointer in its own way
            // (interface cells, window offsets). Putting the pointer in the
            // middle of the tile leaves no room for the two to disagree at
            // tile edges.
            int ax = px, ay = py;
            if (debug_center) {
                ax = px / tile_px * tile_px + tile_px / 2;
                ay = py / tile_px * tile_px + tile_px / 2;
            }
            if (debug_center || debug_px.load() >= 0) {
                int cx = 0, cy = 0;
                interface_cell(ax, ay, cx, cy);
                set_mouse(cx, cy, ax, ay);
            }
        } else {
            set_mouse(-1, -1, -1, -1);
            state = HIDDEN;
        }
    }

    ~PointerRedirect() {
        // Always put the window back. With the window moved the game may try
        // to scroll it back towards the adventurer; keeping that would leave
        // the real map view displaced.
        if (state == SHIFTED) {
            *window_x = save_wx;
            *window_y = save_wy;
            *window_z = save_wz;
        }
        // the mouse is put back unless the game's main thread updated it meanwhile
        if (mouse_changed) {
            if (gps->mouse_x == set_mx) gps->mouse_x = save_mx;
            if (gps->mouse_y == set_my) gps->mouse_y = save_my;
            if (gps->precise_mouse_x == set_px) gps->precise_mouse_x = save_px;
            if (gps->precise_mouse_y == set_py) gps->precise_mouse_y = save_py;
        }
    }
};

// Pointer state for the frame being captured. While the pointer is over the
// 3D view the mouse is hidden from the game for the whole frame: left alone,
// the game would show hover text for the hidden top-down map.
struct FramePointer {
    bool over_view = false, hidden = false;
    bool on_bar = false;            // the pointer is on the moved buttons (see bar_source)
    int px = 0, py = 0;
    int32_t save_mx = 0, save_my = 0, save_px = 0, save_py = 0;
    int32_t bar_mx = 0, bar_my = 0, bar_ppx = 0, bar_ppy = 0;
    fpv::Pick pick;

    // the main thread may have put the real pointer back since begin()
    void bar_again() {
        if (!on_bar)
            return;
        gps->mouse_x = bar_mx;
        gps->mouse_y = bar_my;
        gps->precise_mouse_x = bar_ppx;
        gps->precise_mouse_y = bar_ppy;
    }

    void hide() {
        gps->mouse_x = gps->mouse_y = -1;
        gps->precise_mouse_x = gps->precise_mouse_y = -1;
    }

    // let the game see the pointer where it really is (for one draw)
    void show() {
        int cx = 0, cy = 0;
        interface_cell(px, py, cx, cy);
        gps->mouse_x = cx;
        gps->mouse_y = cy;
        gps->precise_mouse_x = px;
        gps->precise_mouse_y = py;
    }

    void begin() {
        over_view = hidden = on_bar = false;
        pick = fpv::Pick();
        if (!showing || !gps || !pointer_pixels(px, py))
            return;
        // over the moved action bar: the game draws its hover look (and
        // caption) for the same spot of the bar where it drew it
        int bar_px, bar_py;
        if (bar_source(px, py, bar_px, bar_py)) {
            save_mx = gps->mouse_x;
            save_my = gps->mouse_y;
            save_px = gps->precise_mouse_x;
            save_py = gps->precise_mouse_y;
            int cx = 0, cy = 0;
            interface_cell(bar_px, bar_py, cx, cy);
            bar_mx = cx;
            bar_my = cy;
            bar_ppx = bar_px;
            bar_ppy = bar_py;
            on_bar = true;
            bar_again();
            return;
        }
        // over the minimap or the hidden corner buttons: the game gets no pointer, so no hover text for them
        const bool mini = in_minimap(px, py) || in_corner(px, py);
        if (!mini && over_interface(px, py))
            return;
        if (!mini) {
            pick = pick_pixels(px, py);
            over_view = true;
        }
        save_mx = gps->mouse_x;
        save_my = gps->mouse_y;
        save_px = gps->precise_mouse_x;
        save_py = gps->precise_mouse_y;
        hide();
        hidden = true;
    }

    void end() {
        if (on_bar) {
            on_bar = false;
            if (gps->mouse_x == bar_mx && gps->precise_mouse_x == bar_ppx) {
                gps->mouse_x = save_mx;
                gps->mouse_y = save_my;
                gps->precise_mouse_x = save_px;
                gps->precise_mouse_y = save_py;
            }
            return;
        }
        if (!hidden)
            return;
        hidden = false;
        // put the mouse back unless the game's main thread has updated it meanwhile
        if (gps->precise_mouse_x == -1 && gps->mouse_x == -1) {
            gps->mouse_x = save_mx;
            gps->mouse_y = save_my;
            gps->precise_mouse_x = save_px;
            gps->precise_mouse_y = save_py;
        }
    }
} frame_pointer;

// hover text: the name of what the pointer is on (kept for "fpview hint")
std::vector<std::string> hover_lines;

// The game does not repaint the part of the interface grid that lies over
// the map, so what we paint there stays until we take it off again.
void clear_hint() {
    if (!gps || hint_x1 < hint_x0)
        return;
    for (int x = std::max(0, hint_x0); x <= hint_x1 && x < gps->dimx; ++x)
        for (int y = std::max(0, hint_y0); y <= hint_y1 && y < gps->dimy; ++y) {
            const int idx = x * gps->dimy + y;
            std::memset(&gps->screen[idx * 8], 0, 8);
            gps->screentexpos[idx] = 0;
            gps->screentexpos_lower[idx] = 0;
            gps->screentexpos_anchored[idx] = 0;
            gps->screentexpos_flag[idx] = 0;
        }
    hint_x1 = -1;
}

// Our own tip: the name of exactly what the pointer is on, in one line a
// little below the pointer. (The game's own box, with everything on the tile,
// is in the top left corner: move_tip_to_corner.)
void paint_hint(const std::vector<std::string> &text, int px, int py) {
    if (text.empty() || text[0].empty() || gps->dimx < 20 || gps->dimy < 12)
        return;
    const std::string line = " " + text[0].substr(0, size_t(std::min(70, gps->dimx - 4))) + " ";
    int pcx = gps->dimx / 2, pcy = gps->dimy / 2;
    interface_cell(px, py, pcx, pcy);
    const int x0 = std::clamp(pcx - int(line.size()) / 2, 0, gps->dimx - int(line.size()));
    const int y = std::min(pcy + 3, gps->dimy - 8);
    hint_x0 = x0;
    hint_y0 = y;
    hint_x1 = x0 + int(line.size()) - 1;
    hint_y1 = y;
    Screen::paintString(Screen::Pen(' ', COLOR_YELLOW, COLOR_BLACK), x0, y, line);
}

std::string mouse_flags() {
    if (!enabler)
        return "";
    std::string text;
    const auto add = [&](const char *name, int value) {
        if (value)
            text += std::string(name) + " ";
    };
    add("L", enabler->mouse_lbut);
    add("R", enabler->mouse_rbut);
    add("Ldown", enabler->mouse_lbut_down);
    add("Rdown", enabler->mouse_rbut_down);
    add("Llift", enabler->mouse_lbut_lift);
    add("Rlift", enabler->mouse_rbut_lift);
    return text.empty() ? "-" : text;
}

// testing aid: notes what a call into the game did with a pending click
struct MouseTrace {
    const char *where;
    std::string before, note;
    bool on;
    explicit MouseTrace(const char *where_) : where(where_) {
        on = enabler && (enabler->mouse_lbut || enabler->mouse_rbut);
        if (on)
            before = mouse_flags();
    }
    void after() {
        if (!on)
            return;
        const std::vector<std::string> focus = Gui::getFocusStrings(Gui::getDFViewscreen(true));
        note += " => " + (focus.empty() ? std::string("?") : focus[0]);
    }
    ~MouseTrace() {
        if (!on)
            return;
        if (mouse_log.size() > 80)
            mouse_log.erase(mouse_log.begin(), mouse_log.begin() + 40);
        mouse_log.push_back(std::string(where) + ": " + before + "-> " + mouse_flags() + " window " +
                            std::to_string(*window_x) + "," + std::to_string(*window_y) + "," +
                            std::to_string(*window_z) + " mouse " + std::to_string(gps->mouse_x) + "," +
                            std::to_string(gps->mouse_y) + " " + note);
    }
};

bool mouse_buttons_active() {
    return enabler && (enabler->mouse_lbut || enabler->mouse_rbut || enabler->mouse_mbut ||
                       enabler->mouse_lbut_down || enabler->mouse_rbut_down || enabler->mouse_mbut_down ||
                       enabler->mouse_lbut_lift || enabler->mouse_rbut_lift || enabler->mouse_mbut_lift);
}

// ---------------------------------------------------------------------------
// simulation thread: capture the scene, handle input

// The scene is read from the map viewport. While the view is showing, the
// (hidden) top-down map is kept at its native zoom, where it holds the most
// tiles the game will draw cheaply; the player's own zoom comes back when the
// view is not showing.
void zoom_for_capture() {
    const int want = 128;
    if (gps->viewport_zoom_factor == want)
        return;
    if (!zoom_saved) {
        saved_zoom = gps->viewport_zoom_factor;
        zoom_saved = true;
    }
    gps->viewport_zoom_factor = want;
    gps->main_thread_requesting_reshape = true;
}

void restore_zoom() {
    if (!zoom_saved || !gps)
        return;
    zoom_saved = false;
    if (gps->viewport_zoom_factor != saved_zoom) {
        gps->viewport_zoom_factor = saved_zoom;
        gps->main_thread_requesting_reshape = true;
    }
}

// calls fn(bytes, element_size) for every per-tile array of a map viewport
template <typename Fn>
void visit_layers(df::graphic_viewportst *vp, Fn &&fn) {
#define LAYER(name)                                                                     \
    if (vp->name)                                                                       \
        fn(reinterpret_cast<uint8_t *>(vp->name), sizeof(*vp->name));
    LAYER(screentexpos_background)
    LAYER(screentexpos_floor_flag)
    LAYER(screentexpos_background_two)
    LAYER(screentexpos_liquid_flag)
    LAYER(screentexpos_spatter_flag)
    LAYER(screentexpos_spatter)
    LAYER(screentexpos_ramp_flag)
    LAYER(screentexpos_shadow_flag)
    LAYER(screentexpos_building_one)
    LAYER(screentexpos_item)
    LAYER(screentexpos_vehicle)
    LAYER(screentexpos_vermin)
    LAYER(screentexpos_left_creature)
    LAYER(screentexpos)
    LAYER(screentexpos_right_creature)
    LAYER(screentexpos_building_two)
    LAYER(screentexpos_projectile)
    LAYER(screentexpos_high_flow)
    LAYER(screentexpos_top_shadow)
    LAYER(screentexpos_signpost)
    LAYER(screentexpos_upleft_creature)
    LAYER(screentexpos_up_creature)
    LAYER(screentexpos_upright_creature)
    LAYER(screentexpos_designation)
    LAYER(screentexpos_interface)
#undef LAYER
}

// The minimap shows the normal top-down map around the adventurer. A square
// of the map viewport is copied out right after the game has drawn the level
// we stand on (take), and put back into the top-left corner of the otherwise
// emptied viewport at the end of the frame (put). The renderer then asks the
// game to draw exactly those tiles into the minimap.
std::vector<std::vector<uint8_t>> mini_layers;
int mini_taken = 0;
std::atomic<uint64_t> mini_hash{0};     // changes when the minimap's tiles do

void minimap_take(df::graphic_viewportst *vp, int center_x, int center_y) {
    mini_taken = 0;
    if (!settings.minimap || !vp)
        return;
    const int cols = vp->dim_x, rows = vp->dim_y;
    int n = std::min({mini_tiles(), cols, rows});
    if (n % 2 == 0)
        --n;
    if (n < 3)
        return;
    const int x0 = std::clamp(center_x - n / 2, 0, cols - n);
    const int y0 = std::clamp(center_y - n / 2, 0, rows - n);
    mini_layers.clear();
    uint64_t hash = 1469598103934665603ull + uint64_t(n);
    visit_layers(vp, [&](uint8_t *data, size_t size) {
        std::vector<uint8_t> copy(size_t(n) * n * size);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                std::memcpy(&copy[(size_t(i) * n + j) * size], data + (size_t(x0 + i) * rows + (y0 + j)) * size, size);
        for (size_t k = 0; k + 8 <= copy.size(); k += 8) {
            uint64_t word;
            std::memcpy(&word, &copy[k], 8);
            hash = (hash ^ word) * 1099511628211ull;
        }
        mini_layers.push_back(std::move(copy));
    });
    mini_hash = hash;
    mini_taken = n;
    mini_self = (center_x - x0) << 8 | (center_y - y0);
}

void minimap_put(df::graphic_viewportst *vp) {
    const int n = mini_taken;
    if (!n || !vp || vp->dim_x < n || vp->dim_y < n) {
        mini_n = 0;
        return;
    }
    const int rows = vp->dim_y;
    size_t k = 0;
    visit_layers(vp, [&](uint8_t *data, size_t size) {
        if (k < mini_layers.size() && mini_layers[k].size() == size_t(n) * n * size) {
            const std::vector<uint8_t> &copy = mini_layers[k];
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                    std::memcpy(data + (size_t(i) * rows + j) * size, &copy[(size_t(i) * n + j) * size], size);
        }
        ++k;
    });
    mini_n = n;
}

// The minimap can also be drawn the classic way, in text characters. That
// picture is made here from the map itself and handed to the renderer.
constexpr int GLYPH = 16;               // pixels per character
std::mutex ascii_mutex;
std::vector<uint32_t> ascii_pixels;     // ascii_side * ascii_side, row by row
int ascii_side = 0;
uint64_t ascii_version = 0;
uint64_t ascii_hash = 0;

struct AsciiCell {
    uint8_t ch = ' ', fg = 7;
};

const uint8_t ASCII_PALETTE[16][3] = {
    {0, 0, 0},       {13, 103, 196},  {68, 158, 53},   {86, 163, 205},  {151, 26, 26},   {255, 110, 187},
    {120, 94, 47},   {185, 192, 162}, {88, 83, 86},    {145, 202, 255}, {131, 212, 82},  {176, 223, 215},
    {255, 34, 34},   {255, 167, 246}, {255, 218, 90},  {255, 255, 255}};

// one character of the game's font as 16 rows of 16 bits
const uint16_t *ascii_glyph(uint8_t ch) {
    static uint16_t glyphs[256][GLYPH];
    static bool loaded[256];
    if (!loaded[ch]) {
        loaded[ch] = true;
        std::memset(glyphs[ch], 0, sizeof(glyphs[ch]));
        int32_t texpos = init ? int32_t(init->font.large_font_texpos[ch]) : 0;
        if (!texpos && init)
            texpos = int32_t(init->font.small_font_texpos[ch]);
        uint32_t raw[fpv::TEXPIX];
        if (texpos && fpv::TextureStore::readSprite(texpos, raw)) {
            const auto lit = [&](int x, int y) {
                const uint32_t c = raw[y * fpv::TEX + x];
                const int r = fpv::px_r(c), g = fpv::px_g(c), b = fpv::px_b(c);
                if (fpv::px_a(c) < 128 || (r > 200 && b > 200 && g < 80))
                    return 0;
                return std::max({r, g, b}) > 90 ? 1 : 0;
            };
            for (int y = 0; y < GLYPH; ++y)
                for (int x = 0; x < GLYPH; ++x)
                    if (lit(2 * x, 2 * y) + lit(2 * x + 1, 2 * y) + lit(2 * x, 2 * y + 1) + lit(2 * x + 1, 2 * y + 1) >= 2)
                        glyphs[ch][y] |= uint16_t(1u << x);
        }
    }
    return glyphs[ch];
}

AsciiCell ascii_terrain(int x, int y, int z) {
    AsciiCell cell;
    const df::tiletype *ttp = Maps::getTileType(x, y, z);
    const df::tile_designation *des = Maps::getTileDesignation(x, y, z);
    if (!ttp || !des || des->bits.hidden)
        return cell;
    const df::tiletype tt = *ttp;
    const df::tiletype_shape shape = tileShape(tt);
    const df::tiletype_shape_basic basic = tileShapeBasic(shape);
    const df::tiletype_material tm = tileMaterial(tt);
    const bool tree = tm == df::tiletype_material::TREE || tm == df::tiletype_material::MUSHROOM;
    const bool dead = tileSpecial(tt) == df::tiletype_special::DEAD;
    const bool soil = tm == df::tiletype_material::SOIL;
    const bool built = tm == df::tiletype_material::CONSTRUCTION;
    const uint32_t speck = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u;
    uint8_t ground = 7;
    switch (tm) {
    case df::tiletype_material::GRASS_LIGHT: ground = 10; break;
    case df::tiletype_material::GRASS_DARK: ground = 2; break;
    case df::tiletype_material::GRASS_DRY: ground = 14; break;
    case df::tiletype_material::GRASS_DEAD: ground = 6; break;
    case df::tiletype_material::SOIL: ground = 6; break;
    case df::tiletype_material::FROZEN_LIQUID: ground = 11; break;
    default: break;
    }

    if (const df::tile_occupancy *occ = Maps::getTileOccupancy(x, y, z)) {
        if (occ->bits.building != df::tile_building_occ::None) {
            if (df::building *bld = Buildings::findAtTile(df::coord(x, y, z))) {
                cell.fg = 6;
                switch (bld->getType()) {
                case df::building_type::Door: {
                    auto door = virtual_cast<df::building_doorst>(bld);
                    cell.ch = (door && !door->door_flags.bits.closed) ? '\'' : '+';
                    break;
                }
                case df::building_type::Table: cell.ch = 209; break;
                case df::building_type::Chair: cell.ch = 210; break;
                case df::building_type::Bed: cell.ch = 233; break;
                case df::building_type::Box: cell.ch = 146; break;
                case df::building_type::Cabinet: cell.ch = 227; break;
                case df::building_type::Statue: cell.ch = 234; cell.fg = 7; break;
                case df::building_type::Well: cell.ch = 9; cell.fg = 7; break;
                default: cell.ch = 240; cell.fg = 7; break;
                }
                return cell;
            }
        }
        if (occ->bits.item && basic != df::tiletype_shape_basic::Wall) {
            cell.ch = '*';
            cell.fg = 15;
            return cell;
        }
    }
    if (des->bits.flow_size >= 2 && basic != df::tiletype_shape_basic::Wall) {
        cell.ch = des->bits.flow_size >= 4 ? 247 : '~';
        cell.fg = des->bits.liquid_type == df::tile_liquid::Magma ? 12 : 9;
        return cell;
    }
    switch (shape) {
    case df::tiletype_shape::FORTIFICATION: cell.ch = 206; cell.fg = 7; return cell;
    case df::tiletype_shape::STAIR_UP: cell.ch = '<'; cell.fg = 15; return cell;
    case df::tiletype_shape::STAIR_DOWN: cell.ch = '>'; cell.fg = 15; return cell;
    case df::tiletype_shape::STAIR_UPDOWN: cell.ch = 'X'; cell.fg = 15; return cell;
    case df::tiletype_shape::RAMP: cell.ch = 30; cell.fg = ground; return cell;
    case df::tiletype_shape::RAMP_TOP: cell.ch = 31; cell.fg = 8; return cell;
    case df::tiletype_shape::BOULDER: cell.ch = 7; cell.fg = 7; return cell;
    case df::tiletype_shape::PEBBLES: cell.ch = ','; cell.fg = 7; return cell;
    case df::tiletype_shape::SHRUB: cell.ch = '"'; cell.fg = dead ? 6 : 2; return cell;
    case df::tiletype_shape::SAPLING: cell.ch = 231; cell.fg = dead ? 6 : 2; return cell;
    case df::tiletype_shape::BRANCH:
    case df::tiletype_shape::TRUNK_BRANCH:
    case df::tiletype_shape::TWIG:
        cell.ch = shape == df::tiletype_shape::TWIG ? ';' : 5;
        cell.fg = dead ? 6 : 2;
        return cell;
    default: break;
    }
    if (basic == df::tiletype_shape_basic::Wall) {
        if (tree) {
            cell.ch = 'O';
            cell.fg = 6;
        } else {
            cell.ch = (built || tileSpecial(tt) == df::tiletype_special::SMOOTH) ? 219 : 177;
            cell.fg = soil ? 6 : 7;
        }
        return cell;
    }
    if (basic == df::tiletype_shape_basic::Open || basic == df::tiletype_shape_basic::None) {
        // the ground below shows through, dimly
        const df::tiletype *below = Maps::getTileType(x, y, z - 1);
        if (below && tileShapeBasic(tileShape(*below)) != df::tiletype_shape_basic::Open) {
            cell.ch = 250;
            cell.fg = 8;
        }
        return cell;
    }
    // floors
    if (built || tileSpecial(tt) == df::tiletype_special::SMOOTH) {
        cell.ch = '+';
        cell.fg = 7;
    } else {
        static const uint8_t specks[4] = {'.', ',', '`', '\''};
        cell.ch = (ground == 7 || soil) ? '.' : specks[(speck >> 5) & 3];
        cell.fg = ground;
    }
    return cell;
}

void minimap_ascii_build(const df::unit *adv) {
    const int n = mini_tiles();
    const int z = adv->pos.z, x0 = adv->pos.x - n / 2, y0 = adv->pos.y - n / 2;
    std::vector<AsciiCell> cells(size_t(n) * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            cells[size_t(j) * n + i] = ascii_terrain(x0 + i, y0 + j, z);
    {
        // creatures the first-person view shows are on the map as well
        std::lock_guard<std::mutex> lock(scene.mutex);
        for (const fpv::Billboard &b : scene.units) {
            const int i = b.mx - x0, j = b.my - y0;
            if (b.mz != z || i < 0 || j < 0 || i >= n || j >= n)
                continue;
            AsciiCell &cell = cells[size_t(j) * n + i];
            cell.ch = '?';
            cell.fg = 15;
            if (const df::unit *unit = df::unit::find(b.unit_id))
                if (const df::creature_raw *raw = df::creature_raw::find(unit->race)) {
                    if (raw->creature_tile)
                        cell.ch = raw->creature_tile;
                    cell.fg = uint8_t((raw->color[0] & 7) + (raw->color[2] ? 8 : 0));
                    if (cell.fg == 0)
                        cell.fg = 8;
                }
        }
    }
    cells[size_t(n / 2) * n + n / 2] = AsciiCell{'@', 15};

    uint64_t hash = 1469598103934665603ull;
    for (const AsciiCell &cell : cells)
        hash = (hash ^ (uint64_t(cell.ch) << 8 | cell.fg)) * 1099511628211ull;
    const int side = n * GLYPH;
    std::lock_guard<std::mutex> lock(ascii_mutex);
    if (hash == ascii_hash && ascii_side == side)
        return;
    ascii_hash = hash;
    ascii_side = side;
    ascii_pixels.assign(size_t(side) * side, 0xFF000000u);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const AsciiCell &cell = cells[size_t(j) * n + i];
            if (cell.ch == ' ')
                continue;
            const uint16_t *glyph = ascii_glyph(cell.ch);
            const uint8_t *c = ASCII_PALETTE[cell.fg & 15];
            const uint32_t color = fpv::rgb(c[0], c[1], c[2]);
            for (int y = 0; y < GLYPH; ++y)
                for (int x = 0; x < GLYPH; ++x)
                    if (glyph[y] & (1u << x))
                        ascii_pixels[size_t(j * GLYPH + y) * side + i * GLYPH + x] = color;
        }
    ++ascii_version;
}

// take what the interface drew under the minimap off the screen
void hide_interface_under_minimap() {
    SDL_Rect rect;
    if (!gps || !minimap_rect(rect))
        return;
    const auto clear_cell = [](int cx, int cy) {
        if (cx < 0 || cy < 0 || cx >= gps->dimx || cy >= gps->dimy)
            return;
        const int idx = cx * gps->dimy + cy;
        std::memset(&gps->screen[idx * 8], 0, 8);
        gps->screentexpos[idx] = 0;
        gps->screentexpos_lower[idx] = 0;
        gps->screentexpos_anchored[idx] = 0;
        gps->screentexpos_flag[idx] = 0;
    };
    int cx0, cy0, cx1, cy1;
    interface_cell(rect.x - 4, rect.y, cx0, cy0);
    interface_cell(rect.x + rect.w - 1, rect.y + rect.h - 1, cx1, cy1);
    for (int cx = cx0; cx < gps->dimx; ++cx)
        for (int cy = cy0; cy <= cy1; ++cy)
            clear_cell(cx, cy);
    // the two small buttons at the end of the date line
    for (int cx = gps->dimx - 4; cx < gps->dimx; ++cx)
        for (int cy = 0; cy < cy0; ++cy)
            clear_cell(cx, cy);
}

// Simulation thread, right after the game's last draw of the frame: find the
// game's buttons in the bottom right corner and take them off the screen (see
// in_corner). They are pictures, so they are told from the message text by
// their textures: the run of textured columns that ends at the right edge,
// within the bottom rows. Only on the plain adventure screen, where the grid
// was emptied before that draw; menus that cover the corner are left alone.
void hide_corner_buttons(bool plain) {
    if (!gps || !plain || gps->dimx < 60 || gps->dimy < 16) {
        corner_live = false;
        return;
    }
    const int rows = 8;                 // two rows of buttons and a little more
    const int y_top = gps->dimy - rows;
    const auto textured = [](int idx) {
        return gps->screentexpos[idx] || gps->screentexpos_lower[idx] || gps->screentexpos_anchored[idx];
    };
    int found = -1, top = gps->dimy, empty = 0;
    for (int cx = gps->dimx - 1; cx >= gps->dimx - 40; --cx) {
        bool any = false;
        for (int cy = y_top; cy < gps->dimy; ++cy)
            if (textured(cx * gps->dimy + cy)) {
                any = true;
                top = std::min(top, cy);
            }
        if (any) {
            found = cx;
            empty = 0;
        } else if (found >= 0 && ++empty >= 2) {
            break;
        }
    }
    if (found >= 0) {
        corner_x0 = found;
        corner_y0 = top;
    }
    const int x0 = corner_x0.load(), y0 = corner_y0.load();
    corner_live = x0 >= 0 && y0 >= 0;
    if (!corner_live)
        return;
    for (int cx = x0; cx < gps->dimx; ++cx)
        for (int cy = y0; cy < gps->dimy; ++cy) {
            const int idx = cx * gps->dimy + cy;
            std::memset(&gps->screen[idx * 8], 0, 8);
            gps->screentexpos[idx] = 0;
            gps->screentexpos_lower[idx] = 0;
            gps->screentexpos_anchored[idx] = 0;
            gps->screentexpos_flag[idx] = 0;
        }
}

// one interface cell, all its layers
struct GridCell {
    int idx = 0;
    uint8_t screen[8] = {};
    long tex = 0, lower = 0, anchored = 0;
    uint32_t flag = 0;
};

GridCell read_cell(int idx) {
    GridCell c;
    c.idx = idx;
    std::memcpy(c.screen, &gps->screen[idx * 8], 8);
    c.tex = gps->screentexpos[idx];
    c.lower = gps->screentexpos_lower[idx];
    c.anchored = gps->screentexpos_anchored[idx];
    c.flag = gps->screentexpos_flag[idx];
    return c;
}

void write_cell(int idx, const GridCell &c) {
    std::memcpy(&gps->screen[idx * 8], c.screen, 8);
    gps->screentexpos[idx] = c.tex;
    gps->screentexpos_lower[idx] = c.lower;
    gps->screentexpos_anchored[idx] = c.anchored;
    gps->screentexpos_flag[idx] = c.flag;
}

bool same_cell(int idx, const GridCell &c) {
    return std::memcmp(&gps->screen[idx * 8], c.screen, 8) == 0 && gps->screentexpos[idx] == c.tex &&
           gps->screentexpos_lower[idx] == c.lower && gps->screentexpos_anchored[idx] == c.anchored &&
           gps->screentexpos_flag[idx] == c.flag;
}

// what move_bottom_buttons put on the grid, and for which grid size
std::vector<GridCell> bar_written;
int bar_dimx = 0, bar_dimy = 0;

// Take the moved buttons off the grid again. The game does not wipe the grid
// over the map, so they would stay when the view goes or a menu opens. Cells
// the game has drawn something else into since are left alone.
void unmove_action_bar() {
    moved_bar.live = false;
    moved_buttons.live = false;
    if (gps && gps->dimx == bar_dimx && gps->dimy == bar_dimy)
        for (const GridCell &c : bar_written)
            if (same_cell(c.idx, c))
                write_cell(c.idx, GridCell());
    bar_written.clear();
}

// anything in this cell at all, a coloured background included
bool cell_used(int idx) {
    for (int i = 0; i < 8; ++i)
        if (gps->screen[idx * 8 + i])
            return true;
    return gps->screentexpos[idx] || gps->screentexpos_lower[idx] || gps->screentexpos_anchored[idx] ||
           gps->screentexpos_flag[idx];
}

// Simulation thread, after hide_corner_buttons: see MovedBlock. The action
// bar is found as the run of textured columns in the bottom rows around the
// middle of the screen, the buttons as the run of used columns in the bottom
// rows from right of the portrait; gaps of up to two columns (between groups
// of buttons) belong to them.
void move_bottom_buttons(bool plain) {
    moved_bar.live = false;
    moved_buttons.live = false;
    if (!gps || !plain || gps->dimx < 100 || gps->dimy < 20)
        return;
    const int dimx = gps->dimx, dimy = gps->dimy;
    const int rows = 5;
    const auto textured = [dimy](int cx, int cy) {
        const int idx = cx * dimy + cy;
        return gps->screentexpos[idx] || gps->screentexpos_lower[idx] || gps->screentexpos_anchored[idx];
    };
    const auto bar_column = [&](int cx) {
        for (int cy = dimy - rows; cy < dimy; ++cy)
            if (textured(cx, cy))
                return true;
        return false;
    };
    const auto button_column = [&](int cx) {
        for (int cy = dimy - BUTTON_ROWS; cy < dimy; ++cy)
            if (cell_used(cx * dimy + cy))
                return true;
        return false;
    };
    // a run of columns from start, both ways as far as allowed
    const auto run = [](int start, int lo, int hi, const auto &column, int &x0, int &x1) {
        x0 = x1 = start;
        for (int cx = start - 1, gap = 0; cx >= lo && gap <= 2; --cx) {
            if (column(cx)) {
                x0 = cx;
                gap = 0;
            } else {
                ++gap;
            }
        }
        for (int cx = start + 1, gap = 0; cx <= hi && gap <= 2; ++cx) {
            if (column(cx)) {
                x1 = cx;
                gap = 0;
            } else {
                ++gap;
            }
        }
    };

    // the action bar, where the game drew it
    const int right = corner_live ? std::min(dimx, corner_x0.load()) - 1 : dimx - 1;
    int start = -1;
    for (int d = 0; d <= 6 && start < 0; ++d) {
        if (bar_column(dimx / 2 - d))
            start = dimx / 2 - d;
        else if (dimx / 2 + d <= right && bar_column(dimx / 2 + d))
            start = dimx / 2 + d;
    }
    int bx0 = -1, bx1 = -1, btop = dimy;
    if (start >= 0 && start <= right) {
        run(start, BAR_LEFT + 4, right, bar_column, bx0, bx1);
        for (int cx = bx0; cx <= bx1; ++cx)
            for (int cy = dimy - rows; cy < dimy; ++cy)
                if (textured(cx, cy))
                    btop = std::min(btop, cy);
    }

    // the buttons right of the portrait, and the speed display
    int kx0 = -1, kx1 = -1;
    {
        const int hi = (bx0 >= 0 ? bx0 : dimx / 2) - 2;
        int first = -1;
        for (int cx = BAR_LEFT; cx <= BAR_LEFT + 4 && first < 0; ++cx)
            if (button_column(cx))
                first = cx;
        if (first >= 0 && first <= hi)
            run(first, first, hi, button_column, kx0, kx1);
    }

    int pcx = -1, pcy = -1;
    if (frame_pointer.on_bar)
        interface_cell(frame_pointer.px, frame_pointer.py, pcx, pcy);
    const auto pointer_in = [&](int x0, int x1, int y0, int y1) {
        return pcx >= x0 && pcx <= x1 && pcy >= y0 && pcy <= y1;
    };

    bar_dimx = dimx;
    bar_dimy = dimy;
    // Cells taken off the grid, all of them, before any is put back: the
    // blocks' new places overlap where others were. Only cells with something
    // in them are moved, so a block's empty cells do not cover another's.
    struct Lift {
        GridCell cell;
        int mx, my;
    };
    std::vector<Lift> lifted;
    const auto lift = [&](int cx, int cy, int mx, int my) {
        const int idx = cx * dimy + cy;
        const int tx = cx + mx, ty = cy + my;
        if (!cell_used(idx) || tx < 0 || tx >= dimx || ty < 0 || ty >= dimy)
            return;
        lifted.push_back(Lift{read_cell(idx), mx, my});
        write_cell(idx, GridCell());
    };
    // The caption the game shows over the button the pointer is on goes
    // along, if it drew one just above the block. A picture there is another
    // button ("View your companions" above the row right of the portrait) and
    // stays.
    const auto lift_caption = [&](int x0, int x1, int top, int mx, int my) {
        for (int cx = std::max(0, x0 - 12); cx <= std::min(dimx - 1, x1 + 12); ++cx)
            for (int cy = std::max(0, top - 8); cy < top; ++cy)
                if (grid_cell_drawn(size_t(cx) * dimy + cy) && !textured(cx, cy))
                    lift(cx, cy, mx, my);
    };

    // the buttons up beside the timer, the bar left into the bottom rows they leave
    const int bmx = -(bx0 - BAR_LEFT);
    const bool bar_moves = bx0 >= 0 && bmx < 0;
    const int kmx = BUTTONS_LEFT - kx0, kmy = -BUTTON_ROWS, ktop = dimy - BUTTON_ROWS;
    const bool buttons_move = kx0 >= 0 && kx1 + kmx < dimx;
    if (bar_moves && pointer_in(bx0 + bmx, bx1 + bmx, btop, dimy - 1))
        lift_caption(bx0, bx1, btop, bmx, 0);
    if (buttons_move && pointer_in(kx0 + kmx, kx1 + kmx, ktop + kmy, dimy - 1 + kmy))
        lift_caption(kx0, kx1, ktop, kmx, kmy);
    if (buttons_move)
        for (int cx = kx0; cx <= kx1; ++cx)
            for (int cy = ktop; cy < dimy; ++cy)
                lift(cx, cy, kmx, kmy);
    if (bar_moves)
        for (int cx = bx0; cx <= bx1; ++cx)
            for (int cy = btop; cy < dimy; ++cy)
                lift(cx, cy, bmx, 0);
    // captions were lifted first, so they are put back last, over the buttons
    for (auto it = lifted.rbegin(); it != lifted.rend(); ++it) {
        const int cx = it->cell.idx / dimy, cy = it->cell.idx % dimy;
        GridCell placed = it->cell;
        placed.idx = (cx + it->mx) * dimy + (cy + it->my);
        write_cell(placed.idx, placed);
        bar_written.push_back(placed);
    }

    if (buttons_move) {
        const int mx = kmx, my = kmy, top = ktop;
        moved_buttons.x0 = kx0 + mx;
        moved_buttons.x1 = kx1 + mx;
        moved_buttons.y0 = top + my;
        moved_buttons.y1 = dimy - 1 + my;
        moved_buttons.dx = -mx;
        moved_buttons.dy = -my;
        moved_buttons.live = true;
    }

    if (bar_moves) {
        const int dx = -bmx;
        moved_bar.x0 = bx0 - dx;
        moved_bar.x1 = bx1 - dx;
        moved_bar.y0 = btop;
        moved_bar.y1 = dimy - 1;
        moved_bar.dx = dx;
        moved_bar.dy = 0;
        moved_bar.live = true;
    }
}

// The game's box of new messages at the top of the screen (combat, speech,
// with the speaker's portrait), which waits for the player while it has more
// than fits. While the view shows, every message is in the mod's message
// list, so the box is put away before the game draws it, by the game's own
// state for it: scrolled to its end (nothing more to page through) and no
// longer showing. DFHack's advtools fastcombat ends it the same way.
void dismiss_announcements() {
    if (!world)
        return;
    auto &st = world->status;
    if (!st.temp_flag.bits.adv_showing_announcements && !st.temp_flag.bits.adv_have_more)
        return;
    st.adv_scroll_position = std::max(0, int(st.adv_announcement.size()) - 10);
    st.temp_flag.bits.adv_have_more = false;
    st.temp_flag.bits.adv_showing_announcements = false;
}

// do any tiles of this map window on this level have something in them?
std::unordered_map<uint64_t, bool> window_content;

bool window_has_content(int z, int wx, int wy, int cols, int rows) {
    const uint64_t key = (uint64_t(uint16_t(z)) << 40) | (uint64_t(uint32_t(wx) & 0xFFFFF) << 20) |
                         (uint32_t(wy) & 0xFFFFF);
    const auto it = window_content.find(key);
    if (it != window_content.end())
        return it->second;
    bool any = false;
    for (int x = std::max(0, wx); x < wx + cols && !any; ++x)
        for (int y = std::max(0, wy); y < wy + rows; ++y) {
            const df::tiletype *tt = Maps::getTileType(x, y, z);
            if (!tt)
                continue;
            const df::tiletype_shape_basic basic = tileShapeBasic(tileShape(*tt));
            if (basic != df::tiletype_shape_basic::Open && basic != df::tiletype_shape_basic::None) {
                any = true;
                break;
            }
        }
    window_content[key] = any;
    return any;
}

// Empty every layer of the map viewport. The renderer then has no map tiles
// to draw underneath our image, which is most of its work when zoomed out.
// The game fills the layers again the next time it draws.
void blank_viewport(df::graphic_viewportst *vp) {
    if (!vp)
        return;
    const size_t n = size_t(std::max(0, vp->dim_x)) * size_t(std::max(0, vp->dim_y));
    const auto zero = [n](auto *p) {
        if (p)
            std::memset(p, 0, n * sizeof(*p));
    };
    zero(vp->screentexpos_background);
    zero(vp->screentexpos_floor_flag);
    zero(vp->screentexpos_background_two);
    zero(vp->screentexpos_liquid_flag);
    zero(vp->screentexpos_spatter_flag);
    zero(vp->screentexpos_spatter);
    zero(vp->screentexpos_ramp_flag);
    zero(vp->screentexpos_shadow_flag);
    zero(vp->screentexpos_building_one);
    zero(vp->screentexpos_item);
    zero(vp->screentexpos_vehicle);
    zero(vp->screentexpos_vermin);
    zero(vp->screentexpos_left_creature);
    zero(vp->screentexpos);
    zero(vp->screentexpos_right_creature);
    zero(vp->screentexpos_building_two);
    zero(vp->screentexpos_projectile);
    zero(vp->screentexpos_high_flow);
    zero(vp->screentexpos_top_shadow);
    zero(vp->screentexpos_signpost);
    zero(vp->screentexpos_upleft_creature);
    zero(vp->screentexpos_up_creature);
    zero(vp->screentexpos_upright_creature);
    zero(vp->screentexpos_designation);
    zero(vp->screentexpos_interface);
}

// sprite of an item as the game shows it lying about, 0 if we do not know one
int32_t held_texpos(df::item *item) {
    if (auto weapon = virtual_cast<df::item_weaponst>(item)) {
        if (df::itemdef_weaponst *def = weapon->subtype) {
            if (def->texpos_item_default) return def->texpos_item_default;
            if (def->texpos_item_material) return def->texpos_item_material;
            return def->texpos_item_wood;
        }
    } else if (auto shield = virtual_cast<df::item_shieldst>(item)) {
        if (df::itemdef_shieldst *def = shield->subtype)
            return def->texpos_item ? def->texpos_item : def->texpos_item_wooden;
    } else if (auto tool = virtual_cast<df::item_toolst>(item)) {
        if (df::itemdef_toolst *def = tool->subtype)
            return def->texpos_item;
    }
    return 0;
}

bool is_ranged_weapon(df::item *item) {
    auto weapon = virtual_cast<df::item_weaponst>(item);
    return weapon && weapon->subtype && weapon->subtype->skill_ranged != df::job_skill::NONE;
}

// what is in the adventurer's hands; true if one of them holds a ranged weapon
bool update_held(df::unit *adv, bool want_sprites) {
    df::item *hands[2] = {nullptr, nullptr};
    bool ranged = false;
    for (df::unit_inventory_item *inv : adv->inventory) {
        if (!inv || !inv->item || inv->mode != df::inv_item_role_type::Weapon)
            continue;
        ranged = ranged || is_ranged_weapon(inv->item);
        bool left = false;
        if (adv->body.body_plan && inv->body_part_id >= 0 &&
            size_t(inv->body_part_id) < adv->body.body_plan->body_parts.size())
            if (df::body_part_raw *part = adv->body.body_plan->body_parts[inv->body_part_id])
                left = part->flags.is_set(df::body_part_raw_flags::LEFT);
        int hand = left ? 0 : 1;
        if (hands[hand])
            hand = 1 - hand;
        if (!hands[hand])
            hands[hand] = inv->item;
    }
    if (want_sprites) {
        std::lock_guard<std::mutex> lock(held_mutex);
        for (int i = 0; i < 2; ++i) {
            const int32_t texpos = hands[i] ? held_texpos(hands[i]) : 0;
            if (!texpos) {
                held[i].on = false;
                held[i].texpos = 0;
            } else if (texpos != held[i].texpos || !held[i].on) {
                held[i].on = fpv::TextureStore::readSprite(texpos, held[i].px);
                held[i].texpos = texpos;
            }
        }
    }
    return ranged;
}

// What fighting looks like. Every creature's strikes are its Attack actions
// (target, weapon, time to strike); a strike happens when that time runs out
// or the action ends. Then:
//  - yours: the weapon in your hand lunges at the pointer and back (draw_held)
//  - anyone else's: the attacker's picture lunges at its target and a white
//    crescent is swept between them (nudges, draw_combat_fx)
//  - one at you: an arrow from the pointer towards the attacker, white for a
//    miss, red when it wounded you (a new wound: wound_next_id went up)
double fx_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct FxArc {
    float ax, ay, ah, bx, by, bh;   // from the attacker to the target (map tiles, height in tile widths)
    bool at_you;
    double start;
};
struct FxArrow {
    float ax, ay;            // where the attacker is
    int32_t baseline;        // your next wound's id when the strike began
    bool red;
    double start;
};
struct FxNudge {
    float dx, dy;            // towards the target, one tile long
    double start;
};
constexpr double ARC_TIME = 0.4, ARROW_TIME = 1.2, NUDGE_TIME = 0.3, LUNGE_TIME = 0.22;
std::mutex fx_mutex;
std::vector<FxArc> fx_arcs;
std::vector<FxArrow> fx_arrows;
std::unordered_map<int32_t, FxNudge> fx_nudges;   // by attacker

// Words floating up from a creature that was struck: "MISS!" in green, the
// wound in red ("Head: Bruise", "Finger, Left hand: Cut off"), what got stuck
// in it in purple ("Lower body: stuck iron arrow")
struct FxText {
    int32_t unit_id;
    float x, y, h;           // over its head, map tiles / tile widths
    bool you;                // on the adventurer: shown under the crosshair
    std::string text;
    uint32_t color;
    int stack;               // how many came just before for the same creature
    double start;
};
constexpr double TEXT_TIME = 1.4;
std::vector<FxText> fx_texts;
struct PendingMiss {
    int32_t target, baseline;
    size_t stuck;
    double start;
};
std::vector<PendingMiss> fx_misses;
std::unordered_map<int32_t, int32_t> wounds_seen;               // per creature: its next wound id when last looked
struct HeadSeen {
    float x, y, top;   // where its head was last drawn
    uint32_t frame;
};
std::unordered_map<int32_t, HeadSeen> heads_seen;   // per creature (a dead one is not drawn any more)
std::unordered_map<int32_t, std::vector<int32_t>> stuck_seen;   // per creature: what was stuck in it
double fx_lunge_start = -10;
int fx_lunge_hand = 1;

struct SeenAttack {
    int32_t attacker, target, item;
    int32_t target_wounds;   // the target's next wound id when it was first seen
    bool fired;
    uint32_t frame;
};
std::unordered_map<uint64_t, SeenAttack> seen_attacks;

// a body part's name, short: "Left arm", "Finger, Left hand"
std::string part_name(const df::unit *u, int bp) {
    if (!u || !u->body.body_plan || bp < 0 || size_t(bp) >= u->body.body_plan->body_parts.size())
        return "Body";
    const df::body_part_raw *part = u->body.body_plan->body_parts[bp];
    if (!part || part->name_singular.empty() || !part->name_singular[0])
        return "Body";
    std::string name = *part->name_singular[0];
    for (const char *drop : {"first ", "second ", "third ", "fourth ", "fifth ", "upper ", "lower "}) {
        const size_t at = name.find(drop);
        // "upper"/"lower" stay where they name the part itself ("upper body", "lower body")
        if (at != std::string::npos && name.find("body") == std::string::npos)
            name.erase(at, std::strlen(drop));
    }
    const bool digit = part->flags.is_set(df::body_part_raw_flags::DIGIT);
    if (digit && part->con_part_id >= 0 && size_t(part->con_part_id) < u->body.body_plan->body_parts.size())
        if (const df::body_part_raw *parent = u->body.body_plan->body_parts[part->con_part_id])
            if (!parent->name_singular.empty() && parent->name_singular[0]) {
                std::string p = *parent->name_singular[0];
                if (!p.empty())
                    p[0] = char(std::toupper(uint8_t(p[0])));
                name += ", " + p;
            }
    if (!name.empty())
        name[0] = char(std::toupper(uint8_t(name[0])));
    return name;
}

// the worst of what a wound did, in a word or two
std::string wound_word(const df::unit_wound *w) {
    bool cut = false, smashed_apart = false, guts = false, compound = false, broken = false, fracture = false,
         major_artery = false, artery = false, nerve = false, gouged = false, torn = false, sprain = false,
         bruise = false;
    int effect = -1;
    for (const df::unit_wound_layerst *l : w->parts) {
        if (!l)
            continue;
        const auto &f = l->flags1.bits;
        cut = cut || f.cut || f.edged_damage;
        smashed_apart = smashed_apart || f.smashed_apart;
        guts = guts || f.guts_spilled;
        compound = compound || f.compound_fracture;
        broken = broken || f.broken;
        fracture = fracture || f.overlapping_fracture;
        major_artery = major_artery || f.major_artery;
        artery = artery || f.artery;
        nerve = nerve || f.motor_nerve_severed || f.sensory_nerve_severed;
        gouged = gouged || f.gouged;
        torn = torn || f.tendon_torn || f.ligament_torn;
        sprain = sprain || f.tendon_strained || f.ligament_sprained;
        bruise = bruise || f.smashed || f.tendon_bruised || f.ligament_bruised || f.blunt_shake1 || f.blunt_shake2;
        for (const df::wound_effect_type e : l->effect_type) {
            if (e == df::wound_effect_type::Bruise)
                bruise = true;
            else if (int(e) > 0 && effect < 0)
                effect = int(e);
        }
    }
    if (w->flags.bits.severed_part)
        return cut ? "Cut off" : "Torn off";
    if (smashed_apart)
        return "Smashed apart";
    if (guts)
        return "Guts spilled";
    if (compound)
        return "Compound fracture";
    if (broken)
        return "Broken";
    if (fracture)
        return "Fracture";
    if (major_artery)
        return "Artery opened";
    if (nerve)
        return "Nerve severed";
    if (gouged)
        return "Gouged";
    if (torn)
        return "Torn";
    if (cut)
        return artery ? "Cut, bleeding" : "Cut";
    switch (effect) {
    case int(df::wound_effect_type::Heat): return "Scorched";
    case int(df::wound_effect_type::Frostbite): return "Frostbite";
    case int(df::wound_effect_type::Burn): return "Burn";
    case int(df::wound_effect_type::Melting): return "Melted";
    case int(df::wound_effect_type::Boiling): return "Boiled";
    case int(df::wound_effect_type::Freezing): return "Frozen";
    case int(df::wound_effect_type::Condensation): return "Condensed";
    case int(df::wound_effect_type::Necrosis): return "Rotting";
    case int(df::wound_effect_type::Blister): return "Blister";
    default: break;
    }
    if (artery)
        return "Bleeding";
    if (sprain)
        return "Sprain";
    return bruise ? "Bruise" : "Hit";
}

// what is stuck in a creature
std::vector<std::pair<int32_t, int>> stuck_items(const df::unit *u) {
    std::vector<std::pair<int32_t, int>> out;
    for (const df::unit_inventory_item *inv : u->inventory)
        if (inv && inv->item && inv->mode == df::inv_item_role_type::StuckIn)
            out.emplace_back(inv->item->id, inv->body_part_id);
    return out;
}

// simulation thread, with scene.mutex held, right after the creatures were placed
void track_combat(df::unit *adv, uint32_t frame) {
    if (!world || !adv)
        return;
    const double now = fx_now();
    std::unordered_map<int32_t, const fpv::Billboard *> drawn;
    for (const fpv::Billboard &b : scene.units)
        if (b.unit_id >= 0)
            drawn[b.unit_id] = &b;
    const auto where = [&](const df::unit *u, float &x, float &y, float &h) {
        const auto it = drawn.find(u->id);
        if (it != drawn.end()) {
            x = it->second->x;
            y = it->second->y;
            h = it->second->base;
        } else {
            x = u->pos.x + 0.5f;
            y = u->pos.y + 0.5f;
            h = (u->pos.z + scene.groundRaise(u->pos.x, u->pos.y, u->pos.z)) * fpv::LEVEL_H;
        }
    };
    std::lock_guard<std::mutex> lock(fx_mutex);
    for (const auto &[id, b] : drawn)
        if (b->sprite) {
            const fpv::Sprite &sp = *b->sprite;
            heads_seen[id] = HeadSeen{b->x, b->y,
                                      b->base + float(sp.y1 - sp.y0) / fpv::TEX * fpv::THING_SCALE * b->scale *
                                                    (1.0f - b->sink),
                                      frame};
        }
    const auto fire = [&](const SeenAttack &s) {
        df::unit *a = df::unit::find(s.attacker);
        df::unit *t = df::unit::find(s.target);
        if (!a)
            return;
        if (t)
            fx_misses.push_back(PendingMiss{t->id, s.target_wounds, stuck_items(t).size(), now});
        if (a == adv) {
            fx_lunge_start = now;
            fx_lunge_hand = 1;
            for (df::unit_inventory_item *inv : adv->inventory)
                if (inv && inv->item && inv->item->id == s.item && adv->body.body_plan && inv->body_part_id >= 0 &&
                    size_t(inv->body_part_id) < adv->body.body_plan->body_parts.size())
                    if (df::body_part_raw *part = adv->body.body_plan->body_parts[inv->body_part_id])
                        fx_lunge_hand = part->flags.is_set(df::body_part_raw_flags::LEFT) ? 0 : 1;
            return;
        }
        if (!t)
            return;
        FxArc arc{};
        where(a, arc.ax, arc.ay, arc.ah);
        where(t, arc.bx, arc.by, arc.bh);
        arc.at_you = t == adv;
        arc.start = now;
        fx_arcs.push_back(arc);
        const float dx = arc.bx - arc.ax, dy = arc.by - arc.ay, len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.01f)
            fx_nudges[a->id] = FxNudge{dx / len, dy / len, now};
        if (t == adv)
            fx_arrows.push_back(FxArrow{arc.ax, arc.ay, s.target_wounds, false, now});
    };
    for (df::unit *u : world->units.active) {
        if (!u || std::abs(u->pos.z - adv->pos.z) > 2 || std::abs(u->pos.x - adv->pos.x) > 40 ||
            std::abs(u->pos.y - adv->pos.y) > 40)
            continue;
        for (df::unit_action *a : u->actions) {
            if (!a || a->type != df::unit_action_type::Attack)
                continue;
            const uint64_t key = (uint64_t(uint32_t(u->id)) << 32) | uint32_t(a->id);
            auto it = seen_attacks.find(key);
            if (it == seen_attacks.end()) {
                const df::unit *t = df::unit::find(a->data.attack.target_unit_id);
                it = seen_attacks
                         .emplace(key, SeenAttack{u->id, a->data.attack.target_unit_id, a->data.attack.attack_item_id,
                                                  t ? t->body.wound_next_id : 0, false, frame})
                         .first;
            }
            it->second.frame = frame;
            if (!it->second.fired && a->data.attack.timer1 <= 0) {
                fire(it->second);
                it->second.fired = true;
            }
        }
    }
    // an attack that is gone has been made
    for (auto it = seen_attacks.begin(); it != seen_attacks.end();) {
        if (it->second.frame != frame) {
            if (!it->second.fired)
                fire(it->second);
            it = seen_attacks.erase(it);
        } else {
            ++it;
        }
    }
    for (FxArrow &ar : fx_arrows)
        if (now - ar.start < 0.6 && adv->body.wound_next_id > ar.baseline)
            ar.red = true;

    // words over a creature, stacked over the ones it got a moment ago
    const auto say = [&](const df::unit *u, const std::string &text, uint32_t color) {
        FxText t{};
        t.unit_id = u->id;
        where(u, t.x, t.y, t.h);
        // just over the top of its head, where it was last drawn if it is not now
        const auto head = heads_seen.find(u->id);
        if (head != heads_seen.end() && frame - head->second.frame < 600) {
            t.x = head->second.x;
            t.y = head->second.y;
            t.h = head->second.top + 0.05f;
        } else {
            t.h += 0.75f;
        }
        t.you = u == adv;
        t.text = text;
        t.color = color;
        for (const FxText &o : fx_texts)
            if (o.unit_id == u->id && now - o.start < 0.5)
                ++t.stack;
        t.start = now;
        fx_texts.push_back(std::move(t));
    };
    // new wounds and things newly stuck in creatures nearby, from any attack, shot or throw
    for (df::unit *u : world->units.active) {
        if (!u || std::abs(u->pos.z - adv->pos.z) > 2 || std::abs(u->pos.x - adv->pos.x) > 40 ||
            std::abs(u->pos.y - adv->pos.y) > 40)
            continue;
        const auto seen = wounds_seen.find(u->id);
        if (seen != wounds_seen.end() && u->body.wound_next_id > seen->second)
            for (const df::unit_wound *w : u->body.wounds)
                if (w && w->id >= seen->second && !w->parts.empty() && w->parts[0])
                    say(u, part_name(u, w->parts[0]->body_part_id) + ": " + wound_word(w), fpv::rgb(255, 60, 50));
        wounds_seen[u->id] = u->body.wound_next_id;
        std::vector<int32_t> ids;
        for (const auto &[id, bp] : stuck_items(u)) {
            ids.push_back(id);
            const auto old = stuck_seen.find(u->id);
            if (old != stuck_seen.end() && std::find(old->second.begin(), old->second.end(), id) == old->second.end())
                if (df::item *item = df::item::find(id)) {
                    std::string what = Items::getDescription(item, 0, false);
                    if (!what.empty())
                        what[0] = char(std::toupper(uint8_t(what[0])));
                    say(u, part_name(u, bp) + ": stuck " + what, fpv::rgb(205, 90, 255));
                }
        }
        stuck_seen[u->id] = std::move(ids);
    }
    // a strike that left no new wound and nothing stuck missed
    for (auto it = fx_misses.begin(); it != fx_misses.end();) {
        if (now - it->start < 0.35) {
            ++it;
            continue;
        }
        // (a strike that killed may leave no new wound either)
        if (df::unit *t = df::unit::find(it->target)) {
            if (Units::isDead(t) || t->flags2.bits.killed)
                say(t, "DEAD!", fpv::rgb(255, 60, 50));
            else if (t->body.wound_next_id <= it->baseline && stuck_items(t).size() <= it->stuck)
                say(t, "MISS!", fpv::rgb(70, 230, 90));
        }
        it = fx_misses.erase(it);
    }
    std::erase_if(fx_texts, [&](const FxText &t) { return now - t.start > TEXT_TIME; });
    if (frame % 1024 == 0) {
        std::erase_if(wounds_seen, [](const auto &e) { return !df::unit::find(e.first); });
        std::erase_if(stuck_seen, [](const auto &e) { return !df::unit::find(e.first); });
        std::erase_if(heads_seen, [&](const auto &e) { return frame - e.second.frame > 600; });
    }
    std::erase_if(fx_arcs, [&](const FxArc &a) { return now - a.start > ARC_TIME; });
    std::erase_if(fx_arrows, [&](const FxArrow &a) { return now - a.start > ARROW_TIME; });
    std::erase_if(fx_nudges, [&](const auto &e) { return now - e.second.start > NUDGE_TIME; });
    // the attacker's picture lunges a third of a tile at its target and back
    for (fpv::Billboard &b : scene.units) {
        if (b.unit_id < 0)
            continue;
        const auto n = fx_nudges.find(b.unit_id);
        if (n == fx_nudges.end())
            continue;
        const float k = 0.35f * std::sin(3.14159265f * float((now - n->second.start) / NUDGE_TIME));
        b.x += n->second.dx * k;
        b.y += n->second.dy * k;
    }
}

#include "menu.inc"

uint32_t last_curtick = 0;   // the tick the game was last drawn for

// Ctrl-click: attack what the crosshair is on. The click is turned into the
// game's own "what do you want to do" list for that tile, and the attack (or
// shot) is then picked from that list for the player, so that the game goes
// straight on to choosing the strike or aiming.
struct AttackDrive {
    int frames = 0;       // frames left to finish in
    int stage = 0;        // 0: pick from the list, 1: confirm if asked
    int32_t unit_id = -1;
    int intent = 0;       // what the click was for (strike, shoot, throw, wrestle)
    df::coord target;     // the tile that was clicked
    int waited = 0;       // frames spent waiting for the player to pick what to throw
    int px = -1, py = -1; // where in the window the click was
    bool driving = false; // we are feeding the game ourselves right now
} attack;

// A Ctrl-click is ours from the press to the release of the button: the game
// must not see the button still held or let go, or it does what a plain click
// there does (with an arrow on the string: shoot at once).
int swallow_left = 0;       // frames left in which the rest of that click is kept from the game

// the game moved its map window during the frame's first draw
bool first_draw_moved = false;

// a cheap fingerprint of what the game drew into the map viewport
uint64_t viewport_print(const df::graphic_viewportst *vp) {
    uint64_t hash = 1469598103934665603ull;
    if (!vp || !vp->screentexpos_background)
        return 0;
    const size_t n = size_t(std::max(0, vp->dim_x)) * size_t(std::max(0, vp->dim_y));
    const uint32_t blank = uint32_t(gps->black_background_texpos[0]);
    bool any = false;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t a = uint32_t(vp->screentexpos_background[i]);
        const uint32_t b = vp->screentexpos_background_two ? uint32_t(vp->screentexpos_background_two[i]) : 0;
        const uint32_t c = vp->screentexpos_building_one ? uint32_t(vp->screentexpos_building_one[i]) : 0;
        any = any || (a && a != blank) || b || c;
        hash = (hash ^ a) * 1099511628211ull;
        hash = (hash ^ b) * 1099511628211ull;
        hash = (hash ^ c) * 1099511628211ull;
    }
    return any ? hash : 0;   // 0: nothing drawn at all
}

void swallow_rest_of_click() {
    if (swallow_left <= 0 || !enabler)
        return;
    const bool lifted = enabler->mouse_lbut_lift != 0;
    enabler->mouse_lbut = 0;
    enabler->mouse_lbut_down = 0;
    enabler->mouse_lbut_lift = 0;
    if (lifted)
        swallow_left = 0;
}

struct dungeon_hook : df::viewscreen_dungeonmodest {
    typedef df::viewscreen_dungeonmodest interpose_base;

    // The game works out what a click hit from the tiles the top-down map
    // holds, and we keep that map empty. So for the moment of a click the map
    // is drawn for the (shifted) window the click is aimed through, and
    // emptied again afterwards.
    struct MapForClick {
        dungeon_hook *screen;
        bool drawn = false;
        MapForClick(dungeon_hook *screen_, PointerRedirect *redirect) : screen(screen_) {
            if (!redirect || redirect->state != PointerRedirect::SHIFTED || !showing || debug_noblank ||
                !(enabler->mouse_lbut || enabler->mouse_rbut))
                return;
            // without the pointer, so that no hover text is drawn
            const int32_t mx = gps->mouse_x, my = gps->mouse_y;
            const int32_t px = gps->precise_mouse_x, py = gps->precise_mouse_y;
            gps->mouse_x = gps->mouse_y = gps->precise_mouse_x = gps->precise_mouse_y = -1;
            (screen->*interpose_render.chain)(last_curtick);
            gps->mouse_x = mx;
            gps->mouse_y = my;
            gps->precise_mouse_x = px;
            gps->precise_mouse_y = py;
            drawn = true;
        }
        ~MapForClick() {
            if (!drawn || !gps->main_viewport)
                return;
            blank_viewport(gps->main_viewport);
            for (df::graphic_viewportst *lower : gps->lower_viewport)
                blank_viewport(lower);
            minimap_put(gps->main_viewport);
        }
    };

    // draw the game again without the pointer, so that no hover text appears
    void redraw_hidden() {
        const int32_t mx = gps->mouse_x, my = gps->mouse_y;
        const int32_t px = gps->precise_mouse_x, py = gps->precise_mouse_y;
        gps->mouse_x = gps->mouse_y = gps->precise_mouse_x = gps->precise_mouse_y = -1;
        (this->*interpose_render.chain)(last_curtick);
        gps->mouse_x = mx;
        gps->mouse_y = my;
        gps->precise_mouse_x = px;
        gps->precise_mouse_y = py;
    }

    // such a draw puts the top-down map back: empty it again
    static void tidy_map() {
        if (debug_noblank || !gps->main_viewport)
            return;
        blank_viewport(gps->main_viewport);
        for (df::graphic_viewportst *lower : gps->lower_viewport)
            blank_viewport(lower);
        minimap_put(gps->main_viewport);
    }

    // a left click on an interface cell, handed straight to the game
    void click_cell(int cx, int cy) {
        const auto r = static_cast<df::renderer_2d_base *>(enabler->renderer);
        if (!r)
            return;
        const int32_t mx = gps->mouse_x, my = gps->mouse_y;
        const int32_t ppx = gps->precise_mouse_x, ppy = gps->precise_mouse_y;
        gps->mouse_x = cx;
        gps->mouse_y = cy;
        gps->precise_mouse_x = r->origin_x + cx * r->dispx_z + r->dispx_z / 2;
        gps->precise_mouse_y = r->origin_y + cy * r->dispy_z + r->dispy_z / 2;
        const int8_t was_down = enabler->mouse_lbut_down, was_tracking = enabler->tracking_on;
        enabler->tracking_on = 1;
        enabler->mouse_lbut = 1;
        enabler->mouse_lbut_down = 1;
        std::set<df::interface_key> none;
        const bool was_driving = attack.driving;
        attack.driving = true;
        INTERPOSE_NEXT(feed)(&none);
        attack.driving = was_driving;
        enabler->mouse_lbut = 0;
        enabler->mouse_lbut_down = was_down;
        enabler->tracking_on = was_tracking;
        gps->mouse_x = mx;
        gps->mouse_y = my;
        gps->precise_mouse_x = ppx;
        gps->precise_mouse_y = ppy;
    }

    // Input while our attack menu is up; true if it was dealt with here. What
    // is clicked in our menu is clicked in the game's own (unseen) list.
    bool menu_input(std::set<df::interface_key> *input) {
        const std::shared_ptr<const MenuModel> m = get_menu();
        if (!m || !menu_state.on || !game)
            return false;
        using namespace df::enums::interface_key;
        if (input && input->count(LEAVESCREEN))
            return false;       // the game closes its menu, and ours goes with it
        if (enabler->mouse_rbut)
            return false;
        if (!enabler->mouse_lbut)
            return true;        // keys would pick from the list that is not shown
        enabler->mouse_lbut = 0;
        int px, py;
        MenuLayout L;
        if (!pointer_pixels(px, py) || !layout_menu(*m, L))
            return true;
        const int hit = L.at(px, py);
        if (hit < 0)
            return true;
        const MenuItem &it = L.items[hit];
        if (it.kind == ITEM_CANCEL) {
            std::set<df::interface_key> keys{LEAVESCREEN};
            INTERPOSE_NEXT(feed)(&keys);
            return true;
        }
        // the menu drawn last frame may be a step behind the game
        bool ranged = false;
        const int kind = menu_kind_now(ranged);
        if (m->kind != kind || ranged != menu_state.ranged)
            return true;
        const bool option = it.kind == ITEM_OPTION;
        if (!option)
            menu_scroll() = it.index;       // bring its row onto the game's page
        redraw_hidden();
        std::vector<ScreenRow> rows;
        std::string title;
        int tx, ty;
        read_dialog(rows, title, tx, ty);
        const ScreenRow *row = nullptr;
        for (const ScreenRow &r : rows) {
            if (option) {
                if (is_option_row(r) && r.name.rfind(m->options[it.index].label + ":", 0) == 0)
                    row = &r;
            } else if (!is_option_row(r) && r.name == m->entries[it.index].name &&
                       r.d1 == m->entries[it.index].d1) {
                row = &r;
            }
            if (row)
                break;
        }
        if (row)
            click_cell(row->x + 3, row->y);
        mouse_log.push_back(std::string("menu: ") + (option ? m->options[it.index].label : m->entries[it.index].name) +
                            (row ? " clicked" : " not found on the game's page"));
        if (menu_kind_now(ranged) == kind && ranged == menu_state.ranged)
            menu_scroll() = 0;
        tidy_map();
        blank_dialog();
        return true;
    }

    // click an entry of the game's "what do you want to do" list; false if it is not on screen
    bool click_option(df::adventure_optionst *choice) {
        std::string name;
        choice->getName(&name);
        const std::string needle = name.substr(0, 28);
        for (int y = 0; y < gps->dimy && !needle.empty(); ++y) {
            std::string row(size_t(gps->dimx), ' ');
            for (int x = 0; x < gps->dimx; ++x) {
                const unsigned char ch = gps->screen[(x * gps->dimy + y) * 8];
                row[x] = ch >= 32 ? char(ch) : ' ';
            }
            const size_t at = row.find(needle);
            if (at != std::string::npos) {
                click_cell(int(at) + 1, y);
                mouse_log.push_back("attack: chose '" + name + "'");
                return true;
            }
        }
        return false;
    }

    // Shooting and throwing go through the game's own steps: its key, (for a
    // throw) the list of what to throw, the aiming cursor, and the question
    // what on that tile to aim at. The cursor and that question are answered
    // with what was clicked.
    void drive_ranged() {
        using namespace df::enums::interface_key;
        auto &aim = game->main_interface.adventure.aim_projectile;
        auto &list = game->main_interface.adventure.option_list;
        if (attack.stage == 10) {
            std::set<df::interface_key> keys{attack.intent == INTENT_SHOOT ? A_SHOOT : A_THROW};
            INTERPOSE_NEXT(feed)(&keys);
            attack.stage = 11;
            attack.waited = 0;
            attack.frames = 6000;   // the player may take a while to pick what to throw
        } else if (attack.stage == 11) {
            ++attack.waited;
            if (aim.open) {
                attack.stage = 12;
                attack.waited = 0;
                attack.frames = 120;
            } else if (attack.waited > 3 && !game->main_interface.adventure.inventory.open) {
                // nothing came of it: nothing to shoot with, or the list was closed
                if (attack.waited < 12)
                    flash(attack.intent == INTENT_SHOOT ? "Cannot shoot now" : "Nothing to throw");
                attack.frames = 0;
            }
        } else if (attack.stage == 12) {
            ++attack.waited;
            if (list.open && !list.option.empty()) {
                df::adventure_optionst *choice = nullptr, *tile = nullptr;
                for (df::adventure_optionst *option : list.option) {
                    if (!option)
                        continue;
                    auto shot = virtual_cast<df::adventure_movement_shoot_creaturest>(option);
                    auto thrown = virtual_cast<df::adventure_movement_throw_item_at_creaturest>(option);
                    if (!choice && attack.unit_id >= 0 &&
                        ((shot && shot->target == attack.unit_id) || (thrown && thrown->target == attack.unit_id)))
                        choice = option;
                    else if (!tile && (virtual_cast<df::adventure_movement_shoot_tilest>(option) ||
                                       virtual_cast<df::adventure_movement_throw_item_at_tilest>(option)))
                        tile = option;
                }
                // How many ways there are to do it. The game may offer several
                // for one target (ways of aiming a shot); that choice is the
                // player's, so the list is then left up for them.
                int creature_ways = 0, tile_ways = 0;
                for (df::adventure_optionst *option : list.option) {
                    if (!option)
                        continue;
                    auto shot = virtual_cast<df::adventure_movement_shoot_creaturest>(option);
                    auto thrown = virtual_cast<df::adventure_movement_throw_item_at_creaturest>(option);
                    if ((shot && shot->target == attack.unit_id) || (thrown && thrown->target == attack.unit_id))
                        ++creature_ways;
                    else if (virtual_cast<df::adventure_movement_shoot_tilest>(option) ||
                             virtual_cast<df::adventure_movement_throw_item_at_tilest>(option))
                        ++tile_ways;
                }
                // a creature that is not offered by name is hit by aiming at its tile
                if (!choice)
                    choice = tile;
                const int ways = choice == tile ? tile_ways : creature_ways;
                if (choice && ways == 1 && list.option.size() <= 2)
                    click_option(choice);
                else
                    mouse_log.push_back("attack: " + std::to_string(list.option.size()) +
                                        " ways to do it, left to the player");
                attack.frames = 0;
            } else if (!aim.open) {
                attack.frames = 0;      // gone off already
            } else {
                // The game only offers a creature (and how to aim at it) when
                // the creature is clicked; moving the aiming cursor onto it
                // and pressing Enter offers just the ground it stands on. So
                // the click is made again, now that the game is aiming. It is
                // only taken once the aiming screen has settled, so it is
                // repeated a few times.
                if (attack.waited == 3 || attack.waited == 25 || attack.waited == 60) {
                    // where the target is in the view now: at the pixel that
                    // was clicked, or close by if things have shifted a little
                    const auto on_target = [&](int x, int y) {
                        const fpv::Pick now = pick_pixels(x, y);
                        if (!now.valid() || over_interface(x, y))
                            return false;
                        return attack.unit_id >= 0 ? now.unit_id == attack.unit_id
                                                   : (now.x == attack.target.x && now.y == attack.target.y &&
                                                      now.z == attack.target.z);
                    };
                    bool same = false;
                    for (int ring = 0; ring <= 12 && !same && attack.px >= 0; ++ring)
                        for (int dy = -ring; dy <= ring && !same; ++dy)
                            for (int dx = -ring; dx <= ring && !same; ++dx) {
                                if (std::max(std::abs(dx), std::abs(dy)) != ring)
                                    continue;
                                if (on_target(attack.px + dx * 8, attack.py + dy * 8)) {
                                    attack.px += dx * 8;
                                    attack.py += dy * 8;
                                    same = true;
                                }
                            }
                    if (same) {
                        const int32_t dbx = debug_px.load(), dby = debug_py.load();
                        debug_px = attack.px;
                        debug_py = attack.py;
                        const int8_t was_down = enabler->mouse_lbut_down, was_tracking = enabler->tracking_on;
                        enabler->tracking_on = 1;
                        enabler->mouse_lbut = 1;
                        enabler->mouse_lbut_down = 1;
                        std::set<df::interface_key> none;
                        this->feed(&none);      // through our own hook, which aims the click through the view
                        enabler->mouse_lbut = 0;
                        enabler->mouse_lbut_down = was_down;
                        enabler->tracking_on = was_tracking;
                        debug_px = dbx;
                        debug_py = dby;
                        mouse_log.push_back("attack: clicked the target");
                    } else {
                        // the view has moved on: aim at the place instead
                        df::coord pos = attack.target;
                        if (df::unit *unit = df::unit::find(attack.unit_id))
                            pos = unit->pos;
                        aim.cursor = pos;
                        std::set<df::interface_key> keys{SELECT};
                        INTERPOSE_NEXT(feed)(&keys);
                        mouse_log.push_back("attack: aimed at the place");
                    }
                }
            }
        }
    }

    void drive_attack() {
        if (attack.frames <= 0 || !game || !adventure)
            return;
        --attack.frames;
        using namespace df::enums::interface_key;
        using Mode = df::adventure_interface_attack_mode_type;
        attack.driving = true;
        auto &prompt = game->main_interface.adventure.attack;
        if (attack.stage >= 10) {
            drive_ranged();
        } else if (adventure->menu == df::ui_advmode_menu::AttackConfirm ||
                   (attack.stage == 1 && prompt.open && prompt.mode == Mode::CONFIRM)) {
            // "Really start a conflict with ...?": yes, that is what Ctrl-click means
            std::set<df::interface_key> keys{A_ATTACK_CONFIRM};
            INTERPOSE_NEXT(feed)(&keys);
            mouse_log.push_back("attack: confirmed");
            attack.frames = std::max(attack.frames, 30);
        } else if (attack.stage == 1 && prompt.open && prompt.mode == Mode::MOVE_CHOICE) {
            // strike or wrestle: the intent has decided that already
            const bool wrestle = attack.intent == INTENT_WRESTLE;
            const auto wanted = wrestle ? df::attack_move_choice_type::WRESTLE : df::attack_move_choice_type::STRIKE;
            if (std::find(prompt.move_choice.begin(), prompt.move_choice.end(), wanted) == prompt.move_choice.end()) {
                flash(wrestle ? "Cannot wrestle now" : "Cannot strike now");
                mouse_log.push_back("attack: the wanted move is not offered");
            } else {
                std::vector<ScreenRow> rows;
                std::string title;
                int tx, ty;
                read_dialog(rows, title, tx, ty);
                const std::string word = wrestle ? "Wrestle" : "Strike";
                for (const ScreenRow &r : rows)
                    if (r.name.rfind(word, 0) == 0) {
                        click_cell(r.x + 3, r.y);
                        mouse_log.push_back("attack: chose " + word);
                        break;
                    }
            }
            attack.frames = 0;
        } else if (attack.stage == 1 && prompt.open && prompt.mode != Mode::UNIT_CHOICE) {
            attack.frames = 0;      // already further on
        } else if (attack.stage == 0 && game->main_interface.adventure.option_list.open &&
                   !game->main_interface.adventure.option_list.option.empty()) {
            auto &list = game->main_interface.adventure.option_list;
            df::adventure_optionst *choice = nullptr, *fallback = nullptr;
            for (df::adventure_optionst *option : list.option) {
                if (!option)
                    continue;
                if (auto menu = virtual_cast<df::adventure_movement_combat_menu_creaturest>(option)) {
                    if (menu->target == attack.unit_id)
                        choice = option;
                } else if (auto melee = virtual_cast<df::adventure_movement_attack_creaturest>(option)) {
                    if (!fallback && std::find(melee->targets.begin(), melee->targets.end(), attack.unit_id) !=
                                         melee->targets.end())
                        fallback = option;
                }
            }
            if (!choice)
                choice = fallback;
            if (!choice) {
                // too far away: put the list away again
                std::set<df::interface_key> keys{LEAVESCREEN};
                INTERPOSE_NEXT(feed)(&keys);
                flash(attack.intent == INTENT_WRESTLE ? "Too far to wrestle" : "Too far to strike");
                mouse_log.push_back("attack: nothing fitting among " + std::to_string(list.option.size()) + " options");
                attack.frames = 0;
            } else if (click_option(choice)) {
                attack.stage = 1;
                attack.frames = 150;   // the question, if any, comes when it is our turn
            }
        }
        attack.driving = false;
        if (attack.frames == 1 && attack.stage < 10)
            mouse_log.push_back("attack: gave up at stage " + std::to_string(attack.stage) + " menu " +
                                std::to_string(int(adventure->menu)));
        attack_busy = attack.frames > 0;
    }

    void capture(uint32_t curtick) {
        df::unit *adv = World::getAdventurer();
        clear_hint();
        unmove_action_bar();
        if (!adv || !default_adventure_ui() || !gps || !gps->main_viewport) {
            publish_state(false, false);
            look_ok = false;
            portrait_ours = false;
            tip_none();
            // menus that need the map (look, aim, travel ...) get the normal view back
            if (showing || zoom_saved) {
                restore_zoom();
                ++gps->force_full_display_count;
            }
            mini_n = 0;
            showing = false;
            corner_live = false;
            full_capture = true;
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        zoom_for_capture();
        ++capture_frame;
        load_ui_font();

        {
            std::lock_guard<std::mutex> lock(scene.mutex);
            const int old_w = scene.w, old_d = scene.d;
            if (!scene.resize()) {
                showing = false;
                return;
            }
            // Walking far makes the game move its window on the world: every
            // local coordinate then means another place, and what we remember
            // would stand around as ghosts.
            static int region_x = -1, region_y = -1, region_z = -1;
            if (world->map.region_x != region_x || world->map.region_y != region_y ||
                world->map.region_z != region_z) {
                if (region_x != -1)
                    scene.reset();
                scene.resize();
                region_x = world->map.region_x;
                region_y = world->map.region_y;
                region_z = world->map.region_z;
                full_capture = true;
                pre_x = -100000;
                window_content.clear();
            }
            if (scene.w != old_w || scene.d != old_d)
                full_capture = true;
        }

        int ax = adv->pos.x, ay = adv->pos.y, az = adv->pos.z;
        if (free_camera) {
            ax = std::clamp(free_x.load(), 0, scene.w - 1);
            ay = std::clamp(free_y.load(), 0, scene.h - 1);
            az = std::clamp(free_z.load(), 0, scene.d - 1);
        }
        const int zlo = std::max(0, az - settings.levels_down.load());
        const int zhi = std::min(scene.d - 1, az + settings.levels_up.load());
        if (az != last_cam_z) {
            full_capture = true;
            last_cam_z = az;
        }

        // The game draws one z-level and one window of the map at a time. To
        // see more, point it at other levels and at neighbouring windows, the
        // way scrolling the map does, and read each one.
        const int shown_x = *window_x, shown_y = *window_y, shown_z = *window_z;
        const int cols = gps->main_viewport->dim_x, rows = gps->main_viewport->dim_y;
        // Every read must be of the window and level it is filed under. If the
        // game moved the window while drawing (it follows the adventurer) or
        // did not draw anew, the sprites are for some other place: not read.
        constexpr int NOWHERE = std::numeric_limits<int>::min();
        int on_x = shown_x, on_y = shown_y, on_z = shown_z;
        if (first_draw_moved)
            on_x = on_y = on_z = NOWHERE;   // draw the shown window again before reading it
        const auto read = [&](int z, int wx, int wy) {
            if (z != on_z || wx != on_x || wy != on_y) {
                *window_x = wx;
                *window_y = wy;
                *window_z = z;
                if (frame_pointer.hidden)
                    frame_pointer.hide();
                const uint64_t before = viewport_print(gps->main_viewport);
                INTERPOSE_NEXT(render)(curtick);
                const bool moved = *window_x != wx || *window_y != wy || *window_z != z;
                const uint64_t after = viewport_print(gps->main_viewport);
                if (moved || (after != 0 && after == before)) {
                    on_x = on_y = on_z = NOWHERE;
                    ++stat_reads_skipped;
                    return;
                }
                on_x = wx;
                on_y = wy;
                on_z = z;
            }
            std::lock_guard<std::mutex> lock(scene.mutex);
            scene.readLevel(z);
        };

        // what is on screen is read every frame: it has the creatures near us
        read(az, shown_x, shown_y);
        mini_taken = 0;
        if (settings.minimap && settings.minimap_ascii)
            minimap_ascii_build(adv);
        else if (on_z == adv->pos.z)
            minimap_take(gps->main_viewport, adv->pos.x - shown_x, adv->pos.y - shown_y);
        if (capture_frame % 1024 == 0)
            window_content.clear();

        // The shape of everything within sight comes straight from the map, a
        // few levels per frame, nearest first. It only has to be done again
        // after moving some way.
        {
            if (std::abs(ax - pre_x) > 6 || std::abs(ay - pre_y) > 6 || az != pre_z) {
                pre_x = ax;
                pre_y = ay;
                pre_z = az;
                pre_next = 0;
            }
            const int reach = int(settings.view_distance.load()) + 8;
            const int rect[4] = {pre_x - reach, pre_y - reach, 2 * reach + 1, 2 * reach + 1};
            const int last = 2 * std::max(az - zlo, zhi - az);
            for (int n = 0; n < 2 && pre_next <= last; ++pre_next) {
                const int z = az + ((pre_next + 1) / 2) * ((pre_next % 2) ? 1 : -1);
                if (z < zlo || z > zhi)
                    continue;
                std::lock_guard<std::mutex> lock(scene.mutex);
                scene.readLevel(z, rect);
                ++n;
            }
        }

        if (full_capture) {
            // right after switching on or changing level: the levels next to ours
            // at once, the rest quickly over the following frames
            for (int z = std::max(zlo, az - 1); z <= std::min(zhi, az + 1); ++z)
                if (z != az)
                    read(z, shown_x, shown_y);
            full_capture = false;
            warmup = 60;
        }
        // the rest takes turns: a few windows per frame at first, then one
        const int reach = std::clamp(settings.range.load(), 1, 4) - 1;
        const int side = 2 * reach + 1;
        const int nz = zhi - zlo + 1;
        const int total = side * side * nz;
        int budget = warmup > 0 ? 3 : 1;
        if (warmup > 0)
            --warmup;
        for (int tries = 0; tries < total && budget > 0; ++tries) {
            rr_index = (rr_index + 1) % total;
            const int z = zlo + rr_index % nz;
            const int ox = (rr_index / nz) % side - reach, oy = (rr_index / nz) / side - reach;
            if (z == az && ox == 0 && oy == 0)
                continue;
            const int wx = shown_x + ox * cols, wy = shown_y + oy * rows;
            if (wx + cols <= 0 || wy + rows <= 0 || wx >= scene.w || wy >= scene.h)
                continue;   // entirely off the map
            if (!window_has_content(z, wx, wy, cols, rows))
                continue;   // nothing but air up there
            read(z, wx, wy);
            --budget;
        }
        *window_x = shown_x;
        *window_y = shown_y;
        *window_z = shown_z;

        bool snap = false;
        if (last_ax >= 0) {
            const int dx = ax - last_ax, dy = ay - last_ay;
            if (std::abs(dx) > 1 || std::abs(dy) > 1 || std::abs(az - last_az) > 1) {
                snap = true;
            } else if (!settings.relative_controls && (dx || dy)) {
                for (int i = 0; i < 8; ++i)
                    if (DIR_DX[i] == dx && DIR_DY[i] == dy)
                        heading = i;
            }
        } else {
            snap = true;
        }
        last_ax = ax;
        last_ay = ay;
        last_az = az;

        float eye;
        {
            std::lock_guard<std::mutex> lock(scene.mutex);
            // while attacking or aiming, creatures are outlined by how an attack on them would go
            const bool outlines = ctrl_held || attack_busy || adventure->menu != df::ui_advmode_menu::Default ||
                                  (game && game->main_interface.adventure.aim_projectile.open);
            // aiming a strike or a hold: only what is within reach is outlined
            const bool aiming_shot = game && game->main_interface.adventure.aim_projectile.open;
            const int now_intent = effective_intent();
            const bool melee = !aiming_shot && (now_intent == INTENT_STRIKE || now_intent == INTENT_WRESTLE);
            scene.buildUnits(zlo, zhi, adv, capture_frame, outlines, melee, settings.smooth);
            track_combat(adv, capture_frame);
            // lying on the ground, you see the world from down there
            const bool prone = !free_camera && adv->flags1.bits.on_ground;
            eye = (az + scene.groundRaise(ax, ay, az)) * LEVEL_H + (prone ? EYE_PRONE : EYE);
        }

        // distance haze: pale by day under the sky, dark at night and underground
        const df::tile_designation *des = Maps::getTileDesignation(ax, ay, az);
        const float light = daylight_now();
        const float haze = (des && des->bits.subterranean) ? 0.0f : light;
        const uint32_t fog = fpv::rgb(int(8 + 136 * haze), int(9 + 165 * haze), int(12 + 190 * haze));

        {
            std::lock_guard<std::mutex> lock(target_mutex);
            target.x = ax + 0.5f;
            target.y = ay + 0.5f;
            target.eye = eye;
            target.zlo = zlo;
            target.zhi = zhi;
            target.daylight = light;
            target.fog = fog;
            target.valid = true;
            if (snap)
                ++target.snap;
        }
        showing = true;
        publish_state(true, settings.minimap);
        publish_bar();
        look_ok = plain_adventure_focus() && game &&
                  !game->main_interface.adventure.option_list.open;
        can_shoot = update_held(adv, capture_frame % 16 == 1);
        if (capture_frame % 16 == 1)
            update_body(adv);
        portrait_ours = body_shown.load();

        // The pointer is hidden from the game while it is over the view, so the
        // game shows no hover text of its own (it would describe the hidden
        // top-down map). We name exactly what is highlighted instead.
        std::vector<std::string> hint;
        if (frame_pointer.over_view && frame_pointer.pick.valid()) {
            const std::string name = describe(frame_pointer.pick);
            if (!name.empty())
                hint.push_back(name);
        }
        hover_lines = hint;

        // The game's own hover box says everything about a tile (all the
        // items on it, for one), so it is kept, and made to describe the tile
        // the pointer is on in the 3D view. The game works that tile out from
        // where the pointer is on its top-down map; for the last draw of the
        // frame that map is slid so that the tile we look at lies under the
        // pointer. The draws before it, of other levels, may have left a box
        // for some other tile (the game never wipes the interface grid over
        // the map), so on the plain adventure screen the grid is emptied
        // first; that last draw puts everything else back.
        const bool plain = plain_adventure_focus();
        if ((frame_pointer.hidden || plain) && !debug_noblank) {
            if (plain) {
                const size_t cells = size_t(gps->dimx) * gps->dimy;
                std::memset(gps->screen, 0, cells * 8);
                std::memset(gps->screentexpos, 0, cells * sizeof(*gps->screentexpos));
                std::memset(gps->screentexpos_lower, 0, cells * sizeof(*gps->screentexpos_lower));
                std::memset(gps->screentexpos_anchored, 0, cells * sizeof(*gps->screentexpos_anchored));
                std::memset(gps->screentexpos_flag, 0, cells * sizeof(*gps->screentexpos_flag));
            }
            const int tile_px = gps->viewport_zoom_factor / 4;
            const fpv::Pick &p = frame_pointer.pick;
            const bool aimed = frame_pointer.hidden && frame_pointer.over_view && p.valid() && tile_px > 0 &&
                               settings.game_tips;
            if (aimed) {
                // the pointer in the middle of its tile: where exactly the
                // game's main thread puts it meanwhile then makes no difference
                const int px = frame_pointer.px / tile_px * tile_px + tile_px / 2;
                const int py = frame_pointer.py / tile_px * tile_px + tile_px / 2;
                *window_x = p.x - frame_pointer.px / tile_px;
                *window_y = p.y - frame_pointer.py / tile_px;
                *window_z = p.z;
                int cx = 0, cy = 0;
                interface_cell(px, py, cx, cy);
                gps->mouse_x = cx;
                gps->mouse_y = cy;
                gps->precise_mouse_x = px;
                gps->precise_mouse_y = py;
            } else {
                // nothing under the pointer: look at a place off the map
                *window_x = scene.w + 64;
                *window_y = scene.h + 64;
                if (frame_pointer.hidden)
                    frame_pointer.hide();
                frame_pointer.bar_again();
            }
            INTERPOSE_NEXT(render)(curtick);
            if (aimed && plain && !tip_base.empty()) {
                int mcx = 0, mcy = 0;
                interface_cell(frame_pointer.px, frame_pointer.py, mcx, mcy);
                tip_measure(mcx, mcy);
            }
            else
                tip_none();
            if (frame_pointer.hidden)
                frame_pointer.hide();
            *window_x = shown_x;
            *window_y = shown_y;
            *window_z = shown_z;
        } else {
            tip_none();
        }

        // nothing of the top-down map needs to reach the screen, except the
        // square the minimap shows
        if (debug_noblank) {
            mini_n = 0;
        } else {
            blank_viewport(gps->main_viewport);
            for (df::graphic_viewportst *lower : gps->lower_viewport)
                blank_viewport(lower);
            minimap_put(gps->main_viewport);
        }
        hide_interface_under_minimap();
        hide_portrait();
        hide_corner_buttons(plain && !debug_noblank);
        move_bottom_buttons(plain && !debug_noblank);

        tidy_top(plain && !debug_noblank);
        update_messages();
        paint_hint(hint, frame_pointer.px, frame_pointer.py);

        if (settings.compass && !compass_picture) {
            const int h = heading;
            const string text = string(" ") + DIR_NAMES[(h + 6) % 8] + "  " + DIR_NAMES[(h + 7) % 8] + " [" +
                                DIR_NAMES[h] + "] " + DIR_NAMES[(h + 1) % 8] + "  " + DIR_NAMES[(h + 2) % 8] + " ";
            const int x = (gps->dimx - int(text.size())) / 2;
            Screen::paintString(Screen::Pen(' ', COLOR_WHITE, COLOR_BLACK), x, 0, text);
        }

        stat_capture_us = uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count());
    }

    // The timer button: time passes by itself, settings.realtime_rate game
    // ticks a real second. Each step is a wait ('.'), which the game counts
    // down in adventure->wait_timer, one a tick, before the adventurer may act
    // again; the wait is cut to the ticks owed (at most the 10 of a whole
    // one), and the next one starts only when the adventurer is back in
    // control. Anything the player should see to (a menu, a fight, a message
    // box) stops it.
    void realtime_step() {
        static int64_t last_ms = 0;
        static float owed = 0;
        static int pending = 0;      // ticks for a wait the game took but has not started counting yet
        static size_t seen_reports = 0;
        static size_t seen_log[3] = {0, 0, 0};
        static bool was_on = false;
        df::unit *adv = World::getAdventurer();
        if (realtime && (!showing || !adv || !world || attack.driving || attack.frames > 0 ||
                         !plain_adventure_focus() || (game && game->main_interface.adventure.option_list.open)))
            realtime = false;
        if (!realtime) {
            was_on = false;
            return;
        }
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto &reports = world->status.reports;
        if (!was_on) {
            // just switched on: the first wait at once, and only news from now on counts
            was_on = true;
            last_ms = now;
            owed = 1;
            seen_reports = reports.size();
            for (int i = 0; i < 3; ++i)
                seen_log[i] = adv->reports.log[i].size();
        }
        bool alert = !world->status.popups.empty();
        // the adventurer fought, sparred or hunted
        for (int i = 0; i < 3; ++i) {
            if (adv->reports.log[i].size() > seen_log[i])
                alert = true;
            seen_log[i] = adv->reports.log[i].size();
        }
        // a message the game would pause for or put in a box
        const df::d_init *init_d = df::global::d_init;
        if (reports.size() < seen_reports)
            seen_reports = reports.size();
        for (size_t i = seen_reports; i < reports.size() && init_d; ++i) {
            const df::report *r = reports[i];
            const int type = r ? int(r->type) : -1;
            const int types = int(sizeof(init_d->announcements.flags) / sizeof(init_d->announcements.flags[0]));
            if (type >= 0 && type < types) {
                const df::announcement_flags f = init_d->announcements.flags[type];
                if (f.bits.PAUSE || f.bits.DO_MEGA)
                    alert = true;
            }
        }
        seen_reports = reports.size();
        if (alert) {
            realtime = false;
            was_on = false;
            return;
        }
        const int rate = std::clamp(settings.realtime_rate.load(), 1, 99);
        // what cannot be done at once (the game is slower) is not made up for later
        owed = std::min(owed + float(rate) * float(now - last_ms) / 1000.0f, std::max(1.0f, float(rate)));
        last_ms = now;
        using Loop = df::adventure_game_loop_type;
        const bool in_control = adventure->player_control_state == Loop::TAKING_INPUT;
        if (pending > 0 && adventure->wait_timer > 0) {
            adventure->wait_timer = std::min(adventure->wait_timer, pending);
            pending = 0;
        }
        if (in_control)
            pending = 0;
        if (owed >= 1 && in_control && adventure->wait_timer <= 0) {
            const int ticks = std::min(int(owed), 10);
            owed -= float(ticks);
            std::set<df::interface_key> keys{df::interface_key::A_WAIT};
            INTERPOSE_NEXT(feed)(&keys);
            if (adventure->wait_timer > 0)
                adventure->wait_timer = std::min(adventure->wait_timer, ticks);
            else if (adventure->player_control_state != Loop::TAKING_INPUT)
                pending = ticks;
        }
    }

    DEFINE_VMETHOD_INTERPOSE(void, render, (uint32_t curtick)) {
        publish_wheel();
        if (!active) {
            realtime = false;
            corner_live = false;
            showing = false;
            clear_hint();
            unmove_action_bar();
            tip_none();
            publish_state(false, false);
            look_ok = false;
            close_menu();
            INTERPOSE_NEXT(render)(curtick);
            return;
        }
        MouseTrace trace("render");
        last_curtick = curtick;
        last_capture_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (swallow_left > 0)
            --swallow_left;
        if (showing)
            dismiss_announcements();
        frame_pointer.begin();
        const int32_t drew_x = *window_x, drew_y = *window_y, drew_z = *window_z;
        // drawn on an empty grid with the pointer hidden: the interface without a hover box
        const bool tip_base_now = showing && frame_pointer.hidden && !debug_noblank && plain_adventure_focus();
        if (tip_base_now)
            wipe_grid();
        INTERPOSE_NEXT(render)(curtick);
        if (tip_base_now)
            tip_baseline();
        else
            tip_base.clear();
        // the game may follow the adventurer while it draws: then what it drew
        // need not be for the window it ends up with
        first_draw_moved = *window_x != drew_x || *window_y != drew_y || *window_z != drew_z;
        capture(curtick);
        frame_pointer.end();
        update_menu([this] { redraw_hidden(); }, [] { tidy_map(); });
        drive_attack();
        {
            // Space while time runs by itself stops it, and that press does nothing else
            if (space_down && realtime) {
                realtime = false;
                space_stopped = true;
            } else if (!space_down) {
                space_stopped = false;
            }
            // Shift+Space: wait, once per press
            static bool was = false;
            const bool now = space_down && shift_held;
            if (now && !was && !space_stopped && showing && !attack.driving && attack.frames <= 0 &&
                plain_adventure_focus()) {
                std::set<df::interface_key> keys{df::interface_key::A_WAIT};
                INTERPOSE_NEXT(feed)(&keys);
            }
            was = now;
        }
        realtime_step();
    }

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
        if (!attack.driving)
            swallow_rest_of_click();
        MouseTrace trace("logic");
        // The game works out what its hover box says here, from where the
        // pointer is on its top-down map. So for this step that map is slid to
        // put the tile the pointer is on in the 3D view under the pointer
        // (nothing under it: the pointer is hidden from the game).
        std::unique_ptr<PointerRedirect> aim_tip;
        int px = -1, py = -1;
        int bar_px, bar_py;
        const bool on_corner = pointer_pixels(px, py) && (in_corner(px, py) || bar_source(px, py, bar_px, bar_py));
        if (active && showing && (settings.game_tips || on_corner) && !debug_noblank && !attack.driving)
            aim_tip = std::make_unique<PointerRedirect>();
        INTERPOSE_NEXT(logic)();
    }

    DEFINE_VMETHOD_INTERPOSE(void, feed, (std::set<df::interface_key> *input)) {
        if (!attack.driving)
            swallow_rest_of_click();
        if (active && showing && !attack.driving) {
            // Ctrl+R switches the intent; the game does not get that key
            if (swallow_keys && !mouse_buttons_active())
                return;
            // a click on the message panel is the panel's; the game must not take it for one on the map
            if (enabler->mouse_lbut || enabler->mouse_rbut) {
                int px, py;
                if (pointer_pixels(px, py) && in_messages(px, py)) {
                    if (enabler->mouse_lbut)
                        messages_click(px, py);
                    enabler->mouse_lbut = 0;
                    enabler->mouse_rbut = 0;
                    return;
                }
            }
            if (menu_input(input))
                return;
            // Space while time runs by itself only stops it (the waits sent
            // for the timer go past this hook, so they never count as Space)
            if (input && input->count(df::interface_key::STRING_A032) && !mouse_buttons_active() &&
                (realtime || space_stopped)) {
                realtime = false;
                space_stopped = true;
                return;
            }
            // Space lets a moment pass, like "," does. Shift+Space, a longer
            // one like ".", is sent from the frame hook: what key the game
            // makes of Shift+Space is not something to rely on.
            if (input && !mouse_buttons_active() && plain_adventure_focus()) {
                if (shift_held && space_down)
                    return;
                if (input->count(df::interface_key::STRING_A032)) {
                    std::set<df::interface_key> out{df::interface_key::A_SHORT_WAIT};
                    INTERPOSE_NEXT(feed)(&out);
                    return;
                }
            }
        }
        // clicks act on what the pointer is on in the 3D view
        MouseTrace trace("feed");
        std::unique_ptr<PointerRedirect> redirect;
        // where the pointer is before the click is aimed through the view
        int click_px = -1, click_py = -1;
        if (!pointer_pixels(click_px, click_py))
            click_px = click_py = -1;
        if (mouse_buttons_active()) {
            redirect = std::make_unique<PointerRedirect>();
        }
        MapForClick map_for_click(this, redirect.get());
        if (redirect) {
            // Ctrl-click on a creature: attack it
            const int click_intent = effective_intent();
            const bool at_range = click_intent == INTENT_SHOOT || click_intent == INTENT_THROW;
            if (ctrl_held && !attack.driving && enabler->mouse_lbut && redirect->state == PointerRedirect::SHIFTED &&
                redirect->picked.valid() && default_adventure_ui() && game &&
                !game->main_interface.adventure.aim_projectile.open && !game->main_interface.adventure.attack.open) {
                attack.unit_id = redirect->picked.unit_id;
                attack.intent = click_intent;
                attack.target = df::coord(int16_t(redirect->picked.x), int16_t(redirect->picked.y),
                                          int16_t(redirect->picked.z));
                attack.px = click_px;
                attack.py = click_py;
                if (at_range) {
                    // Ctrl-click with shoot or throw: at a creature or at a place
                    enabler->mouse_lbut = 0;
                    enabler->mouse_lbut_down = 0;
                    attack.stage = 10;
                    attack.frames = 2;
                    attack_busy = true;
                    trace.after();
                    return;
                }
                if (redirect->picked.unit_id >= 0) {
                    // Ctrl-click on a creature with strike or wrestle: the game's list for it, then its attack
                    enabler->mouse_lbut = 0;
                    enabler->mouse_rbut = 1;
                    INTERPOSE_NEXT(feed)(input);
                    enabler->mouse_rbut = 0;
                    enabler->mouse_lbut_down = 0;
                    if (game->main_interface.adventure.option_list.open) {
                        attack.stage = 0;
                        attack.frames = 30;
                    }
                    trace.after();
                    return;
                }
            }
            if (trace.on)
                trace.note = "redirect " + std::to_string(int(redirect->state)) + " pick " +
                             std::to_string(redirect->picked.x) + "," + std::to_string(redirect->picked.y) + "," +
                             std::to_string(redirect->picked.z) + " keys " +
                             std::to_string(input ? input->size() : 0) + " focus " +
                             (Gui::getCurFocus(true).empty() ? "" : Gui::getCurFocus(true)[0]);
        }
        if (active && showing && settings.relative_controls && input && default_adventure_ui() &&
            plain_adventure_focus()) {
            using namespace df::enums::interface_key;
            static const df::interface_key flat[8] = {A_MOVE_N, A_MOVE_NE, A_MOVE_E, A_MOVE_SE,
                                                      A_MOVE_S, A_MOVE_SW, A_MOVE_W, A_MOVE_NW};
            static const df::interface_key up[8] = {A_MOVE_N_UP, A_MOVE_NE_UP, A_MOVE_E_UP, A_MOVE_SE_UP,
                                                    A_MOVE_S_UP, A_MOVE_SW_UP, A_MOVE_W_UP, A_MOVE_NW_UP};
            static const df::interface_key down[8] = {A_MOVE_N_DOWN, A_MOVE_NE_DOWN, A_MOVE_E_DOWN, A_MOVE_SE_DOWN,
                                                      A_MOVE_S_DOWN, A_MOVE_SW_DOWN, A_MOVE_W_DOWN, A_MOVE_NW_DOWN};
            const int look = heading;
            if (settings.wasd && !attack.driving) {
                // w a s d walk and strafe; what those keys normally do moves to
                // the arrow keys (up for w, left for a, down for s, right for d)
                static const df::interface_key letters[4] = {STRING_A119, STRING_A097, STRING_A115, STRING_A100};
                static const int steps[4] = {0, 6, 4, 2};
                static const df::interface_key arrows[4] = {A_MOVE_N, A_MOVE_W, A_MOVE_S, A_MOVE_E};
                static std::set<df::interface_key> usual[4];
                static bool known[4] = {false, false, false, false};
                for (int i = 0; i < 4; ++i)
                    if (input->count(letters[i])) {
                        if (!known[i]) {
                            usual[i] = *input;
                            usual[i].erase(letters[i]);
                            known[i] = true;
                        }
                        std::set<df::interface_key> out{flat[(look + steps[i]) % 8]};
                        INTERPOSE_NEXT(feed)(&out);
                        return;
                    }
                for (int i = 0; i < 4; ++i)
                    if (input->count(arrows[i]) && known[i] && !usual[i].empty()) {
                        std::set<df::interface_key> out = usual[i];
                        INTERPOSE_NEXT(feed)(&out);
                        return;
                    }
            }
            // left and right turn; they cost no game time
            if (input->count(A_MOVE_W)) {
                heading = (heading + 7) % 8;
                return;
            }
            if (input->count(A_MOVE_E)) {
                heading = (heading + 1) % 8;
                return;
            }
            // everything else moves relative to where we look
            const int h = heading;
            for (int i = 0; i < 8; ++i) {
                const df::interface_key *set = nullptr;
                if (i != 2 && i != 6 && input->count(flat[i]))
                    set = flat;
                else if (input->count(up[i]))
                    set = up;
                else if (input->count(down[i]))
                    set = down;
                if (set) {
                    std::set<df::interface_key> out{set[(h + i) % 8]};
                    INTERPOSE_NEXT(feed)(&out);
                    return;
                }
            }
        }
        INTERPOSE_NEXT(feed)(input);
        trace.after();
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(dungeon_hook, render);
IMPLEMENT_VMETHOD_INTERPOSE(dungeon_hook, feed);
IMPLEMENT_VMETHOD_INTERPOSE(dungeon_hook, logic);

// ---------------------------------------------------------------------------
// render thread: animate the camera and draw

SDL_Texture *frame_texture = nullptr;
int tex_w = 0, tex_h = 0;
std::vector<uint32_t> frame_buffer;
Camera camera;
bool camera_valid = false;
uint32_t camera_snap = 0;
std::chrono::steady_clock::time_point last_frame_time;

float angle_diff(float to, float from) {
    float d = std::fmod(to - from, 6.2831853f);
    if (d > 3.14159265f)
        d -= 6.2831853f;
    if (d < -3.14159265f)
        d += 6.2831853f;
    return d;
}

float approach(float value, float goal, float step) {
    const float d = goal - value;
    if (std::abs(d) <= step)
        return goal;
    return value + (d > 0 ? step : -step);
}

SDL_Texture *mini_texture = nullptr;
int mini_tw = 0, mini_th = 0;
uint64_t mini_drawn_hash = 0;
int mini_drawn_n = 0, mini_drawn_tile = 0;
SDL_Texture *ascii_texture = nullptr;
int ascii_tex_side = 0;
uint64_t ascii_drawn_version = 0;

void destroy_frame_texture() {
    if (frame_texture && sdl_destroy_texture)
        sdl_destroy_texture(frame_texture);
    frame_texture = nullptr;
    tex_w = tex_h = 0;
    if (mini_texture && sdl_destroy_texture)
        sdl_destroy_texture(mini_texture);
    mini_texture = nullptr;
    mini_tw = mini_th = 0;
    mini_drawn_n = 0;
    if (ascii_texture && sdl_destroy_texture)
        sdl_destroy_texture(ascii_texture);
    ascii_texture = nullptr;
    ascii_tex_side = 0;
    destroy_ui_textures();
}

// a frame in the colour of the game's panels
void draw_minimap_frame(SDL_Renderer *sdl, const SDL_Rect &dst) {
    Uint8 cr = 0, cg = 0, cb = 0, ca = 255;
    sdl_get_render_draw_color(sdl, &cr, &cg, &cb, &ca);
    sdl_set_render_draw_color(sdl, 255, 170, 30, 255);
    for (int i = 0; i < 2; ++i) {
        const SDL_Rect edge = {dst.x - 1 - i, dst.y - 1 - i, dst.w + 2 + 2 * i, dst.h + 2 + 2 * i};
        sdl_render_draw_rect(sdl, &edge);
    }
    sdl_set_render_draw_color(sdl, cr, cg, cb, ca);
}

// A small white arrow in the tile next to the adventurer that a step forward
// goes to. The minimap shows n x n tiles; (si, sj) is the adventurer's tile.
void draw_minimap_arrow(SDL_Renderer *sdl, const SDL_Rect &dst, int n, int si, int sj) {
    if (n < 3 || si < 0 || sj < 0 || si >= n || sj >= n)
        return;
    static const int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1}, DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    const int h = heading.load() & 7;
    const float tile = float(dst.w) / n;
    // the same size at every zoom (a tile at the default zoom); when tiles are
    // smaller than that it moves out far enough not to cover the adventurer
    const float size = float(dst.w) / 11;
    const float reach = std::max(tile, size * 0.75f) / (DX[h] && DY[h] ? 1.2f : 1.0f);
    const float cx = dst.x + (si + 0.5f) * tile + DX[h] * reach, cy = dst.y + (sj + 0.5f) * tile + DY[h] * reach;
    // the arrow is drawn on a grid of G x G cells
    constexpr int G = 9;
    const float cell = size / G;
    const float ang = h * 0.78539816f, fx = std::sin(ang), fy = -std::cos(ang);
    bool on[G][G] = {};
    for (int y = 0; y < G; ++y)
        for (int x = 0; x < G; ++x) {
            const float px = (x - G / 2) / float(G / 2), py = (y - G / 2) / float(G / 2);
            const float fwd = px * fx + py * fy, lat = std::abs(px * -fy + py * fx);
            on[y][x] = (fwd >= -0.05f && fwd <= 0.85f && lat <= (0.85f - fwd) * 0.95f) ||
                       (fwd >= -0.8f && fwd < 0 && lat <= 0.22f);
        }
    Uint8 cr = 0, cg = 0, cb = 0, ca = 255;
    sdl_get_render_draw_color(sdl, &cr, &cg, &cb, &ca);
    // half see-through, so the tile under it shows
    SDL_BlendMode blend_was = SDL_BLENDMODE_NONE;
    sdl_get_render_draw_blend_mode(sdl, &blend_was);
    sdl_set_render_draw_blend_mode(sdl, SDL_BLENDMODE_BLEND);
    constexpr Uint8 ARROW_ALPHA = 128;
    const auto put = [&](int x, int y) {
        const int x0 = int(cx + (x - G / 2.0f) * cell), y0 = int(cy + (y - G / 2.0f) * cell);
        const int x1 = int(cx + (x + 1 - G / 2.0f) * cell), y1 = int(cy + (y + 1 - G / 2.0f) * cell);
        const SDL_Rect r = {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
        sdl_render_fill_rect(sdl, &r);
    };
    // a dark edge, so that it reads on light ground too
    sdl_set_render_draw_color(sdl, 0, 0, 0, ARROW_ALPHA);
    for (int y = 0; y < G; ++y)
        for (int x = 0; x < G; ++x) {
            if (on[y][x])
                continue;
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
                for (int dx = -1; dx <= 1 && !edge; ++dx)
                    edge = y + dy >= 0 && y + dy < G && x + dx >= 0 && x + dx < G && on[y + dy][x + dx];
            if (edge)
                put(x, y);
        }
    sdl_set_render_draw_color(sdl, 255, 255, 255, ARROW_ALPHA);
    for (int y = 0; y < G; ++y)
        for (int x = 0; x < G; ++x)
            if (on[y][x])
                put(x, y);
    sdl_set_render_draw_color(sdl, cr, cg, cb, ca);
    sdl_set_render_draw_blend_mode(sdl, blend_was);
}

// the minimap in text characters: a picture made on the simulation thread
void draw_minimap_ascii(SDL_Renderer *sdl, const SDL_Rect &dst) {
    {
        std::lock_guard<std::mutex> lock(ascii_mutex);
        if (ascii_side <= 0 || ascii_pixels.size() != size_t(ascii_side) * ascii_side)
            return;
        if (!ascii_texture || ascii_tex_side != ascii_side) {
            if (ascii_texture)
                sdl_destroy_texture(ascii_texture);
            ascii_texture = sdl_create_texture(sdl, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, ascii_side,
                                               ascii_side);
            ascii_tex_side = ascii_side;
            ascii_drawn_version = 0;
            if (!ascii_texture)
                return;
            sdl_set_texture_blend_mode(ascii_texture, SDL_BLENDMODE_NONE);
            sdl_set_texture_scale_mode(ascii_texture, SDL_ScaleModeLinear);
        }
        if (ascii_drawn_version != ascii_version) {
            sdl_update_texture(ascii_texture, nullptr, ascii_pixels.data(), ascii_side * 4);
            ascii_drawn_version = ascii_version;
        }
    }
    sdl_render_copy(sdl, ascii_texture, nullptr, &dst);
    const int n = ascii_tex_side / GLYPH;
    draw_minimap_arrow(sdl, dst, n, n / 2, n / 2);
    draw_minimap_frame(sdl, dst);
}

// The minimap is the game's own drawing: the tiles parked in the top-left of
// the map viewport are drawn by the game into an off-screen texture, which is
// then copied, scaled down, into the corner.
void draw_minimap(df::renderer_2d_base *r, SDL_Renderer *sdl) {
    const int n = mini_n.load();
    SDL_Rect dst;
    df::graphic_viewportst *vp = gps ? gps->main_viewport : nullptr;
    if (!minimap_rect(dst))
        return;
    if (settings.minimap_ascii) {
        draw_minimap_ascii(sdl, dst);
        return;
    }
    if (n <= 0 || !vp || vp->dim_x < n || vp->dim_y < n)
        return;
    const int tile = std::max(1, r->viewport_zoom_factor * 32 / 128);
    const int size = n * tile;
    const int tw = size + std::max(0, r->origin_x) + 8, th = size + std::max(0, r->origin_y) + 8;
    if (!mini_texture || mini_tw != tw || mini_th != th) {
        if (mini_texture)
            sdl_destroy_texture(mini_texture);
        mini_texture = sdl_create_texture(sdl, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
        mini_tw = tw;
        mini_th = th;
        mini_drawn_n = 0;
        if (!mini_texture)
            return;
        sdl_set_texture_blend_mode(mini_texture, SDL_BLENDMODE_NONE);
        sdl_set_texture_scale_mode(mini_texture, SDL_ScaleModeLinear);
    }
    Uint8 cr = 0, cg = 0, cb = 0, ca = 255;
    sdl_get_render_draw_color(sdl, &cr, &cg, &cb, &ca);
    // the tiles are only drawn again when something on them changed
    const uint64_t hash = mini_hash.load();
    if (hash != mini_drawn_hash || n != mini_drawn_n || tile != mini_drawn_tile) {
        if (sdl_set_render_target(sdl, mini_texture) != 0)
            return;
        sdl_set_render_draw_color(sdl, 0, 0, 0, 255);
        sdl_render_clear(sdl);
        for (int x = 0; x < n; ++x)
            for (int y = 0; y < n; ++y)
                r->update_viewport_tile(vp, x, y);
        sdl_set_render_target(sdl, nullptr);
        mini_drawn_hash = hash;
        mini_drawn_n = n;
        mini_drawn_tile = tile;
    }

    const SDL_Rect src = {std::max(0, r->origin_x), std::max(0, r->origin_y), size, size};
    sdl_render_copy(sdl, mini_texture, &src, &dst);
    sdl_set_render_draw_color(sdl, cr, cg, cb, ca);
    const int self = mini_self.load();
    draw_minimap_arrow(sdl, dst, n, self >> 8, self & 255);
    draw_minimap_frame(sdl, dst);
}

float look_yaw = 0, look_pitch = 0;   // radians; fraction of the view height
bool look_valid = false;              // look_yaw holds where the mouse last pointed the camera
int look_heading = -1;

// The pointer over the view: none while the mouse turns the camera (the
// crosshair is there instead), a gauntlet while it is free. Outside the view
// the system's own pointer comes back.
enum class Pointer { System, None, Gauntlet };
Pointer pointer_shown = Pointer::System;
SDL_Cursor *cursor_none = nullptr, *cursor_gauntlet = nullptr;

SDL_Cursor *make_cursor(const char *const *rows, int w, int h, int scale) {
    SDL_Surface *surface = sdl_create_rgb_surface_with_format(0, w * scale, h * scale, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return nullptr;
    for (int y = 0; y < h * scale; ++y) {
        auto row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(surface->pixels) + y * surface->pitch);
        for (int x = 0; x < w * scale; ++x) {
            uint32_t c = 0;
            switch (rows ? rows[y / scale][x / scale] : '.') {
            case '#': c = fpv::rgb(24, 20, 30); break;      // outline
            case 'o': c = fpv::rgb(128, 136, 150); break;   // steel
            case 'd': c = fpv::rgb(86, 92, 108); break;     // steel in shade
            case 'w': c = fpv::rgb(226, 232, 240); break;   // shine
            case 'g': c = fpv::rgb(214, 164, 48); break;    // gold trim
            case 'y': c = fpv::rgb(255, 224, 120); break;   // gold shine
            default: break;
            }
            row[x] = c;
        }
    }
    SDL_Cursor *cursor = sdl_create_color_cursor(surface, 0, 0);
    sdl_free_surface(surface);
    return cursor;
}

void show_pointer(Pointer want) {
    static const char *const GAUNTLET[24] = {
        "##..................",
        "#w#.................",
        "#ww#................",
        ".#ww#...............",
        ".#oww#..............",
        "..#oww#.............",
        "..#ooww#.##.........",
        "...#ooww#ww#.##.....",
        "...#dooww#ow#ww#....",
        "....#dooww#ow#ow#.#.",
        "....#dooowwoowoow#w#",
        ".##..#doooowwooowoo#",
        "#ww#.#doooooowwooow#",
        "#oww##dooooooooooow#",
        ".#oowwdoooooooooooo#",
        "..#dooooooooooooood#",
        "...#dooooooooooood#.",
        "....#ddoooooooodd#..",
        ".....#gggggggggg#...",
        ".....#gygggggygg#...",
        "......#dooooood#....",
        "......#dowoowod#....",
        "......#dddddddd#....",
        "......##########....",
    };
    if (!cursor_none)
        cursor_none = make_cursor(nullptr, 8, 8, 1);
    // the pointer from the media folder, if there is one
    static uint32_t cursor_serial = 0xFFFFFFFFu;
    if (cursor_serial != media_serial.load()) {
        cursor_serial = media_serial.load();
        const std::shared_ptr<const fpv::Media> m = get_media();
        if (m && m->cursor.ok() && m->cursor.w <= 256 && m->cursor.h <= 256) {
            SDL_Surface *surface =
                sdl_create_rgb_surface_with_format(0, m->cursor.w, m->cursor.h, 32, SDL_PIXELFORMAT_RGBA32);
            if (surface) {
                for (int y = 0; y < m->cursor.h; ++y)
                    std::memcpy(static_cast<uint8_t *>(surface->pixels) + y * surface->pitch,
                                &m->cursor.px[size_t(y) * m->cursor.w], size_t(m->cursor.w) * 4);
                if (SDL_Cursor *made = sdl_create_color_cursor(surface, 0, 0))
                    cursor_gauntlet = made;
                sdl_free_surface(surface);
            }
        }
    }
    if (!cursor_gauntlet)
        cursor_gauntlet = make_cursor(GAUNTLET, 20, 24, 2);
    SDL_Cursor *cursor = nullptr;
    if (want == Pointer::None)
        cursor = cursor_none;
    else if (want == Pointer::Gauntlet)
        cursor = cursor_gauntlet;
    if (!cursor) {
        if (pointer_shown == Pointer::System)
            return;   // nothing of ours to take back
        cursor = sdl_get_default_cursor();
        want = Pointer::System;
    }
    // set every frame while ours is up: the game may put its own back
    if (cursor)
        sdl_set_cursor(cursor);
    pointer_shown = want;
}

void release_look() {
    look_on = false;
}

// what the adventurer holds, low in the corners like in a first-person
// shooter; the hand that strikes lunges half way to the pointer (cx, cy) and back
void draw_held(uint32_t *buf, int W, int H, int cx, int cy) {
    double lunge_start;
    int lunge_hand;
    {
        std::lock_guard<std::mutex> lock(fx_mutex);
        lunge_start = fx_lunge_start;
        lunge_hand = fx_lunge_hand;
    }
    const double lt = (fx_now() - lunge_start) / LUNGE_TIME;
    std::lock_guard<std::mutex> lock(held_mutex);
    const int size = int(H * 0.36f);
    for (int hand = 0; hand < 2; ++hand) {
        if (!held[hand].on)
            continue;
        int x0 = hand == 1 ? int(W * 0.66f) : int(W * 0.34f) - size;
        int y0 = H - int(size * 0.84f);
        if (hand == lunge_hand && lt >= 0 && lt < 1) {
            const float k = 0.5f * std::sin(3.14159265f * float(lt));
            x0 += int((cx - (x0 + size / 2)) * k);
            y0 += int((cy - (y0 + size / 2)) * k);
        }
        for (int x = std::max(0, x0); x < std::min(W, x0 + size); ++x) {
            int tx = (x - x0) * fpv::TEX / size;
            if (hand == 0)
                tx = fpv::TEX - 1 - tx;   // the left hand mirrors the right
            uint32_t *col = buf + size_t(x) * H;
            for (int y = std::max(0, y0); y < H; ++y) {
                if (y - y0 < 0)
                    continue;
                const int ty = (y - y0) * fpv::TEX / size;
                if (ty >= fpv::TEX)
                    break;
                const uint32_t c = held[hand].px[ty * fpv::TEX + tx];
                if (fpv::px_a(c) >= 128)
                    col[y] = c | 0xFF000000u;
            }
        }
    }
}

// mix a colour into a pixel of the frame (0xAABBGGRR)
inline void blend(uint32_t &dst, int r, int g, int b, float a) {
    if (a <= 0)
        return;
    a = std::min(a, 1.0f);
    const int dr = fpv::px_r(dst), dg = fpv::px_g(dst), db = fpv::px_b(dst);
    dst = fpv::rgb(int(dr + (r - dr) * a), int(dg + (g - dg) * a), int(db + (b - db) * a));
}

// the attack crescents and the arrows of strikes at you, into the frame
// (column by column); (cx, cy) is where the pointer is
// (doll_x, doll_y): the middle of the top of your own paperdoll, where words about you rise from
void draw_combat_fx(uint32_t *buf, int W, int H, const fpv::Camera &cam, int cx, int cy, int cell_w, int cell_h,
                    int doll_x, int doll_y) {
    std::vector<FxArc> arcs;
    std::vector<FxArrow> arrows;
    std::vector<FxText> texts;
    {
        std::lock_guard<std::mutex> lock(fx_mutex);
        arcs = fx_arcs;
        arrows = fx_arrows;
        texts = fx_texts;
    }
    if (arcs.empty() && arrows.empty() && texts.empty())
        return;
    const double now = fx_now();
    const float P = std::tan(cam.fov * 3.14159265f / 360.0f);
    const float S = W / (2 * P), hz = H / 2.0f + cam.pitch;
    const float dirx = std::sin(cam.ang), diry = -std::cos(cam.ang), rgtx = std::cos(cam.ang), rgty = std::sin(cam.ang);
    const auto project = [&](float x, float y, float h, float &sx, float &sy, float &d) {
        const float rx = x - cam.x, ry = y - cam.y;
        d = rx * dirx + ry * diry;
        if (d < 0.2f)
            return false;
        sx = W / 2.0f + (rx * rgtx + ry * rgty) * S / d;
        sy = hz + (cam.eye - h) * S / d;
        return true;
    };

    // a white crescent swept across the strike, bulging towards the target
    for (const FxArc &a : arcs) {
        const float t = float((now - a.start) / ARC_TIME);
        float bx = a.bx, by = a.by;
        if (a.at_you) {
            // at you: it comes towards the eye
            bx = a.ax + (cam.x - a.ax) * 0.6f;
            by = a.ay + (cam.y - a.ay) * 0.6f;
        }
        float ax, ay, ad, tx, ty, td;
        if (!project(a.ax, a.ay, a.ah + 0.38f, ax, ay, ad) || !project(bx, by, a.bh + 0.38f, tx, ty, td))
            continue;
        float vx = tx - ax, vy = ty - ay;
        float len = std::sqrt(vx * vx + vy * vy);
        if (len < 1) {
            vx = 1;
            vy = 0;
            len = 1;
        }
        vx /= len;
        vy /= len;
        const float mx = (ax + tx) / 2, my = (ay + ty) / 2, md = (ad + td) / 2;
        const float r = std::max(8.0f, 0.42f * S / md);
        const float ix = mx - vx * r * 0.4f, iy = my - vy * r * 0.4f, ir = r * 0.92f;
        const float alpha = 0.55f * (1 - t);
        const int x0 = std::max(0, int(mx - r)), x1 = std::min(W - 1, int(mx + r));
        const int y0 = std::max(0, int(my - r)), y1 = std::min(H - 1, int(my + r));
        for (int x = x0; x <= x1; ++x) {
            uint32_t *col = buf + size_t(x) * H;
            for (int y = y0; y <= y1; ++y) {
                const float px = x + 0.5f - mx, py = y + 0.5f - my;
                if (px * px + py * py > r * r)
                    continue;
                const float qx = x + 0.5f - ix, qy = y + 0.5f - iy;
                if (qx * qx + qy * qy < ir * ir)
                    continue;
                if (px * vx + py * vy < -0.1f * r)
                    continue;
                blend(col[y], 255, 255, 255, alpha);
            }
        }
    }

    // the words, rising and fading, with a dark edge so they read on anything;
    // the font half as big again as the interface's
    cell_w = cell_w * 3 / 2;
    cell_h = cell_h * 3 / 2;
    if (ui_font_ready && cell_w > 0 && cell_h > 0)
        for (const FxText &tx : texts) {
            const float t = float((now - tx.start) / TEXT_TIME);
            float sx, sy, d;
            if (tx.you) {
                sx = float(doll_x);
                sy = float(doll_y) - 2;
            } else if (!project(tx.x, tx.y, tx.h, sx, sy, d)) {
                continue;
            }
            sy -= t * H * 0.06f + tx.stack * (cell_h + 2);
            const float alpha = t < 0.6f ? 1.0f : std::max(0.0f, 1 - (t - 0.6f) / 0.4f);
            const int x0 = int(sx) - int(tx.text.size()) * cell_w / 2, y0 = int(sy) - cell_h;
            const auto glyphs = [&](int ox, int oy, int r, int g, int b, float a) {
                for (size_t i = 0; i < tx.text.size(); ++i) {
                    const uint8_t *rows = ui_font.rows[uint8_t(tx.text[i])];
                    for (int yy = 0; yy < cell_h; ++yy) {
                        const int y = y0 + oy + yy;
                        if (y < 0 || y >= H)
                            continue;
                        const uint8_t bits = rows[yy * 12 / cell_h];
                        if (!bits)
                            continue;
                        for (int xx = 0; xx < cell_w; ++xx) {
                            const int x = x0 + ox + int(i) * cell_w + xx;
                            if (x >= 0 && x < W && (bits >> (xx * 8 / cell_w)) & 1)
                                blend(buf[size_t(x) * H + y], r, g, b, a);
                        }
                    }
                }
            };
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                    if (ox || oy)
                        glyphs(ox, oy, 0, 0, 0, 0.8f * alpha);
            glyphs(0, 0, fpv::px_r(tx.color), fpv::px_g(tx.color), fpv::px_b(tx.color), alpha);
        }

    // an arrow from the pointer towards whoever struck at you
    for (const FxArrow &a : arrows) {
        const float t = float((now - a.start) / ARROW_TIME);
        const float rx = a.ax - cam.x, ry = a.ay - cam.y;
        float vx = rx * rgtx + ry * rgty, vy = -(rx * dirx + ry * diry);
        const float len = std::sqrt(vx * vx + vy * vy);
        if (len < 0.01f)
            continue;
        vx /= len;
        vy /= len;
        const float nx = -vy, ny = vx;
        const float r0 = H * 0.035f, r1 = H * 0.13f, head = H * 0.04f, half = H * 0.007f, wide = H * 0.028f;
        const float alpha = t < 0.45f ? 1.0f : std::max(0.0f, 1 - (t - 0.45f) / 0.55f);
        const int cr = a.red ? 235 : 255, cg = a.red ? 30 : 255, cb = a.red ? 30 : 255;
        const float reach = r1 + wide + 3;
        const int x0 = std::max(0, int(cx - reach)), x1 = std::min(W - 1, int(cx + reach));
        const int y0 = std::max(0, int(cy - reach)), y1 = std::min(H - 1, int(cy + reach));
        const auto inside = [&](float u, float w, float grow) {
            if (u >= r0 - grow && u <= r1 - head && std::abs(w) <= half + grow)
                return true;
            return u > r1 - head && u <= r1 + grow && std::abs(w) <= wide * (r1 - u) / head + grow;
        };
        for (int x = x0; x <= x1; ++x) {
            uint32_t *col = buf + size_t(x) * H;
            for (int y = y0; y <= y1; ++y) {
                const float px = x + 0.5f - cx, py = y + 0.5f - cy;
                const float u = px * vx + py * vy, w = px * nx + py * ny;
                if (inside(u, w, 0))
                    blend(col[y], cr, cg, cb, 0.9f * alpha);
                else if (inside(u, w, 2))
                    blend(col[y], 0, 0, 0, 0.6f * alpha);
            }
        }
    }
}

void draw_crosshair(SDL_Renderer *sdl, int cx, int cy, bool red) {
    Uint8 cr = 0, cg = 0, cb = 0, ca = 255;
    sdl_get_render_draw_color(sdl, &cr, &cg, &cb, &ca);
    const auto arms = [&](int grow) {
        const SDL_Rect rects[4] = {{cx - 14 - grow, cy - 1 - grow, 10 + 2 * grow, 2 + 2 * grow},
                                   {cx + 4 - grow, cy - 1 - grow, 10 + 2 * grow, 2 + 2 * grow},
                                   {cx - 1 - grow, cy - 14 - grow, 2 + 2 * grow, 10 + 2 * grow},
                                   {cx - 1 - grow, cy + 4 - grow, 2 + 2 * grow, 10 + 2 * grow}};
        for (const SDL_Rect &rect : rects)
            sdl_render_fill_rect(sdl, &rect);
    };
    sdl_set_render_draw_color(sdl, 0, 0, 0, 255);
    arms(1);
    if (red)
        sdl_set_render_draw_color(sdl, 255, 40, 30, 255);
    else
        sdl_set_render_draw_color(sdl, 255, 255, 255, 255);
    arms(0);
    sdl_set_render_draw_color(sdl, cr, cg, cb, ca);
}

void draw_view(df::renderer_2d_base *r) {
    auto sdl = static_cast<SDL_Renderer *>(r->sdl_renderer);
    if (!sdl || !renderer)
        return;

    Target t;
    {
        std::lock_guard<std::mutex> lock(target_mutex);
        t = target;
    }
    if (!t.valid)
        return;

    int out_w = 0, out_h = 0;
    if (sdl_get_renderer_output_size(sdl, &out_w, &out_h) != 0 || out_w < 16 || out_h < 16)
        return;
    const float res = std::clamp(settings.resolution.load(), 0.1f, 1.0f);
    const int W = std::max(64, int(out_w * res) & ~1), H = std::max(36, int(out_h * res) & ~1);

    // the frame is stored column by column, so the texture is the image on its side
    if (!frame_texture || tex_w != W || tex_h != H) {
        destroy_frame_texture();
        // ARGB is the layout the game's Direct3D renderer stores textures in:
        // anything else is converted pixel by pixel on every upload
        frame_texture = sdl_create_texture(sdl, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, H, W);
        if (!frame_texture)
            return;
        sdl_set_texture_blend_mode(frame_texture, SDL_BLENDMODE_NONE);
        sdl_set_texture_scale_mode(frame_texture, res < 0.999f ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        tex_w = W;
        tex_h = H;
        frame_buffer.assign(size_t(W) * H, 0xFF000000u);
    }

    // glide towards where the adventurer is and where they look
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_frame_time).count();
    last_frame_time = now;
    dt = std::clamp(dt, 0.0f, 0.1f);
    const float tang = heading * 0.78539816f;
    if (!camera_valid || camera_snap != t.snap || !settings.smooth) {
        camera.x = t.x;
        camera.y = t.y;
        camera.eye = t.eye;
        camera.ang = tang;
        camera_valid = true;
        camera_snap = t.snap;
    } else {
        const float dx = t.x - camera.x, dy = t.y - camera.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float step = std::max(5.5f, dist * 9.0f) * dt;
        if (dist <= step) {
            camera.x = t.x;
            camera.y = t.y;
        } else {
            camera.x += dx / dist * step;
            camera.y += dy / dist * step;
        }
        camera.eye = approach(camera.eye, t.eye, 9.0f * dt);
        const float da = angle_diff(tang, camera.ang);
        const float astep = 10.0f * dt;
        camera.ang = std::abs(da) <= astep ? tang : camera.ang + (da > 0 ? astep : -astep);
    }
    camera.fov = settings.fov;

    // Mouse look: the pointer stays in the middle of the window and how far it
    // was moved turns the camera. Holding Alt lets go of it, to reach the
    // interface; so does anything on screen that needs the pointer.
    const SDL_Keymod mods = sdl_get_mod_state();
    SDL_Window *window = sdl_get_keyboard_focus();
    ctrl_held = debug_ctrl || (window && (mods & KMOD_CTRL) != 0);
    shift_held = window && (mods & KMOD_SHIFT) != 0;
    {
        // Ctrl+R: the next intent
        static bool r_was = false, ctrl_was = false;
        const Uint8 *keys = sdl_get_keyboard_state(nullptr);
        const bool ctrl = ctrl_held;
        const bool r_down = window && keys && keys[SDL_SCANCODE_R];
        space_down = window && keys && keys[SDL_SCANCODE_SPACE];
        swallow_keys = ctrl && r_down;
        if (ctrl && r_down && !r_was)
            cycle_intent();
        else if (ctrl && !ctrl_was && (look_on || debug_look))
            flash(INTENT_NAMES[effective_intent()]);
        r_was = r_down;
        ctrl_was = ctrl;
    }
    if (settings.mouse_look && look_ok && window && !(mods & KMOD_ALT) && debug_px.load() < 0) {
        const int cx = out_w / 2, cy = out_h / 2;
        int mx = cx, my = cy;
        sdl_get_mouse_state(&mx, &my);
        if (!look_on) {
            look_on = true;
            if (!look_valid) {
                look_yaw = camera.ang;
                look_heading = heading;
                look_valid = true;
            }
        } else {
            if (heading != look_heading)
                look_yaw = heading * 0.78539816f;   // turned with the keys
            look_yaw += (mx - cx) * 0.0028f;
            look_pitch = std::clamp(look_pitch - (my - cy) * 0.0022f, -1.0f, 1.0f);
        }
        if (mx != cx || my != cy)
            sdl_warp_mouse_in_window(window, cx, cy);
        look_yaw = std::fmod(look_yaw, 6.2831853f);
        if (look_yaw < 0)
            look_yaw += 6.2831853f;
        look_heading = int(std::floor(look_yaw / 0.78539816f + 0.5f)) & 7;
        heading = look_heading;
        camera.ang = look_yaw;
    } else {
        release_look();
        // The view stays where the mouse left it when the pointer is let go
        // (a menu opened, Alt held): what was under the crosshair stays
        // there. Turning with the keys still turns it.
        if (look_valid && settings.mouse_look) {
            if (heading != look_heading) {
                look_yaw = heading * 0.78539816f;
                look_heading = heading;
            }
            camera.ang = look_yaw;
        }
    }
    camera.pitch = look_pitch * H;
    show_pointer(look_on ? Pointer::None : Pointer::Gauntlet);

    view_out_w = out_w;
    view_out_h = out_h;

    fpv::RenderOptions opt;
    {
        // brighten what the pointer is on (found in the previous frame)
        int mx, my;
        if (pointer_pixels(mx, my) && !over_interface(mx, my))
            opt.highlight = pick_pixels(mx, my);
    }
    opt.zlo = t.zlo;
    opt.zhi = t.zhi;
    opt.view_distance = settings.view_distance;
    opt.daylight = t.daylight;
    opt.fog = t.fog;

    const auto t0 = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(scene.mutex);
        renderer->render(scene, camera, opt, W, H, frame_buffer.data());
    }
    {
        // the pointer, in the frame's pixels: the middle while the mouse turns the view
        int fx = W / 2, fy = H / 2;
        if (!look_on && !debug_look) {
            int mx = out_w / 2, my = out_h / 2;
            sdl_get_mouse_state(&mx, &my);
            fx = mx * W / out_w;
            fy = my * H / out_h;
        }
        // the game's font at the interface's size, in the frame's pixels
        int cell_w = 8, cell_h = 12;
        if (r->dispx_z > 0 && r->dispy_z > 0) {
            cell_w = std::max(4, r->dispx_z * W / out_w);
            cell_h = std::max(6, r->dispy_z * H / out_h);
        }
        // your paperdoll sits where the portrait was: the bottom left BODY_COLS x BODY_ROWS cells
        int doll_x = W / 8, doll_y = H - H / 6;
        if (r->dispx_z > 0 && r->dispy_z > 0 && gps) {
            doll_x = (r->origin_x + BODY_COLS * r->dispx_z / 2) * W / out_w;
            doll_y = (r->origin_y + (gps->dimy - BODY_ROWS) * r->dispy_z) * H / out_h;
        }
        draw_combat_fx(frame_buffer.data(), W, H, camera, fx, fy, cell_w, cell_h, doll_x, doll_y);
        draw_held(frame_buffer.data(), W, H, fx, fy);
    }
    renderer->swapRedBlue(frame_buffer.data(), size_t(W) * H);   // our pixels are ABGR
    const auto t1 = std::chrono::steady_clock::now();
    sdl_update_texture(frame_texture, nullptr, frame_buffer.data(), H * 4);

    // Turn the sideways texture back upright: flip it, then rotate a quarter
    // turn around its top-left corner, which ends up at the top right of the
    // window.
    const SDL_Rect dst = {out_w, 0, out_h, out_w};
    const SDL_Point pivot = {0, 0};
    sdl_render_copy_ex(sdl, frame_texture, nullptr, &dst, 90.0, &pivot, SDL_FLIP_VERTICAL);
    draw_minimap(r, sdl);
    sync_ui_textures(sdl);
    if (settings.compass)
        draw_compass(sdl, out_w, camera.ang);
    draw_messages(sdl);
    draw_body(sdl);
    draw_menu(sdl);
    if (look_on || debug_look) {
        const UiTexture &mark = ctrl_held ? tex_aim[std::clamp(effective_intent(), 0, fpv::AIM_COUNT - 1)] : tex_crosshair;
        if (mark.tex)
            mark.draw(sdl, out_w / 2 - mark.w / 2, out_h / 2 - mark.h / 2);
        else
            draw_crosshair(sdl, out_w / 2, out_h / 2, ctrl_held);
        draw_flash(sdl, out_w / 2, out_h / 2);
    } else {
        int mx = out_w / 2, my = out_h / 2;
        sdl_get_mouse_state(&mx, &my);
        draw_flash(sdl, mx, my);
    }
    const auto t2 = std::chrono::steady_clock::now();

    stat_render_us = uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    stat_upload_us = uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count());
    stat_w = W;
    stat_h = H;
    ++stat_frames;
}

struct renderer_hook : df::renderer_2d_base {
    typedef df::renderer_2d_base interpose_base;
    DEFINE_VMETHOD_INTERPOSE(void, update_all, ()) {
        // another screen has taken over (back to the arena, the title ...):
        // nothing is captured any more, so there is nothing of ours to show
        if (showing) {
            const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (now_ms - last_capture_ms.load() > 700)
                showing = false;
        }
        if (active && showing) {
            draw_view(this);
        } else {
            release_look();
            look_valid = false;
            show_pointer(Pointer::System);
            camera_valid = false;
            if (frame_texture && !active)
                destroy_frame_texture();
        }
        INTERPOSE_NEXT(update_all)();
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(renderer_hook, update_all);

// ---------------------------------------------------------------------------
// commands

bool hooks_installed = false;

bool install_hooks(color_ostream &out) {
    if (hooks_installed)
        return true;
    if (!bind_sdl(out))
        return false;
    watch_wheel(true);
    if (!renderer)
        renderer = std::make_unique<fpv::Renderer>();
    if (!INTERPOSE_HOOK(dungeon_hook, render).apply() || !INTERPOSE_HOOK(dungeon_hook, feed).apply() ||
        !INTERPOSE_HOOK(dungeon_hook, logic).apply() || !INTERPOSE_HOOK(renderer_hook, update_all).apply()) {
        INTERPOSE_HOOK(dungeon_hook, render).remove();
        INTERPOSE_HOOK(dungeon_hook, feed).remove();
        INTERPOSE_HOOK(dungeon_hook, logic).remove();
        INTERPOSE_HOOK(renderer_hook, update_all).remove();
        out.printerr("fpview: could not hook the game\n");
        return false;
    }
    hooks_installed = true;
    return true;
}

void remove_hooks() {
    if (!hooks_installed)
        return;
    INTERPOSE_HOOK(dungeon_hook, render).remove();
    INTERPOSE_HOOK(dungeon_hook, feed).remove();
    INTERPOSE_HOOK(dungeon_hook, logic).remove();
    INTERPOSE_HOOK(renderer_hook, update_all).remove();
    hooks_installed = false;
}

command_result set_active(color_ostream &out, bool on) {
    if (on) {
        if (!World::getAdventurer()) {
            out.printerr("fpview: the first-person view only works while you control an adventurer\n");
            return CR_FAILURE;
        }
        if (!install_hooks(out))
            return CR_FAILURE;
        full_capture = true;
        last_ax = -1;
        {
            std::lock_guard<std::mutex> lock(target_mutex);
            target.valid = false;
        }
        active = true;
    } else {
        active = false;
        showing = false;
        realtime = false;
        restore_zoom();
    }
    is_enabled = active;
    // make the game repaint the map now that the image is gone or about to appear
    if (gps)
        ++gps->force_full_display_count;
    return CR_OK;
}

bool parse_bool(const string &v, bool &out) {
    if (v == "true" || v == "on" || v == "1" || v == "yes") { out = true; return true; }
    if (v == "false" || v == "off" || v == "0" || v == "no") { out = false; return true; }
    return false;
}

command_result set_setting(color_ostream &out, const string &key, const string &value) {
    bool b = false;
    float f = 0;
    try {
        if (key == "relative_controls" || key == "smooth" || key == "compass" || key == "minimap" ||
            key == "minimap_ascii" || key == "wasd" || key == "mouse_look" || key == "combat_menus" ||
            key == "game_tips" || key == "body_doll") {
            if (!parse_bool(value, b))
                return CR_WRONG_USAGE;
            if (key == "relative_controls") settings.relative_controls = b;
            else if (key == "smooth") settings.smooth = b;
            else if (key == "minimap") settings.minimap = b;
            else if (key == "minimap_ascii") settings.minimap_ascii = b;
            else if (key == "wasd") settings.wasd = b;
            else if (key == "mouse_look") settings.mouse_look = b;
            else if (key == "combat_menus") settings.combat_menus = b;
            else if (key == "game_tips") settings.game_tips = b;
            else if (key == "body_doll") settings.body_doll = b;
            else settings.compass = b;
            return CR_OK;
        }
        f = std::stof(value);
    } catch (...) {
        return CR_WRONG_USAGE;
    }
    if (key == "fov") settings.fov = std::clamp(f, 40.0f, 150.0f);
    else if (key == "resolution") settings.resolution = std::clamp(f, 10.0f, 100.0f) / 100.0f;
    else if (key == "view_distance") settings.view_distance = std::clamp(f, 4.0f, 160.0f);
    else if (key == "minimap_tiles") settings.minimap_tiles = std::clamp(int(f), 5, 61);
    else if (key == "realtime_rate") settings.realtime_rate = std::clamp(int(f), 1, 99);
    else if (key == "range") { settings.range = std::clamp(int(f), 1, 4); full_capture = true; }
    else if (key == "levels_up") { settings.levels_up = std::clamp(int(f), 0, 40); full_capture = true; }
    else if (key == "levels_down") { settings.levels_down = std::clamp(int(f), 0, 24); full_capture = true; }
    else {
        out.printerr("fpview: unknown setting '{}'\n", key);
        return CR_FAILURE;
    }
    return CR_OK;
}

command_result do_command(color_ostream &out, vector<string> &parameters) {
    const string cmd = parameters.empty() ? "status" : parameters[0];
    if (cmd == "on" || cmd == "off" || cmd == "toggle") {
        const bool on = cmd == "on" || (cmd == "toggle" && !active);
        const command_result res = set_active(out, on);
        if (res == CR_OK)
            out.print("First-person view {}.\n", active ? "on" : "off");
        return res;
    }
    if (cmd == "set" && parameters.size() == 3)
        return set_setting(out, parameters[1], parameters[2]);
    if (cmd == "realtime") {
        // the timer button: "realtime on|off|toggle" lets time pass by itself
        if (parameters.size() == 2) {
            const bool on = parameters[1] == "on" || (parameters[1] == "toggle" && !realtime);
            if (on && !(active && showing)) {
                out.printerr("fpview: time can only run by itself while the first-person view is shown\n");
                return CR_FAILURE;
            }
            realtime = on;
        }
        out.print("Time running by itself: {} ({} game ticks a second)\n", realtime ? "on" : "off",
                  settings.realtime_rate.load());
        return CR_OK;
    }
    if (cmd == "media" && parameters.size() >= 2) {
        // the art folder; read once, and again when asked with "reload"
        const std::shared_ptr<const fpv::Media> have = get_media();
        if (have && have->dir == parameters[1] && parameters.size() == 2)
            return CR_OK;
        const int n = set_media(parameters[1]);
        out.print("fpview: {} art files loaded from {}\n", n, parameters[1]);
        return CR_OK;
    }
    if (cmd == "intent") {
        // "intent" shows it, "intent next" switches like Ctrl+R, "intent strike|shoot|throw|wrestle" sets it
        if (parameters.size() == 2) {
            if (parameters[1] == "next") {
                cycle_intent();
            } else {
                for (int i = 0; i < INTENT_COUNT; ++i) {
                    std::string name = INTENT_NAMES[i];
                    name[0] = char(name[0] - 'A' + 'a');
                    if (parameters[1] == name) {
                        intent = (i + INTENT_COUNT - 1) % INTENT_COUNT;
                        const bool could = can_shoot;
                        can_shoot = true;
                        cycle_intent();
                        can_shoot = could;
                    }
                }
            }
        }
        out.print("Intent: {}{}\n", INTENT_NAMES[intent.load()],
                  effective_intent() != intent.load() ? " (nothing to shoot with: strikes)" : "");
        return CR_OK;
    }
    if (cmd == "menu") {
        // testing aid: what our attack menu holds and where its parts are, in window pixels
        const std::shared_ptr<const MenuModel> m = get_menu();
        if (!m) {
            out.print("(no menu)\n");
            return CR_OK;
        }
        MenuLayout L;
        const bool laid = layout_menu(*m, L);
        out.print("menu {} '{}' doll {} panel {},{} {}x{}\n", m->kind, m->title, m->doll ? m->doll->name : "-", L.x, L.y,
                  L.w, L.h);
        if (laid)
            for (const MenuItem &it : L.items) {
                const int cx = L.x + it.x + it.w / 2, cy = L.y + it.y + it.h / 2;
                if (it.kind == ITEM_OPTION)
                    out.print("  option {},{} [{}] {}\n", cx, cy, m->options[it.index].on ? "x" : " ",
                              m->options[it.index].label);
                else if (it.kind == ITEM_CANCEL)
                    out.print("  cancel {},{}\n", cx, cy);
                else
                    out.print("  {} {},{} {}: {} / {}{}\n", it.kind == ITEM_DOLL ? "doll" : "list", cx, cy,
                              m->entries[it.index].name, m->entries[it.index].d1, m->entries[it.index].d2,
                              m->entries[it.index].mark.empty() ? "" : " " + m->entries[it.index].mark);
            }
        out.print("{} entries, {} options\n", m->entries.size(), m->options.size());
        return CR_OK;
    }
    if (cmd == "turn" && parameters.size() == 2) {
        heading = (heading + (parameters[1] == "left" ? 7 : 1)) % 8;
        return CR_OK;
    }
    if (cmd == "pointer" && parameters.size() >= 2) {
        // testing aid: pretend the mouse is at this window pixel ("pointer off" to stop)
        if (parameters[1] == "off") {
            debug_px = -1;
            debug_py = -1;
        } else if (parameters.size() == 3) {
            debug_py = std::atoi(parameters[2].c_str());
            debug_px = std::atoi(parameters[1].c_str());
        } else {
            return CR_WRONG_USAGE;
        }
        return CR_OK;
    }
    if (cmd == "camera" && parameters.size() >= 2) {
        // testing aid: "camera <x> <y> <z>" looks from that map tile, "camera off" goes back
        if (parameters[1] == "off") {
            free_camera = false;
        } else if (parameters.size() == 4) {
            free_x = std::atoi(parameters[1].c_str());
            free_y = std::atoi(parameters[2].c_str());
            free_z = std::atoi(parameters[3].c_str());
            free_camera = true;
        } else {
            return CR_WRONG_USAGE;
        }
        full_capture = true;
        return CR_OK;
    }
    if (cmd == "face" && parameters.size() == 2) {
        // testing aid: "face N|NE|E|SE|S|SW|W|NW"
        for (int i = 0; i < 8; ++i)
            if (parameters[1] == DIR_NAMES[i])
                heading = i;
        return CR_OK;
    }
    if (cmd == "find") {
        // testing aid: aim the stand-in pointer at a creature that is in view
        int x = 0, y = 0;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(scene.mutex);
            found = renderer && renderer->findCreature(x, y);
        }
        if (!found || stat_w.load() <= 0) {
            out.print("no creature in view\n");
            return CR_OK;
        }
        debug_px = int(int64_t(x) * view_out_w.load() / stat_w.load());
        debug_py = int(int64_t(y) * view_out_h.load() / stat_h.load());
        out.print("pointer aimed at {},{}\n", debug_px.load(), debug_py.load());
        return CR_OK;
    }
    if (cmd == "hint") {
        // testing aid: the hover text currently shown
        for (const std::string &line : hover_lines)
            out.print("{}\n", line);
        if (hover_lines.empty())
            out.print("(no hover text)\n");
        return CR_OK;
    }
    if (cmd == "mouselog") {
        // testing aid: what recent clicks did
        for (const std::string &line : mouse_log)
            out.print("{}\n", line);
        out.print("{} entries\n", mouse_log.size());
        mouse_log.clear();
        return CR_OK;
    }
    if (cmd == "debug" && parameters.size() == 3) {
        if (parameters[1] == "noblank")
            debug_noblank = parameters[2] == "on";
        else if (parameters[1] == "center")
            debug_center = parameters[2] == "on";
        else if (parameters[1] == "ctrl")
            debug_ctrl = parameters[2] == "on";
        else if (parameters[1] == "look")
            debug_look = parameters[2] == "on";
        else
            return CR_WRONG_USAGE;
        return CR_OK;
    }
    if (cmd == "pick") {
        int px, py;
        if (!pointer_pixels(px, py)) {
            out.print("pointer is outside the window\n");
            return CR_OK;
        }
        int cx = -1, cy = -1;
        const bool cell = interface_cell(px, py, cx, cy);
        out.print("pointer at {},{} (interface cell {},{}){}\n", px, py, cx, cy,
                  over_interface(px, py) ? " - over the interface" : "");
        if (cell) {
            const int idx = cx * gps->dimy + cy;
            out.print("  cell: char {} texpos {} lower {} anchored {} flag {}\n", int(gps->screen[idx * 8]),
                      int(gps->screentexpos[idx]), int(gps->screentexpos_lower[idx]),
                      int(gps->screentexpos_anchored[idx]), gps->screentexpos_flag[idx]);
        }
        const fpv::Pick p = pick_pixels(px, py);
        if (p.valid())
            out.print("  on tile {},{},{} unit {}\n", p.x, p.y, p.z, p.unit_id);
        else
            out.print("  on nothing\n");
        return CR_OK;
    }
    if (cmd != "status")
        return CR_WRONG_USAGE;

    out.print("First-person view is {}{}.\n", active ? "ON" : "off",
              (active && !showing) ? " (hidden while a menu needs the map)" : "");
    out.print("  mouse look {}\n", look_on ? "holding the pointer" : look_ok ? "ready" : "off (a menu is open)");
    out.print("  facing {}\n", DIR_NAMES[heading.load()]);
    out.print("  time running by itself: {} ({} game ticks a second)\n", realtime ? "on" : "off",
              settings.realtime_rate.load());
    if (stat_frames)
        out.print("  {}x{}, {} threads: draw {:.1f} ms, upload {:.1f} ms, capture {:.1f} ms, {} frames\n",
                  stat_w.load(), stat_h.load(), renderer ? renderer->threads() : 0,
                  stat_render_us.load() / 1000.0, stat_upload_us.load() / 1000.0,
                  stat_capture_us.load() / 1000.0, stat_frames.load());
    out.print("  map reads left out (the game drew some other place): {}\n", stat_reads_skipped.load());
    out.print("  fov {}  resolution {}%  view_distance {}  range {}  levels_up {}  levels_down {}\n",
              settings.fov.load(), int(settings.resolution.load() * 100 + 0.5f), settings.view_distance.load(),
              settings.range.load(), settings.levels_up.load(), settings.levels_down.load());
    out.print("  relative_controls {}  smooth {}  compass {}  minimap {}  minimap_ascii {}  minimap_tiles {}\n",
              settings.relative_controls.load(), settings.smooth.load(), settings.compass.load(),
              settings.minimap.load(), settings.minimap_ascii.load(), mini_tiles());
    out.print("  wasd {}  mouse_look {}  combat_menus {}\n", settings.wasd.load(), settings.mouse_look.load(),
              settings.combat_menus.load());
    {
        const std::shared_ptr<const fpv::Media> m = get_media();
        out.print("  intent {}  art {} files{}{}\n", INTENT_NAMES[intent.load()], m ? m->files : 0, m ? " from " : "",
                  m ? m->dir : "");
    }
    return CR_OK;
}

} // namespace

DFhackCExport command_result plugin_init(color_ostream &out, std::vector<PluginCommand> &commands) {
    commands.push_back(PluginCommand(
        plugin_name,
        "First-person view for adventure mode.",
        do_command));
    load_intent();
    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable) {
    return set_active(out, enable);
}

// The view belongs to a living creature the player controls. When that ends
// (the adventurer dies, or control of an arena creature is given up) the view
// switches itself off, and the mouse and the normal map come back.
DFhackCExport command_result plugin_onupdate(color_ostream &out) {
    static int gone = 0;
    if (!active) {
        gone = 0;
        return CR_OK;
    }
    df::unit *adv = World::getAdventurer();
    // (in the arena, isAdventureMode() is false while a creature is controlled)
    const bool arena = df::global::gametype && *df::global::gametype == df::game_type::ADVENTURE_ARENA;
    if (!(World::isAdventureMode() || arena) || !adv || Units::isDead(adv)) {
        if (++gone >= 10) {
            gone = 0;
            set_active(out, false);
        }
    } else {
        gone = 0;
    }
    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event) {
    if (event == SC_MAP_UNLOADED || event == SC_WORLD_UNLOADED || event == SC_MAP_LOADED) {
        // texture numbers and the map itself change with the world
        if (event != SC_MAP_LOADED) {
            active = false;
            is_enabled = false;
        }
        showing = false;
        realtime = false;
        restore_zoom();
        // the buttons of the mod must not go by what was true in the last game
        published_state = -1;
        publish_state(false, false);
        std::lock_guard<std::mutex> lock(scene.mutex);
        scene.reset();
        full_capture = true;
        pre_x = -100000;
        last_ax = -1;
        std::lock_guard<std::mutex> tlock(target_mutex);
        target.valid = false;
    }
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out) {
    active = false;
    showing = false;
    remove_hooks();
    watch_wheel(false);
    // give a frame that is already inside one of the hooks time to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    renderer.reset();
    // The textures belong to the render thread, which normally frees them
    // when the view is switched off; if they are still here they are left to SDL.
    frame_texture = nullptr;
    mini_texture = nullptr;
    ascii_texture = nullptr;
    if (gps)
        ++gps->force_full_display_count;
    return CR_OK;
}
