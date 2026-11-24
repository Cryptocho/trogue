"""Sparse connected native brush marks, never a full-surface noise texture.

Only two original PixelLab studies supply small ink shapes. Their color, baked
lighting and periodic layouts do not transfer. Authored grass, leaf duff, chips
and drainage from ground_art.py remain untouched.
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SOURCE = Path(__file__).resolve().parent / "woodland-inks"


def marks(name, crop):
    source = Image.open(SOURCE / f"{name}-original.png").convert("RGB").crop(crop)
    source = source.resize((source.width // 2, source.height // 2), Image.Resampling.BOX)
    luminosity = np.array(source) @ np.array([.25, .60, .15])
    bands = np.digitize(luminosity, np.percentile(luminosity, (13, 33, 68, 87)))
    # Connected masses survive; isolated single-pixel terrazzo does not.
    return np.array(Image.fromarray(bands.astype(np.uint8)).filter(ImageFilter.ModeFilter(3)))


def relief_floor(color, normal, colors):
    rng = np.random.default_rng(841779)
    base = np.array(color, dtype=np.int16)
    dst = base.copy()
    data = np.array(normal)
    height, width = base.shape[:2]

    def mask_for(names):
        mask = np.zeros((height, width), bool)
        for name in names:
            mask |= (base == colors(name)).all(axis=2)
        return mask

    turf = mask_for(("grass", "turf_light", "turf_shade", "moss"))
    duff = mask_for(("duff", "duff_light"))
    dirt = mask_for(("soil", "soil_worn"))
    turf_ink = marks("turf", (0, 0, 96, 80))
    soil_ink = marks("soil", (30, 0, 66, 80))
    for tile, terrain, count, low, high in (
        (turf_ink, turf, 1150, (-8, -12, -8), (13, 17, 9)),
        (soil_ink, duff, 160, (-9, -7, -5), (13, 10, 6)),
        (soil_ink, dirt, 330, (-10, -8, -8), (13, 11, 9)),
    ):
        candidates = np.flatnonzero(terrain)
        if not len(candidates):
            continue
        th, tw = tile.shape
        # Nonperiodic islands occupy only a little of each biome. Most soil
        # and turf stays quiet; there is no carpet of evenly spaced flecks.
        for centre in rng.choice(candidates, size=min(count, len(candidates)), replace=False):
            cy, cx = divmod(int(centre), width)
            # Even the clearing keeps a few muted connected loam marks; quiet
            # means lower contrast here, never a large featureless flat plate.
            quiet = ((cx - 614) / 30) ** 2 + ((cy - 547) / 21) ** 2 < 1
            rx, ry = int(rng.integers(7, 17)), int(rng.integers(4, 10))
            x0, x1 = max(0, cx - rx), min(width, cx + rx + 1)
            y0, y1 = max(0, cy - ry), min(height, cy + ry + 1)
            area = np.s_[y0:y1, x0:x1]
            yy, xx = np.ogrid[y0 - cy:y1 - cy, x0 - cx:x1 - cx]
            brush = tile[(yy + int(rng.integers(th))) % th,
                         (xx * int(rng.choice((-1, 1))) + int(rng.integers(tw))) % tw]
            patch = Image.new("1", (x1 - x0, y1 - y0))
            ImageDraw.Draw(patch).polygon(
                [(cx - x0 - rx, cy - y0), (cx - x0 - rx // 2, cy - y0 - ry),
                 (cx - x0 + 2, cy - y0 - ry + 2), (cx - x0 + rx, cy - y0 - ry // 3),
                 (cx - x0 + rx - 2, cy - y0 + ry), (cx - x0 - rx // 3, cy - y0 + ry - 1)],
                fill=1,
            )
            shape = terrain[area] & np.array(patch)
            # Broad source clusters supply both loam short dashes and connected
            # hummocks. The middle band stays bare; minor planes are subdued.
            dark, lit = shape & (brush <= 1), shape & (brush >= 3)
            strength = .45 if quiet else 1.
            for mask, delta, extreme in ((dark, low, 0), (lit, high, 4)):
                amount = np.where(brush[mask] == extreme, strength, strength * .45)
                dst[area][mask] = base[area][mask] + (np.array(delta) * amount[:, None]).astype(np.int16)
            if not quiet:
                data[area][shape & (brush == 0)] = (139, 253, 143)
                data[area][shape & (brush == 4)] = (117, 253, 113)
    return Image.fromarray(np.clip(dst, 0, 255).astype(np.uint8)), Image.fromarray(data)
