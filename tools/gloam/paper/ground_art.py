"""苔径留灯：原生网格上的林道、折线浅池、低植物与原创 PixelLab 地面笔触。

Rebuild from the repository root with the existing uv-managed environment:
  uv run --project tools/gloam/terrain --no-sync python tools/gloam/paper/ground_art.py

The HD-guided composition study is a visual reference only, never texture input.
Only small native PixelLab moss and dirt brush marks from woodland-inks/ transfer.
Path control points and three shallow drainage locations come from the shared map.
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
    "ink": "#192236", "deep": "#253b3c", "grass": "#344b42",
    "leaf": "#568367", "leaf_light": "#83a57b", "tip": "#bac38e",
    "stone_dark": "#454754", "stone": "#626673", "stone_light": "#878a91",
    "stone_tip": "#b0b4a4", "soil_dark": "#3e3541", "soil": "#62535a",
    "soil_light": "#92776a", "sand": "#b99b71", "rust": "#974f4a",
    "cap": "#cd7450", "petal": "#e8a25e", "cream": "#ffe7aa",
    "turf_shade": "#293d38", "turf_light": "#405849",
    "duff": "#4b443e", "duff_light": "#625044", "soil_worn": "#716163",
    "moss": "#647e50", "water": "#273642", "water_light": "#455766",
    "water_deep": "#202c37", "water_glint": "#697a86", "lilac": "#b0a6c0",
    "sedge": "#738b61", "sedge_light": "#a7b889", "sedge_shade": "#465d46",
    "dry_grass": "#858360", "dry_tip": "#b1ad7c", "leaf_rust": "#855d49",
    "leaf_ochre": "#a18057", "leaf_dark": "#5a433b", "wet_bank": "#30363a",
    "wet_mud": "#424147", "wet_shallow": "#303d47", "amber_water": "#8e754c",
    "moss_dark": "#30433b", "moss_pad": "#3d5143", "moss_tip": "#53684a",
    "olive_dark": "#3b4536", "olive_pad": "#4c5841", "olive_tip": "#64704d",
    "dry_moss": "#5b523e", "dry_moss_tip": "#756746",
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


# Large low-contrast mats establish dark turf, olive moss and rusty leaf duff.
# They are separated by unmarked humus rather than tiled texture or terrazzo.
mat_centres = []
for _ in range(210):
    cx, cy = RNG.randint(-20, MAP_W + 20), RNG.randint(-20, MAP_H + 20)
    x, z = world(cx, cy)
    if path_distance(x, z) < 1.65:
        continue
    if any(math.hypot(cx - px, cy - py) < 42 for px, py in mat_centres):
        continue
    mat_centres.append((cx, cy))
    width, height = RNG.randint(35, 82), RNG.randint(20, 47)
    variant = len(mat_centres)
    color = ("turf_shade", "turf_light", "grass", "duff", "turf_shade")[variant % 5]
    island(cx, cy, width, height, color, variant)
    if variant % 5 == 1:
        island(cx - width * .18, cy + height * .10, width * .47, height * .48,
               "moss", variant + 1)
    elif color == "duff":
        island(cx + width * .12, cy - height * .08, width * .48, height * .44,
               "duff_light", variant + 1)


def moss_bed(u, v, variant, brush):
    """A colony of overlapping low cushions, with quiet soil between colonies."""
    palettes = (("moss_dark", "moss_pad", "moss_tip"),
                ("olive_dark", "olive_pad", "olive_tip"),
                ("duff", "dry_moss", "dry_moss_tip"))
    dark, mid, light = palettes[variant % 3]
    rx, ry = brush.randint(10, 24), brush.randint(5, 11)
    for j in range(brush.randint(8, 18)):
        x = u + round(brush.gauss(0, rx * .43))
        y = v + round(brush.gauss(0, ry * .43))
        w, h = brush.randint(2, 4), brush.randint(1, 2)
        points = [(x - w, y), (x - w + 1, y - h), (x, y - h),
                  (x + 1, y - h - 1), (x + w, y), (x + w - 1, y + 1),
                  (x, y + 1), (x - 1, y + 2)]
        floor.poly(points, dark if j % 4 == 0 else mid, (.1, 1, -.12))
        if j % 3 == 0:
            # A small broken face, never an outline or a lone bright grain.
            floor.line([(x - 1, y - h + 1), (x, y - h), (x + 1, y - h)], light,
                       (-.15, 1, -.18))


# Low moss belongs to chosen damp meadows; rusty and dry-moss beds gather on
# the opposite side. The shared clearing and walking strip remain unpainted.
bed_rng = random.Random(SEED + 53)
for index in range(470):
    z = bed_rng.uniform(-37.5, 13.5)
    side = bed_rng.choice((-1, 1))
    x = path_x(z) + side * bed_rng.uniform(2.1, 11.0)
    if not vegetation_clearance(x, z):
        continue
    u, v = at(x, z)
    moss_bed(u, v, (index % 2) if side > 0 else (1 if index % 3 else 2), bed_rng)


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


# Creeping leaves live in local colonies, not a uniform scatter over every mat.
for _ in range(95):
    u, v = RNG.randrange(MAP_W), RNG.randrange(MAP_H)
    x, z = world(u, v)
    if path_distance(x, z) > 2.5 and vegetation_clearance(x, z):
        for j in range(RNG.choice((1, 2, 3))):
            leaf_cluster(u + j * RNG.randint(7, 12), v + RNG.randint(-4, 4),
                         RNG.choice((-1, 1)), RNG.random() < 0.18)




def trail_edge(side):
    points = []
    # Two-pixel runs give the road an authored stair-step contour at native size.
    shoulders = (0, 0, 1, 1, 0, -1, -1, 0, 1, 0, 0, -1)
    for v in range(-2, MAP_H + 3, 2):
        _, z = world(0, v)
        half_width = 1.17 + (0.18 if -1.6 < z < 2.1 else 0)
        u, _ = at(path_x(z) + side * half_width, z)
        points.append((u + shoulders[(v // 8 + side) % len(shoulders)], v))
    return points


left, right = trail_edge(-1), trail_edge(1)
floor.poly(left + list(reversed(right)), "soil")
# Broken short soil tongues embed the path in turf without a continuous rim.
shoulder_rng = random.Random(SEED + 71)
for side, edge in ((-1, left), (1, right)):
    for u, v in edge[3::8]:
        if shoulder_rng.random() < .58:
            continue
        spread = shoulder_rng.randint(2, 4)
        floor.poly([(u - side * 2, v - 1), (u + side * spread, v - 2),
                    (u + side * (spread + 1), v), (u + side, v + 2)],
                   "soil" if v % 3 else "soil_dark", (.1, 1, .1))

# Faint, unequally spaced wear leaves long stretches of bare walking surface.
for i, z in enumerate((-34.5, -28, -21.8, -17, -12.2, -8.1, -2.0, .5, 3.4, 9.3, 12.7)):
    cx, cy = at(path_x(z) + RNG.choice((-.20, .08, .18)), z)
    island(cx, cy, RNG.randint(17, 26), RNG.randint(7, 14), "soil_worn", i)

def fallen_leaf(u, v, variant=0, damp=False):
    """A folded two-plane leaf, only 3–7 pixels long, with a broken midrib."""
    shapes = (
        ((-3, 0), (-2, -1), (0, -1), (2, -2), (3, 0), (1, 2), (-1, 1)),
        ((-2, 1), (-3, -1), (0, -2), (2, -1), (4, 0), (1, 1), (0, 2)),
        ((-3, -1), (-1, -2), (1, -1), (3, -1), (2, 1), (0, 2), (-1, 0)),
    )
    flip = -1 if variant % 2 else 1
    points = [(u + x * flip, v + y) for x, y in shapes[variant % 3]]
    floor.poly(points, "leaf_dark" if damp else ("leaf_rust" if variant % 3 else "duff_light"),
               (flip * .12, 1, -.12))
    if variant % 4 == 0:
        floor.line([(u - flip, v), (u + flip, v - 1)],
                   "duff_light" if damp else "leaf_ochre", (-.2, 1, -.2))
    return points


def verge_tuft(u, v, variant=0, dry=False):
    """Thin unequal grass bends; dark roots, no repeated outline or V stamp."""
    shade, mid, light = (("duff", "dry_grass", "dry_tip") if dry else
                         ("sedge_shade", "sedge", "sedge_light"))
    # Each crown has a different missing sector, lean, height and strand count.
    strands = (
        ((-6, -4, -8, -2), (-3, -8, -6, -7), (1, -10, 3, -9),
         (4, -6, 7, -4), (5, -3, 9, -2)),
        ((-5, -3, -8, -3), (-2, -6, -4, -5), (1, -9, 4, -7),
         (3, -5, 6, -2)),
        ((-4, -5, -7, -4), (-1, -11, -3, -10), (2, -7, 5, -7),
         (5, -4, 8, -1), (0, -4, -1, -3)),
        ((-6, -2, -9, -1), (-3, -7, -5, -6), (2, -8, 2, -5)),
    )
    flip = -1 if variant % 3 == 1 else 1
    roots = [(u - 4, v + 1), (u, v), (u + 4, v + 1)]
    floor.line(roots, "turf_shade")
    floor.wd.line(roots, fill=(0, 0, 0))
    for j, (bx, by, tx, ty) in enumerate(strands[variant % 4]):
        root = (u + (j % 3 - 1) * 2, v + j % 2)
        bend = (u + bx * flip, v + by + (variant % 3 - 1))
        tip = (u + tx * flip, v + ty + (variant % 3 - 1))
        floor.line([root, bend, tip], shade, (flip * .35, .85, .30))
        floor.wd.line([root, bend, tip], fill=(0, 0, 0))
        # Not every strand receives a highlight, and roots remain quiet.
        if (variant + j) % 4 != 0:
            points = [(round((root[0] + bend[0]) / 2),
                       round((root[1] + bend[1]) / 2)), bend, tip]
            floor.line(points, light if j == variant % len(strands[variant % 4]) else mid,
                       (-flip * .4, .85, -.25))
            floor.wd.line(points, fill=(0, 0, 0))


# Planted verge comes in broken banks, not a continuous decorative path outline.
# The broad clearing around the traveller stays bare; the taller crowns gather
# beside, behind and in front of it instead of competing with its silhouette.
verge_rng = random.Random(SEED + 29)
for side in (-1, 1):
    z = -37.0
    while z < 13.5:
        z += verge_rng.uniform(.65, 1.8)
        if verge_rng.random() < .24:
            continue
        x = path_x(z) + side * verge_rng.uniform(1.45, 2.05)
        if not vegetation_clearance(x, z):
            continue
        for j in range(verge_rng.choice((2, 3, 4))):
            px, pz = x + side * verge_rng.uniform(-.08, .48), z + verge_rng.uniform(-.20, .35)
            if not vegetation_clearance(px, pz) or (-1.0 < px < 2.5 and -.8 < pz < 1.9):
                continue
            u, v = at(px, pz)
            variant = verge_rng.randrange(12)
            verge_tuft(u, v, variant, dry=side < 0 and variant % 4 == 0)

# Outer moss meadows and dry leaf beds have different shapes and density.
for index in range(105):
    u, v = verge_rng.randrange(12, MAP_W - 12), verge_rng.randrange(14, MAP_H - 12)
    x, z = world(u, v)
    if path_distance(x, z) < 2.6 or not vegetation_clearance(x, z):
        continue
    for j in range(verge_rng.choice((1, 2, 3))):
        verge_tuft(u + j * 9, v + verge_rng.randint(-3, 4), index + j,
                   dry=(index % 5 == 0))

# Rusty duff collects in asymmetric coves beside the route and around roots.
for index in range(155):
    z = RNG.uniform(-37, 13)
    x = path_x(z) + RNG.choice((-1, 1)) * RNG.uniform(1.65, 4.9)
    if not vegetation_clearance(x, z):
        continue
    u, v = at(x, z)
    for j in range(RNG.randint(3, 7)):
        fallen_leaf(u + RNG.randint(-12, 12), v + RNG.randint(-6, 6), index + j)
    if index % 8 == 0:
        floor.line([(u - 7, v + 4), (u + 2, v + 1), (u + 8, v + 1)], "leaf_dark")
        floor.line([(u + 2, v + 1), (u + 4, v - 2)], "duff_light")


def pebble(u, v, width, height, variant=0):
    """Small chipped planes partly swallowed by soil, never a raised paver."""
    shapes = (
        ((-4, 0), (-2, -3), (1, -2), (4, -1), (3, 2), (0, 3), (-3, 2)),
        ((-4, -1), (-1, -2), (2, -3), (4, 0), (1, 2), (-2, 1)),
        ((-3, -2), (0, -3), (3, -1), (4, 1), (1, 3), (-2, 2), (-4, 0)),
        ((-4, 0), (-2, -2), (2, -2), (3, 1), (0, 2), (-2, 1)),
    )
    points = [(round(u + x * width / 8), round(v + y * height / 6))
              for x, y in shapes[variant % 4]]
    floor.poly(points, "stone_dark", (.15, .95, .16), rough=218)
    floor.poly([points[0], points[1], points[2], (u + 1, v), (u - 2, v + 1)],
               "stone" if variant % 3 else "stone_light", (-.25, 1, -.18), rough=212)
    # Soil clips a different corner on each chip; no full dark cast-shadow rim.
    floor.line(points[-2:], "soil_dark" if variant % 2 else "soil", (0, 1, 0))
    if variant % 5 == 1:
        floor.line([(u - 1, v - 1), (u + 1, v - 1)], "stone_light", (-.2, 1, -.3))


# Fractured local chip families replace the previous oversized rectangular slabs.
for group, (z, pieces) in enumerate((
    (-18.2, ((-.47, -.18, 8, 4), (-.12, -.08, 6, 3), (.26, .20, 9, 5), (.49, .37, 5, 3))),
    (-4.5, ((-.52, -.22, 9, 5), (-.12, -.05, 6, 4), (.36, .22, 8, 4), (-.27, .57, 5, 3))),
    (5.7, ((-.42, -.08, 7, 4), (-.10, .22, 5, 3), (.38, .38, 9, 4))),
)):
    for piece, (dx, dz, width, height) in enumerate(pieces):
        u, v = at(path_x(z + dz) + dx, z + dz)
        pebble(u, v, width, height, group * 3 + piece)

# A few tiny half-buried pebbles favor the verge; the central path stays open.
for index in range(42):
    z = RNG.uniform(-37, 13)
    x = path_x(z) + RNG.choice((-1, 1)) * RNG.uniform(.90, 1.58)
    u, v = at(x, z)
    for j in range(RNG.choice((1, 1, 2))):
        pebble(u + j * 6, v + RNG.randint(-2, 2), RNG.randint(3, 5), 3, index + j)


PONDS = (
    # Crooked shallow runnels follow the existing channels. Narrow throats,
    # mud fingers and uneven margins replace the broad concentric platforms.
    ((-4.42, 3.35), (-4.10, 3.13), (-3.84, 3.17), (-3.51, 2.94),
     (-3.17, 3.04), (-2.91, 2.89), (-2.57, 2.96), (-2.29, 2.77),
     (-1.95, 2.74), (-1.58, 2.37), (-1.19, 2.38), (-1.42, 2.73),
     (-1.81, 2.98), (-2.18, 3.09), (-2.45, 3.39), (-2.76, 3.32),
     (-3.04, 3.54), (-3.37, 3.37), (-3.72, 3.64), (-4.08, 3.56)),
    ((.77, 2.70), (.99, 2.46), (1.26, 2.50), (1.55, 2.27),
     (1.87, 2.29), (2.14, 2.18), (2.39, 2.31), (2.66, 2.20),
     (2.95, 2.03), (3.28, 2.16), (3.14, 2.42), (2.83, 2.47),
     (2.62, 2.75), (2.34, 2.69), (2.10, 2.86), (1.78, 2.70),
     (1.48, 2.87), (1.18, 2.79), (.96, 2.92)),
    ((-6.77, -5.75), (-6.49, -5.98), (-6.01, -5.68), (-5.72, -5.61),
     (-5.25, -5.80), (-4.76, -5.75), (-4.61, -5.48), (-5.10, -5.40),
     (-5.49, -5.49), (-5.84, -5.30), (-6.23, -5.45), (-6.59, -5.45)),
)


for pond_index, pond in enumerate(PONDS):
    points = [at(x, z) for x, z in pond]
    # Only the rear cut gets a recessed dark wall, not a bright closed rim.
    floor.poly([(x, y - 1) for x, y in points], "wet_mud", (0, .8, .35), rough=196)
    floor.poly(points, "water_deep", rough=176)
    floor.wd.polygon(points, fill=(255, 0, 0))
    for start in (1, 6, 13):
        floor.line(points[start:start + 2], "wet_bank", (0, .8, .4), rough=187)
        floor.wd.line(points[start:start + 2], fill=(0, 0, 0))
    # A bank notch is soil, not a raised lily pad or turquoise shelf.
    for index in (4, 11, 16) if pond_index < 2 else (3, 8):
        u, v = points[index]
        notch = [(u - 3, v - 1), (u + 2, v), (u + 4, v + 2), (u - 2, v + 1)]
        floor.poly(notch, "wet_mud", (.1, .95, .1), rough=191)
        floor.wd.polygon(notch, fill=(0, 0, 0))

    min_x, max_x = min(p[0] for p in points), max(p[0] for p in points)
    min_y, max_y = min(p[1] for p in points), max(p[1] for p in points)
    # These native one-pixel horizontal glints are disconnected. Two tiny
    # broken amber strokes in the right channel carry B, still attenuated by
    # the real lamp in the shader; no warm color is spread over the whole pool.
    strokes = ((.19, .68, 4), (.34, .40, 8), (.48, .76, 3),
               (.62, .22, 5), (.74, .54, 6), (.86, .72, 3))
    for j, (fy, fx, length) in enumerate(strokes):
        v = round(min_y + (max_y - min_y) * fy)
        spans, run = [], []
        for u in range(min_x + 1, max_x):
            if floor.water.getpixel((u, v))[0]:
                run.append(u)
            elif run:
                spans.append(run)
                run = []
        if run:
            spans.append(run)
        spans = [span for span in spans if len(span) >= length + 2]
        if not spans:
            continue
        span = max(spans, key=len)
        u = min(span[-1] - length, span[round((len(span) - 1) * fx)])
        lamp = pond_index == 1 and j in (1, 4)
        for k in range(length):
            if length > 5 and k == 3:
                continue
            floor.line([(u + k, v), (u + k, v)],
                       "amber_water" if lamp else ("water_glint" if j == 0 else "water_light"),
                       rough=176)
            floor.wd.point((u + k, v), fill=(255, 40 if lamp else 96, 224 if lamp else 0))

    # Damp leaf litter and a few bent grasses sit on the bank, leaving the
    # central dark water and narrow lamp reflections readable.
    for index in (0, 3, 9, 14) if pond_index < 2 else (0, 5, 9):
        u, v = points[index]
        leaf = fallen_leaf(u + (2 if index % 2 else -3), v + 2, index + pond_index, damp=True)
        floor.wd.polygon(leaf, fill=(0, 0, 0))
    for index in (1, 12) if pond_index < 2 else (1,):
        u, v = points[index]
        verge_tuft(u - 3, v + 2, index + pond_index)


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
    # Fine bent sedge and one/two small seed heads. Open gaps let the recessed
    # bank show through instead of making a fence of broad green spearheads.
    for j, (tx, ty) in enumerate(((4, 19), (7, 13 + index), (14, 7 + index),
                                (20, 10), (26, 17 - index), (29, 22))):
        root = (14 + j % 4, 27)
        bend = ((root[0] + tx) // 2, ty - 1)
        plant.line([root, bend, (tx, ty)], "sedge_shade", ((j - 2) * .16, .9, .2), .42)
        if (j + index) % 3:
            plant.line([((root[0] + bend[0]) // 2, (root[1] + bend[1]) // 2),
                        bend, (tx, ty)], "sedge_light" if j == 2 else "sedge",
                       (-.3, .9, -.2), .46)
    for j, (x, y) in enumerate(((12, 12), (23, 17))):
        if index == 1 and j:
            continue
        x += index % 2
        plant.line([(x - 1, 27), (x, y)], "sedge", height=.43)
        plant.poly([(x - 1, y - 3), (x, y - 4), (x + 1, y - 2),
                    (x + 1, y), (x - 1, y)], "leaf_dark", (.3, .8, .2), .58)
        plant.line([(x - 1, y - 2), (x - 1, y - 1)], "leaf_ochre", (-.5, .8, 0), .60)
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
    (-4.43, 3.51, 13), (-1.26, 2.39, 15),
    (.78, 2.87, 14), (3.27, 2.52, 13), (2.94, 2.04, 15),
    (-6.70, -5.90, 14), (-4.62, -5.40, 15),
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
floor.water.crop((cx - 256, cy - 144, cx + 256, cy + 144)).resize(
    (2048, 1152), Image.Resampling.NEAREST).save(HERE / "ground_water_review.png", compress_level=9)
sheet = Image.new("RGBA", atlas.size, (*rgb("deep"), 255))
sheet.alpha_composite(atlas)
sheet.convert("RGB").resize((512, 512), Image.Resampling.NEAREST).save(HERE / "ground_plants_review.png", compress_level=9)
metadata = {
    "名称": "苔径留灯 · 原创16-bit像素地表",
    "seed": SEED, "map_native_pixels": [MAP_W, MAP_H],
    "map_bounds_xz": [[X0, X1], [Z0, Z1]], "atlas_native_pixels": list(atlas.size),
    "palette_srgb": PALETTE, "normal_space": "world XYZ, linear RGB",
    "plant_count": len(plants), "plant_scale": "1.0; no filtered resampling",
    "source": "ground_art.py + ground_inks.py; original clustered ink, planted verge and broken drainage; two native PixelLab brushes in woodland-inks/",
    "preview": "ground_preview.png is native 512×288, ground_review.png is exact nearest 4×",
    "water_data": "linear RGB: binary coverage / isolated sky shards / lamp shard mask",
    "art_direction": "HD-guided composition study viewed only: reference/gloam-comparison/art-direction/hd2d-to-pixel.webp; never sampled or used as a runtime texture",
}
(HERE / "ground_art.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
print(json.dumps({"map": [MAP_W, MAP_H], "atlas": list(atlas.size),
                  "plants": len(plants), "seed": SEED}, ensure_ascii=False))
