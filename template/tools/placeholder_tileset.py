#!/usr/bin/env python3
"""placeholder_tileset.py —— 本地生成「已标注」的占位 tro-tileset v2 + 贴图。

用途：正式瓦片集尚未就位时，让 Agent 立刻拿到一份 autotile 可用的资产——
16 个 4 角组合 × 2 个地形基色 = 32 tile，peering_bits / terrain 由构造保证自洽，
不需要任何外部标注。像素内容是纯色块，仅用于占位。

产物（CWD = 项目根）：
  assets/tilesets/<name>.json
  assets/textures/<name>.png

标注约定（与引擎 pick_tile 的 corners 模式、Godot 邻位语义一致）：
  - 角组合编码 c = tl*8 + tr*4 + br*2 + bl；
  - tile id = terrain*16 + c；terrain = **该 tile 所代表格子的地形**，
    peering_bits 每个角位 = 该方向邻格的地形序号（0 = lower，1 = upper）；
  - 两层池各含全部 16 个角组合 → 任意 pattern 都能在本格地形池内精确命中
    （pick_tile 零降级）；
  - 贴图：底色 = terrain 色，异地形角画 1/4 边长的角块 → 视觉与标注同源。
  terrain 0/1 的名称与颜色可用参数覆盖。

确定性：同参数在**同一环境**重跑产物逐字节一致（无时间戳、无随机、PNG 压缩
参数固定；zlib 版本差异可能改变压缩后的字节，但像素不变）。
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import zlib

# 角组合位序：(位权, peering_bits 键, 角块在 tile 内的 (dx, dy) 方向)
CORNERS = (
    (8, "top_left_corner", (0, 0)),
    (4, "top_right_corner", (1, 0)),
    (2, "bottom_right_corner", (1, 1)),
    (1, "bottom_left_corner", (0, 1)),
)

COMBO_COUNT = 16
TERRAIN_COUNT = 2
NOTCH_DIV = 4  # 角块边长 = tile_size / NOTCH_DIV
TILE_SIZE_MAX = 256  # 贴图为 16×2 个 tile，256 时约 4096×512（防误传巨大值撑爆内存）


def parse_color(text: str) -> tuple[int, int, int]:
    if len(text) != 7 or text[0] != "#":
        raise ValueError(f"颜色须为 #rrggbb（实际 {text!r}）")
    try:
        return tuple(int(text[i : i + 2], 16) for i in (1, 3, 5))  # type: ignore[return-value]
    except ValueError as exc:
        raise ValueError(f"颜色非法 {text!r}") from exc


def png_bytes(pixels: list[list[tuple[int, int, int]]]) -> bytes:
    """RGB 像素矩阵 → PNG（8 位 RGB，逐行 filter 0）。stdlib 实现，无第三方依赖。"""
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    raw = bytearray()
    for row in pixels:
        raw.append(0)  # filter type 0（None）
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8bpp RGB
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def tile_pixels(terrain: int, c: int, tile_size: int, colors: list[tuple[int, int, int]]
                ) -> list[list[tuple[int, int, int]]]:
    """单个 tile 的像素：底色 = 本地形色，异地形角画角块（与 peering_bits 同源）。"""
    notch = tile_size // NOTCH_DIV
    rows = [[colors[terrain]] * tile_size for _ in range(tile_size)]
    for weight, _key, (nx, ny) in CORNERS:
        corner_terrain = 1 if (c & weight) else 0
        if corner_terrain == terrain:
            continue  # 同地形角块 = 底色，无需绘制
        x0 = 0 if nx == 0 else tile_size - notch
        y0 = 0 if ny == 0 else tile_size - notch
        for y in range(y0, y0 + notch):
            for x in range(x0, x0 + notch):
                rows[y][x] = colors[corner_terrain]
    return rows


def check_name(name: str) -> str:
    """产物名须是不含路径成分的安全文件名（防 --name ../x 越出 assets/）。"""
    if (not name or name in (".", "..") or os.path.basename(name) != name or
            "/" in name or "\\" in name or name.startswith(".")):
        raise ValueError(f"name {name!r} 非法（须为不含路径分隔符的安全文件名）")
    return name


def build(name: str, tile_size: int, lower_name: str, upper_name: str,
          lower_color: str, upper_color: str) -> tuple[bytes, dict]:
    """构建 (png_bytes, tro-tileset 文档)。"""
    check_name(name)
    if tile_size < NOTCH_DIV or tile_size % NOTCH_DIV != 0:
        raise ValueError(
            f"tile_size {tile_size} 非法（须为 {NOTCH_DIV} 的整数倍：角块需整边长）")
    if tile_size > TILE_SIZE_MAX:
        raise ValueError(f"tile_size {tile_size} 超过上限 {TILE_SIZE_MAX}（占位集无需更大）")
    colors = [parse_color(lower_color), parse_color(upper_color)]

    # 布局：行 = terrain，列 = 角组合 c（列数 = COMBO_COUNT）
    sheet = []
    for terrain in range(TERRAIN_COUNT):
        tiles = [tile_pixels(terrain, c, tile_size, colors) for c in range(COMBO_COUNT)]
        for y in range(tile_size):
            row: list[tuple[int, int, int]] = []
            for c in range(COMBO_COUNT):
                row += tiles[c][y]
            sheet.append(row)
    png = png_bytes(sheet)

    out_tiles = []
    for terrain in range(TERRAIN_COUNT):
        for c in range(COMBO_COUNT):
            out_tiles.append({
                "id": terrain * COMBO_COUNT + c,
                "col": c,
                "row": terrain,
                "terrain_set": 0,
                "terrain": terrain,
                "peering_bits": {
                    key: (1 if (c & weight) else 0) for weight, key, _q in CORNERS
                },
            })

    doc = {
        "format": "tro-tileset", "version": 2,
        "texture": f"textures/{name}.png",
        "tile_width": tile_size, "tile_height": tile_size,
        "columns": COMBO_COUNT, "rows": TERRAIN_COUNT,
        "terrain_sets": [{"mode": "corners", "terrains": [
            {"name": lower_name, "color": lower_color},
            {"name": upper_name, "color": upper_color},
        ]}],
        "tiles": out_tiles,
    }
    return png, doc


def write_assets(name: str, png: bytes, doc: dict) -> tuple[str, str]:
    ts_rel = os.path.join("assets", "tilesets", f"{name}.json")
    png_rel = os.path.join("assets", "textures", f"{name}.png")
    for path in (ts_rel, png_rel):
        os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(png_rel, "wb") as f:
        f.write(png)
    with open(ts_rel, "w", encoding="utf-8") as f:
        f.write(json.dumps(doc, indent=1, ensure_ascii=False) + "\n")
    return ts_rel, png_rel


def main() -> int:
    p = argparse.ArgumentParser(prog="placeholder_tileset")
    p.add_argument("--name", default="placeholder", help="产物名（assets 下文件名）")
    p.add_argument("--tile-size", type=int, default=16,
                   help="tile 边长（须为 4 的整数倍）")
    p.add_argument("--lower-name", default="ground", help="terrain 0 名")
    p.add_argument("--upper-name", default="wall", help="terrain 1 名")
    p.add_argument("--lower-color", default="#2f3542", help="terrain 0 底色")
    p.add_argument("--upper-color", default="#c8d0e0", help="terrain 1 底色")
    a = p.parse_args()

    try:
        png, doc = build(a.name, a.tile_size, a.lower_name, a.upper_name,
                         a.lower_color, a.upper_color)
    except ValueError as exc:
        print(f"placeholder_tileset: {exc}", file=sys.stderr)
        return 2
    ts_rel, png_rel = write_assets(a.name, png, doc)
    print(f"OK {a.name}: {ts_rel} + {png_rel} "
          f"({len(doc['tiles'])} tiles, {a.tile_size}x{a.tile_size}, corners mode)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
