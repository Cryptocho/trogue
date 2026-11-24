"""苔径留灯：原生网格上的林道、折线浅池、低植物与原创 PixelLab 地面笔触。

Rebuild from the repository root with the existing uv-managed environment:
  uv run --project tools/gloam/terrain --no-sync python tools/gloam/paper/ground_art.py

No photograph, HD-2D material or screenshot enters the drawing. The only
texture inputs are the native PixelLab moss and dirt swatches in woodland-inks/.
Path control points and three wetland locations come from the shared map.
Every preview is exported at its native size or enlarged by nearest neighbour.
"""
from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/gloam/terrain"))
from terrain_field import PARAMS, path_x, path_distance, vegetation_clearance
from ground_inks import relief_floor

HERE = Path(__file__).resolve().parent
OUT = ROOT / "assets/gloam/paper"
OUT.mkdir(parents=True, exist_ok=True)
SEED = PARAMS["seed"] + 27032
RNG = random.Random(SEED)
X0, X1 = PARAMS["bounds_game_xyz"]["x"]
Z0, Z1 = PARAMS["bounds_game_xyz"]["z"]
PPM = 25
MAP_W, MAP_H = 1200, 738
CELL, COLS, VARIANTS = 32, 4, 16
CARD, ROOT_PIXELS, MAX_HEIGHT = CELL / PPM, 5, 0.8

PALETTE = {
    "ink": "#192236", "deep": "#22434b", "grass": "#326157",
    "leaf": "#49876a", "leaf_light": "#71a77b", "tip": "#a4c58b",
    "stone_dark": "#39485e", "stone": "#566984", "stone_light": "#8498a4",
    "stone_tip": "#b9c5b4", "soil_dark": "#534356", "soil": "#756052",
    "soil_light": "#998165", "sand": "#b99b71", "rust": "#974f4a",
    "cap": "#cd7450", "petal": "#e8a25e", "cream": "#ffe7aa",
    "turf_shade": "#2b5550", "turf_light": "#3c705d",
    "duff": "#4b5850", "duff_light": "#6a6b53", "soil_worn": "#87715c",
    "moss": "#5b8b69", "water": "#35646c", "water_light": "#4e8183",
    "water_deep": "#294857", "water_glint": "#81aaa9", "lilac": "#b0a6c0",
}


def rgb(color):
    color = PALETTE.get(color, color).lstrip("#")
    return tuple(int(color[i:i + 2], 16) for i in (0, 2, 4))


def normal(n):
    length = math.sqrt(sum(v * v for v in n))
    return tuple(round((v / length * 0.5 + 0.5) * 255) for v in n)


def at(x, z):
    return (round((x - X0) / (X1 - X0) * MAP_W),
            round((z - Z0) / (Z1 - Z0) * MAP_H))


def world(u, v):
    return X0 + u / MAP_W * (X1 - X0), Z0 + v / MAP_H * (Z1 - Z0)


class Surface:
    def __init__(self):
        self.color = Image.new("RGB", (MAP_W, MAP_H), rgb("grass"))
        self.normal = Image.new("RGB", self.color.size, normal((0, 1, 0)))
        self.orm = Image.new("RGB", self.color.size, (255, 225, 0))
        # R: water coverage, G: isolated sky shards, B: lamp shards. No waves.
        self.water = Image.new("RGB", self.color.size, (0, 0, 0))
        self.cd, self.nd, self.od, self.wd = [
            ImageDraw.Draw(image) for image in
            (self.color, self.normal, self.orm, self.water)
        ]

    def poly(self, points, color, n=(0, 1, 0), rough=225):
        points = [(round(x), round(y)) for x, y in points]
        self.cd.polygon(points, fill=rgb(color))
        self.nd.polygon(points, fill=normal(n))
        self.od.polygon(points, fill=(255, rough, 0))

    def line(self, points, color, n=(0, 1, 0), rough=225, width=1):
        points = [(round(x), round(y)) for x, y in points]
        self.cd.line(points, fill=rgb(color), width=width)
        self.nd.line(points, fill=normal(n), width=width)
        self.od.line(points, fill=(255, rough, 0), width=width)


floor = Surface()


def island(cx, cy, width, height, color, variant=0, n=(0, 1, 0)):
    """Concave moss/leaf mats: no long rectangular edge and no scanline bands."""
    shapes = (
        ((0, 42), (8, 36), (6, 21), (17, 19), (25, 4), (38, 7),
         (45, 0), (58, 11), (70, 9), (77, 23), (93, 22), (89, 39),
         (100, 48), (87, 57), (91, 70), (75, 79), (64, 94), (49, 89),
         (38, 100), (29, 81), (13, 82), (15, 66), (3, 58)),
        ((0, 27), (14, 20), (26, 23), (31, 9), (42, 12), (51, 0),
         (62, 12), (60, 30), (79, 27), (87, 37), (100, 43), (93, 57),
         (78, 59), (82, 75), (65, 77), (57, 95), (42, 100), (37, 82),
         (20, 86), (12, 73), (19, 57), (4, 48)),
        ((0, 55), (7, 39), (19, 38), (15, 21), (30, 14), (39, 19),
         (49, 0), (60, 8), (62, 25), (78, 20), (90, 32), (86, 44),
         (100, 58), (89, 69), (73, 67), (76, 83), (59, 88), (48, 100),
         (34, 91), (36, 76), (18, 80), (10, 66)),
    )
    points = [(round(cx + (x - 50) * width / 100),
               round(cy + (y - 50) * height / 100))
              for x, y in shapes[variant % len(shapes)]]
    if variant % 2:
        points = [(round(2 * cx - x), y) for x, y in points]
    floor.poly(points, color, n)
    return points


# The centres do not sit on rows. Irregular adjoining mats are each <100px wide;
# their quiet shared base is more important than covering every pixel in marks.
mat_centres = []
for _ in range(150):
    cx, cy = RNG.randint(-20, MAP_W + 20), RNG.randint(-20, MAP_H + 20)
    if any(math.hypot(cx - x, cy - y) < 47 for x, y in mat_centres):
        continue
    mat_centres.append((cx, cy))
    width, height = RNG.randint(44, 94), RNG.randint(30, 63)
    variant = len(mat_centres)
    color = ("turf_shade", "turf_light", "turf_shade", "duff")[variant % 4]
    points = island(cx, cy, width, height, color, variant)
    if variant % 3 == 0:
        # Only a short local edge is layered; there is no raised turf platform.
        floor.line(points[9:12], "grass" if color == "turf_shade" else "moss", width=2)
    if variant % 4 == 3:
        island(cx - width * .12, cy + height * .08, width * .5, height * .5,
               "duff_light", variant + 1)
        for dx, dy in ((-7, 2), (1, -5), (9, 4)):
            floor.poly([(cx + dx - 2, cy + dy), (cx + dx, cy + dy - 2),
                        (cx + dx + 4, cy + dy - 1), (cx + dx + 2, cy + dy + 1)],
                       "soil_light", (.1, 1, .1))


def leaf_cluster(cx, cy, direction=1, light=False):
    """Five broad pointed leaves on a broken, asymmetric creeping stem."""
    floor.line([(cx - 9 * direction, cy + 3), (cx, cy), (cx + 8 * direction, cy - 3)],
               "deep")
    for dx, dy, side in ((-8, 1, -1), (-3, 0, 1), (1, -2, -1), (5, -2, 1), (8, -4, -1)):
        x, y = cx + dx * direction, cy + dy
        floor.poly([(x - direction * 2, y), (x + direction, y - 3 * side),
                    (x + direction * 5, y - 3 * side), (x + direction * 3, y)],
                   "leaf" if not light else "moss", (direction * 0.25, 1, side * 0.2))
        if (dx + dy) % 3 == 0:
            floor.line([(x + direction, y - side), (x + direction * 3, y - side)],
                       "leaf_light" if light else "turf_light")


# Creeping leaves are small designed clusters separated by broad undisturbed mats.
for _ in range(270):
    u, v = RNG.randrange(MAP_W), RNG.randrange(MAP_H)
    x, z = world(u, v)
    if path_distance(x, z) > 2.05 and vegetation_clearance(x, z):
        leaf_cluster(u, v, RNG.choice((-1, 1)), RNG.random() < 0.22)




def trail_edge(side, extra=0):
    points = []
    # Two-pixel runs give the road an authored stair-step contour at native size.
    shoulders = (0, 0, 1, 1, 0, -1, -1, 0, 1, 0, 0, -1)
    for v in range(-2, MAP_H + 3, 2):
        _, z = world(0, v)
        half_width = 1.17 + (0.18 if -1.6 < z < 2.1 else 0)
        u, _ = at(path_x(z) + side * half_width, z)
        points.append((u + side * extra + shoulders[(v // 8 + side) % len(shoulders)], v))
    return points


left, right = trail_edge(-1), trail_edge(1)
floor.poly(trail_edge(-1, 2) + list(reversed(trail_edge(1, 2))), "turf_shade")
floor.poly(left + list(reversed(right)), "soil")

# Faint, unequally spaced wear leaves long stretches of bare walking surface.
for i, z in enumerate((-34.5, -28, -21.8, -17, -12.2, -8.1, -2.0, .5, 3.4, 9.3, 12.7)):
    cx, cy = at(path_x(z) + RNG.choice((-.20, .08, .18)), z)
    island(cx, cy, RNG.randint(17, 26), RNG.randint(7, 14), "soil_worn", i)
for side, edge in ((-1, left), (1, right)):
    for i in range(0, len(edge) - 7, 5):
        if RNG.random() < .55:
            continue
        run = edge[i:i + RNG.choice((3, 4, 6))]
        floor.line([(x + side, y - 1) for x, y in run],
                   "moss" if i % 3 == 0 else "leaf", (side * -.3, 1, .15), width=2)


def slate(u, v, width, height, variant=0):
    """Chipped shale: three planes, one broken seam, no bevel gradients."""
    w, h = width // 2, height // 2
    points = [(u - w + 2, v - h), (u + w - 3, v - h),
              (u + w, v - h + 2), (u + w - 1, v + h - 1),
              (u + w - 4, v + h + 1), (u - w, v + h), (u - w, v - h + 2)]
    floor.poly([(x, y + 2) for x, y in points], "soil_dark", (0, .5, 1))
    floor.poly(points, "stone", rough=205)
    floor.poly([points[0], points[1], (u + w - 5, v), (u - w + 4, v + 1)],
               "stone_light", (-.12, 1, -.25), rough=205)
    floor.line(points[3:6], "stone_dark", (.15, .6, .8))
    if variant % 3 == 0:
        floor.line([(u - 1, v - h), (u - 2, v), (u + 1, v + 1)], "stone_dark")
    elif variant % 3 == 1:
        floor.line([(u - w + 4, v - h), (u - w + 8, v - h)], "stone_tip")


# Just three broken remnants on the whole map, two within the ordinary frame.
# No aligned rows or repeated paving intervals: the traveller stands on earth.
for group, (z, pieces) in enumerate((
    (-18.2, ((-.42, -.18, 23, 7), (.40, .17, 15, 6))),
    (-4.5, ((-.35, -.20, 24, 7), (.38, .28, 16, 6), (-.13, .64, 9, 5))),
    (5.7, ((-.40, -.08, 18, 6), (.37, .38, 23, 7))),
)):
    for piece, (dx, dz, width, height) in enumerate(pieces):
        u, v = at(path_x(z + dz) + dx, z + dz)
        slate(u, v, width, height, group + piece)
    u, v = at(path_x(z) - .6, z)
    floor.line([(u, v + 3), (u + 4, v + 3)], "moss")

# A few pebble families give the eye scale. Each family has clear shared space.
for index in range(48):
    z = RNG.uniform(-37, 13)
    x = path_x(z) + RNG.choice((-1, 1)) * RNG.uniform(.82, 1.48)
    u, v = at(x, z)
    for j in range(RNG.choice((1, 2, 3))):
        px, py = u + j * 5 + RNG.randint(-1, 1), v + RNG.randint(-2, 2)
        floor.poly([(px, py), (px + 3, py - 1), (px + 5, py + 1), (px + 3, py + 2), (px, py + 1)],
                   "stone_dark" if j else "stone", (.15, 1, .2))
        if j == 0:
            floor.line([(px + 1, py), (px + 3, py)], "stone_light")


PONDS = (
    ((-4.42, 3.1), (-4.2, 2.86), (-3.65, 2.72), (-3.22, 2.55),
     (-2.82, 2.62), (-2.45, 2.75), (-1.94, 2.55), (-1.52, 2.2),
     (-1.12, 2.38), (-1.37, 2.92), (-1.9, 3.28), (-2.36, 3.78),
     (-2.96, 3.76), (-3.20, 3.56), (-3.79, 3.85), (-4.20, 3.61)),
    ((.67, 2.60), (.95, 2.28), (1.47, 2.09), (1.9, 1.96),
     (2.35, 2.12), (2.67, 2.04), (2.97, 1.87), (3.34, 2.15),
     (3.30, 2.62), (2.91, 2.88), (2.61, 3.13), (2.15, 3.15),
     (1.77, 2.97), (1.19, 3.18), (.86, 3.02)),
    ((-6.77, -5.75), (-6.49, -5.98), (-6.01, -5.68), (-5.72, -5.61),
     (-5.25, -5.80), (-4.76, -5.75), (-4.61, -5.48), (-5.10, -5.29),
     (-5.49, -5.32), (-5.84, -5.11), (-6.23, -5.27), (-6.59, -5.45)),
)


def inset(points, amount):
    cx = sum(p[0] for p in points) / len(points)
    cy = sum(p[1] for p in points) / len(points)
    return [(round(cx + (x - cx) * amount), round(cy + (y - cy) * amount)) for x, y in points]


for pond_index, pond in enumerate(PONDS):
    points = [at(x, z) for x, z in pond]
    floor.poly([(x, y + 2) for x, y in points], "soil_dark", (0, .4, 1))
    floor.poly(points, "deep", rough=170)
    inner = inset(points, .90)
    floor.poly(inner, "water", rough=178)
    floor.wd.polygon(inner, fill=(255, 0, 0))
    floor.poly(inset(points, .58), "water_deep", rough=178)
    # Only some shore runs catch light; the top contour is not a closed outline.
    for start, end in ((1, 4), (8, 11)):
        floor.line(points[start:end], "leaf", (0, 1, .2))
    min_x, max_x = min(p[0] for p in inner), max(p[0] for p in inner)
    min_y, max_y = min(p[1] for p in inner), max(p[1] for p in inner)
    # Deliberate horizontal shallow-water brushwork, with only two tiny cold
    # mirror shards. Choose actual interior spans so narrow pools cannot lose
    # every reflection to rejected random samples or surrounding reeds.
    strokes = ((.19, .24, 11), (.30, .68, 9), (.40, .40, 16),
               (.53, .77, 8), (.61, .20, 13), (.72, .52, 12),
               (.82, .70, 8), (.46, .18, 5), (.67, .76, 5))
    for j, (fy, fx, length) in enumerate(strokes):
        v = round(min_y + (max_y - min_y) * fy)
        xs = [u for u in range(min_x + 1, max_x)
              if floor.water.getpixel((u, v))[0]]
        if len(xs) < length + 3:
            continue
        u = min(xs[-1] - length, xs[round((len(xs) - 1) * fx)])
        if all(floor.water.getpixel((px, v))[0] for px in range(u, u + length)):
            cold = j >= 7
            floor.line([(u, v), (u + length - 1, v)],
                       "water_glint" if cold else "water_light", rough=178)
            floor.wd.line([(u, v), (u + length - 1, v)],
                          fill=(255, 128 if cold else 32, 255 if pond_index == 1 and j in (2, 5) else 0))
    # Single-notched lily pads are flat map pixels, not extra sorted draw calls.
    for offset in ((.30, .42), (.71, .61)) if pond_index < 2 else ((.45, .5),):
        u = round(min_x + (max_x - min_x) * offset[0])
        v = round(min_y + (max_y - min_y) * offset[1])
        if floor.water.getpixel((u, v))[0]:
            floor.poly([(u - 4, v), (u - 2, v - 2), (u + 3, v - 2),
                        (u + 5, v), (u + 3, v + 2), (u, v + 2),
                        (u, v), (u - 2, v + 2), (u - 4, v + 1)], "leaf")
            floor.line([(u - 1, v - 1), (u + 2, v - 1)], "leaf_light")
            floor.wd.polygon([(u - 4, v - 2), (u + 5, v - 2), (u + 5, v + 2), (u - 4, v + 2)], fill=(0, 0, 0))


class Plant:
    """Native 32px sprite; the data map is painted alongside each planar leaf."""
    def __init__(self):
        self.color = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
        self.data = Image.new("RGBA", (CELL, CELL), (*normal((0, 1, 0)), 0))
        self.cd, self.nd = ImageDraw.Draw(self.color), ImageDraw.Draw(self.data)

    def poly(self, points, color, n=(0, 1, 0), height=.18):
        points = [(round(x), round(y)) for x, y in points]
        self.cd.polygon(points, fill=(*rgb(color), 255))
        self.nd.polygon(points, fill=(*normal(n), round(height / MAX_HEIGHT * 255)))

    def line(self, points, color, n=(0, 1, 0), height=.18):
        points = [(round(x), round(y)) for x, y in points]
        self.cd.line(points, fill=(*rgb(color), 255))
        self.nd.line(points, fill=(*normal(n), round(height / MAX_HEIGHT * 255)))


def fern(index):
    plant = Plant()
    # Five individually raked fronds with solid triangular leaflet clusters.
    fronds = (
        ((16, 26), (7, 18), (3, 18)),
        ((16, 26), (10, 13), (7, 10)),
        ((16, 26), (17, 11), (20, 8)),
        ((16, 26), (24, 16), (29, 14)),
        ((16, 26), (24, 23), (29, 23)),
    )
    if index % 2:
        fronds = tuple(tuple((31 - x, y + (1 if k == 2 else 0)) for k, (x, y) in enumerate(f)) for f in fronds)
    # The four crowns have distinct height, spread and missing leaflets.
    fronds = tuple(tuple(
        (x + ((index - 1) if k == 2 else 0),
         y + ((f + index) % 3 - 1 if k else 0))
        for k, (x, y) in enumerate(frond))
        for f, frond in enumerate(fronds))
    for f, points in enumerate(fronds):
        base, bend, tip = points
        plant.line(points, "moss", height=.35)
        for k in range(1, 5):
            t = k / 5
            x = base[0] + (bend[0] - base[0]) * t
            y = base[1] + (bend[1] - base[1]) * t
            spread = 3 if k < 4 else 2
            for side in (-1, 1):
                n = (side * .50, .85, (-1 if f % 2 else 1) * .23)
                plant.poly([(x, y), (x + side * spread, y - 2),
                            (x + side * (spread + 1), y - 4), (x + side, y - 2)],
                           "leaf" if side < 0 else "moss", n, .12 + t * .44)
        plant.line([bend, tip], "leaf_light" if f == 2 else "leaf", height=.6)
    plant.poly([(12, 26), (16, 23), (20, 25), (19, 27), (13, 27)], "deep", height=.02)
    plant.line([(15, 25), (16, 20), (18, 18)], "leaf_light", height=.35)
    return plant


def broadleaf(index):
    plant = Plant()
    leaves = ((14, 22, 5, 15), (16, 22, 12, 11 + index), (17, 23, 22 + index, 12),
              (17, 25, 28 - index, 21 - index), (14, 25, 6, 24))
    for i, (bx, by, tx, ty) in enumerate(leaves):
        tx += index % 2
        n = (-.5 if tx < bx else .5, .85, -.1)
        plant.poly([(bx, by), ((bx + tx) // 2 - 3, (by + ty) // 2 - 2),
                    (tx, ty), ((bx + tx) // 2 + 3, (by + ty) // 2 + 1)],
                   "leaf" if i % 2 else "moss", n, .30)
        plant.line([(bx, by), (tx, ty + 1)], "leaf_light", n, .32)
    plant.poly([(12, 26), (15, 23), (20, 26), (18, 27)], "deep", height=.03)
    return plant


def mushrooms(index):
    plant = Plant()
    for j, (x, y, size) in enumerate(((11, 24, 4), (19, 19, 5), (25, 25, 3))):
        if index % 2:
            x = 31 - x
        plant.poly([(x - 1, y - 2), (x + 1, y - 2), (x + 1, y + 2), (x - 1, y + 3)], "sand", (.1, .5, 1), .22)
        plant.poly([(x - size, y - 2), (x - size + 2, y - 5),
                    (x + 1, y - 6), (x + size, y - 3), (x + size, y - 1),
                    (x - size + 1, y - 1)], "cap" if index < 2 else "soil_light", (0, 1, 0), .37)
        plant.line([(x - size + 1, y - 1), (x + size - 1, y - 1)], "rust", (0, .45, 1), .29)
        plant.line([(x - 2, y - 4), (x, y - 4)], "petal" if index < 2 else "stone_tip", (-.3, .9, 0), .40)
    plant.line([(7, 28), (11, 26), (15, 27)], "leaf", height=.05)
    return plant


def flowers(index):
    plant = broadleaf(index)
    for j, (x, y) in enumerate(((10, 14 + index), (17, 10), (24 - index, 16))):
        plant.line([(16, 25), (x, y + 2)], "moss", height=.30)
        plant.poly([(x - 2, y), (x - 1, y - 2), (x + 1, y - 2),
                    (x + 2, y), (x + 1, y + 2), (x - 1, y + 1)],
                   "lilac" if index % 2 else "stone_tip", height=.48)
        plant.line([(x, y), (x + 1, y)], "cream" if j == 1 else "tip", height=.48)
    return plant


def reeds(index):
    plant = Plant()
    # Broad broken leaves and two blunt cattail heads, never uniform V glyphs.
    for j, (tx, ty) in enumerate(((6, 17 + index), (10, 9), (17, 6 - index), (23 + index, 12), (28, 20))):
        bx = 15 + j % 3
        plant.poly([(bx, 27), (tx - 1, ty + 4), (tx, ty), (tx + 1, ty + 3), (bx + 1, 25)],
                   "moss" if j % 2 else "leaf", ((j - 2) * .22, .9, .15), .48)
        if j == 2:
            plant.line([(bx, 24), (tx, ty + 2)], "leaf_light", height=.6)
    for x, y in ((12, 11), (22, 15)):
        x += index % 2
        plant.line([(x, 27), (x, y)], "moss", height=.45)
        plant.poly([(x - 1, y - 4), (x + 1, y - 4), (x + 1, y), (x - 1, y)], "soil", (.3, .8, .2), .65)
        plant.line([(x - 1, y - 4), (x - 1, y - 2)], "sand", (-.5, .8, 0), .67)
    return plant


atlas = Image.new("RGBA", (COLS * CELL, COLS * CELL))
data_atlas = Image.new("RGBA", atlas.size, (*normal((0, 1, 0)), 0))
drawings = [fern(i) for i in range(4)] + [broadleaf(i) for i in range(3)]
drawings += [mushrooms(i) for i in range(3)]
drawings += [flowers(i) for i in range(3)] + [reeds(i) for i in range(3)]
for index, plant in enumerate(drawings):
    xy = (index % COLS * CELL, index // COLS * CELL)
    atlas.paste(plant.color, xy)
    data_atlas.paste(plant.data, xy)

plants = []


def place(x, z, variant, *, aquatic=False):
    if not aquatic and not vegetation_clearance(x, z):
        return
    # Clear space belongs to the traveller, lamp and stone, not more grass.
    if -1.0 < x < 2.5 and -.8 < z < 1.9:
        return
    if math.hypot(x + 3.15, z + .98) < 1.65:
        return
    # Snap the foot itself to the same native map grid, never fractional scaling.
    u, v = at(x, z)
    x, z = world(u, v)
    plants.append([round(x, 4), round(z, 4), variant])


# A small number of botanical cards supplies real depth ordering. Low litter and
# creeping plants are already painted into the single surface draw.
for side in (-1, 1):
    z = -24.0 + RNG.uniform(0, 2)
    while z < 12:
        x = path_x(z) + side * RNG.uniform(1.55, 2.4)
        place(x, z, RNG.randrange(7))
        if RNG.random() < .38:
            place(x + side * .47, z + .33, RNG.choice((1, 4, 6)))
        z += RNG.uniform(1.6, 3.4)
for index in range(56):
    x, z = RNG.uniform(-11, 11), RNG.uniform(-22, 10)
    if path_distance(x, z) > 3:
        place(x, z, index % 7)

# Authored roadside stopping points: mushroom family, moonflowers and pond reeds.
for x, z, variant in (
    (-4.95, .6, 7), (-5.45, .85, 8), (-4.7, 1.3, 1),
    (-4.9, -2.9, 9), (-5.4, -2.55, 4),
    (3.3, .85, 10), (3.88, 1.10, 11), (4.32, 1.45, 0),
    (3.7, -.25, 4), (4.3, -.75, 12),
    (-4.55, 3.30, 13), (-1.25, 2.37, 15),
    (.52, 2.6, 14), (3.72, 3.10, 13), (3.45, 2.05, 15),
    (-6.70, -5.94, 14), (-4.48, -5.40, 15),
    (-5.5, 5.35, 7), (-6.2, 5.35, 0), (4.85, 4.55, 11),
    (-.9, -7.8, 12), (-1.45, -8.0, 10), (4.95, -9.4, 8),
):
    place(x, z, variant, aquatic=variant >= 13)
plants.sort(key=lambda item: item[1])
floor.color, floor.normal = relief_floor(floor.color, floor.normal, rgb)

for image, filename in (
    (floor.color, "ground_albedo.png"), (floor.normal, "ground_normal.png"),
    (floor.orm, "ground_orm.png"), (floor.water, "ground_water.png"),
    (atlas, "ground_understory.png"), (data_atlas, "ground_understory_data.png"),
):
    image.save(OUT / filename, compress_level=9)

manifest = {
    "seed": SEED,
    "source": "原创整数像素画笔 / tools/gloam/paper/ground_art.py",
    "terrain_source": "Only path and wetland placement from tools/gloam/terrain/parameters.json",
    "atlas_columns": COLS, "variants": VARIANTS,
    "card_width": CARD, "card_height": CARD, "root_above_bottom": ROOT_PIXELS / PPM,
    "max_light_height": MAX_HEIGHT, "plants": plants,
}
(OUT / "ground_plants.json").write_text(json.dumps(manifest, ensure_ascii=False, separators=(",", ":")) + "\n")

# Native review is an unlit art composite, never a screenshot or runtime input.
review = floor.color.copy().convert("RGBA")
for x, z, variant in plants:
    u, v = at(x, z)
    review.alpha_composite(drawings[variant].color, (u - CELL // 2, v - CELL + ROOT_PIXELS))
review.convert("RGB").save(HERE / "ground_source.png", compress_level=9)
cx, cy = at(.25, -1.1)
crop = review.crop((cx - 256, cy - 144, cx + 256, cy + 144)).convert("RGB")
crop.save(HERE / "ground_preview.png", compress_level=9)
crop.resize((2048, 1152), Image.Resampling.NEAREST).save(HERE / "ground_review.png", compress_level=9)
sheet = Image.new("RGBA", atlas.size, (*rgb("deep"), 255))
sheet.alpha_composite(atlas)
sheet.convert("RGB").resize((512, 512), Image.Resampling.NEAREST).save(HERE / "ground_plants_review.png", compress_level=9)
metadata = {
    "名称": "苔径留灯 · 原创16-bit像素地表",
    "seed": SEED, "map_native_pixels": [MAP_W, MAP_H],
    "map_bounds_xz": [[X0, X1], [Z0, Z1]], "atlas_native_pixels": list(atlas.size),
    "palette_srgb": PALETTE, "normal_space": "world XYZ, linear RGB",
    "plant_count": len(plants), "plant_scale": "1.0; no filtered resampling",
    "source": "ground_art.py + ground_inks.py; integer brushwork and two native PixelLab drawings in woodland-inks/",
    "preview": "ground_preview.png is native 512×288, ground_review.png is exact nearest 4×",
    "water_data": "linear RGB: binary coverage / isolated sky shards / lamp shard mask",
}
(HERE / "ground_art.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
print(json.dumps({"map": [MAP_W, MAP_H], "atlas": list(atlas.size),
                  "plants": len(plants), "seed": SEED}, ensure_ascii=False))
