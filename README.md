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
| left click | does what the current *intent* says with what the crosshair (or pointer) is on (see below) |
| right click | the game's list of what can be done with that tile, exactly like a right click on the normal map |
| **R** | next intent: Info, Interact, Walk, Strike, Shoot (only with a ranged weapon in hand; called Nock arrow while nothing is on the string, Load bolt with a crossbow), Unnock arrow (only with an arrow on the string; Unload bolt with a crossbow), Throw, Wrestle, then Info again. The crosshair changes to the intent's mark and its name shows for a moment above it. The choice is remembered, also between sessions. With Strike, Shoot, Throw or Wrestle, creatures are outlined in the colour of the conflict they are in (see below), grey = in no conflict, green = companion; with Strike or Wrestle only creatures within reach (next to you) are |
| **w** / **s** | walk forward / backward (`wasd`) |
| **a** / **d** | strafe left / right |
| **Shift+W** / **Shift+S** | go up / down (stairs, like the game's `<` and `>`). The game's wrestle and sneak keys do not work while the view is on: wrestling is an intent (R) |
| **c** | lie down (crawl) / stand up. The game's companions key does not work while the view is on: click its "View your companions" button |
| arrow keys | do what w, a, s, d normally do in adventure mode (up = w, left = a, down = s, right = d), once each letter has been pressed at least once in the session |
| **Space** / **Shift+Space** | let a moment pass, like `,` / a longer one, like `.` (for example while an arrow is being nocked). Space no longer clears announcements while the view is on |
| **camera** button, right of the game's "View your companions" button (with the action ring: at the top of the ring while the view is on, the button only while it is off) | switches between the normal and the first-person view. If the view goes off by itself while you sleep or rest, it comes back when you can act again; it stays off when you die or switch it off yourself |
| **timer** button, right of the camera (with the action ring: the number at the top of the ring, beside the camera, yellow while time runs) | lets time pass by itself: every second the game moves on by as many ticks as the number on the button (a wait, `.`, is 10 ticks). The border turns gold while it runs. It stops on a second click, on **Space** (that press does nothing else), when a menu or dialog opens, when you fight or a message the game would pause for appears, or when the view goes off. Only there while the view is on |
| mouse wheel over the timer button (or the ring's timer) | raises (up) or lowers (down) the number, 1 to 99 (`realtime_rate`): by 1 up to 10, then by 5. Works with Alt held too |
| mouse wheel anywhere else (not over the timer button or the message list) | changes your gait: up the next faster one, down the next slower one (crawling gaits while you lie down; your mount's gait while you ride). The speed shown at the top of the message list follows |
| **travel** and **sleep** buttons, right of the game's row of buttons | do what the game's map and sleep buttons do (`T`, `Z`), with the game's own pictures for them. The game's buttons in the bottom right corner (centre on yourself, build, travel, sleep) are hidden while the view is on. The bottom is rearranged: the game's row of buttons right of the portrait moves up beside the timer (one row with "View your companions", the camera, the timer, the game's buttons, then travel and sleep), the speed display ("Walk") is taken away (what it says is shown at the top of the message list), and the game's action bar from the middle of the bottom moves into the bottom row, right of the portrait |
| message list, bottom right corner | every message of the adventure in the game's colours, newest at the bottom, on a see-through background, each message on a rounded box of its own with its kind's icon (sword: combat, speech bubble: talk, box: everything else) before its first line; it scrolls to a new one when it comes in. The mouse wheel over it (Alt held, with mouse look), dragging its scrollbar's handle, or a click on the scrollbar beside the handle (above or below the middle: a page back or on) looks back. Four buttons at its top right: a sword (combat messages), a speech bubble (talk) and a box (everything else) show or hide that kind (yellow border: shown, blue: hidden); at its top left, while it is unfolded, the date ("15th Granite, 125") and what the game's speed display says ("Walk: ..."), each in a thin frame (the speed in purple while you sneak); the down arrow folds the list away, leaving only the newest message and an up arrow in the bottom right corner that brings it back. It reaches from the right edge to just past the game's action bar, which sits at the bottom left while the view is on. Conversation lines are read there: the game's box for new messages and conversation lines at the top of the screen is not shown (unless the game is waiting for you to page through more than fits). The game's boxes with one message and an "Okay" button (a god speaking to you, "You will have to leave this structure first ...") are answered at once while the view is on; their messages are in the list in gold, among the talk |
| gear, **@**, **+**, **-** next to the minimap, under the DFHack logo | the mod's settings; minimap in text characters and back; zoom in; zoom out. While the view is on, DFHack's logo button moves from the bottom left (where the paperdoll is) to the top of this column, or over the gear when the minimap is off; it goes back when the view goes off (DFHack's own overlay settings are not changed) |

What you hold in your hands is drawn low in the corners of the view. Lying on the ground lowers the camera. Riding a creature raises the camera by the height of a table, and the creature is not drawn in the view: the top quarter of its picture, enlarged, runs along the bottom of the screen, behind what you hold and the interface.

The bottom of the screen is one row, as tall as the message list: your own **paperdoll** in a square on
the left, marked where you are hurt; the message list in the middle, as wide as the room between; and a
square of six bars on the right, each with its icon on the left (`media/vitals`): **Bleedout** (blood lost),
**Exhaustion**, **Pain**, **Hunger**, **Thirst** and **Sleepiness**. Above the bars' square, while they last, bars for passing states: Suffocation (Drowning under water),
Unconscious, Stunned, Webbed, Paralysis, Numbness, Fever, Nausea, Dizziness, Infection, and Starving once
hunger has begun to burn your fat. A new one comes in nearest the square and pushes the others up; when one
ends, those above it come down. The pointer over a hurt part of the doll (Alt held, with mouse look) shows
under it what is wrong there, part by part: the part named in red (it no longer works), yellow (damaged),
brown (only the skin) or cyan (an organ that no longer works); what the whole part has (bleeding, swollen,
infected, a nerve severed, bandaged ...); then each hurt tissue, for example `muscle: torn, bruised`. These
words are made from the wounds the game keeps, not read off its Health tab, so they can differ from it. A click anywhere on the doll opens your
character sheet (the game's own); clicks on the bars do nothing. Every bar fills as things get worse, and the thin lines across a bar are
the game's own stages (Hungry, Very hungry, Starving ...). The pointer over a bar shows its value of its
maximum and the stage you are at, for example `Thirst: 83,159 / 864,000  (Thirsty)`. The maxima are where
the game kills you or knocks you out: thirst kills at 864,000 (ten days without water; a day is 86,400),
hunger starts burning fat at 5,184,000 and kills when it is gone, exhaustion knocks you out at 6,000 (you
collapse at 4,000 and can no longer sprint from 3,800), pain is at most 200 and from 100 can knock you
out each moment on a roll of discipline, willpower and focus, suffocation kills at 200 + (toughness +
endurance) / 4. Losing blood kills when none is left; it never knocks you out. Sleepiness never kills in
adventure mode; its bar ends at "Slumberous". These numbers were read from the game's code
(Dwarf Fortress 53.17). The game's own icons for hunger, thirst and the like are not shown. Bars for what your
creature never feels (no pain, no need to eat, drink or sleep) are left out. On the game's other screens
(inventory and the like) the doll keeps the portrait's place with only the Bleedout bar. `status_bars false`
does the same everywhere.

Smoothed natural walls and floors are plain stone with faint blotches; built stone floors are laid in square
slabs. An engraved wall shows, inside a carved frame, the main thing shown in its engraving carved in relief in
the wall's stone: the game's picture of the creature (a statue's upper half for peoples), tree or plant, item or
shape, or the character the game uses for it in text mode when the image itself is not loaded.

Furniture (tables, chairs, beds, chests, cabinets, coffins, statues, stands, racks, slabs, hatches, wells,
pedestals, supports, altars) is drawn as simple shapes made of blocks, in the colour the game shows it in and with
the game's picture of it on top. Stone and metal furniture has one even surface; wooden furniture shows planks. A cabinet or bookcase is a plain block with its back to a wall and its
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
ground, and ground not seen yet looks like ground of the same kind that has been seen. Where two kinds of
natural ground meet (grass and soil, sand and stone), the stronger one (grass, then soil, sand and ash, then
stone and ice) reaches into the other along an uneven line with a few loose tufts beyond it, instead of
stopping at the square edge of its tile; built, smoothed and farmed floors keep their edges. Snow, blood, mud,
vomit, water, dust and fallen leaves lie flat on the ground and on slopes with the game's own spatter pictures:
in view exactly as the game draws them, out of view picked from the map the way the game picks them (a few
bits, more, or a full cover that joins up with the covered tiles next to it). What is laid out on a
table or pedestal is shown on its top, trinkets and coins as small pictures and equipment bigger.
Water is a see-through block as deep as the game says (1/7 to 7/7 of a level), its sides showing where it
stands higher than what is beside it, in the game's own water picture for that depth, 85% solid:
15% of the floor or slope under it shows through (the seabed, a shore slope going into the sea). Only the top of a
body of water has a surface, so the sea is flat at its real level however deep the bottom lies. Towards dry
ground its edge has the game's light rim and thins out in a dither; the game's ocean waves rolling in towards the
coast are drawn on it with the game's own pictures (and on the shore where they run up onto it). The pointer aims through water, at the bottom and at what swims there. Magma is drawn the same way but solid. Brooks keep the game's
own brook pictures. Several things on
one tile of ground are drawn as a heap of small pictures of up to five of them. Corpses, pieces of corpses and
vermin remains are never hidden in the heap: each one (up to sixteen per tile) is drawn on its own, sized by the
body, and they stand in a small ring with the heap of the other things, like creatures sharing a tile. A creature
lying down is drawn lying: one on two legs is turned on its side, one on four legs or none sinks a quarter into
the ground. Stairs (`<`, `X`) are a spiral: a round column in the middle of the tile and twelve wedge-shaped steps
around it, one whole turn up to the next level, rising clockwise. Each level of a stack starts on the same side
(a side of the lowest tile that is not a wall), so stacked stairs join into one spiral; a creature on stairs stands
on the first step. Where stairs lead down to stairs coming up there is no floor (and no ceiling over the ones below),
so you see through to the other level; the lowest stairs stand on a floor and the top `>` has the level's ceiling above;
a hatch is a thin frame set in the floor around the edge of its tile and, while it is shut, a lid inside it with the
game's picture of the hatch on top; open, only the frame is left and the way down shows (a hatch on stairs that
also go up is not drawn).
A fortification is a low sill with
a block on each corner, so a row of them reads as a battlement from any side. You and other creatures glide
from tile to tile (`smooth`); the camera crosses a tile in about the time your last steps took, so
walking is one steady movement rather than a dash and a stop on every tile. The mouse can tilt the view far enough down to put your own tile under the
crosshair.

### Intents and the attack menus

A left click in the view does what the intent says (R goes to the next one):

- **Info**: the game's own sheet for the creature, building or item that was clicked (the same one the
  game shows elsewhere); on anything else nothing happens.
- **Interact**: on a creature, talk to it: the game's talk key, a click on that creature, and its "Start
  new conversation with ..." picked for you, so you land on "What will you say?". If the game does not
  offer to talk to it (too far, an animal ...) a note says so. On anything else, the game's list for that
  tile, with the one way to use what is there picked for you (pull a lever, raise or lower a bucket, push
  or ride something); if there are several, or other things to do there (pick up, chop ...), the list is
  left for you; if there is nothing to use, a note says so.
- **Walk**: what a plain click on the normal map does (walk there). On a creature next to you, the step
  onto its tile is taken from the game's list ("Move ..."), so the creature is not struck; if the game does
  not allow the step, a note says it is in the way.
- **Strike**, **Shoot**, **Throw**, **Wrestle**, **Nock** / **Unnock**: an attack, described below.

A click that comes while the game is still busy with what you did last (a strike on its way, a step
being taken) is not lost: it is carried out as soon as the game takes input again, if that is within
two and a half seconds. (Info does not wait: a sheet can be shown at any time.)

An attack click does the whole lead-up of an attack for you, with the game's own menus doing the work underneath:

- **Strike** and **Wrestle**, on a creature next to you: the game's "Combat ..." entry is chosen, its
  "Really start a conflict?" question is answered and Strike or Wrestle is picked, so you land directly on
  the choice of where to hit. If the game does not offer that move (wrestling with both hands full, say)
  a note says so and the game's own list stays up.
- With **Strike**, two buttons appear at the pointer first, in place of the game's list: **Quick strike**
  just above it, which is the game's "Attack ..." entry and hits at once, and **Aimed strike** just below,
  which goes on as described. While someone's strike at you is on its way there is a third button under
  them, **Dodge**: it picks dodging from the game's list of moves and leaves you on the game's own choice
  of where to dodge to. Clicking the creature again (beside the buttons) is a Quick strike. A click
  anywhere else, Esc or a right click puts the buttons away; that click then counts as a new one. (With `combat_menus` off, or when the
  game offers only one of the two, there are no buttons and the strike goes on to the choice of where to
  hit as before.)
- A strike of yours that is still on its way when the game hands control back is waited out for you (up
  to 40 ticks), so you see it land without passing a turn yourself.
- **Auto attack** (`auto_attack`, off by default, also in the settings behind the gear): on, walking into
  a creature you are in a conflict with strikes it, as the game does. Off, the step is taken from the
  game's own list for that tile instead ("Move ..."), so you end up on its tile if the game allows it; if
  it does not, a note says the creature is in the way and nothing happens.
- A creature on your own tile (lying under you, or standing over you) is at your eye and cannot be seen
  or clicked. An attack click that is on no creature goes to it, if you are in a conflict with it or the
  click is on your own tile.
- An attack click just beside a creature (within about 3% of the window's height, as when it is gliding
  from tile to tile) or on the tile it stands on goes to that creature.
- Otherwise a click with Strike, Wrestle or Shoot on a place where no creature is does nothing.
- **Throw**, on a creature or a place at any distance: the game's "What do you want to throw?" list opens;
  once you pick something, the aiming cursor is put on what you clicked and the throw goes off.
- **Shoot**, on a creature: the same without the list.
- With either, at a creature, two buttons appear at the pointer in place of the game's list: **Quick shot**
  (**Quick throw**) just above it and **Aimed shot** (**Aimed throw**) just below, which goes on to the
  choice of where to hit. They are for the creature you clicked only, whoever else stands on its tile. Esc
  or a right click puts them away. (With `combat_menus` off, the game's own list is left open instead.)
- **Nock arrow** (Shoot with no arrow on the string): a click anywhere in the view nocks one with the
  game's own key. The time that takes, for which the game asks for a key every tick, passes by itself; that
  also holds when you nock with the game's `n` or shoot without an arrow on the string.
- **Unnock arrow** (offered while an arrow is on the string): a click anywhere in the view takes it off
  again ("Cease nocking" in the game's list for your own tile). The intent then goes back to Shoot.
- With a **crossbow** (any weapon that is loaded rather than strung) the two are called **Load bolt** and
  **Unload bolt**. Load picks "Load into ..." from the game's list for your own tile; if you carry several
  kinds of bolt the list is left open for you to pick from. The game itself cannot unload a crossbow, so
  Unload is the mod's own: the bolt goes back into your quiver (else your backpack, else to your feet) at
  once, as a stack of its own.

Where to hit is shown on a **paperdoll** of the target instead of the game's long list: each large body
part carries two coloured letters, the first for how hard it is to hit (**T**ricky, **N**ormal,
**D**ifficult, **E**asy, **E**asier, **I**mpossible), the second for how well a hit would land
(**S**quare, **F**airly solid, **G** can only graze, **Q** can't quite connect, **L** can't land squarely).
The colours are the game's own, and the letters are spelled out at the bottom of the menu. Small parts
(fingers, eyes, teeth ...) are in compact columns next to the doll. Pointing at anything shows the game's
full text for it. A part of the target that is hurt carries the cut or bruise mark of your own doll just
above its letters, and pointing at it shows the same hint of its wounds as your doll does. A part that is
gone (cut off, destroyed) says "gone" in red, on the doll and in the columns, and cannot be clicked. The doll is picked by the body parts the creature's kind has, lost
ones included, so it stays the same however much of the creature is cut off; a creature that fits none
of them gets the list alone.

What to attack with follows in the same style: the attacks in a short list and the six options (quick,
heavy, wild, precise, charge, multi-attack) as check boxes in two columns.

Both menus are mouse menus: click an entry, or **Cancel** / Esc / right click to leave. `first-person set
combat_menus false` brings the game's own lists back. They also show when the attack was started from
the keyboard.

### Jumping

With the game's jump open (**J**, or jump on the action ring), a gold square lies on every tile you can
jump to, at the height of the floor you stand on, also over a pit or a drop (that is how you jump down).
The pointer becomes the jump picture, the square under it lights up, and a click on a square jumps there,
even where it hangs in the air. A standing jump goes exactly two tiles; with a run-up (momentum) it goes
3 to 5 tiles ahead, by your speed, as the game works it out. The game's own cursor keys and Enter still
work; its cursor's square is lit while the pointer is on none. The squares need the game's SDL to have
`SDL_RenderGeometry` (as the graphics card renderer does).

### The inventory panel

While the view is on, opening the inventory (`i`) shows a panel of the mod's own in place of the game's
inventory screen, in front of the view, in the room left by the mod's own interface (the buttons at the
bottom, the message list, your paperdoll, the compass and the minimap stay in sight). Its text is in the
game's own font, and "Burden" shows a weight sign (`media/weight.png`) where the game writes its own:

- On the left your creature's own picture, enlarged, with a square for each body part that wears or
  holds things around it: the head(s) in a row above it, the other parts in two columns beside it in
  the order of the body from top to bottom, your right side on the left as when facing you, each joined
  to its place on the picture by a thin line. Which parts get a square is worked out from the body plan
  (head, upper and lower body, every grasping and every standing part, and any other part that has
  something on it), so other bodies (four arms, claws, a tail) get their own squares. Under each square
  its name, shortened to fit: "left hand" is "l. hand", and a name still too long is cut to the first
  letters of its words ("upper left big sharp claw" becomes "ULBSC"). What is worn or held on a part
  is shown in its square, several things side by side in it. A grasping part (a hand) has two squares:
  the upper one for what is worn on it (a gauntlet), the one under it, labelled with its name and
  "held", for what it holds.
- The rest of the panel is what you carry: a block for each container (backpack, quiver, pouch, a
  waterskin and what is in it, a loaded crossbow and its bolt) with a square for each thing in it, at
  the size the game draws a tile, with the picture the game itself shows for it in its inventory. Empty
  containers show too, so that things can be dropped into them.
  It keeps to the right of your picture, above the ground row; when it is longer, the game's own
  scrollbar appears beside it (arrows, a handle to drag) and the mouse wheel scrolls it.
- Along the bottom, all the way across: what lies on your tile, one row, scrolled across (the game's
  scrollbar turned on its side, or the wheel) when it is longer than the panel. A click on a thing there
  offers **Pick up** (the game's own pick-up) and, for a stack, **Split stack**. When the game asks how
  many of a stack to pick up, the panel's own dialog asks instead (slider or typed number, OK or Cancel). Drag it onto a body part
  to pick it up and wear it (onto a hand's "held" square: just pick it up), into a container to pick it up and put it
  there, onto a stack of the same thing to share them out. Drag something you carry onto the row to drop
  it (taken off first if worn).
- Dragging something you wear into a container takes it off and then puts it in.
- Pointing at an item names it next to the pointer; pointing at a body part names it in full.
- A stack shows how many it holds in the bottom right corner of its square.
- A click or right click on an item opens the game's own list of what can be done with it (view, drop,
  remove, put in a container, eat or drink, throw, name ...) as a small menu next to it; click a row to
  do it. Esc, a click elsewhere or a second click on the item puts the menu away. The inventory stays
  open after what you did (the game closes it; the mod opens it again once the game takes input).
- **Split stack** (not in the game): at the end of that menu for a stack of several in a container. A
  dialog shows the number for the new stack and a slider; drag the slider or type the number, Enter or
  **OK** splits, Esc or **Cancel** leaves it. The new stack goes into the same container.
- Dragging a stack onto another stack of exactly the same thing (same kind, material, quality, maker,
  wear) opens the same dialog to share them out: the number is what stays in the dragged stack, the
  rest goes to the other; either may be made 0, which leaves one stack. When they lie in different
  containers, the slider only goes as far as there is room. Splitting and sharing take no game time.
- **Strapping a pouch back on** (not in the game): the game starts you with a pouch strapped over your
  clothes but has no way to put it back once it is off. Drop a container the game will not let you wear
  (a pouch, a bag) on a body part where you wear a garment (armour, a shirt or coat, trousers) and it is
  strapped there over it, as at the start. With nothing worn there it says so. No game time.
- Dragging an item onto a body part wears it, or, onto a grasping part (a hand), takes it into the hand
  (out of its container, or off). Dragging it onto a container's block puts it in that container;
  something worn is taken off first. The game decides which hand or foot it ends up on.
- Keys still work as in the game's inventory: a letter opens that item's list, Esc goes back and closes.
- The game's red **Done** button at the top right (or Esc) closes the inventory.

`first-person set inventory_panel false` brings back the game's own inventory screen.

### The action ring (Tab)

While the view is on, the game's rows of buttons at the bottom of the screen are gone (setting
`radial_menu`, on by default). **Tab** opens a ring of round buttons around the pointer (around the
middle of the view while mouse look holds the pointer), one for each of those actions, with the game's
own icon: interact and pick up, search, climb, jump, talk, perform, craft and butcher, abilities, strike,
wrestle, shoot, yield, weapon, stance, status, journal, inventory, movement and combat options, sneak,
companions, centre on yourself, travel, sleep. Pointing at one names it; a click does it, as its key
would. Esc, a right click, Tab again or a click beside the ring closes it. The button at the bottom of
the ring (the game's settings gear, the game has no "..." picture) opens a list of all the actions: a
click puts an action on the ring or takes it off; the choice is kept in `dfhack-config/fpview-radial.txt`.
The game's dialogs (the g list, talking, performing, crafting, abilities) no longer bring the old row of
buttons back, and the message panel stays unless such a dialog reaches into its place.
The ring also has the game's tracks button (show or hide tracks; dimmed while hidden): while tracks are
shown, the game's track symbols lie on the ground in the view, over snow, blood and the rest.
At the top of the ring, always there, are the mod's camera (back to the normal view) and timer (its
number; a click lets time pass by itself or stops it, the wheel over it changes the number); while the ring
is in use they are not on the bottom of the screen.
`first-person set radial_menu false` brings the game's buttons back (moved as before; the bottom row of
doll, message list and bars was laid out for the ring, and the moved buttons may lie over the doll).

### Inventory, Journal and Map

The inventory panel, the journal and the map share one place on the screen and one look: the game's own
window art (its frame with the corner knots, tabs, buttons, scrollbars and search box, taken from the
game's interface graphics). Three tabs on top of it switch between them: **Inventory** (the game's
inventory is opened behind it), **Journal** and **Map**. The journal's list and its page each have a scrollbar
when they are longer than the room: drag its handle (or press on the bar to bring the handle there), or
use the wheel.

The **Map** shows the world map in the game's own tiles (biome, forests, mountains, rivers and the site
icons; squares you have not seen stay dark), you as the game's travel marker, and every tracked journal
entry as an exclamation mark on its square. Every tracked entry gets a colour of its own, picked at
random among those no other marker has; its mark on the map, its label on the compass and its box in
the journal take that colour. A tracked region is also tinted in its colour over all its known squares
(its mark stands in its middle) and a click anywhere in it opens it. The known sites on the map name
themselves under the pointer and open their journal entry on a click; marks are found before sites,
and sites before a region's tint, when they lie on one another. Drag it to move it, use the wheel or the - and + buttons to
zoom, "Centre on you" to come back. A click on an exclamation mark opens that entry in the journal; a
click on the small map of a journal page opens the Map there. The tiles are the game's, but which
variant of a tile and the blended edges between kinds of land are not the game's choice.

The castle button right of the compass shows or hides the game's nearby places on the compass (tracked
journal entries stay). The choice is kept in `dfhack-config/fpview-compass-places.txt`.

### The journal panel (prototype)

While the view is on, the game's journal (its key, its button, or a click on a place under the compass)
opens the mod's journal instead, in the same room as the inventory panel.

- Tabs: Quests, Rumours, Sites, Places (named locations in sites), People (quest parties, your kin, and
  the leaders of the sites and civilizations you know), Civilizations, Regions, Artifacts (those named in
  rumours and quests or held by people you know) and Bestiary, each with how many entries it has.
- Every entry shows both its names, the English one and the one in the world's language, how far and
  which way it lies (as the compass says it), and what it is. Typing searches both names; Esc clears the
  search, Esc again closes. The list sorts by nearest, by name or by native name.
- The right side is the entry's page: where it is, who holds it, what is in it, the people and quests
  tied to it. A quest says where to go: its sites, where each figure in it was last known, the nearest
  site of each group in it. Every name on the page is a link to its own page; Back and Forward (or a
  right click) step through what you opened.
- Each page with a known place shows a small piece of the world map: the entry (yellow), you (white),
  the sites you know (pale dots); squares you have not uncovered are dark.
- **Tracking**: the square box at the left of each entry (and before a page's title) tracks it. A
  tracked entry is marked on the compass, first among the marks, in yellow with a check mark, and a
  click on its label opens its page. A tracked person in the loaded area is marked with a triangle
  pointing up when they are on a level above yours and down when below; on your level, and for
  everything else, the mark is a square. The box is yellow when where it is is known, grey when not; a grey
  one can still be ticked and shows on the compass once its place becomes known. Tracked entries are
  kept per save in `dfhack-config/fpview-tracked.txt`.
- "Game journal" opens the game's own journal.

On the game's **travel map** (while the view is on when you start travelling) every tracked entry is
marked too: an exclamation mark in its colour over its square, with its name in the small font under
the square. An entry that is off the travel map waits at the map's edge, on the line from you towards
it, with an arrow pointing its way and its distance under the name, the way the compass holds the
places outside its window. The travel map's journal button (the book at the bottom left) and its key
open the mod's journal there instead of the game's.

`first-person set journal_panel false` brings back the game's own journal.

### Sky and weather

The sky shows what the game has where you are (outdoors; underground there is none):

- **Time of day.** The sun crosses the sky from where the world's sun rises to where it sets, highest at
  noon and higher in summer than in winter, south of you (north in a world that lies south of the
  equator). The sky goes from blue by day through red at dawn and dusk to dark at night.
- **Night.** Stars come out after dusk, twinkle and turn slowly through the night. The moon is in the
  phase the game gives it and where that phase puts it: beside the sun when new (unseen), across the sky
  from it when full (up all night), its lit side towards the sun. By day it shows faintly.
- **Clouds.** The game keeps cumulus (a few, many, or storm clouds), cirrus and stratus (thin, overcast,
  rain clouds) for each region; each kind is its own picture, drifting with the region's wind. Heavy
  clouds grey the sky and the haze and hide the sun, moon and stars.
- **Fog** (mist, fog, thick fog) lays a haze over the distance, lighter or thicker with the weather; so
  do rain, snow and an evil cloud. It is only a haze: the view, the far land and its trees are drawn as
  far as in clear weather, and stay faintly visible even in thick fog.
- **Rain and snow** fall as light drops around you, never indoors: only where the tile is open to the
  sky and nothing solid stands between it and you, so you see rain through a doorway but not through a
  roof. Snow falls far slower and sways.
- **Storms** (rain under storm clouds): lightning lights up the view every 30 to 90 seconds, and its
  thunder (`media/thunder.wav`) is heard 2 to 5 seconds later, at the game's master and adventure
  ambience volumes. Only outdoors.
- **Evil weather.** A rain of something other than water over your world tile (blood rain) falls in that
  thing's colour. An evil cloud creeping near covers the sky with dark clouds of its own colour, and
  tints the haze in it.

Changes of weather come over a few seconds. `first-person status` shows the weather the sky is drawn from.

### The land beyond the map (far land)

The game keeps only a block of the world around you loaded. Beyond the draw distance the land goes on to
the horizon, made of blocks like the map: cells with flat tops at whole z-levels and sides down to their
lower neighbours, so no gap opens between them or where they meet the map. The real tiles are drawn over
it wherever they are shown.

- **Ground.** Inside the loaded map (beyond the draw distance) each cell takes the level of the map's real
  ground there, and the colour of its grass, soil, rock, ice or water. Outside the map: near you the
  game's mid-level map (one height per 48 x 48 tiles, what the game builds its local maps from), farther
  the world map's height per world tile, smoothed, with made-up relief (ridges in mountains, gentle hills
  elsewhere), lined up with the loaded map by measuring its real ground. Cells are 4 tiles wide near the
  map and twice as wide in every ring outward.
- **Colours** by biome (grass, forest floor, savanna, desert sand, badland, tundra, glacier, rock), snow on
  high mountains, sandy shores; sides in soil, rock or sand. Tops, west and east sides, north and south
  sides are lit like the map's.
- **Water.** The sea lies at the height of the loaded map's sea surface (as deep as its water is, so the
  far sea and the near one meet without a step), lakes at their world tile's level. Its colour is the
  near water's: the game's water picture over what lies under it, measured on the map's own water.
- **Trees** are blocks, a trunk and the leaves of each level, in the colours of that kind's own leaf and
  bark textures. The trees of the loaded map stand as the game grew them (their real shapes, from the
  game's tree data). Outside the map stand trees of the kinds the region grows in that biome (good and evil
  land have their own), as many as the biome and its vegetation give: dense in forests, scattered on
  savanna and grassland, none in deserts, on tundra or high up a mountain; each is shaped like a real tree
  of its kind in the loaded map, or with none loaded, after the kind's raws. Out to `far_trees` tiles, and
  at most the nearest 6000; beyond the last of them a dense wood is a block as high as its trees, in their
  leaves' colour. The software renderer draws trees as columns of leaves (no trunks under them).
- **Snow.** As much snow as lies on the loaded map's ground (measured from the game's snow on it whenever
  some 100 ticks of game time have passed, so it comes and goes with the weather) lies on the far land too:
  on the ground in patches (a cell is snow or bare ground, as much of it snow as lies there), on the
  leaves of trees in white specks like the map's snowy leaves (far off, where specks would be finer
  than a pixel, their colour together), on distant woods, and lakes freeze. Inside the loaded map every tile has its own; beyond
  it there is less in land warmer than where you are (by the world map's temperature) and none where it
  is much warmer.
- **Haze.** With the far land on, things fade into haze by the horizon's distance (`horizon`), not by the
  draw distance, so the loaded map itself is barely hazed. Fog, rain and snow still shorten the view.

Not shown: rivers, roads, towns and other sites beyond the map (inside it, buildings beyond the draw
distance stand as blocks of the ground). Underground, and where no sky can be seen, there is no far land.
It is worked out again, off the game's thread, every 48 tiles you walk and whenever the game moves its
loaded block; `first-person status` (the `far land` line) shows how it was lined up with the map, how many
real trees it found and how long it took.

### Art you can replace

Everything the mod draws that is not the game's own is in the mod's `media` folder and is read when the
game starts (`first-person reload` reads it again):

| File | What it is |
|---|---|
| `cursor.png` | the pointer while it is free (Alt held, or a menu open); the top left corner is the tip |
| `crosshair.png` | the crosshair; the middle of the picture is the aim point |
| `aim_info.png`, `aim_interact.png`, `aim_walk.png`, `aim_strike.png`, `aim_shoot.png`, `aim_throw.png`, `aim_wrestle.png`, `aim_nock.png`, `aim_unnock.png` | the crosshair for each intent, in place of `crosshair.png` (which is used for one that is missing) |
| `icon_ascii.png`, `icon_zoom_in.png`, `icon_zoom_out.png`, `icon_settings.png` | the buttons next to the minimap, 16 x 12 pixels (the gear opens the mod's settings) |
| `minimap_arrow.png` | the arrow on the minimap for the way you face: the top of the picture is forward, it is turned for the other directions and drawn with its own transparency. Square, any size |
| `button_camera.png` | the camera button, the whole button with its border, exactly 32 x 36 pixels |
| `button_timer.png`, `button_timer_on.png` | the timer button stopped and running, exactly 56 x 36 pixels; the number is written over pixels 32-47 across, 12-23 down |
| `button_travel.png`, `button_sleep.png` | the travel (map) and sleep buttons, exactly 32 x 36 pixels each; only used when the game has no pictures of its own for them (no graphics) |
| `checkbox_on.png`, `checkbox_off.png` | the boxes of the attack options |
| `shot_quick.png`, `shot_aimed.png`, `throw_quick.png`, `throw_aimed.png`, `strike_quick.png`, `strike_aimed.png`, `dodge.png` | the buttons at the pointer when you shoot or throw at a creature or strike one (and Dodge under them), words included; made for 8 x 12 interface cells (96 x 24 pixels is 12 cells by 2) and drawn larger with the interface |
| `vitals/NAME.png` | the icon left of each bar by your own paperdoll (`bleedout`, `exhaustion`, `pain`, `hunger`, `thirst`, `sleepiness`, `starving`, `suffocation`, `drowning`, `unconscious`, `stunned`, `webbed`, `paralysis`, `numbness`, `fever`, `nausea`, `dizziness`, `infection`), square, drawn as high as a bar (12 x 12 fits the normal size) |
| `dolls/NAME.png` + `dolls/NAME.txt` | a paperdoll and where each body part is on it |
| `sky/sun.png` | the sun; one pixel covers about a third of a degree of the sky |
| `sky/moon_0.png` ... `sky/moon_7.png` | the moon's eight phases, 0 new, 4 full; lit from the right while it waxes (0-4), from the left while it wanes. It is turned so the lit side faces the sun |
| `sky/stars.png` | a row of square star pictures, small ones first (most stars use those) |
| `sky/clouds_cirrus.png`, `sky/clouds_cumulus.png`, `sky/clouds_cumulus_many.png`, `sky/clouds_nimbus.png`, `sky/clouds_altostratus.png`, `sky/clouds_stratus.png` | the kinds of cloud, each a picture whose edges fit together, repeated over a ceiling high above and drifting with the wind; transparency is thickness. Draw them in greys: the hour's light colours them |
| `sky/rain.png` | one raindrop, stretched along its fall; blood rain and the like take its lightness in their colour |
| `sky/snow.png` | a row of square snowflakes |

A doll's text file has one line per place, `body part name = x, y` in pixels of the picture, with `|`
between alternative names (`right foot | right hoof = 33, 125`). The names are the ones in the game's
"What do you want to aim for?" list. Add a picture and a text file for a body shape that is missing and it
is used wherever it fits best. Paperdolls are made by hand.

`models/` holds the 3D shapes of furniture, stairs, wells and battlements as Wavefront OBJ files, one per
thing (`table.obj`, `chair.obj`, `cabinet.obj`, `altar.obj`, `well.obj`, `stairs.obj`, `stairs_bottom.obj` for
the lowest level of a stair, ...), with their
materials in `models.mtl`. Open them in Blender (import and export with the default axes), change them and
export over the same file: the view picks the change up within a second. Every object becomes an upright
block (a box, or a prism on its outline seen from above), so build with upright cubes and cylinders. A
material named `own...` is the thing's own material (with a PNG texture: the texture tinted in that
material's colour), `picture...` the game's drawing of it (cabinet doors, a slab's carving), any other its
texture or a plain pattern, in its colour. Words in square brackets in an object's name mark the special
parts (`[when=shut]`, `[hang]`, `[surface]` ...). `media/models/README.txt` has the details. A missing or
unreadable file falls back to the built-in model.

Additional settings: `minimap_ascii` (false), `minimap_tiles` (11, tiles across the minimap, 5 to 61), `wasd` (true), `mouse_look` (true), `combat_menus` (true), `inventory_panel` (true), `journal_panel` (true), `body_doll` (true, your paperdoll in place of the portrait), `status_bars` (true, the bars under and above it), `any_conflict_discs` (false), `realtime_rate` (5, game ticks a second while the timer button runs, 1 to 99).

The view also works in the object testing arena, after "Assume Control" of a creature.

Walls, trees and ground that has not been seen yet are drawn with built-in patterns (blocks, rough stone, soil, planks, bark, leaves, grass) tinted in the colour of their material; the colour is taken from the game's own sprite the first time a material is in view. Tree trunks are solid wood, branches and leaves are boxes of foliage. A constructed wall of wood is a full block whose sides look like a row of upright, rounded logs with bark, four to a tile. Every constructed wall (blocks, logs, any material) has a plain band of its material along its foot where it stands on the ground (nothing built below it) and along its head where it ends (nothing built above it); a wall one level high has both. Natural walls have none.

When developing: do not `unload`/`load` the plugin while an adventure is loaded. Go to the title menu first.

## Parts

| Part | Where it goes | What it does |
|---|---|---|
| `fpview` plugin (C++) | `<DF>\hack\plugins\fpview.plug.dll` | captures the scene and draws it |
| `first-person` script (Lua) | DF `mods` folder | hotkey and saved settings |

The plugin is native code, so it has to be built for the exact DFHack version you run and
rebuilt when DFHack updates. DFHack loads plugins only from `hack/plugins`, never from a mod, so the
mod carries a copy in its `dfhack_plugin` folder (with `dfhack_version.txt`, the DFHack it was built
for) and `first-person.lua` copies it into `hack/plugins` and loads it at the title screen: on the first
start, and again whenever a mod update brings a different file (unloading the old one first). With
another DFHack version installed it leaves the plugin alone and says so in a message. Windows only.
To publish on the Steam Workshop, run `install.ps1` (or `plugin\build.ps1 -Install`) and upload the
installed `mods\first_person_view` folder from the game.

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

`.\plugin\build.ps1 -Install` builds and installs everything in one go: the plugin, the script
mod and the art (art you have edited in the installed `media` folder is kept). With the game at the
title menu it swaps the plugin itself (unload, copy, load); with a world loaded it installs only the
scripts and art and says so, since unloading the plugin then freezes the game. It also reloads the
overlay widgets, so script changes show at once.

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
- When something strikes at you, a mark points from the pointer towards it, turned with your view:
  a red barbed arrow when it wounded you (`media/struck_hit.png`), two green chevrons when it missed
  (`media/struck_miss.png`).
- Words float up over a creature that is struck: a green "MISS!" when a strike left no wound, a red "DEAD!" when it killed, the
  wound in red as body part and what happened to it ("Head: Bruise", "Left arm: Cut",
  "Finger, Left hand: Cut off", "Broken", "Torn", "Artery opened", "Burn" ...), and in purple what got
  stuck in it ("Lower body: stuck iron arrow"). A blow that hurts several parts at once (the head struck, the
  neck bruised by the force) gives a line for each, one after another, each new one pushing the earlier ones
  up. Words only show for fights the game reports in its message list. Your own start just under the pointer
  and sink.

The game's box describing the looked-at tile is moved into the top left corner. The game's list of
nearby places that was there is shown on the compass instead: each place is a coloured pip on the compass
where it lies (one behind you is held, dimmer, at the nearer end), with two small centred lines under the
compass: its symbol and name (of the nearest world site that lies the way the game says), then its kind and
how far away it is in tiles ("monument 3518"). Hold Alt to free the pointer and click such a label to open
the game's journal on its list of sites with that place selected (a place you have not heard of is not in
the journal, which then just opens). `compass_markers false` leaves the places off the compass. The compass is 2 2/3 times as wide as its frame picture and shows most of the circle.

While the view is on, the game's date line and its box with the current quest in the top right
corner are not shown; the date is at the top of the message list instead. In their place is a
minimap: the normal top-down map around you, drawn by the game itself at a smaller size. "Minimap"
in the settings (`first-person set minimap false`) turns it off: then only the gear that opens the
settings is there, in the very corner, with the row of foes left of it. An arrow next to you on it (`media/minimap_arrow.png`, half see-through as drawn by default; the picture's own transparency is used) points at the tile a step forward goes to; it keeps its size
at every zoom, and the zoom goes out to 61 tiles across.

Every creature that is in a conflict with you always has a faint disc on the ground around its
feet, in the colour the game itself uses for the level of that conflict (taken from the game's
palette): No Quarter light magenta, Lethal light red, Non-lethal yellow, Brawl light cyan, Training
white, Horseplay light green, Encounter grey. It is the level between its side and yours. With
`first-person set any_conflict_discs true` ("Show discs for any conflict", off by default) creatures
fighting each other are marked too, in the highest level their side has with anyone; while it is off
they count as being in no conflict. Your companions and pets stay green. The creature under the pointer gets the disc too (grey when it is in no conflict) and,
while it walks, an arrow from the disc to the tile it steps to next. The outlines with an attack intent
use the same colours.

Right under the minimap is a row of pictures of every creature in view that is in a conflict with you
above Training (Brawl, Non-lethal, Lethal, No Quarter), each framed in the colour of that level and as
big as a tile of the minimap at its default zoom. A picture is the creature's portrait when the game has
made one, otherwise the creature itself; with the pointer on it, its name is written below. Hold **Alt** to free the pointer and click a picture:
the camera turns to that creature and looks at its middle. When there are more than fit, an arrow
appears at the end that has more; click it to scroll. Without the minimap the row is along the top of the screen, left of the gear. "Enemy list" in the settings (`enemy_list`) turns the row off.

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
| `fov` | `120` | horizontal field of view in degrees (40 to 160). Also a slider behind the gear next to the minimap |
| `resolution` | `100` | rendered resolution as a percentage of the window |
| `view_distance` | `48` | draw distance in tiles (4 to 999): the loaded map is drawn this far (beyond it the far land, when on); with the far land off, things also fade into haze up to it. Also in the settings behind the gear (D, type, Enter). Only the part of the world the game has loaded can be drawn, and large values cost frame time, the graphics card renderer most |
| `range` | `2` | how far terrain is tracked: 1 = the map window around you (60x34 tiles at 1080p), 2 = three windows across, 3 = five |
| `max_height` | `24` | Maximum height: z-levels drawn above you (0 to 999, up to the top of the map). Also in the settings behind the gear (U). Many levels cost memory and frame time |
| `max_depth` | `8` | Maximum depth: z-levels drawn below you (0 to 999, down to the bottom of the map). Also in the settings behind the gear (N). Many levels cost memory and frame time |
| `far_land` | `true` | Far land: the land beyond the loaded map, out to the horizon, from the world map (see "The land beyond the map"); haze then goes by `horizon`. Also in the settings behind the gear (L) |
| `visibility_gradient` | `true` | Visibility gradient: what the game says you cannot see (its line of sight, which is short in the dark and indoors) is black, and what you see fades towards it over about five tiles of open ground (not through walls); under a roof everything is a little darker (80%). Under the open sky it only applies while the sun is down (fading in at dusk and out at dawn); by day the outdoors is fully lit. The land beyond the map is as dark as the edge of the map near it. Also in the settings behind the gear (V) |
| `horizon` | `20000` | Horizon: how far the far land reaches, in tiles (500 to 60000), and the distance at which everything has faded into haze. Also in the settings (Z) |
| `far_trees` | `1400` | Far trees: how far trees are drawn as trees (blocks) beyond the draw distance, in tiles (0 to 1400; 0: none; at most the nearest 6000, beyond them dense woods are blocks of canopy). Also in the settings (T) |
| `model_lines` | `1` | dark lines along the edges of every face of furniture, stairs and other models, this many game pixels wide (a tile is 32), so they look like the game's outlined pixel art; `0` turns them off. Faces with the game's own picture on them (cabinet doors, signs) get none |
| `relative_controls` | `true` | movement keys follow the view; `false` keeps compass movement and turns the view to face where you last moved |
| `smooth` | `true` | animate turning, walking and climbing |
| `compass` | `true` | heading indicator at the top of the screen, with the nearby places on it. Switching it off switches `compass_markers` off too |
| `compass_markers` | `true` | the nearby places on the compass (pips and labels) |
| `cursor_hint` | `true` | the line below the pointer naming the creature, item, plant or building it is on (the ground and walls are never named there) |
| `minimap` | `true` | top-down map in the top right corner ("Minimap" in the settings) |
| `enemy_list` | `true` | the row of pictures of the creatures in a conflict with you ("Enemy list" in the settings) |
| `auto_attack` | `false` | walking into a creature you are in a conflict with strikes it ("Auto attack" in the settings); off, you step onto its tile instead if the game lets you |
| `hardware_renderer` | `true` | draw the view on the graphics card instead of in software. The camera then really tilts: you can look straight up (at birds overhead) or straight down without the picture stretching. It goes through the game's own SDL renderer, so it works wherever the game does (Windows or Linux, any maker of graphics card); if the game's SDL cannot do it, the software renderer is used. `resolution` does not apply to it: it always draws at the window's full size. The gear next to the minimap opens the settings with this ("Hardware renderer"), `cursor_hint`, `compass`, `compass_markers`, `minimap`, `enemy_list`, `auto_attack`, `far_land`, `visibility_gradient`, the distances and heights, and the `fov` slider |
| `inventory_panel` | `true` | the mod's inventory panel (your picture with a square per body part, what you carry as squares, drag and drop) in place of the game's inventory screen while the view is on (see "The inventory panel") |
| `journal_panel` | `true` | the mod's journal panel (both names of everything, where it is, every name a link) in place of the game's journal while the view is on (see "The journal panel") |
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
  thin surfaces, fortifications are hip-high walls you
  look over. A ramp is a real slope: each of its corners is up at the next level if a wall
  touches that corner, so it rises towards the wall whose top it leads onto (at the foot of a cliff it
cannot lead up, towards the cliff). Where an edge of a ramp is
raised by the walls beside it but has open ground beyond it (a door in a wall line, a fortification),
the ramp dips in the middle as a channel towards that side, so what lies past it can be seen. Closed doors fill
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
  stand on the ground at the lowest drawn pixel; boulders are sunk halfway in. Driftwood is a log
  lying on the tile like any other thing on the ground, the tile itself plain soil or sand.
- **Drawing.** A software renderer walks the map for every screen column and draws wall faces,
  floors, ceilings and billboards. Columns are spread over all CPU cores, which is what makes a
  full-resolution frame take a few milliseconds. It can only slide the picture up and down to
  look up or down.
- **Drawing on the graphics card** (`hardware_renderer`). The world is turned into textured triangles
  and handed to the game's own SDL renderer (`SDL_RenderGeometry`), which fills them in on the
  graphics card. Only the corners are worked out on the CPU, spread over several cores, so a frame
  takes about a millisecond and the camera can tilt freely. SDL draws flat triangles only, with no
  depth buffer: tiles are drawn far to near, surfaces close to the eye are cut into small pieces so
  their pictures do not bend, and haze is a second see-through triangle over each one. All
  pictures are kept in one large texture, each wall and floor texture with smaller copies of
  itself that are used far away, where the full picture would flicker.
- **Sky and weather.** The software renderer works out the colour of every empty pixel from the
  direction it looks in, a column at a time. The graphics card renderer is given the same sky as a
  few thousand triangles instead: a dome coloured at its corners, the sun, moon and stars as small
  pictures, the clouds as tiles of their pictures on a ceiling high up (farther out, larger tiles of
  smaller copies) and fog over it, all from one texture that is sent to the card once; that takes a
  fraction of a millisecond. Rain and snow are drops around you, painted into the software frame or
  drawn as triangles over the world.
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
- With the software renderer (`hardware_renderer false`) looking up or down only slides the picture,
  so it stretches near the top and bottom.
- Tree trunks are blocks, so trees are square.
- A ramp's direction is worked out from the walls around it, not from where the game lets you
  walk up, so an unusual ramp can slope the wrong way.
- Creatures and items are flat sprites that always face you.

## Credits

- `media/thunder.wav`: "Thunder.wav" by IllusiaProductions, <https://freesound.org/s/249950/>,
  licensed under [Creative Commons Attribution 4.0](https://creativecommons.org/licenses/by/4.0/).

## Layout

```
info.txt                                   mod metadata
install.ps1                                installer
media/                                     cursors, icons, paperdolls, sky and models (replace freely)
tools/make_media.py                        draws default media that is missing (never overwrites)
plugin/                                    the fpview plugin
  build.ps1                                build script
  fpview.cpp                               hooks, camera, controls, commands
  menu.inc                                 intents, paperdoll and attack menus
  vital_icons.inc                          built-in icons of the paperdoll's bars
  media.h/.cpp                             loads the media folder; small pixel canvas
  models.h/.cpp                            reads media/models/*.obj into the blocks of furniture
  scene.h/.cpp                             captures map levels and creatures
  textures.h/.cpp                          turns game sprites into textures
  render.h/.cpp                            the renderer
  sky.h/.cpp                               the sky (sun, moon, stars, clouds, fog) and falling rain and snow
  sound.h/.cpp                             plays thunder through SDL's audio
scripts_modinstalled/
  first-person.lua                         command, hotkey, settings
  internal/first-person/config.lua         settings storage
```
