"""处理独立河岸道具：模型原图 → 二值透明原生像素 → 结构法线。"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / "tools/gloam/riverside/props"
ASSETS = ROOT / "assets/gloam/riverside/props"
SPECS = {
    "lamp": {"fit": (30, 104), "colours": 24, "source": "lamp.webp"},
    "traveler": {"fit": (24, 37), "colours": 24, "source": "traveler.png"},
    "stone": {"fit": (32, 42), "colours": 24, "source": "stone.png"},
}


def extract_responses() -> None:
    """保存原始生成文件；响应文本与请求另行保留。"""
    for name in SPECS:
        destination = SOURCE / "raw" / SPECS[name]["source"]
        if destination.exists():
            continue
        response = (SOURCE / "raw" / f"{name}-response.txt").read_text()
        paths = re.findall(r"(/[^\s\"<>]+\.(?:png|webp))", response)
        paths = [Path(path) for path in paths if Path(path).is_file()]
        if not paths:
            raise ValueError(f"生成响应没有可读取的原图：{name}")
        shutil.copyfile(paths[0], destination)


def key_and_fit(name: str) -> Image.Image:
    original = Image.open(SOURCE / "raw" / SPECS[name]["source"]).convert("RGBA")
    rgb = np.asarray(original)[:, :, :3].astype(np.float32)
    alpha = np.asarray(original)[:, :, 3] > 127
    # Exact magenta matte and its anti-aliased fringe are not material pixels.
    magenta = (rgb[:, :, 0] > rgb[:, :, 1] + 35) & (rgb[:, :, 2] > rgb[:, :, 1] + 35)
    mask = alpha & ~magenta
    labels, count = ndimage.label(mask)
    if count == 0:
        raise ValueError(f"去底后没有主体：{name}")
    areas = np.bincount(labels.ravel())
    areas[0] = 0
    mask = labels == int(areas.argmax())
    ys, xs = np.nonzero(mask)
    bbox = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    mask = mask[bbox[1]:bbox[3], bbox[0]:bbox[2]]
    rgb = rgb[bbox[1]:bbox[3], bbox[0]:bbox[2]]
    fit = SPECS[name]["fit"]
    scale = min(fit[0] / rgb.shape[1], fit[1] / rgb.shape[0])
    size = (max(1, round(rgb.shape[1] * scale)), max(1, round(rgb.shape[0] * scale)))
    # Coverage and RGB are resized independently in premultiplied form. The
    # binary coverage threshold excludes exterior matte, never bakes it in.
    coverage = np.asarray(Image.fromarray(mask.astype(np.float32)).resize(size, Image.Resampling.BOX))
    colours = []
    for channel in range(3):
        premultiplied = Image.fromarray(rgb[:, :, channel] * mask)
        channel_small = np.asarray(premultiplied.resize(size, Image.Resampling.BOX))
        colours.append(channel_small / np.maximum(coverage, 1e-6))
    small = np.stack(colours, axis=2).clip(0, 255).astype(np.uint8)
    opaque = coverage >= 0.50
    # Outside is made black before palette reduction; there is no white matte.
    small[~opaque] = 0
    result_rgb = Image.fromarray(small).quantize(colors=SPECS[name]["colours"], method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")
    result = result_rgb.convert("RGBA")
    result.putalpha(Image.fromarray(opaque.astype(np.uint8) * 255))
    pixels = np.array(result)
    pixels[~opaque, :3] = 0
    result = Image.fromarray(pixels)
    result.save(SOURCE / "review" / f"{name}-keyed.png")
    (SOURCE / "review" / f"{name}-crop.json").write_text(json.dumps({"source_size": original.size, "source_bbox": bbox, "native_size": result.size}, indent=2) + "\n")
    return result


def normal_rgb(vector: tuple[float, float, float]) -> tuple[int, int, int]:
    values = np.asarray(vector, dtype=np.float64)
    values /= np.linalg.norm(values)
    return tuple(np.rint((values * 0.5 + 0.5) * 255).astype(np.uint8))


def hand_normals(name: str, image: Image.Image, spec: dict) -> Image.Image:
    """离散法线来自明确绘制的表面平面，不从颜色亮度推导。"""
    normal = Image.new("RGB", image.size, normal_rgb((0, 0, 1)))
    draw = ImageDraw.Draw(normal)
    for region in spec.get("normal_regions", []):
        colour = normal_rgb(tuple(region["normal"]))
        if "polygon" in region:
            draw.polygon([tuple(point) for point in region["polygon"]], fill=colour)
        elif "ellipse" in region:
            draw.ellipse(tuple(region["ellipse"]), fill=colour)
        else:
            draw.rectangle(tuple(region["rectangle"]), fill=colour)
    normal = normal.convert("RGBA")
    normal.putalpha(image.getchannel("A"))
    data = np.array(normal)
    data[data[:, :, 3] == 0, :3] = (128, 128, 255)
    return Image.fromarray(data)


def emission_mask(image: Image.Image, spec: dict) -> Image.Image:
    mask = Image.new("L", image.size, 0)
    draw = ImageDraw.Draw(mask)
    for region in spec["emission_regions"]:
        draw.polygon([tuple(point) for point in region["polygon"]], fill=region["value"])
    data = np.array(mask)
    pixels = np.asarray(image)
    # Geometric glass polygons are further intersected with actual ochre
    # glass material: never emit from a dark metal edge or outside alpha.
    glass = (pixels[:, :, 0] >= 100) & (pixels[:, :, 1] >= 85) & (pixels[:, :, 2] <= 180)
    data[(pixels[:, :, 3] == 0) | ~glass] = 0
    return Image.fromarray(data)


def review_sheet(images: dict[str, Image.Image], normals: dict[str, Image.Image]) -> None:
    """仅制作离线素材审阅图，不代表游戏运行画面。"""
    sheet = Image.new("RGB", (1100, 760), "#111c29")
    draw = ImageDraw.Draw(sheet)
    draw.text((24, 16), "RIVERSIDE / MODEL-CONDITIONED SPRITES / OFFLINE ASSET REVIEW", fill="#e9e1c6")
    for index, name in enumerate(SPECS):
        x = 30 + index * 360
        image = images[name]
        draw.text((x, 48), f"{name}: {image.width} x {image.height}", fill="#d4d9cd")
        # All three albedos shown at the same integer pixel scale.
        albedo = image.resize((image.width * 4, image.height * 4), Image.Resampling.NEAREST)
        sheet.paste(albedo, (x + 100, 500 - albedo.height), albedo)
        normal = normals[name].resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST)
        sheet.paste(normal, (x + 30, 730 - normal.height), normal)
        draw.text((x, 516), "albedo / binary alpha", fill="#aebbc4")
        draw.text((x + 114, 714), "normal (+Y up)", fill="#aebbc4")
        for bx, background in enumerate(("#e1dcc6", "#304b3d")):
            small = Image.new("RGB", (100, 140), background)
            scale = min(2, 90 // image.width, 130 // image.height)
            thumb = image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
            small.paste(thumb, ((100 - thumb.width) // 2, 135 - thumb.height), thumb)
            sheet.paste(small, (x + 110 + bx * 110, 560))
    sheet.save(SOURCE / "review" / "sprites-review.png")


def lighting_review(images: dict[str, Image.Image], normals: dict[str, Image.Image]) -> None:
    sheet = Image.new("RGB", (1260, 530), "#1d2e28")
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 12), "OFFLINE MATERIAL DIAGNOSTIC ONLY / SAME ALBEDO, OPPOSITE WHITE LIGHTS", fill="#f0e7cf")
    for index, name in enumerate(SPECS):
        rgba = np.asarray(images[name])
        srgb = rgba[:, :, :3] / 255.0
        linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
        normal = np.asarray(normals[name])[:, :, :3] / 255.0 * 2 - 1
        for side, lx in enumerate((-0.85, 0.85)):
            light = np.array((lx, 0.15, 0.51))
            light /= np.linalg.norm(light)
            diffuse = 0.15 + 0.95 * np.maximum(np.sum(normal * light, axis=2), 0)
            lit = np.clip(linear * diffuse[:, :, None], 0, 1)
            rgb = np.where(lit <= 0.0031308, lit * 12.92, 1.055 * lit ** (1 / 2.4) - 0.055)
            output = Image.fromarray(np.concatenate((np.rint(rgb * 255).astype(np.uint8), rgba[:, :, 3:4]), axis=2))
            output = output.resize((output.width * 4, output.height * 4), Image.Resampling.NEAREST)
            x = 20 + index * 420 + side * 200
            sheet.paste(output, (x + 35, 490 - output.height), output)
            draw.text((x, 510), f"{name} / {'LEFT' if side == 0 else 'RIGHT'}", fill="#e3ddc6")
    sheet.save(SOURCE / "review" / "opposite-light-review.png")
    lamp = images["lamp"]
    mask = Image.open(ASSETS / "lamp-emission.png")
    emission = Image.merge("RGBA", (mask, mask, mask, lamp.getchannel("A")))
    card = Image.new("RGB", (430, 900), "#18372c")
    card.paste(lamp.resize((lamp.width * 8, lamp.height * 8), Image.Resampling.NEAREST), (4, 24), lamp.resize((lamp.width * 8, lamp.height * 8), Image.Resampling.NEAREST))
    large = emission.resize((lamp.width * 8, lamp.height * 8), Image.Resampling.NEAREST)
    card.paste(large, (218, 24), large)
    ImageDraw.Draw(card).text((8, 870), "ALBEDO          GLASS-ONLY EMISSION", fill="white")
    card.save(SOURCE / "review" / "lamp-emission-review.png")


def main() -> None:
    parser = argparse.ArgumentParser(description="河岸独立道具素材加工 / offline asset processing")
    parser.add_argument("--extract", action="store_true", help="从已完成响应复制模型原图")
    parser.add_argument("--key-only", action="store_true", help="先输出原生去底候选，以便绘制结构法线")
    args = parser.parse_args()
    if args.extract:
        extract_responses()
    images = {name: key_and_fit(name) for name in SPECS}
    if args.key_only:
        for name, image in images.items():
            print(f"{name}: {image.width}x{image.height}")
        return
    authored = json.loads((SOURCE / "surface-regions.json").read_text())
    normals = {}
    entries = []
    for name, image in images.items():
        spec = authored[name]
        normals[name] = hand_normals(name, image, spec)
        image.save(ASSETS / f"{name}.png")
        normals[name].save(ASSETS / f"{name}-normal.png")
        entry = {
            "name": name,
            "size_px": list(image.size),
            "anchor_px": spec["anchor_px"],
            "image": f"gloam/riverside/props/{name}.png",
            "normal": f"gloam/riverside/props/{name}-normal.png",
        }
        if name == "lamp":
            emission_mask(image, spec).save(ASSETS / "lamp-emission.png")
            entry["emission"] = "gloam/riverside/props/lamp-emission.png"
            entry["emitter_px"] = spec["emitter_px"]
        entries.append(entry)
    (ASSETS / "manifest.json").write_text(json.dumps({"sprites": entries}, ensure_ascii=False, indent=2) + "\n")
    review_sheet(images, normals)
    lighting_review(images, normals)
    report = {}
    for name, image in images.items():
        alpha = np.asarray(image)[:, :, 3]
        report[name] = {
            "size_px": list(image.size),
            "alpha_values": np.unique(alpha).tolist(),
            "opaque_pixel_count": int((alpha == 255).sum()),
            "rgb_colour_count": int(len(np.unique(np.asarray(image)[:, :, :3][alpha == 255], axis=0))),
            "sha256": hashlib.sha256((ASSETS / f"{name}.png").read_bytes()).hexdigest(),
        }
    (SOURCE / "processing-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
