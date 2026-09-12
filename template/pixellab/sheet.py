"""PixelLab API v2 spritesheet ZIP -> tro-animations v1."""

from __future__ import annotations

import io
import json
import zipfile
from pathlib import PurePosixPath

from PIL import Image

SHEET_MAX = 4096


def _cell_size(layout: dict) -> tuple[int, int]:
    cell = layout.get("cell_size") or layout.get("cell")
    if isinstance(cell, dict):
        w, h = cell.get("width"), cell.get("height")
    elif isinstance(cell, list) and len(cell) == 2:
        w, h = cell
    else:
        w, h = layout.get("cell_width"), layout.get("cell_height")
    if not isinstance(w, int) or not isinstance(h, int) or w <= 0 or h <= 0:
        raise ValueError("spritesheet layout 缺少合法 cell_size")
    return w, h


def _rows(layout: dict) -> list[dict]:
    rows = layout.get("rows") or layout.get("layout")
    if not isinstance(rows, list):
        raise ValueError("spritesheet layout 缺少 rows 数组")
    return rows


def build(zip_bytes: bytes, name: str, fps: int,
          loop_clips: set[str]) -> tuple[bytes, dict]:
    """保留原始统一网格 PNG，按 layout JSON 生成动画 frame region。"""
    try:
        archive = zipfile.ZipFile(io.BytesIO(zip_bytes))
    except zipfile.BadZipFile as exc:
        raise ValueError(f"{name}: spritesheet 不是合法 ZIP") from exc
    with archive:
        png_names = [n for n in archive.namelist()
                     if PurePosixPath(n).suffix.lower() == ".png"]
        json_names = [n for n in archive.namelist()
                      if PurePosixPath(n).suffix.lower() == ".json"]
        if len(png_names) != 1 or len(json_names) != 1:
            raise ValueError(f"{name}: spritesheet ZIP 必须恰好含一个 PNG 和一个 JSON")
        png = archive.read(png_names[0])
        layout = json.loads(archive.read(json_names[0]).decode("utf-8"))

    with Image.open(io.BytesIO(png)) as image:
        width, height = image.size
    if width > SHEET_MAX or height > SHEET_MAX:
        raise ValueError(f"{name}: spritesheet {width}x{height} 超过 {SHEET_MAX} 上限")
    declared_size = layout.get("sheet_size") or layout.get("image_size")
    if isinstance(declared_size, dict):
        if (declared_size.get("width"), declared_size.get("height")) != (width, height):
            raise ValueError(f"{name}: layout sheet_size 与 PNG 尺寸不一致")
    cell_w, cell_h = _cell_size(layout)
    columns = layout.get("columns", layout.get("column_count"))
    if columns is not None and (not isinstance(columns, int) or columns != width // cell_w):
        raise ValueError(f"{name}: layout columns 与 PNG/cell 尺寸不一致")
    if width % cell_w or height % cell_h:
        raise ValueError(f"{name}: sheet {width}x{height} 不能整除 cell {cell_w}x{cell_h}")

    clips = []
    seen = set()
    for row_index, row in enumerate(_rows(layout)):
        if not isinstance(row, dict):
            raise ValueError(f"{name}: rows[{row_index}] 必须为 object")
        kind = str(row.get("kind", row.get("type", ""))).lower()
        animation = row.get("animation") or row.get("animation_name") or row.get("clip")
        direction = row.get("direction")
        if not animation or kind in {"rotation", "rotations", "idle_rotation"}:
            continue
        clip = f"{animation}_{direction}" if direction else str(animation)
        if clip in seen:
            raise ValueError(f"{name}: clip 名重复 {clip!r}；请隔离 rotation/animation 命名")
        seen.add(clip)
        raw_row = row.get("row", row_index)
        if not isinstance(raw_row, int) or raw_row < 0:
            raise ValueError(f"{name}: {clip} row 非法")
        y = raw_row * cell_h
        if y + cell_h > height:
            raise ValueError(f"{name}: {clip} row 超出 PNG")
        raw_count = row.get("frame_count", row.get("frames", columns or width // cell_w))
        if not isinstance(raw_count, int) or isinstance(raw_count, bool):
            raise ValueError(f"{name}: {clip} frame_count 必须为 int")
        count = raw_count
        if count <= 0 or count > width // cell_w:
            raise ValueError(f"{name}: {clip} frame_count 非法")
        frames = [{"texture": 0, "region": [i * cell_w, y, cell_w, cell_h]}
                  for i in range(count)]
        base_loop = str(animation) in loop_clips
        clips.append({"name": clip, "fps": fps, "loop": base_loop or clip in loop_clips,
                      "frames": frames})
    if not clips:
        raise ValueError(f"{name}: layout 没有可导入的 animation rows")
    doc = {"format": "tro-animations", "version": 1,
           "textures": [f"textures/pixellab/{name}.png"],
           "animations": clips}
    return png, doc
