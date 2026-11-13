# First Person View: instructions for agents

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
- Installed plugin: `<game>\hack\plugins\fpview.plug.dll`
- Saves: `%APPDATA%\Bay 12 Games\Dwarf Fortress\save`
- Toolchain (git-ignored): `_refs\dfhack` (DFHack source at the matching tag), `_refs\tools`
  (portable Perl, pip CMake). See the header of `plugin\build.ps1`.
- The plugin's remembered intent: `<game>\dfhack-config\fpview-intent.txt`. Settings:
  `<game>\dfhack-config\mods\first_person_view\settings.json`.

## Build and install

```powershell
plugin\build.ps1            # compile only (incremental, seconds)
plugin\build.ps1 -Install   # compile and copy the DLL into the game
install.ps1                 # copy scripts + media into the mods folder (and the DLL if it can)
```

- The DLL is locked while the plugin is loaded. Installing needs the game closed, or the game at the
  **title menu** with `dfhack-run unload fpview` first and `dfhack-run load fpview` after.
- **Never unload or load the plugin while an adventure is loaded: the game freezes.**
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
  `fpview face N|NE|...`, `fpview camera <x> <y> <z>|off`, `fpview debug ctrl|look|noblank|center
  on|off`, `fpview intent [next|strike|shoot|throw|wrestle]`, `fpview menu` (contents and pixel
  positions of the paperdoll/attack menu), `fpview hint`, `fpview mouselog` (what recent clicks and
  the Ctrl-click driver did), `fpview tilt <degrees>|off` and `fpview yaw <degrees>|off` (look up
  or down and in any direction, as the mouse would). Always end with `fpview pointer off` and
  `fpview debug ctrl off`. `fpview status` shows which renderer drew the last frame, its triangle
  count and where its time went.
- **What the test pointer cannot do**: real mouse look (it needs window focus and the real mouse),
  real key state (Ctrl+R, Shift+Space), and the main-thread races described below. Several bugs only
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
  updates the paperdoll menus (`update_menu`) and runs the Ctrl-click driver (`drive_attack`).
- `feed`: input. Menu clicks, Ctrl-click, WASD and relative movement, Space waiting, and
  `PointerRedirect`/`MapForClick`, which make a click act on the tile under the pointer in the 3D
  view.
- `logic`: slides the hidden map so the game's own hover box describes the looked-at tile.
- `renderer_2d_base::update_all` (`draw_view`): camera animation, mouse look, ray-cast the frame
  into a texture drawn under the interface, then minimap, compass, player doll, menus, crosshair.
  With `new_renderer` the world is drawn by `gpu.cpp` instead and the frame texture becomes a
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
- `gpu.h/.cpp`: the renderer on the graphics card (setting `new_renderer`). It builds textured
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
- `media.h/.cpp`: loads `media` (via DFHack's `DFIMG_Load`), paperdoll layouts and the doll matcher
  (`bestDoll`), and a small pixel `Canvas` with the game's 8x12 font.
- `menu.inc`: included inside `fpview.cpp`'s anonymous namespace. Intents, media/textures for the
  UI, the paperdoll and attack menus (model, layout shared by drawing and clicking, painting,
  reading the game's dialog), the player's own paperdoll, the compass.

Lua (`scripts_modinstalled/first-person.lua`, `internal/first-person/config.lua`): command line,
settings schema (entries with `plugin=true` are pushed to the plugin with `fpview set`), hotkey,
`media_dir()` handed to the plugin with `fpview media <dir>`, overlay widgets. The plugin tells Lua
its state through the Lua globals `fpview_view_shown` and `fpview_minimap_shown`; the overlay must
not call into the plugin (a suspected deadlock during plugin load).

## Mechanisms worth knowing before touching them

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
  depends on English text layout, not on game structures.
- **Ctrl-click driver** (`AttackDrive`): Strike/Wrestle right-click the creature, pick the game's
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
  `humanoid_neck` for those with one. Do not merge them.
- **Never call `dfhack.textures.deleteHandle` on batches of textures**: it crashed the game.

## State of the work

Confirmed working in the game (at some point; later changes may have affected them): the view
itself, minimap and its buttons, held items, WASD, right-click, Ctrl-click Strike end to end through
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
- Ctrl-click Shoot with real mouse look after the "view no longer snaps" fix.
- Wrestle (the test character's hands were full), paperdolls other than the humanoid ones.
- Blocks inside a tile drawn per pixel (bed sheet over its frame), cabinets against their wall with the
  game's sprite as doors, item icons on tables (from `building_actual.contained_items`, role TEMP) and
  heaps of icons for several items on one ground tile (icons from `world.raws.itemdefs`).
- Valley ramps (`RAMP_VALLEY_U/V`), slab emblem crop (`spriteEmblem`, a colour heuristic), minimap facing
  arrow, hover disc and next-step arrow (`draw_ring`), reach-limited melee outlines, leaves from
  `plant_raw.tree_texture_info` instead of the screen.
- Cabinets and bookcases built from parts and turned to their wall, palm fronds (`SH_FROND_HUB/ARM`,
  `spreadFronds`; a crown is a lone leaf tile on a trunk), coins from the `ITEM_COINS` tile page tinted
  by metal.

Known guesses and loose ends:

- Furniture detection assumes site furniture is a building of the listed types; proportions are
  estimates; beds face north and slabs face north/south regardless of the room.
- The portrait's place is taken as the bottom-left 12 x 8 interface cells.
- Click diagnostics (`MouseTrace`, `mouselog`, `debug` switches) and a no-op
  `swallow_rest_of_click` are still in the build.
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
