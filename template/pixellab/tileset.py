"""tileset.py —— Wang 4×4 sheet → tro-tileset v2（plan-13 §5.2 B）。

sheet 本身即 atlas（不重切片）：texture + col/row 直接引用，columns=4 rows=4。
每 tile 按 mapping 三要素写 terrain_sets[0]（corners mode）+ 4 角 peering_bits
+ 归池 terrain。25-tile（transition_size=1.0）拒绝。
"""

from __future__ import annotations

import json

from mapping import corners_to_peering, pool_of


def build(meta: dict, name: str, lower: str, upper: str) -> dict:
    """从 tileset 元数据构建 tro-tileset 文档。

    PNG 由调用方单独下载（metadata 端点不内嵌图像；用 tileset image URL）。
    """
    ts = meta["tileset_data"]["tiles"]
    if len(ts) != 16:
        raise ValueError(
            f"{name}: 本期仅支持 16-tile 4×4 集（实际 {len(ts)}；"
            "25-tile transition_size=1.0 不在范围，plan-13 §5.4）")
    size = meta["tile_size"]
    tw, th = size["width"], size["height"]
    if (tw, th) not in ((16, 16), (32, 32)):
        raise ValueError(f"{name}: tile_size {tw}x{th} 不支持（仅 16/32）")

    out_tiles = []
    for i, t in enumerate(ts):
        corners = t["corners"]
        bb = t["bounding_box"]
        if bb["width"] != tw or bb["height"] != th:
            raise ValueError(f"{name}: tile {t['id']} bbox {bb} 与 tile 尺寸不符")
        out_tiles.append({
            "id": i,
            "col": bb["x"] // tw,
            "row": bb["y"] // th,
            "terrain_set": 0,
            "terrain": pool_of(corners),
            "peering_bits": corners_to_peering(corners),
        })

    doc = {
        "format": "tro-tileset", "version": 2,
        "texture": f"textures/pixellab/{name}.png",
        "tile_width": tw, "tile_height": th,
        "columns": 4, "rows": 4,
        "terrain_sets": [{"mode": "corners", "terrains": [
            {"name": lower, "color": "#000000"},   # 颜色仅 schema 占位（引擎不消费）
            {"name": upper, "color": "#ffffff"},
        ]}],
        "tiles": out_tiles,
    }
    return doc
