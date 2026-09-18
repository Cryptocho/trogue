"""PixelLab tileset15 -> tro-tileset v2 dual-grid conversion."""

from __future__ import annotations

import io
import os
from typing import Any

from PIL import Image


CORNERS = ("NW", "NE", "SW", "SE")
LABELS = {"lower": 0, "upper": 1}


def _error(message: str) -> ValueError:
    return ValueError(f"tileset15: {message}")


def build(metadata: dict[str, Any], png: bytes, name: str) -> tuple[bytes, dict[str, Any]]:
    if not name or os.path.basename(name) != name or name in {".", ".."} or "/" in name or "\\\\" in name:
        raise _error("name 必须是不含路径分隔符的安全文件名")
    if metadata.get("format") != "tileset15":
        raise _error("format 必须为 tileset15")
    data = metadata.get("tileset_data")
    if not isinstance(data, dict):
        raise _error("缺少 tileset_data object")
    size = data.get("tile_size")
    if not isinstance(size, dict) or size.get("width") != size.get("height"):
        raise _error("tileset_data.tile_size 必须为等宽尺寸")
    tile_size = size["width"]
    if not isinstance(tile_size, int) or tile_size <= 0:
        raise _error("tile_size 必须为正整数")
    source_tiles = data.get("tiles")
    if not isinstance(source_tiles, list) or len(source_tiles) != 16:
        raise _error("首版只接受恰好 16 个 tiles")

    try:
        with Image.open(io.BytesIO(png)) as image:
            width, height = image.size
    except Exception as exc:
        raise _error(f"PNG 无法读取: {exc}") from exc

    checked: list[tuple[dict[str, Any], tuple[int, int, int, int]]] = []
    boxes: list[tuple[int, int, int, int]] = []
    combinations: set[tuple[int, int, int, int]] = set()
    for index, tile in enumerate(source_tiles):
        if not isinstance(tile, dict):
            raise _error(f"tiles[{index}] 必须为 object")
        corners = tile.get("corners")
        if not isinstance(corners, dict) or set(corners) != set(CORNERS):
            raise _error(f"tiles[{index}].corners 必须恰好包含 NW/NE/SW/SE")
        key = tuple(LABELS.get(corners[corner], -1) for corner in CORNERS)
        if -1 in key:
            raise _error(f"tiles[{index}].corners 只能使用 lower/upper")
        if key in combinations:
            raise _error(f"tiles[{index}] 重复四角组合")
        combinations.add(key)

        box = tile.get("bounding_box")
        if not isinstance(box, dict):
            raise _error(f"tiles[{index}] 缺少 bounding_box")
        values = tuple(box.get(k) for k in ("x", "y", "width", "height"))
        if any(not isinstance(value, int) or isinstance(value, bool) for value in values):
            raise _error(f"tiles[{index}].bounding_box 必须为整数")
        x, y, bw, bh = values
        if x < 0 or y < 0 or bw != tile_size or bh != tile_size:
            raise _error(f"tiles[{index}].bounding_box 尺寸或坐标非法")
        if x % tile_size or y % tile_size:
            raise _error(f"tiles[{index}].bounding_box 坐标必须按 tile_size 对齐")
        if x + bw > width or y + bh > height:
            raise _error(f"tiles[{index}].bounding_box 超出 PNG")
        current = (x, y, bw, bh)
        if current in boxes:
            raise _error(f"tiles[{index}].bounding_box 重复")
        boxes.append(current)
        checked.append((tile, current))

    if len(combinations) != 16:
        raise _error("必须覆盖全部 16 个四角组合")

    # 位置只由服务端最终 bounding_box 决定；排序键固定，避免生成顺序漂移。
    checked.sort(key=lambda pair: (pair[1][1], pair[1][0], str(pair[0].get("name", "")),
                                   str(pair[0].get("id", ""))))
    cols = width // tile_size
    out_tiles = []
    for index, (tile, (x, y, _bw, _bh)) in enumerate(checked):
        corners = tile["corners"]
        out_tiles.append({
            "id": index,
            "col": x // tile_size,
            "row": y // tile_size,
            "dual_grid_corners": {
                corner: LABELS[corners[corner]] for corner in CORNERS
            },
        })

    doc = {
        "format": "tro-tileset",
        "version": 2,
        "texture": f"textures/pixellab/{name}.png",
        "tile_width": tile_size,
        "tile_height": tile_size,
        "columns": cols,
        "rows": height // tile_size,
        "dual_grid": {
            "mode": "corners",
            "terrains": ["lower", "upper"],
            "tile_count": 16,
        },
        "tiles": out_tiles,
    }
    return png, doc
