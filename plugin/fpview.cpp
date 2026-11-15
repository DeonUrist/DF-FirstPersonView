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

#include <optional>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <random>
#include <set>
#include <sstream>
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
#include "df/adventure_movement_movest.h"
#include "df/adventure_movement_building_interactst.h"
#include "df/adventure_movement_item_interactst.h"
#include "df/adventure_movement_pathst.h"
#include "df/adventure_movement_optionst.h"
#include "df/adventure_option_view_itemst.h"
#include "df/adventure_interface_inventoryst.h"
#include "df/adventure_interface_inventory_context_type.h"
#include "df/adventure_inventory_option_list_type.h"
#include "df/adventure_option_inventory_itemst.h"
#include "df/adventure_option_inventory_item_option_flag.h"
#include "df/adventure_option_wear_itemst.h"
#include "df/adventure_option_drop_itemst.h"
#include "df/adventure_environment_pick_up_ground_itemst.h"
#include "df/adventure_interface_option_list_context_type.h"
#include "df/tile_pagest.h"
#include "df/texture_handlerst.h"
#include "df/adventure_option_throw_itemst.h"
#include "df/adventure_item_interact_give_namest.h"
#include "df/adventure_option_remove_itemst.h"
#include "df/adventure_environment_place_in_it_containerst.h"
#include "df/adventure_option_talk_new_conversationst.h"
#include "df/adventure_option_talk_existing_conversationst.h"
#include "df/view_sheets_interfacest.h"
#include "df/adventure_movement_combat_menu_creaturest.h"
#include "df/adventure_movement_shoot_creaturest.h"
#include "df/adventure_optionst.h"
#include "df/adventure_item_interact_load_ranged_weaponst.h"
#include "df/adventure_item_interact_unnockst.h"
#include "df/adventurest.h"
#include "df/adventure_game_loop_type.h"
#include "df/body_part_raw.h"
#include "df/body_part_layer_raw.h"
#include "df/body_part_status.h"
#include "df/adventure_interface_aim_projectilest.h"
#include "df/body_part_raw_flags.h"
#include "df/caste_body_info.h"
#include "df/gait_info.h"
#include "df/gait_type.h"
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
#include "df/popup_message.h"
#include "df/report.h"
#include "df/announcement_type.h"
#include "df/init.h"
#include "df/tile_building_occ.h"
#include "df/tile_occupancy.h"
#include "df/tiletype.h"
#include "df/tiletype_material.h"
#include "df/tiletype_shape.h"
#include "df/tiletype_special.h"
#include "df/conflict_level.h"
#include "df/world.h"
#include "df/adv_announcementst.h"
#include "df/world_data.h"
#include "df/region_map_entry.h"
#include "df/region_weather.h"
#include "df/descriptor_color.h"
#include "df/material.h"
#include "df/world_site.h"
#include "df/abstract_building.h"
#include "df/agreement.h"
#include "df/agreement_details.h"
#include "df/agreement_details_data_citizenship.h"
#include "df/agreement_details_data_join_party.h"
#include "df/agreement_details_data_location.h"
#include "df/agreement_details_data_offer_service.h"
#include "df/agreement_details_data_parley.h"
#include "df/agreement_details_data_plot_abduct.h"
#include "df/agreement_details_data_plot_assassination.h"
#include "df/agreement_details_data_plot_steal_artifact.h"
#include "df/agreement_details_data_residency.h"
#include "df/agreement_details_data_retrieve_artifact.h"
#include "df/agreement_party.h"
#include "df/artifact_record.h"
#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/historical_figure_info.h"
#include "df/histfig_entity_link.h"
#include "df/histfig_hf_link.h"
#include "df/histfig_site_link.h"
#include "df/knowledge_profilest.h"
#include "df/site_reputation_info.h"
#include "df/site_reputation_report.h"
#include "df/state_profilest.h"
#include "df/world_region.h"
#include "df/main_hover_instruction.h"
#include "df/biome_type.h"
#include "df/region_map_entry_flags.h"
#include "df/entity_event.h"
#include "df/creature_knowledgest.h"
#include "df/caste_raw.h"
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
#include "df/viewscreen_adventure_logst.h"
#include "df/site_entryst.h"
#include "df/adventure_log_mode.h"

#include "media.h"
#include "render.h"
#include "render3d.h"
#include "gpu.h"
#include "scene.h"
#include "farland.h"
#include "sky.h"
#include "sound.h"

using std::string;
using std::vector;

using namespace DFHack;

DFHACK_PLUGIN("fpview");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(adventure);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(cur_year);
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
using fpv::EYE_RIDING;
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
decltype(&SDL_SetTextureAlphaMod) sdl_set_texture_alpha_mod = nullptr;   // optional: bars fade in and out
fpv::GpuApi gpu_api;   // what the renderer on the graphics card needs; not there in an old SDL
fpv::SoundApi sound_api;   // SDL's audio, for thunder; not needed for anything else
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
    sdl_set_texture_alpha_mod =
        reinterpret_cast<decltype(sdl_set_texture_alpha_mod)>(LookupPlugin(handle, "SDL_SetTextureAlphaMod"));
#define BIND_SOUND(name, target) target = reinterpret_cast<decltype(target)>(LookupPlugin(handle, #name));
    BIND_SOUND(SDL_WasInit, sound_api.was_init);
    BIND_SOUND(SDL_InitSubSystem, sound_api.init_subsystem);
    BIND_SOUND(SDL_QuitSubSystem, sound_api.quit_subsystem);
    BIND_SOUND(SDL_RWFromFile, sound_api.rw_from_file);
    BIND_SOUND(SDL_LoadWAV_RW, sound_api.load_wav_rw);
    BIND_SOUND(SDL_FreeWAV, sound_api.free_wav);
    BIND_SOUND(SDL_OpenAudioDevice, sound_api.open_audio_device);
    BIND_SOUND(SDL_CloseAudioDevice, sound_api.close_audio_device);
    BIND_SOUND(SDL_PauseAudioDevice, sound_api.pause_audio_device);
    BIND_SOUND(SDL_QueueAudio, sound_api.queue_audio);
    BIND_SOUND(SDL_MixAudioFormat, sound_api.mix_audio_format);
    BIND_SOUND(SDL_GetError, sound_api.get_error);
#undef BIND_SOUND
    gpu_api.create_texture = sdl_create_texture;
    gpu_api.destroy_texture = sdl_destroy_texture;
    gpu_api.update_texture = sdl_update_texture;
    gpu_api.set_texture_blend_mode = sdl_set_texture_blend_mode;
    gpu_api.set_texture_scale_mode = sdl_set_texture_scale_mode;
    gpu_api.render_geometry =
        reinterpret_cast<decltype(gpu_api.render_geometry)>(LookupPlugin(handle, "SDL_RenderGeometry"));
    gpu_api.get_renderer_info =
        reinterpret_cast<decltype(gpu_api.get_renderer_info)>(LookupPlugin(handle, "SDL_GetRendererInfo"));
    sdl_bound = true;
    return true;
}

// ---------------------------------------------------------------------------
// settings (set from the console or by the mod's Lua script)

struct Settings {
    std::atomic<float> fov{120};
    std::atomic<float> resolution{1.0f};      // fraction of the window resolution
    std::atomic<float> view_distance{48};
    std::atomic<float> model_lines{1};        // width of the lines along the edges of models, in game pixels (0: none)
    std::atomic<int> range{2};                // map windows read in each direction (1 = just the middle one)
    std::atomic<int> max_height{24};        // levels drawn above you
    std::atomic<bool> minimap{true};
    std::atomic<bool> minimap_ascii{false};
    std::atomic<bool> enemy_list{true};       // the row of foes in the top right corner
    std::atomic<int> minimap_tiles{11};       // tiles across the minimap (its zoom)
    std::atomic<bool> wasd{true};             // w a s d walk and strafe
    std::atomic<bool> mouse_look{true};       // the mouse turns the camera
    std::atomic<bool> combat_menus{true};     // paperdoll and compact lists instead of the game's attack lists
    std::atomic<bool> radial_menu{true};      // the game's bottom buttons taken off; their actions on the Tab ring
    std::atomic<bool> journal_panel{true};    // our journal panel (both names, where things are, links) instead of the game's journal
    std::atomic<bool> inventory_panel{true};  // our panel (paperdoll, squares, drag and drop) instead of the game's inventory
    std::atomic<bool> game_tips{true};        // the game's own hover box, for the tile the pointer is on
    std::atomic<bool> body_doll{true};        // the adventurer's paperdoll in place of the portrait
    std::atomic<bool> status_bars{true};      // bars for blood, pain, hunger ... under and above the doll
    std::atomic<bool> any_conflict_discs{false};   // discs and colours for conflicts between others too
    std::atomic<bool> auto_attack{false};     // walking into a foe strikes it (the game's way)
    std::atomic<int> max_depth{8};          // levels drawn below you
    std::atomic<bool> far_land{true};         // the land beyond the loaded map, from the world map
    std::atomic<float> horizon{20000};        // tiles: how far the far land reaches (and the haze over it)
    std::atomic<float> far_trees{1400};       // tiles: how far stand-ins for trees are drawn on it
    std::atomic<bool> relative_controls{true};
    std::atomic<bool> smooth{true};
    std::atomic<bool> compass{true};
    std::atomic<bool> compass_markers{true};  // the nearby places on the compass
    std::atomic<bool> cursor_hint{true};      // our line naming the thing under the pointer
    std::atomic<int> realtime_rate{5};        // game ticks per second while time runs by itself
    std::atomic<bool> hardware_renderer{true};   // draw through the graphics card: the view tilts for real (gpu.h)
} settings;

// ---------------------------------------------------------------------------
// shared state

std::atomic<bool> active{false};        // the user wants the view on
std::atomic<bool> showing{false};       // ... and the game is in a state where it can be shown
// The creature the view was on when it went off by itself rather than by the
// player's command (sleeping or resting takes the adventurer away for a while,
// and can reload the map). When that creature is controlled again, alive, the
// view comes back. -1: nothing to bring back.
int resume_unit = -1;
int last_view_unit = -1;                // the controlled creature while the view was on
// The same creature as a historical figure: while travelling the adventurer's
// unit is not to be found, and the view did not come back after travel (seen by
// the owner, 2026-11-15); the figure stays the same.
int32_t last_view_hf = -1, resume_hf = -1;
std::atomic<bool> doll_only{false};     // a screen that needs the map is open: only the paperdoll stays
std::atomic<int64_t> last_capture_ms{0}; // when the adventure screen last drew itself
// The adventure screen's game tick: running now, and when it last ended. A
// long tick (the game loading a new part of the world as you walk) keeps the
// screen from drawing for a while; that is not another screen taking over.
std::atomic<bool> in_game_tick{false};
std::atomic<int64_t> last_tick_ms{0};
std::atomic<int> heading{0};            // 0..7, N NE E SE S SW W NW
std::atomic<bool> realtime{false};      // the timer button is on: time passes by itself
bool space_stopped = false;             // Space turned the timer off; that press does nothing else

fpv::Scene scene;
fpv::FarLand farland;   // the land beyond the loaded map; worked out off the game's thread
std::unique_ptr<fpv::Renderer> renderer;
std::unique_ptr<fpv::GpuRenderer> gpu;          // the renderer on the graphics card, made when first used
std::atomic<bool> drew_3d{false};               // the last frame came from it
std::atomic<uint32_t> stat_gpu_tris{0};
std::atomic<bool> gpu_refused{false};           // SDL would not draw its triangles: the software renderer from then on

// where the camera wants to be; written by the simulation thread
std::mutex target_mutex;
struct Target {
    float x = 0, y = 0, eye = 0;
    int zlo = 0, zhi = -1;
    float daylight = 1;
    uint32_t fog = 0xFF0C0908;
    bool underground = false;   // no sky, no weather
    bool outside = false;       // the adventurer's tile is open to the sky (lightning is seen and heard)
    fpv::Weather weather;       // the game's (or the one picked to test)
    int weather_pick = 0;       // which one was picked to test, 0 = the game's
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
// One of the game's windows (naming an item, say) reaches into the corner
// under the minimap: the minimap and its buttons stay off it (hide_interface_under_minimap).
std::atomic<bool> corner_covered{false};

void publish_state(bool view, bool minimap) {
    const bool timer = view && realtime;
    const bool covered = view && corner_covered;
    minimap = minimap && !covered;
    const int state = (view ? 1 : 0) | (minimap ? 2 : 0) | (timer ? 4 : 0) | (covered ? 8 : 0);
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
    lua_pushboolean(L, covered);
    lua_setglobal(L, "fpview_corner_covered");
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
std::atomic<bool> shift_held{false};      // Shift is down
std::atomic<bool> space_down{false};      // the space bar is down
std::atomic<bool> debug_look{false};      // testing aid: draw the crosshair even without the mouse held
std::atomic<bool> arrow_nocked{false};    // the ranged weapon in hand has an arrow on the string (a bolt loaded)
std::atomic<bool> weapon_loads{false};    // that weapon is loaded like a crossbow, not strung like a bow
std::mutex ammo_mutex;
std::string ammo_word;                    // what a loaded weapon shoots ("bolt"), for the intent's name
std::atomic<bool> attack_busy{false};     // an attack started with a click is being carried through

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
std::atomic<uint32_t> stat_map_shifts{0};      // times the game moved its loaded part of the world and the scene moved along
std::atomic<uint64_t> stat_frames{0};
std::atomic<int> stat_w{0}, stat_h{0};

// Where frame time goes, averaged and at its worst since "fpview status" last
// showed it (the worst frame is what is seen as a stutter).
struct Probe {
    std::atomic<uint64_t> sum{0};
    std::atomic<uint32_t> n{0}, peak{0};
    void add(uint32_t us) {
        sum += us;
        ++n;
        uint32_t p = peak.load();
        while (us > p && !peak.compare_exchange_weak(p, us)) {
        }
    }
    std::string take() {
        const uint32_t count = n.exchange(0);
        const uint64_t total = sum.exchange(0);
        const uint32_t worst = peak.exchange(0);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.2f ms avg, %.1f ms worst (%u)", count ? total / 1000.0 / count : 0.0,
                      worst / 1000.0, count);
        return buf;
    }
};
// one per frame: the whole capture, the game's own draws in it, map reads,
// creatures; the render thread: its wait for the scene, its whole frame
Probe probe_capture, probe_game_draws, probe_reads, probe_units, probe_scene_wait, probe_frame;
Probe probe_logic_ours, probe_logic_game;   // per game tick: our part of logic, the game's
std::atomic<uint32_t> probe_draw_count{0};   // game draws in the last capture
// testing aid: long frames on either thread and the move keys, with times
std::mutex hitch_mutex;
std::vector<std::string> hitch_log;
std::atomic<bool> hitch_on{false};       // "fpview hitches on" starts the log
void hitch(const std::string &what) {
    if (!hitch_on)
        return;
    const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count() %
                       1000000;
    std::lock_guard<std::mutex> lock(hitch_mutex);
    if (hitch_log.size() > 400)
        hitch_log.erase(hitch_log.begin(), hitch_log.begin() + 200);
    hitch_log.push_back(std::to_string(ms) + " " + what);
}
struct ProbeTimer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    uint32_t us() const {
        return uint32_t(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    }
};

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
bool plain_focus(const std::vector<std::string> &focus) {
    if (focus.empty())
        return false;
    for (const std::string &f : focus)
        if (f != "dungeonmode/Default" && f != "dungeonmode/ADVENTURE_ARENA")
            return false;
    return true;
}

bool plain_adventure_focus() {
    return plain_focus(Gui::getCurFocus(true));
}

// The same with a DFHack window over it (the mod's settings, gui/launcher):
// the game's screen under it is still the plain one, and what the mod does to
// its interface (hidden corners, moved buttons, the message list) stays.
bool plain_adventure_under() {
    return plain_focus(Gui::getFocusStrings(Gui::getDFViewscreen(true)));
}

float daylight_now() {
    // 1200 ticks per day; fade around dawn (5-7h) and dusk (18-20h)
    const float hour = float(*cur_year_tick % 1200) / 50.0f;
    const auto ramp = [](float a, float b, float x) { return std::clamp((x - a) / (b - a), 0.0f, 1.0f); };
    return std::min(ramp(5, 7, hour), 1 - ramp(18, 20, hour));
}

// ---------------------------------------------------------------------------
// weather

// The weather can be picked from a list for testing (the slider in the
// settings); 0 is the game's own. The names are shown by the Lua script.
std::atomic<int> debug_weather{0};
const char *const WEATHER_NAMES[] = {"None (the game's weather)", "Clear", "Cumulus", "Many cumulus", "Cirrus",
                                     "Altostratus", "Overcast", "Mist", "Fog", "Thick fog", "Rain", "Storm",
                                     "Snow", "Blood rain", "Evil cloud"};
constexpr int WEATHER_PICKS = int(sizeof(WEATHER_NAMES) / sizeof(WEATHER_NAMES[0]));

// the colour of a material in a state (liquid for rain, gas for a cloud)
uint32_t material_color(int16_t type, int32_t index, df::matter_state state, uint32_t fallback) {
    MaterialInfo mi(type, index);
    if (!mi.isValid() || !mi.material)
        return fallback;
    const int32_t c = mi.material->state_color[state];
    const auto &colors = world->raws.descriptors.colors;
    if (c < 0 || size_t(c) >= colors.size() || !colors[c])
        return fallback;
    const df::descriptor_color *dc = colors[c];
    return fpv::rgb(std::clamp(int(dc->red * 255), 0, 255), std::clamp(int(dc->green * 255), 0, 255),
                    std::clamp(int(dc->blue * 255), 0, 255));
}

// simulation thread: the sky and the weather over a tile
fpv::Weather read_weather(int x, int y, int z) {
    fpv::Weather w;
    const int32_t tick = *cur_year_tick;
    w.hour = float(tick % 1200) / 50.0f;
    w.day = (tick / 1200) % 336;
    df::world_data *wd = world->world_data;
    df::coord2d rgn(-1, -1);
    if (Maps::isValidTilePos(x, y, z))
        rgn = Maps::getTileBiomeRgn(df::coord(x, y, z));
    if (wd) {
        // the moon's angle from the sun, in hundredths of a degree; its phase (0..27) if that is off
        if (wd->moon_angle >= 0 && wd->moon_angle < 36000)
            w.moon = wd->moon_angle / 36000.0f;
        else
            w.moon = float(((wd->moon_phase % 28) + 28) % 28) / 28.0f;
        w.rise_west = wd->flip_longitude == df::sunrise_type::West;
        // with only a south pole the world lies south of the equator, and the sun passes north
        if (wd->flip_latitude == df::pole_type::South)
            w.sun_north = true;
        else if (wd->flip_latitude == df::pole_type::Both)
            w.sun_north = rgn.y >= 0 && rgn.y >= wd->world_height / 2;
    }
    if (rgn.x >= 0) {
        if (df::region_map_entry *e = Maps::getRegionBiome(rgn)) {
            const auto &cl = e->clouds.bits;
            switch (int(cl.cumulus)) {
            case 1: w.cloud[fpv::CLOUD_CUMULUS] = 1; break;
            case 2: w.cloud[fpv::CLOUD_CUMULUS_MANY] = 1; break;
            case 3:
                w.cloud[fpv::CLOUD_NIMBUS] = 1;
                w.cloud[fpv::CLOUD_CUMULUS_MANY] = 0.5f;
                break;
            default: break;
            }
            if (cl.cirrus)
                w.cloud[fpv::CLOUD_CIRRUS] = 1;
            switch (int(cl.stratus)) {
            case 1: w.cloud[fpv::CLOUD_ALTOSTRATUS] = 1; break;
            case 2: w.cloud[fpv::CLOUD_STRATUS] = 1; break;
            case 3:
                w.cloud[fpv::CLOUD_STRATUS] = 1;
                w.cloud[fpv::CLOUD_NIMBUS] = std::max(w.cloud[fpv::CLOUD_NIMBUS], 0.8f);
                break;
            default: break;
            }
            w.fog = int(cl.fog) / 3.0f;
            // the wind of the morning; the clouds drift with it
            const auto &wb = e->wind.bits;
            float wx = float((wb.east_1 || wb.east_2) - (wb.west_1 || wb.west_2));
            float wy = float((wb.south_1 || wb.south_2) - (wb.north_1 || wb.north_2));
            if (wx == 0 && wy == 0) {
                wx = 1;
                wy = 0.3f;
            }
            const float l = std::sqrt(wx * wx + wy * wy);
            w.wind_x = wx / l;
            w.wind_y = wy / l;
        }
        // rain or snow: the game keeps it for the 5 x 5 world tiles around the map
        if (df::global::current_weather) {
            const int wx = rgn.x - world->map.region_x / 16 + 1, wy = rgn.y - world->map.region_y / 16 + 1;
            if (wx >= 0 && wy >= 0 && wx < 5 && wy < 5) {
                const df::weather_type ww = (*df::global::current_weather)[wx][wy];
                if (ww == df::weather_type::Rain) {
                    w.rain = 1;
                    w.cloud[fpv::CLOUD_STRATUS] = std::max(w.cloud[fpv::CLOUD_STRATUS], 1.0f);
                    w.cloud[fpv::CLOUD_NIMBUS] = std::max(w.cloud[fpv::CLOUD_NIMBUS], 0.6f);
                } else if (ww == df::weather_type::Snow) {
                    w.snow = 1;
                    w.cloud[fpv::CLOUD_STRATUS] = std::max(w.cloud[fpv::CLOUD_STRATUS], 1.0f);
                }
            }
        }
    }
    // evil weather: a rain of something (blood) over this world tile, a cloud creeping nearby
    if (wd) {
        const int gx = world->map.region_x * 48 + x, gy = world->map.region_y * 48 + y;
        for (const df::region_weather *rw : wd->region_weather) {
            if (!rw)
                continue;
            if (rw->type == df::region_weather_type::FallingMaterial) {
                if (rw->region_x == rgn.x && rw->region_y == rgn.y) {
                    w.rain = std::max(w.rain, 0.7f);
                    w.rain_color = material_color(rw->mat_type, rw->mat_index, df::matter_state::Liquid,
                                                  fpv::rgb(150, 20, 20));
                    w.cloud[fpv::CLOUD_STRATUS] = std::max(w.cloud[fpv::CLOUD_STRATUS], 1.0f);
                }
            } else if (rw->type != df::region_weather_type::NONE && rw->world_in_game_x > -30000) {
                const float dx = float(rw->world_in_game_x - gx), dy = float(rw->world_in_game_y - gy);
                const float near = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy) / 48.0f, 0.0f, 1.0f);
                if (near > w.evil) {
                    const df::matter_state st = rw->type == df::region_weather_type::CreepingDust
                                                    ? df::matter_state::Powder
                                                    : rw->type == df::region_weather_type::CreepingVapor
                                                          ? df::matter_state::Gas
                                                          : df::matter_state::Gas;
                    w.evil = near;
                    w.evil_color = material_color(rw->mat_type, rw->mat_index, st, fpv::rgb(90, 140, 60));
                }
            }
        }
        // the creeping cloud is the sky's cloud cover, dark and of its stuff (Sky::prepare colours it)
        if (w.evil > 0)
            w.cloud[fpv::CLOUD_STRATUS] = std::max(w.cloud[fpv::CLOUD_STRATUS], std::min(1.0f, w.evil * 2));
    }
    return w;
}

// the weather picked for testing over the game's time of day and moon
void pick_weather(fpv::Weather &w, int pick) {
    if (pick <= 0 || pick >= WEATHER_PICKS)
        return;
    for (float &c : w.cloud)
        c = 0;
    w.fog = w.rain = w.snow = w.evil = 0;
    w.rain_color = w.evil_color = 0;
    switch (pick) {
    case 2: w.cloud[fpv::CLOUD_CUMULUS] = 1; break;
    case 3: w.cloud[fpv::CLOUD_CUMULUS_MANY] = 1; break;
    case 4: w.cloud[fpv::CLOUD_CIRRUS] = 1; break;
    case 5: w.cloud[fpv::CLOUD_ALTOSTRATUS] = 1; break;
    case 6: w.cloud[fpv::CLOUD_STRATUS] = 1; break;
    case 7: w.fog = 1 / 3.0f; break;
    case 8: w.fog = 2 / 3.0f; break;
    case 9: w.fog = 1; break;
    case 10:
        w.cloud[fpv::CLOUD_STRATUS] = 1;
        w.cloud[fpv::CLOUD_NIMBUS] = 0.6f;
        w.rain = 0.6f;
        break;
    case 11:
        w.cloud[fpv::CLOUD_NIMBUS] = 1;
        w.cloud[fpv::CLOUD_STRATUS] = 1;
        w.rain = 1;
        break;
    case 12:
        w.cloud[fpv::CLOUD_STRATUS] = 1;
        w.snow = 1;
        break;
    case 13:
        w.cloud[fpv::CLOUD_STRATUS] = 1;
        w.rain = 0.7f;
        w.rain_color = fpv::rgb(150, 20, 20);
        break;
    case 14:
        w.cloud[fpv::CLOUD_CUMULUS_MANY] = 0.6f;
        w.cloud[fpv::CLOUD_STRATUS] = 1;
        w.evil = 0.8f;
        w.evil_color = fpv::rgb(90, 140, 60);
        break;
    default: break;   // 1: clear
    }
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
std::atomic<int> doll_rows{8};           // interface rows the doll and the bars under it cover, from the bottom
std::atomic<int> passing_rows{0};        // and the passing bars above the bars' panel
std::atomic<int> doll_cols{12};          // interface columns the doll's panel covers, from the left
std::atomic<int> bars_cols{0};           // and the bars' panel, from the right (0: none)
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
// (move_bottom_buttons). The row of buttons right of the portrait goes up
// into the row of "View your companions", right after the mod's camera and
// timer (from column BUTTONS_LEFT; the mod's travel and sleep buttons follow
// them). The speed display ("Walk") after those buttons is taken off the
// screen: what it says is read and shown at the top of the message list
// (speed_text). The action bar (hand, look, talk ...),
// normally centred, takes their place at the bottom right of the portrait
// (from column BAR_LEFT). What the game drew is moved; the game still takes
// the buttons to be where it drew them, so the pointer over a moved block is
// handed to the game at the same spot of the original (bar_source).
constexpr int BAR_LEFT = 12;       // right of the portrait (BODY_COLS in menu.inc)
constexpr int BUTTONS_LEFT = 27;   // right of the timer (TimeButton in first-person.lua: x=21, 7 cells wide)
constexpr int BUTTON_ROWS = 3;     // a row of the game's 4 x 3 buttons
constexpr int BUTTON_COLS = 24;    // six of them; the speed display comes after

// what the game's speed display says, its lines joined ("Walk: ...")
std::mutex speed_mutex;
std::string speed_text;

struct MovedBlock {
    std::atomic<bool> live{false};
    std::atomic<int> x0{0}, x1{-1}, y0{0}, y1{-1};   // where it is shown, in interface cells
    std::atomic<int> dx{0}, dy{0};                   // where the game drew it, from there
};
MovedBlock moved_bar, moved_buttons;

// While the game's auto-move question is up (hide_action_prompt) the game
// draws it instead of the action bar. The bar is then shown as it last was, so
// that it and the message panel beside it stay put; it does nothing then (what
// the game has at the bar's place is the hidden question), so the pointer is
// not handed to the game there and clicks on it are dropped.
std::atomic<bool> bar_frozen{false};
// With the ring (settings.radial_menu): the message panel is shown, on a game
// dialog too, unless the dialog reaches into the panel's place.
std::atomic<bool> msg_ok{false};

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
        if (b == &moved_bar && bar_frozen.load())
            return false;
        opx = px + b->dx.load() * r->dispx_z;
        opy = py + b->dy.load() * r->dispy_z;
        return true;
    }
    return false;
}

// on the action bar while it is only shown as it last was (see bar_frozen)
bool in_frozen_bar(int px, int py) {
    int cx, cy;
    if (!bar_frozen.load() || !moved_bar.live.load() || !interface_cell(px, py, cx, cy))
        return false;
    return cx >= moved_bar.x0.load() && cx <= moved_bar.x1.load() && cy >= moved_bar.y0.load() &&
           cy <= moved_bar.y1.load();
}

bool in_messages(int px, int py);   // the message panel (menu.inc)

// The rectangle a panel of ours (journal, map) covers, window pixels, with its
// tabs; x1 < x0 when none. Under it the game is shown no pointer and nothing in
// the view is highlighted, as under the game's own windows.
std::atomic<int> cover_x0{0}, cover_y0{0}, cover_x1{-1}, cover_y1{-1};

bool in_cover(int px, int py) {
    return px >= cover_x0.load() && px < cover_x1.load() && py >= cover_y0.load() && py < cover_y1.load();
}

bool over_interface(int px, int py) {
    if (in_cover(px, py))
        return true;
    int cx, cy;
    if (!interface_cell(px, py, cx, cy))
        return true;
    if (in_menu_panel(px, py) || in_messages(px, py))
        return true;
    // the doll's square and the bars' square (with the passing bars over it)
    // belong to the interface while our doll is drawn
    if (portrait_ours && ((cx < doll_cols.load() && cy >= gps->dimy - doll_rows.load()) ||
                          (cx >= gps->dimx - bars_cols.load() &&
                           cy >= gps->dimy - doll_rows.load() - passing_rows.load())))
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

// the game has something drawn in the interface cell at these window pixels
// (one of its windows lying over our panels: the click is the game's)
bool game_cell_at(int px, int py) {
    int cx, cy;
    if (!gps || !interface_cell(px, py, cx, cy) || cx < 0 || cy < 0 || cx >= gps->dimx || cy >= gps->dimy)
        return false;
    return grid_cell_drawn(size_t(cx) * gps->dimy + cy);
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
    if (drew_3d && gpu)
        return gpu->pick(scene, (px + 0.5f) / ow, (py + 0.5f) / oh);
    const int x = int(int64_t(px) * stat_w.load() / ow), y = int(int64_t(py) * stat_h.load() / oh);
    if (!renderer)
        return p;
    return renderer->pick(x, y);
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
bool corner_rect(SDL_Rect &rect) {
    if (!gps || !enabler || !enabler->renderer)
        return false;
    const auto r = static_cast<df::renderer_2d_base *>(enabler->renderer);
    if (r->dispx_z <= 0 || r->dispy_z <= 0 || gps->dimx < MINI_COLS + 8)
        return false;
    const int side = MINI_COLS * r->dispx_z;
    // in the top right corner, over the game's date line (hidden: the date is in the message list)
    rect = {r->origin_x + (gps->dimx - MINI_COLS) * r->dispx_z - 3, r->origin_y + 4, side, side};
    return true;
}

bool minimap_rect(SDL_Rect &rect) {
    return settings.minimap && !corner_covered && corner_rect(rect);
}

// The creatures in view that are in a conflict with you above training: a row
// of their pictures right under the minimap, each as big as a tile of the
// minimap at its default zoom. A click on one turns the camera to it. The list
// is made on the simulation thread (collect_foes).
struct Foe {
    int32_t unit_id;
    float x, y, h;           // its middle: map tiles, height in tile widths
    uint32_t color;          // of the conflict's level
    uint32_t px[32 * 32];    // its picture
};
std::mutex foe_mutex;
std::vector<Foe> foes;
std::atomic<int> foe_count{0};   // how many there are
std::atomic<int> foe_scroll{0};  // the first one shown when they do not all fit
std::atomic<int> foe_aim{-1};    // the creature a click asked the camera to turn to
SDL_Texture *foe_texture = nullptr;
SDL_Texture *arrow_texture = nullptr;   // the minimap's arrow (media minimap_arrow.png)

// where the row and its parts are, in window pixels: shared by drawing and clicking
struct FoeStrip {
    SDL_Rect area{};
    int size = 0, step = 0, x0 = 0;
    int first = 0, shown = 0;
    bool more_left = false, more_right = false;   // scroll arrows show
    SDL_Rect left{}, right{};
    SDL_Rect slot(int i) const { return {x0 + i * step, area.y, size, size}; }
};

bool foe_strip(FoeStrip &s) {
    const int total = foe_count.load();
    SDL_Rect m;
    if (total <= 0 || !settings.enemy_list || !corner_rect(m))
        return false;
    constexpr int GAP = 3;
    s.size = std::max(8, m.w / 11);
    s.step = s.size + GAP;
    int top = m.y + m.h + 6;
    if (!settings.minimap) {
        // no minimap: along the top, left of the gear button in the corner (4 interface cells)
        top = m.y;
        m.w -= 4 * static_cast<df::renderer_2d_base *>(enabler->renderer)->dispx_z + 4;
    }
    s.area = {m.x, top, m.w, s.size};
    s.x0 = m.x;
    s.first = 0;
    s.shown = total;
    s.more_left = s.more_right = false;
    if (total > std::max(1, (m.w + GAP) / s.step)) {
        // too many: room for an arrow at each end
        const int aw = s.size * 2 / 3;
        s.shown = std::max(1, (m.w - 2 * (aw + GAP) + GAP) / s.step);
        s.first = std::clamp(foe_scroll.load(), 0, total - s.shown);
        s.more_left = s.first > 0;
        s.more_right = s.first + s.shown < total;
        s.left = {m.x, s.area.y, aw, s.size};
        s.right = {m.x + m.w - aw, s.area.y, aw, s.size};
        s.x0 = m.x + aw + GAP;
    }
    return true;
}

bool in_foe_strip(int px, int py) {
    FoeStrip s;
    return foe_strip(s) && px >= s.area.x - 2 && px < s.area.x + s.area.w + 2 && py >= s.area.y - 2 &&
           py < s.area.y + s.area.h + 2;
}

// a click on the row: scroll it, or have the camera turn to the creature
bool foes_click(int px, int py) {
    FoeStrip s;
    if (!in_foe_strip(px, py) || !foe_strip(s))
        return false;
    const auto in = [&](const SDL_Rect &r) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; };
    if (s.more_left && in(s.left)) {
        foe_scroll = s.first - 1;
    } else if (s.more_right && in(s.right)) {
        foe_scroll = s.first + 1;
    } else {
        for (int i = 0; i < s.shown; ++i)
            if (in(s.slot(i))) {
                std::lock_guard<std::mutex> lock(foe_mutex);
                if (size_t(s.first + i) < foes.size())
                    foe_aim = foes[s.first + i].unit_id;
                break;
            }
    }
    return true;
}

// the creature whose picture in the row is at these window pixels, -1 if none
int foe_at(int px, int py) {
    FoeStrip s;
    if (!in_foe_strip(px, py) || !foe_strip(s))
        return -1;
    for (int i = 0; i < s.shown; ++i) {
        const SDL_Rect r = s.slot(i);
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
            std::lock_guard<std::mutex> lock(foe_mutex);
            return size_t(s.first + i) < foes.size() ? foes[s.first + i].unit_id : -1;
        }
    }
    return -1;
}

// A creature's picture for the row: its portrait if the game has made one,
// else the piece of its own picture that holds its body (what it holds out
// reaches into other pieces: with those it would be small and off to a side).
bool foe_picture(const df::unit *u, uint32_t *out) {
    if (u->portrait_texpos > 0 && fpv::TextureStore::readSmooth(u->portrait_texpos, out))
        return true;
    uint32_t raw[32 * 32];
    int most = 0;
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 2; ++y) {
            if (!u->texpos_currently_in_use[x][y] || u->texpos[x][y] <= 0 ||
                !fpv::TextureStore::readSprite(u->texpos[x][y], raw))
                continue;
            int solid = 0;
            for (const uint32_t c : raw)
                solid += fpv::px_a(c) >= 150;
            if (solid > most) {
                most = solid;
                std::memcpy(out, raw, sizeof(raw));
            }
        }
    return most > 0;
}

// simulation thread, with the scene locked, after its creatures were made
void collect_foes() {
    std::vector<Foe> list;
    for (const fpv::Billboard &b : scene.units) {
        if (b.unit_id < 0 || b.foe <= int(df::conflict_level::Training) || !b.sprite)
            continue;
        const fpv::Sprite &sp = *b.sprite;
        if (sp.w < 1 || sp.h < 1 || sp.px.size() < size_t(sp.w) * sp.h)
            continue;
        Foe f{};
        f.unit_id = b.unit_id;
        f.x = b.x;
        f.y = b.y;
        f.h = b.base + 0.38f;
        f.color = b.ring;
        const df::unit *u = df::unit::find(b.unit_id);
        if (!u || !foe_picture(u, f.px)) {
            // a creature without a picture of its own: what the view shows of
            // it, its middle in the middle; a larger one is fitted in
            const int bw = sp.x1 - sp.x0, bh = sp.y1 - sp.y0;
            const int side = std::max({bw, bh, 32});
            const int ox = (side - bw) / 2 - sp.x0, oy = side - bh - sp.y0;
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) {
                    const int sx = x * side / 32 - ox, sy = y * side / 32 - oy;
                    if (sx >= 0 && sy >= 0 && sx < sp.w && sy < sp.h)
                        f.px[y * 32 + x] = sp.px[size_t(sy) * sp.w + sx];
                }
        }
        list.push_back(f);
    }
    // in an order that does not change while they move about
    std::sort(list.begin(), list.end(), [](const Foe &a, const Foe &b) { return a.unit_id < b.unit_id; });
    std::lock_guard<std::mutex> lock(foe_mutex);
    foes.swap(list);
    foe_count = int(foes.size());
}

bool in_minimap(int px, int py) {
    if (in_foe_strip(px, py))
        return true;   // the row of foes is the mod's too, with or without the minimap
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

// name of exactly the thing a pick points at; terrain false: nothing for the
// ground or a wall
std::string describe(const fpv::Pick &p, bool terrain = true) {
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
    return terrain ? describe_terrain(pos) : std::string();
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
        const bool mini = in_minimap(px, py) || in_corner(px, py) || in_frozen_bar(px, py) || in_cover(px, py);
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

// the same at a given interface cell (the left end of the line)
void paint_hint_at(const std::string &text, int x0, int y) {
    if (text.empty() || gps->dimx < 20 || gps->dimy < 12)
        return;
    const std::string line = " " + text.substr(0, size_t(std::max(0, gps->dimx - 4))) + " ";
    x0 = std::clamp(x0, 0, std::max(0, gps->dimx - int(line.size())));
    y = std::clamp(y, 0, gps->dimy - 1);
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
// Every per-tile array of a viewport, by slot: fn(pointer to the array's
// pointer, size of one element). The same order every time, missing ones too.
#define SLOT(name) fn(reinterpret_cast<void **>(&vp->name), sizeof(*vp->name));
template <typename Fn>
void viewport_slots(df::graphic_viewportst *vp, Fn &&fn) {
    SLOT(screentexpos_background)
    SLOT(screentexpos_floor_flag)
    SLOT(screentexpos_background_two)
    SLOT(screentexpos_liquid_flag)
    SLOT(screentexpos_spatter_flag)
    SLOT(screentexpos_spatter)
    SLOT(screentexpos_ramp_flag)
    SLOT(screentexpos_shadow_flag)
    SLOT(screentexpos_building_one)
    SLOT(screentexpos_item)
    SLOT(screentexpos_vehicle)
    SLOT(screentexpos_vermin)
    SLOT(screentexpos_left_creature)
    SLOT(screentexpos)
    SLOT(screentexpos_right_creature)
    SLOT(screentexpos_building_two)
    SLOT(screentexpos_projectile)
    SLOT(screentexpos_high_flow)
    SLOT(screentexpos_top_shadow)
    SLOT(screentexpos_signpost)
    SLOT(screentexpos_upleft_creature)
    SLOT(screentexpos_up_creature)
    SLOT(screentexpos_upright_creature)
    SLOT(screentexpos_designation)
    SLOT(screentexpos_interface)
    SLOT(core_tree_species_plus_one)
    SLOT(shadow_tree_species_plus_one)
}
template <typename Fn>
void viewport_old_slots(df::graphic_viewportst *vp, Fn &&fn) {
    SLOT(screentexpos_background_old)
    SLOT(screentexpos_floor_flag_old)
    SLOT(screentexpos_background_two_old)
    SLOT(screentexpos_liquid_flag_old)
    SLOT(screentexpos_spatter_flag_old)
    SLOT(screentexpos_spatter_old)
    SLOT(screentexpos_ramp_flag_old)
    SLOT(screentexpos_shadow_flag_old)
    SLOT(screentexpos_building_one_old)
    SLOT(screentexpos_item_old)
    SLOT(screentexpos_vehicle_old)
    SLOT(screentexpos_vermin_old)
    SLOT(screentexpos_left_creature_old)
    SLOT(screentexpos_old)
    SLOT(screentexpos_right_creature_old)
    SLOT(screentexpos_building_two_old)
    SLOT(screentexpos_projectile_old)
    SLOT(screentexpos_high_flow_old)
    SLOT(screentexpos_top_shadow_old)
    SLOT(screentexpos_signpost_old)
    SLOT(screentexpos_upleft_creature_old)
    SLOT(screentexpos_up_creature_old)
    SLOT(screentexpos_upright_creature_old)
    SLOT(screentexpos_designation_old)
    SLOT(screentexpos_interface_old)
}
#undef SLOT

// What the render thread draws the minimap from: a copy of the minimap's
// square, all its arrays, made on the simulation thread. The render thread
// does not read the game's map viewport itself: the game writes it while it
// draws (other levels, other windows, then blanked), and the render thread
// drawing it then made the minimap black now and then.
std::mutex mini_shot_mutex;
struct MiniShot {
    int n = 0;
    uint64_t hash = 0;
    std::vector<std::vector<uint8_t>> slots;   // n * n elements each, empty where the game has none
} mini_shot;

std::vector<std::vector<uint8_t>> mini_layers;
int mini_taken = 0;
// Frames since the copy was last taken. A frame whose map read is skipped (the
// game moved its window while drawing, or drew nothing new) takes none; the
// last copy is put back then, so the minimap does not blink out while walking.
int mini_kept = 0;
constexpr int MINI_KEEP = 60;
std::atomic<uint64_t> mini_hash{0};     // changes when the minimap's tiles do

// false when no new copy was taken; the last one is kept then (the caller
// counts such frames against MINI_KEEP)
bool minimap_take(df::graphic_viewportst *vp, int center_x, int center_y) {
    if (!settings.minimap || !vp) {
        mini_taken = 0;
        return false;
    }
    const int cols = vp->dim_x, rows = vp->dim_y;
    int n = std::min({mini_tiles(), cols, rows});
    if (n % 2 == 0)
        --n;
    if (n < 3) {
        mini_taken = 0;
        return false;
    }
    // The game does not draw its map anew every frame, and we empty it after
    // each one: in a frame it skipped, the viewport is still empty. Copied, that
    // turned the minimap black for a frame. The adventurer's own tile is always
    // drawn, so an empty one there means the game drew nothing.
    {
        if (center_x < 0 || center_y < 0 || center_x >= cols || center_y >= rows || !vp->screentexpos_background)
            return false;
        const size_t i = size_t(center_x) * rows + center_y;
        const uint32_t blank = uint32_t(gps->black_background_texpos[0]);
        const uint32_t a = uint32_t(vp->screentexpos_background[i]);
        const bool drawn = (a && a != blank) ||
                           (vp->screentexpos_background_two && vp->screentexpos_background_two[i]) ||
                           (vp->screentexpos_building_one && vp->screentexpos_building_one[i]);
        if (!drawn)
            return false;
    }
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
    {
        MiniShot shot;
        shot.n = n;
        shot.hash = hash;
        viewport_slots(vp, [&](void **slot, size_t size) {
            std::vector<uint8_t> copy;
            if (const auto *data = static_cast<const uint8_t *>(*slot)) {
                copy.resize(size_t(n) * n * size);
                for (int i = 0; i < n; ++i)
                    std::memcpy(&copy[size_t(i) * n * size], data + (size_t(x0 + i) * rows + y0) * size, size_t(n) * size);
            }
            shot.slots.push_back(std::move(copy));
        });
        std::lock_guard<std::mutex> lock(mini_shot_mutex);
        mini_shot = std::move(shot);
    }
    mini_hash = hash;
    mini_taken = n;
    mini_self = (center_x - x0) << 8 | (center_y - y0);
    return true;
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

// Take what the interface drew under the minimap off the screen. On the plain
// screen always (after hide_top_right); with one of the game's windows open
// over the view only while the cells there are the ones the plain screen had.
// Else the window reaches into the corner (naming an item does): it is left
// whole and the minimap is not drawn over it (corner_covered).
void hide_interface_under_minimap(bool plain, bool like_plain = false) {
    static std::vector<int> seen;
    static int seen_dimx = 0, seen_dimy = 0;
    SDL_Rect rect;
    if (!gps || !settings.minimap || !corner_rect(rect)) {
        corner_covered = false;
        return;
    }
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
    cx0 = std::max(0, cx0);
    cy0 = std::max(0, cy0);
    cy1 = std::min(gps->dimy - 1, cy1);
    std::vector<int> cells;
    for (int cx = cx0; cx < gps->dimx; ++cx)
        for (int cy = cy0; cy <= cy1; ++cy)
            if (grid_cell_drawn(size_t(cx) * gps->dimy + cy))
                cells.push_back(cx * gps->dimy + cy);
    if (plain) {
        seen = cells;
        seen_dimx = gps->dimx;
        seen_dimy = gps->dimy;
    } else if (!like_plain && (gps->dimx != seen_dimx || gps->dimy != seen_dimy || cells != seen)) {
        corner_covered = true;
        return;
    }
    corner_covered = false;
    for (int cx = cx0; cx < gps->dimx; ++cx)
        for (int cy = cy0; cy <= cy1; ++cy)
            clear_cell(cx, cy);
}

// Simulation thread: everything the game puts in the top right corner goes
// off the screen: the date line, the weather and the small buttons under it,
// and the box with the current quest (the date is in the message list). These
// do not all reach the right edge and have empty rows between them, so the
// corner is taken as a block: the last columns of the screen, from the top
// down to three empty rows in a row, each row on to the left as far as
// something is drawn in it.
//
// With one of the game's dialogs open over the view (plain false) the block
// only goes when it is cell for cell what it was on the plain screen: a
// dialog that reaches into the corner is left whole.
void hide_top_right(bool on, bool plain, bool like_plain = false) {
    static std::vector<int> seen;
    static int seen_dimx = 0, seen_dimy = 0;
    if (!gps || !on || gps->dimx < 80)
        return;
    const int dimx = gps->dimx, dimy = gps->dimy;
    const int left = dimx - MINI_COLS - 4;   // the quest box starts a few columns left of the minimap
    std::vector<int> cells;
    for (int cy = 0, empty = 0; cy < dimy / 2 && empty < 3; ++cy) {
        const size_t before = cells.size();
        for (int cx = left; cx < dimx; ++cx)
            if (grid_cell_drawn(size_t(cx) * dimy + cy))
                cells.push_back(cx * dimy + cy);
        empty = cells.size() > before ? 0 : empty + 1;
        for (int cx = left - 1; cx >= dimx / 2 + 13 && grid_cell_drawn(size_t(cx) * dimy + cy); --cx)
            cells.push_back(cx * dimy + cy);
    }
    if (plain) {
        seen = cells;
        seen_dimx = dimx;
        seen_dimy = dimy;
    } else if (!like_plain && (dimx != seen_dimx || dimy != seen_dimy || cells != seen)) {
        return;
    }
    for (const int idx : cells) {
        std::memset(&gps->screen[idx * 8], 0, 8);
        gps->screentexpos[idx] = 0;
        gps->screentexpos_lower[idx] = 0;
        gps->screentexpos_anchored[idx] = 0;
        gps->screentexpos_flag[idx] = 0;
    }
}

// Simulation thread, after the game's last draw of the frame and before
// anything at the bottom is changed: may the bottom of the screen be
// rearranged (hide_corner_buttons, move_bottom_buttons, the message list)?
// On the plain adventure screen always. With one of the game's dialogs open
// over the view, only while the pictures in the bottom rows (as many as the
// message list is tall) are where they were on the plain screen: then the
// dialog does not reach down there.
bool bottom_is_plain(bool on, bool plain) {
    static std::vector<uint8_t> seen;
    static int seen_dimx = 0, seen_dimy = 0;
    constexpr int ROWS = 12;   // MSG_ROWS in menu.inc
    if (!gps || !on || gps->dimy < 2 * ROWS)
        return false;
    const int dimx = gps->dimx, dimy = gps->dimy;
    std::vector<uint8_t> mask(size_t(dimx) * ROWS);
    for (int cx = 0; cx < dimx; ++cx)
        for (int r = 0; r < ROWS; ++r) {
            const int idx = cx * dimy + dimy - ROWS + r;
            mask[size_t(cx) * ROWS + r] =
                gps->screentexpos[idx] || gps->screentexpos_lower[idx] || gps->screentexpos_anchored[idx];
        }
    if (plain) {
        seen.swap(mask);
        seen_dimx = dimx;
        seen_dimy = dimy;
        return true;
    }
    return dimx == seen_dimx && dimy == seen_dimy && mask == seen;
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
    bar_frozen = false;
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

// the game's top layer (drawn over the rest) has something in this cell
bool top_cell_used(int idx) {
    if (!gps->top_in_use || !gps->screen_top)
        return false;
    for (int i = 0; i < 8; ++i)
        if (gps->screen_top[idx * 8 + i])
            return true;
    return gps->screentexpos_top[idx] || gps->screentexpos_top_lower[idx] || gps->screentexpos_top_anchored[idx] ||
           gps->screentexpos_top_flag[idx];
}

// a cell as it shows (the top layer if it holds anything there), taken off both layers
GridCell take_cell(int idx) {
    GridCell c = read_cell(idx);
    if (top_cell_used(idx)) {
        std::memcpy(c.screen, &gps->screen_top[idx * 8], 8);
        c.tex = gps->screentexpos_top[idx];
        c.lower = gps->screentexpos_top_lower[idx];
        c.anchored = gps->screentexpos_top_anchored[idx];
        c.flag = gps->screentexpos_top_flag[idx];
        std::memset(&gps->screen_top[idx * 8], 0, 8);
        gps->screentexpos_top[idx] = 0;
        gps->screentexpos_top_lower[idx] = 0;
        gps->screentexpos_top_anchored[idx] = 0;
        gps->screentexpos_top_flag[idx] = 0;
    }
    write_cell(idx, GridCell());
    return c;
}

// the character a cell shows
uint8_t shown_char(int idx) {
    if (gps->top_in_use && gps->screen_top && gps->screen_top[idx * 8])
        return gps->screen_top[idx * 8];
    return gps->screen[idx * 8];
}

// Simulation thread, after the game's last draw: the game's auto-move
// question goes. While the adventurer (or the mount) walks a path over
// several turns and something interrupts it, the game puts "Continue action",
// "Stop action" and "Finish action" (or "Finish anyway", "Finishing up...")
// where the action bar is, buttons three rows high with the words in the
// middle row. Read from the game's own code (53.17): it draws them while
// unit.dungeon_control asks. Each button goes as the run of used cells on
// its words' row, with the rows above and below. The question stays open in
// the game; the next move or click answers it as without the view.
bool prompt_hidden = false;   // this frame (move_bottom_buttons then shows the bar as it last was)

void hide_action_prompt(bool plain) {
    prompt_hidden = false;
    if (!gps || !plain || gps->dimx < 60 || gps->dimy < 16)
        return;
    static const char *const LABELS[] = {"Continue action", "Stop action", "Finish action", "Finish anyway",
                                         "Finishing up..."};
    const int dimx = gps->dimx, dimy = gps->dimy;
    const auto used = [&](int cx, int cy) {
        const int idx = cx * dimy + cy;
        return cell_used(idx) || top_cell_used(idx);
    };
    for (int cy = dimy - 10; cy < dimy; ++cy)
        for (int cx = 0; cx < dimx; ++cx)
            for (const char *label : LABELS) {
                const int n = int(std::strlen(label));
                if (cx + n > dimx)
                    continue;
                int i = 0;
                while (i < n && shown_char((cx + i) * dimy + cy) == uint8_t(label[i]))
                    ++i;
                if (i < n)
                    continue;
                int x0 = cx, x1 = cx + n - 1;
                while (x0 > 0 && cx - x0 < 8 && used(x0 - 1, cy))
                    --x0;
                while (x1 < dimx - 1 && x1 - cx < 60 && used(x1 + 1, cy))
                    ++x1;
                for (int x = x0; x <= x1; ++x)
                    for (int y = std::max(0, cy - 1); y <= std::min(dimy - 1, cy + 1); ++y)
                        take_cell(x * dimy + y);
                prompt_hidden = true;
                break;
            }
}

// Simulation thread, before anything at the bottom is moved: the caption the
// game shows for the button under the pointer, taken off the grid whole (all
// its layers), so that it can go along with the moved buttons. Where and how
// big the game draws it was read from its own code (53.17): 28 columns from
// main_interface.current_hover_left_x, up from current_hover_bot_y, as many
// rows as the text has and one more, two more again with a hotkey line.
struct Caption {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    std::vector<GridCell> cells;   // idx is where the game drew each
};

Caption cut_caption() {
    Caption cap;
    if (!game || !frame_pointer.on_bar)
        return cap;
    auto &mi = game->main_interface;
    const int which = int(mi.current_hover);
    if (which < 0 || which >= int(std::size(mi.hover_instruction)))
        return cap;
    const int lines = int(mi.hover_instruction[which].text.size());
    const int x0 = mi.current_hover_left_x, y1 = mi.current_hover_bot_y;
    const int y0 = y1 - lines - 1 - (int(mi.current_hover_key) != 0 ? 2 : 0);
    cap.x0 = std::max(0, x0);
    cap.x1 = std::min(gps->dimx - 1, x0 + 27);
    cap.y0 = std::max(0, y0);
    cap.y1 = std::min(gps->dimy - 1, y1);
    for (int cx = cap.x0; cx <= cap.x1; ++cx)
        for (int cy = cap.y0; cy <= cap.y1; ++cy) {
            const int idx = cx * gps->dimy + cy;
            if (cell_used(idx) || top_cell_used(idx))
                cap.cells.push_back(take_cell(idx));
        }
    return cap;
}

// The moved caption as move_bottom_buttons placed it this frame. DFHack's
// overlay widgets (ours among them: camera, timer, travel and sleep) are drawn
// after the game's draw and would lie over it, so dungeon_top_hook puts it
// back once they are drawn.
std::vector<GridCell> caption_placed;
bool caption_pending = false;
int caption_dimx = 0, caption_dimy = 0;

// a cell over whatever DFHack's widgets drew there, in either layer
void put_cell_over(const GridCell &c) {
    write_cell(c.idx, c);
    if (gps->top_in_use && gps->screen_top) {
        std::memset(&gps->screen_top[c.idx * 8], 0, 8);
        gps->screentexpos_top[c.idx] = 0;
        gps->screentexpos_top_lower[c.idx] = 0;
        gps->screentexpos_top_anchored[c.idx] = 0;
        gps->screentexpos_top_flag[c.idx] = 0;
    }
}

// A caption box drawn as the game draws its own (read from its code, 53.17):
// 28 columns, a frame of the game's pictures with their bottom row at y1, the
// text from the second row and second column, a hotkey line above the bottom
// row after an empty one. The nine pictures are texture ids at offset 0x1924
// of gps (an unnamed part of df::graphic): top left, left, bottom left, top,
// middle, bottom, top right, right, bottom right. In text mode a frame of
// line characters stands in.
void paint_game_caption(int x0, int y1, const std::vector<std::string> &lines, const std::string &key) {
    const int dimx = gps->dimx, dimy = gps->dimy;
    const int w = 28, h = int(lines.size()) + 2 + (key.empty() ? 0 : 2);
    if (dimx < w || dimy < h)
        return;
    x0 = std::clamp(x0, 0, dimx - w);
    y1 = std::clamp(y1, h - 1, dimy - 1);
    const int x1 = x0 + w - 1, y0 = y1 - h + 1;
    const int32_t *tex = reinterpret_cast<const int32_t *>(reinterpret_cast<const char *>(gps) + 0x1924);
    const bool graphics = Screen::inGraphicsMode();
    static const uint8_t LINES[9] = {218, 179, 192, 196, ' ', 196, 191, 179, 217};
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y) {
            const int col = x == x0 ? 0 : x == x1 ? 2 : 1, row = y == y0 ? 0 : y == y1 ? 2 : 1;
            const int k = col * 3 + row;
            const int idx = x * dimy + y;
            if (graphics) {
                Screen::paintTile(Screen::Pen(' ', COLOR_WHITE, COLOR_BLACK), x, y);
                if (gps->top_in_use && gps->screen_top)
                    gps->screentexpos_top_lower[idx] = tex[k];
                else
                    gps->screentexpos_lower[idx] = tex[k];
            } else {
                Screen::paintTile(Screen::Pen(char(LINES[k]), COLOR_YELLOW, COLOR_BLACK), x, y);
            }
        }
    Screen::Pen text(' ', COLOR_WHITE, COLOR_BLACK, true);
    text.keep_lower = true;
    for (size_t i = 0; i < lines.size(); ++i)
        Screen::paintString(text, x0 + 1, y0 + 1 + int(i), lines[i].substr(0, size_t(w - 2)));
    if (!key.empty()) {
        Screen::paintString(text, x0 + 1, y1 - 1, "Hotkey: ");
        Screen::Pen green(' ', COLOR_LIGHTGREEN, COLOR_BLACK);
        green.keep_lower = true;
        Screen::paintString(green, x0 + 9, y1 - 1, key.substr(0, size_t(std::max(0, w - 10))));
    }
}

std::vector<std::string> msg_wrap(const std::string &text, int width);   // menu.inc

// a Lua global as an integer (0 if there is none)
int lua_global_int(lua_State *L, const char *name) {
    lua_getglobal(L, name);
    const int v = lua_isnumber(L, -1) ? int(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    return v;
}

// The mod's travel and sleep buttons (TravelSleepButtons in first-person.lua)
// stand in for the game's, which are hidden while the view shows; with the
// pointer on one, its caption, in the game's words and look. The widget says
// which button is under the pointer and where it is (fpview_widget_hint: 1
// travel, 2 sleep; fpview_widget_hint_x, _y: its top left cell); the pointer
// must still be there, as the widget does not say when it is no longer drawn.
void paint_widget_hint() {
    lua_State *L = Core::getInstance().getLuaState(true);
    if (!L || !game)
        return;
    const int which = lua_global_int(L, "fpview_widget_hint");
    if (which != 1 && which != 2)
        return;
    const int bx = lua_global_int(L, "fpview_widget_hint_x"), by = lua_global_int(L, "fpview_widget_hint_y");
    int px = 0, py = 0, cx = 0, cy = 0;
    if (!pointer_pixels(px, py) || !interface_cell(px, py, cx, cy) || cx < bx || cx > bx + 3 || cy < by ||
        cy > by + 2)
        return;
    using df::main_hover_instruction;
    const main_hover_instruction inst =
        which == 1 ? main_hover_instruction::ADVENTURE_TRAVEL : main_hover_instruction::ADVENTURE_SLEEP;
    std::vector<std::string> lines;
    const auto &box = game->main_interface.hover_instruction[int(inst)];
    for (const std::string *s : box.text)
        if (s)
            lines.push_back(*s);
    if (lines.empty())
        if (const char *caption = ENUM_ATTR(main_hover_instruction, caption, inst))
            lines = msg_wrap(caption, 26);
    const std::string key =
        Screen::getKeyDisplay(which == 1 ? df::interface_key::A_TRAVEL : df::interface_key::A_SLEEP);
    paint_game_caption(bx, by - 1, lines, key);
}

// Simulation thread, after hide_corner_buttons: see MovedBlock. The action
// bar is found as the run of textured columns in the bottom rows around the
// middle of the screen, the buttons as the run of used columns in the bottom
// rows from right of the portrait; gaps of up to two columns (between groups
// of buttons) belong to them.
void move_bottom_buttons(bool plain) {
    moved_bar.live = false;
    moved_buttons.live = false;
    bar_frozen = false;
    caption_pending = false;
    if (!gps || !plain || gps->dimx < 100 || gps->dimy < 20)
        return;
    // the grid is emptied before every frame's draw: only this frame's writes can still be on it
    bar_written.clear();
    const int dimx = gps->dimx, dimy = gps->dimy;
    // before the blocks are looked for: the caption reaches down among their rows
    const Caption caption = cut_caption();
    // The game's "View your companions" button (right of the portrait, in the
    // row the buttons go up into) as it last was, for frames where the game
    // draws nothing there (while walking, between turns), so that it does not
    // blink. It stays at its own place; the mod's camera and timer come after
    // it, drawn later by DFHack.
    {
        static std::vector<GridCell> kept;
        static int kept_dimx = 0, kept_dimy = 0, missing = 0;
        std::vector<GridCell> now;
        bool any = false;
        for (int cx = BAR_LEFT; cx < BUTTONS_LEFT && cx < dimx; ++cx)
            for (int cy = dimy - 2 * BUTTON_ROWS; cy < dimy - BUTTON_ROWS; ++cy) {
                const int idx = cx * dimy + cy;
                if (cell_used(idx) || top_cell_used(idx)) {
                    any = true;
                    now.push_back(read_cell(idx));
                }
            }
        if (any) {
            kept.swap(now);
            kept_dimx = dimx;
            kept_dimy = dimy;
            missing = 0;
        } else if (++missing <= 90 && kept_dimx == dimx && kept_dimy == dimy) {
            for (const GridCell &c : kept)
                write_cell(c.idx, c);
        }
    }
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

    // the buttons right of the portrait, with the speed display after them
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
    // the speed display: its text is kept, the display itself goes
    if (kx0 >= 0 && kx1 >= kx0 + BUTTON_COLS) {
        std::string said;
        for (int cy = dimy - BUTTON_ROWS; cy < dimy; ++cy) {
            std::string line;
            for (int cx = kx0 + BUTTON_COLS; cx <= kx1; ++cx) {
                const int idx = cx * dimy + cy;
                const uint8_t ch = gps->screen[idx * 8];
                // the letters sit on the display's own picture (its frame and background)
                line += ch > 32 && ch < 127 ? char(ch) : ' ';
                write_cell(idx, GridCell());
            }
            const size_t a = line.find_first_not_of(' '), b = line.find_last_not_of(' ');
            if (a == std::string::npos)
                continue;
            if (!said.empty())
                said += said.back() == ':' ? " " : ": ";
            said += line.substr(a, b - a + 1);
        }
        kx1 = kx0 + BUTTON_COLS - 1;
        std::lock_guard<std::mutex> lock(speed_mutex);
        speed_text = said;
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
    // the buttons up beside the timer, the bar left into the bottom rows they leave
    const int bmx = -(bx0 - BAR_LEFT);
    const bool bar_moves = bx0 >= 0 && bmx < 0;
    const int kmx = BUTTONS_LEFT - kx0, kmy = -BUTTON_ROWS, ktop = dimy - BUTTON_ROWS;
    const bool buttons_move = kx0 >= 0 && kx1 + kmx < dimx;
    if (buttons_move)
        for (int cx = kx0; cx <= kx1; ++cx)
            for (int cy = ktop; cy < dimy; ++cy)
                lift(cx, cy, kmx, kmy);
    if (bar_moves)
        for (int cx = bx0; cx <= bx1; ++cx)
            for (int cy = btop; cy < dimy; ++cy)
                lift(cx, cy, bmx, 0);
    std::vector<GridCell> bar_now, buttons_now;   // where the bar's and the buttons' cells went
    for (auto it = lifted.rbegin(); it != lifted.rend(); ++it) {
        const int cx = it->cell.idx / dimy, cy = it->cell.idx % dimy;
        GridCell placed = it->cell;
        placed.idx = (cx + it->mx) * dimy + (cy + it->my);
        write_cell(placed.idx, placed);
        bar_written.push_back(placed);
        if (bar_moves && it->my == 0 && it->mx == bmx)
            bar_now.push_back(placed);
        else if (buttons_move && it->my == kmy && it->mx == kmx)
            buttons_now.push_back(placed);
    }

    // The bar and the buttons as they last were, on frames where the game drew
    // none of them: while it shows its auto-move question in the bar's place
    // (see bar_frozen), and for a moment while it is busy between turns, which
    // made them blink while walking. Only into cells nothing else holds.
    static std::vector<GridCell> bar_cache, buttons_cache;
    static int cache_dimx = 0, cache_dimy = 0, cache_x0 = 0, cache_x1 = -1, cache_y0 = 0, cache_dx = 0;
    static int bar_missing = 0, buttons_missing = 0;   // frames since each was last drawn by the game
    constexpr int KEEP_FRAMES = 90;
    const auto put_back = [&](const std::vector<GridCell> &cells) {
        for (const GridCell &c : cells)
            if (!cell_used(c.idx) && !top_cell_used(c.idx)) {
                write_cell(c.idx, c);
                bar_written.push_back(c);
            }
    };
    if (buttons_move) {
        buttons_cache.swap(buttons_now);
        buttons_missing = 0;
    } else if (++buttons_missing <= KEEP_FRAMES && cache_dimx == dimx && cache_dimy == dimy) {
        put_back(buttons_cache);
    }
    if (bar_moves) {
        bar_cache.swap(bar_now);
        bar_missing = 0;
        cache_dimx = dimx;
        cache_dimy = dimy;
        cache_x0 = bx0 + bmx;
        cache_x1 = bx1 + bmx;
        cache_y0 = btop;
        cache_dx = -bmx;
    } else if ((prompt_hidden || ++bar_missing <= KEEP_FRAMES) && !bar_cache.empty() && cache_dimx == dimx &&
               cache_dimy == dimy) {
        put_back(bar_cache);
        moved_bar.x0 = cache_x0;
        moved_bar.x1 = cache_x1;
        moved_bar.y0 = cache_y0;
        moved_bar.y1 = dimy - 1;
        moved_bar.dx = cache_dx;
        moved_bar.dy = 0;
        moved_bar.live = true;
        bar_frozen = true;
    }

    // The caption goes where the button under the pointer went, kept whole on
    // the screen, over everything else (the game draws it over its buttons too).
    if (!caption.cells.empty()) {
        int mx = 0, my = 0;
        if (bar_moves && pointer_in(bx0 + bmx, bx1 + bmx, btop, dimy - 1)) {
            mx = bmx;
        } else if (buttons_move && pointer_in(kx0 + kmx, kx1 + kmx, ktop + kmy, dimy - 1 + kmy)) {
            mx = kmx;
            my = kmy;
        }
        mx = std::clamp(mx, -caption.x0, dimx - 1 - caption.x1);
        my = std::clamp(my, -caption.y0, dimy - 1 - caption.y1);
        caption_placed.clear();
        for (const GridCell &c : caption.cells) {
            GridCell placed = c;
            placed.idx = (c.idx / dimy + mx) * dimy + (c.idx % dimy + my);
            write_cell(placed.idx, placed);
            bar_written.push_back(placed);
            caption_placed.push_back(placed);
        }
        caption_pending = true;
        caption_dimx = dimx;
        caption_dimy = dimy;
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

// With the ring (radial.inc) the game's buttons at the bottom go: every cell
// of the bottom two rows of buttons right of the portrait (the action bar, the
// row with the speed display, View your companions, the corner's buttons) is
// emptied on the plain screen and remembered. On a game dialog over the view
// only cells that still hold what the plain screen had there are emptied, so
// the dialog's own cells stay; the old way left the bar at the game's place
// and the message panel off whenever a dialog was up (seen by the owner with
// the g list, talking, performing, crafting and abilities, 2026-11-15).
std::vector<GridCell> bottom_gone;
int bottom_gone_dimx = 0, bottom_gone_dimy = 0;

void remove_bottom_buttons(bool plain, bool like_plain = false) {
    moved_bar.live = false;
    moved_buttons.live = false;
    bar_frozen = false;
    caption_pending = false;
    corner_live = false;
    msg_ok = false;
    if (!gps || gps->dimx < 100 || gps->dimy < 20)
        return;
    const int dimx = gps->dimx, dimy = gps->dimy;
    const int y0 = dimy - 2 * BUTTON_ROWS;
    const auto same = [](const GridCell &a, const GridCell &b) {
        return std::memcmp(a.screen, b.screen, 8) == 0 && a.tex == b.tex && a.lower == b.lower && a.anchored == b.anchored;
    };
    if (plain) {
        cut_caption();
        // the speed display's words, for the message panel's top line: right of the six buttons
        {
            int kx0 = -1;
            for (int cx = BAR_LEFT; cx <= BAR_LEFT + 4 && kx0 < 0; ++cx)
                for (int cy = dimy - BUTTON_ROWS; cy < dimy; ++cy)
                    if (cell_used(cx * dimy + cy))
                        kx0 = cx;
            std::string said;
            if (kx0 >= 0)
                for (int cy = dimy - BUTTON_ROWS; cy < dimy; ++cy) {
                    std::string line;
                    for (int cx = kx0 + BUTTON_COLS, gap = 0; cx < dimx && gap < 3; ++cx) {
                        const int idx = cx * dimy + cy;
                        const uint8_t ch = gps->screen[idx * 8];
                        gap = cell_used(idx) || top_cell_used(idx) ? 0 : gap + 1;
                        line += ch > 32 && ch < 127 ? char(ch) : ' ';
                    }
                    const size_t a = line.find_first_not_of(' '), b = line.find_last_not_of(' ');
                    if (a == std::string::npos)
                        continue;
                    if (!said.empty())
                        said += said.back() == ':' ? " " : ": ";
                    said += line.substr(a, b - a + 1);
                }
            if (!said.empty()) {
                std::lock_guard<std::mutex> lock(speed_mutex);
                speed_text = said;
            }
        }
        std::vector<GridCell> gone;
        for (int cx = BAR_LEFT; cx < dimx; ++cx)
            for (int cy = y0; cy < dimy; ++cy) {
                const int idx = cx * dimy + cy;
                if (!cell_used(idx) && !top_cell_used(idx))
                    continue;
                gone.push_back(read_cell(idx));
                write_cell(idx, GridCell());
            }
        // a frame between turns draws none of it: the last ones are kept
        if (!gone.empty()) {
            bottom_gone.swap(gone);
            bottom_gone_dimx = dimx;
            bottom_gone_dimy = dimy;
        }
        msg_ok = true;
        return;
    }
    // Under one of the game's windows (a sheet, a list, a dialog). What the
    // game draws along the bottom that is no part of a window goes: its buttons,
    // the portrait, its status icons. Comparing them with the plain screen's was
    // not enough: with a sheet open the game draws some of them otherwise, and
    // they stayed over the doll and the bars. A window is told by its size: a
    // block of drawn cells that reaches up out of the bottom band, where the
    // buttons and icons stay inside it. The message list stays; a window over
    // it covers it, and clicks there go to the window (in_messages_free).
    (void)like_plain;
    // First what is drawn exactly as on the plain screen (the action bar, under
    // a unit sheet): it can touch the window's edge and would be taken for part of it.
    if (bottom_gone_dimx == dimx && bottom_gone_dimy == dimy)
        for (const GridCell &c : bottom_gone)
            if (same(read_cell(c.idx), c))
                take_cell(c.idx);
    const int band_top = std::max(0, dimy - (12 + std::max(4, passing_rows.load())));
    const auto used = [&](int idx) { return cell_used(idx) || top_cell_used(idx); };
    const auto lettered = [&](int idx) {
        const uint8_t a = gps->screen[idx * 8];
        const uint8_t b = gps->top_in_use && gps->screen_top ? gps->screen_top[idx * 8] : 0;
        return (a > 32 && a < 255) || (b > 32 && b < 255);
    };
    struct Block {
        std::vector<int> cells;
        int x0, x1, y0, y1;
        bool keep;
    };
    std::vector<Block> blocks;
    std::vector<uint8_t> seen(size_t(dimx) * dimy, 0);
    std::vector<int> stack;
    for (int cx = 0; cx < dimx; ++cx)
        for (int cy = band_top; cy < dimy; ++cy) {
            const int start = cx * dimy + cy;
            if (seen[start] || !used(start))
                continue;
            Block b{{}, cx, cx, cy, cy, false};
            std::set<int> text_rows;
            stack.assign(1, start);
            seen[start] = 1;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                b.cells.push_back(i);
                const int x = i / dimy, y = i % dimy;
                b.x0 = std::min(b.x0, x);
                b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y);
                b.y1 = std::max(b.y1, y);
                if (lettered(i))
                    text_rows.insert(y);
                const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                for (const auto &n : nb) {
                    if (n[0] < 0 || n[1] < 0 || n[0] >= dimx || n[1] >= dimy)
                        continue;
                    const int j = n[0] * dimy + n[1];
                    if (!seen[j] && used(j)) {
                        seen[j] = 1;
                        stack.push_back(j);
                    }
                }
            }
            // a window: reaches up out of the band, or holds lines of text (the
            // buttons and icons are pictures; the speed display is a line or two)
            b.keep = b.y0 < band_top || text_rows.size() >= 3;
            blocks.push_back(std::move(b));
        }
    // and what lies just under a window, within its width (its own buttons, split off by an empty row)
    for (bool grew = true; grew;) {
        grew = false;
        for (Block &b : blocks) {
            if (b.keep)
                continue;
            for (const Block &w : blocks)
                if (w.keep && b.x0 >= w.x0 - 1 && b.x1 <= w.x1 + 1 && b.y0 > w.y1 && b.y0 - w.y1 <= 3) {
                    b.keep = grew = true;
                    break;
                }
        }
    }
    for (const Block &b : blocks)
        if (!b.keep)
            for (int i : b.cells)
                take_cell(i);
    msg_ok = true;
}

// Simulation thread: the mouse wheel on the plain adventure screen picks the
// gait, up the next faster one, down the next slower, among those of the way
// of moving in use (crawling when lying down, walking otherwise). Not over
// the timer button or the message list, where the wheel has its own use.
void gait_wheel(bool plain) {
    static int seen = 0;
    const int total = wheel_total.load();
    const int turned = total - seen;
    seen = total;
    if (!turned || !plain || !gps)
        return;
    int mx = -1, my = -1, cx = -1, cy = -1;
    sdl_get_mouse_state(&mx, &my);
    if (in_messages(mx, my))
        return;
    // the timer (TimeButton in first-person.lua): columns 20 to 26 of the row above the bottom one
    if (interface_cell(mx, my, cx, cy) && cx >= 20 && cx < BUTTONS_LEFT && cy >= gps->dimy - 2 * BUTTON_ROWS &&
        cy < gps->dimy - BUTTON_ROWS)
        return;
    df::unit *adv = World::getAdventurer();
    if (!adv)
        return;
    // riding, the mount's pace is set (as the game's own speed button does)
    df::unit *who = adv;
    if (!adv->flags1.bits.on_ground)
        if (const int32_t mount = fpv::mountOf(adv); mount >= 0)
            if (df::unit *m = df::unit::find(mount))
                who = m;
    if (!who->body.body_plan)
        return;
    const df::gait_type type = who->flags1.bits.on_ground ? df::gait_type::CRAWL : df::gait_type::WALK;
    const auto &gaits = who->body.body_plan->gait_info[type];
    int32_t &cur = who->enemy.gait_index[type];
    for (int n = std::abs(turned); n > 0; --n) {
        if (cur < 0 || cur >= int(gaits.size()) || !gaits[cur])
            return;
        // full_speed is the time a step takes: less is faster
        int best = -1, best_d = 0;
        for (int i = 0; i < int(gaits.size()); ++i) {
            if (!gaits[i])
                continue;
            const int d = (gaits[i]->full_speed - gaits[cur]->full_speed) * (turned > 0 ? -1 : 1);
            if (d > 0 && (best < 0 || d < best_d)) {
                best = i;
                best_d = d;
            }
        }
        if (best < 0)
            return;
        cur = best;
        // A mount picks its own gait again at each step (seen: a trot, whatever
        // the wheel had set) unless it has a commanded one: the index and the
        // flag saying there is one (the index alone did nothing; the flag was
        // off on a ridden horse, read in the game 2026-11-15).
        if (who != adv) {
            who->status.command_gait_index[type] = best;
            who->status.unit_command_flag.bits.HAVE_COMMAND_GAIT = true;
        }
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

// The game's box in the middle of the screen with one message and an "Okay"
// button (a god speaking to you, "You will have to leave this structure first
// ..."). The message is in the message list as well, so while the view shows
// the box is answered at once by taking it off the game's list of them. What
// they said is kept, to tell those messages from the others in the list.
std::string squeeze_spaces(const std::string &text) {
    std::string out;
    for (const char ch : text)
        if (ch != ' ' && ch != char(10) && ch != char(13))
            out += ch;
    return out;
}

std::vector<std::string> popup_texts;   // simulation thread; without spaces
int popup_serial = 0;

void dismiss_popups() {
    if (!world || world->status.popups.empty())
        return;
    for (df::popup_message *p : world->status.popups) {
        if (!p)
            continue;
        const std::string text = squeeze_spaces(p->text);
        if (!text.empty() && std::find(popup_texts.begin(), popup_texts.end(), text) == popup_texts.end())
            popup_texts.push_back(text);
        delete p;
    }
    world->status.popups.clear();
    if (popup_texts.size() > 64)
        popup_texts.erase(popup_texts.begin(), popup_texts.end() - 64);
    ++popup_serial;
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

// the ranged weapon the creature holds, if any
df::item_weaponst *held_ranged_weapon(df::unit *unit) {
    for (df::unit_inventory_item *inv : unit->inventory)
        if (inv && inv->item && inv->mode == df::inv_item_role_type::Weapon && is_ranged_weapon(inv->item))
            return virtual_cast<df::item_weaponst>(inv->item);
    return nullptr;
}

// What is ready to be shot: an arrow on a bow's string is carried by the
// creature itself, a bolt in a crossbow lies inside the weapon.
df::item *loaded_ammo(df::unit *unit) {
    for (df::unit_inventory_item *inv : unit->inventory)
        if (inv && inv->item && inv->mode == df::inv_item_role_type::Nocked)
            return inv->item;
    if (df::item *weapon = held_ranged_weapon(unit)) {
        std::vector<df::item *> inside;
        Items::getContainedItems(weapon, &inside);
        for (df::item *item : inside)
            if (item && item->getType() == df::item_type::AMMO)
                return item;
    }
    return nullptr;
}

// The game offers no way to take a bolt out of a crossbow again, so it is
// put back by hand: into the quiver, else the backpack, else at the feet.
bool unload_weapon(df::unit *unit) {
    df::item *ammo = loaded_ammo(unit);
    df::item *weapon = held_ranged_weapon(unit);
    if (!ammo)
        return false;
    df::item *quiver = nullptr, *pack = nullptr;
    for (df::unit_inventory_item *inv : unit->inventory) {
        if (!inv || !inv->item)
            continue;
        if (!quiver && inv->item->getType() == df::item_type::QUIVER)
            quiver = inv->item;
        if (!pack && inv->item->getType() == df::item_type::BACKPACK)
            pack = inv->item;
    }
    df::item *into = quiver ? quiver : pack;
    const bool moved = into ? Items::moveToContainer(ammo, into) : Items::moveToGround(ammo, unit->pos);
    if (moved && weapon)
        weapon->flags.bits.weight_computed = false;
    return moved;
}

// what is in the adventurer's hands; true if one of them holds a ranged weapon
bool update_held(df::unit *adv, bool want_sprites) {
    df::item *hands[2] = {nullptr, nullptr};
    bool ranged = false;
    const bool nocked = loaded_ammo(adv) != nullptr;
    bool loads = false;
    if (df::item_weaponst *weapon = held_ranged_weapon(adv)) {
        loads = weapon->subtype->load_type == df::weapon_load_type::LOADED;
        std::string word = weapon->subtype->ranged_ammo;
        for (char &c : word)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        std::lock_guard<std::mutex> lock(ammo_mutex);
        ammo_word = word.empty() ? "weapon" : word;
    }
    weapon_loads = loads;
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
    arrow_nocked = ranged && nocked;
    return ranged;
}

// the creature is putting an arrow on the string (loading its weapon)
// someone's strike at this creature is on its way: the game then offers to dodge it
bool struck_at(const df::unit *unit) {
    if (!unit || !world)
        return false;
    for (df::unit *other : world->units.active) {
        if (!other || other == unit)
            continue;
        for (df::unit_action *a : other->actions)
            if (a && a->type == df::unit_action_type::Attack && a->data.attack.target_unit_id == unit->id &&
                a->data.attack.timer1 > 0)
                return true;
    }
    return false;
}

// this creature's own strike has been started and has not landed yet
bool striking(const df::unit *unit) {
    for (df::unit_action *a : unit->actions)
        if (a && a->type == df::unit_action_type::Attack && a->data.attack.timer1 > 0)
            return true;
    return false;
}

bool loading_weapon(df::unit *unit) {
    for (df::unit_action *a : unit->actions)
        if (a && a->type == df::unit_action_type::LoadRangedWeapon)
            return true;
    return false;
}

// What fighting looks like. Every creature's strikes are its Attack actions
// (target, weapon, time to strike); a strike happens when that time runs out
// or the action ends. Then:
//  - yours: the weapon in your hand lunges at the pointer and back (draw_held)
//  - anyone else's: the attacker's picture lunges at its target and a white
//    crescent is swept between them (nudges, draw_combat_fx)
//  - one at you: an arrow from the pointer towards the attacker, green for a
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
    bool you;                // on the adventurer: sinks from under the pointer
    std::string text;
    uint32_t color;
    double start;            // may lie ahead: words over one creature come one after another
};
// Several words for one creature at once (a head struck and the neck bruised by
// the force) come TEXT_GAP apart; each new one pushes the earlier ones up a line
// over TEXT_PUSH.
constexpr double TEXT_TIME = 1.4, TEXT_GAP = 0.2, TEXT_PUSH = 0.12;
std::vector<FxText> fx_texts;
// Words wait until a report of the fight is in the game's message list (or
// TOLD_WAIT has passed, and they are dropped): fights the game does not tell
// you about show none. Reports newly in the list, by id: when they came and
// where they happened.
struct Told {
    double when;
    df::coord pos, pos2;
    bool combat;
};
constexpr double TOLD_KEEP = 3.0, TOLD_WAIT = 1.0;
std::unordered_map<int32_t, Told> fx_told;
int32_t fx_told_last = -1;   // the newest report id looked at
bool fx_told_started = false;
struct Unsaid {
    int32_t unit_id;
    std::string text;
    uint32_t color;
    double start;
};
std::vector<Unsaid> fx_unsaid;
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

// the worst of what a wound did to one body part (bp), in a word or two;
// first: the wound's first part, the one a severed_part flag is about
std::string wound_word(const df::unit_wound *w, int bp, bool first) {
    bool cut = false, smashed_apart = false, guts = false, compound = false, broken = false, fracture = false,
         major_artery = false, artery = false, nerve = false, gouged = false, torn = false, sprain = false,
         bruise = false;
    int effect = -1;
    for (const df::unit_wound_layerst *l : w->parts) {
        if (!l || l->body_part_id != bp)
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
    if (first && w->flags.bits.severed_part)
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
        // What the target had before the strike. A quick strike can land in
        // the same moment it is first seen here, its wound already there: so
        // go by what the target had the last time it was looked at.
        int32_t wounds_before = s.target_wounds;
        size_t stuck_before = t ? stuck_items(t).size() : 0;
        if (t) {
            const auto w = wounds_seen.find(t->id);
            if (w != wounds_seen.end())
                wounds_before = std::min(wounds_before, w->second);
            const auto st = stuck_seen.find(t->id);
            if (st != stuck_seen.end())
                stuck_before = std::min(stuck_before, st->second.size());
            fx_misses.push_back(PendingMiss{t->id, wounds_before, stuck_before, now});
        }
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
            fx_arrows.push_back(FxArrow{arc.ax, arc.ay, wounds_before, false, now});
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

    // reports newly in the message list (ids only grow; a smaller one is another world)
    {
        const auto &list = world->status.announcements;
        const int32_t newest = list.empty() || !list.back() ? -1 : list.back()->id;
        if (newest >= 0 && newest < fx_told_last) {
            fx_told.clear();
            fx_told_started = false;
        }
        if (!fx_told_started) {
            fx_told_started = true;
            fx_told_last = newest;   // only news from now on
        } else {
            for (size_t i = list.size(); i-- > 0;) {
                const df::report *r = list[i];
                if (!r)
                    continue;
                if (r->id <= fx_told_last)
                    break;
                const std::string type = ENUM_KEY_STR(announcement_type, r->type);
                fx_told[r->id] = Told{now, r->pos, r->pos2, type.rfind("COMBAT_", 0) == 0};
            }
            fx_told_last = std::max(fx_told_last, newest);
        }
        std::erase_if(fx_told, [&](const auto &e) { return now - e.second.when > TOLD_KEEP; });
    }
    // whether a fight of this creature was just told: a report in its combat,
    // sparring or hunting log, or a combat report at its tile or next to it
    const auto told = [&](const df::unit *u) {
        if (fx_told.empty())
            return false;
        for (int k = 0; k < 3; ++k) {
            const auto &log = u->reports.log[k];
            for (size_t i = log.size(), n = 0; i-- > 0 && n < 8; ++n)
                if (fx_told.count(log[i]))
                    return true;
        }
        const auto near = [&](const df::coord &c) {
            return c.z == u->pos.z && std::abs(c.x - u->pos.x) <= 1 && std::abs(c.y - u->pos.y) <= 1;
        };
        for (const auto &[id, t] : fx_told)
            if (t.combat && (near(t.pos) || near(t.pos2)))
                return true;
        return false;
    };
    const auto say = [&](const df::unit *u, const std::string &text, uint32_t color) {
        fx_unsaid.push_back(Unsaid{u->id, text, color, now});
    };
    // words over a creature, after the ones it got a moment ago
    const auto show = [&](const df::unit *u, const std::string &text, uint32_t color) {
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
        t.start = now;
        for (const FxText &o : fx_texts)
            if (o.unit_id == u->id)
                t.start = std::max(t.start, o.start + TEXT_GAP);
        fx_texts.push_back(std::move(t));
    };
    // new wounds and things newly stuck in creatures nearby, from any attack, shot or throw
    for (df::unit *u : world->units.active) {
        if (!u || std::abs(u->pos.z - adv->pos.z) > 2 || std::abs(u->pos.x - adv->pos.x) > 40 ||
            std::abs(u->pos.y - adv->pos.y) > 40)
            continue;
        const auto seen = wounds_seen.find(u->id);
        if (seen != wounds_seen.end() && u->body.wound_next_id > seen->second)
            for (const df::unit_wound *w : u->body.wounds) {
                if (!w || w->id < seen->second)
                    continue;
                // one wound can reach several parts (a blow to the head that bruises
                // the neck too): a line for each, in the order the game lists them
                std::vector<int> parts;
                for (const df::unit_wound_layerst *l : w->parts)
                    if (l && std::find(parts.begin(), parts.end(), l->body_part_id) == parts.end())
                        parts.push_back(l->body_part_id);
                for (size_t i = 0; i < parts.size(); ++i)
                    say(u, part_name(u, parts[i]) + ": " + wound_word(w, parts[i], i == 0), fpv::rgb(255, 60, 50));
            }
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
    for (auto it = fx_unsaid.begin(); it != fx_unsaid.end();) {
        const df::unit *u = df::unit::find(it->unit_id);
        if (u && told(u)) {
            show(u, it->text, it->color);
            it = fx_unsaid.erase(it);
        } else if (!u || now - it->start > TOLD_WAIT) {
            it = fx_unsaid.erase(it);
        } else {
            ++it;
        }
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
#include "inventory.inc"
#include "gamestyle.inc"
#include "panels.inc"
#include "journal.inc"
#include "radial.inc"

uint32_t last_curtick = 0;   // the tick the game was last drawn for

// Ctrl-click: attack what the crosshair is on. The click is turned into the
// game's own "what do you want to do" list for that tile, and the attack (or
// shot) is then picked from that list for the player, so that the game goes
// straight on to choosing the strike or aiming.
struct AttackDrive {
    int frames = 0;       // frames left to finish in
    int stage = 0;        // 0: pick from the list, 1: confirm if asked, 2: quick or aimed strike asked, 10..: shoot or throw, 20..: nock, unnock, load, 30..: talk, 40: use, 41: step
    int32_t unit_id = -1;
    int intent = 0;       // what the click was for (strike, shoot, throw, wrestle)
    df::coord target;     // the tile that was clicked
    int waited = 0;       // frames spent waiting for the player to pick what to throw
    int px = -1, py = -1; // where in the window the click was
    bool dodge = false;   // the Dodge button was clicked: dodging is picked from the game's list of moves
    bool driving = false; // we are feeding the game ourselves right now
} attack;

// A right click (or Esc) on the Quick / Aimed shot buttons closes the game's
// list under them, which leaves its aiming screen ("Click a target ...") open
// and the pointer held. That screen is closed too, a frame or more later, once
// the list is gone (leave_aim_step); frames left to do it in.
int leave_aim_frames = 0;

// A click with an intent that comes while the game is still busy with what
// was done last (a strike on its way, a step being taken) would be lost: the
// game takes no input then. It is kept and carried out as soon as the game
// takes input again, if that is soon.
struct PendingClick {
    bool on = false;
    int intent = 0;
    int32_t unit_id = -1;
    df::coord target;
    int px = -1, py = -1;
    int64_t until_ms = 0;
} pending_click;

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// the game would act on a key or click of the player right now
bool game_takes_input(df::unit *adv) {
    return adv && adventure && world &&
           adventure->player_control_state == df::adventure_game_loop_type::TAKING_INPUT &&
           adventure->wait_timer <= 0 && !world->status.temp_flag.bits.adv_have_more && !striking(adv);
}

// the thing on the ground at a tile that a click names (the first one there)
df::item *ground_item_at(const df::coord &pos) {
    if (df::map_block *block = Maps::getTileBlock(pos))
        for (int32_t id : block->items) {
            df::item *item = df::item::find(id);
            if (item && item->pos == pos && item->flags.bits.on_ground)
                return item;
        }
    return nullptr;
}

// The creature an attack click was meant for when none is right under the
// pointer. Creatures glide between tiles and a click is aimed through the
// frame drawn last, so a quick click can land just beside one. The nearest
// one drawn within a few pixels of the click (about 3% of the window's
// height), else one on the tile that was clicked (lying low, say).
int32_t creature_near_click(int px, int py, const df::coord *tile, const df::unit *adv) {
    const int step = std::max(4, view_out_h.load() / 180), rings = 5;
    for (int ring = 1; ring <= rings; ++ring)
        for (int dy = -ring; dy <= ring; ++dy)
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring)
                    continue;
                const int x = px + dx * step, y = py + dy * step;
                if (x < 0 || y < 0 || over_interface(x, y))
                    continue;
                const fpv::Pick p = pick_pixels(x, y);
                if (p.valid() && p.unit_id >= 0 && (!adv || p.unit_id != adv->id))
                    return p.unit_id;
            }
    if (tile && world)
        for (df::unit *u : world->units.active)
            if (u && u != adv && u->pos == *tile && Units::isActive(u) && !Units::isDead(u))
                return u->id;
    return -1;
}

// A click is ours from the press to the release of the button: the game
// must not see the button still held or let go, or it does what a plain click
// there does (with an arrow on the string: shoot at once).
int swallow_left = 0;       // frames left in which the rest of that click is kept from the game
const char *tile_list_refused = "";   // what is said when the step (step_onto) is not offered

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

int strike_ticks = 0;       // ticks waited for the strike that is on its way (strike_step)
uint32_t frame_draw_us = 0, frame_draw_n = 0, frame_read_us = 0;   // this frame's game draws and map reads (probes)

// The models in media/models: read when the art folder is first known and
// again within half a second of a file there changing, so a model saved in
// Blender shows at once. Simulation thread, from capture.
int models_check = 0;
uint64_t models_seen = 0;
std::string models_dir = "?";

void follow_models() {
    // a game tile is 32 pixels across
    const float edge = settings.model_lines.load() / 32;
    if (edge != scene.edgeWidth()) {
        std::lock_guard<std::mutex> lock(scene.mutex);
        scene.setEdgeWidth(edge);
    }
    if (--models_check > 0)
        return;
    models_check = 30;
    const std::shared_ptr<const fpv::Media> media = get_media();
    const std::string dir = media ? media->dir : "";
    const uint64_t stamp = fpv::models_stamp(dir);
    if (dir == models_dir && stamp == models_seen)
        return;
    models_dir = dir;
    models_seen = stamp;
    std::shared_ptr<const fpv::Models> models = fpv::load_models(dir);
    for (const std::string &p : models->problems)
        WARN(log).print("fpview: %s\n", p.c_str());
    std::lock_guard<std::mutex> lock(scene.mutex);
    scene.setModels(std::move(models));
}

// A hook struct is laid over the game's own object: it must not have data
// members of its own. One would lie past the end of the game's object, and
// writing it damages whatever the game keeps next in memory (the game then
// crashes somewhere unrelated, later). State goes in variables outside it.
command_result set_active(color_ostream &out, bool on);

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
            // While aiming a shot or a throw the game keeps its map on the
            // aiming cursor's level and takes a click as x, y on that level:
            // a creature on another level was shot at as the tile above or
            // below it on yours. The cursor is put on the clicked tile's level.
            if (game && game->main_interface.adventure.aim_projectile.open) {
                auto &cursor = game->main_interface.adventure.aim_projectile.cursor;
                cursor.x = redirect->picked.x;
                cursor.y = redirect->picked.y;
                cursor.z = redirect->picked.z;
            }
            // without the pointer, so that no hover text is drawn
            const int32_t mx = gps->mouse_x, my = gps->mouse_y;
            const int32_t px = gps->precise_mouse_x, py = gps->precise_mouse_y;
            gps->mouse_x = gps->mouse_y = gps->precise_mouse_x = gps->precise_mouse_y = -1;
            {
                const ProbeTimer pt;
                (screen->*interpose_render.chain)(last_curtick);
                frame_draw_us += pt.us();
                ++frame_draw_n;
            }
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
        {
            const ProbeTimer pt;
            (this->*interpose_render.chain)(last_curtick);
            frame_draw_us += pt.us();
            ++frame_draw_n;
        }
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

#include "inventory_hook.inc"
#include "panels_hook.inc"

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
        // a missing body part cannot be aimed for
        if ((it.kind == ITEM_DOLL || it.kind == ITEM_LIST) && it.index >= 0 && size_t(it.index) < m->entries.size() &&
            m->entries[it.index].gone)
            return true;
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
        auto &list = game->main_interface.adventure.option_list;
        if (click_listed(list.option, choice, std::max(0, int(list.scroll_position))))
            return true;
        // The game shows about a dozen entries at a time; with several
        // creatures on a tile the one wanted is often further down. Scroll
        // its list to the entry, have it drawn, and look again.
        const auto at = std::find(list.option.begin(), list.option.end(), choice);
        if (at == list.option.end())
            return false;
        list.scroll_position = int32_t(at - list.option.begin());
        redraw_hidden();
        return click_listed(list.option, choice, std::max(0, int(list.scroll_position)));
    }

    // The entry of a list the game has drawn, clicked by its name on the
    // screen. `first` is the list's first entry on the page.
    bool click_listed(const std::vector<df::adventure_optionst *> &list, df::adventure_optionst *choice,
                      int first = 0) {
        std::string name;
        choice->getName(&name);
        const std::string needle = name.substr(0, 28);
        // entries that begin alike (two creatures of one kind, two ways to
        // shoot at one) are told apart by their order on the page
        int skip = 0, index = 0;
        for (df::adventure_optionst *other : list) {
            if (other == choice)
                break;
            if (index++ < first)
                continue;
            std::string other_name;
            if (other)
                other->getName(&other_name);
            if (other_name.substr(0, 28) == needle)
                ++skip;
        }
        for (int y = 0; y < gps->dimy && !needle.empty(); ++y) {
            std::string row(size_t(gps->dimx), ' ');
            for (int x = 0; x < gps->dimx; ++x) {
                const unsigned char ch = gps->screen[(x * gps->dimy + y) * 8];
                row[x] = ch >= 32 ? char(ch) : ' ';
            }
            const size_t at = row.find(needle);
            if (at != std::string::npos && skip-- > 0)
                continue;
            if (at != std::string::npos) {
                click_cell(int(at) + 1, y);
                mouse_log.push_back("attack: chose '" + name + "'");
                return true;
            }
        }
        return false;
    }

    // the game's two ways to shoot or throw at the creature that was clicked; false unless both are offered
    bool shot_ways(df::adventure_optionst *&quick, df::adventure_optionst *&careful) {
        quick = careful = nullptr;
        auto &list = game->main_interface.adventure.option_list;
        if (!list.open || attack.unit_id < 0)
            return false;
        for (df::adventure_optionst *option : list.option) {
            auto shot = option ? virtual_cast<df::adventure_movement_shoot_creaturest>(option) : nullptr;
            auto thrown = option ? virtual_cast<df::adventure_movement_throw_item_at_creaturest>(option) : nullptr;
            if (!(shot && shot->target == attack.unit_id) && !(thrown && thrown->target == attack.unit_id))
                continue;
            const bool aimed = shot ? shot->aim_carefully : thrown->aim_carefully;
            if (aimed ? !careful : !quick)
                (aimed ? careful : quick) = option;
        }
        return quick && careful;
    }

    // The game's two ways to strike the creature that was clicked: "Attack"
    // strikes at once, "Combat" goes on to the choice of where to hit. False
    // unless both are offered.
    bool strike_ways(df::adventure_optionst *&quick, df::adventure_optionst *&careful) {
        quick = careful = nullptr;
        auto &list = game->main_interface.adventure.option_list;
        if (!list.open || attack.unit_id < 0)
            return false;
        for (df::adventure_optionst *option : list.option) {
            if (!option)
                continue;
            if (auto menu = virtual_cast<df::adventure_movement_combat_menu_creaturest>(option)) {
                if (!careful && menu->target == attack.unit_id)
                    careful = option;
            } else if (auto melee = virtual_cast<df::adventure_movement_attack_creaturest>(option)) {
                if (!quick && std::find(melee->targets.begin(), melee->targets.end(), attack.unit_id) !=
                                  melee->targets.end())
                    quick = option;
            }
        }
        // the game does not always offer "Attack"; "Combat" alone still gets the buttons
        return careful != nullptr;
    }

    // Empty the cells of the game's "what do you want to do" list, which our
    // two buttons stand in for: the solid block of cells around its entries.
    void hide_option_list() {
        auto &list = game->main_interface.adventure.option_list;
        int x0 = -1, x1 = -1, y0 = -1, y1 = -1;
        for (df::adventure_optionst *option : list.option) {
            std::string name;
            if (option)
                option->getName(&name);
            const std::string needle = name.substr(0, 28);
            for (int y = 0; y < gps->dimy && !needle.empty(); ++y) {
                std::string row(size_t(gps->dimx), ' ');
                for (int x = 0; x < gps->dimx; ++x) {
                    const unsigned char ch = gps->screen[(x * gps->dimy + y) * 8];
                    row[x] = ch >= 32 ? char(ch) : ' ';
                }
                const size_t at = row.find(needle);
                if (at == std::string::npos)
                    continue;
                if (x0 < 0) {
                    x0 = x1 = int(at);
                    y0 = y1 = y;
                }
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
        if (x0 < 0)
            return;
        const int row = y0;
        while (x0 > 0 && cell_used(x0 - 1, row)) --x0;
        while (x1 + 1 < gps->dimx && cell_used(x1 + 1, row)) ++x1;
        while (y0 > 0 && cell_used(x0, y0 - 1)) --y0;
        while (y1 + 1 < gps->dimy && cell_used(x0, y1 + 1)) ++y1;
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) {
                const int idx = x * gps->dimy + y;
                std::memset(&gps->screen[idx * 8], 0, 8);
                gps->screentexpos[idx] = 0;
                gps->screentexpos_lower[idx] = 0;
                gps->screentexpos_anchored[idx] = 0;
                gps->screentexpos_flag[idx] = 0;
            }
    }

    // Input while the two shot buttons are up; true if it was dealt with
    // here. A click on one picks that entry of the game's (unseen) list.
    bool shot_input(std::set<df::interface_key> *input) {
        if (!shot_choice.on || !game)
            return false;
        if ((input && input->count(df::interface_key::LEAVESCREEN)) || enabler->mouse_rbut) {
            if (shot_choice.kind != 2)
                leave_aim_frames = 30;
            return false;       // the game closes its list, and the buttons go with it
        }
        if (!enabler->mouse_lbut)
            return true;        // keys would pick from the list that is not shown
        int px = -1, py = -1;
        if (!pointer_pixels(px, py))
            px = py = -1;
        int hit = px >= 0 ? shot_button_at(px, py) : -1;
        const bool strike = shot_choice.kind == 2;
        if (hit < 0 && px >= 0 && !over_interface(px, py)) {
            // A click beside the buttons: on the creature they are for, a
            // strike at once (clicking it again is quicker than aiming for a
            // button); anywhere else, the buttons go away and the click is
            // taken as a new one.
            const fpv::Pick p = pick_pixels(px, py);
            if (strike && p.valid() && p.unit_id >= 0 && p.unit_id == attack.unit_id) {
                hit = shot_choice.quick ? 0 : 1;
            } else {
                std::set<df::interface_key> leave{df::interface_key::LEAVESCREEN};
                INTERPOSE_NEXT(feed)(&leave);
                shot_choice.on = false;
                attack.frames = 0;
                attack_busy = false;
                return false;
            }
        }
        enabler->mouse_lbut = 0;
        df::adventure_optionst *quick = nullptr, *careful = nullptr;
        if (hit < 0 || !(strike ? strike_ways(quick, careful) : shot_ways(quick, careful)))
            return true;
        if (hit == 0 && !quick)
            return true;
        redraw_hidden();        // the list's cells were emptied: have them drawn to click on
        // dodging is one of the moves behind the game's "Combat" entry
        const bool clicked = click_option(hit == 0 ? quick : careful);
        tidy_map();
        if (clicked && strike) {
            attack.dodge = hit == 2;
            shot_choice.on = false;
            attack.stage = 1;
            attack.waited = 0;
            attack.frames = 150;   // the question, if any, comes when it is our turn
            attack_busy = true;
        } else if (clicked) {
            shot_choice.on = false;
            attack.frames = 0;
            attack_busy = false;
        } else {
            hide_option_list();
            mouse_log.push_back("attack: the shot's entry was not found in the game's list");
        }
        return true;
    }

    // see leave_aim_frames
    void leave_aim_step() {
        if (leave_aim_frames <= 0 || !game)
            return;
        --leave_aim_frames;
        const auto &ui = game->main_interface.adventure;
        if (!ui.aim_projectile.open) {
            leave_aim_frames = 0;
            return;
        }
        if (ui.option_list.open)
            return;   // the list goes first
        leave_aim_frames = 0;
        shot_choice.on = false;
        attack.frames = 0;
        attack_busy = false;
        std::set<df::interface_key> keys{df::interface_key::LEAVESCREEN};
        INTERPOSE_NEXT(feed)(&keys);
    }

    // Shooting and throwing go through the game's own steps: its key, (for a
    // throw) the list of what to throw, the aiming cursor, and the question
    // what on that tile to aim at. The cursor and that question are answered
    // with what was clicked.
    void drive_ranged() {
        using namespace df::enums::interface_key;
        auto &aim = game->main_interface.adventure.aim_projectile;
        auto &list = game->main_interface.adventure.option_list;
        if (attack.stage == 13) {
            // the two buttons are up for as long as the game's list is
            df::adventure_optionst *quick = nullptr, *careful = nullptr;
            if (!shot_choice.on || !aim.open || !shot_ways(quick, careful)) {
                shot_choice.on = false;
                attack.frames = 0;
            } else {
                attack.frames = 6000;
                hide_option_list();
            }
        } else if (attack.stage == 10) {
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
                const bool named = choice != nullptr;
                if (!choice)
                    choice = tile;
                const int ways = choice == tile ? tile_ways : creature_ways;
                df::adventure_optionst *quick = nullptr, *careful = nullptr;
                if (settings.combat_menus && shot_ways(quick, careful)) {
                    // quickly or carefully, at this creature: asked with our
                    // two buttons at the pointer; whoever else is on the tile
                    // is not offered
                    shot_choice.kind = attack.intent == INTENT_THROW ? 1 : 0;
                    shot_choice.dodge = false;
                    shot_choice.quick = true;
                    shot_choice.px = attack.px;
                    shot_choice.py = attack.py;
                    shot_choice.on = true;
                    hide_option_list();
                    attack.stage = 13;
                    attack.frames = 6000;
                    mouse_log.push_back("attack: quick or aimed, asked at the pointer");
                    return;
                }
                // one way to do it to the creature that was clicked: that one,
                // whoever else the list names
                if (choice && ways == 1 && (named || list.option.size() <= 2))
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

    // A right click on a map tile, as if it lay under the middle of the
    // window: the game's list of what can be done there. The game finds the
    // tile on its top-down map, so the map is slid there and drawn for it.
    void right_click_tile(const df::coord &pos) {
        click_tile(pos, true);
    }

    // a left or right click on a map tile, the same way
    void click_tile(const df::coord &pos, bool right) {
        const int tile_px = gps->viewport_zoom_factor / 4;
        const int w = view_out_w.load(), h = view_out_h.load();
        if (tile_px <= 0 || w <= 0 || h <= 0)
            return;
        const int px = (w / 2) / tile_px * tile_px + tile_px / 2, py = (h / 2) / tile_px * tile_px + tile_px / 2;
        int cx = 0, cy = 0;
        if (!interface_cell(px, py, cx, cy))
            return;
        const int32_t wx = *window_x, wy = *window_y, wz = *window_z;
        const int32_t mx = gps->mouse_x, my = gps->mouse_y;
        const int32_t ppx = gps->precise_mouse_x, ppy = gps->precise_mouse_y;
        *window_x = pos.x - px / tile_px;
        *window_y = pos.y - py / tile_px;
        *window_z = pos.z;
        redraw_hidden();
        gps->mouse_x = cx;
        gps->mouse_y = cy;
        gps->precise_mouse_x = px;
        gps->precise_mouse_y = py;
        const int8_t was_l = enabler->mouse_lbut, was_down = enabler->mouse_lbut_down;
        const int8_t was_tracking = enabler->tracking_on;
        enabler->tracking_on = 1;
        enabler->mouse_lbut = right ? 0 : 1;
        enabler->mouse_lbut_down = right ? 0 : 1;
        enabler->mouse_rbut = right ? 1 : 0;
        std::set<df::interface_key> none;
        INTERPOSE_NEXT(feed)(&none);
        enabler->mouse_rbut = 0;
        enabler->mouse_lbut = was_l;
        enabler->mouse_lbut_down = was_down;
        enabler->tracking_on = was_tracking;
        *window_x = wx;
        *window_y = wy;
        *window_z = wz;
        gps->mouse_x = mx;
        gps->mouse_y = my;
        gps->precise_mouse_x = ppx;
        gps->precise_mouse_y = ppy;
        tidy_map();
    }

    // Nocking an arrow is the game's own key; the time it takes is then
    // passed by nock_step. Taking the arrow off again is an entry ("Cease
    // nocking") of the list the game shows for your own tile.
    void drive_nock() {
        using namespace df::enums::interface_key;
        df::unit *adv = World::getAdventurer();
        auto &list = game->main_interface.adventure.option_list;
        if (!adv) {
            attack.frames = 0;
        } else if (attack.stage == 20) {
            if (loading_weapon(adv)) {
                attack.frames = 0;      // already at it
            } else if (weapon_loads) {
                // a crossbow is loaded from the list for your own tile ("Load into ...")
                right_click_tile(adv->pos);
                if (list.open) {
                    attack.stage = 24;
                    attack.frames = 30;
                } else {
                    flash("Cannot load now");
                    mouse_log.push_back("load: no list for your own tile");
                    attack.frames = 0;
                }
            } else {
                std::set<df::interface_key> keys{A_NOCK};
                INTERPOSE_NEXT(feed)(&keys);
                mouse_log.push_back("nock: pressed the game's key");
                attack.stage = 21;
                attack.waited = 0;
                attack.frames = 20;
            }
        } else if (attack.stage == 21) {
            ++attack.waited;
            if (loading_weapon(adv) || list.open) {
                attack.frames = 0;      // begun, or the game asks which arrow: the player's choice
            } else if (attack.waited > 6) {
                flash("Nothing to nock");
                mouse_log.push_back("nock: nothing came of it");
                attack.frames = 0;
            }
        } else if (attack.stage == 24) {
            if (list.open && !list.option.empty()) {
                df::adventure_optionst *choice = nullptr;
                int offered = 0;
                for (df::adventure_optionst *option : list.option)
                    if (option && virtual_cast<df::adventure_item_interact_load_ranged_weaponst>(option)) {
                        choice = option;
                        ++offered;
                    }
                if (offered > 1) {
                    attack.frames = 0;  // several kinds of bolt: the player's choice
                } else if (choice && click_option(choice)) {
                    tidy_map();
                    attack.frames = 0;
                } else if (!choice || attack.frames <= 1) {
                    std::set<df::interface_key> keys{LEAVESCREEN};
                    INTERPOSE_NEXT(feed)(&keys);
                    flash(choice ? "Cannot load now" : "Nothing to load");
                    mouse_log.push_back(choice ? "load: its entry is not on screen" : "load: not offered");
                    attack.frames = 0;
                }
            } else if (!list.open) {
                attack.frames = 0;
            }
        } else if (attack.stage == 22 && weapon_loads) {
            if (unload_weapon(adv)) {
                // the next thing to do with the crossbow is to load it again or shoot
                set_intent(INTENT_SHOOT, false);
                mouse_log.push_back("unload: put the bolt away");
            } else {
                flash("Cannot unload now");
                mouse_log.push_back("unload: the bolt could not be moved");
            }
            attack.frames = 0;
        } else if (attack.stage == 22) {
            right_click_tile(adv->pos);
            if (list.open) {
                attack.stage = 23;
                attack.frames = 30;
            } else {
                flash("Cannot unnock now");
                mouse_log.push_back("unnock: no list for your own tile");
                attack.frames = 0;
            }
        } else if (attack.stage == 23) {
            if (list.open && !list.option.empty()) {
                df::adventure_optionst *choice = nullptr;
                for (df::adventure_optionst *option : list.option)
                    if (option && virtual_cast<df::adventure_item_interact_unnockst>(option))
                        choice = option;
                if (choice && click_option(choice)) {
                    // the next thing to do with the bow is to nock again or shoot
                    set_intent(INTENT_SHOOT, false);
                    tidy_map();
                    attack.frames = 0;
                } else if (!choice || attack.frames <= 1) {
                    std::set<df::interface_key> keys{LEAVESCREEN};
                    INTERPOSE_NEXT(feed)(&keys);
                    flash("Cannot unnock now");
                    mouse_log.push_back(choice ? "unnock: its entry is not on screen" : "unnock: not offered");
                    attack.frames = 0;
                }
            } else if (!list.open) {
                attack.frames = 0;
            }
        } else {
            attack.frames = 0;
        }
    }

    // While an arrow is being nocked the game hands control back every tick
    // and waits for a key. Nothing else is done in that time, so the ticks
    // are waited out for the player, one a frame the game is ready for it.
    void nock_step() {
        df::unit *adv = World::getAdventurer();
        if (!adv || !game || !adventure || !world || !showing || attack.driving || attack.frames > 0)
            return;
        if (!loading_weapon(adv) || !plain_adventure_focus() || game->main_interface.adventure.option_list.open)
            return;
        if (adventure->player_control_state != df::adventure_game_loop_type::TAKING_INPUT ||
            adventure->wait_timer > 0 || world->status.temp_flag.bits.adv_have_more)
            return;
        std::set<df::interface_key> keys{df::interface_key::A_SHORT_WAIT};
        INTERPOSE_NEXT(feed)(&keys);
    }

    // The game can hand control back while a strike of yours is still on its
    // way; it then only lands once more time passes. Those ticks are waited
    // out for the player, like the nocking of an arrow, but not for ever.
    void strike_step() {
        df::unit *adv = World::getAdventurer();
        if (!adv || !game || !adventure || !world || !showing || !striking(adv)) {
            strike_ticks = 0;
            return;
        }
        if (attack.driving || attack.frames > 0 || strike_ticks >= 40 || !plain_adventure_focus() ||
            game->main_interface.adventure.option_list.open || game->main_interface.adventure.attack.open)
            return;
        if (adventure->player_control_state != df::adventure_game_loop_type::TAKING_INPUT ||
            adventure->wait_timer > 0 || world->status.temp_flag.bits.adv_have_more)
            return;
        ++strike_ticks;
        std::set<df::interface_key> keys{df::interface_key::A_SHORT_WAIT};
        INTERPOSE_NEXT(feed)(&keys);
    }

    // With "Auto attack" off, walking into a creature you are in a conflict
    // with does not strike it: the step is taken from the game's list for
    // that tile ("Move ..."), if it is there. True if the keys were dealt
    // with here.
    bool step_not_strike(const std::set<df::interface_key> &keys) {
        using namespace df::enums::interface_key;
        static const df::interface_key moves[3][8] = {
            {A_MOVE_N, A_MOVE_NE, A_MOVE_E, A_MOVE_SE, A_MOVE_S, A_MOVE_SW, A_MOVE_W, A_MOVE_NW},
            {A_MOVE_N_UP, A_MOVE_NE_UP, A_MOVE_E_UP, A_MOVE_SE_UP, A_MOVE_S_UP, A_MOVE_SW_UP, A_MOVE_W_UP,
             A_MOVE_NW_UP},
            {A_MOVE_N_DOWN, A_MOVE_NE_DOWN, A_MOVE_E_DOWN, A_MOVE_SE_DOWN, A_MOVE_S_DOWN, A_MOVE_SW_DOWN,
             A_MOVE_W_DOWN, A_MOVE_NW_DOWN}};
        df::unit *adv = World::getAdventurer();
        if (settings.auto_attack || !active || !showing || !adv || !game || !world || attack.driving ||
            attack.frames > 0 || !plain_adventure_focus())
            return false;
        int dir = -1, dz = 0;
        for (int level = 0; level < 3 && dir < 0; ++level)
            for (int i = 0; i < 8 && dir < 0; ++i)
                if (keys.count(moves[level][i])) {
                    dir = i;
                    dz = level == 1 ? 1 : level == 2 ? -1 : 0;
                }
        if (dir < 0)
            return false;
        const df::coord dest(int16_t(adv->pos.x + DIR_DX[dir]), int16_t(adv->pos.y + DIR_DY[dir]),
                             int16_t(adv->pos.z + dz));
        df::unit *foe = nullptr;
        for (df::unit *u : world->units.active)
            if (u && u != adv && u->pos == dest && Units::isActive(u) && !Units::isDead(u) &&
                fpv::conflictWithYou(u, adv) >= 0) {
                foe = u;
                break;
            }
        if (!foe)
            return false;
        step_onto(dest, "In the way (Auto attack is off)");
        return true;
    }

    // A step onto a tile, taken from the game's list for it ("Move ..."): a
    // plain step there would strike whoever stands on it. The list is opened
    // now and the step picked from it in the next frame (drive_tile_list):
    // the game takes nothing more in the frame it opened it.
    void step_onto(const df::coord &dest, const char *refused) {
        right_click_tile(dest);
        if (!game->main_interface.adventure.option_list.open) {
            flash(refused);
            return;
        }
        attack.target = dest;
        attack.unit_id = -1;
        attack.stage = 41;
        attack.frames = 10;
        attack.waited = 0;
        tile_list_refused = refused;
        attack_busy = true;
    }

    // the step or use picked from the game's list for a tile (step_onto, use_tile)
    void drive_tile_list() {
        using namespace df::enums::interface_key;
        auto &list = game->main_interface.adventure.option_list;
        if (!list.open) {
            attack.frames = 0;
            return;
        }
        if (attack.waited++ < 1)
            return;     // the list is drawn once first
        if (attack.stage == 41) {
            df::adventure_optionst *step = nullptr;
            for (df::adventure_optionst *option : list.option)
                if (auto move = option ? virtual_cast<df::adventure_movement_movest>(option) : nullptr)
                    if (!step && move->dest == attack.target)
                        step = option;
            bool stepped = false;
            if (step) {
                redraw_hidden();        // the list's cells are drawn to click on
                stepped = click_option(step);
                tidy_map();
            }
            if (!stepped && list.open) {
                std::set<df::interface_key> leave{LEAVESCREEN};
                INTERPOSE_NEXT(feed)(&leave);
                flash(tile_list_refused);
            }
            mouse_log.push_back(std::string("step: ") + (stepped ? "taken from the game's list" : "not offered"));
            attack.frames = 0;
            return;
        }
        // stage 40: Interact with a thing. One way to use it: done. Several,
        // or other entries (pick up, chop ...): the list stays up for the
        // player. Only ways to move there, or looks at what is there: nothing.
        df::adventure_optionst *use = nullptr;
        int uses = 0, others = 0;
        for (df::adventure_optionst *option : list.option) {
            if (!option)
                continue;
            if (virtual_cast<df::adventure_movement_building_interactst>(option) ||
                virtual_cast<df::adventure_movement_item_interactst>(option)) {
                use = option;
                ++uses;
            } else if (!virtual_cast<df::adventure_movement_optionst>(option) &&
                       !virtual_cast<df::adventure_option_view_itemst>(option)) {
                ++others;
            }
        }
        attack.frames = 0;
        if (uses == 1) {
            redraw_hidden();
            const bool clicked = click_option(use);
            tidy_map();
            mouse_log.push_back(std::string("use: ") + (clicked ? "picked" : "entry not found on screen"));
            return;
        }
        if (uses > 1 || others > 0)
            return;
        std::set<df::interface_key> leave{LEAVESCREEN};
        INTERPOSE_NEXT(feed)(&leave);
        df::unit *adv = World::getAdventurer();
        const df::coord &pos = attack.target;
        const bool far = adv && (std::abs(adv->pos.x - pos.x) > 1 || std::abs(adv->pos.y - pos.y) > 1 ||
                                 std::abs(adv->pos.z - pos.z) > 1);
        flash(far ? "Too far away" : "Nothing to use there");
    }

    // Info: the game's own sheet for a creature, building or item
    void show_info(df::unit *unit, const df::coord &pos) {
        auto &vs = game->main_interface.view_sheets;
        df::building *bld = unit ? nullptr : Buildings::findAtTile(pos);
        df::item *item = unit || bld ? nullptr : ground_item_at(pos);
        if (!unit && !bld && !item)
            return;
        vs.context = df::view_sheets_context_type::REGULAR_PLAY;
        vs.active_sub_tab = 0;
        vs.scroll_position = 0;
        if (unit) {
            vs.active_sheet = df::view_sheet_type::UNIT;
            vs.active_id = unit->id;
            vs.viewing_unid.clear();
            vs.viewing_unid.push_back(unit->id);
        } else if (bld) {
            vs.active_sheet = df::view_sheet_type::BUILDING;
            vs.active_id = bld->id;
            vs.viewing_bldid = bld->id;
        } else {
            vs.active_sheet = df::view_sheet_type::ITEM;
            vs.active_id = item->id;
            vs.viewing_itid.clear();
            vs.viewing_itid.push_back(item->id);
        }
        vs.open = true;
    }

    // Interact with a creature: the game's talk key, which asks for a
    // creature to be clicked. The click on this one and the pick of "Start
    // new conversation with ..." follow in the next frames (drive_talk): the
    // game's dialog takes nothing before it has been drawn once.
    void talk_to(df::unit *unit) {
        using namespace df::enums::interface_key;
        std::set<df::interface_key> keys{A_TALK};
        INTERPOSE_NEXT(feed)(&keys);
        if (!game->main_interface.adventure.conversation.open) {
            mouse_log.push_back("talk: the game opened no dialog");
            return;
        }
        attack.unit_id = unit->id;
        attack.stage = 30;
        attack.frames = 30;
        attack.waited = 0;
        attack_busy = true;
    }

    void drive_talk() {
        using namespace df::enums::interface_key;
        auto &conv = game->main_interface.adventure.conversation;
        df::unit *unit = df::unit::find(attack.unit_id);
        if (!unit || !conv.open || !conv.selecting_conversation) {
            attack.frames = 0;
            return;
        }
        if (attack.stage == 30) {
            click_tile(unit->pos, false);
            attack.stage = 31;
            mouse_log.push_back("talk: clicked " + std::to_string(unit->pos.x) + "," + std::to_string(unit->pos.y) + " options " + std::to_string(conv.select_option.size()));
            return;
        }
        df::adventure_optionst *choice = nullptr, *going_on = nullptr;
        int going_on_n = 0;
        for (df::adventure_optionst *option : conv.select_option) {
            auto talk = option ? virtual_cast<df::adventure_option_talk_new_conversationst>(option) : nullptr;
            if (talk && talk->unit_id == unit->id)
                choice = option;
            else if (option && virtual_cast<df::adventure_option_talk_existing_conversationst>(option)) {
                going_on = option;
                ++going_on_n;
            }
        }
        if (!choice && going_on_n == 1)
            choice = going_on;     // already talking with them: that conversation
        if (choice) {
            redraw_hidden();
            const bool clicked = click_listed(conv.select_option, choice);
            tidy_map();
            mouse_log.push_back(std::string("talk: ") + (clicked ? "started" : "entry not found on screen"));
            attack.frames = 0;
            return;
        }
        if (attack.waited == 2)
            click_tile(unit->pos, false);   // once more: the first may have come too early
        if (++attack.waited > 5) {
            // not offered: too far, asleep, an animal ...
            std::set<df::interface_key> leave{LEAVESCREEN};
            INTERPOSE_NEXT(feed)(&leave);
            flash("Cannot talk to " + Units::getReadableName(unit) + " now");
            mouse_log.push_back("talk: not offered");
            attack.frames = 0;
        }
    }

    // Interact with a thing: what the game's list for its tile offers to do
    // with a building or item there (pull a lever, raise a bucket, push a
    // cart ...), picked in the next frame (drive_tile_list).
    void use_tile(const df::coord &pos) {
        right_click_tile(pos);
        if (!game->main_interface.adventure.option_list.open) {
            flash("Nothing to use there");
            return;
        }
        attack.target = pos;
        attack.unit_id = -1;
        attack.stage = 40;
        attack.frames = 10;
        attack.waited = 0;
        attack_busy = true;
    }

    // What an intent does with the creature or tile that was clicked, now
    // that the game takes input.
    void carry_out(int which, int32_t unit_id, df::coord target, int px, int py) {
        df::unit *unit = unit_id >= 0 ? df::unit::find(unit_id) : nullptr;
        if (unit_id >= 0 && !unit)
            return;     // gone meanwhile
        if (unit)
            target = unit->pos;
        if (which == INTENT_INFO) {
            show_info(unit, target);
            return;
        }
        if (which == INTENT_INTERACT) {
            if (unit)
                talk_to(unit);
            else
                use_tile(target);
            return;
        }
        attack.unit_id = unit_id;
        attack.intent = which;
        attack.dodge = false;
        attack.target = target;
        attack.px = px;
        attack.py = py;
        if (which == INTENT_UNNOCK || (which == INTENT_SHOOT && !arrow_nocked)) {
            // nocking and taking the arrow off need no target
            attack.unit_id = -1;
            attack.stage = which == INTENT_UNNOCK ? 22 : 20;
            attack.frames = 2;
            attack_busy = true;
            return;
        }
        if (which == INTENT_SHOOT || which == INTENT_THROW) {
            attack.stage = 10;
            attack.frames = 2;
            attack_busy = true;
            return;
        }
        // strike or wrestle: the game's list for the creature's tile, then its attack
        right_click_tile(target);
        if (game->main_interface.adventure.option_list.open) {
            attack.stage = 0;
            attack.frames = 30;
            attack_busy = true;
        }
    }

    // a click kept while the game was busy: carried out once it takes input
    void pending_step() {
        if (!pending_click.on)
            return;
        df::unit *adv = World::getAdventurer();
        if (!showing || now_ms() > pending_click.until_ms) {
            pending_click.on = false;
            return;
        }
        if (attack.driving || attack.frames > 0 || !game || !plain_adventure_focus() ||
            game->main_interface.adventure.option_list.open || !game_takes_input(adv))
            return;
        pending_click.on = false;
        mouse_log.push_back("click: carried out after the game was busy");
        carry_out(pending_click.intent, pending_click.unit_id, pending_click.target, pending_click.px,
                  pending_click.py);
    }

    void drive_attack() {
        if (attack.frames <= 0 || !game || !adventure)
            return;
        --attack.frames;
        using namespace df::enums::interface_key;
        using Mode = df::adventure_interface_attack_mode_type;
        attack.driving = true;
        auto &prompt = game->main_interface.adventure.attack;
        if (attack.stage >= 40) {
            drive_tile_list();
        } else if (attack.stage >= 30) {
            drive_talk();
        } else if (attack.stage >= 20) {
            drive_nock();
        } else if (attack.stage >= 10) {
            drive_ranged();
        } else if (attack.stage == 2) {
            // the two strike buttons are up for as long as the game's list is
            df::adventure_optionst *quick = nullptr, *careful = nullptr;
            if (!shot_choice.on || !strike_ways(quick, careful)) {
                shot_choice.on = false;
                attack.frames = 0;
            } else {
                attack.frames = 6000;
                hide_option_list();
            }
        } else if (attack.stage == 1 && !prompt.open && adventure->menu != df::ui_advmode_menu::AttackConfirm &&
                   striking(World::getAdventurer())) {
            // the strike is on its way (a quick one asks nothing): done here,
            // so that strike_step and a click kept meanwhile can go on
            attack.frames = 0;
        } else if (attack.stage == 1 && !prompt.open && attack.waited > 0 &&
                   adventure->menu != df::ui_advmode_menu::AttackConfirm) {
            attack.frames = 0;      // the move question was answered and has closed
        } else if (adventure->menu == df::ui_advmode_menu::AttackConfirm ||
                   (attack.stage == 1 && prompt.open && prompt.mode == Mode::CONFIRM)) {
            // "Really start a conflict with ...?": yes, that is what Ctrl-click means
            std::set<df::interface_key> keys{A_ATTACK_CONFIRM};
            INTERPOSE_NEXT(feed)(&keys);
            mouse_log.push_back("attack: confirmed");
            attack.frames = std::max(attack.frames, 30);
        } else if (attack.stage == 1 && prompt.open && prompt.mode == Mode::MOVE_CHOICE) {
            // strike or wrestle: the intent has decided that already; or
            // dodge, if that button was clicked
            // The question can open in this very frame (when it comes on our
            // turn rather than at once), and a dialog takes no click in the
            // frame it opened in. So the click comes a frame later, and is
            // made again for as long as the question stays up.
            const bool wrestle = attack.intent == INTENT_WRESTLE, dodge = attack.dodge;
            const auto wanted = dodge ? df::attack_move_choice_type::DODGE_AWAY
                                : wrestle ? df::attack_move_choice_type::WRESTLE : df::attack_move_choice_type::STRIKE;
            const int seen = attack.waited++;
            attack.frames = std::max(attack.frames, 2);
            if (seen % 4 != 1) {
                // waiting for the dialog to take clicks, or for the last one to work
            } else if (seen > 16) {
                mouse_log.push_back("attack: the move question did not take the click; left to the player");
                attack.frames = 0;
            } else if (std::find(prompt.move_choice.begin(), prompt.move_choice.end(), wanted) ==
                       prompt.move_choice.end()) {
                flash(dodge ? "Cannot dodge now" : wrestle ? "Cannot wrestle now" : "Cannot strike now");
                mouse_log.push_back("attack: the wanted move is not offered");
                attack.frames = 0;
            } else {
                std::vector<ScreenRow> rows;
                std::string title;
                int tx, ty;
                read_dialog(rows, title, tx, ty);
                const std::string word = dodge ? "Dodge" : wrestle ? "Wrestle" : "Strike";
                for (const ScreenRow &r : rows)
                    if (r.name.rfind(word, 0) == 0) {
                        click_cell(r.x + 3, r.y);
                        mouse_log.push_back("attack: chose " + word);
                        break;
                    }
            }
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
            if (attack.intent == INTENT_STRIKE && settings.combat_menus && choice) {
                // At once or aimed: asked with our buttons at the pointer. The
                // game sometimes offers only "Combat" for a creature; then
                // there is no Quick button, but Aimed (and Dodge) still show.
                shot_choice.kind = 2;
                shot_choice.quick = fallback != nullptr;
                shot_choice.dodge = struck_at(World::getAdventurer());
                shot_choice.px = attack.px;
                shot_choice.py = attack.py;
                shot_choice.on = true;
                hide_option_list();
                attack.stage = 2;
                attack.frames = 6000;
                mouse_log.push_back("attack: quick or aimed strike, asked at the pointer");
                attack.driving = false;
                attack_busy = true;
                return;
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
                attack.waited = 0;
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
            // What stays ours on the other screens of the adventure (inventory,
            // journal ...): the paperdoll where the game draws the portrait, so
            // the two do not take turns, and the list of nearby places stays off.
            bool doll = false;
            if (adv && adventure && gps && !debug_noblank) {
                hide_nearby_again();
                if (body_shown && portrait_drawn()) {
                    hide_portrait(false);
                    doll = true;
                }
            }
            doll_only = doll;
            // menus that need the map (look, aim, travel ...) get the normal view back
            if ((showing || zoom_saved) && !journal_being_replaced()) {
                restore_zoom();
                ++gps->force_full_display_count;
            }
            mini_n = 0;
            mini_taken = 0;
            showing = false;
            corner_live = false;
            full_capture = true;
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        zoom_for_capture();
        ++capture_frame;
        load_ui_font();
        follow_models();

        {
            std::lock_guard<std::mutex> lock(scene.mutex);
            const int old_w = scene.w, old_h = scene.h, old_d = scene.d;
            if (!scene.resize()) {
                showing = false;
                return;
            }
            // Walking far makes the game move its window on the world, in steps
            // of 48 tiles: every local coordinate then means another place. What
            // is still loaded is moved to where it is now, and only the part
            // newly loaded is read (from the map, a few levels a frame, by the
            // reads below). Anything else (another level range, a far jump)
            // starts again from nothing.
            static int region_x = -1, region_y = -1, region_z = -1;
            if (world->map.region_x != region_x || world->map.region_y != region_y ||
                world->map.region_z != region_z) {
                const int dx = (world->map.region_x - region_x) * 48, dy = (world->map.region_y - region_y) * 48;
                const bool kept = region_x != -1 && world->map.region_z == region_z && scene.w == old_w &&
                                  scene.h == old_h && scene.d == old_d && std::abs(dx) < scene.w && std::abs(dy) < scene.h;
                if (kept) {
                    scene.shift(dx, dy);
                    ++stat_map_shifts;
                } else {
                    if (region_x != -1)
                        scene.reset();
                    scene.resize();
                    full_capture = true;
                }
                region_x = world->map.region_x;
                region_y = world->map.region_y;
                region_z = world->map.region_z;
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
        const int zlo = std::max(0, az - settings.max_depth.load());
        const int zhi = std::min(scene.d - 1, az + settings.max_height.load());
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
                {
                    const ProbeTimer pt;
                    INTERPOSE_NEXT(render)(curtick);
                    frame_draw_us += pt.us();
                    ++frame_draw_n;
                }
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
            const ProbeTimer pt;
            std::lock_guard<std::mutex> lock(scene.mutex);
            scene.readLevel(z);
            frame_read_us += pt.us();
        };

        // what is on screen is read every frame: it has the creatures near us
        read(az, shown_x, shown_y);
        if (settings.minimap && settings.minimap_ascii) {
            mini_taken = 0;
            minimap_ascii_build(adv);
        } else if (on_z == adv->pos.z && minimap_take(gps->main_viewport, adv->pos.x - shown_x, adv->pos.y - shown_y)) {
            mini_kept = 0;
        } else if (!settings.minimap || ++mini_kept > MINI_KEEP) {
            mini_taken = 0;
        }
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
                const ProbeTimer pt;
                std::lock_guard<std::mutex> lock(scene.mutex);
                scene.readLevel(z, rect);
                frame_read_us += pt.us();
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

        {
            fpv::FarSettings fs;
            fs.on = settings.far_land;
            fs.horizon = settings.horizon;
            fs.trees = settings.far_trees;
            fs.view_distance = settings.view_distance;
            const ProbeTimer pt;
            farland.update(scene, ax + 0.5f, ay + 0.5f, fs);
            frame_read_us += pt.us();
        }

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
            // with an attacking intent, while attacking or aiming, creatures are
            // outlined by how an attack on them would go
            const bool outlines = combat_intent(effective_intent()) || attack_busy ||
                                  adventure->menu != df::ui_advmode_menu::Default ||
                                  (game && game->main_interface.adventure.aim_projectile.open);
            // aiming a strike or a hold: only what is within reach is outlined
            const bool aiming_shot = game && game->main_interface.adventure.aim_projectile.open;
            const int now_intent = effective_intent();
            const bool melee = !aiming_shot && (now_intent == INTENT_STRIKE || now_intent == INTENT_WRESTLE);
            {
                const ProbeTimer pt;
                scene.buildUnits(zlo, zhi, adv, capture_frame, outlines, melee, settings.smooth,
                                 settings.any_conflict_discs);
                probe_units.add(pt.us());
            }
            collect_foes();
            track_combat(adv, capture_frame);
            // lying on the ground, you see the world from down there
            const bool prone = !free_camera && adv->flags1.bits.on_ground;
            // riding, from up on its back (a fall from it leaves you prone, not riding)
            const bool riding = !free_camera && !prone && fpv::mountOf(adv) >= 0;
            eye = (az + scene.groundRaise(ax, ay, az)) * LEVEL_H + (prone ? EYE_PRONE : EYE) +
                  (riding ? EYE_RIDING : 0.0f);
        }

        // distance haze: pale by day under the sky, dark at night and underground
        const df::tile_designation *des = Maps::getTileDesignation(ax, ay, az);
        const float light = daylight_now();
        const bool underground = des && des->bits.subterranean;
        const float haze = underground ? 0.0f : light;
        const uint32_t fog = fpv::rgb(int(8 + 136 * haze), int(9 + 165 * haze), int(12 + 190 * haze));
        const int pick = debug_weather.load();
        fpv::Weather weather = read_weather(ax, ay, az);
        pick_weather(weather, pick);

        {
            std::lock_guard<std::mutex> lock(target_mutex);
            target.x = ax + 0.5f;
            target.y = ay + 0.5f;
            target.eye = eye;
            target.zlo = zlo;
            target.zhi = zhi;
            target.daylight = light;
            target.fog = fog;
            target.underground = underground;
            target.outside = des && des->bits.outside;
            target.weather = weather;
            target.weather_pick = pick;
            target.valid = true;
            if (snap)
                ++target.snap;
        }
        showing = true;
        doll_only = false;
        publish_state(true, settings.minimap);
        publish_bar();
        // Not between two panels either: on a switch from the inventory to the
        // journal the game's inventory is closed a frame or more before the
        // journal is up, and mouse look taking the pointer meanwhile put it back
        // in the middle of the window.
        look_ok = plain_adventure_focus() && game &&
                  !game->main_interface.adventure.option_list.open && !jr_up && !jr.want && !inv_up &&
                  panel_switch.load() < 0 && !radial_open && !radial_config_open && msg_drag.load() < 0;
        can_shoot = update_held(adv, capture_frame % 16 == 1);
        if (capture_frame % 4 == 1)
            update_body(adv);
        portrait_ours = body_shown.load();

        // The pointer is hidden from the game while it is over the view, so the
        // game shows no hover text of its own (it would describe the hidden
        // top-down map). We name exactly what is highlighted instead.
        // Only things get that line (creatures, items, plants, buildings):
        // the ground and walls are not named.
        std::vector<std::string> hint;
        hover_lines.clear();
        if (frame_pointer.over_view && frame_pointer.pick.valid()) {
            const fpv::Pick &p = frame_pointer.pick;
            const std::string name = describe(p);
            if (!name.empty()) {
                hover_lines.push_back(name);
                if (settings.cursor_hint && (p.unit_id >= 0 || p.kind >= 0) && !describe(p, false).empty())
                    hint.push_back(name);
            }
        }
        // over a picture in the row of foes: who that is
        if (const int who = frame_pointer.hidden && !frame_pointer.over_view
                                ? foe_at(frame_pointer.px, frame_pointer.py)
                                : -1;
            who >= 0)
            if (df::unit *unit = df::unit::find(who)) {
                const std::string name = Units::getReadableName(unit);
                hover_lines.push_back(name);
                hint.push_back(name);
            }

        // The game's own hover box says everything about a tile (all the
        // items on it, for one), so it is kept, and made to describe the tile
        // the pointer is on in the 3D view. The game works that tile out from
        // where the pointer is on its top-down map; for the last draw of the
        // frame that map is slid so that the tile we look at lies under the
        // pointer. The draws before it, of other levels, may have left a box
        // for some other tile (the game never wipes the interface grid over
        // the map), so on the plain adventure screen the grid is emptied
        // first; that last draw puts everything else back.
        // With a dialog of the game's open the same is done, so that the grid
        // holds only what the game drew this frame: that is compared with the
        // plain screen to see whether the dialog leaves the corners alone
        // (hide_top_right, bottom_is_plain).
        const bool plain = plain_adventure_under();
        if (!debug_noblank) {
            {
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
            {
                const ProbeTimer pt;
                INTERPOSE_NEXT(render)(curtick);
                frame_draw_us += pt.us();
                ++frame_draw_n;
            }
            inventory_grid_capture();
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
        // With the game's inventory window taken off for our panel the screen is
        // the plain one under it: its corners, bottom and doll are laid out as
        // there (without taking it for the plain screen's picture). Left to
        // the checks for a dialog, the minimap, the message list or the doll's
        // square could go for a frame when the inventory opened.
        const bool like_plain = !plain && inv_window_erased && !debug_noblank;
        hide_top_right(!debug_noblank, plain, like_plain);
        hide_action_prompt(plain && !debug_noblank);
        const bool bottom = bottom_is_plain(!debug_noblank, plain) || inv_window_erased;
        hide_interface_under_minimap(plain && !debug_noblank, like_plain);
        // with the ring, after the bottom row is read (the speed display is found
        // from the buttons right of the portrait, which lie under the doll's square)
        if (!settings.radial_menu || debug_noblank)
            hide_portrait((plain || like_plain) && !debug_noblank);
        if (settings.radial_menu && !debug_noblank) {
            remove_bottom_buttons(plain, like_plain);
            // under a dialog the portrait went with the game's buttons, unless it is part of the dialog
            if (plain || like_plain)
                hide_portrait(true);
        } else {
            msg_ok = false;
            hide_corner_buttons(bottom);
            move_bottom_buttons(bottom);
        }
        // sneaking: the game sets the ambush flag on the adventurer while it sneaks
        adv_sneaking = adv->flags1.bits.hidden_in_ambush;
        adv_prone = adv->flags1.bits.on_ground;
        radial_wheel();
        gait_wheel(plain_adventure_focus() && !radial_open && !radial_config_open);

        tidy_top(plain && !debug_noblank);
        if (!plain && !debug_noblank)
            hide_nearby_again();
        update_messages();
        paint_hint(hint, frame_pointer.px, frame_pointer.py);
        // over a bar of the doll: what it stands at, of how much
        if (!frame_pointer.over_view && !debug_noblank) {
            const std::string text = vital_hint(adv, frame_pointer.px, frame_pointer.py);
            int pcx = 0, pcy = 0;
            // left of the bars' square
            if (!text.empty() && interface_cell(frame_pointer.px, frame_pointer.py, pcx, pcy))
                paint_hint_at(text, gps->dimx - bars_cols.load() - int(text.size()) - 3, pcy);
        }

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
            inv_up = false;
            inv_drag = -1;
            jr_close();
            INTERPOSE_NEXT(render)(curtick);
            return;
        }
        MouseTrace trace("render");
        const ProbeTimer whole_frame;
        {
            static std::chrono::steady_clock::time_point last_render;
            const auto now_t = std::chrono::steady_clock::now();
            const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now_t - last_render).count();
            if (gap > 35 && gap < 5000)
                hitch("game gap " + std::to_string(gap) + " ms");
            last_render = now_t;
        }
        frame_draw_us = frame_draw_n = frame_read_us = 0;
        last_curtick = curtick;
        last_capture_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (swallow_left > 0)
            --swallow_left;
        if (showing) {
            dismiss_announcements();
            dismiss_popups();
        }
        frame_pointer.begin();
        const int32_t drew_x = *window_x, drew_y = *window_y, drew_z = *window_z;
        // drawn on an empty grid with the pointer hidden: the interface without a hover box
        const bool tip_base_now = showing && frame_pointer.hidden && !debug_noblank && plain_adventure_focus();
        if (tip_base_now)
            wipe_grid();
        {
            const ProbeTimer pt;
            INTERPOSE_NEXT(render)(curtick);
            frame_draw_us += pt.us();
            ++frame_draw_n;
        }
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
        inventory_frame();
        journal_frame(Gui::getCurViewscreen(true) == static_cast<df::viewscreen *>(this));
        drive_attack();
        leave_aim_step();
        nock_step();
        strike_step();
        pending_step();
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
        if (showing) {
            if (whole_frame.us() > 20000)
                hitch("game frame " + std::to_string(whole_frame.us() / 1000) + " ms: draws " +
                      std::to_string(frame_draw_n) + " " + std::to_string(frame_draw_us / 1000) + " ms, reads " +
                      std::to_string(frame_read_us / 1000) + " ms");
            probe_capture.add(whole_frame.us());
            probe_game_draws.add(frame_draw_us);
            probe_reads.add(frame_read_us);
            probe_draw_count = frame_draw_n;
        }
    }

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
        const ProbeTimer ours;
        if (!attack.driving)
            swallow_rest_of_click();
        // what the inventory panel asks of the game (never from inside the drawing)
        inventory_logic();
        panels_logic();
        // the camera on the ring (Tab): back to the normal view, as the command does
        if (radial_view_off.exchange(false) && active)
            set_active(Core::getInstance().getConsole(), false);
        MouseTrace trace("logic");
        // The game works out what its hover box says here, from where the
        // pointer is on its top-down map. So for this step that map is slid to
        // put the tile the pointer is on in the 3D view under the pointer
        // (nothing under it: the pointer is hidden from the game).
        std::unique_ptr<PointerRedirect> aim_tip;
        int px = -1, py = -1;
        int bar_px, bar_py;
        const bool on_corner = pointer_pixels(px, py) && (in_corner(px, py) || bar_source(px, py, bar_px, bar_py));
        if (active && showing && (settings.game_tips || on_corner) && !debug_noblank && !attack.driving && !inv_up && !jr_up)
            aim_tip = std::make_unique<PointerRedirect>();
        const uint32_t ours_us = ours.us();
        const ProbeTimer theirs;
        in_game_tick = true;
        INTERPOSE_NEXT(logic)();
        last_tick_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        in_game_tick = false;
        const uint32_t theirs_us = theirs.us();
        aim_tip.reset();
        probe_logic_ours.add(ours_us);
        probe_logic_game.add(theirs_us);
        if (theirs_us > 15000)
            hitch("game tick " + std::to_string(theirs_us / 1000) + " ms (ours " + std::to_string(ours_us / 1000) +
                  " ms)");
    }

    DEFINE_VMETHOD_INTERPOSE(void, feed, (std::set<df::interface_key> *input)) {
        if (hitch_on && input && !input->empty())
            hitch("feed key " + std::to_string(int(*input->begin())) + (input->size() > 1 ? " +" : ""));
        if (!attack.driving)
            swallow_rest_of_click();
        if (active && showing && !attack.driving) {
            // the inventory panel takes the mouse while it is up
            if (inventory_input(input))
                return;
            // so does the journal panel
            if (const int j = journal_input(input)) {
                if (j == 2) {
                    std::set<df::interface_key> keys{df::interface_key::A_LOG};
                    INTERPOSE_NEXT(feed)(&keys);
                }
                return;
            }
            if (journal_key(input))
                return;
            // the radial menu (Tab): it takes the mouse while it is up; an action's key goes to the game
            {
                df::interface_key send = df::interface_key::NONE;
                const int r = radial_input(input, send);
                if (r == 2) {
                    std::set<df::interface_key> keys{send};
                    INTERPOSE_NEXT(feed)(&keys);
                }
                if (r)
                    return;
            }
            // A click on the doll's square opens your sheet, wherever on the square it
            // lands; the square is larger than the game's portrait under it, and a
            // click beside the portrait went to the game's hidden buttons there
            // (companions, the speed display). One on the bars' square is ours too.
            if ((enabler->mouse_lbut || enabler->mouse_rbut) && portrait_ours && plain_adventure_focus()) {
                int px, py, cx, cy;
                if (pointer_pixels(px, py) && !game_cell_at(px, py) && interface_cell(px, py, cx, cy)) {
                    const int rows = doll_rows.load();
                    const bool doll = cx < doll_cols.load() && cy >= gps->dimy - rows;
                    const bool bars = cx >= gps->dimx - bars_cols.load() &&
                                      cy >= gps->dimy - rows - passing_rows.load();
                    if (doll || bars) {
                        if (doll && enabler->mouse_lbut)
                            if (df::unit *adv = World::getAdventurer())
                                show_info(adv, adv->pos);
                        enabler->mouse_lbut = 0;
                        enabler->mouse_lbut_down = 0;
                        enabler->mouse_rbut = 0;
                        enabler->mouse_rbut_down = 0;
                        return;
                    }
                }
            }
            // a click on the message panel is the panel's; the game must not take it for one on the map
            if (enabler->mouse_lbut || enabler->mouse_rbut) {
                int px, py;
                if (pointer_pixels(px, py) && in_messages(px, py) && !game_cell_at(px, py)) {
                    if (enabler->mouse_lbut)
                        messages_click(px, py);
                    enabler->mouse_lbut = 0;
                    enabler->mouse_rbut = 0;
                    return;
                }
                if (pointer_pixels(px, py) && in_frozen_bar(px, py)) {
                    enabler->mouse_lbut = 0;
                    enabler->mouse_rbut = 0;
                    return;
                }
            }
            // a click on the row of foes under the minimap turns the camera
            if (enabler->mouse_lbut) {
                int px, py;
                if (pointer_pixels(px, py) && foes_click(px, py)) {
                    enabler->mouse_lbut = 0;
                    return;
                }
            }
            // the castle right of the compass: the game's nearby places on or off
            if (enabler->mouse_lbut && plain_adventure_focus()) {
                int px, py;
                if (pointer_pixels(px, py) && places_button_at(px, py)) {
                    enabler->mouse_lbut = 0;
                    enabler->mouse_lbut_down = 0;
                    toggle_compass_places();
                    return;
                }
            }
            // a click on a place's label under the compass opens the journal at it
            if (enabler->mouse_lbut && plain_adventure_focus()) {
                int px, py, kind = JK_NONE;
                int32_t id = -1, sub = -1;
                if (settings.journal_panel && pointer_pixels(px, py) && mark_entry_at(px, py, kind, id, sub)) {
                    enabler->mouse_lbut = 0;
                    enabler->mouse_lbut_down = 0;
                    jr_open(-1, jr_ref(kind, id, sub));
                    return;
                }
                const int32_t site = pointer_pixels(px, py) ? mark_site_at(px, py) : -1;
                if (site >= 0) {
                    enabler->mouse_lbut = 0;
                    enabler->mouse_lbut_down = 0;
                    journal_site = site;
                    journal_tries = 60;
                    std::set<df::interface_key> keys{df::interface_key::A_LOG};
                    INTERPOSE_NEXT(feed)(&keys);
                    return;
                }
            }
            if (shot_input(input))
                return;
            if (menu_input(input))
                return;
            // R: the next intent. On the plain adventure screen the game has
            // nothing on that key.
            if (input && plain_adventure_focus() && !mouse_buttons_active() &&
                (input->count(df::interface_key::STRING_A114) || input->count(df::interface_key::STRING_A082))) {
                cycle_intent();
                return;
            }
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
        // drawn only once the click is known to go on to the game: a click that is
        // dropped must not show the game's own screen for a moment
        std::optional<MapForClick> map_for_click;
        if (redirect) {
            // A left click in the view does what the intent says (R switches)
            const int click_intent = effective_intent();
            df::unit *adv = World::getAdventurer();
            const bool view_click = enabler->mouse_lbut && !attack.driving && default_adventure_ui() && game && adv &&
                                    world && click_px >= 0 && redirect->state != PointerRedirect::ON_BAR &&
                                    !over_interface(click_px, click_py) && !in_minimap(click_px, click_py) &&
                                    !in_corner(click_px, click_py) &&
                                    !game->main_interface.adventure.aim_projectile.open &&
                                    !game->main_interface.adventure.attack.open && plain_adventure_focus() &&
                                    !game->main_interface.adventure.option_list.open;
            const bool on_tile = redirect->state == PointerRedirect::SHIFTED && redirect->picked.valid();
            const df::coord picked_pos(int16_t(redirect->picked.x), int16_t(redirect->picked.y),
                                       int16_t(redirect->picked.z));
            if (view_click && click_intent != INTENT_WALK) {
                // the click is ours from here on: the game never sees it
                enabler->mouse_lbut = 0;
                enabler->mouse_lbut_down = 0;
                int32_t unit_id = on_tile ? redirect->picked.unit_id : -1;
                df::coord target = on_tile ? picked_pos : adv->pos;
                const bool no_target = click_intent == INTENT_UNNOCK || nock_intent();
                if (combat_intent(click_intent) && !no_target && unit_id < 0) {
                    const int32_t near_id =
                        creature_near_click(click_px, click_py, on_tile ? &picked_pos : nullptr, adv);
                    if (df::unit *near_unit = near_id >= 0 ? df::unit::find(near_id) : nullptr) {
                        unit_id = near_id;
                        target = near_unit->pos;
                        mouse_log.push_back("click: snapped to the creature beside it");
                    }
                }
                if (combat_intent(click_intent) && !no_target && unit_id < 0) {
                    // A creature on your own tile is at the eye: it is not drawn
                    // and cannot be clicked. A click that is on no creature is
                    // for it, if you are in a conflict with it or the click is
                    // on your own tile.
                    const bool own_tile = on_tile && picked_pos == adv->pos;
                    df::unit *beside = nullptr;
                    for (df::unit *u : world->units.active) {
                        if (!u || u == adv || u->pos != adv->pos || !Units::isActive(u) || Units::isDead(u) ||
                            u->id == fpv::mountOf(adv))
                            continue;
                        const bool foe = fpv::conflictWithYou(u, adv) >= 0;
                        if (foe || (own_tile && !beside))
                            beside = u;
                        if (foe)
                            break;
                    }
                    if (beside) {
                        unit_id = beside->id;
                        target = adv->pos;
                    } else if (click_intent != INTENT_THROW || !on_tile) {
                        // Strike, Wrestle or Shoot at a place where no creature is: nothing
                        mouse_log.push_back("click: no creature there");
                        trace.after();
                        return;
                    }
                }
                if (!no_target && !on_tile && unit_id < 0) {
                    trace.after();
                    return;     // the sky
                }
                // (a sheet can be shown at any time)
                if (click_intent != INTENT_INFO && (attack.frames > 0 || !game_takes_input(adv))) {
                    pending_click.on = true;
                    pending_click.intent = click_intent;
                    pending_click.unit_id = unit_id;
                    pending_click.target = target;
                    pending_click.px = click_px;
                    pending_click.py = click_py;
                    pending_click.until_ms = now_ms() + 2500;
                    mouse_log.push_back("click: kept until the game takes input");
                    trace.after();
                    return;
                }
                pending_click.on = false;
                carry_out(click_intent, unit_id, target, click_px, click_py);
                trace.after();
                return;
            }
            // Walk: a creature next to you is stepped past onto its tile, not struck
            if (view_click && on_tile && redirect->picked.unit_id >= 0 &&
                std::abs(picked_pos.x - adv->pos.x) <= 1 && std::abs(picked_pos.y - adv->pos.y) <= 1 &&
                std::abs(picked_pos.z - adv->pos.z) <= 1 && picked_pos != adv->pos) {
                enabler->mouse_lbut = 0;
                enabler->mouse_lbut_down = 0;
                if (game_takes_input(adv) && attack.frames <= 0)
                    step_onto(picked_pos, "In the way");
                trace.after();
                return;
            }
            if (view_click && !on_tile) {
                // the sky: a plain click there would go to whatever the game finds under it
                enabler->mouse_lbut = 0;
                enabler->mouse_lbut_down = 0;
                trace.after();
                return;
            }
            map_for_click.emplace(this, redirect.get());
            if (trace.on)
                trace.note = "redirect " + std::to_string(int(redirect->state)) + " pick " +
                             std::to_string(redirect->picked.x) + "," + std::to_string(redirect->picked.y) + "," +
                             std::to_string(redirect->picked.z) + " keys " +
                             std::to_string(input ? input->size() : 0) + " focus " +
                             (Gui::getCurFocus(true).empty() ? "" : Gui::getCurFocus(true)[0]);
        }
        if (active && showing && input && !attack.driving && !mouse_buttons_active() && default_adventure_ui() &&
            plain_adventure_focus()) {
            // Shift+W and Shift+S climb and descend, c lies down or stands up;
            // what the game has on those keys (wrestle, sneak, companions) is not passed on
            using namespace df::enums::interface_key;
            static const df::interface_key typed[3] = {STRING_A087, STRING_A083, STRING_A099};
            static const df::interface_key does[3] = {A_MOVE_UP, A_MOVE_DOWN, A_STANCE};
            for (int i = 0; i < 3; ++i)
                if (input->count(typed[i])) {
                    std::set<df::interface_key> out{does[i]};
                    INTERPOSE_NEXT(feed)(&out);
                    return;
                }
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
                        if (!step_not_strike(out))
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
                    if (!step_not_strike(out))
                        INTERPOSE_NEXT(feed)(&out);
                    return;
                }
            }
        }
        if (input && !attack.driving && !mouse_buttons_active() && default_adventure_ui() &&
            step_not_strike(*input))
            return;
        INTERPOSE_NEXT(feed)(input);
        trace.after();
    }
};

static_assert(sizeof(dungeon_hook) == sizeof(df::viewscreen_dungeonmodest),
              "a hook struct must not add data members");
IMPLEMENT_VMETHOD_INTERPOSE(dungeon_hook, render);
IMPLEMENT_VMETHOD_INTERPOSE(dungeon_hook, feed);
IMPLEMENT_VMETHOD_INTERPOSE(dungeon_hook, logic);

// Outside DFHack's overlay (its render hook has priority 100, so its widgets
// are drawn after dungeon_hook's capture): what has to be over those widgets,
// the moved caption of a game button and the captions of the mod's travel and
// sleep buttons, is drawn here, after them. No data members (see dungeon_hook).
struct dungeon_top_hook : df::viewscreen_dungeonmodest {
    typedef df::viewscreen_dungeonmodest interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, render, (uint32_t curtick)) {
        INTERPOSE_NEXT(render)(curtick);
        if (!gps)
            return;
        if (caption_pending && gps->dimx == caption_dimx && gps->dimy == caption_dimy)
            for (const GridCell &c : caption_placed)
                put_cell_over(c);
        caption_pending = false;
        if (active && showing)
            paint_widget_hint();
    }
};

static_assert(sizeof(dungeon_top_hook) == sizeof(df::viewscreen_dungeonmodest),
              "a hook struct must not add data members");
IMPLEMENT_VMETHOD_INTERPOSE_PRIO(dungeon_top_hook, render, 200);

// ---------------------------------------------------------------------------
// render thread: animate the camera and draw

SDL_Texture *frame_texture = nullptr;
int tex_w = 0, tex_h = 0;
// With the graphics card drawing the world, the frame only carries what is
// painted over it (what you hold, the marks of a fight) and is see-through
// everywhere else.
bool frame_overlay = false;
// the part of the frame painted over the world this time
struct FxPainted {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    void reset() { x0 = y0 = x1 = y1 = 0; }
    void add(int ax0, int ay0, int ax1, int ay1) {
        if (ax1 <= ax0 || ay1 <= ay0)
            return;
        if (x1 <= x0 || y1 <= y0) {
            x0 = ax0;
            y0 = ay0;
            x1 = ax1;
            y1 = ay1;
        } else {
            x0 = std::min(x0, ax0);
            y0 = std::min(y0, ay0);
            x1 = std::max(x1, ax1);
            y1 = std::max(y1, ay1);
        }
    }
} fx_painted;
std::vector<uint32_t> frame_buffer;
Camera camera;
bool camera_valid = false;
uint32_t camera_snap = 0;
// the walking pace the camera glides at: seconds between the last steps
float glide_pace = 0.25f, glide_tx = -1, glide_ty = -1;
std::chrono::steady_clock::time_point glide_changed;
std::chrono::steady_clock::time_point last_frame_time;

// the sky and the weather as shown (render thread only): they follow the
// game's slowly, so a change of weather comes over a few seconds
fpv::Sky sky;
fpv::Precip precip;
fpv::Weather sky_shown;
bool sky_shown_valid = false;
int sky_shown_pick = -1;
const auto sky_epoch = std::chrono::steady_clock::now();

// Lightning in a storm (render thread only): a flash every 30 to 90 seconds,
// its thunder 2 to 5 seconds later; only seen and heard outdoors.
struct Lightning {
    double next = -1;         // when the next flash comes (seconds since sky_epoch); -1: not counting
    double flash_at = -1e9;   // when the last one came
    double thunder_at = -1;   // when its thunder is heard; -1: none due
    std::mt19937 rng{std::random_device{}()};
    double between(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); }
} lightning;
float flash_now = 0;   // how bright the lightning is in this frame, 0..1

// A storm as the game reports it: rain under nimbus clouds (plain rain
// brings nimbus to 0.6 in read_weather, a storm's own clouds 0.8 or more).
bool is_storm(const fpv::Weather &w) {
    return w.rain > 0.3f && w.cloud[fpv::CLOUD_NIMBUS] >= 0.75f;
}

// how bright the flash is now, 0..1: a strike, a flicker, then the main flash fading
float lightning_step(const fpv::Weather &w, bool outside, double secs) {
    Lightning &L = lightning;
    if (!is_storm(w) || !outside) {
        L.next = -1;
        L.thunder_at = -1;
        return 0;
    }
    if (L.next < 0)
        L.next = secs + L.between(30, 90);
    if (secs >= L.next) {
        L.flash_at = secs;
        L.thunder_at = secs + L.between(2, 5);
        L.next = secs + L.between(30, 90);
    }
    if (L.thunder_at >= 0 && secs >= L.thunder_at) {
        L.thunder_at = -1;
        const auto m = get_media();
        if (m && init && !init->media.flag.is_set(df::init_media_flags::SOUND_OFF)) {
            // the game's master and adventure ambience volumes, 0..255 each
            const float vol = std::clamp(init->media.volume_master / 255.0f, 0.0f, 1.0f) *
                              std::clamp(init->media.volume_ambience_adv / 255.0f, 0.0f, 1.0f);
            fpv::play_sound(sound_api, m->dir + "/thunder.wav", vol);
        }
    }
    const double t = secs - L.flash_at;
    const auto pulse = [t](double start, double len, float peak) {
        const double u = (t - start) / len;
        return u < 0 || u > 1 ? 0.0f : peak * float((1 - u) * (1 - u));
    };
    return std::max({pulse(0.0, 0.12, 0.8f), pulse(0.2, 0.08, 0.5f), pulse(0.34, 0.45, 1.0f)});
}

void follow_weather(const fpv::Weather &want, int pick, float dt) {
    if (!sky_shown_valid || pick != sky_shown_pick) {
        sky_shown = want;
        sky_shown_valid = true;
        sky_shown_pick = pick;
        return;
    }
    fpv::Weather &w = sky_shown;
    const float k = 1 - std::exp(-dt * 0.5f);
    const auto go = [k](float &v, float to) { v += (to - v) * k; };
    for (int i = 0; i < fpv::CLOUD_COUNT; ++i)
        go(w.cloud[i], want.cloud[i]);
    go(w.fog, want.fog);
    go(w.rain, want.rain);
    go(w.snow, want.snow);
    go(w.evil, want.evil);
    if (want.rain_color)
        w.rain_color = want.rain_color;
    if (want.evil_color)
        w.evil_color = want.evil_color;
    go(w.wind_x, want.wind_x);
    go(w.wind_y, want.wind_y);
    const float wl = std::sqrt(w.wind_x * w.wind_x + w.wind_y * w.wind_y);
    if (wl > 1e-3f) {
        w.wind_x /= wl;
        w.wind_y /= wl;
    }
    // the clock goes round: the short way, and a jump (a night's sleep) at once
    float dh = std::remainder(want.hour - w.hour, 24.0f);
    if (std::abs(dh) > 1.5f)
        w.hour = want.hour;
    else
        w.hour = std::fmod(w.hour + dh * (1 - std::exp(-dt * 3.0f)) + 24.0f, 24.0f);
    float dm = std::remainder(want.moon - w.moon, 1.0f);
    if (std::abs(dm) > 0.1f)
        w.moon = want.moon;
    else
        w.moon = std::fmod(w.moon + dm * (1 - std::exp(-dt * 3.0f)) + 1.0f, 1.0f);
    w.day = want.day;
    w.rise_west = want.rise_west;
    w.sun_north = want.sun_north;
}

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
    if (gpu)
        gpu->dropTextures();
    if (mini_texture && sdl_destroy_texture)
        sdl_destroy_texture(mini_texture);
    mini_texture = nullptr;
    mini_tw = mini_th = 0;
    mini_drawn_n = 0;
    if (ascii_texture && sdl_destroy_texture)
        sdl_destroy_texture(ascii_texture);
    ascii_texture = nullptr;
    ascii_tex_side = 0;
    if (foe_texture)
        sdl_destroy_texture(foe_texture);
    foe_texture = nullptr;
    if (arrow_texture)
        sdl_destroy_texture(arrow_texture);
    arrow_texture = nullptr;
    destroy_ui_textures();
    destroy_inv_textures();
    destroy_jr_textures();
    destroy_radial_textures();
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

// the minimap's arrow when media has no minimap_arrow.png (same as tools/make_media.py):
// white with a dark edge, both half see-through, pointing up
const fpv::Image &builtin_minimap_arrow() {
    static fpv::Image img;
    if (img.ok())
        return img;
    constexpr int S = 36;
    img.w = img.h = S;
    img.px.assign(size_t(S) * S, 0);
    const auto on = [](int x, int y) {
        if (x < 0 || y < 0 || x >= S || y >= S)
            return false;
        const float px = (x + 0.5f - S / 2) / (S / 2), fwd = -(y + 0.5f - S / 2) / (S / 2), lat = std::abs(px);
        return (fwd >= -0.05f && fwd <= 0.85f && lat <= (0.85f - fwd) * 0.95f) ||
               (fwd >= -0.8f && fwd < 0 && lat <= 0.22f);
    };
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            if (on(x, y)) {
                img.px[size_t(y) * S + x] = 0x80FFFFFFu;
                continue;
            }
            bool edge = false;
            for (int dy = -2; dy <= 2 && !edge; ++dy)
                for (int dx = -2; dx <= 2 && !edge; ++dx)
                    edge = on(x + dx, y + dy);
            if (edge)
                img.px[size_t(y) * S + x] = 0x80000000u;
        }
    return img;
}

// An arrow in the tile next to the adventurer that a step forward goes to:
// media's minimap_arrow.png (its top is forward), turned to the heading and
// drawn with the picture's own transparency. The minimap shows n x n tiles;
// (si, sj) is the adventurer's tile.
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

    // the turned picture is made once for a heading, a size and a picture
    static std::shared_ptr<const fpv::Media> made_media;
    static int made_h = -1, made_side = 0;
    static bool made = false;
    const std::shared_ptr<const fpv::Media> media = get_media();
    const int side = std::max(4, int(size * 1.42f) + 2);   // room for the picture turned by 45 degrees
    if (!arrow_texture || side != made_side) {
        if (arrow_texture)
            sdl_destroy_texture(arrow_texture);
        arrow_texture = sdl_create_texture(sdl, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, side, side);
        if (!arrow_texture)
            return;
        sdl_set_texture_blend_mode(arrow_texture, SDL_BLENDMODE_BLEND);
        made_side = side;
        made = false;
    }
    if (!made || h != made_h || media != made_media) {
        const fpv::Image &pic = media && media->minimap_arrow.ok() ? media->minimap_arrow : builtin_minimap_arrow();
        const float ang = h * 0.78539816f, fx = std::sin(ang), fy = -std::cos(ang);
        const auto texel = [&](int ix, int iy) {
            return ix < 0 || iy < 0 || ix >= pic.w || iy >= pic.h ? 0u : pic.px[size_t(iy) * pic.w + ix];
        };
        std::vector<uint32_t> px(size_t(side) * side, 0);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const float rx = x + 0.5f - side / 2.0f, ry = y + 0.5f - side / 2.0f;
                const float fwd = rx * fx + ry * fy, lat = rx * -fy + ry * fx;
                const float u = (lat / size + 0.5f) * pic.w - 0.5f, v = (0.5f - fwd / size) * pic.h - 0.5f;
                const int ix = int(std::floor(u)), iy = int(std::floor(v));
                const float ax = u - ix, ay = v - iy;
                float sr = 0, sg = 0, sb = 0, sa = 0;
                for (int k = 0; k < 4; ++k) {
                    const uint32_t c = texel(ix + (k & 1), iy + (k >> 1));
                    const float w = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay) * fpv::px_a(c) / 255.0f;
                    sr += fpv::px_r(c) * w;
                    sg += fpv::px_g(c) * w;
                    sb += fpv::px_b(c) * w;
                    sa += w;
                }
                if (sa > 0.004f)
                    px[size_t(y) * side + x] = uint32_t(sr / sa) | uint32_t(sg / sa) << 8 | uint32_t(sb / sa) << 16 |
                                               uint32_t(std::min(255.0f, sa * 255.0f)) << 24;
            }
        sdl_update_texture(arrow_texture, nullptr, px.data(), side * 4);
        made = true;
        made_h = h;
        made_media = media;
    }
    const SDL_Rect at = {int(cx - side / 2.0f), int(cy - side / 2.0f), side, side};
    sdl_render_copy(sdl, arrow_texture, nullptr, &at);
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
// render thread: a viewport of our own holding only the minimap's square (see
// MiniShot), laid out like the game's, which the game's renderer draws from
alignas(df::graphic_viewportst) unsigned char mini_vp_mem[sizeof(df::graphic_viewportst)];
std::vector<std::vector<uint8_t>> mini_vp_slots, mini_vp_old;
uint64_t mini_vp_hash = 0;
int mini_vp_n = 0;

df::graphic_viewportst *minimap_viewport() {
    if (!gps || !gps->main_viewport)
        return nullptr;
    auto *vp = reinterpret_cast<df::graphic_viewportst *>(mini_vp_mem);
    {
        std::lock_guard<std::mutex> lock(mini_shot_mutex);
        if (mini_shot.n <= 0)
            return nullptr;
        if (mini_shot.hash != mini_vp_hash || mini_shot.n != mini_vp_n || mini_vp_slots.empty()) {
            mini_vp_slots = mini_shot.slots;
            mini_vp_hash = mini_shot.hash;
            mini_vp_n = mini_shot.n;
        }
    }
    const int n = mini_vp_n;
    // the game's own settings (flag), then the size and the arrays made ours
    std::memcpy(mini_vp_mem, static_cast<const void *>(gps->main_viewport), sizeof(mini_vp_mem));
    vp->dim_x = vp->dim_y = n;
    vp->clipx[0] = vp->clipy[0] = 0;
    vp->clipx[1] = vp->clipy[1] = n - 1;
    vp->screen_x = vp->screen_y = n - 1;
    size_t k = 0;
    viewport_slots(vp, [&](void **slot, size_t size) {
        if (k >= mini_vp_slots.size())
            mini_vp_slots.emplace_back();
        std::vector<uint8_t> &a = mini_vp_slots[k++];
        if (a.size() != size_t(n) * n * size)
            a.assign(size_t(n) * n * size, 0);
        *slot = a.data();
    });
    k = 0;
    viewport_old_slots(vp, [&](void **slot, size_t size) {
        if (k >= mini_vp_old.size())
            mini_vp_old.emplace_back();
        // empty every time: nothing may count as already drawn
        std::vector<uint8_t> &a = mini_vp_old[k++];
        a.assign(size_t(n) * n * size, 0);
        *slot = a.data();
    });
    return vp;
}

void draw_minimap(df::renderer_2d_base *r, SDL_Renderer *sdl) {
    SDL_Rect dst;
    if (!minimap_rect(dst))
        return;
    if (settings.minimap_ascii) {
        draw_minimap_ascii(sdl, dst);
        return;
    }
    if (mini_n.load() <= 0)
        return;
    df::graphic_viewportst *vp = minimap_viewport();
    if (!vp)
        return;
    const int n = mini_vp_n;
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
    const uint64_t hash = mini_vp_hash;
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

// the row of foes under the minimap: a framed picture each in the colour of
// its conflict, and an arrow at an end where there are more
void draw_foes(SDL_Renderer *sdl) {
    constexpr int SLOTS = 24;
    FoeStrip s;
    if (!foe_strip(s))
        return;
    static std::vector<uint32_t> px;
    uint32_t colors[SLOTS];
    int shown = 0;
    {
        std::lock_guard<std::mutex> lock(foe_mutex);
        shown = std::min({s.shown, SLOTS, int(foes.size()) - s.first});
        if (shown <= 0)
            return;
        px.resize(size_t(32) * shown * 32);
        for (int i = 0; i < shown; ++i) {
            const Foe &f = foes[s.first + i];
            colors[i] = f.color;
            for (int y = 0; y < 32; ++y)
                std::memcpy(&px[(size_t(y) * shown + i) * 32], &f.px[y * 32], 32 * sizeof(uint32_t));
        }
    }
    if (!foe_texture) {
        foe_texture = sdl_create_texture(sdl, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 32 * SLOTS, 32);
        if (!foe_texture)
            return;
        sdl_set_texture_blend_mode(foe_texture, SDL_BLENDMODE_BLEND);
        sdl_set_texture_scale_mode(foe_texture, SDL_ScaleModeLinear);
    }
    const SDL_Rect part = {0, 0, 32 * shown, 32};
    sdl_update_texture(foe_texture, &part, px.data(), 32 * shown * 4);

    Uint8 cr = 0, cg = 0, cb = 0, ca = 255;
    sdl_get_render_draw_color(sdl, &cr, &cg, &cb, &ca);
    SDL_BlendMode blend_was = SDL_BLENDMODE_NONE;
    sdl_get_render_draw_blend_mode(sdl, &blend_was);
    sdl_set_render_draw_blend_mode(sdl, SDL_BLENDMODE_BLEND);
    const auto box = [&](const SDL_Rect &r, int red, int green, int blue) {
        sdl_set_render_draw_color(sdl, 0, 0, 0, 170);
        sdl_render_fill_rect(sdl, &r);
        sdl_set_render_draw_color(sdl, Uint8(red), Uint8(green), Uint8(blue), 255);
        const SDL_Rect edge = {r.x - 1, r.y - 1, r.w + 2, r.h + 2};
        sdl_render_draw_rect(sdl, &edge);
    };
    for (int i = 0; i < shown; ++i) {
        const SDL_Rect d = s.slot(i);
        box(d, fpv::px_r(colors[i]), fpv::px_g(colors[i]), fpv::px_b(colors[i]));
        const SDL_Rect src = {i * 32, 0, 32, 32};
        sdl_render_copy(sdl, foe_texture, &src, &d);
    }
    // an arrowhead in a small frame, in the colour of the game's panels
    const auto arrow = [&](const SDL_Rect &r, bool to_left) {
        box(r, 255, 170, 30);
        const int pad = std::max(2, r.w / 5), span = std::max(1, r.w - 2 * pad);
        const int reach = std::max(2, r.h / 2 - pad - 1);
        for (int i = 0; i < span; ++i) {
            // the point is at the outer end
            const int half = std::max(0, (to_left ? i : span - 1 - i) * reach / std::max(1, span - 1));
            const SDL_Rect line = {r.x + pad + i, r.y + r.h / 2 - half, 1, 2 * half + 1};
            sdl_render_fill_rect(sdl, &line);
        }
    };
    if (s.more_left)
        arrow(s.left, true);
    if (s.more_right)
        arrow(s.right, false);
    sdl_set_render_draw_color(sdl, cr, cg, cb, ca);
    sdl_set_render_draw_blend_mode(sdl, blend_was);
}

float look_yaw = 0, look_pitch = 0;   // radians; fraction of the view height
// testing aids: hold the view at a tilt / a heading given in degrees (1000 = off)
std::atomic<float> debug_tilt{1000}, debug_yaw{1000};
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

// What you ride: the top quarter of its picture, much enlarged, along the
// bottom of the view, as its back and head seen from the saddle. Drawn before
// what you hold and the marks of a fight, which go over it.
void draw_mount(uint32_t *buf, int W, int H, const fpv::Sprite &sp) {
    if (sp.empty() || W <= 0 || H <= 0)
        return;
    const int cols = sp.x1 - sp.x0;
    const int rows = std::max(1, (sp.y1 - sp.y0 + 3) / 4);
    // pixels of the screen to one of the picture: as tall as a fifth of the
    // view, no wider than most of it
    const float k = std::min(H * 0.22f / rows, W * 0.8f / cols);
    const int w = int(cols * k), h = int(rows * k);
    const int x0 = (W - w) / 2, y0 = H - h;
    fx_painted.add(std::max(0, x0), std::max(0, y0), std::min(W, x0 + w), H);
    for (int x = std::max(0, x0); x < std::min(W, x0 + w); ++x) {
        const int tx = sp.x0 + std::min(cols - 1, int((x - x0) / k));
        uint32_t *col = buf + size_t(x) * H;
        for (int y = std::max(0, y0); y < H; ++y) {
            const int ty = sp.y0 + std::min(rows - 1, int((y - y0) / k));
            const uint32_t c = sp.px[size_t(ty) * sp.w + tx];
            if (fpv::px_a(c) >= 128)
                col[y] = c | 0xFF000000u;
        }
    }
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
        fx_painted.add(x0, y0, x0 + size, H);
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

// mix a colour into a pixel of the frame (0xAABBGGRR). The pixel may be
// see-through itself (the frame lying over a world drawn by the graphics card).
inline void blend(uint32_t &dst, int r, int g, int b, float a) {
    if (a <= 0)
        return;
    a = std::min(a, 1.0f);
    const float da = fpv::px_a(dst) / 255.0f;
    const float oa = a + da * (1 - a);
    const float ka = a / oa, kd = da * (1 - a) / oa;
    const int dr = fpv::px_r(dst), dg = fpv::px_g(dst), db = fpv::px_b(dst);
    dst = (fpv::rgb(int(r * ka + dr * kd), int(g * ka + dg * kd), int(b * ka + db * kd)) & 0x00FFFFFFu) |
          (uint32_t(oa * 255.0f + 0.5f) << 24);
}

// the attack crescents and the arrows of strikes at you, into the frame
// (column by column); (cx, cy) is where the pointer is
void draw_combat_fx(uint32_t *buf, int W, int H, const fpv::Camera &cam, int cx, int cy, int cell_w, int cell_h) {
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
    const fpv::View3D view = fpv::View3D::of(cam, W, H);
    const float S = view.S;
    const float dirx = std::sin(cam.ang), diry = -std::cos(cam.ang), rgtx = std::cos(cam.ang), rgty = std::sin(cam.ang);
    // on the screen as either renderer draws it
    const auto project = [&](float x, float y, float h, float &sx, float &sy, float &d) {
        return view.project(x, y, h, sx, sy, d) && d >= 0.2f;
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
        fx_painted.add(x0, y0, x1 + 1, y1 + 1);
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

    // an arrow from the pointer towards whoever struck at you: a picture from
    // the media folder (one for a wound, one for a miss) whose top points at
    // the attacker, or the built-in shape where there is none
    const std::shared_ptr<const fpv::Media> media = arrows.empty() ? nullptr : get_media();
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
        // a thin shaft that widens towards a barbed head: a V with its back cut out
        const float r0 = H * 0.035f, r1 = H * 0.13f, head = H * 0.05f, half = H * 0.005f, wide = H * 0.026f;
        const float neck = r1 - head * 0.5f;   // where the shaft meets the notch in the head's back
        const float alpha = t < 0.45f ? 1.0f : std::max(0.0f, 1 - (t - 0.45f) / 0.55f);
        const fpv::Image *pic = media ? (a.red ? &media->struck_hit : &media->struck_miss) : nullptr;
        if (pic && pic->ok()) {
            // as long as the built-in arrow, as wide as its shape asks; smoothed between its pixels
            const float long_px = r1 - r0, half_w = long_px * pic->w / pic->h / 2;
            const float reach = std::sqrt(r1 * r1 + half_w * half_w) + 2;
            const int x0 = std::max(0, int(cx - reach)), x1 = std::min(W - 1, int(cx + reach));
            const int y0 = std::max(0, int(cy - reach)), y1 = std::min(H - 1, int(cy + reach));
            fx_painted.add(x0, y0, x1 + 1, y1 + 1);
            const auto texel = [&](int ix, int iy) {
                return ix < 0 || iy < 0 || ix >= pic->w || iy >= pic->h ? 0u : pic->px[size_t(iy) * pic->w + ix];
            };
            for (int x = x0; x <= x1; ++x) {
                uint32_t *col = buf + size_t(x) * H;
                for (int y = y0; y <= y1; ++y) {
                    const float px = x + 0.5f - cx, py = y + 0.5f - cy;
                    const float u = px * vx + py * vy, w = px * nx + py * ny;
                    if (u < r0 - 1 || u > r1 + 1 || std::abs(w) > half_w + 1)
                        continue;
                    const float fx = (w + half_w) / (2 * half_w) * pic->w - 0.5f;
                    const float fy = (r1 - u) / long_px * pic->h - 0.5f;
                    const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
                    const float ax = fx - ix, ay = fy - iy;
                    float sr = 0, sg = 0, sb = 0, sa = 0;
                    for (int k = 0; k < 4; ++k) {
                        const uint32_t c = texel(ix + (k & 1), iy + (k >> 1));
                        const float wgt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay) * fpv::px_a(c) / 255.0f;
                        sr += fpv::px_r(c) * wgt;
                        sg += fpv::px_g(c) * wgt;
                        sb += fpv::px_b(c) * wgt;
                        sa += wgt;
                    }
                    if (sa > 0.004f)
                        blend(col[y], int(sr / sa), int(sg / sa), int(sb / sa), sa * alpha);
                }
            }
            continue;
        }
        // a miss in the green of the word "MISS!"
        const int cr = a.red ? 235 : 70, cg = a.red ? 30 : 230, cb = a.red ? 30 : 90;
        const float reach = r1 + wide + 3;
        const int x0 = std::max(0, int(cx - reach)), x1 = std::min(W - 1, int(cx + reach));
        const int y0 = std::max(0, int(cy - reach)), y1 = std::min(H - 1, int(cy + reach));
        fx_painted.add(x0, y0, x1 + 1, y1 + 1);
        const auto inside = [&](float u, float w, float grow) {
            const float aw = std::abs(w);
            if (u >= r0 - grow && u <= neck && aw <= half * (0.3f + 0.7f * (u - r0) / (neck - r0)) + grow)
                return true;
            return u <= r1 + grow && aw <= wide * (r1 - u) / head + grow &&
                   u >= r1 - head + (neck - (r1 - head)) * (1 - aw / wide) - grow;
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

    // the words, rising and fading (yours sink from under the pointer instead),
    // with a dark edge so they read on anything;
    // the font half as big again as the interface's
    cell_w = cell_w * 3 / 2;
    cell_h = cell_h * 3 / 2;
    if (ui_font_ready && cell_w > 0 && cell_h > 0)
        for (const FxText &tx : texts) {
            if (now < tx.start)
                continue;
            const float t = float((now - tx.start) / TEXT_TIME);
            float sx, sy, d;
            // pushed a line further by each later word over the same creature
            float push = 0;
            for (const FxText &o : texts)
                if (o.unit_id == tx.unit_id && o.start > tx.start && o.start <= now)
                    push += float(std::min(1.0, (now - o.start) / TEXT_PUSH));
            const float drift = t * H * 0.06f + push * (cell_h + 2);
            if (tx.you) {
                sx = float(cx);
                sy = cy + H * 0.03f + cell_h + drift;
            } else if (!project(tx.x, tx.y, tx.h, sx, sy, d)) {
                continue;
            } else {
                sy -= drift;
            }
            const float alpha = t < 0.6f ? 1.0f : std::max(0.0f, 1 - (t - 0.6f) / 0.4f);
            const int x0 = int(sx) - int(tx.text.size()) * cell_w / 2, y0 = int(sy) - cell_h;
            fx_painted.add(x0 - 1, y0 - 1, x0 + int(tx.text.size()) * cell_w + 1, y0 + cell_h + 1);
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
        frame_overlay = false;
    }

    // glide towards where the adventurer is and where they look
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_frame_time).count();
    last_frame_time = now;
    probe_frame.add(uint32_t(std::min(dt, 10.0f) * 1e6f));
    if (dt > 0.035f)
        hitch("render gap " + std::to_string(int(dt * 1000)) + " ms");
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
        // The camera keeps pace with the steps: it takes as long to cross a
        // tile as the last steps took, so one step runs into the next. (At a
        // fixed high speed it dashed across each tile and then stood until
        // the next step, which looks like stuttering.) Behind by more than a
        // tile, it catches up.
        if (t.x != glide_tx || t.y != glide_ty) {
            const float since = std::chrono::duration<float>(now - glide_changed).count();
            if (since < 0.7f)
                glide_pace = std::clamp(0.5f * glide_pace + 0.5f * since, 0.08f, 0.6f);
            glide_changed = now;
            glide_tx = t.x;
            glide_ty = t.y;
        }
        const float dx = t.x - camera.x, dy = t.y - camera.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float step = std::max(1.0f, dist * dist) / glide_pace * dt;
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
    shift_held = window && (mods & KMOD_SHIFT) != 0;
    {
        const Uint8 *keys = sdl_get_keyboard_state(nullptr);
        space_down = window && keys && keys[SDL_SCANCODE_SPACE];
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
    // Looking up and down: the renderer on the graphics card tilts the camera
    // for real, up to nearly straight up or down; the software one slides the
    // picture up or down.
    const bool want_gpu = settings.hardware_renderer && gpu_api.ok() && !gpu_refused;
    // a click on a foe under the minimap: turn to it and look at its middle
    if (const int want = foe_aim.exchange(-1); want >= 0) {
        float tx = 0, ty = 0, th = 0;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(foe_mutex);
            for (const Foe &f : foes)
                if (f.unit_id == want) {
                    tx = f.x;
                    ty = f.y;
                    th = f.h;
                    found = true;
                }
        }
        const float dx = tx - camera.x, dy = ty - camera.y, dist = std::sqrt(dx * dx + dy * dy);
        if (found && dist > 0.05f) {
            float ang = std::atan2(dx, -dy);
            if (ang < 0)
                ang += 6.2831853f;
            look_heading = int(std::floor(ang / 0.78539816f + 0.5f)) & 7;
            heading = look_heading;
            if (settings.mouse_look) {
                look_yaw = ang;
                look_valid = true;
                camera.ang = ang;
            }
            // as the two renderers look up and down (below)
            const float up = th - camera.eye;
            look_pitch = std::clamp(want_gpu ? std::atan2(up, dist) / 1.55f
                                             : up * fpv::View3D::of(camera, W, H).S / (dist * H),
                                    -1.0f, 1.0f);
        }
    }
    if (debug_tilt.load() < 999)
        look_pitch = std::clamp(debug_tilt.load() / 90.0f, -1.0f, 1.0f);
    if (debug_yaw.load() < 999)
        camera.ang = debug_yaw.load() * 0.0174532925f;
    if (want_gpu) {
        camera.tilt = look_pitch * 1.55f;
        camera.pitch = 0;
    } else {
        camera.tilt = 0;
        camera.pitch = look_pitch * H;
    }
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
    {
        follow_weather(t.weather, t.weather_pick, dt);
        const double secs = std::chrono::duration<double>(now - sky_epoch).count();
        // the waves on water step through their pictures ten times a second
        opt.wave_frame = int(std::fmod(secs * 10.0, double(fpv::WAVE_FRAMES)));
        const float P = std::tan(settings.fov.load() * 3.14159265f / 360.0f);
        sky.prepare(sky_shown, get_media(), secs, 2 * P / W);
        flash_now = t.underground ? 0.0f : lightning_step(sky_shown, t.outside, secs);
        if (sky.ready()) {
            opt.sky = &sky;
            opt.precip = &precip;
            opt.weather = &sky_shown;
            if (!t.underground) {
                // The weather only hazes the view; what is drawn stays the same.
                opt.fog = sky.haze();
                opt.weather_haze = sky.weatherHaze();
                opt.weather_reach = sky.sightLimit();
            }
        }
        // Beyond the map the land goes on to the horizon, and the haze is the
        // horizon's: what is near is barely hazed. Not underground.
        if (settings.far_land && !t.underground) {
            opt.far_land = true;
            opt.fog_distance = settings.horizon;
            opt.fog_distance = std::max(opt.fog_distance, opt.view_distance);
        }
        fpv::Weather falling = sky_shown;
        if (t.underground)
            falling.rain = falling.snow = 0;
        precip.step(dt, camera.x, camera.y, camera.eye, falling);
    }

    const auto t0 = std::chrono::steady_clock::now();
    bool gpu_frame = false;
    {
        const ProbeTimer wait;
        std::lock_guard<std::mutex> lock(scene.mutex);
        probe_scene_wait.add(wait.us());
        if (want_gpu) {
            if (!gpu)
                gpu = std::make_unique<fpv::GpuRenderer>(gpu_api);
            gpu_frame = gpu->build(sdl, scene, camera, opt, out_w, out_h);
        }
        if (gpu_frame)
            stat_gpu_tris = uint32_t(gpu->triangles());
        else
            renderer->render(scene, camera, opt, W, H, frame_buffer.data());
        drew_3d = gpu_frame;
    }
    // what the last frame painted over the world: x0, y0, x1, y1
    static int painted_before[4] = {0, 0, 0, 0};
    bool send_all = false;
    if (gpu_frame != frame_overlay) {
        frame_overlay = gpu_frame;
        sdl_set_texture_blend_mode(frame_texture, gpu_frame ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
        if (gpu_frame)
            std::fill(frame_buffer.begin(), frame_buffer.end(), 0u);
        painted_before[0] = painted_before[1] = painted_before[2] = painted_before[3] = 0;
        send_all = true;
    }
    if (gpu_frame)
        for (int x = painted_before[0]; x < painted_before[2]; ++x)
            std::fill(frame_buffer.begin() + size_t(x) * H + painted_before[1],
                      frame_buffer.begin() + size_t(x) * H + painted_before[3], 0u);
    fx_painted.reset();
    if (!gpu_frame && !precip.empty()) {
        // (the graphics card renderer has drawn them already)
        if (const auto m = get_media()) {
            int box[4] = {0, 0, 0, 0};
            {
                std::lock_guard<std::mutex> lock(scene.mutex);
                precip.draw(frame_buffer.data(), W, H, fpv::View3D::of(camera, W, H), scene, *m, sky_shown, box);
            }
            if (box[2] > box[0] && box[3] > box[1])
                fx_painted.add(box[0], box[1], box[2], box[3]);
        }
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
        std::shared_ptr<const fpv::Sprite> mount;
        {
            std::lock_guard<std::mutex> lock(scene.mutex);
            mount = scene.mount;
        }
        if (mount)
            draw_mount(frame_buffer.data(), W, H, *mount);
        draw_combat_fx(frame_buffer.data(), W, H, camera, fx, fy, cell_w, cell_h);
        draw_held(frame_buffer.data(), W, H, fx, fy);
    }
    const auto t1 = std::chrono::steady_clock::now();
    if (!gpu_frame) {
        renderer->swapRedBlue(frame_buffer.data(), size_t(W) * H);   // our pixels are ABGR
        sdl_update_texture(frame_texture, nullptr, frame_buffer.data(), H * 4);
    } else {
        // only what was painted now, and what was painted before and is gone
        int now_r[4] = {std::clamp(fx_painted.x0, 0, W), std::clamp(fx_painted.y0, 0, H),
                        std::clamp(fx_painted.x1, 0, W), std::clamp(fx_painted.y1, 0, H)};
        if (now_r[2] <= now_r[0] || now_r[3] <= now_r[1])
            now_r[0] = now_r[1] = now_r[2] = now_r[3] = 0;
        for (int x = now_r[0]; x < now_r[2]; ++x) {
            uint32_t *col = frame_buffer.data() + size_t(x) * H;
            for (int y = now_r[1]; y < now_r[3]; ++y) {
                const uint32_t c = col[y];
                col[y] = (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
            }
        }
        int u[4] = {now_r[0], now_r[1], now_r[2], now_r[3]};
        if (painted_before[2] > painted_before[0]) {
            if (u[2] <= u[0]) {
                std::copy(painted_before, painted_before + 4, u);
            } else {
                u[0] = std::min(u[0], painted_before[0]);
                u[1] = std::min(u[1], painted_before[1]);
                u[2] = std::max(u[2], painted_before[2]);
                u[3] = std::max(u[3], painted_before[3]);
            }
        }
        if (send_all) {
            u[0] = u[1] = 0;
            u[2] = W;
            u[3] = H;
        }
        if (u[2] > u[0] && u[3] > u[1]) {
            // the texture is the picture on its side: a column of the frame is a row of it
            const SDL_Rect part = {u[1], u[0], u[3] - u[1], u[2] - u[0]};
            sdl_update_texture(frame_texture, &part, frame_buffer.data() + size_t(u[0]) * H + u[1], H * 4);
        }
        std::copy(now_r, now_r + 4, painted_before);
        if (!gpu->draw(sdl))
            gpu_refused = true;
    }

    // Turn the sideways texture back upright: flip it, then rotate a quarter
    // turn around its top-left corner, which ends up at the top right of the
    // window.
    const SDL_Rect dst = {out_w, 0, out_h, out_w};
    const SDL_Point pivot = {0, 0};
    sdl_render_copy_ex(sdl, frame_texture, nullptr, &dst, 90.0, &pivot, SDL_FLIP_VERTICAL);
    if (flash_now > 0.003f) {
        // lightning: the whole view lit up, under the interface
        Uint8 cr, cg, cb, ca;
        SDL_BlendMode bm;
        sdl_get_render_draw_color(sdl, &cr, &cg, &cb, &ca);
        sdl_get_render_draw_blend_mode(sdl, &bm);
        sdl_set_render_draw_blend_mode(sdl, SDL_BLENDMODE_ADD);
        sdl_set_render_draw_color(sdl, 225, 230, 255, Uint8(std::clamp(int(flash_now * 150), 0, 255)));
        const SDL_Rect all = {0, 0, out_w, out_h};
        sdl_render_fill_rect(sdl, &all);
        sdl_set_render_draw_blend_mode(sdl, bm);
        sdl_set_render_draw_color(sdl, cr, cg, cb, ca);
    }
    draw_minimap(r, sdl);
    draw_foes(sdl);
    sync_ui_textures(sdl);
    if (settings.compass)
        draw_compass(sdl, out_w, camera.ang);
    draw_messages(sdl);
    draw_body(sdl, false);
    draw_menu(sdl);
    draw_shot_buttons(sdl);
    if (look_on || debug_look) {
        // the mark of the intent; the plain crosshair for one without a picture
        const UiTexture *mark = &tex_aim[std::clamp(aim_mark(), 0, fpv::AIM_COUNT - 1)];
        if (!mark->tex)
            mark = &tex_crosshair;
        if (mark->tex)
            mark->draw(sdl, out_w / 2 - mark->w / 2, out_h / 2 - mark->h / 2);
        else
            draw_crosshair(sdl, out_w / 2, out_h / 2, combat_intent(effective_intent()));
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
        if (showing || doll_only) {
            const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (!in_game_tick && now_ms - std::max(last_capture_ms.load(), last_tick_ms.load()) > 700)
                showing = doll_only = false;
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
            // under the interface, like the view: the portrait's cells were emptied
            if (active && doll_only && sdl_renderer)
                draw_body(static_cast<SDL_Renderer *>(sdl_renderer), true);
        }
        INTERPOSE_NEXT(update_all)();
        // the inventory panel in front of the whole interface
        if (active && inv_up && sdl_renderer) {
            draw_inventory(static_cast<SDL_Renderer *>(sdl_renderer));
            draw_leaf_tabs(static_cast<SDL_Renderer *>(sdl_renderer), inv_place_x, inv_place_y, PT_INVENTORY);
        }
        if (active && jr_up && sdl_renderer)
            draw_journal(static_cast<SDL_Renderer *>(sdl_renderer));
        // the hint over a hurt body part (the doll, the aiming menu), over everything but the ring
        if (active && sdl_renderer && (showing || doll_only) && !look_on && !radial_open && !radial_config_open)
            draw_hover_tips(static_cast<SDL_Renderer *>(sdl_renderer));
        if (active && sdl_renderer)
            draw_radial(static_cast<SDL_Renderer *>(sdl_renderer));
    }
};

static_assert(sizeof(renderer_hook) == sizeof(df::renderer_2d_base),
              "a hook struct must not add data members");
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
        !INTERPOSE_HOOK(dungeon_hook, logic).apply() || !INTERPOSE_HOOK(renderer_hook, update_all).apply() ||
        !INTERPOSE_HOOK(dungeon_top_hook, render).apply()) {
        INTERPOSE_HOOK(dungeon_hook, render).remove();
        INTERPOSE_HOOK(dungeon_hook, feed).remove();
        INTERPOSE_HOOK(dungeon_hook, logic).remove();
        INTERPOSE_HOOK(renderer_hook, update_all).remove();
        INTERPOSE_HOOK(dungeon_top_hook, render).remove();
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
    INTERPOSE_HOOK(dungeon_top_hook, render).remove();
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
            key == "minimap_ascii" || key == "wasd" || key == "mouse_look" || key == "combat_menus" || key == "inventory_panel" || key == "journal_panel" || key == "radial_menu" ||
            key == "game_tips" || key == "body_doll" || key == "status_bars" || key == "hardware_renderer" || key == "any_conflict_discs" ||
            key == "compass_markers" || key == "cursor_hint" || key == "enemy_list" || key == "auto_attack" ||
            key == "far_land") {
            if (!parse_bool(value, b))
                return CR_WRONG_USAGE;
            if (key == "relative_controls") settings.relative_controls = b;
            else if (key == "smooth") settings.smooth = b;
            else if (key == "minimap") settings.minimap = b;
            else if (key == "minimap_ascii") settings.minimap_ascii = b;
            else if (key == "enemy_list") settings.enemy_list = b;
            else if (key == "auto_attack") settings.auto_attack = b;
            else if (key == "wasd") settings.wasd = b;
            else if (key == "mouse_look") settings.mouse_look = b;
            else if (key == "combat_menus") settings.combat_menus = b;
            else if (key == "inventory_panel") settings.inventory_panel = b;
            else if (key == "journal_panel") settings.journal_panel = b;
            else if (key == "radial_menu") settings.radial_menu = b;
            else if (key == "game_tips") settings.game_tips = b;
            else if (key == "body_doll") settings.body_doll = b;
            else if (key == "status_bars") settings.status_bars = b;
            else if (key == "any_conflict_discs") settings.any_conflict_discs = b;
            else if (key == "hardware_renderer") settings.hardware_renderer = b;
            else if (key == "compass_markers") settings.compass_markers = b;
            else if (key == "cursor_hint") settings.cursor_hint = b;
            else if (key == "far_land") settings.far_land = b;
            else settings.compass = b;
            return CR_OK;
        }
        f = std::stof(value);
    } catch (...) {
        return CR_WRONG_USAGE;
    }
    if (key == "fov") settings.fov = std::clamp(f, 40.0f, 160.0f);
    else if (key == "resolution") settings.resolution = std::clamp(f, 10.0f, 100.0f) / 100.0f;
    else if (key == "view_distance") settings.view_distance = std::clamp(f, 4.0f, 999.0f);
    else if (key == "minimap_tiles") settings.minimap_tiles = std::clamp(int(f), 5, 61);
    else if (key == "realtime_rate") settings.realtime_rate = std::clamp(int(f), 1, 99);
    else if (key == "range") { settings.range = std::clamp(int(f), 1, 4); full_capture = true; }
    else if (key == "max_height") { settings.max_height = std::clamp(int(f), 0, 999); full_capture = true; }
    else if (key == "model_lines") settings.model_lines = std::clamp(f, 0.0f, 4.0f);
    else if (key == "horizon") settings.horizon = std::clamp(f, 500.0f, 60000.0f);
    else if (key == "far_trees") settings.far_trees = std::clamp(f, 0.0f, 1400.0f);
    else if (key == "max_depth") { settings.max_depth = std::clamp(int(f), 0, 999); full_capture = true; }
    else if (key == "debug_weather") debug_weather = std::clamp(int(f), 0, WEATHER_PICKS - 1);
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
        resume_unit = -1;
        resume_hf = -1;
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
        // "intent" shows it, "intent next" switches like R, "intent info|interact|walk|strike|shoot|throw|wrestle|unnock" sets it
        if (parameters.size() == 2) {
            if (parameters[1] == "next") {
                cycle_intent();
            } else {
                for (int i = 0; i < INTENT_COUNT; ++i) {
                    std::string name = INTENT_NAMES[i];
                    name[0] = char(name[0] - 'A' + 'a');
                    if (parameters[1] == name)
                        set_intent(i);
                }
            }
        }
        out.print("Intent: {} (a click now: {})\n", INTENT_NAMES[intent.load()], intent_label());
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
    if ((cmd == "tilt" || cmd == "yaw") && parameters.size() == 2) {
        // testing aid: look up or down / in a direction by so many degrees, as the mouse would
        float v = 1000;
        if (parameters[1] != "off")
            v = float(std::atof(parameters[1].c_str()));
        (cmd == "tilt" ? debug_tilt : debug_yaw) = v;
        if (cmd == "tilt" && v > 999)
            look_pitch = 0;
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
            float fx = 0, fy = 0;
            if (drew_3d && gpu) {
                found = gpu->findCreature(scene, fx, fy);
                x = int(fx * stat_w.load());
                y = int(fy * stat_h.load());
            } else {
                found = renderer && renderer->findCreature(x, y);
            }
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
    if (cmd == "hitches") {
        // testing aid: "hitches on|off" starts or stops the log, "hitches" shows and empties it
        if (parameters.size() == 2)
            hitch_on = parameters[1] == "on";
        std::lock_guard<std::mutex> lock(hitch_mutex);
        for (const std::string &line : hitch_log)
            out.print("{}\n", line);
        hitch_log.clear();
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
    if (gpu_refused)
        out.print("  graphics card renderer: not available (the game's renderer would not draw its triangles)\n");
    else if (!gpu_api.ok())
        out.print("  graphics card renderer: not available (this SDL has no SDL_RenderGeometry)\n");
    else if (drew_3d && gpu)
        out.print("  graphics card renderer: on, {} triangles, texture {}x{} {}% full ({})\n", stat_gpu_tris.load(),
                  gpu->atlasSize(), gpu->atlasSize(), gpu->atlasPercent(), gpu->timings());
    else
        out.print("  graphics card renderer: off\n");
    {
        fpv::Weather w;
        int pick = 0;
        {
            std::lock_guard<std::mutex> lock(target_mutex);
            w = target.weather;
            pick = target.weather_pick;
        }
        out.print("  weather: {}; {:.1f} h, day {}, moon {:.2f}; cirrus {:.1f} cumulus {:.1f}/{:.1f} nimbus {:.1f} "
                  "altostratus {:.1f} stratus {:.1f} fog {:.2f} rain {:.1f} snow {:.1f} evil {:.1f}\n",
                  WEATHER_NAMES[std::clamp(pick, 0, WEATHER_PICKS - 1)], w.hour, w.day, w.moon,
                  w.cloud[fpv::CLOUD_CIRRUS], w.cloud[fpv::CLOUD_CUMULUS], w.cloud[fpv::CLOUD_CUMULUS_MANY],
                  w.cloud[fpv::CLOUD_NIMBUS], w.cloud[fpv::CLOUD_ALTOSTRATUS], w.cloud[fpv::CLOUD_STRATUS], w.fog,
                  w.rain, w.snow, w.evil);
    }
    out.print("  sound: {}\n", sound_api.ok() ? fpv::sound_status() : std::string("SDL audio functions not found"));
    out.print("  map reads left out (the game drew some other place): {}\n", stat_reads_skipped.load());
    out.print("  ocean wave tiles read: {} ({} with the game's own picture)\n", scene.wave_tiles, scene.wave_pictures);
    out.print("  far land: {}\n", farland.status());
    out.print("  loaded part of the world moved {} times (scene kept); map {} x {} x {}\n", stat_map_shifts.load(),
              scene.w, scene.h, scene.d);
    out.print("  game thread, per frame: all {}\n", probe_capture.take());
    out.print("    the game's own draws ({} last frame): {}\n", probe_draw_count.load(), probe_game_draws.take());
    out.print("    map reads: {}\n", probe_reads.take());
    out.print("    creatures: {}\n", probe_units.take());
    out.print("  game ticks: ours {}\n", probe_logic_ours.take());
    out.print("    the game's {}\n", probe_logic_game.take());
    out.print("  render thread: time between frames {}\n", probe_frame.take());
    out.print("    waiting for the game thread's scene: {}\n", probe_scene_wait.take());
    out.print("  fov {}  resolution {}%  view_distance {}  range {}  max_height {}  max_depth {}\n",
              settings.fov.load(), int(settings.resolution.load() * 100 + 0.5f), settings.view_distance.load(),
              settings.range.load(), settings.max_height.load(), settings.max_depth.load());
    out.print("  relative_controls {}  smooth {}  compass {}  minimap {}  minimap_ascii {}  minimap_tiles {}\n",
              settings.relative_controls.load(), settings.smooth.load(), settings.compass.load(),
              settings.minimap.load(), settings.minimap_ascii.load(), mini_tiles());
    out.print("  wasd {}  mouse_look {}  combat_menus {}  inventory_panel {}  journal_panel {}\n", settings.wasd.load(),
              settings.mouse_look.load(), settings.combat_menus.load(), settings.inventory_panel.load(),
              settings.journal_panel.load());
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
    load_compass_places();
    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable) {
    resume_unit = -1;
    resume_hf = -1;
    return set_active(out, enable);
}

// The view belongs to a living creature the player controls. When that ends
// (the adventurer dies, or control of an arena creature is given up) the view
// switches itself off, and the mouse and the normal map come back. When it ends
// only for a while (sleep, rest) and the same creature is controlled again, the
// view comes back by itself.
DFhackCExport command_result plugin_onupdate(color_ostream &out) {
    static int gone = 0;
    static int back = 0;
    df::unit *adv = World::getAdventurer();
    // (in the arena, isAdventureMode() is false while a creature is controlled)
    const bool arena = df::global::gametype && *df::global::gametype == df::game_type::ADVENTURE_ARENA;
    const bool controlled = (World::isAdventureMode() || arena) && adv && !Units::isDead(adv) && Maps::IsValid();
    if (!active) {
        gone = 0;
        if (resume_unit < 0 && resume_hf < 0) {
            back = 0;
        } else if (df::unit *u = resume_unit >= 0 ? df::unit::find(resume_unit) : nullptr; u && Units::isDead(u)) {
            resume_unit = -1;
            resume_hf = -1;
        } else if (controlled && (adv->id == resume_unit || (resume_hf >= 0 && adv->hist_figure_id == resume_hf)) &&
                   default_adventure_ui()) {
            // wait until the game has settled on its plain screen again
            if (++back >= 10) {
                back = 0;
                resume_unit = -1;
                resume_hf = -1;
                set_active(out, true);
            }
        } else {
            back = 0;
        }
        return CR_OK;
    }
    journal_takeover();
    journal_step();
    if (!controlled) {
        if (++gone >= 10) {
            gone = 0;
            df::unit *was = last_view_unit >= 0 ? df::unit::find(last_view_unit) : nullptr;
            resume_unit = !was || !Units::isDead(was) ? last_view_unit : -1;
            resume_hf = !was || !Units::isDead(was) ? last_view_hf : -1;
            set_active(out, false);
        }
    } else {
        gone = 0;
        last_view_unit = adv->id;
        last_view_hf = adv->hist_figure_id;
    }
    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event) {
    if (event == SC_MAP_UNLOADED || event == SC_WORLD_UNLOADED || event == SC_MAP_LOADED) {
        // texture numbers and the map itself change with the world
        if (event != SC_MAP_LOADED) {
            // a map that goes away under a living adventurer (sleep, rest) brings
            // the view back afterwards; a world that goes away does not
            if (event == SC_WORLD_UNLOADED) {
                resume_unit = -1;
                resume_hf = -1;
            } else if (active && last_view_unit >= 0) {
                resume_unit = last_view_unit;
                resume_hf = last_view_hf;
            }
            active = false;
            is_enabled = false;
        }
        showing = false;
        realtime = false;
        restore_zoom();
        // the buttons of the mod must not go by what was true in the last game
        published_state = -1;
        publish_state(false, false);
        farland.clear(scene);
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
    gpu.reset();
    fpv::close_sound(sound_api);
    // The textures belong to the render thread, which normally frees them
    // when the view is switched off; if they are still here they are left to SDL.
    frame_texture = nullptr;
    mini_texture = nullptr;
    ascii_texture = nullptr;
    if (gps)
        ++gps->force_full_display_count;
    return CR_OK;
}
