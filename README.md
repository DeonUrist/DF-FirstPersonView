# First Person View

A DFHack mod for Dwarf Fortress (Steam, v53+) that shows adventure mode in first person.

Press **Ctrl-Shift-F** while controlling an adventurer and the map area becomes a 3D view of
your surroundings, drawn with the sprites the game itself uses for the tiles, plants, items,
buildings and creatures around you. Several z-levels are drawn at once, so hills, pits,
multi-storey buildings and tree canopies show up. The rest of the adventure interface (status,
menus, announcements, conversations) keeps working on top of the view. Press the hotkey again
to get the normal map back.

It is a script-only mod: no raws, nothing is written to your saves, and it does not have to be
enabled for a world.

## Install

Requires DFHack (tested with DF 53.17 / DFHack 53.17-r1 on Windows).

Run the installer from this folder:

```powershell
.\install.ps1
```

It copies the mod to `%APPDATA%\Bay 12 Games\Dwarf Fortress\mods\first_person_view`, which is
where the Steam version and DFHack look for mods. For a portable install pass the mods folder
yourself: `.\install.ps1 -ModsDir "<DF folder>\mods"`. Then restart Dwarf Fortress.

For development, `.\install.ps1 -Link` creates a junction to this repository instead of
copying, so edits to the scripts are used the next time the command runs.

## Use

| | |
|---|---|
| **Ctrl-Shift-F** | toggle the first-person view (adventure mode only) |
| Up / Down | walk forward / backward |
| Left / Right | turn 45 degrees |
| Diagonal keys | step diagonally without turning |

Everything else behaves as usual. While a menu that needs the map is open (look, aim, travel)
the normal top-down map is shown, and the view comes back when the menu closes.

The same things are available as a DFHack command:

```
first-person                  toggle
first-person on | off
first-person status           state, frame timing and all settings
first-person hotkey Alt-V     change the hotkey (remembered)
first-person set <setting> <value>
```

## Settings

Change with `first-person set <setting> <value>`. They are stored in
`<DF>/dfhack-config/mods/first_person_view/settings.json`.

| Setting | Default | Meaning |
|---|---|---|
| `hotkey` | `Ctrl-Shift-F` | DFHack keybinding that toggles the view |
| `fov` | `80` | horizontal field of view in degrees |
| `quality` | `16` | rendered pixels per map tile; lower is faster and blockier |
| `max_width` | `800` | upper limit for the rendered image width in pixels |
| `view_distance` | `18` | distance in tiles at which things fade into haze |
| `levels_up` | `4` | z-levels drawn above you |
| `levels_down` | `3` | z-levels drawn below you |
| `relative_controls` | `true` | movement keys follow the view; `false` keeps compass movement and turns the view to face where you last moved |
| `smooth` | `true` | animate turning, walking and climbing |
| `fast_motion` | `true` | draw at half resolution while the view is moving |
| `compass` | `true` | heading indicator at the top of the screen |
| `start_enabled` | `false` | switch the view on automatically when an adventure loads |

If the view feels slow, lower `quality` (12 or 8) first, then `levels_up`.

## How it works

- **Sprites.** The game stores, per map tile, which sprite it drew in each layer (ground, wall
  or plant, items, creatures, buildings). The mod reads those layers right after the game fills
  them, and reads the pixels of each sprite straight from the game's texture memory.
- **Other z-levels.** The layers only hold one level, so the mod briefly points the map at
  each neighbouring level and has the game draw it, exactly as scrolling levels with the mouse
  wheel does, then puts the view back. One extra level is refreshed per update, all of them
  when you change level.
- **Geometry.** Tile shapes come from the map: wall tiles are blocks, ramps are half blocks,
  floors and leaves are thin surfaces, water sits three quarters up its tile. Top-down wall
  sprites are only block edging, so wall faces are built by tiling that edging.
- **Drawing.** A ray-caster written in Lua walks the map for every screen column and draws
  wall faces, floors, ceilings and finally billboards for everything standing on a tile.
- **Display.** The image is copied into a grid of small textures placed on the top layer of
  the map view; after each frame the renderer is told to refresh them.

Only what your adventurer has seen is drawn, because that is all the game draws.

## Limitations

- Everything is drawn on the CPU by a Lua script. Expect roughly 20 to 50 ms per redrawn frame
  at the default quality; the image is only redrawn when something changes.
- Mouse clicks on the 3D image are still interpreted by the game as clicks on the top-down
  map underneath, so use the keyboard for movement and interaction while the view is on.
- You cannot look up or down yet.
- Trunks and ramps are blocks, so trees are square and hillsides are stepped.
- Creatures and items are flat sprites that always face you.
- Night is not darkened, apart from the sky.
- The view is an overlay widget (`first-person.view`); if you disable it in DFHack's overlay
  settings, the hotkey does nothing.

## Layout

```
info.txt                                   mod metadata
install.ps1                                installer
scripts_modinstalled/
  first-person.lua                         command, hotkey, overlay, camera and controls
  internal/first-person/
    config.lua                             settings
    scene.lua                              reads map levels into a scene
    sprites.lua                            turns game sprites into textures
    raycaster.lua                          the renderer
    framebuffer.lua                        puts the image on screen
```
