Art of the First Person View mod. Everything here is loaded when the game starts
(and again with "first-person reload"). Replace any file with your own PNG; sizes are free
unless noted. Delete a file to get the plain built-in look for it.

cursor.png          the pointer over the view while it is free (Alt held, or a menu open).
                    The top left corner is the tip.
crosshair.png       the crosshair in the middle of the view. The middle of the picture is the aim point.
aim_info.png        the crosshair for each intent, in place of crosshair.png (R switches the intent)
aim_interact.png    Info, Interact, Walk, Strike, Shoot, Throw, Wrestle. The middle of the picture
aim_walk.png        is the aim point. One that is missing falls back to crosshair.png.
aim_strike.png
aim_shoot.png
aim_throw.png
aim_wrestle.png
aim_nock.png        with Shoot and no arrow on the string (no bolt in the crossbow): a click nocks (loads) one
aim_unnock.png      with Unnock (Unload): a click takes the arrow off the string (the bolt out)
icon_ascii.png      the three buttons next to the minimap, 16 x 12 pixels (two interface cells)
icon_zoom_in.png
icon_zoom_out.png
compass_places.png  the picture on the button right of the compass that shows or hides the game's
                    nearby places on it. Optional: without it the game's own castle icon (the world
                    map's SITE_CASTLE) is used, and a built-in castle if that cannot be read.
minimap_arrow.png   the arrow on the minimap that shows the way you face. The top of the picture is
                    forward; it is turned for the other directions and drawn with its own
                    transparency. Square; any size.
strike_quick.png    the two buttons at the pointer when you click a creature with Strike: Quick
strike_aimed.png    strike (drawn just above the pointer) hits at once, Aimed strike (just below) goes
                    on to the choice of where to hit. The words are part of the picture. Made for
                    interface cells of 8 x 12 pixels (96 x 24 is 12 cells by 2); drawn larger when
                    the interface is.
dodge.png           a third button under those two while someone is striking at you: Dodge
weight.png          the inventory panel's sign for weight, in place of the game's own (after
                    "Burden:"). Square; drawn as tall as a line of text.
checkbox_on.png    the boxes in the attack menu
checkbox_off.png
sky/sun.png         the sun. Its middle is where the sun is; one pixel of it covers about a third
                    of a degree of the sky, whatever the window's size.
sky/moon_0.png      the moon in each of its eight phases: 0 new, 1 waxing crescent, 2 first quarter,
 ... moon_7.png     3 waxing gibbous, 4 full, 5 waning gibbous, 6 last quarter, 7 waning crescent.
                    Drawn lit from the right while it waxes (0-4) and from the left while it wanes;
                    it is turned so that the lit side faces the sun. Transparent pixels show the sky.
sky/stars.png       a row of square star pictures (as many as the row is wide / high). Small ones
                    first: most stars use the first pictures, few the last. They twinkle and turn
                    with the night.
sky/clouds_cirrus.png         the kinds of cloud the game has, each a picture that repeats across a
sky/clouds_cumulus.png        flat ceiling high over you (so the clouds shrink towards the horizon)
sky/clouds_cumulus_many.png   and drifts with the wind. Any size; the edges must fit together.
sky/clouds_nimbus.png         Transparency is how thick the cloud is. Coloured by the light of the
sky/clouds_altostratus.png    hour (white by day, dark at night, warm at dawn and dusk), so draw them
sky/clouds_stratus.png        in plain greys. Cirrus is highest, stratus (the overcast) lowest.
sky/rain.png        one raindrop, top to bottom; it is stretched along the way it falls. A rain of
                    something else (blood rain) takes the drop's lightness in that thing's colour.
sky/snow.png        a row of square snowflakes; each flake picks one.
thunder.wav         the thunder a few seconds after lightning in a storm. Any WAV file SDL reads
                    (PCM, any rate, mono or stereo). Played at the game's master volume times its
                    adventure ambience volume. No built-in copy: without it the storm is silent.
vitals/NAME.png     the icons left of the bars by your own paperdoll: bleedout, exhaustion, pain,
                    hunger, thirst, sleepiness (under it); starving, suffocation, drowning,
                    unconscious, stunned, webbed, paralysis, numbness, fever, nausea, dizziness,
                    infection (above it, while they last). Square, drawn as high as a bar (one
                    interface cell, 12 pixels at the normal size); 12 x 12 fits exactly.
                    A missing one is drawn from the plugin's built-in copy.
dolls/NAME.png      a paperdoll for the "aim for" menu
dolls/NAME.txt      where each body part is on NAME.png (see the comments inside)
models/*.obj        the shapes of furniture, stairs, wells and battlements, for Blender, with their
models/models.mtl   materials; see models/README.txt. Changes show while you play.

tools/make_media.py in the mod's source draws the default set again.
