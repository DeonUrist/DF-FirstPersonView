"""Draws the mod's default art into ../media, but only files that are not there.

Everything in the media folder is loaded when the game starts and can be
replaced with your own PNG files of any size. A file that already exists is
never touched, so art edited by hand is safe: to get one default picture back,
delete it and run  python tools/make_media.py

Paperdolls (media/dolls) are not drawn here; they are made by hand.

No libraries needed.
"""
import math
import os
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'media')


class Img:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [(0, 0, 0, 0)] * (w * h)

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return (0, 0, 0, 0)

    def shape(self, inside, c):
        for y in range(self.h):
            for x in range(self.w):
                if inside(x + 0.5, y + 0.5):
                    self.px[y * self.w + x] = c

    def ellipse(self, cx, cy, rx, ry, c):
        self.shape(lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1, c)

    def rect(self, x0, y0, x1, y1, c):
        self.shape(lambda x, y: x0 <= x < x1 and y0 <= y < y1, c)

    def capsule(self, x0, y0, x1, y1, width, c):
        dx, dy = x1 - x0, y1 - y0
        length2 = dx * dx + dy * dy or 1

        def inside(x, y):
            t = max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / length2))
            return math.hypot(x - (x0 + t * dx), y - (y0 + t * dy)) <= width / 2
        self.shape(inside, c)

    def poly(self, pts, c):
        def inside(x, y):
            hit = False
            for i in range(len(pts)):
                (ax, ay), (bx, by) = pts[i], pts[(i + 1) % len(pts)]
                if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                    hit = not hit
            return hit
        self.shape(inside, c)

    def ring(self, cx, cy, r, width, c):
        self.shape(lambda x, y: r - width / 2 <= math.hypot(x - cx, y - cy) <= r + width / 2, c)

    def outline(self, c):
        """a line of colour c around everything drawn so far"""
        add = []
        for y in range(self.h):
            for x in range(self.w):
                if self.get(x, y)[3] == 0 and any(self.get(x + dx, y + dy)[3] for dx, dy in
                                                  ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    add.append((x, y))
        for x, y in add:
            self.put(x, y, c)

    def over(self, other):
        for i, c in enumerate(other.px):
            if c[3]:
                self.px[i] = c

    def scaled(self, n):
        out = Img(self.w * n, self.h * n)
        for y in range(out.h):
            for x in range(out.w):
                out.px[y * out.w + x] = self.px[(y // n) * self.w + x // n]
        return out

    def save(self, name):
        path = os.path.join(ROOT, name)
        if os.path.exists(path):
            print('kept', name)
            return
        os.makedirs(os.path.dirname(path), exist_ok=True)
        raw = b''.join(b'\x00' + b''.join(bytes(c) for c in self.px[y * self.w:(y + 1) * self.w])
                       for y in range(self.h))

        def chunk(kind, data):
            body = kind + data
            return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)
        with open(path, 'wb') as f:
            f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', self.w, self.h, 8, 6, 0, 0, 0)) +
                    chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
        print('wrote', name, '%dx%d' % (self.w, self.h))


def from_rows(rows, colors, scale=1):
    img = Img(len(rows[0]), len(rows))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in colors:
                img.put(x, y, colors[ch])
    return img.scaled(scale) if scale > 1 else img


BLACK = (16, 14, 20, 255)

# ---------------------------------------------------------------------------
# pointers

GAUNTLET = [
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
]
from_rows(GAUNTLET, {'#': (24, 20, 30, 255), 'o': (128, 136, 150, 255), 'd': (86, 92, 108, 255),
                     'w': (226, 232, 240, 255), 'g': (214, 164, 48, 255), 'y': (255, 224, 120, 255)}, 2).save('cursor.png')


def crosshair(color):
    img = Img(33, 33)
    c = 16
    for a, b in ((2, 12), (21, 31)):
        img.rect(a, c - 1, b, c + 2, color)
        img.rect(c - 1, a, c + 2, b, color)
    img.outline(BLACK)
    return img


crosshair((255, 255, 255, 255)).save('crosshair.png')

RED = (255, 52, 40, 255)
ORANGE = (255, 150, 30, 255)
YELLOW = (255, 220, 60, 255)

# strike: four blades pointing at the middle
img = Img(41, 41)
for ang in (45, 135, 225, 315):
    dx, dy = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    px, py = -dy, dx
    tip = (20.5 + dx * 4, 20.5 + dy * 4)
    base = (20.5 + dx * 18, 20.5 + dy * 18)
    img.poly([tip, (base[0] + px * 4, base[1] + py * 4), (base[0] - px * 4, base[1] - py * 4)], RED)
img.outline(BLACK)
img.save('aim_strike.png')

# shoot: a sight ring with ticks and a dot
img = Img(41, 41)
img.ring(20.5, 20.5, 13, 2.2, RED)
for a, b in ((1, 10), (31, 40)):
    img.rect(a, 19.5, b, 21.5, RED)
    img.rect(19.5, a, 21.5, b, RED)
img.ellipse(20.5, 20.5, 2, 2, RED)
img.outline(BLACK)
img.save('aim_shoot.png')

# throw: an arc over a dot
img = Img(41, 41)
img.shape(lambda x, y: 12 <= math.hypot(x - 20.5, y - 24) <= 15 and y < 22, ORANGE)
img.poly([(33, 13), (39, 24), (29, 23)], ORANGE)
img.ellipse(20.5, 26, 3, 3, ORANGE)
img.rect(3, 34, 38, 36, ORANGE)
img.outline(BLACK)
img.save('aim_throw.png')

# wrestle: four corners closing on the middle
img = Img(41, 41)
for sx in (-1, 1):
    for sy in (-1, 1):
        cx, cy = 20.5 + sx * 13, 20.5 + sy * 13
        img.rect(min(cx, cx - sx * 9), cy - 1.5, max(cx, cx - sx * 9), cy + 1.5, YELLOW)
        img.rect(cx - 1.5, min(cy, cy - sy * 9), cx + 1.5, max(cy, cy - sy * 9), YELLOW)
img.ellipse(20.5, 20.5, 3, 3, YELLOW)
img.outline(BLACK)
img.save('aim_wrestle.png')

# ---------------------------------------------------------------------------
# buttons next to the minimap: 16 x 12, two interface cells

ICON = {'#': (255, 255, 255, 255), 'o': (255, 214, 90, 255)}
from_rows([
    "................",
    ".....oooooo.....",
    "....oo....oo....",
    "...oo..ooo.oo...",
    "...oo.oo.o.oo...",
    "...oo.oo.o.oo...",
    "...oo.oo.o.oo...",
    "...oo..oooooo...",
    "....oo..........",
    ".....ooooooo....",
    "................",
    "................",
], ICON).save('icon_ascii.png')
from_rows([
    "................",
    "................",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    "....########....",
    "....########....",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    "................",
    "................",
], ICON).save('icon_zoom_in.png')
from_rows([
    "................",
    "................",
    "................",
    "................",
    "................",
    "....########....",
    "....########....",
    "................",
    "................",
    "................",
    "................",
    "................",
], ICON).save('icon_zoom_out.png')

# ---------------------------------------------------------------------------
# buttons beside "View your companions", like the game's square ones: a dark
# outline, a steel blue border (gold while lit), a dark inside. Each is a
# whole number of interface cells: 4 x 3 (32 x 36 pixels) and 7 x 3.

def button(cells, lit=False):
    img = Img(cells * 8, 36)
    w, h = img.w, img.h
    img.rect(1, 2, w - 1, h - 2, (18, 14, 10, 255))
    img.rect(2, 3, w - 2, h - 3, (150, 110, 30, 255) if lit else (74, 93, 113, 255))
    img.rect(3, 4, w - 3, h - 4, (255, 206, 70, 255) if lit else (100, 129, 159, 255))
    img.rect(4, 5, w - 4, h - 5, (28, 28, 28, 255))
    return img


# camera: a body with a lens and a viewfinder on top
cam = Img(32, 36)
cam.rect(11, 9, 17, 13, (150, 156, 168, 255))
cam.rect(6, 12, 26, 27, (190, 196, 206, 255))
cam.rect(21, 14, 24, 16, (255, 214, 90, 255))
cam.ellipse(16, 19.5, 6, 6, (40, 44, 52, 255))
cam.ellipse(16, 19.5, 4, 4, (70, 120, 170, 255))
cam.ellipse(14.5, 18, 1.2, 1.2, (230, 240, 255, 255))
cam.outline((18, 14, 10, 255))
img = button(4)
img.over(cam)
img.save('button_camera.png')


# timer: a stopwatch on the left; the number of game ticks a second is written
# into the black window on the right (interface cells 4 and 5 of the middle row)
def timer(lit):
    img = button(7, lit)
    watch = Img(img.w, img.h)
    watch.rect(14, 5, 19, 8, (190, 196, 206, 255))
    watch.capsule(23, 9, 25, 7, 2, (190, 196, 206, 255))
    watch.ellipse(16.5, 19, 9, 9, (190, 196, 206, 255))
    watch.ellipse(16.5, 19, 7, 7, (236, 236, 226, 255))
    for k in range(12):
        a = math.radians(k * 30)
        watch.put(int(16.5 + math.sin(a) * 5.5), int(19 - math.cos(a) * 5.5), (110, 116, 130, 255))
    watch.capsule(16.5, 19, 16.5, 13.5, 1.6, (40, 44, 52, 255))
    watch.capsule(16.5, 19, 20.5, 21, 1.6, (200, 50, 40, 255) if lit else (40, 44, 52, 255))
    watch.outline((18, 14, 10, 255))
    img.over(watch)
    img.rect(30, 10, 50, 26, (74, 93, 113, 255))
    img.rect(31, 11, 49, 25, (0, 0, 0, 255))
    return img


timer(False).save('button_timer.png')
timer(True).save('button_timer_on.png')


# travel: a folded map with a river, a mountain and a red cross
def travel():
    m = Img(32, 36)
    m.poly([(6.5, 10), (12.5, 8), (19.5, 10), (25.5, 8), (25.5, 28), (19.5, 30), (12.5, 28), (6.5, 30)],
           (222, 196, 140, 255))
    m.poly([(12.5, 8), (19.5, 10), (19.5, 30), (12.5, 28)], (190, 160, 104, 255))
    m.poly([(7.5, 24), (10, 17), (12.5, 24)], (120, 108, 92, 255))
    m.capsule(15, 12, 17, 26, 1.6, (70, 120, 190, 255))
    m.capsule(20.5, 14, 24, 18, 1.7, (200, 40, 30, 255))
    m.capsule(24, 14, 20.5, 18, 1.7, (200, 40, 30, 255))
    m.outline((18, 14, 10, 255))
    img = button(4)
    img.over(m)
    return img


# sleep: a crescent moon and two Zs
def sleep():
    m = Img(32, 36)
    m.ellipse(13, 21, 7.5, 7.5, (240, 220, 120, 255))
    m.ellipse(16.5, 18.5, 6.5, 6.5, (0, 0, 0, 0))
    white = (230, 236, 246, 255)
    for x0, y0, size in ((18.5, 8, 6), (15, 14.5, 3.5)):
        m.capsule(x0, y0, x0 + size, y0, 1.4, white)
        m.capsule(x0 + size, y0, x0, y0 + size, 1.4, white)
        m.capsule(x0, y0 + size, x0 + size, y0 + size, 1.4, white)
    m.outline((18, 14, 10, 255))
    img = button(4)
    img.over(m)
    return img


travel().save('button_travel.png')
sleep().save('button_sleep.png')

# check boxes of the attack menu
BOX = {'#': (170, 178, 196, 255), '.': (24, 26, 34, 255), 'x': (120, 240, 110, 255)}
from_rows([
    "############",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "############",
], BOX).save('checkbox_off.png')
from_rows([
    "############",
    "#..........#",
    "#........xx#",
    "#.......xx.#",
    "#......xx..#",
    "#.xx..xx...#",
    "#..xxxx....#",
    "#...xx.....#",
    "#..........#",
    "#..........#",
    "#..........#",
    "############",
], BOX).save('checkbox_on.png')

# wound marks on the player's own paperdoll (bottom left of the screen)
img = Img(13, 13)
img.ellipse(6.5, 6.5, 5, 4.5, (150, 70, 190, 255))
img.ellipse(5.5, 5.5, 2.5, 2, (200, 130, 230, 255))
img.outline(BLACK)
img.save('wound_bruise.png')

img = Img(13, 13)
img.capsule(2.5, 10.5, 10.5, 2.5, 2.6, (235, 40, 36, 255))
img.ellipse(9.5, 10, 1.6, 1.8, (235, 40, 36, 255))
img.outline(BLACK)
img.save('wound_cut.png')

# ---------------------------------------------------------------------------
# compass: a strip with all eight directions that slides behind a frame

LETTERS = {
    'N': ["#...#", "##..#", "##..#", "#.#.#", "#..##", "#..##", "#...#"],
    'E': ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    'S': [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    'W': ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
}


def word(img, text, cx, top, color, scale):
    width = len(text) * 6 * scale - scale
    x0 = int(cx - width / 2)
    for i, ch in enumerate(text):
        for y, row in enumerate(LETTERS[ch]):
            for x, bit in enumerate(row):
                if bit == '#':
                    img.rect(x0 + (i * 6 + x) * scale, top + y * scale, x0 + (i * 6 + x + 1) * scale,
                             top + (y + 1) * scale, color)


SEG = 64                                    # pixels per eighth of a turn
strip = Img(8 * SEG, 22)
strip.rect(0, 0, strip.w, strip.h, (18, 20, 26, 255))
for i, name in enumerate(['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW']):
    cx = i * SEG + SEG // 2
    if len(name) == 1:
        word(strip, name, cx, 4, (255, 214, 90, 255) if name == 'N' else (255, 255, 255, 255), 2)
    else:
        word(strip, name, cx, 7, (150, 156, 170, 255), 1)
    for k in (1, 2, 3):                     # ticks between the names
        x = i * SEG + SEG // 2 + k * SEG // 4
        if k == 2:
            strip.rect(x - 1, 7, x + 1, 15, (110, 116, 130, 255))
        else:
            strip.rect(x, 9, x + 1, 13, (80, 86, 100, 255))
strip.save('compass.png')

frame = Img(236, 30)
GOLD = (255, 170, 30, 255)
frame.rect(0, 0, frame.w, frame.h, GOLD)
frame.rect(2, 2, frame.w - 2, frame.h - 2, (70, 52, 20, 255))
frame.rect(3, 3, frame.w - 3, frame.h - 3, (18, 20, 26, 255))
for y in range(4, frame.h - 4):             # the window the strip shows through
    for x in range(4, frame.w - 4):
        frame.put(x, y, (0, 0, 0, 0))
mid = frame.w // 2
frame.poly([(mid - 5, 2), (mid + 5, 2), (mid, 8)], GOLD)
frame.poly([(mid - 5, frame.h - 2), (mid + 5, frame.h - 2), (mid, frame.h - 8)], GOLD)
frame.save('compass_frame.png')

README = os.path.join(ROOT, 'README.txt')
if os.path.exists(README):
    print('kept README.txt')
else:
    with open(README, 'w', newline='\n') as f:
        f.write("""Art of the First Person View mod. Everything here is loaded when the game starts
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
""")
print('done')
