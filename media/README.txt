Art of the First Person View mod. Everything here is loaded when the game starts
(and again with "first-person reload"). Replace any file with your own PNG; sizes are free
unless noted. Delete a file to get the plain built-in look for it.

cursor.png          the pointer over the view while it is free (Alt held, or a menu open).
                    The top left corner is the tip.
crosshair.png       the crosshair in the middle of the view. The middle of the picture is the aim point.
aim_strike.png      the crosshair while Ctrl is held, one per intent (Ctrl+R switches)
aim_shoot.png
aim_throw.png
aim_wrestle.png
icon_ascii.png      the three buttons next to the minimap, 16 x 12 pixels (two interface cells)
icon_zoom_in.png
icon_zoom_out.png
button_camera.png   the camera button next to the game's "View your companions" button.
                    Exactly 32 x 36 pixels (4 x 3 interface cells of 8 x 12).
button_timer.png    the timer button right of it, off and on. Exactly 56 x 36 pixels (7 x 3 cells).
button_timer_on.png The number of game ticks a second is written over the 5th and 6th cell of
                    the middle row (pixels 32-47 across, 12-23 down), in white on black.
button_travel.png   the travel (map) and sleep buttons right of the speed display ("Walk").
button_sleep.png    Exactly 32 x 36 pixels each. Only used when the game has no
                    pictures of its own for these buttons (no graphics).
                    A button picture of another size is ignored and the button is drawn in
                    text characters instead. Button pictures are looked at again every few
                    seconds, so a changed one shows without a reload.
compass.png        the compass: one strip with N NE E SE S SW W NW, each an eighth of its width,
                    N in the first eighth. It slides behind the frame as you turn.
                    It is drawn twice as wide as the picture.
compass_frame.png   the frame over the strip; transparent where the strip shows through
                    It is drawn four times as wide: the 8 pixels at each end and the
                    16 in the middle stay as they are, the rest is stretched.
wound_bruise.png    marks on your own paperdoll (bottom left) where a body part is bruised or cut
wound_cut.png
checkbox_on.png     the boxes in the attack menu
checkbox_off.png
dolls/NAME.png      a paperdoll for the "aim for" menu
dolls/NAME.txt      where each body part is on NAME.png (see the comments inside)

tools/make_media.py in the mod's source draws any default picture that is missing
(it never replaces a file that is there). Paperdolls are made by hand.
