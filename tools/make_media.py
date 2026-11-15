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
import re
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

# nock and unnock: an arrow, green to put it on the string, blue to take it off
def arrow_mark(color):
    img = Img(41, 41)
    d = (0.7071, -0.7071)
    p = (0.7071, 0.7071)
    def at(along, side):
        return (20.5 + d[0] * along + p[0] * side, 20.5 + d[1] * along + p[1] * side)
    img.poly([at(-17, -1.3), at(9, -1.3), at(9, 1.3), at(-17, 1.3)], color)
    img.poly([at(19, 0), at(8, -5.5), at(8, 5.5)], color)
    for back in (-17, -13):
        img.poly([at(back, 0), at(back - 4, -5), at(back, -5), at(back + 4, 0)], color)
        img.poly([at(back, 0), at(back - 4, 5), at(back, 5), at(back + 4, 0)], color)
    img.outline(BLACK)
    return img


arrow_mark((96, 224, 96, 255)).save('aim_nock.png')
arrow_mark((110, 176, 255, 255)).save('aim_unnock.png')

CYAN = (110, 210, 255, 255)
GREEN = (120, 230, 120, 255)
WHITE = (240, 240, 240, 255)

# info: a ring with an "i" in it
img = Img(41, 41)
img.ring(20.5, 20.5, 14, 2.4, CYAN)
img.ellipse(20.5, 13.5, 2, 2, CYAN)
img.rect(18.5, 17.5, 22.5, 28.5, CYAN)
img.rect(16.5, 26.5, 24.5, 28.5, CYAN)
img.outline(BLACK)
img.save('aim_info.png')

# interact: a speech bubble with a dot in the middle
img = Img(41, 41)
img.shape(lambda x, y: 11 <= math.hypot((x - 20.5) / 1.25, y - 19) <= 13.4, GREEN)
img.poly([(12, 27), (8, 36), (19, 30)], GREEN)
img.ellipse(20.5, 19.5, 2.5, 2.5, GREEN)
img.outline(BLACK)
img.save('aim_interact.png')

# walk: two footprints, one each side of the aim point
img = Img(41, 41)
for (fx, fy) in ((12.5, 26), (28.5, 14)):
    img.ellipse(fx, fy - 3, 3.6, 5, WHITE)      # the ball of the foot
    img.ellipse(fx, fy + 5.5, 2.8, 3, WHITE)    # the heel
img.ellipse(20.5, 20.5, 1.6, 1.6, WHITE)
img.outline(BLACK)
img.save('aim_walk.png')

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
# the two buttons at the pointer when you shoot or throw at a creature: 96 x 24, which
# is 12 interface cells by 2, words included

LETTERS = {
    'Q': [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    'A': [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    'D': ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    'u': [".....", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#"],
    'i': ["..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###."],
    'c': [".....", ".....", ".###.", "#....", "#....", "#...#", ".###."],
    'k': ["#....", "#....", "#..#.", "#.#..", "##...", "#.#..", "#..#."],
    's': [".....", ".....", ".####", "#....", ".###.", "....#", "####."],
    'h': ["#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#"],
    'g': [".....", ".####", "#...#", "#...#", ".####", "....#", ".###."],
    'o': [".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."],
    't': [".#...", ".#...", "###..", ".#...", ".#...", ".#..#", "..##."],
    'm': [".....", ".....", "##.#.", "#.#.#", "#.#.#", "#...#", "#...#"],
    'e': [".....", ".....", ".###.", "#...#", "#####", "#....", ".###."],
    'd': ["....#", "....#", ".##.#", "#..##", "#...#", "#...#", ".####"],
    'r': [".....", ".....", "#.##.", "##..#", "#....", "#....", "#...."],
    'w': [".....", ".....", "#...#", "#...#", "#.#.#", "#.#.#", ".#.#."],
}


def shot_button(words):
    img = Img(96, 24)
    img.rect(0, 0, 96, 24, BLACK)
    img.rect(1, 1, 95, 23, (196, 160, 96, 255))     # the lit edge, top and left
    img.rect(2, 2, 95, 23, (96, 74, 44, 255))       # the edge in shade, bottom and right
    img.rect(2, 2, 94, 22, (150, 118, 68, 255))
    img.rect(3, 3, 93, 21, (46, 40, 36, 255))
    x = (96 - (len(words) * 6 - 1)) // 2
    for ch in words:
        for dy, row in enumerate(LETTERS.get(ch, [])):
            for dx, bit in enumerate(row):
                if bit == '#':
                    img.put(x + dx + 1, 9 + dy + 1, BLACK)
        for dy, row in enumerate(LETTERS.get(ch, [])):
            for dx, bit in enumerate(row):
                if bit == '#':
                    img.put(x + dx, 9 + dy, (255, 255, 255, 255))
        x += 6
    return img


shot_button('Quick shot').save('shot_quick.png')
shot_button('Aimed shot').save('shot_aimed.png')
shot_button('Quick throw').save('throw_quick.png')
shot_button('Aimed throw').save('throw_aimed.png')
shot_button('Quick strike').save('strike_quick.png')
shot_button('Aimed strike').save('strike_aimed.png')
shot_button('Dodge').save('dodge.png')

# ---------------------------------------------------------------------------
# struck at: the mark that points from the pointer at whoever struck at you.
# The top of the picture is the end towards the attacker.

# it wounded you: a red arrow with a barbed head
img = Img(48, 96)
img.poly([(24, 2), (45, 40), (24, 28), (3, 40)], (235, 30, 30, 255))
img.poly([(21.5, 26), (26.5, 26), (25, 94), (23, 94)], (235, 30, 30, 255))
img.outline(BLACK)
img.outline(BLACK)
img.save('struck_hit.png')

# it missed: two green chevrons, nothing solid behind them
img = Img(48, 96)
for top in (4, 40):
    img.poly([(24, top), (45, top + 36), (37, top + 40), (24, top + 16), (11, top + 40), (3, top + 36)],
             (70, 230, 90, 255))
img.outline(BLACK)
img.outline(BLACK)
img.save('struck_miss.png')

# ---------------------------------------------------------------------------
# the arrow on the minimap for the way you face: the top of the picture is
# forward. Half see-through, so the tile under it shows.

img = Img(36, 36)
img.poly([(18, 2.5), (32.5, 18.5), (22, 18.5), (22, 32.5), (14, 32.5), (14, 18.5), (3.5, 18.5)], (255, 255, 255, 128))
img.outline((0, 0, 0, 128))
img.outline((0, 0, 0, 128))
img.save('minimap_arrow.png')

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


# ---------------------------------------------------------------------------
# the buttons of the message panel: 16 x 12, two interface cells

from_rows([
    "............##..",
    "...........###..",
    "..........###...",
    ".........###....",
    "........###.....",
    ".......###......",
    "..oo..###.......",
    "...oo###........",
    "....ooo.........",
    "...ooooo........",
    "..oo...oo.......",
    ".oo.............",
], ICON).save('msg_combat.png')
from_rows([
    "................",
    "..###########...",
    ".#...........#..",
    ".#.##.###.##.#..",
    ".#...........#..",
    ".#.####.###..#..",
    ".#...........#..",
    "..####..######..",
    ".....#.#........",
    ".....##.........",
    ".....#..........",
    "................",
], ICON).save('msg_talk.png')
from_rows([
    "................",
    "..############..",
    "..#..........#..",
    "..#..........#..",
    "..############..",
    "..#....oo....#..",
    "..#....oo....#..",
    "..#..........#..",
    "..#..........#..",
    "..############..",
    "................",
    "................",
], ICON).save('msg_other.png')
from_rows([
    "................",
    "......####......",
    "......####......",
    "......####......",
    "......####......",
    "..############..",
    "...##########...",
    "....########....",
    ".....######.....",
    "......####......",
    ".......##.......",
    "................",
], ICON).save('msg_hide.png')
from_rows([
    "................",
    ".......##.......",
    "......####......",
    ".....######.....",
    "....########....",
    "...##########...",
    "..############..",
    "......####......",
    "......####......",
    "......####......",
    "......####......",
    "................",
], ICON).save('msg_show.png')

# the gear next to the minimap that opens the settings
from_rows([
    "................",
    ".......##.......",
    "...##.####.##...",
    "...##########...",
    "....###..###....",
    "..####....####..",
    "..####....####..",
    "....###..###....",
    "...##########...",
    "...##.####.##...",
    ".......##.......",
    "................",
], ICON).save('icon_settings.png')


# ---------------------------------------------------------------------------
# the sky (media/sky): sun, moon phases, stars, clouds, rain and snow

# the icons of the bars under and above the player's paperdoll, from the rows
# built into the plugin (plugin/vital_icons.inc), so the two stay the same
def vital_icons():
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'plugin', 'vital_icons.inc'),
               encoding='utf-8').read()
    colours = {k: (int(r), int(g), int(b), 255)
               for k, r, g, b in re.findall(r"\{'(.)', (\d+), (\d+), (\d+)\}", src)}
    for name, body in re.findall(r'\{"(\w+)", \{(.*?)\}\}', src, re.S):
        rows = re.findall(r'"([^"]*)"', body)
        assert len(rows) == 12 and all(len(r) == 12 for r in rows), name
        assert all(ch == '.' or ch in colours for r in rows for ch in r), name
        from_rows(rows, colours).save('vitals/%s.png' % name)


vital_icons()


def sky_sun():
    img = Img(32, 32)
    c = 16
    for y in range(32):
        for x in range(32):
            dx, dy = x + 0.5 - c, y + 0.5 - c
            r = math.hypot(dx, dy)
            ang = math.atan2(dy, dx)
            if r <= 5:
                img.put(x, y, (255, 255, 236, 255))
            elif r <= 7:
                img.put(x, y, (255, 238, 150, 255))
            elif r <= 8:
                img.put(x, y, (255, 206, 70, 255))
            elif r <= 10:
                img.put(x, y, (255, 214, 110, 90))
            else:
                # rays: long ones up, down and to the sides, short ones between
                k = (ang / (math.pi / 4)) % 1.0
                near = min(k, 1 - k)
                step = round(ang / (math.pi / 4)) % 2
                reach = 15 if step == 0 else 12
                if near * r < 0.9 and r <= reach:
                    a = 230 if r < reach - 2 else 140
                    img.put(x, y, (255, 196, 70, a))
    img.save('sky/sun.png')


def sky_moons():
    size, rad = 24, 10.0
    craters = [(-3, -3, 2.2), (3, 1, 1.8), (-1, 4, 1.5), (4, -4, 1.2), (-5, 2, 1.0)]
    for phase in range(8):
        img = Img(size, size)
        phi = phase / 8 * 2 * math.pi
        lx, lz = math.sin(phi), -math.cos(phi)
        for y in range(size):
            for x in range(size):
                dx, dy = x + 0.5 - size / 2, y + 0.5 - size / 2
                d2 = dx * dx + dy * dy
                if d2 > rad * rad:
                    continue
                nx, nz = dx / rad, math.sqrt(max(0.0, 1 - d2 / (rad * rad)))
                lit = nx * lx + nz * lz
                if lit > 0.05:
                    col = (236, 232, 208)
                    for cx, cy, cr in craters:
                        if math.hypot(dx - cx, dy - cy) <= cr:
                            col = (200, 196, 176)
                    if d2 > (rad - 1.2) ** 2:
                        col = (214, 210, 188)
                    img.put(x, y, col + (255,))
                elif lit > -0.05:
                    img.put(x, y, (150, 150, 140, 200))
                else:
                    # the dark side: only a faint disc against the sky
                    img.put(x, y, (70, 76, 100, 60))
        img.save('sky/moon_%d.png' % phase)


def sky_stars():
    img = Img(30, 5)
    def star(f, pts):
        for (x, y), c in pts:
            img.put(f * 5 + x, y, c)
    star(0, [((2, 2), (200, 200, 215, 200))])
    star(1, [((2, 2), (255, 255, 255, 255))])
    star(2, [((2, 2), (255, 250, 225, 255))] + [((2 + dx, 2 + dy), (255, 250, 225, 90))
                                                  for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))])
    star(3, [((2, 2), (255, 255, 255, 255))] + [((2 + dx, 2 + dy), (190, 205, 255, 160))
                                                  for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))])
    star(4, [((2, 2), (255, 244, 205, 255))] +
         [((2 + dx, 2 + dy), (255, 236, 190, 200)) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))] +
         [((2 + dx, 2 + dy), (255, 236, 190, 80)) for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2))])
    star(5, [((2, 2), (255, 255, 255, 255))] +
         [((2 + dx, 2 + dy), (225, 235, 255, 210)) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))] +
         [((2 + dx, 2 + dy), (225, 235, 255, 110)) for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2))] +
         [((2 + dx, 2 + dy), (225, 235, 255, 60)) for dx, dy in ((1, 1), (-1, 1), (1, -1), (-1, -1))])
    img.save('sky/stars.png')


def tile_noise(w, h, cx, cy, seed):
    """smooth noise that repeats across the picture's edges"""
    import random
    rnd = random.Random(seed)
    grid = [[rnd.random() for _ in range(cx)] for _ in range(cy)]
    out = [0.0] * (w * h)
    for y in range(h):
        fy = y * cy / h
        iy = int(fy)
        ty = fy - iy
        ty = ty * ty * (3 - 2 * ty)
        r0, r1 = grid[iy % cy], grid[(iy + 1) % cy]
        for x in range(w):
            fx = x * cx / w
            ix = int(fx)
            tx = fx - ix
            tx = tx * tx * (3 - 2 * tx)
            x0, x1 = ix % cx, (ix + 1) % cx
            a = r0[x0] + (r0[x1] - r0[x0]) * tx
            b = r1[x0] + (r1[x1] - r1[x0]) * tx
            out[y * w + x] = a + (b - a) * ty
    return out


def fbm(w, h, cx, cy, octaves, seed):
    total = [0.0] * (w * h)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        n = tile_noise(w, h, cx << o, cy << o, seed * 31 + o)
        for i in range(w * h):
            total[i] += n[i] * amp
        norm += amp
        amp *= 0.5
    return [t / norm for t in total]


def puffy(name, threshold, seed, light, mid, dark, alpha=255, cells=5):
    """heaps of cloud: lit on one side, shadowed on the other, in a few flat shades"""
    w = h = 256
    d = fbm(w, h, cells, cells, 4, seed)
    img = Img(w, h)
    for y in range(h):
        for x in range(w):
            v = d[y * w + x]
            if v < threshold:
                continue
            up = d[((y - 4) % h) * w + x]
            down = d[((y + 4) % h) * w + x]
            if down < threshold + 0.01:
                col = dark
            elif up < v - 0.02 or v - threshold > 0.12:
                col = light
            else:
                col = mid
            a = alpha if v - threshold > 0.015 else alpha * 2 // 3
            img.put(x, y, col + (a,))
    img.save(name)


def wisps(name, seed):
    """long thin streaks, high up"""
    w = h = 256
    d = fbm(w, h, 2, 16, 3, seed)
    m = fbm(w, h, 4, 4, 2, seed + 7)
    img = Img(w, h)
    for y in range(h):
        for x in range(w):
            v = d[y * w + x] * 0.7 + m[y * w + x] * 0.3
            if v < 0.56:
                continue
            a = 70 if v < 0.6 else 120 if v < 0.65 else 160
            img.put(x, y, (246, 248, 252, a))
    img.save(name)


def sheet(name, seed, base, blot, alpha_lo, alpha_hi, cells=6, ripple=0.0):
    """a cover over the whole sky, thicker and thinner in places"""
    w = h = 256
    d = fbm(w, h, cells, cells, 4, seed)
    img = Img(w, h)
    for y in range(h):
        for x in range(w):
            v = d[y * w + x]
            if ripple:
                v += ripple * math.sin((x + y * 0.3) * 2 * math.pi / 16)
            col = blot if v > 0.58 else base
            a = alpha_lo if v < 0.45 else (alpha_lo + alpha_hi) // 2 if v < 0.55 else alpha_hi
            img.put(x, y, col + (a,))
    img.save(name)


def sky_clouds():
    puffy('sky/clouds_cumulus.png', 0.6, 1, (252, 252, 254), (228, 231, 238), (182, 188, 202))
    puffy('sky/clouds_cumulus_many.png', 0.52, 2, (250, 250, 252), (224, 227, 234), (176, 182, 196))
    puffy('sky/clouds_nimbus.png', 0.36, 3, (150, 154, 162), (120, 124, 134), (88, 92, 102), 248, cells=4)
    wisps('sky/clouds_cirrus.png', 4)
    sheet('sky/clouds_altostratus.png', 5, (214, 218, 226), (196, 200, 210), 90, 170, ripple=0.04)
    sheet('sky/clouds_stratus.png', 6, (170, 174, 182), (150, 154, 162), 228, 250)


def sky_drops():
    rain = Img(2, 12)
    for y in range(12):
        a = 60 + 160 * y // 11
        rain.put(0, y, (205, 218, 236, a))
        rain.put(1, y, (205, 218, 236, a * 2 // 5))
    rain.save('sky/rain.png')
    snow = Img(15, 5)
    white = (245, 248, 255)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            snow.put(2 + dx, 2 + dy, white + (255 if dx == 0 and dy == 0 else 150,))
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        snow.put(7 + dx, 2 + dy, white + (255,))
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0), (0, 2), (0, -2)):
        snow.put(12 + dx, 2 + dy, white + (255 if abs(dx) + abs(dy) < 2 else 160,))
    for dx, dy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
        snow.put(12 + dx, 2 + dy, white + (110,))
    snow.save('sky/snow.png')


sky_sun()
sky_moons()
sky_stars()
sky_clouds()
sky_drops()

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
                    It is drawn 1 1/3 times as wide as the picture.
compass_frame.png   the frame over the strip; transparent where the strip shows through
                    It is drawn 2 2/3 times as wide: the 8 pixels at each end and the
                    16 in the middle stay as they are, the rest is stretched.
wound_bruise.png    marks on your own paperdoll (bottom left) where a body part is bruised or cut
wound_cut.png
minimap_arrow.png   the arrow on the minimap that shows the way you face. The top of the picture is
                    forward; it is turned for the other directions and drawn with its own
                    transparency. Square; any size.
struck_hit.png      the mark that points from the pointer at whoever struck at you: one for a strike
struck_miss.png     that wounded you, one for a miss. The top of the picture is the end towards the
                    attacker; it is turned to point at them and drawn about a tenth of the window
                    high, its bottom a little way from the pointer
shot_quick.png      the two buttons at the pointer when you click a creature with Shoot: Quick shot
shot_aimed.png      (drawn just above the pointer) and Aimed shot (just below). The words are part of
                    the picture. Made for interface cells of 8 x 12 pixels (96 x 24 is 12 cells by 2);
                    drawn larger when the interface is.
throw_quick.png     the same two buttons for Throw
throw_aimed.png
strike_quick.png    the same two buttons for Strike: Quick strike hits at once, Aimed strike goes on
strike_aimed.png    to the choice of where to hit
dodge.png           a third button under those two while someone is striking at you: Dodge
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
dolls/NAME.png      a paperdoll for the "aim for" menu
dolls/NAME.txt      where each body part is on NAME.png (see the comments inside)

tools/make_media.py in the mod's source draws any default picture that is missing
(it never replaces a file that is there). Paperdolls are made by hand.
""")
print('done')
