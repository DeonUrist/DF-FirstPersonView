Models of the First Person View mod: the shapes of furniture, stairs, wells and battlements,
as Wavefront OBJ files you can open and change in Blender. Save (export) a model over its
file and the game shows the change within a second while the view is on. Delete a file to
get the built-in model back. Whatever the plugin cannot use is named in the DFHack console.

Files
  table.obj          Table                  chair.obj         Chair (a stool)
  bed.obj            Bed                    chest.obj         Box / chest
  cabinet.obj        Cabinet                bookcase.obj      Bookcase
  coffin.obj         Coffin                 statue.obj        Statue (the podium; the game's
  altar.obj          Offering place                           picture of the statue stands on it)
  armor_stand.obj    Armor stand            weapon_rack.obj   Weapon rack
  slab.obj           Slab                   sign.obj          Shop or tavern sign by a door
  hatch.obj          Hatch, floor grate     well.obj          Well
                     and floor bars         pedestal.obj      Display pedestal
  pillar.obj         Support                stairs.obj        Spiral stairs (one level of them)
  stairs_bottom.obj  the lowest level of a stair (no stair below it); without it, stairs.obj
                     with whatever reaches under the floor cut off
  fortification.obj  the blocks on a fortification's sill (the low sill itself is not part of it)
  models.mtl         the materials the models use (a model may also bring its own .mtl)
  Only these names are used; any other .obj here is named in the DFHack console as unused.

In Blender
  Import: File > Import > Wavefront (.obj), with the default axes (Forward -Z, Up Y).
  Export: File > Export > Wavefront (.obj), default axes again, with "Materials" on, over the
  same file name in this folder. Pictures go next to the files (Path Mode "Copy" or
  "Relative" does that; an absolute path works too, but only on your computer).

  One Blender unit is one tile. The tile is the square from -0.5 to 0.5 around the origin on
  X (west to east) and Y (south to north: +Y is north). Z is up from the floor at 0; one level
  of the map is 1.2 high. A dwarf is about 1 tall.

Making a new model from an old one
  1. Import the old model (File > Import > Wavefront). Blender may name its materials own.001,
     picture.001 ... if they already exist in the scene: that is fine, only the start counts.
  2. Change it. To put the bottom of parts on the floor: select the lowest vertices in Edit
     Mode, press N, and set Median Z to 0 (or S, Z, 0 to flatten them, then G, Z to move).
  3. Keep the marker objects ([surface], [stand]) and the words in [brackets] in the names.
     They are easy to lose: check the Outliner before exporting.
  4. Export with "Selection Only" off (or everything selected), Materials on, default axes,
     to the name of the thing in this folder. Blender writes a .mtl of the same name next to
     it; keep both.
  5. With the view on in the game it shows at once; anything wrong is named in the DFHack
     console (fpview: models/...).

What a model can be
  The view draws only upright blocks, so every object becomes one block:
  - an object whose outline seen from above is a rectangle along the axes is drawn as that
    box (its size is the object's extent);
  - any other object becomes an upright prism on its outline seen from above (a stair's
    step, a round column, a block turned at an angle), from its lowest point to its highest,
    with at most 12 corners (a rounder one loses its smallest corners). Hollows are filled.
  So model with cubes and cylinders standing upright; sloped or tilted faces are not drawn
  sloped. At most 24 objects count in one model.

Materials (the material names matter)
  own...      the thing's own material (oak, granite, iron ...) as the game makes it. With a
              picture (an Image Texture on its Base Color: map_Kd) the picture is used,
              tinted in the colour of the thing's material: draw it in greys.
  picture...  the game's own drawing of the thing. On a side it is stretched over that side
              (cabinet doors, books on a bookcase, the carving on a slab or a sign); on a top
              it covers the top (a hatch's lid).
  anything else: its picture if it has one, else a plain pattern, either way in its Base
              Color. The pattern is named by how the material's name starts: blocks smooth
              rough soil planks bark leaves grass ice grit (the default) concrete logs slabs.
              grit_cloth is cloth, smooth_rope a rope. A Base Color of white keeps a
              picture's own colours.
  A picture is a PNG; it covers one tile width and repeats. Each side of a block takes the
  material of its faces: the top faces, the bottom faces, and the side faces (the side
  material with the most area counts for all sides, except sides given "picture").

Words in the object's name, in [square brackets] (as "Lid [when=shut]")
  when=shut   only while a hatch is closed           when=open   only while it is open
  when=wall   only for a cabinet, bookcase or sign against a wall
  when=free   only when it stands free (out in the room, a sign on its post)
  hang        its bottom goes down as deep as the well's shaft (the well's rope)
  fit         shrunk to the proportions of the game's picture, keeping its top and its west
              or north edge (a sign's board)
  surface     not drawn: its top is the height things put on it lie at (a table top)
  stand       not drawn: where a creature on its tile stands (its middle seen from above,
              its bottom), if not in the middle on the floor (the first step of a stair)

Lines
  Every face of a block gets a dark line along its edges, one game pixel wide (1/32 of a
  tile; the setting model_lines changes the width, 0 turns them off), except faces with the
  game's picture on them. Where two blocks meet, both have their line, so build a surface
  that should look like one piece as one object. Upright edges of round pieces get a line
  only where the sides turn sharply (a stair step's corners, not the column's).

Facing
  Cabinets, bookcases and signs are made with their back to the NORTH wall (+Y); the game
  turns them to stand against their own wall. Stairs are made with the first step on the
  NORTH side, rising clockwise; they are turned to start on the side they are entered from.
  Other models are not turned. A stair's parts below the floor are cut off at the floor,
  except where the stair goes on down to the level below.
