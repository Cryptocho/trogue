#!/usr/bin/env python3
"""下载并裁切 Gloam 静态精灵；只处理像素，不重绘、不重采样。

准备：uv venv tools/gloam/sprites/.venv
依赖：uv pip install --python tools/gloam/sprites/.venv/bin/python -r tools/gloam/sprites/requirements.txt
运行：tools/gloam/sprites/.venv/bin/python tools/gloam/sprites/process.py --download
之后重做：tools/gloam/sprites/.venv/bin/python tools/gloam/sprites/process.py
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from urllib.request import urlopen

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[3]
TOOLS = ROOT / "tools/gloam/sprites"
ASSETS = ROOT / "assets/gloam/sprites"
RAW = ASSETS / "raw"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(url: str, path: Path) -> None:
    with urlopen(url, timeout=90) as response:
        data = response.read()
    path.write_bytes(data)


def process(entry: dict) -> dict:
    name = entry["name"]
    source = RAW / entry["raw_image"]
    image = Image.open(source).convert("RGBA")
    alpha = image.getchannel("A").point(lambda value: 255 if value >= 128 else 0)
    bbox = alpha.getbbox()
    if bbox is None:
        raise ValueError(f"{name}: 精灵没有可见像素 / sprite is empty")
    cleaned = Image.new("RGBA", image.size)
    cleaned.paste(image, (0, 0), alpha)
    cleaned.putalpha(alpha)
    cropped = cleaned.crop(bbox)
    target = ASSETS / f"{name}.png"
    cropped.save(target, optimize=True)

    x0, y0, x1, y1 = bbox
    ground_x, ground_y = entry["ground_anchor_raw"]
    ground = [ground_x - x0, ground_y - y0]
    result = {
        "name": name,
        "image": f"gloam/sprites/{name}.png",
        "raw_image": f"gloam/sprites/raw/{entry['raw_image']}",
        "job_id": entry["job_id"],
        "gallery_asset_id": entry["gallery_asset_id"],
        "candidate_index": entry.get("candidate_index", 0),
        "source_sha256": digest(source),
        "image_sha256": digest(target),
        "raw_size_px": list(image.size),
        "crop_xyxy": list(bbox),
        "visible_size_px": list(cropped.size),
        "visible_aspect_width_over_height": cropped.width / cropped.height,
        "ground_anchor_px_top_left": ground,
        "ground_anchor_uv_top_left": [ground[0] / cropped.width, ground[1] / cropped.height],
        "ground_anchor_bevy_center_offset_px": [ground[0] - cropped.width / 2, cropped.height / 2 - ground[1]],
        "recommended_world_height_m": entry["world_height_m"],
        "notes_zh": entry["notes_zh"],
        "cleanup": "alpha threshold >=128, fully transparent RGB cleared, exact visible bounding-box crop; no resampling, palette change or repaint",
    }
    if "raw_normal" in entry:
        normal_source = RAW / entry["raw_normal"]
        normal = Image.open(normal_source).convert("RGBA")
        if normal.size != image.size:
            raise ValueError(f"{name}: 法线尺寸不匹配 / normal size mismatch")
        strength = entry.get("normal_tangent_strength", 0.7)
        normal_pixels = []
        for (red, green, blue, _), visible in zip(normal.get_flattened_data(), alpha.get_flattened_data()):
            if not visible:
                normal_pixels.append((128, 128, 255, 0))
                continue
            nx = (red / 127.5 - 1) * strength
            ny = (green / 127.5 - 1) * strength
            nz = blue / 127.5 - 1
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            if length == 0:
                raise ValueError(f"{name}: 可见像素法线为零 / visible zero normal")
            normal_pixels.append(tuple(round((value / length + 1) * 127.5) for value in (nx, ny, nz)) + (255,))
        normal.putdata(normal_pixels)
        normal.crop(bbox).save(ASSETS / f"{name}_normal.png", optimize=True)
        result.update({
            "normal": f"gloam/sprites/{name}_normal.png",
            "normal_job_id": entry["normal_job_id"],
            "normal_convention": "OpenGL: tangent +X screen-right, +Y screen-up, +Z toward viewer; linear texture",
            "normal_tangent_strength": strength,
            "normal_source_sha256": digest(normal_source),
        })
    print(f"{name}: {cropped.width}×{cropped.height}, 基点 / ground = {ground}, 原图裁切 / crop = {bbox}")
    return result


def review_sheet(entries: list[dict]) -> None:
    cell_w, cell_h = 352, 352
    sheet = Image.new("RGB", (cell_w * len(entries), cell_h), "#383d3d")
    draw = ImageDraw.Draw(sheet)
    for index, entry in enumerate(entries):
        image = Image.open(ASSETS / f"{entry['name']}.png").convert("RGBA")
        scale = min((cell_w - 20) // image.width, (cell_h - 44) // image.height)
        scale = max(scale, 1)
        preview = image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
        x = index * cell_w + (cell_w - preview.width) // 2
        y = cell_h - 24 - preview.height
        sheet.paste(preview, (x, y), preview)
        draw.text((index * cell_w + 12, 8), f"{entry['name']} {image.width}x{image.height}", fill="#deded2")
        draw.line((index * cell_w + 8, cell_h - 24, (index + 1) * cell_w - 8, cell_h - 24), fill="#798c7a")
    sheet.save(TOOLS / "review_sheet.png")


def main() -> None:
    parser = argparse.ArgumentParser(description="Gloam 原创静态精灵加工 / reproducible sprite processing")
    parser.add_argument("--download", action="store_true", help="从已验收的 PixelLab 导出 URL 下载原图和法线")
    args = parser.parse_args()
    source_manifest = json.loads((TOOLS / "sources.json").read_text(encoding="utf-8"))
    entries = source_manifest["sprites"]
    RAW.mkdir(parents=True, exist_ok=True)
    if args.download:
        for entry in entries:
            download(entry["download_url"], RAW / entry["raw_image"])
            if "normal_download_url" in entry:
                download(entry["normal_download_url"], RAW / entry["raw_normal"])
    processed = [process(entry) for entry in entries]
    manifest = {
        "version": 1,
        "camera": "fixed three-quarter view, approximately 35 degrees down from horizontal",
        "pixel_sampling": "nearest; RGBA alpha-cutout; one static frame per asset",
        "coordinates": "crop and anchors use top-left image coordinates; y increases down; ground anchor may be at the bottom edge y=height",
        "reference_policy_zh": "原创 PixelLab 生成；Rogue: Genesia 参考图未作为贴图或生成输入。",
        "sources": "tools/gloam/sprites/sources.json",
        "sprites": processed,
    }
    (ASSETS / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    review_sheet(entries)


if __name__ == "__main__":
    main()
