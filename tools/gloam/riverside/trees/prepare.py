"""参考条件生成树木 → 二值原生精灵与分区 OpenGL 法线。

uv run --project tools/gloam/terrain tools/gloam/riverside/trees/prepare.py
Only reads retained original trees; never samples landscape pixels into assets.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from shutil import copyfile

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import binary_erosion, binary_fill_holes, gaussian_filter, label

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
OUT = ROOT / "assets/gloam/riverside/trees"
SPECS = {
    "round": {"size": (100, 118),
              "lobes": [(49, 43, 47, 42, 23), (26, 61, 25, 26, 16),
                        (70, 61, 27, 27, 17), (48, 74, 30, 21, 17)]},
    "spruce": {"size": (100, 132),
               "lobes": [(49, 22, 13, 19, 10), (47, 39, 26, 21, 14),
                         (51, 61, 36, 23, 19), (48, 84, 46, 24, 22),
                         (28, 98, 25, 17, 14), (71, 98, 26, 18, 14)]},
    "cypress": {"size": (36, 108),
                "lobes": [(17, 24, 8, 23, 8), (18, 48, 13, 30, 11),
                          (17, 76, 16, 27, 13)]},
}
# Unlit pigments. Values are intentionally brighter than the night screenshot:
# scene ambient/light, not the albedo, is responsible for nighttime exposure.
FOLIAGE = np.array([
    (13, 24, 26), (23, 39, 37), (30, 49, 43), (37, 59, 47),
    (46, 69, 51), (55, 79, 55), (65, 89, 60), (78, 101, 67),
    (91, 112, 75),
], dtype=np.uint8)
WOOD = np.array([(22, 21, 23), (41, 32, 31), (61, 43, 35),
                 (79, 54, 39), (99, 68, 46), (119, 83, 54)], dtype=np.uint8)
INK = np.array((10, 17, 21), dtype=np.uint8)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract_response(name):
    """Retain actual generated image from the tool's plain-text response."""
    dest = HERE / "raw" / f"{name}-original.png"
    if dest.exists():
        return dest
    response = (HERE / "raw" / f"{name}-result.txt").read_text()
    match = re.search(r"^\s+(/\S+\.png)\s+\(", response, re.MULTILINE)
    if match is None:
        raise ValueError(f"No generated image path in {name}-result.txt")
    copyfile(match.group(1), dest)
    return dest


def largest(mask):
    components, count = label(mask)
    if not count:
        raise ValueError("No solid tree pixels")
    return components == (np.bincount(components.ravel())[1:].argmax() + 1)


def native_color(path, size):
    source = np.asarray(Image.open(path).convert("RGBA"))
    rgb = source[:, :, :3].astype(np.int16)
    # Pure/antialiased magenta is never foliage or wood. Some image models
    # return genuine alpha instead of the requested key, so honor both.
    keyed = (rgb[:, :, 0] > rgb[:, :, 1] + 45) & (rgb[:, :, 2] > rgb[:, :, 1] + 30)
    mask = largest((source[:, :, 3] >= 192) & ~keyed)
    mask = binary_fill_holes(mask)
    ys, xs = np.where(mask)
    bounds = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    left, top, right, bottom = bounds
    mask = mask[top:bottom, left:right]
    rgb = source[top:bottom, left:right, :3].astype(np.float32)
    # Premultiplied area sampling prevents key/transparent pixels contaminating
    # edge pigments. Native alpha is thresholded, not antialiased.
    inner = (size[0] - 4, size[1] - 4)
    coverage = np.asarray(Image.fromarray(mask.astype(np.float32)).resize(inner, Image.Resampling.BOX))
    channels = [np.asarray(Image.fromarray(rgb[:, :, c] * mask).resize(inner, Image.Resampling.BOX))
                / np.maximum(coverage, .001) for c in range(3)]
    small = np.stack(channels, axis=-1)
    solid = binary_fill_holes(largest(coverage >= .52))
    # Classify bark by chroma, not brightness; foliage's blue-green shadows
    # remain canopy instead of becoming falsely brown shaded planes.
    wood = (small[:, :, 0] > small[:, :, 1] * 1.10) & (small[:, :, 0] > small[:, :, 2] * 1.08)
    intensity = small.mean(axis=-1)
    # A percentile transfer retains the generated connected painted shapes
    # while removing accidental global exposure and the magenta key fringe.
    foliage_values = intensity[solid & ~wood]
    low, high = np.percentile(foliage_values, [7, 98])
    level = np.clip((intensity - low) / max(high - low, 1), 0, 1)
    fi = np.rint(level * (len(FOLIAGE) - 1)).astype(int)
    wi = np.rint(level * (len(WOOD) - 1)).astype(int)
    color = FOLIAGE[fi].copy()
    color[wood] = WOOD[wi[wood]]
    # Preserve a readable one-native-pixel dark contour even when the source
    # model drew a subpixel line. The original shape itself is never replaced.
    contour = solid & ~binary_erosion(solid)
    color[contour] = INK
    art = np.zeros((size[1], size[0], 4), dtype=np.uint8)
    art[2:-2, 2:-2, :3] = color
    art[2:-2, 2:-2, 3] = solid.astype(np.uint8) * 255
    art[art[:, :, 3] == 0, :3] = 0
    bark = np.zeros(art.shape[:2], bool)
    bark[2:-2, 2:-2] = wood & solid
    return art, bark, bounds, (int(low), int(high))


def normal_map(art, bark, spec):
    h, w = art.shape[:2]
    yy, xx = np.mgrid[:h, :w]
    heights = []
    for cx, cy, rx, ry, depth in spec["lobes"]:
        radius = ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2
        heights.append(depth * np.sqrt(np.clip(1 - radius, 0, 1)))
    # A smooth upper envelope avoids seams while preserving separate rounded
    # crown/branch-tier volumes. It is geometry, not a brightness-to-normal hack.
    canopy = gaussian_filter(np.max(heights, axis=0), 1.5)
    dy, dx = np.gradient(canopy)
    normals = np.stack((-dx, dy, np.ones_like(xx)), axis=-1)
    # Trunk's own row-local cylindrical surface is independent of leaf lobes.
    for y in range(h):
        columns = np.where(bark[y])[0]
        if len(columns) < 2:
            continue
        center = (columns[0] + columns[-1]) / 2
        radius = max((columns[-1] - columns[0]) / 2, 2)
        nx = np.clip((xx[y] - center) / radius, -.85, .85)
        trunk = np.stack((nx, np.full(w, .08), np.sqrt(1 - nx ** 2)), axis=-1)
        normals[y, bark[y]] = trunk[bark[y]]
    normals /= np.maximum(np.linalg.norm(normals, axis=-1, keepdims=True), .001)
    data = np.zeros_like(art)
    data[:, :, :3] = np.rint((normals * .5 + .5) * 255).astype(np.uint8)
    data[:, :, 3] = art[:, :, 3]
    data[data[:, :, 3] == 0, :3] = (128, 128, 255)
    return data


def lit(art, data, direction):
    n = data[:, :, :3].astype(float) / 255 * 2 - 1
    light = np.array(direction, dtype=float)
    light /= np.linalg.norm(light)
    # Source-only diagnostic, not a fake Bevy verification screenshot.
    shade = .30 + .93 * np.maximum((n * light).sum(axis=-1), 0)
    result = art.copy()
    result[:, :, :3] = np.clip(art[:, :, :3] * shade[:, :, None], 0, 255).astype(np.uint8)
    return Image.fromarray(result)


def make_review(results):
    sheet = Image.new("RGB", (1500, 1060), (30, 35, 45))
    draw = ImageDraw.Draw(sheet)
    labels = ["UNLIT ALBEDO / 2x", "NORMAL +Y UP / 2x", "LEFT WHITE / 2x", "RIGHT WHITE / 2x"]
    for col, text in enumerate(labels):
        draw.text((20 + col * 375, 12), text, fill=(220, 227, 224))
    for row, (name, art, normals) in enumerate(results):
        y = 42 + row * 330
        draw.text((15, y), f"{name}: {art.shape[1]}x{art.shape[0]}", fill=(220, 227, 224))
        pictures = [Image.fromarray(art), Image.fromarray(normals),
                    lit(art, normals, (-.8, .25, .7)), lit(art, normals, (.8, .25, .7))]
        # 2x for tall assets: all previews use a consistent native scale.
        for col, picture in enumerate(pictures):
            large = picture.resize((picture.width * 2, picture.height * 2), Image.Resampling.NEAREST)
            sheet.paste(large, (col * 375 + (375 - large.width) // 2, y + 24), large)
    sheet.save(HERE / "review.png")
    # Real native scene scale next to the approved entire reference; this is
    # explicitly an asset comparison board, never a runtime screenshot.
    native = Image.new("RGBA", (432, 288), (26, 39, 40, 255))
    for x, (name, art, _) in zip((12, 150, 303), results):
        native.alpha_composite(Image.fromarray(art), (x, 190 - art.shape[0]))
    board = Image.new("RGB", (864, 310), (23, 29, 37))
    ref = Image.open(HERE / "input/riverside-study-02.webp").convert("RGB")
    ref = ref.resize((432, 288), Image.Resampling.LANCZOS)
    board.paste(ref, (0, 22))
    board.paste(native.convert("RGB"), (432, 22))
    ImageDraw.Draw(board).text((6, 5), "APPROVED REFERENCE | NEW UNLIT NATIVE ASSETS (not runtime)", fill="white")
    board.save(HERE / "native-comparison.png")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    entries, results, records = [], [], []
    for name, spec in SPECS.items():
        original = extract_response(name)
        art, bark, bounds, levels = native_color(original, spec["size"])
        normals = normal_map(art, bark, spec)
        Image.fromarray(art).save(OUT / f"{name}.png")
        Image.fromarray(normals).save(OUT / f"{name}_normal.png")
        bottom = int(np.where(art[:, :, 3] > 0)[0].max())
        support = np.where(art[bottom, :, 3] > 0)[0]
        anchor = [float(np.median(support) + .5), bottom + 1]
        entry = {"name": name, "size_px": list(spec["size"]), "anchor_px": anchor,
                 "image": f"gloam/riverside/trees/{name}.png",
                 "normal": f"gloam/riverside/trees/{name}_normal.png"}
        entries.append(entry)
        results.append((name, art, normals))
        records.append({"name": name, "original": str(original.relative_to(HERE)),
                        "sha256": sha(original), "source_bounds_px": list(bounds),
                        "size_px": list(spec["size"]), "anchor_px": anchor,
                        "source_intensity_range": list(levels), "opaque_pixels": int((art[:, :, 3] > 0).sum()),
                        "palette_size": int(len(np.unique(art[art[:, :, 3] > 0, :3], axis=0))),
                        "alpha_values": [int(x) for x in np.unique(art[:, :, 3])]})
    (OUT / "manifest.json").write_text(json.dumps({"sprites": entries}, indent=2) + "\n")
    (HERE / "processing-report.json").write_text(json.dumps({"sprites": records}, indent=2) + "\n")
    make_review(results)
    print(json.dumps(records, indent=2))


if __name__ == "__main__":
    main()
