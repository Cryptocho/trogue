"""Original 2D undergrowth and water masks for 苔径留灯; no scene render/3D input.

Rebuild from the repository root (the existing uv-managed terrain environment):
  uv run --project tools/gloam/terrain --no-sync python tools/gloam/paper/ground_art.py
Dependencies are declared in tools/gloam/terrain/pyproject.toml.

All silhouettes are painted with two-dimensional Bezier ribbons and pinnate leaf
outlines, with independently painted world-space normals and virtual light height.
The atlas has 24 unique sedge/grass tufts, 16 ferns and 16 curled litter clusters.
The world placement and drainage outline reuse terrain_field.py/parameters.json.
Albedo is unlit sRGB; data RGB is linear world normal, A is height / 0.65 m.
No screenshot, Blender scene, or external vegetation image enters these assets.
"""
from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/gloam/terrain"))
from terrain_field import PARAMS, path_x, path_distance, vegetation_clearance, wetness

OUT = ROOT / "assets/gloam/paper"
OUT.mkdir(parents=True, exist_ok=True)
HERE = Path(__file__).resolve().parent
SEED = PARAMS["seed"] + 27031
CELL, COLS, SUPER = 256, 8, 2
SCALE = CELL * SUPER / 1.6
PITCH, UPRIGHT, MAX_HEIGHT = 0.568, 0.823, 0.65
# Each card is 1.6 x 1.28 screen metres, root 0.30 m above its lower edge.
WIDTH, HEIGHT, FOOT = 1.6, 1.28, 0.30
atlas = Image.new("RGBA", (CELL * COLS, CELL * COLS))
data_atlas = Image.new("RGBA", atlas.size, (128, 255, 128, 0))


def unit(v):
    length = math.sqrt(sum(x * x for x in v))
    return tuple(x / length for x in v)


def lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def normal_color(n, height):
    n = unit(n)
    return tuple(round((c * 0.5 + 0.5) * 255) for c in n) + (
        round(max(0, min(1, height / MAX_HEIGHT)) * 255),
    )


def tint(c, factor):
    return tuple(max(0, min(255, round(x * factor))) for x in c)


class Painting:
    def __init__(self):
        self.color = Image.new("RGBA", (CELL * SUPER, CELL * SUPER))
        self.data = Image.new("RGBA", self.color.size, (128, 255, 128, 0))
        self.cd = ImageDraw.Draw(self.color)
        self.nd = ImageDraw.Draw(self.data)

    @staticmethod
    def pixel(p):
        return ((p[0] / WIDTH + 0.5) * CELL * SUPER,
                (1 - (p[1] + FOOT) / HEIGHT) * CELL * SUPER)

    def polygon(self, points, color, n, height):
        points = [self.pixel(p) for p in points]
        self.cd.polygon(points, fill=(*color, 255))
        self.nd.polygon(points, fill=normal_color(n, height))

    def line(self, points, color, n, height, width):
        points = [self.pixel(p) for p in points]
        width = max(1, round(width * SCALE))
        self.cd.line(points, fill=(*color, 255), width=width, joint="curve")
        self.nd.line(points, fill=normal_color(n, height), width=width, joint="curve")

    def finish(self):
        color = self.color.resize((CELL, CELL), Image.Resampling.LANCZOS)
        # Data A is light height, not opacity: never premultiply it into normals.
        data = Image.merge("RGBA", [c.resize((CELL, CELL), Image.Resampling.BOX)
                                    for c in self.data.split()])
        return color, data


def bezier(points, t):
    a, b, c, d = points
    return tuple((1-t)**3*a[k] + 3*(1-t)**2*t*b[k] +
                 3*(1-t)*t*t*c[k] + t**3*d[k] for k in range(2))


def blade(painting, rng, base, reach, lift, width, color, angle, dry=False):
    """A curved 2D leaf ribbon with a narrow raised midrib, not a line symbol."""
    end = (base[0] + reach, base[1] + lift * rng.uniform(0.46, 0.81))
    curve = (base, (base[0] + reach*0.05, base[1] + lift*0.88),
             (base[0] + reach*0.68, base[1] + lift*1.18), end)
    count = 23
    centres = [bezier(curve, i/count) for i in range(count+1)]
    # World-space side of the blade fold; Y always means up, never image green.
    side = (math.cos(angle), 0.0, math.sin(angle))
    left, right = [], []
    for i, c in enumerate(centres):
        t = i/count
        prev, nxt = centres[max(0, i-1)], centres[min(count, i+1)]
        dx, dy = nxt[0]-prev[0], nxt[1]-prev[1]
        size = max(0.0001, math.hypot(dx, dy))
        w = width * (0.20 + math.sin(math.pi*t)**0.6) * (1-t)**0.48
        left.append((c[0]-dy/size*w, c[1]+dx/size*w))
        right.append((c[0]+dy/size*w, c[1]-dx/size*w))
    for i in range(count):
        t = (i+0.5)/count
        h = max(0.006, lift*math.sin(t*1.55))
        center_normal = (math.sin(angle)*0.24, 0.93-t*0.14, math.cos(angle)*0.24)
        for edge, sign in ((left, -1), (right, 1)):
            n = tuple(center_normal[k] + side[k]*sign*0.38 for k in range(3))
            fade = 0.79 + 0.22*math.sin(t*2.0)
            c = tint(color, fade*(1.0 if sign < 0 else 0.97))
            if dry and t > 0.68:
                c = tuple(round(v) for v in lerp(c, (139, 122, 65), (t-0.68)*2.2))
            painting.polygon([edge[i], edge[i+1], centres[i+1], centres[i]], c, n, h)
    if width > 0.009:
        painting.line(centres, tint(color, 1.04), (0, 1, 0), lift*0.55, 0.0018)


def grass(index):
    rng = random.Random(SEED + index*117)
    p = Painting()
    palette = [(91, 113, 48), (102, 122, 57), (77, 109, 53),
               (111, 125, 62), (86, 111, 57), (123, 126, 71)]
    fan = rng.uniform(-0.12, 0.12)
    blades = rng.randint(26, 42)
    for i in range(blades):
        angle = rng.uniform(-math.pi, math.pi)
        bx, by = rng.uniform(-0.12, 0.12), rng.uniform(-0.025, 0.035)
        lift = rng.uniform(0.21, 0.53)
        reach = rng.uniform(-0.52, 0.52) + fan
        width = rng.uniform(0.0034, 0.0105)
        color = tint(rng.choice(palette), rng.uniform(0.87, 1.08))
        blade(p, rng, (bx, by), reach, lift, width, color, angle, rng.random() < 0.18)
    # A handful of fallen, almost horizontal old blades break the neat fan shape.
    for i in range(rng.randint(4, 7)):
        blade(p, rng, (rng.uniform(-0.12, 0.12), -0.02), rng.uniform(-0.63, 0.63),
              rng.uniform(0.07, 0.15), 0.003, (111, 100, 60), rng.random()*math.tau, True)
    return p.finish()


def leaflet(p, base, tip, width, color, angle, height, rng):
    """Pinnate leaf: curved central fold and subtly toothed margins."""
    dx, dy = tip[0]-base[0], tip[1]-base[1]
    length = max(0.0001, math.hypot(dx, dy))
    sx, sy = -dy/length, dx/length
    centers, left, right = [], [], []
    for i in range(9):
        t = i/8
        center = (base[0]+dx*t, base[1]+dy*t+0.007*math.sin(t*math.pi))
        w = width * math.sin(t*math.pi)**0.7 * (1 - 0.24*t)
        w *= 0.87 if i % 2 else 1.08
        centers.append(center)
        left.append((center[0]+sx*w, center[1]+sy*w))
        right.append((center[0]-sx*w, center[1]-sy*w))
    fold = (math.cos(angle)*0.40, 0.83, math.sin(angle)*0.40)
    for i in range(8):
        for edge, sign in ((left, -1), (right, 1)):
            n = (fold[0]+sign*math.sin(angle)*0.28, fold[1],
                 fold[2]-sign*math.cos(angle)*0.28)
            p.polygon([centers[i], edge[i], edge[i+1], centers[i+1]],
                      tint(color, 0.96 if sign < 0 else 1.03), n, height)
    p.line(centers, tint(color, 1.12), fold, height+0.004, 0.0014)


def fern(index):
    rng = random.Random(SEED + 9100 + index*339)
    p = Painting()
    fronds = rng.randint(7, 10)
    angles = [i*math.tau/fronds + rng.uniform(-0.18, 0.18) for i in range(fronds)]
    angles.sort(key=lambda angle: math.sin(angle))
    palette = [(52, 93, 53), (60, 104, 53), (65, 107, 57), (58, 96, 47)]
    for angle in angles:
        dx, dz = math.cos(angle), math.sin(angle)
        length, lift = rng.uniform(0.40, 0.64), rng.uniform(0.22, 0.39)
        color = tint(rng.choice(palette), rng.uniform(0.92, 1.08))
        points = []
        for k in range(15):
            t = k/14
            # This is an authored 2D curve, not rendered 3D geometry.
            x = dx*length*t
            y = -dz*length*t*PITCH + lift*math.sin(t*math.pi*0.79)*UPRIGHT
            points.append((x, y))
        p.line(points, tint(color, 1.14), (0, 1, 0), lift*0.45, 0.0030)
        for k in range(1, 14):
            t = k/14
            span = (0.025 + 0.125*math.sin(t*math.pi)**0.9)*(1-0.32*t)
            span *= rng.uniform(0.88, 1.10)
            for sign in (-1, 1):
                # Side leaflets have a forward rake; never identical comb teeth.
                side_x, side_z = -dz*sign, dx*sign
                tip = (points[k][0]+side_x*span+dx*0.025,
                       points[k][1]-(side_z*span+dz*0.025)*PITCH+0.005)
                leaflet(p, points[k], tip, rng.uniform(0.010, 0.016)*(1-0.25*t),
                        color, angle+sign*1.2, lift*math.sin(t*math.pi*0.79), rng)
    # Two unfurled central tips lend the rosette a natural, uneven centre.
    for sign in (-1, 1):
        blade(p, rng, (sign*0.013, 0), sign*0.04, rng.uniform(0.29, 0.36),
              0.005, (80, 112, 54), sign*0.8)
    return p.finish()


def litter(index):
    rng = random.Random(SEED + 19100 + index*339)
    p = Painting()
    palette = [(109, 75, 39), (127, 92, 46), (94, 68, 37), (122, 104, 61), (81, 79, 43)]
    for i in range(rng.randint(10, 21)):
        bx, by = rng.uniform(-0.46, 0.46), rng.uniform(-0.15, 0.13)
        angle = rng.random()*math.tau
        length = rng.uniform(0.045, 0.13)
        end = (bx+math.cos(angle)*length, by+math.sin(angle)*length*PITCH)
        leaflet(p, (bx, by), end, rng.uniform(0.012, 0.032), rng.choice(palette),
                angle, rng.uniform(0.006, 0.026), rng)
    for i in range(7):
        x, y = rng.uniform(-0.45, 0.45), rng.uniform(-0.16, 0.13)
        p.line([(x, y), (x+rng.uniform(-0.12, 0.12), y+rng.uniform(-0.04, 0.04))],
               (98, 80, 48), (0, 1, 0), 0.003, 0.0023)
    return p.finish()


for cell in range(56):
    color, data = grass(cell) if cell < 24 else fern(cell-24) if cell < 40 else litter(cell-40)
    xy = ((cell % COLS)*CELL, (cell // COLS)*CELL)
    atlas.paste(color, xy)
    data_atlas.paste(data, xy)
atlas.save(OUT / "ground_understory.png", compress_level=9)
data_atlas.save(OUT / "ground_understory_data.png", compress_level=9)

# Review source image is not loaded by the game. A neutral background exposes edges.
review = Image.new("RGB", atlas.size, (42, 44, 40))
review.paste(atlas, mask=atlas.getchannel("A"))
review.resize((1536, 1536), Image.Resampling.LANCZOS).save(HERE / "ground_review.png")

# Reuse the original drainage field including its exact sinusoidal irregular banks.
resolution = 2048
x0, x1 = PARAMS["bounds_game_xyz"]["x"]
z0, z1 = PARAMS["bounds_game_xyz"]["z"]
xs = np.linspace(x0, x1, resolution, dtype=np.float32)
water = np.zeros((resolution, resolution), dtype=np.uint8)
for start in range(0, resolution, 64):
    stop = min(resolution, start+64)
    zs = np.linspace(z0, z1, resolution, dtype=np.float32)[start:stop]
    x, z = np.meshgrid(xs, zs)
    field = np.zeros_like(x)
    for channel in PARAMS["wet_channels"]:
        distance = np.full_like(x, 1e9)
        for a, b in zip(channel["points"], channel["points"][1:]):
            dx, dz = b[0]-a[0], b[1]-a[1]
            t = np.clip(((x-a[0])*dx+(z-a[1])*dz)/(dx*dx+dz*dz), 0, 1)
            distance = np.minimum(distance, np.hypot(x-a[0]-t*dx, z-a[1]-t*dz))
        width = channel["half_width"]*(1+0.16*np.sin(8.2*x+2.1*z)+0.11*np.sin(5.8*z-3.3*x))
        field = np.maximum(field, np.clip(1-distance/width, 0, 1))
    # Original water mesh clips channel_value at .31. Preserve that shoreline.
    cover = np.clip((field-0.285)/0.05, 0, 1)
    water[start:stop] = np.uint8(cover*cover*(3-2*cover)*255)
Image.fromarray(water).save(OUT / "ground_water.png", compress_level=9)

rng = random.Random(SEED)
plants = []


def clear(x, z):
    if not vegetation_clearance(x, z) or math.hypot(x-0.55, z-0.55) < 1.23:
        return False
    # Preserve every tall hero silhouette, not merely the actor's root point.
    if -1.1 < x < 2.45 and -0.55 < z < 2.0:
        return False
    return True


def plant(x, z, kind, scale=1):
    if not clear(x, z):
        return
    start, count = ((0, 24), (24, 16), (40, 16))[kind]
    plants.append([round(x, 4), round(z, 4), round(scale, 4), start+rng.randrange(count)])


def colony(cx, cz, radius, count, scale):
    for _ in range(count):
        angle, r = rng.random()*math.tau, radius*math.sqrt(rng.random())
        x, z = cx+math.cos(angle)*r, cz+math.sin(angle)*r*0.62
        if path_distance(x, z) > 0.98:
            plant(x, z, 0, scale*rng.uniform(0.78, 1.12))


# Broken shoulder colonies have deliberate gaps and multiple overlapping growth ages.
for side in (-1, 1):
    for i in range(43):
        z = -19+i*0.66+rng.uniform(-0.2, 0.2)
        if rng.random() < 0.20:
            continue
        x = path_x(z)+side*rng.uniform(1.40, 2.10)
        colony(x, z, rng.uniform(0.35, 0.62), rng.randint(4, 7), rng.uniform(0.68, 0.95))
for _ in range(105):
    x, z = rng.uniform(-13, 13), rng.uniform(-24, 10)
    if path_distance(x, z) > 2.3:
        colony(x, z, rng.uniform(0.45, 0.90), rng.randint(4, 8), rng.uniform(0.70, 1.05))
for x, z, radius in ((2.75, 1.20, 0.43), (3.65, 1.55, 0.48), (1.63, 1.87, 0.34)):
    colony(x, z, radius, 15, 1.0)
for _ in range(145):
    x, z = rng.uniform(-13, 13), rng.uniform(-24, 10)
    if path_distance(x, z) > 1.55:
        plant(x, z, 1, rng.uniform(0.85, 1.25))
# Low litter is on the map plane; taller plants use the exact actor sort key.
for _ in range(330):
    x, z = rng.uniform(-13, 13), rng.uniform(-24, 10)
    if wetness(x, z) < 0.16:
        plant(x, z, 2, rng.uniform(0.65, 1.15))
plants.sort(key=lambda p: p[1])
manifest = {
    "seed": SEED,
    "source": "Original 2D botanical drawing; tools/gloam/paper/ground_art.py",
    "terrain_source": "tools/gloam/terrain/parameters.json + terrain_field.py",
    "atlas_columns": COLS,
    "variants": 56,
    "card_width": WIDTH,
    "card_height": HEIGHT,
    "root_above_bottom": FOOT,
    "max_light_height": MAX_HEIGHT,
    "plants": plants,
}
(OUT / "ground_plants.json").write_text(json.dumps(manifest, ensure_ascii=False, separators=(",", ":"))+"\n")
print(json.dumps({"atlas": list(atlas.size), "plants": len(plants),
                  "grass": sum(p[3] < 24 for p in plants),
                  "ferns": sum(24 <= p[3] < 40 for p in plants),
                  "litter": sum(p[3] >= 40 for p in plants)}, ensure_ascii=False))
