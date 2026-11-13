# First Person View

A DFHack mod for Dwarf Fortress (Steam, v53+) that shows adventure mode in first person.

Press **Ctrl-Shift-F** while controlling an adventurer and the map area becomes a 3D view of
your surroundings, drawn with the sprites the game itself uses for the tiles, plants, items,
buildings and creatures around you. Several z-levels are drawn at once, so hills, pits,
multi-storey buildings and tree canopies show up. The rest of the adventure interface (status,
menus, announcements, conversations) keeps working on top of the view. Press the hotkey again
to get the normal map back.

Nothing is written to your saves and the mod does not have to be enabled for a world.

## Controls in the first-person view

| Input | What it does |
|---|---|
| mouse | turns the camera; the pointer stays in the middle under a crosshair (`mouse_look`) |
| **Alt** (held) | frees the pointer (shown as a gauntlet), to reach the game's buttons and panels |
| left / right click | acts on what the crosshair (or pointer) is on, exactly like a click on that tile of the normal map |
| **Ctrl** (held) | the crosshair becomes the aim mark of the current *intent*, and creatures are outlined: red = enemy, white = the game would ask first, green = companion. With Strike or Wrestle only creatures within reach (next to you) are outlined |
| **Ctrl** + **R** | next intent: Strike, Shoot (only with a ranged weapon in hand), Throw, Wrestle. Its name shows for a moment above the crosshair. The choice is remembered, also between sessions |
| **Ctrl** + click | carries out the intent on what the crosshair is on (see below) |
| **w** / **s** | walk forward / backward (`wasd`) |
| **a** / **d** | strafe left / right |
| arrow keys | do what w, a, s, d normally do in adventure mode (up = w, left = a, down = s, right = d), once each letter has been pressed at least once in the session |
| **Space** / **Shift+Space** | let a moment pass, like `,` / a longer one, like `.` (for example while an arrow is being nocked). Space no longer clears announcements while the view is on |
| **camera** button, right of the game's "View your companions" button | switches between the normal and the first-person view |
| **timer** button, right of the camera | lets time pass by itself: every second the game moves on by as many ticks as the number on the button (a wait, `.`, is 10 ticks). The border turns gold while it runs. It stops on a second click, on **Space** (that press does nothing else), when a menu or dialog opens, when you fight or a message the game would pause for appears, or when the view goes off. Only there while the view is on |
| mouse wheel over the timer button | raises (up) or lowers (down) the number, 1 to 99 (`realtime_rate`): by 1 up to 10, then by 5. Works with Alt held too |
| **travel** and **sleep** buttons, right of the speed display ("Walk") | do what the game's map and sleep buttons do (`T`, `Z`), with the game's own pictures for them. The game's buttons in the bottom right corner (centre on yourself, build, travel, sleep) are hidden while the view is on. The bottom is rearranged: the game's row of buttons right of the portrait and the speed display move up beside the timer (one row with "View your companions", the camera, the timer, then travel and sleep), and the game's action bar from the middle of the bottom moves into the bottom row, right of the portrait |
| message list, bottom right corner | every message of the adventure in the game's colours, newest at the bottom, on a see-through background; it scrolls to a new one when it comes in. The mouse wheel over it (Alt held, with mouse look) or a click on its scrollbar (above or below the middle: a page back or on) looks back. Four buttons at its top right: a sword (combat messages), a speech bubble (talk) and a box (everything else) show or hide that kind (yellow border: shown, blue: hidden); the down arrow folds the list away, leaving only the newest message and an up arrow in the bottom right corner that brings it back. It reaches from the right edge to just past the game's action bar, which sits at the bottom left while the view is on. Conversation lines are read there: the game's box for new messages and conversation lines at the top of the screen is not shown (unless the game is waiting for you to page through more than fits) |
| **@**, **+**, **-** next to the minimap | minimap in text characters and back; zoom in; zoom out |

What you hold in your hands is drawn low in the corners of the view. Lying on the ground lowers the camera.

Furniture (tables, chairs, beds, chests, cabinets, coffins, statues, stands, racks, slabs, hatches, wells,
pedestals, supports, altars) is drawn as simple shapes made of blocks, in the colour the game shows it in and with
the game's picture of it on top. A cabinet or bookcase is a plain block with its back to a wall and its
front painted (two doors with handles, or shelves of books) in the colours of the game's picture of it;
out of sight, as one of its kind looked, or in its material's colour. A well has a rim filling its tile, two
posts with a bar across and a rope down its shaft to a bucket. Ceilings seen from below are plain grey
concrete, whatever the floor above is made of. A slab carries the
engraved picture from the game's drawing of it on its flat sides, filling them. A shop or tavern sign (a slab
engraved as one) hangs instead: a thin board cut to the shape of the game's picture of it (see-through where the
picture is empty), on both sides, under a bar sticking out from the wall at about the height of a door top, in
the corner of its tile, also on a slope. A statue is the upper half of the game's picture of it (the figure;
the lower half is the game's pedestal) standing on a low plain podium. Plants, boulders and other ground pictures are left out on a furniture tile. An altar is a long plain block
with a red cloth laid over the middle of its top, hanging a little down the front and back. Tree leaves use
the leaf picture from each kind of tree's own graphics, or the game's standard leaves. Every piece of trunk,
its sloping top included, and the thick branches are blocks of bark; thinner branches and twigs are leaves.
A palm (a bare trunk ending in a sloping top) gets eight long, thin fronds reaching out from the top of its
trunk, about three tiles each, rising a little and then drooping towards their pointed tips. The roots, shadows and leaves the game draws on the ground near trees are not taken as the
ground, and ground not seen yet looks like ground of the same kind that has been seen. Snow, blood, mud,
vomit, water, dust and fallen leaves lie flat on the ground and on slopes with the game's own spatter pictures:
in view exactly as the game draws them, out of view picked from the map the way the game picks them (a few
bits, more, or a full cover that joins up with the covered tiles next to it). What is laid out on a
table or pedestal is shown on its top, trinkets and coins as small pictures and equipment bigger. Several things on
one tile of ground are drawn as a heap of small pictures of up to five of them. Corpses, pieces of corpses and
vermin remains are never hidden in the heap: each one (up to sixteen per tile) is drawn on its own, sized by the
body, and they stand in a small ring with the heap of the other things, like creatures sharing a tile. A creature
lying down is drawn lying: one on two legs is turned on its side, one on four legs or none sinks a quarter into
the ground. A fortification is a low sill with
a block on each corner, so a row of them reads as a battlement from any side. You and other creatures glide
from tile to tile (`smooth`). The mouse can tilt the view far enough down to put your own tile under the
crosshair.

### Intents and the attack menus

A Ctrl-click does the whole lead-up of an attack for you, with the game's own menus doing the work underneath:

- **Strike** and **Wrestle**, on a creature next to you: the game's "Combat ..." entry is chosen, its
  "Really start a conflict?" question is answered and Strike or Wrestle is picked, so you land directly on
  the choice of where to hit. If the game does not offer that move (wrestling with both hands full, say)
  a note says so and the game's own list stays up.
- **Throw**, on a creature or a place at any distance: the game's "What do you want to throw?" list opens;
  once you pick something, the aiming cursor is put on what you clicked and the throw goes off.
- **Shoot**: the same without the list. If the game offers more than one way to take the shot, its list is
  left open for you to pick from.

Where to hit is shown on a **paperdoll** of the target instead of the game's long list: each large body
part carries two coloured letters, the first for how hard it is to hit (**T**ricky, **N**ormal,
**D**ifficult, **E**asy, **E**asier, **I**mpossible), the second for how well a hit would land
(**S**quare, **F**airly solid, **G** can only graze, **Q** can't quite connect, **L** can't land squarely).
The colours are the game's own, and the letters are spelled out at the bottom of the menu. Small parts
(fingers, eyes, teeth ...) are in compact columns next to the doll. Pointing at anything shows the game's
full text for it. The doll is picked by the body parts the creature has; a creature that fits none of them
gets the list alone.

What to attack with follows in the same style: the attacks in a short list and the six options (quick,
heavy, wild, precise, charge, multi-attack) as check boxes in two columns.

Both menus are mouse menus: click an entry, or **Cancel** / Esc / right click to leave. `first-person set
combat_menus false` brings the game's own lists back. They also show when the attack was started from
the keyboard.

### Art you can replace

Everything the mod draws that is not the game's own is in the mod's `media` folder and is read when the
game starts (`first-person reload` reads it again):

| File | What it is |
|---|---|
| `cursor.png` | the pointer while it is free (Alt held, or a menu open); the top left corner is the tip |
| `crosshair.png` | the crosshair; the middle of the picture is the aim point |
| `aim_strike.png`, `aim_shoot.png`, `aim_throw.png`, `aim_wrestle.png` | the crosshair while Ctrl is held, one per intent |
| `icon_ascii.png`, `icon_zoom_in.png`, `icon_zoom_out.png`, `icon_settings.png` | the buttons next to the minimap, 16 x 12 pixels (the gear opens the mod's settings) |
| `button_camera.png` | the camera button, the whole button with its border, exactly 32 x 36 pixels |
| `button_timer.png`, `button_timer_on.png` | the timer button stopped and running, exactly 56 x 36 pixels; the number is written over pixels 32-47 across, 12-23 down |
| `button_travel.png`, `button_sleep.png` | the travel (map) and sleep buttons, exactly 32 x 36 pixels each; only used when the game has no pictures of its own for them (no graphics) |
| `checkbox_on.png`, `checkbox_off.png` | the boxes of the attack options |
| `dolls/NAME.png` + `dolls/NAME.txt` | a paperdoll and where each body part is on it |

A doll's text file has one line per place, `body part name = x, y` in pixels of the picture, with `|`
between alternative names (`right foot | right hoof = 33, 125`). The names are the ones in the game's
"What do you want to aim for?" list. Add a picture and a text file for a body shape that is missing and it
is used wherever it fits best. Paperdolls are made by hand.

Additional settings: `minimap_ascii` (false), `minimap_tiles` (11, tiles across the minimap, 5 to 61), `wasd` (true), `mouse_look` (true), `combat_menus` (true), `realtime_rate` (5, game ticks a second while the timer button runs, 1 to 99).

The view also works in the object testing arena, after "Assume Control" of a creature.

Walls, trees and ground that has not been seen yet are drawn with built-in patterns (blocks, rough stone, soil, planks, bark, leaves, grass) tinted in the colour of their material; the colour is taken from the game's own sprite the first time a material is in view. Tree trunks are solid wood, branches and leaves are boxes of foliage. A constructed wall of wood is a full block whose sides look like a row of upright, rounded logs with bark, four to a tile.

When developing: do not `unload`/`load` the plugin while an adventure is loaded. Go to the title menu first.

## Parts

| Part | Where it goes | What it does |
|---|---|---|
| `fpview` plugin (C++) | `<DF>\hack\plugins\fpview.plug.dll` | captures the scene and draws it |
| `first-person` script (Lua) | DF `mods` folder | hotkey and saved settings |

The plugin is native code, so it has to be built for the exact DFHack version you run and
rebuilt when DFHack updates. It is not something the Steam Workshop can deliver.

## Build and install

Tested with DF 53.17 / DFHack 53.17-r1 on Windows.

One-time setup, all inside the ignored `_refs` folder:

```powershell
git clone --depth 1 --branch 53.17-r1 --recurse-submodules --shallow-submodules https://github.com/DFHack/dfhack _refs\dfhack
python -m pip install --target _refs\tools\pycmake cmake
python -m pip install ninja
# unpack a portable Strawberry Perl (it includes XML::LibXML and XML::LibXSLT) into _refs\tools\perl
```

You also need Visual Studio with the MSVC x64 compiler and a Windows SDK. Then:

```powershell
.\plugin\build.ps1      # builds the DFHack core once (several minutes), then the plugin
.\install.ps1           # copies the script mod and the plugin into the game
```

`build.ps1` makes three local changes to the DFHack source tree so it builds with compilers
newer than VS 2022; they are described at the top of that script. `install.ps1 -Link` links
the script mod to this repository instead of copying it.

Testing aids in the DFHack console: `fpview pick` reports what the pointer is on, `fpview
pointer <x> <y>` / `fpview pointer off` stands in for the mouse at a window pixel, and `fpview
find` aims that stand-in pointer at a creature in view, `fpview hint` prints the terrain hover
text. `fpview camera <x> <y> <z>` looks from
another map tile (`fpview camera off` returns to the adventurer) and `fpview face <N|NE|...>`
turns the view.

While the game is running you can swap the plugin without restarting:

```
unload fpview        (DFHack console)
.\plugin\build.ps1 -Install
load fpview
```

## Use

| | |
|---|---|
| **Ctrl-Shift-F** or the **camera** button right of "View your companions" | toggle the first-person view (an adventure, or a creature you control in the arena) |
| Up / Down | walk forward / backward |
| Left / Right | turn 45 degrees |
| Diagonal keys | step diagonally without turning |
| Mouse | hover and click act on what the pointer is on in the 3D view |

The tile or creature under the pointer is drawn brighter and named in one line a little below
the pointer: creatures,
items, plants and buildings by their own name, terrain by the text the game itself gives for
that tile. A click does whatever a click on that tile does on the normal map (walk there, open
the action menu, ...). Over the game's own panels the mouse works as always.

Fighting is shown in the view:

- Each creature has its own status bubble over its head (the game's pictures for stunned,
  unconscious, winded, wounded, lying down, hungry ...), several taking turns.
- When you strike, the weapon in that hand lunges half way to the pointer and back.
- When any other creature strikes, its picture lunges at its target and a white crescent is swept
  between them.
- When something strikes at you, an arrow points from the pointer towards it, turned with your view:
  white when it missed, red when it wounded you.
- Words float up over a creature that is struck: a green "MISS!" when a strike left no wound, a red "DEAD!" when it killed, the
  wound in red as body part and what happened to it ("Head: Bruise", "Left arm: Cut",
  "Finger, Left hand: Cut off", "Broken", "Torn", "Artery opened", "Burn" ...), and in purple what got
  stuck in it ("Lower body: stuck iron arrow"). Your own rise from just above your paperdoll (bottom left).

The game's box describing the looked-at tile is moved into the top left corner. The game's list of
nearby places that was there is shown on the compass instead: each place is a coloured pip on the compass
where it lies (one behind you is held, dimmer, at the nearer end), with two small lines under the compass
saying what it is (the name and kind of the nearest world site that lies the way the game says) and how far
away it is, in tiles. The compass is four times as wide as before and shows most of the circle.

In the top right corner the buttons are replaced by a minimap: the normal top-down map around
you, drawn by the game itself at a smaller size (`first-person set minimap false` brings the
buttons back). A small white arrow next to you on it (half see-through) points at the tile a step forward goes to; it keeps its size
at every zoom, and the zoom goes out to 61 tiles across.

The creature under the pointer gets a faint disc on the ground around its feet in the same colours
as the Ctrl outlines (red enemy, white neutral, green companion); while it walks, an arrow from the
disc points to the tile it steps to next.

Everything else behaves as usual. While a menu that needs the map is open (look, aim, travel)
the normal top-down map is shown, and the view comes back when the menu closes.

```
first-person                  toggle
first-person on | off
first-person status           state, frame timing and all settings
first-person hotkey Alt-V     change the hotkey (remembered)
first-person set <setting> <value>
first-person reload           read the settings and the media folder again
```

## Settings

Stored in `<DF>/dfhack-config/mods/first_person_view/settings.json`.

| Setting | Default | Meaning |
|---|---|---|
| `hotkey` | `Ctrl-Shift-F` | DFHack keybinding that toggles the view |
| `fov` | `100` | horizontal field of view in degrees (40 to 150) |
| `resolution` | `100` | rendered resolution as a percentage of the window |
| `view_distance` | `48` | distance in tiles at which things fade into haze |
| `range` | `2` | how far terrain is tracked: 1 = the map window around you (60x34 tiles at 1080p), 2 = three windows across, 3 = five |
| `levels_up` | `24` | z-levels drawn above you |
| `levels_down` | `8` | z-levels drawn below you |
| `relative_controls` | `true` | movement keys follow the view; `false` keeps compass movement and turns the view to face where you last moved |
| `smooth` | `true` | animate turning, walking and climbing |
| `compass` | `true` | heading indicator at the top of the screen, with the nearby places on it |
| `minimap` | `true` | top-down map in the top right corner instead of the buttons there |
| `new_renderer` | `false` | draw the view on the graphics card instead of in software. The camera then really tilts: you can look straight up (at birds overhead) or straight down without the picture stretching. It goes through the game's own SDL renderer, so it works wherever the game does (Windows or Linux, any maker of graphics card); if the game's SDL cannot do it, the software renderer is used. `resolution` does not apply to it: it always draws at the window's full size. Also switched by the gear next to the minimap |
| `realtime_rate` | `5` | game ticks a second while the timer button lets time pass by itself (1 to 99; a wait is 10; the mouse wheel over the button changes it) |
| `start_enabled` | `false` | switch the view on automatically when an adventure loads |

## How it works

- **Sprites.** The game stores, per map tile, which sprite it drew in each layer (ground, wall
  or plant, items, creatures, buildings). The plugin reads those layers right after the game
  fills them, and reads sprite pixels straight from the game's texture memory.
- **Other levels and distance.** The layers only hold one z-level and one screenful of map,
  so the plugin points the (hidden) map at neighbouring levels and neighbouring map windows and
  has the game draw them, exactly as scrolling the map does, then puts the view back. One such
  window is refreshed per frame in rotation; everything read is remembered. While the view is
  showing, the hidden map is kept at its native zoom and its tiles are not sent to the screen.
- **Geometry.** Tile shapes come from the map: wall tiles are blocks, floors and leaves are
  thin surfaces, water sits three quarters up its tile, fortifications are hip-high walls you
  look over. A ramp is a real slope: each of its corners is up at the next level if a wall
  touches that corner, so it rises towards the wall whose top it leads onto. Where an edge of a ramp is
raised by the walls beside it but has no wall beyond it (a door in a wall line, a fortification, open
ground), the ramp dips in the middle as a channel towards that side, so what lies past it can be seen. Closed doors fill
  their tile; an open door is drawn with the game's open-door sprite, which is the frame with
  an opening you see through. Top-down wall sprites are only block edging, so wall faces are
  built by tiling that edging.
- **Out of sight.** For tiles the adventurer cannot see at the moment the game draws dark grey
  placeholder icons instead of the real sprites. The plugin recognises those and keeps the
  textures, plants, boulders and door states it saw while the tile was in view. A tile that has
  not been in view yet borrows the texture last seen on the same kind of tile, on the tile next
  to it, or on a tile of the same material, and only shows the placeholder if none is known.
- **Minimap.** A square of the map viewport around the adventurer is copied aside each frame
  and parked in a corner of the otherwise emptied viewport; the renderer then has the game draw
  exactly those tiles into an off-screen texture, which is copied into the corner scaled down.
  It is only redrawn when its tiles change.
- **Creatures.** Every creature the game shows becomes its own billboard using that creature's
  own sprite, scaled by body size; several creatures on one tile stand side by side. Sprites
  stand on the ground at the lowest drawn pixel; boulders are sunk halfway in.
- **Drawing.** A software renderer walks the map for every screen column and draws wall faces,
  floors, ceilings and billboards. Columns are spread over all CPU cores, which is what makes a
  full-resolution frame take a few milliseconds. It can only slide the picture up and down to
  look up or down.
- **Drawing on the graphics card** (`new_renderer`). The world is turned into textured triangles
  and handed to the game's own SDL renderer (`SDL_RenderGeometry`), which fills them in on the
  graphics card. Only the corners are worked out on the CPU, spread over several cores, so a frame
  takes about a millisecond and the camera can tilt freely. SDL draws flat triangles only, with no
  depth buffer: tiles are drawn far to near, surfaces close to the eye are cut into small pieces so
  their pictures do not bend, and haze is a second see-through triangle over each one. All
  pictures are kept in one large texture, each wall and floor texture with smaller copies of
  itself that are used far away, where the full picture would flicker.
- **Display.** The plugin hooks the stage of the game's renderer that draws the interface and,
  just before it, copies its image over the map. The interface then lands on top. The camera
  is animated there, on the render thread, so motion is smooth.
- **Mouse.** The software renderer remembers which tile or creature every pixel shows; with the
  graphics card renderer one ray is followed through the map for the pixel asked about. The game works
  out what the mouse is on from the mouse position and the origin of the map window, so for
  the moment the game handles a click or draws its hover text, the plugin moves the map window
  so that the answer is the tile the pointer is on in 3D.

Only what your adventurer has seen is drawn, because that is all the game draws.

## Limitations

- The game draws at most 50 frames per second unless you raise its graphics FPS cap
  (`G_FPS_CAP` in the game's settings); the view cannot be smoother than that.
- Terrain outside the map window around you is refreshed in rotation, so far-away changes and
  far-away creatures can take a second or two to show up.
- Mouse dragging and the mouse wheel are not handled specially.
- You cannot look up or down yet.
- Tree trunks are blocks, so trees are square.
- A ramp's direction is worked out from the walls around it, not from where the game lets you
  walk up, so an unusual ramp can slope the wrong way.
- Creatures and items are flat sprites that always face you.
- Night is not darkened, apart from the sky and haze.

## Layout

```
info.txt                                   mod metadata
install.ps1                                installer
media/                                     cursors, icons and paperdolls (replace freely)
tools/make_media.py                        draws default media that is missing (never overwrites)
plugin/                                    the fpview plugin
  build.ps1                                build script
  fpview.cpp                               hooks, camera, controls, commands
  menu.inc                                 intents, paperdoll and attack menus
  media.h/.cpp                             loads the media folder; small pixel canvas
  scene.h/.cpp                             captures map levels and creatures
  textures.h/.cpp                          turns game sprites into textures
  render.h/.cpp                            the renderer
scripts_modinstalled/
  first-person.lua                         command, hotkey, settings
  internal/first-person/config.lua         settings storage
```
