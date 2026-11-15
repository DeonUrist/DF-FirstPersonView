# First Person View: instructions for agents

## Read first: the mistake that crashed the game for an evening

**Never declare a variable inside `dungeon_hook` or `renderer_hook` (or any struct that hooks a game
class with `VTableInterpose`). Put it outside the struct, next to the other file-level state.**

On 2026-11-13 a counter was added like this, where it looked natural, next to the function using it:

```cpp
struct dungeon_hook : df::viewscreen_dungeonmodest {
    ...
    int strike_ticks = 0;      // WRONG: a data member of a hook struct
    void strike_step() { ... strike_ticks = 0; ... }
```

A hook struct is never created: the game creates its own object (`viewscreen_dungeonmodest`, 424
bytes) and DFHack only makes its calls land in our functions, with `this` pointing at the game's
object. A member we add does not exist in that object. It lies just past its end, in memory that
belongs to whatever the game allocated next, and every write to it damages that.

What it looked like, and why it took hours to find:

- It compiles, and it works: the counter counted. Nothing pointed at it.
- The game crashed with heap corruption (`0xc0000374`) seconds to minutes later, in the game's own
  code: while freeing a civilisation's data when the world unloaded (quitting to the title menu, the
  character dying), or inside the overlay's Lua. Never in the plugin.
- It looked tied to whatever was being tested at the time (a hatch, one save, a new build). It was
  tied to none of them: every build with that member crashed, on any save, whenever the view was on.

What stops it now, and what to do:

- `static_assert`s after both hook structs compare their size with the game class's. Adding a member
  fails the build with "a hook struct must not add data members". Do not remove or relax them; add
  the same check after any new hook struct.
- Functions, nested helper types and `static` members inside a hook struct are fine. Data is not.
- If the game crashes with `0xc0000374`, or crashes on quitting to the title menu, suspect a write
  outside our own memory first. Do not guess from what was on screen: build with the sanitizer (see
  "Hunting a memory error" under "Mechanisms worth knowing"), which names the file and line.

A Dwarf Fortress mod that turns adventure mode (and a controlled creature in the arena) into a
first-person view. Read this before changing anything. `README.md` is the user-facing description
and is kept up to date with every feature; this file is about how to work on the project.

## What it is made of

| Part | Where | What it does |
|---|---|---|
| Native DFHack plugin `fpview` | `plugin/` (C++20) | All drawing, input handling, menus. Almost all the work happens here. |
| Lua script `first-person` | `scripts_modinstalled/` | Command line, hotkey, stored settings, the Camera and minimap buttons (DFHack overlay widgets). |
| Art | `media/` | PNGs and paperdoll layouts loaded at start. The player edits these. |
| Art generator | `tools/make_media.py` | Draws default art that is missing from `media`. Pure Python, no libraries. |
| Installer | `install.ps1` | Copies the script mod and art into the player's mods folder, and the plugin into the game. |

Target: Dwarf Fortress 53.17 (Steam) with DFHack 53.17-r1, Windows. The plugin must be built against
exactly the installed DFHack version.

## Paths on this machine

- Game: `C:\Soft\Steam\steamapps\common\Dwarf Fortress`
- Installed mod (what the game reads): `%APPDATA%\Bay 12 Games\Dwarf Fortress\mods\first_person_view`
  (scripts and `media`). DFHack only scans that mods folder.
- Installed plugin: `<game>\hack\plugins\fpview.plug.dll`. The installed mod also carries a copy in
  `dfhack_plugin\` with `dfhack_version.txt` (written by `install.ps1`): that is how the Steam Workshop
  delivers it. `first-person.lua` (`install_plugin`) copies it into `hack\plugins` at the title screen,
  once per game session, when the two files differ, unloading the old plugin first. Built 2026-11-16,
  not run in the game.
- Saves: `%APPDATA%\Bay 12 Games\Dwarf Fortress\save`
- Toolchain (git-ignored): `_refs\dfhack` (DFHack source at the matching tag), `_refs\tools`
  (portable Perl, pip CMake). See the header of `plugin\build.ps1`.
- The plugin's remembered intent: `<game>\dfhack-config\fpview-intent.txt`. Settings:
  `<game>\dfhack-config\mods\first_person_view\settings.json`.

## Build and install

```powershell
plugin\build.ps1            # compile only (incremental, seconds)
plugin\build.ps1 -Install   # compile and install everything (what the owner uses)
install.ps1                 # copy scripts + media into the mods folder (and the DLL if it can)
```

- `build.ps1 -Install` copies the DLL and then runs `install.ps1 -NoPlugin` (scripts and art). With
  the game running it asks it whether a world is loaded: at the title menu it unloads, replaces and
  loads the plugin itself; with a world loaded it leaves the plugin alone and warns. Either way it
  reloads the overlay widgets (`overlay.rescan()`), so script changes take effect at once.

- The DLL is locked while the plugin is loaded. Installing needs the game closed, or the game at the
  **title menu** with `dfhack-run unload fpview` first and `dfhack-run load fpview` after.
- **Never unload or load the plugin while an adventure is loaded: the game freezes.**
- Every build keeps its DLL, `fpview.pdb` and `fpview.plug.map` (each function's address, plain text)
  in `_refs\symbols\<link time>` (git-ignored, newest 30). A crash dump
  (`%LOCALAPPDATA%\CrashDumps\Dwarf Fortress.exe.<pid>.dmp`) names the loaded plugin by its link
  time, which picks the folder. The build folder is shared: two sessions building at once, or a
  build while another session is mid-edit, gives a DLL of mixed states.
- `install.ps1` keeps art the player has edited in the installed `media` folder and only adds files
  that are missing (`-ResetMedia` restores defaults). Do not delete or overwrite the player's art.
- Script changes can be applied to a running game: run `install.ps1`, then
  `dfhack-run lua "require('plugins.overlay').rescan()"`. Bump the widget's `version` when its
  default position changes.
- `tools\make_media.py` only writes files that do not exist yet in the repo's `media` (the owner
  edits that art by hand); delete a file to have it redrawn. Paperdolls are not drawn by it at all.
  It does not touch the installed folder.

## Rules the owner has set

These came up repeatedly; follow them.

1. **Do only what was asked.** Do not "improve" the player's files or the design on your own
   initiative. If you made an unrequested change, say so and undo it when asked.
2. **Do not drive the game while the player is using it.** They play in the same window. Check
   first (is the process running, which screen, is the window in front). When in doubt, build only
   and ask them to go to the title menu. If they say "don't test in game, just build", do that.
3. **Never click title-menu or save-list entries by fixed pixel positions.** The layout changes
   (a "Delete" button sits next to each world). Find the row by its text on screen and click that.
4. **Commit only when asked.** Nothing has been committed since `48b109a`; the whole plugin, `media`
   and `tools` are still untracked.
5. Saves do not matter to the owner, but quitting without saving loses whatever they were doing, so
   rule 2 still applies.
6. Report honestly what was verified in the game and what was only compiled.
7. **Check that every Lua file you touched still parses before installing it.** The plugin's build
   says nothing about the Lua scripts. A syntax error in `first-person.lua` or `config.lua` stops the
   whole script from loading: the Camera button and every other widget of the mod disappear, and the
   view cannot be turned on. This has happened more than once (last on 2026-11-14: an apostrophe in a
   `desc='...'` string of `config.lua`, whose backslash was lost when the line was written through a
   Python patch inside a shell heredoc). Write Lua strings that contain an apostrophe with double
   quotes (`desc="the game's ..."`) instead of escaping it, and after every Lua change check each file
   with the game running:
   `dfhack-run lua "print(loadfile('<path to the .lua>'))"`, for the repo copy and the installed one
   (`%APPDATA%\Bay 12 Games\Dwarf Fortress\mods\first_person_view\scripts_modinstalled\...`). It must
   print a function, not `nil` and an error. Also look at the end of `<game>\stderr.log` after
   installing: script errors land there.

## How to test in the running game

`<game>\hack\dfhack-run.exe` runs a DFHack command or Lua in the live game:
`dfhack-run lua -f file.lua args`, `dfhack-run fpview status`. Each call pauses the game briefly, so
batch steps.

Useful techniques (helper scripts lived in session scratch folders and are not in the repo; they are
a few lines each):

- **Read the screen as text**: `gps.screen[(x * gps.dimy + y) * 8]` is the character of interface
  cell x,y; bytes 1..3 are its colour. Use it to find menu rows by their text.
- **Keys**: `gui.simulateInput(dfhack.gui.getDFViewscreen(true), 'A_ATTACK')`.
- **Clicks**: set `gps.mouse_x/y`, `gps.precise_mouse_x/y`, `enabler.tracking_on = 1`,
  `enabler.mouse_lbut = 1` and `mouse_lbut_down = 1`, simulate `_MOUSE_L`, then **clear
  `mouse_lbut` and `mouse_lbut_down`**. Left set, the game acts on the click a second time.
- **Screenshots**: `PrintWindow(hwnd, hdc, 2)` on the game window from PowerShell. The OS cursor is
  not captured.
- **Start the game**: `Start-Process 'steam://rungameid/975370'` (lands on the title menu).
- **Plugin test aids** (all still in the build): `fpview pointer <x> <y>|off` (stand-in mouse
  pointer in window pixels), `fpview find` (aim it at a creature in view), `fpview pick`,
  `fpview face N|NE|...`, `fpview camera <x> <y> <z>|off`, `fpview debug look|noblank|center
  on|off`, `fpview intent [next|info|interact|walk|strike|shoot|throw|wrestle|unnock]`, `fpview menu` (contents and pixel
  positions of the paperdoll/attack menu), `fpview hint`, `fpview tilt <degrees>|off` and `fpview yaw <degrees>|off` (look up
  or down and in any direction, as the mouse would). Always end with `fpview pointer off`.
  `fpview status` shows which renderer drew the last frame, its triangle count and where frame time
  went since the last `status` (average and worst: the whole capture, the game's own draws in it, map
  reads, creatures, our part and the game's part of each tick, the render thread's frame interval and
  its wait for the scene lock). `fpview hitches on|off` logs long frames on both threads and every key
  `feed` gets, with millisecond times; `fpview hitches` prints and empties the log.
- **What the test pointer cannot do**: real mouse look (it needs window focus and the real mouse),
  real key state (Shift+Space), and the main-thread races described below. Several bugs only
  appeared with real input; say so when a fix could not be reproduced.
- **Arena**: title "Object testing arena" -> "Create arena" -> first bottom toolbar button (type in
  the filter, pick a row, "Create") -> click tiles to place, right-click to finish -> click a
  creature -> "Assume Control". Unpaused creatures fight at once (`df.global.pause_state = true`).
  `modtools/create-unit` refuses to run on 53.17.

## Architecture

Threads: the game's simulation thread calls the viewscreen's `logic`, `feed` and `render`; the
render (main) thread calls the renderer's `update_all` and also updates `gps->mouse_*` at any time.

Hooks (`VTableInterpose`, all in `plugin/fpview.cpp`):

- `viewscreen_dungeonmodest::render` (`capture`): after the game draws, read the scene from the map
  and the map viewport, ask the game to draw other levels/windows to read those too, then blank the
  top-down map. Paints our one-line tip (top left), rearranges the bottom buttons (`move_bottom_buttons`), hides the portrait and the interface under the minimap,
  updates the paperdoll menus (`update_menu`) and runs the attack driver (`drive_attack`).
- `feed`: input. Menu clicks, intent clicks and R, WASD and relative movement, Space waiting, and
  `PointerRedirect`/`MapForClick`, which make a click act on the tile under the pointer in the 3D
  view.
- `logic`: slides the hidden map so the game's own hover box describes the looked-at tile.
- `renderer_2d_base::update_all` (`draw_view`): camera animation, mouse look, ray-cast the frame
  into a texture drawn under the interface, then minimap, compass, player doll, menus, crosshair.
  With `hardware_renderer` the world is drawn by `gpu.cpp` instead and the frame texture becomes a
  see-through layer over it that only carries what you hold and the marks of a fight (only the
  part of it that changed is uploaded).

Files:

- `scene.h/.cpp`: the cached world. `Cell` per tile per level (solid block, low block, floor, raised
  surface, see-through frame, slope, `box` = shapes inside the tile). `readLevel` fills it from the
  map (tile types, materials, buildings) and from the viewport's sprite layers. `buildUnits` makes
  creature billboards (outlines, gliding). `boxSet` defines the block shapes of furniture and
  fortifications.
- `render.h/.cpp`: the software ray-caster. One ray per screen column walks the map tile by tile,
  front to back, with a list of still-empty row ranges per column for occlusion. Vertical faces,
  horizontal surfaces, slopes, boxes inside tiles; billboards last with a depth test. Pitch is a
  vertical shift of the horizon, not a real rotation. Pixels are stored column by column.
- `gpu.h/.cpp`: the renderer on the graphics card (setting `hardware_renderer`). It builds textured
  triangles and hands them to the game's SDL renderer with `SDL_RenderGeometry`, so it works on
  every SDL back end; nothing in it is specific to a system or a card. No depth buffer: tiles are
  emitted far to near (rows of the map side by side on worker threads, joined in order), and inside
  a tile floor, blocks, furniture, things and door frames go in a fixed order. No perspective
  inside a triangle: `quad()` cuts surfaces near the eye into pieces. No shaders: haze is a second
  see-through triangle in the haze's colour, the highlight a white one. One 4096 x 4096 atlas
  holds every wall/floor texture (with smaller copies for far away), every sprite, its white
  silhouette and its outlined variant; it starts again when full. The software renderer is the
  reference for how things look: compare the two when changing either.
- `render3d.h/.cpp`: `View3D` (a camera that really tilts: used by the graphics card renderer and
  for placing marks on the screen) and `pick_ray` (what one ray through a pixel hits: mouse
  picking for the graphics card renderer, which keeps no per-pixel record).
- `textures.h/.cpp`: reads the game's sprites from `enabler->textures.raws` and makes 32x32
  textures: tinted patterns (`material`), flattened sprites, crops.
- `sky.h/.cpp`: `Sky` (the colour of the sky in a direction for the software renderer, the same sky as
  triangles over `SkyAtlas` for the graphics card renderer, the haze colour and how far fog lets one
  see) and `Precip` (rain and snow drops, painted or as triangles).
- `media.h/.cpp`: loads `media` (via DFHack's `DFIMG_Load`), paperdoll layouts and the doll matcher
  (`bestDoll`), and a small pixel `Canvas` with the game's 8x12 font.
- `models.h/.cpp`: the shapes of furniture, stairs, wells and battlements, read from
  `media/models/*.obj` + `.mtl` (Blender's OBJ export, Y up). Each object becomes one `ModelPart`:
  a box if its outline from above is its axis-aligned rectangle, else an upright prism on its convex
  outline (at most 12 corners). Face normals decide top/underside/side materials; material names
  `own...`/`picture...` mean the thing's material/the game's picture; `[when=..] [hang] [fit]
  [surface] [stand]` in object names. `Scene::makeBoxSet` turns a model into a `BoxSet` (turning
  cabinets, signs and stairs to their wall or entry); `follow_models` in `fpview.cpp` reloads
  within half a second of a file changing and `Scene::setModels` remakes every set in place (tile
  ids stay valid). `build.ps1` embeds the repo's `media/models` files as the fallback
  (`models_builtin.inc`). Palm fronds are still worked out in code. `stairs_bottom.obj`
  (`SH_STAIRS_BOTTOM`, a model only) is used for a stair level with no stair below it; the
  owner made it in Blender 4.3 (own `.mtl`, materials `own.002`). Unknown `.obj` names are warned about.
- `inventory.inc` / `inventory_hook.inc`: the inventory panel (see "The inventory panel" under
  "Mechanisms"). `inventory.inc` sits in the anonymous namespace after `menu.inc` (model, layout,
  painting, the render thread's drawing, the file-level state); `inventory_hook.inc` is included inside
  `dungeon_hook` and holds only functions (input, the clicks on the game's list). Item pictures come from
  `fpv::itemPicture` in `scene.cpp`.
- `jump.inc`: the jump squares, picking on your level and the jump pointer (see "Jumping" under
  "Mechanisms").
- `menu.inc`: included inside `fpview.cpp`'s anonymous namespace. Intents, media/textures for the
  UI, the paperdoll and attack menus (model, layout shared by drawing and clicking, painting,
  reading the game's dialog), the player's own paperdoll, the compass.

Lua (`scripts_modinstalled/first-person.lua`, `internal/first-person/config.lua`): command line,
settings schema (entries with `plugin=true` are pushed to the plugin with `fpview set`), hotkey,
`media_dir()` handed to the plugin with `fpview media <dir>`, overlay widgets. The plugin tells Lua
its state through the Lua globals `fpview_view_shown` and `fpview_minimap_shown`; the overlay must
not call into the plugin (a suspected deadlock during plugin load).

## Mechanisms worth knowing before touching them

- **The game captions whatever of its own lies under the pointer on its own layout, hidden or not**
  (2026-11-15, built only). Over the view the game is shown the pointer at `game_pointer_spot` (the
  middle of the screen, or a point halfway to a side if a window of the game's is there), with the
  window moved to put the looked-at tile under it, in `logic` (`PointerRedirect(..., hover)`) and in the
  tip draw in `capture`; shown it where it really was, it captioned the hidden nearby places in the top
  left. Over our panels (doll, bars, message list) the game gets no pointer unless one of its own cells
  is left there after capture (`note_game_cells`, `game_shows_at`); before, it captioned the buttons
  the ring replaces. Clicks still reach the game at the real pointer.
- **The game's top layer (`screen_top`) holds windows drawn over the rest.** Code that checks the lower
  layer must not empty both: `take_cell` / `blank_cell` do. The "drawn as on the plain screen" step of
  `remove_bottom_buttons` cut the bottom of the ability and craft/butcher windows that way, and
  `hide_nearby_again` cut the corner of the Esc settings; both now empty only lower-layer cells equal
  to what they took off the plain screen. That those windows are in the top layer is the best reading
  of the code, not checked in the game.
- **Jumping** (`jump.inc`, 2026-11-16, built only, not seen in the game). The game's jump
  (`main_interface.adventure.jump.open`, focus `dungeonmode/Jump`, so mouse look is off) takes a tile on
  your own level, also one in the air. `jump_update` (capture) lists the tiles the game allows, by its
  rules read from the code of 53.17 (the comment at the top of `jump.inc`: up to 5 tiles away, a standing
  jump exactly 2 tiles, a running one 3..5 ahead by speed from `unit.job.move_momentum_dir` and
  `gait_buildup`, offsets checked by a `static_assert`). Guesses: the game's tile test is taken as "not
  hidden", the speed is the gait's own without the game's modifiers, walls are left out. `jump_pick` crosses
  the pointer's ray with your level's floor height (the camera of the frame on screen, `jump_note_camera`);
  `PointerRedirect` uses it instead of `pick_pixels` while jumping, and `MapForClick` puts the game's jump
  cursor on the tile. `draw_jump_squares` draws the squares with `SDL_RenderGeometry` under the interface;
  the pointer is `Pointer::Jump`, the ring's jump picture (`jump_cursor`, about three cells tall). Whether
  a click on a map tile jumps at once in the game's jump mode (rather than only moving its cursor) was
  not checked.
- **The game decides what a click or hover hit from its top-down map.** We keep that map blank, so
  for a click the window is shifted to put the picked tile under the pointer and the map is drawn
  for that moment (`PointerRedirect`, `MapForClick`). The hover box is computed in `logic`, so the
  same shift is applied there.
- **The game never wipes the interface grid over the map.** Anything drawn there once stays. On the
  plain adventure screen the grid is emptied before the frame's last draw, which repaints the rest.
- **The main thread rewrites the mouse position at any time**, constantly while the mouse turns the
  camera. Code that hides or moves `gps->mouse_*` must tolerate it being put back mid-draw.
- **The paperdoll and attack menus are a skin over the game's own dialog.** The dialog stays open
  with its cells blanked; our panel is drawn in pixels. The dialog's rows are read off the interface
  grid (hotkey letter in green, name, two rated descriptions, marks), scrolling the game's list to
  see every row. A click on our menu scrolls the game's list to that row and clicks it there. This
  depends on English text layout, not on game structures. A left click inside the menu's panel
  (`on_menu_panel`) goes to `menu_input` before the message panel and the doll and bars squares: the
  panel reaches down over them, and the aimed-strike doll's feet were lost to the message panel
  (2026-11-15, built only).
- **The inventory panel** (`inventory.inc`, `inventory_hook.inc`, setting `inventory_panel`), 2026-11-14:
  with the game's inventory open (`main_interface.adventure.inventory.open`; `adventure->menu` stays
  `Default`, so the view keeps showing behind it) and no DFHack window or item sheet over it. Read from
  the game in the running game (2026-11-14, with the owner opening the screens): on opening, the game
  fills `inventory.option[]` for every list at once (MAIN, DETAILS, DROP, WEAR, REMOVE, PUT_IN,
  EAT_DRINK, INTERACT, THROW); MAIN holds an `adventure_option_inventory_itemst` per item (`depth` =
  how deep inside other items, `invitem` with the body part for what is worn or held, `option_flag`:
  1 details, 2 drop, 4 wear, 8 remove, 16 put in, 32 eat/drink, 64 interact) followed by contaminants.
  `option_current` is the list on screen. Clicking an item's row in MAIN makes `context`
  ONE_ITEM_FULL_LIST, `context_item` that item, and `option_current` its actions (view, drop, remove,
  `adventure_environment_place_in_it_containerst` per container with `getContainerItem()`, eat, throw,
  name); each row on screen is `getFullListName()` after a key letter. The panel is a skin: our clicks
  go to the game's unseen list by finding that text on the grid (`click_inv_entry`, scrolling
  `inventory.scroll_position`, which is taken to count entries), over several frames (`inventory_drive`).
  It is drawn after `update_all`, in front of the interface; the grid cells under it are emptied each
  frame. Clicks on the game's list use what the game drew this frame: no extra draw of the game's
  screen (`redraw_hidden`) for them, since the render thread could show such a draw (map and the game's
  window) for a moment, which the owner saw as a flash on every click; a row on another page scrolls the
  unseen list and is clicked a frame later. The panel's rectangle avoids the mod's interface
  (`inv_free_area`: bottom button rows, message list, paperdoll, compass, minimap). Item pictures are
  the ones the game draws in its own list (`read_game_icons`: a texture anchored over 5 x 3 cells per
  row, flag 0 at its top left, the name on the row below its top after the key letter; these are
  textures the game makes per kind of item, readable with `readSprite`), the unseen list paged a frame
  at a time until every item was seen; `fpv::itemPicture` is the fallback. Their pixels are copied in
  `inventory_grid_capture`, right after the game's draw (`inv_grid.pictures`), and kept per item with its
  listed name (`GameIcon`), never a texture number for later: keeping numbers (item id -> number, number ->
  picture, never cleared, read a frame late) showed the thirst status icon (UNIT_STATUS) on random items
  (2026-11-15), since the game hands freed numbers out again. Cleared on world unload and when the adventurer
  changes (`inv_forget_pictures`). A read is only a candidate (`note_game_icon`): it becomes the item's
  picture once the same number has shown the same pixels for `ICON_STEADY_MS` (250 ms; no picture at all
  only after `ICON_WAIT_MS`), and paging keeps a page up until its pictures have settled. The game makes
  these pictures on its main thread, and a page shown for one frame was read while the number still held
  its old picture (the thirst icon on the water in a waterskin after loading a save, 2026-11-15); that this
  is the mechanism is the best reading, not proven. Until settled the panel shows `fpv::itemPicture`. Built only. Of `unit.texpos[3][2]` some
  are empty (measured: [0][1] and [1][0] fully see-through, [1][1] the sprite): the one with most drawn
  pixels is taken, drawn on the panel's own background. The game's inventory window is taken off the
  grid right after the game's last draw (`inventory_grid_capture`, called from `capture`): its block is
  found from the "Done" text by following the cells with a lower-layer picture; what the game drew is
  copied first (`inv_grid`) and the panel reads rows, pictures and the burden from the copy. With the
  window gone the bottom is laid out as on the plain screen (`bottom_is_plain(...) || inv_window_erased`),
  which before left the game's long bottom bar showing under the panel. A grasping part gets two
  squares (`InvSlot::held`, `held_of` in `inv_slots`): worn, and under it held (an item whose
  `invitem->mode` is `Weapon` (the game's GRASP) or `Hauled`); a drop on the held one takes the item
  into the hand, on the worn one wears it (2026-11-15, built only). Ground row: items with
  `on_ground` on your own tile (what the game's `g` list offers); Pick up goes through that list:
  inventory closed, `A_GROUND`, the row of the `adventure_environment_pick_up_ground_itemst` whose
  `getPickupItem()` is the item clicked (read off the game, 2026-11-14: one "Get ..." row per item on your
  tile, "Get water [3]" for the contents of a container there, environment rows for the tiles around).
  For a stack the game asks how many (read 2026-11-14): `option_list.doing_pickup_amount` set,
  `pickup_amount_max`, the chosen number in `number_amount`, a window "Pick up how many? (0-N)" with
  Okay and Cancel; the panel's dialog (`InvSplit.mode` 1) answers it: `number_amount` set, then "Okay"
  clicked on the copied grid (SELECT if not found). Dropping a stack does not ask (owner, 2026-11-14).
  Reopening sends `A_INV_LOOK` again every 15 drawn frames until the inventory is open (once was not
  always enough), for up to 20 s while the focus stays plain; whether to reopen is decided before the
  click (`reopens_after`), since the game frees its option lists when it acts. Follow-ups (`InvThen`): take off then
  put in or drop; pick up then wear or put in; done once the item is in the list again with the game's
  flag for it. Scrollbars: INTERFACE_BITS tiles as graphics_interface.txt places SCROLLBAR (0..1,
  13..15) and the scroller's top, centre, blank and bottom (6..7, 15..18); across, the same turned a
  quarter (the game has none across). Built only. Done is the game's red button: the 3 x 3 tiles at
  columns 3..5, rows 0..2 of the `INTERFACE_BITS_2` page (read off the game's inventory window), the
  middle column repeated. The game's font: `Canvas::text(GlyphFont)` samples each pixel at its centre
  (it sampled the corner, which doubled other rows and columns than the game's renderer does when it
  stretches its 8 x 12 font into larger cells, so the letters looked different); this also changes the
  message panel. Kept open after an action (`inventory_between`, `inv_reopen_until`): the game closes
  its inventory after most actions; the panel stays drawn and `A_INV_LOOK` is fed once the game takes
  input on its plain screen (not after View, Name, Throw, or when another screen came up). Split and
  share (not the game's): `InvSplit` dialog; the change is made only after the game's inventory has been
  closed (its option lists point at the items; sharing down to 0 removes one with `Items::remove`), then
  it is opened again. Split is `item->splitStack(n, true)` + `categorize(true)` (as DFHack's own scripts
  do), offered only for stacks in a container; share sets `setStackSize` on both; "same thing" is
  `same_kind` (type, subtype, material, quality, maker, wear, no artifact, nothing inside). Strapping
  (`try_strap`, `apply_strap`, owner's request 2026-11-14): the game starts an adventurer with its pouch
  in `Flask` mode on the upper body (its list then names the armour under it) and offers no way to do that
  again; a container without the game's wear flag dropped on a body part with a worn ARMOR or PANTS item
  is put there with `Items::moveToInventory(item, unit, Flask, bp)`, also only with the game's inventory
  closed. Built only. A game crash
  in its grid drawing (`update_all`, 2026-11-14) came with the build that scrolled the hidden list to any
  entry and emptied the picture layers under the panel each frame: now the list is never scrolled past
  its last page (`inv_scroll_for`) and only the text under the panel is emptied. Which of the two (if
  either) caused it is not known. The burden line keeps the game's weight sign (Γ, byte 226) and draws
  `media/weight.png` there. Left-button presses are tracked from `feed` (press) and SDL's button state in `render`
  (release), so a drag needs no event of its own. The creature's picture is the first readable of
  `unit.texpos[3][2]` with most drawn pixels, else the paperdoll. Seen by the owner 2026-11-14 (first
  version). Built only, not seen: the fixes after that (place, font, sprite, icons, weight sign, no flash), the actions, drag and drop, what the game does after an action (whether
  the inventory stays open), `scroll_position` units, what "Remove" does with a worn item when dropped
  on a hand.
- **The bottom row, wound hints, Camera and Timer on the ring (2026-11-15, built only, not seen in the
  game).** `body_layout` (menu.inc): non-compact, the doll is a square `BOTTOM_ROWS` (12) cells tall at the
  bottom left, the six bars a square of the same size at the bottom right (`bx..bh`, `bar_box`), the
  passing bars stack above the bars' square (`passing_box`); the message list fills the room between
  (`msg_span`, window pixels; replaces `msg_columns`). `publish_doll_rows` publishes `doll_rows`,
  `doll_cols`, `bars_cols`, `passing_rows`, used by `over_interface`, `hide_portrait` (blanks both squares
  on the plain screen) and `remove_bottom_buttons` (the message list's free check now spans doll to bars).
  With `radial_menu` off the moved action bar (from `BAR_LEFT` 12) lies over the wider doll square; not
  rearranged. Wound words (`wound_tip`, `layer_hurt`, `TipLine` at the top of menu.inc) are composed from
  `unit_wound_layerst` flags (names from df.unit.xml), `unit_wound.flags` and `body_part_status`; the
  game's Health tab text was not read, so the wording is ours. The player's doll keeps per-slot tips
  (`BodyModel::tips`, parts gathered to the slot as the hurt marks are); the aiming menu looks the target up
  by `attack.attack_unit` / `confirm_unit` or `aim_projectile.target_unit` and matches each row's name to a
  body part's `name_singular` (`find_part`): `MenuEntry::hurt` (mark drawn above the doll box), `gone`
  (drawn as "gone", clicks dropped in `menu_input`), `tip`. The hints are drawn on the render thread after
  `update_all` (`draw_hover_tips`, `doll_drawn` set by `draw_body`). The ring has two fixed slots at the
  top (`RADIAL_CAMERA`, `RADIAL_TIMER`, `radial_slots`): the camera sets `radial_view_off`, carried out with
  `set_active(false)` from the logic hook; the timer toggles `realtime` and shows only the rate; the wheel
  over it (`radial_wheel`, from capture; `gait_wheel` is off while the ring is open) changes
  `settings.realtime_rate` and hands it to Lua in `fpview_realtime_rate`, which `CameraButton:render`
  stores in the settings. The Lua CameraButton is not drawn and takes no clicks while the view and the
  ring are on (it stays the way to turn the view on); TimeButton is hidden with the ring.
  Panel fixes the same day: `inv_free_area` now uses only what stays put (the bottom row across, the
  compass, the minimap's corner even when covered), since the message list, passing bars and minimap
  differ on the game's inventory screen and the panel moved when it opened; with the game's inventory
  window taken off (`inv_window_erased`) capture treats the corners, bottom and doll as on the plain screen
  (`like_plain`, without recording it as the plain screen's picture), so they do not drop out for a frame.
  `look_ok` is also false while `jr.want`, `inv_up` or a `panel_switch` is pending: the frame between the
  game's inventory closing and the journal coming up let mouse look warp the pointer to the middle. The
  journal's scrollbars take a press and a drag (`JB_LBAR`, `JB_DBAR`, `jr.bar_press`, `jr_handle`).
  Under one of the game's windows (a unit sheet opened by a click on the doll, which lies over the game's
  portrait, lists, dialogs) `remove_bottom_buttons(false)` no longer compares the bottom with the plain
  screen's picture (with a sheet open the game drew some buttons otherwise and they stayed over the doll and
  the bars): every block of drawn cells (4-connected) in the bottom band (12 rows + passing bars, at least
  4) that does not reach up out of the band is taken off; a block that does is a window and stays. The
  message list stays under windows (`msg_ok` true); a click on it goes to the game where one of its cells
  is drawn there (`game_cell_at`). A block is also kept when it holds text on 3 or more rows, or lies
  within 3 rows under a kept block and inside its width (a dialog's own buttons split off by an empty row:
  the owner saw the bottom of the craft/butcher and ability windows missing). Under a dialog the portrait's
  12 x 8 is no longer blanked separately (it was cutting dialogs anchored bottom left); it goes with the
  other picture-only blocks. Before the blocks are looked at, cells drawn exactly as on the plain screen
  (`bottom_gone`) are taken off: the action bar under a unit sheet touches the sheet's bottom edge and
  was kept as part of it (seen by the owner). Built only.
  A click anywhere on the doll's square (plain screen, no game cell drawn there) opens the adventurer's
  sheet through `show_info` and is swallowed; one on the bars' square is swallowed. Before, the click went
  to the game, which opened the sheet only over its hidden portrait (12 x 8) and pressed its hidden
  buttons elsewhere on the square (companions, the speed display). Built only.
  Sneaking: `adv_sneaking` from `unit.flags1.hidden_in_ambush` (taken to be the game's sneak state, not
  checked) colours the speed line at the top of the message list purple. Tracks: a ring action "tracks"
  (A_DISPLAY_TRACKS, the travel page's TRACKS_ON picture, dimmed while `adventure.tracks_flags.display_latest`
  is off). With the display on, `readLevel` looks for a picture of the TRACKING_SYMBOLS page (464 tiles,
  `is_track_symbol`) in the viewport's layers over the ground (`track_symbol_at`; which layer the game uses
  was not read) and lays it over the tile's floor texture after the spatters (`tex.over`), so both
  renderers show it; such a picture is never taken for an item, plant or spatter. Built only.
  Ring toggles (`RadialAction::state`, `ax/ay`, `lit`; `radial_toggle_on`): sneak (travel page SNEAK_OFF
  23,0 / SNEAK_ON 19,0, state `adv_sneaking`) and tracks (TRACKS_OFF 47,0 / TRACKS_ON 43,0) show their "on"
  picture and the gold ring while on (the game outlines the on button; `strip_button_art` would take that
  outline off with the frame, so the ring stands in for it); stance switches to PRONE 48,3 while
  `adv_prone` (`flags1.on_ground`), without the ring. Yield and weapon have pictures for both states but
  the state was not found in the game's structures; they keep one picture. Built only.
  With the ring, `hide_portrait` runs after `remove_bottom_buttons`: the speed display ("Walk: ...", the
  message list's top line) is found from the buttons right of the portrait, which the wider doll square
  covered, and it went missing. The ring's wheel uses SDL's pointer (`gps->precise_mouse_*` is hidden while
  the game draws) and swallows the keys the game makes of a notch (`radial_feed_wheel`). Ring icons:
  `strip_button_art` makes see-through what joins the block's edge in the button chrome of
  interface_bits_adventure.png (background 28,28,28, frame 102,132,163, gold top line 255,191,1).
- **Do not let the game's journal come up and go away again (2026-11-15).** The first journal panel let
  the game's journal open (its button) and closed it a frame later. Each time a game screen comes or goes,
  `capture` switches the hidden map's zoom (`restore_zoom` / `zoom_for_capture`, setting
  `gps->main_thread_requesting_reshape`), which makes the main thread reallocate every array of the map
  viewports. Two such switches a frame apart crashed the game twice (black screen, then a memset into a null
  `screentexpos_item` of `gps->main_viewport` at `Dwarf Fortress.exe+0xaf62a0`, the game's viewport clear,
  on the main thread) and once froze it (heap already corrupted when Lua freed memory; the game's crash
  handler at `+0xaefdd0` then waited forever for the main thread). Read from the crash dumps; that the
  reshape race is the cause is the best reading of them, not proven. The journal key and a click with
  `main_interface.current_hover == ADVENTURE_LOG` now open the panel without the game's journal
  (`journal_key`), and while a game journal that is about to be closed is up the zoom is left alone
  (`journal_being_replaced`). Closing it uses LEAVESCREEN, never `Screen::dismiss`. How to read such a
  dump without a debugger: parse the minidump's thread, module and exception streams; name plugin frames
  with `llvm-symbolizer --obj=fpview.plug.dll` from `_refs\symbols\<link time>` (MSVC's bin folder has
  it); DFHack frames from `dfhack.dll`'s export table; game code with `dumpbin /disasm /range:`.
- **The ring and the bottom row** (2026-11-15, built only): `radial.inc` (geometry, drawing, Tab and
  clicks from `feed`), the actions in `radial_actions.inc` (keys and icon tiles read off the game's screen
  by texture on the owner's save; the hand as A_GROUND and sneak as A_SNEAK are guesses; "..." is the
  INTERFACE_BITS settings gear, the game has no ellipsis art). Tab is CHANGETAB (or STRING_A009). An
  action's key is fed with INTERPOSE_NEXT(feed) from the feed hook; A_LOG opens the journal panel
  instead. `remove_bottom_buttons` (fpview.cpp) replaces `hide_corner_buttons` + `move_bottom_buttons`
  while `radial_menu` is on: on the plain screen every used cell of the bottom 6 rows from column
  BAR_LEFT is remembered and emptied (the speed display's words read first); on a game dialog only cells
  still equal to that are emptied, and `msg_ok` keeps the message panel unless the dialog has cells in
  its place (bottom 12 rows, right half). The owner's report that crafting and abilities shift the
  interface and cut its bottom was not reproduced; whether this change cures it is not known.
  The view resumes after travel by historical figure as well (`resume_hf`): the unit may not be found
  while travelling. The camera button hides itself on the travel map (`adventure.menu == Travel`). Under
  the journal and the map the pointer is not handed to the game and nothing is highlighted
  (`cover_x0..y1`, `in_cover`).
- **Game-style panels, tabs, the Map** (2026-11-15, built only, never run): `gamestyle.inc` copies the
  INTERFACE_BITS tiles (x < 46, y < 20) once per world on the simulation thread (`load_game_art`, from
  `journal_frame`) and paints from the copy: the frame (corners 4 x 3 cells at 0,0 / 5,0 / 0,4 / 5,4,
  edges 4,0 / 4,4 / 0,3 / 5,3; the double line about 1.5 cells in, so content starts 3 cells in), TAB /
  TAB_SELECTED (16,6 / 21,6, 5 x 3) and SHORT_TAB (32,8 / 37,8, 5 x 2), BUTTON_RECTANGLE (0,7) and its
  selected one (16,9) squeezed into 2-row buttons, SCROLLBAR (0,13) with the handle pieces (6,15..18),
  BUTTON_FILTER (0,16, fitted to 2 rows), BUTTON_ADD / SUBTRACT (3,7 / 3,10). Positions were read from
  graphics_interface.txt by a helper, not checked in the game. `panel_room` (inventory.inc) is the one
  place for all three panels, with `panel_tabs_h` (2 rows) kept above for the tabs (`panels.inc`, drawn
  after the panel, their third row over its top edge). Switching (`panel_switch`, `panels_logic` in
  `panels_hook.inc`, from the logic hook): to Inventory, A_INV_LOOK every 15 frames until it is open; to
  Journal/Map, LEAVESCREEN until the game's inventory is closed, then `jr_open`. The journal waits (not
  cancelled) while `inv_up`. The inventory's insets changed with the frame (pad 3 cells, title at row 3,
  content from row 6, status 4.5 rows from the bottom, Done inside the frame). The Map (`jr.view` 1,
  `read_map_view`, `paint_map_view`): world squares from `wmap.inc`, which reads
  `data/vanilla/vanilla_world_map/graphics/graphics_world_map.txt` itself and composes a square from
  WORLD_MAP_TILES (biome from `Maps::getBiomeType`, `_GOOD` / `_EVIL` / `...SAV` from evilness and
  savagery), WORLD_MAP_FORESTS / MOUNTAINS scaled onto it, RIVER_* / BROOK_* from the region flags,
  SITE_* by site type; variants chosen by a hash of the position. Coasts (`wm_coast`): read from the
  game's code by a helper (2026-11-15), vanilla draws no land edge into water (only shallow into deep
  water, and glacier edges); the plugin cuts the land neighbour's tile into the water square with
  WORLD_EDGE_SHAPE_* as a mask and lays WORLD_EDGE_BEACH_LINE_* over it, and deep water gets
  WORLD_EDGE_WATER_*. Shapes as the game's rules choose straight, thick corner, inlet and surround; thin
  corners only for a lone diagonal; the thinning and transition pieces are not used. Otherwise no edge
  blending (the game's own
  choice is not known; the game fills `gps->main_map_port` only while its map screens draw). Marks are
  the tracked entries, drawn as `!` in each one's colour (`MARKER_COLOURS`, `new_marker_colour`: random
  among the unused; kept in the tracking file as a fifth field in hex, lines without one get a colour by
  position). A tracked region's mark stands at `region_centre` (the discovered square nearest the mean of
  its discovered squares; `marker_pos`) and its known squares are tinted (`JMapView::area_of`). Hits on the
  map: marks, then known sites (`JB_MSITE`, named on hover), then a tinted region (`JB_MAREA`); a press on
  any of them is a click when the pointer has not moved, else a drag. The compass button (`draw_places_button`, `toggle_compass_places`,
  menu.inc) uses media/compass_places.png, else the game's SITE_CASTLE, else built-in rows.
- **The journal panel** (`journal.inc`, setting `journal_panel`), 2026-11-15: the game's journal key is
  caught in `feed` (`journal_key`) so the game's journal never opens; the game's journal
  (`viewscreen_adventure_logst`, its button) is closed from the plugin's update (`journal_takeover`,
  never from a draw) whenever it comes up, unless the panel's own "Game journal" button asked for it, and
  the panel opens on the matching tab (and at `journal_site` for a compass click). Read from the game
  (2026-11-15, the owner's save, the game's Quests tab open): it listed one agreement (OfferService, the
  adventurer's figure a party, a lord as the other) while `known_info.known_agreement_id` and
  `heard_of_stid` were empty and `known_locations` null; so quests are the agreements with the
  adventurer's figure in a party, a site is known when its world tile is `discovered` (or heard of). A
  place is a named `abstract_building` of a known site. `agreement.smm_x/y` is taken as world tiles / 16.
  The model is rebuilt on the game thread on every click or key (`jr_publish`), drawn on the render
  thread like the inventory panel; the room is the inventory panel's (`inv_free_area`, same caps).
  The owner's answers (2026-11-15): distances in the compass's form ("3k NE"); English name first, native
  under it; tabs for rumours/events, artifacts and the bestiary, a small map on each page, and the People
  tab to also hold the leaders of known sites and civilizations; and a track box on each entry that puts
  it on the compass (yellow when its place is known, grey when not). All built 2026-11-15, not run.
  Rumours are `known_info.rumor_info.events`, identified by their index there (an entry tracked by index
  points elsewhere if that list changes order). Bestiary: `creature_knowledge`, race from
  `raws.creatures.list_creature[combined_caste_id]`, merged per race. Artifacts: named by a rumour or quest,
  or held/owned by a listed person. Incidents (witness reports), which the game's Events list also shows,
  are not read. Tracking: `JRef`s per save (`cur_savegame.save_dir`) in `dfhack-config/fpview-tracked.txt`;
  `add_tracked_marks` (called from `tidy_top` in `menu.inc`) puts them first on the compass, recomputed every
  30 frames; `CompassMark`/`MarkSpot` carry the entry (`jkind`, `jid`, `jsub`, the `JK_*` enum now lives in
  `menu.inc`), and a click on such a label opens the panel there directly (`mark_entry_at`).
- **The travel map** (`travelmap.inc`, 2026-11-15, built only, not seen in the game). While travelling
  the adventurer's unit is gone (`World::getAdventurer()` null: the local map is put away), so
  `plugin_onupdate` turns the view off (`active` false) and remembers `resume_hf`; `travel_map_ours()` is
  the travel map (`adventure.menu == Travel`, focus `dungeonmode/Travel` only) while the view is on or
  waiting to come back. Its place then: `travel_smm` (the player army's `pos`, in steps of 16 tiles, or
  `travel_origin_x/y` while `travel_not_moved`); `adv_tile` falls back to it and `adv_figure` to the
  nemesis record `adventure.player_id`, so the journal works there. Layout read in the game (1920 x 1080,
  owner on the travel map): `gps->main_map_port` 120 x 68 squares of 16 px from the renderer's origin
  (0, 4), the army on (`screen_x`, `screen_y`), the port's middle; a square is one embark square (48 tiles,
  checked against three sites) or, when the game zooms in at a site by itself (`site_level_zoom` 1), 16
  tiles (seen on the screen: the coast three times larger). The square's pixel size is taken as window /
  port size rounded up; other window sizes were not seen. `travel_frame` (game thread, after the game's
  draw in both branches of the render hook) turns the tracked entries into squares; `draw_travel_marks`
  (render thread, after `update_all`, under the journal panel) draws a "!" with its foot on the square's
  top edge and the name label under the square, or, outside the room (2 rows under the top, 4 above the
  bottom), at the room's edge on the line from you, with an arrow and the distance. The journal panel
  works there: the render hook's inactive branch keeps it (`journal_frame`, `journal_input`,
  `journal_key` in a travel branch of `feed`, which also takes `A_TRAVEL_LOG` and a click with
  `current_hover == ADVENTURE_TRAVEL_LOG`); `journal_takeover` also runs while travelling. The Inventory
  tab does nothing there (no inventory while travelling).
- **Intents** (`menu.inc`, R in `feed`): a left click in the view does what the intent says. Info sets
  `game->main_interface.view_sheets` (UNIT, BUILDING or ITEM: the game's own sheets, which adventure
  mode shows over the view). Interact on a creature: `A_TALK` opens "Click a creature to start a new
  conversation"; people are not listed, the creature has to be clicked on the map (`click_tile`), which
  turns the list into "Start new conversation with ..." (`adventure_option_talk_new_conversationst`,
  `unit_id`), which is then clicked (`drive_talk`, stages 30-31). Interact on a thing and Walk onto a
  creature's tile go through the tile's right-click list (`drive_tile_list`, stages 40-41). Walk is
  otherwise the plain click passed to the game. Ctrl has no meaning any more.
- **A dialog or list of the game takes no key or click in the frame it was opened in** (seen with the
  talk dialog and the right-click list: a LEAVESCREEN or a click fed right after the key or click that
  opened it does nothing). Whatever must be answered in it is done from `drive_attack` in a later
  frame. `step_onto` used to pick from the list in the same frame and could not have worked.
- **Clicks while the game is busy** (`pending_click`, `pending_step`): an intent click while the game
  does not take input (`game_takes_input`: not TAKING_INPUT, a wait running, more messages, or your own
  strike still on its way) or while `attack.frames` > 0 is kept for 2.5 s and carried out when the game
  is ready. Before this such clicks were simply lost ("a fast click does nothing").
- **Attack driver** (`AttackDrive`): Strike/Wrestle right-click the creature, pick the game's
  "Combat ..." entry, answer the conflict question and pick the move. Shoot/Throw press the game's
  key, wait for the aiming screen, then **repeat the click on the target**: the game only offers a
  creature (and "Aim carefully / Quickly shoot") when it is clicked, not when the aiming cursor is
  moved onto it. If several ways are offered, the list is left for the player.
- **The view must not move when the pointer is released** (a menu opening): what was under the
  crosshair has to stay there for the repeated click. `look_yaw` is kept while look is off.
- **Other mods' overlay widgets can sit on ours** (Herald's alert is on the Camera button) and take
  the click first. The Camera button therefore detects the press in `render` from
  `enabler.mouse_lbut_down`.
- **Focus strings**: an adventure's plain screen is `dungeonmode/Default`; in the arena it is
  `dungeonmode/ADVENTURE_ARENA`, which stays in the list while menus are open. Use
  `plain_adventure_focus()`.
- **Paperdolls are data.** `media/dolls/NAME.png` + `NAME.txt` (`part name | alt = x, y`). The doll
  with the best score for the creature's part names wins; `humanoid` is for bodies without a neck,
  `humanoid_neck` for those with one. Do not merge them. The aiming menu scores the target's whole body
  plan (`body.body_plan->body_parts`, lost parts too), not the game's list: the game leaves severed parts
  out of it, and a doll picked from the list switched mid-fight (a molemarian, `HAND_FOOT_CENTAUR_NECK`,
  went from `quadruped_neck` to `humanoid_legless` after losing two legs, 2026-11-15). The game's rows are
  put on the doll by name; a lost part with a place on the doll gets an entry of its own after the
  game's rows (an entry's index is its row in the game's list) with `gone` set. Built only.
- **DFHack calls `plugin_onupdate` only while `is_enabled` is true** (`Plugin::on_update`). The view
  coming back after sleep or travel (`resume_unit`, `resume_hf`) is decided there, and `set_active(false)`
  used to set `is_enabled` false, so it never came back (found 2026-11-16, built only). `sync_enabled()`
  keeps it true while a resume is pending; use it wherever `active` or the resume ids change.
- **Never call `dfhack.textures.deleteHandle` on batches of textures**: it crashed the game.
- **A texture number does not name a picture for good.** The game hands freed texture numbers out again
  (seen 2026-11-15: after a second world load in one run the pages' numbers ran downwards through old
  ones). Copies of the game's tiles kept by number (`page_image`'s cache) or once per save (ring icons,
  panel frames) then showed other tiles until a later load happened to fit. `page_epoch()`
  (inventory.inc) changes when any page's first, middle or last texture has another number or picture;
  everything that keeps a copy of a page tile compares it. Read in the running game: the game's own
  tiles were right while ours were wrong. The fix is built only, not seen in the game.
- **The far land** (`farland.h/.cpp`, `Scene::far`, `FarView`, `Scene::farTreeLook`), 2026-11-14, rebuilt
  as blocks the same day after the owner saw the first version (a smoothed triangle mesh that left
  see-through gaps where it met the map, and round billboard trees). Checked offline only, never seen
  in the game in this form. `FarLand::update` (game thread, scene unlocked) snapshots the world map once
  per world (`region_map` via `Maps::getRegionBiome`, `getBiomeType`, region tree lists `tree_biomes` /
  `tree_tiles_1/2/good/evil`, else `population` entries of type Tree; tree colours from the leaf and
  bark textures under the scene lock; tree shapes made up after the raws), and whenever the game's
  window moves reads `midmap_data.region_details` (`elevation[x][y]`, `biome` as a keypad digit 1..9
  naming the world tile whose biome applies), the loaded map's ground level and colour on every tile
  (scanning down from a guess near the last column's ground), and the real trees' shapes
  (`plant::tree_info->body[z][x + y * dim_x]`, x and y offset by `dim_x / 2`, `dim_y / 2` from
  `plant::pos`), and measures the offset (median of real ground minus mid-level elevation). Builds run
  with `std::async` every 48 tiles walked; a finished view is taken only if `region_x/y` still match.
  Rings: `RING` = 48 cells each side, `C0 << k` tiles a cell (4, 8, ...), the centre snapped to twice the
  cell; heights are whole levels; `FarLevel::low` is how far down each side shows (the lowest neighbour
  top along it, found with `FarView::cellAt`). Trees: `FarTree` with full (leaves in up to three bands a
  level) and simple (one block a level) box lists; at most `MAX_TREES` (6000) nearest; beyond the last a
  cell with forest cover is raised by its kinds' height. GPU: after the sky, before the map; cells and
  trees gathered and their triangles made on the worker pool, joined in the sorted order (far to near; a
  tree right after the cell under it nearest the eye); flat colours, lit by face (top 1, west/east 0.9,
  north/south 0.8, bottom 0.6), hazed per corner; cells wholly inside the drawn part of the map, and
  trees there, are left out; only where the sky is seen. Software: a march along each column's ray from
  where it leaves the drawn map, nearest cell (blocky), a cell's side colour where the march steps up onto
  it, tree tops per tile (`tree_top`, leaf columns, no trunks), depth per pixel (`far_depth_`, spans with
  `tz == -2`). Snow (`FarLand::readSnow`, once the game's clock, `cur_year` * 403200 + `cur_year_tick`, has moved 100 ticks since the last read, and when the window moves): on every loaded-map tile
  the largest `block_square_event_material_spatterst` with `mat_type` 6 (water) not liquid, at the
  tile's ground level, full from 100; its mean over the land is `Snow::cover`, and a change of more than
  3 points rebuilds. In the build, `snow_at`: the tile's own on the map, else `cover` times
  `1 - (temperature - temperature here) / 25` (the world map's `region_map_entry::temperature`, whose
  units were not checked). Ground: a cell is snow or bare (`rnd` of the cell < 1.3 x snow), never a blend
  (the owner saw a blend as washed out). Leaves: `FarBox::snow` (1.15 x snow, at most 0.9), drawn by the
  GPU renderer as a second pass of white specks (`Impl::speck`: sheets of 8 x 8 tiles at 4 densities and
  32/16/8/4 pixels a tile, made in `resetAtlas`), or mixed in where a tile spans under 3 pixels; the
  software renderer mixes it in (specks would be streaks there). Woods 0.8 / 0.6 of that mixed in;
  lakes freeze from 0.6. Trunk boxes are drawn before a tree's leaves (they stand inside them). Snow on trees is taken from the ground's, not read off the
  leaves. Offline, dense forest everywhere at 960 x 400: GPU triangles 5-7 ms on the pool, bare land
  2.5 ms; a build 30-40 ms off the game's thread. Guesses: one elevation is one z-level, sea level at
  elevation 99/100 (measured when the map has sea), a lake's surface at its world tile's elevation, tree
  density per biome, the made-up relief, the colours of ground classes (the near view learns its colours
  from the game's pictures; these are fixed). `FARLAND_OFFLINE` compiles only the builder, for a test
  program without the game; the test (in a session scratch folder) also ran both renderers, the GPU one
  through stand-in SDL functions that fill the triangles into a picture.
- **Visibility gradient** (`sight.h/.cpp`, `Scene::sight`, setting `visibility_gradient`), 2026-11-15, built
  only, never run. Read from the game's map on the game thread (`readSight`, after `farland.update`, again
  when the tick, your tile, the drawn levels or the map's region change, and every 30 frames): in adventure
  mode the first of a tile's `designation.dig` bits is its line of sight (df-structures' comment, not checked
  in the game) and `outside` is open to the sky. Seen tiles get 1 (0.8 under a roof), times a fade from
  unseen ground (a floor, slope, stair or water not in sight) over `SIGHT_FADE` (5) tiles, measured through
  seen open tiles only (bucketed distance, 2 across, 3 diagonally), so the rock behind a wall does not darken
  the room; unseen rock and air say nothing; a seen wall is as light as the open ground it faces. Tiles
  with `outside` are lifted towards 1 by `daylight_now()` (passed as `day`; owner's request 2026-11-16:
  outdoors only while the sun is down), unseen outdoor ground to `day` itself. Sampled
  bilinearly between tile middles (`SightMap::at`, taking the brighter of the two levels a floor or ceiling
  touches; nothing known there: the column's light); beyond the part read (and the far land) the edge's
  (`column`). Software renderer: `Span::da/db` (distance at a span's first and last row), the light at both
  ends spread over the rows after the water veil; the far land per pixel from `far_depth_`; billboards, door
  frames (`Masked::keep`) and discs through `Shade(fog, f, keep)`. GPU: vertex colour of the textured pass
  (`light()` turns camera space back into the world), the haze pass's colour dimmed alike, far faces and
  decals dimmed. The sky is never darkened. Whether the game's line of sight covers the whole map by day
  (else the far land goes dark outdoors too) was not checked.
- **The game moves its loaded part of the world** in steps of 48 tiles (`world.map.region_x/y` count
  them) when the adventurer walks far: every local coordinate then means another place. The scene
  is moved along (`Scene::shift`: level grids, column ranges, billboards, creature tiles, glides),
  textures and shapes are kept, and only the newly loaded strip plus a 2-tile band along the old edge
  are left unread for the map reads (2 levels a frame, unread tiles only). Before 2026-11-14 the
  whole scene and every texture were thrown away there, which showed as the world rebuilding itself.
  A change of `region_z`, of the map's size or a jump farther than the map still resets everything.
  Nothing beyond the loaded block can be drawn, whatever `view_distance` says. Built only, not seen in
  the game; `fpview status` counts the shifts and prints the map's size.
- **Never feed the game a key that opens one of its windows from inside the `render` hook.** On
  2026-11-14 the inventory panel reopened the game's inventory (`A_INV_LOOK`) from `render`, after a
  drink: the game froze for good (not responding, no CPU). A thread dump (MiniDumpWriteDump with
  MiniDumpNormal, then the stacks scanned for return addresses against `fpview.plug.map`) showed the
  simulation thread sleeping in a wait inside the game's drawing and the main thread waiting on it.
  Opening a window makes the game build pictures (item icons), which needs the main thread, and the
  main thread waits for `render` to end. The inventory panel's keys, clicks and item changes are made
  from the `logic` hook (`inventory_logic`, once per drawn frame); `render` only draws and keeps
  account. Older code still feeds keys and clicks from `render` (`drive_attack` and the steps after
  it); none of those is known to open a window that builds pictures, but keep it in mind.
- **A hook struct (`dungeon_hook`, `renderer_hook`) must not have data members.** It is laid over the
  game's own object, so a member lies past that object's end and writing it damages the game's next
  allocation. `int strike_ticks` inside `dungeon_hook` did this on 2026-11-13: the game crashed with
  heap corruption (0xc0000374) seconds to minutes later, in unrelated game code or when the world
  unloaded. `static_assert`s on the structs' sizes now stop the build. State goes in variables
  outside the struct.
- **Walking lag (measured 2026-11-13)**: after each step the game's own tick takes 50-90 ms (the same
  with the view off) and the render thread waits for it, so no frame is drawn then. Our part of a tick
  is ~0 ms, the render thread never waits on the scene lock, and our capture is ~6 ms a frame (three
  game draws: the frame's own, one window read, the game-tip draw). Turning off tips, lowering
  `range` or `resolution` did not change the walking pace (~9 steps per 2.5 s with the view, ~11
  without). What looked laggy was the camera dashing across a tile in ~0.15 s and then standing until
  the next step; it now glides at the pace of the last steps (`glide_pace` in `draw_view`).
- **Hunting a memory error**: set the environment variable `FPVIEW_ASAN`, touch
  `plugin\CMakeLists.txt` and build (AddressSanitizer). Copy `clang_rt.asan_dynamic-x86_64.dll`
  from the compiler's `bin\Hostx64\x64` next to the game's exe, start the exe directly (not through
  Steam) with `ASAN_OPTIONS=log_path=asan.log` (a relative path: a drive letter's colon breaks the
  option), and read `asan.log.<pid>` in the game folder: it names the file and line of the bad
  write. Unset the variable and touch the file again to go back.
- **Vitals** (the bars of the player's doll, `read_vitals` in `menu.inc`): every number was read from
  the machine code of Dwarf Fortress 53.17 (disassembly of `Dwarf Fortress.exe`, 2026-11-14), not
  guessed. Adventure mode uses other numbers than fortress mode (the game tests `gamemode`).
  - The unit sheet's status words (function at 0x140486f00): pain, fever, paralysis, numbness >0 slight,
    >=50 moderate, >=100 extreme/serious; exertion 2000 Tired, 4000 Over-exerted, 6000 Exhausted.
    Adventure: hunger 57600/172800 Hungry, 1209600 Very hungry, 2592000 Starving; thirst 57600..172800
    Thirsty, 345600 Dehydrated; sleepiness 115200..259200 Drowsy, 345600 Very drowsy, 864000 Slumberous.
  - Needs (per-unit tick 0x14136d0f0): in adventure only `adv_unit` gets hungry, thirsty, sleepy, +1 a tick
    each (86400 a day). Thirst kills at 864000; hunger from 5184000 burns 10 fat a tick, death when fat
    is 0; sleepiness never kills or forces sleep in adventure (capped at 5184000). Eating/drinking
    takes 50000 off. Sleeping in adventure: net about -2 a tick.
  - Exertion: + the gait's energy use each move tick (Sprint 50, Run 10, Jog 5), fixed amounts in
    combat; recovers rand(ENDURANCE+400)/200 a tick. 3800 drops a commanded gait, 4000 collapses (no
    standing up below Endurance and Willpower 1750), 6000 knocks out (50..399 ticks).
  - Pain: rebuilt every tick as the sum of wound and wound-layer pain (a part with its nerve cut adds
    none), capped at 200, 0 while unconscious and for creatures that feel none. From 100, each tick a
    discipline roll (Willpower, Focus; halved by the pain itself and by other states) decides
    "gives in to pain" (unconscious 50..399). How much pain a new wound gets was not traced.
  - Blood: death at 0 (blood_max is the body size); losing blood never knocks out; the sheet's moderate /
    severe blood loss below 1/2 and 1/4 left. Infection_level >= 300 drains blood instead of refilling.
  - Suffocation +1 a tick while winded, death at 200 + (TOUGHNESS + ENDURANCE) / 4 (`flags1.drowning`:
    drowning). Stunned and unconscious are countdowns in ticks (a blow stuns 50..99, a knock-out is
    350..499). Webbed: each web 50..99, stuck from 10. Nausea, dizziness, paralysis, numbness, fever are
    rebuilt every tick from the wounds; a syndrome sets nausea/dizziness to 200 in adventure.

## State of the work

Confirmed working in the game (at some point; later changes may have affected them): the view
itself, minimap and its buttons, held items, WASD, right-click, Strike end to end through
the paperdoll and attack menus, Throw end to end, Shoot up to and through careful aim on the
paperdoll, intent switching and aim marks, compass picture, game tip aimed at the looked-at tile,
our tip above the bottom buttons, Camera button, player paperdoll with blood bar, arena support,
Space and Shift+Space waiting.

The graphics card renderer was compared with the software one in the game on 2026-11-13 (town,
altar and statue, well, rooftops, slopes, palm forest, shop sign, looking straight up and down,
view distance 160, picking, tile highlight, the ring under a creature, switching between the two
while playing): the pictures matched. Not seen with it: real mouse look, the marks of a fight
(crescents, arrows, rising words) over it, water, fortifications, a night scene, Linux.

Built and installed but **never seen running** (the owner asked for build-only, or they need real
input):

- Furniture and fortifications as block shapes, items at 75% size and on table tops, leaf sprites,
  wider camera tilt, smooth movement of the player and creatures.
- Plain furniture tops, bed sheet and pillow, slab drawing on its flat sides.
- Large (multi-tile) creatures centred over their tile.
- The view switching itself off when control of a creature ends or it dies.
- Wound marks on the player's paperdoll (no test character was hurt).
- Shoot with real mouse look after the "view no longer snaps" fix.
- Wrestle (the test character's hands were full), paperdolls other than the humanoid ones.
- Blocks inside a tile drawn per pixel (bed sheet over its frame), cabinets against their wall with the
  game's sprite as doors, item icons on tables (from `building_actual.contained_items`, role TEMP) and
  heaps of icons for several items on one ground tile (icons from `world.raws.itemdefs`).
- Valley ramps (`RAMP_VALLEY_U/V`), slab emblem crop (`spriteEmblem`, a colour heuristic), minimap facing
  arrow, hover disc and next-step arrow (`draw_ring`), reach-limited melee outlines, leaves from
  `plant_raw.tree_texture_info` instead of the screen.
- Cabinets and bookcases built from parts and turned to their wall, palm fronds (`SH_FROND_HUB/ARM`,
  `spreadFronds`; a crown is a trunk tile carrying leaves by the tree's raws: a LEAVES growth hosted
  on the trunk, in season, at that tile's height, `has_frond_leaves`), coins from the `ITEM_COINS` tile page tinted
  by metal.

- Stairs as a shape (`SH_STAIRS`, a spiral of round pieces: `Box::round`, `roundClip`, `roundBox` and `split` in
  `gpu.cpp`; the software renderer tests the blocks of all levels of a tile together; hatches on up/down stairs skipped; no floor in stairs that lead down to stairs
  coming up, `stair_hole`), Shift+W / Shift+S for up / down and c for lie down / stand up (sent as
  `A_MOVE_UP`, `A_MOVE_DOWN`, `A_STANCE` from `feed`).

- Hatches (`SH_HATCH`): a frame in the floor, plus a lid with the game's picture (`flatWall`) while
  `building_hatchst.door_flags.closed`; a hatch never seen gets the shut lid from the game's `ITEM_HATCH_COVER`
  page (column 1, row by material: wood, stone, metal, glass), since from below the game shows nothing of it; never a solid block (the door handling in `readLevel` skips
  them: as a block a shut hatch buried the walls around whoever stood on it). Floor grates and bars
  share the shape and are always shut.

- Conflict colours (`conflict_of`, `conflict_colour` in `scene.cpp`): discs under every creature in a
  conflict and outlines in the level's palette colour, read from `unit.activities` ->
  `activity_event_conflictst.sides[].enemies`. That `enemies[].id` is the other side's `id` and that
  finished conflicts leave `unit.activities` are assumptions.

- The row of foes under the minimap (`Foe`, `foe_strip`, `collect_foes`, `draw_foes`, `foes_click` in
  `fpview.cpp`): creatures whose conflict with you is above Training; a click sets `foe_aim`, which
  `draw_view` turns into `look_yaw` / `look_pitch`. Whether the game draws interface over that strip
  was not checked.

- Crossbows (`load_type` LOADED): Load picks `adventure_item_interact_load_ranged_weaponst` from the
  list for the player's own tile, Unload moves the bolt out by hand (`unload_weapon`). That a loaded
  bolt is an AMMO item contained in the weapon is a guess (`loaded_ammo`), not read from a game.

- The mouse wheel changing the gait (`gait_wheel`): it writes `unit.enemy.gait_index[WALK or CRAWL]`
  and orders the gaits by `gait_info.full_speed`. That the game takes the gait from that field alone
  is a guess. The date and the speed display's text at the top of the message list (`date_text`,
  `speed_text`, read off the grid from 24 columns after the first button of the row).

- The game's whole top right block (date, weather, small buttons, quest box) taken off the screen
  (`hide_top_right`: the last 32 columns from the top down to three empty rows). The rearranged
  interface kept under DFHack windows (`plain_adventure_under`) and under the game's dialogs over the
  view: the grid is now emptied before the last draw on every frame the view shows, and the corners
  are only changed under a dialog when they hold what they held on the plain screen
  (`hide_top_right`, `bottom_is_plain`). Screens where the view itself goes off (inventory, look,
  travel: `default_adventure_ui` false) are untouched. The settings "Minimap" and "Enemy list",
  the row of foes and the gear (`SettingsButton`) in the corner without the minimap, the minimap's
  arrow from `media/minimap_arrow.png`.

- Quick strike / Aimed strike buttons for a click with Strike (`strike_ways`, stage 2 of
  `AttackDrive`, `shot_choice.kind` 2): Quick clicks the game's "Attack" entry
  (`adventure_movement_attack_creaturest`), Aimed its "Combat" entry. That "Attack" strikes at once
  without a further question is an assumption.

- The third button, Dodge (`shot_choice.dodge`, `struck_at`, `attack.dodge`): shown when another
  creature has an Attack action aimed at you with `timer1` > 0; it clicks "Combat" and then the row
  starting "Dodge" of the game's list of moves. That the game offers dodging exactly then, and what
  that row is called, are guesses.

- A strike of yours that has not landed is waited out (`strike_step`, `striking`, up to 40 presses
  of `A_SHORT_WAIT`). This answers the owner's "sometimes nothing happens until I pass a turn"; the
  cause was not reproduced, so it is a guess that this is it. It also takes away the chance to
  start a second strike while the first is on its way.

- Setting `auto_attack` (off by default; `step_not_strike`): a move key towards a creature in a
  conflict with you (`fpv::conflictWithYou`) opens the game's list for that tile and clicks its
  `adventure_movement_movest` entry. Whether the game offers that entry for an occupied tile is
  not known; without it a note is flashed and nothing happens. Creatures the game would strike
  without a conflict yet (wild animals, say) are not caught.

- Riding (`fpv::mountOf`: `flags1.rider` and `relationship_ids[RiderMount]`; `Scene::mount`, `draw_mount`,
  `EYE_RIDING`), 2026-11-14, built only, not seen in the game: the mount is left out of the billboards (so
  it cannot be picked or targeted as the creature on your tile), the eye is raised by a table top (0.37),
  and the top quarter of its picture's opaque box is drawn at the bottom of the frame, centred, at most
  22% of the view high and 80% wide, before the marks of a fight and the held items. Its picture: its own,
  else `unit_look_` (what the game drew for it alone), else its kind's raw one.
- A creature on your own tile (`beside` in `feed`): drawn in a ring 0.45 tiles around you (`self_key`
  in `buildUnits`), so it is drawn and picked when in front of you. An attack click on no creature targets it (a foe, or any creature when the click
  is on your own tile) through `right_click_tile(adv->pos)`. A click with Strike, Wrestle or Shoot
  on an empty place is dropped instead of reaching the game as a plain click. That the plain click was
  why such a strike had no swing and no sound, and that the game's list for your own tile offers
  "Combat ..." for a creature there, are guesses.

- The intent system (R cycles Info, Interact, Walk, then the attack intents; Ctrl no longer used),
  2026-11-13, tested in the game with the test pointer: R cycles and the mark and label change; Info
  opened a goblin's sheet and a well's sheet; Interact started a conversation with a goblin and closed
  the list on a well with "Nothing to use there"; Walk walked to a clicked tile; Strike at two tiles
  said "Too far to strike"; Strike next to the goblin brought up Quick/Aimed, a click on the goblin
  between them was a quick strike that landed; clicks fired every 10 frames while the strike was on its
  way were kept and one was carried out after it landed. Not seen: pulling a lever or any other use
  (no lever in the test save), the step onto an adjacent creature's tile with Walk, the new camera
  glide with real walking (only its code), Info on an item lying on the ground, real input.
- Attack clicks snap to a creature (`creature_near_click`): with nothing under the pointer, picks in rings
  up to 5 x (window height / 180) pixels around it, else a creature on the clicked tile. Tested
  2026-11-13: 15 px beside a goblin it snapped, 60 px away it did not, beside an adjacent one it
  brought up Quick/Aimed strike.

- The sky and weather (`sky.h/.cpp`, `read_weather`, `pick_weather`, `follow_weather` in `fpview.cpp`),
  2026-11-13, built only, not seen in the game. The software renderer asks `Sky::column` for the
  empty rows of each column (one call a column; the sun and moon are only tested in columns whose
  upright plane passes near them; each cloud layer's fade and smaller copy come from a table by
  height). The graphics card renderer gets the same sky as triangles (`Sky::mesh`): a dome coloured at
  its corners, star, moon and sun pictures, a fan for the sun's glow, cloud tiles on a ceiling at
  height 1 (out from overhead, tiles of 2x2, 4x4, 8x8 pictures in the smaller copies, on nested grids
  so the levels meet without gaps) and a fog dome; all pictures are in `SkyAtlas`, a texture of its
  own sent to the card only when the media change. It is drawn after the haze quad and before the
  world (`bg_end`). Rain and snow (`Precip`) are drops in a box around the eye; a drop shows only on a
  tile with `CELL_SKY` and with a clear line to the eye. The software renderer paints them into its
  frame (`Precip::draw`), the graphics card renderer draws them as quads after the world
  (`Precip::mesh`), so the overlay texture is not sent again each frame while it rains.
  Measured offline at 1920 x 1080, one thread: per-pixel sky 41-120 ms a frame (spread over the
  workers still 5-15 ms; that was the first version for the graphics card renderer), the triangles
  0.06-0.28 ms (600-3400 triangles), drops 0.2 ms; the software renderer's columns 15 ns a pixel for
  a clear sky, 36 in a storm, 74 with every kind of cloud at night. A test program drew the triangles
  with a small rasteriser beside the per-pixel sky for every preset: they match. Read from the game: `cur_year_tick` (hour, day), `world_data.moon_angle`
  (taken as the angle from the sun in hundredths of a degree, 19140 next to `moon_phase` 15 at the
  title screen; `moon_phase` / 28 otherwise), `flip_longitude` (sunrise side), `flip_latitude` (which
  way the sun passes), the region's `clouds` bits and `wind`, `current_weather` for the tile's region
  (as `Maps::getCurrentWeather` indexes it), and `region_weather`: `FallingMaterial` on the tile's world
  tile is taken as a rain of that material, other kinds within 48 tiles of `world_in_game_x/y` as an
  evil cloud nearby. Guesses: what the moon numbers mean, that evil rain falls whenever its entry
  exists, the wind bits' direction, the sun's height by season. The weather picker for testing
  (`debug_weather`) was removed for the release (2026-11-16). Neither the sky nor the drops have been seen in the game.

- Lightning, thunder and evil clouds (2026-11-15), built only, not seen or heard in the game. A storm is
  `is_storm`: rain > 0.3 under nimbus >= 0.75 (plain rain gives nimbus 0.6 in `read_weather`, the game's
  storm clouds 0.8 or 1). With the adventurer's tile `outside` (`Target::outside`), `lightning_step`
  (render thread) flashes every 30-90 s (an additive white fill over the view, under the interface) and
  plays `media/thunder.wav` 2-5 s later. Sound (`sound.h/.cpp`): the game plays its own through FMOD; the
  thunder goes through SDL's audio (functions looked up in the game's SDL2.dll, `SoundApi`), the audio
  subsystem started by us on first use, a device of our own fed with `SDL_QueueAudio`, on a worker
  thread (`std::async`; waited for in `plugin_shutdown`). Volume `init.media.volume_master` x
  `volume_ambience_adv` (0..255 each, as the game's init.txt says), none with `SOUND_OFF`. `fpview status`
  shows the last sound result. That SDL audio and FMOD share the output without trouble is an assumption.
  Creeping gas, dust or vapour within 48 tiles now raises stratus to `2 x evil` and `Sky::prepare` mixes
  `cloud_light_` towards half the material's colour (fully from `evil` 1/3), so every cloud layer is dark
  and of its colour, besides the haze tint.
- Weather no longer shortens what is drawn (owner's request 2026-11-15): before, `Sky::sightLimit` (10-40
  tiles in fog, rain, snow, an evil cloud) cut `view_distance` and `fog_distance`, so the far land and
  everything past that vanished. Now both stay as in clear weather and `haze_at` (render.h) adds the
  weather's haze, `RenderOptions::weather_haze` (Sky::weatherHaze: mist 0.52, fog 0.73, thick fog 0.95,
  rain 0.45, snow 0.6, evil cloud 0.7, combined) reached over `weather_reach` (the old sight limit) tiles.
  The GPU renderer's haze table (`fog_a`) is rebuilt each frame from it. Built only.
- The picture minimap went black for a frame when the game skipped drawing its map (we blank it each
  frame, so `minimap_take` copied an empty map). `minimap_take` now keeps the last copy when the
  adventurer's own tile is undrawn. Built only.

- Bands on built walls (`Cell::trim`, `CELL_FOOT`, `CELL_HEAD`, `TRIM_H`), 2026-11-14, built only: a
  constructed wall gets a plain band (`Pattern::Grit` in the wall's colour) at its foot when the tile
  below is not a built wall and at its head when the tile above is not; read from the map in
  `readLevel`. Drawn by the software renderer (per pixel in the wall loop), the graphics card renderer
  (the face split into three strips) and `pick_ray`.

- Snow on shrubs, saplings and boulders (`TextureStore::snowCapped`, `Scene::snowySprite`, `Spatters::snowLevel`),
  2026-11-14, built only: with snow lying on the tile, the sprite is painted in snow down to
  an uneven line (deeper with more snow, 4 variants of the line by tile) and on upward-facing edges
  in its upper part. Cached per picture, amount and variant; both renderers use the same sprite.

- Spatters as layers (`Spatters::layers`), 2026-11-14, built only: every kind on a tile is drawn,
  mud and dust under snow under blood and the rest (the game draws only the largest). In view the
  game's picture stands in for its own kind when partly covered (a full cover is joined by
  `pictureAt`, which counts snow under mud as snow). Slopes take their spatters from the map only, falling
  back to the `RAMP_TOP` tile above; that the game's spatter layer is what left slopes bare, and that
  it may keep their snow on the tile above, are guesses.
  Snow over mud or dust has holes (`TextureStore::overHoled`, `Spatters::holesAt`): smooth noise over
  world pixels decides where, the hole density is set per tile corner from the mud on the four tiles
  meeting there, so holes run on and thin out across tile borders. Full covers join up across slopes
  (`levelBeside`): a slope and the `RAMP_TOP` over it count as one surface, and a slope joins the top
  of a wall beside it.

- Slope shapes (`ramp_corners`), 2026-11-14, built only: a wall whose top can be stepped onto
  lifts a corner (`holds_slope`: not tree material, the tile above not a wall or canopy); with none of
  those, any wall beside it does; a slope with no wall beside it lies flat. A side up at one end only with open ground beyond it (`open_beside`) is brought
  down: one such side drops its raised corner, two opposite ones make a ridge (`RAMP_RIDGE` with the
  `RAMP_VALLEY_U/V` bit naming its line). A snowed slope's sides use the top's texture. The channel
  (`RAMP_VALLEY_U/V` without the ridge) is dug only when the tile beyond the raised edge is open ground
  (`open_beside`); before, any tile that was not a climbable wall counted, which cut a V into every other
  slope in the notches along a cliff (seen by the owner in water, 2026-11-14).

- Water (`Cell::water`, `Cell::wet`, `CELL_UNDER`, `CELL_WAVES`, `water_in` in `readLevel`,
  `TextureStore::water`), 2026-11-14, built only, not seen in the game. Before, water was a `c.top` at 12/16
  of a level and only on tiles with a floor, so the sea was drawn at the seabed's height (bumpy, and below
  the far sea: the band of nothing between them the owner saw). Now every non-solid tile with water gets a
  see-through block `flow_size`/7 deep; only the top of a body of water has a surface (`CELL_UNDER`: more
  water above, or 7/7 under a floor or rock). Picture: the game's LIQUIDS page (column 0 water, 1 magma, row
  7 - depth; read from `data/vanilla/vanilla_environment/graphics`), its rim pieces (columns 4-6, rows 0-2,
  magma 3-5) towards dry ground, an ordered 4 x 4 dither over 7 pixels towards dry ground, alpha baked into
  the pixels (`WATER_OPACITY` 0.85 times the picture's own alpha; magma 1; the owner asked for 75% opaque,
  then 85%). Waves: the game's
  ocean waves are flows (`map_block::flows`, `flow_info` of type `OceanWave` or `SeaFoam`, sent towards
  the coast by `world.event.ocean_wave_makers`), not the water's current; the graphics files have no
  picture named for them (FLOWS..FLOWS4 hold only `BROOK_TO_*` / `RIVER_TO_*` streaks). On a tile with
  such a flow the picture the game drew in the viewport's `screentexpos_high_flow` is laid over the water
  (or over dry ground the wave has run up onto); that this layer is where the game draws them is a guess,
  `fpview status` counts wave tiles read and how many had a picture there. Without one: the FLOWS streaks
  along the wave's way (`dest - pos`; rows NW NE SE SW N S W E), the page by density (a guess), 16 frames
  as 16 consecutive texture ids shown at 10 a second (`RenderOptions::wave_frame`). The first version took
  waves from `map_block::liquid_flow` (sloshing, currents) and showed none on the coast. Brook tops (`BROOK_TOP`) are left to their floor picture.
  Software renderer: water does not fill rows; its colour goes into `veil_` (nearest water per pixel, with
  its distance) and is blended over the column once everything else is drawn; a slope pixel nearer than the
  water on its row cancels it (the part standing out of the water); picking goes through water (the owner wants to aim at the bottom and at swimming creatures).
  Billboards and boxes under water are not tinted there. Graphics card renderer: texture modes 3 and 4 (own
  alpha, white with own alpha), drawn in the tile after the floor and the slope pieces under the surface
  (slopes in water in 8 x 8 pieces; those wholly above it come after), before boxes and billboards; sides
  where the neighbour's water (or ground) is lower. `pick_ray` does not hit water. The far land: map cells at
  the water's real height, the sea outside the map at the map's sea surface (`World::sea` is no longer a
  whole level), coloured as `mix(ground under, WATER_DEEP, 0.25)` (mean over the map: `sea_col`, `lake_col`),
  `FarLevel::wet` instead of comparing colours. With it, slopes: where no climbable wall touches a slope,
  any wall does (`holds_slope(..., any)`), so a slope at the foot of a cliff it cannot lead up rises towards
  the cliff instead of lying flat (seen flat by the owner in water at a cliff's foot, 2026-11-14).

- Smoothed and engraved walls, 2026-11-14, built only: `Pattern::Smooth` used to draw a 16-pixel grid
  of slabs, which put a tile grid on every smoothed natural wall and floor; it is now plain, and the grid
  is `Pattern::Slabs`, used only for built stone floors. Engraved walls (`wall_engraving_picture` in
  `scene.cpp`, `TextureStore::engraved`) are looked up in `world.event.engravings` (an engraved wall is a
  `SMOOTH` tile like any other) and resolved through `world.art_image_chunks` to the image's main
  element's sprite, carved in relief on every face of the wall. Guesses: that a wall engraving's `pos`
  is the wall tile itself, which of `plant_raw.texpos` is a usable plant picture (the first set one is
  taken), and that a chunk not in `art_image_chunks.all` is simply not loaded (the text-mode character
  `engraving.tile` stands in).

- Models from OBJ files (`media/models`, `models.cpp`), 2026-11-14, built only, not seen in the game:
  the 19 shapes that were hard-coded in `Scene::boxSet` are now `.obj` files; an offline test read
  them back to the old numbers (boxes, prisms of the stair, picture sides, colours, `when`). Live
  reload, PNG textures (`TextureStore::image`) and an actual Blender export were not tried. Turning is
  now a real rotation: a sign or cabinet on a west wall was mirrored before, so a sign there now hangs
  at the other corner of its tile.

- Lines along model edges (`Box::edge`, `Box::sharp`, `EDGE_DARK`, setting `model_lines` in game
  pixels, default 1), 2026-11-14, built only, not seen in the game. Software renderer: a face pixel
  within `edge` of its face's edge is darkened before shading. Graphics card renderer: see-through
  black strips (`edgePoly`, `vEdges`, `hEdges`, the inset band in `roundBox`) laid over each drawn
  face, their alpha lowered by the haze at their middle. No line on faces carrying the game's
  picture (cut-out signs would get a frame over their empty parts). Not compared between the two
  renderers.

- Uneven borders between kinds of natural ground (`ground_class` in `scene.cpp`, `Level::ground_class`,
  `class_look_`, `TextureStore::fringed`), 2026-11-14, built only, not seen in the game: a natural floor
  (grass 3 > soil/sand/ash 2 > stone/ice 1, ties broken by kind) takes the texture of a stronger class on
  any of its 8 neighbours along a noise line 1-11 px deep (4 px at the tile's corners, so neighbours join),
  with loose tufts and a darker lip; corners only where neither side beside them has a line. The texture
  that reaches in is one look per class (the first seen in the game's picture), not the neighbour's own
  picture, which keeps the number of made textures small; it is baked into the floor texture before
  spatters, so both renderers draw it with no cost per frame. Each side picks one of two lines by where
  it lies. How many textures a busy patchy field makes (atlas room in the graphics card renderer) was
  not measured.

- Creatures without a picture of their own (animals) on a shared tile, 2026-11-14, built only: the game
  shows the creatures of a shared tile (and its status bubbles) in turn, so each is drawn from what the
  game drew on its tile when that picture is one of its kind's raw pictures (`is_kind_picture`,
  remembered in `unit_look_`), else from its caste's raw picture (`raw_pieces`). A creature the game skips
  for a frame is held for 150 frames. That the flashing came from frames with no creature drawn is a guess.

- The vitals bars (`Vital`, `read_vitals`, `order_passing`, `body_layout`, `paint_vital`, `draw_passing`,
  `vital_hint` in `menu.inc`; setting `status_bars`), 2026-11-14: six bars under the doll (which is
  drawn higher, the panel growing upward), passing states stacked above it sliding and fading
  (`SDL_SetTextureAlphaMod` is bound only if present), the hover line right of the panel. The owner saw
  the first version (names written on the bars) and asked for icons instead: each bar has its icon on
  the left, as high as the bar, from `media/vitals/NAME.png` (`Media::vital_icon`), else from the rows in
  `plugin/vital_icons.inc`, which `tools/make_media.py` also draws the PNGs from. The icon version is
  built only. DFHack's logo button (`hotkeys.adventuremenu`, normally over the portrait) is moved to the
  top of the minimap's button column while the view is on (`place_dfhack_logo` in `first-person.lua`,
  run from `CameraButton:render`; only the widget's frame, not DFHack's overlay config); the gear is
  now the first of those buttons. The game's status icons above the portrait are taken off by blanking a few rows over the panel
  on the plain screen (`hide_portrait(true)`); where exactly the game draws them was not checked.
  Unconscious also counts sleep, so sleeping may show as "Unconscious".

- The game's auto-move question taken off the screen (`hide_action_prompt`), 2026-11-14, built only: read
  from the game's code (53.17), it draws "Continue action / Stop action / Finish action" ("Finish anyway",
  "Finishing up...") in place of the action bar while the adventurer or its mount walks a path
  (`unit.dungeon_control`: PROMPT asks, CONTINUE goes on until a distraction such as a conflict nearby,
  CONTINUE_IGNORE_DISTRACTIONS finishes). The buttons are found by their words and blanked; the question is
  not answered, so an interrupted walk to a clicked tile stops there. The game draws no action bar then, so
  the bar is shown as it last was (`bar_frozen`, the cache in `move_bottom_buttons`) and the message panel
  keeps its width; the pointer is not handed to the game there and clicks on it are dropped.
- The message panel's text (lines, date, speed) in the game's own font (`game_glyphs`, `panel_text` in
  `menu.inc`, `Canvas::text(GlyphFont...)`, `TextureStore::readNative`), 2026-11-14, built only: the glyph
  pictures at their own size with colour and alpha, magenta see-through, scaled nearest to the cell and
  multiplied by the text colour. Of the small and large font, the largest not taller than a cell. The
  paperdoll and attack menus use it too (since 2026-11-14 evening); the inventory panel still uses the 1-bit `ui_font`.
- Button captions over the moved buttons (`cut_caption`), 2026-11-14, built only: the game draws the caption
  of the button under the pointer at the button's original place, 28 columns from
  `main_interface.current_hover_left_x` up from `current_hover_bot_y` (text lines + 1 rows, + 2 with a hotkey;
  read from the game's code). It is taken off the grid whole (both layers) before the blocks are looked for,
  and put back moved with the block the pointer is on. Before, a part of it was lifted and its bottom row
  went along with the bar. DFHack's overlay widgets are drawn after our capture (overlay's render hook has
  priority 100), so `dungeon_top_hook` (render, priority 200) puts the caption back over them once they are
  drawn. It also draws the captions of the mod's travel and sleep buttons (`paint_widget_hint`): the widget
  sets `fpview_widget_hint` (1 travel, 2 sleep) and `_x`, `_y` (that button's top left cell); the box is
  the game's own (`paint_game_caption`: the frame pictures are texture ids at gps + 0x1924, read from the
  game's code), the words `hover_instruction[ADVENTURE_TRAVEL / ADVENTURE_SLEEP]`, the hotkey A_TRAVEL /
  A_SLEEP.

- Fixes of 2026-11-14 (evening), built and installed, none seen in the game. Minimap blinking while walking:
  frames whose map read is skipped (`first_draw_moved`, or a draw that changed nothing) took no minimap copy
  and `mini_n` went to 0; the last copy is now put back for up to `MINI_KEEP` frames. Bottom bar blinking:
  taken to be frames where the game drew no bar or buttons (between turns); the last moved bar and buttons are
  shown for up to 90 frames (`bar_missing`, `buttons_missing` in `move_bottom_buttons`), a guess at the cause.
  Black screen when a new part of the world loads: `update_all` stopped drawing the view when the adventure
  screen had not drawn for 700 ms, which a long game tick does; the game's tick (`logic`) now counts as the
  screen being alive (`in_game_tick`, `last_tick_ms`); that the load happens inside `logic` is a guess. The
  minimap over the game's windows (naming an item): `hide_interface_under_minimap(plain)` only empties the
  corner when its cells are the plain screen's, else sets `corner_covered`: no minimap, and the Lua global
  `fpview_corner_covered` hides its buttons and the gear. Mouse wheel while riding sets the mount's
  `enemy.gait_index` and `status.command_gait_index` (with only the first, the game put the old gait back
  after a step; read in the game: both were 1 on a horse). The paperdoll and attack menus
  (`paint_menu`, also the body part list after an aimed shot) write their text in the game's font
  (`panel_text`) instead of the 1-bit `ui_font`. Second round (same night), after the owner still saw the minimap go black, "View your companions" and
  the tile tip in the top left blink while walking: the render thread drew the minimap straight from the
  game's map viewport, which the game rewrites while it draws (other levels, then blanked). The minimap is now
  drawn from a copy of its square (`MiniShot`, made in `minimap_take`) through a viewport of our own
  (`minimap_viewport`: the game's struct copied, size n, our arrays, the `_old` arrays emptied each time).
  The companions button (columns 12..26 of the row above the bottom buttons, before DFHack draws its widgets)
  and the tile tip (`move_tip_to_corner`) are kept as they last were for frames where the game draws none
  (90 and 30 frames). Right click or Esc on the shot buttons: the game closes its list, then `leave_aim_step` sends
  LEAVESCREEN to close its aiming screen as well, once the list is gone.

- Floating words and driftwood (2026-11-15), built only, not seen in the game. Words over a struck creature
  (`say` in `track_combat`) wait in `fx_unsaid` until the fight is told (`told`: a report that reached
  `world->status.announcements` in the last `TOLD_KEEP` s is among the last 8 ids of one of the creature's
  `reports.log`, or a `COMBAT_` report's `pos`/`pos2` is on its tile or next to it); after `TOLD_WAIT` they are
  dropped. That the game logs a fight in the creature's report logs even when it does not announce it, and
  that the report is in the list within a second of the wound, are assumptions. Each body part a new wound
  reaches gets its own line (`wound_word(w, bp, first)`), `TEXT_GAP` apart; each later word pushes the earlier
  ones up a line (the old fixed `stack` offset is gone). Driftwood (`tiletype_material::DRIFTWOOD`, a FLOOR
  tiletype whose material is the place's soil): its picture (`is_driftwood_picture`, the DRIFTWOOD page) is a
  `BillKind::Item` billboard, the ground is taken as `SoilFloor1` of that soil. Which of the game's two floor
  layers holds the driftwood picture was not checked (both are handled).

Known guesses and loose ends:

- Furniture detection assumes site furniture is a building of the listed types; proportions are
  estimates; beds face north and slabs face north/south regardless of the room.
- The portrait's place is taken as the bottom-left 12 x 8 interface cells.
- The click log (`MouseTrace`, `mouselog`) was removed for the release (2026-11-16); the console test
  aids (`pointer`, `find`, `pick`, `debug` switches, `hitches` ...) stay, players never see them.
- Sliding the map during `logic` (for the game's tip) is new; `game_tips false` turns it off if it
  has side effects.
- Throwing at a creature may only be offered as throwing at its tile.

## Conventions

- Keep `README.md` in step with behaviour and settings; add new settings to `config.lua`
  (`SCHEMA` and `ORDER`) and to the plugin's `set_setting`.
- Everything the mod draws that is not the game's own goes in `media` with a built-in fallback, and
  is documented in `media/README.txt` (edit it by hand: the script only writes it when missing).
- Comments say why, in plain words. No decorative or change-log comments.
- Large edits to `fpview.cpp` were made with small Python patch scripts that assert each anchor
  occurs exactly once; that pattern works well on a 3000-line file.
  After running one, grep the file for a line it added: on 2026-11-14 a script that edited a copy of
  the text inside a helper wrote the unchanged original back, printed "ok", and the build passed, so a
  whole change was missing from what the owner tested.
