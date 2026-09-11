"""mapping.py —— Wang 16-tile ↔ peering_bits(corners) 显式映射（plan-13 §5.3）。

三要素（W2 实测锁定，fixture: wang_grass_dirt）：
1. 组合映射：PixelLab tile 的 corners{NW,NE,SW,SE}（值 lower/upper）↔ 引擎
   TerrainBit 4 角位（NW→top_left, NE→top_right, SW→bottom_left, SE→bottom_right）。
2. 归池规则：tile.terrain = 多数角（≥3 upper → upper 池；否则 lower 池；2/2 平分
   归 lower）。实测 16/16 精确命中（探针 2026-09-11）。
3. 顶点采样：格的 4 顶点 = 该格 terrain 与 4/8 邻格多数投票（见 vertex_corners）。

纯数据 + 纯函数，无 IO；单测见 tests/test_mapping.py。
"""

from __future__ import annotations

# PixelLab 角名 → 引擎 TerrainBit 名（tro-tileset peering_bits 键名）
CORNER_TO_BIT = {
    "NW": "top_left_corner",
    "NE": "top_right_corner",
    "SW": "bottom_left_corner",
    "SE": "bottom_right_corner",
}

# 顶点组合编码（与 terrain_test 的 corner_pattern 一致）：
# c = tl*8 + tr*4 + br*2 + bl，值 0=lower 1=upper
def corners_to_peering(corners: dict[str, str]) -> dict[str, int]:
    """PixelLab corners 对象 → peering_bits dict（值 = terrain 序号）。

    corners 值为 PixelLab 固定枚举 'lower'/'upper'（非 terrain 描述名）。
    """
    out = {}
    for k, bit in CORNER_TO_BIT.items():
        v = corners[k]
        if v == "lower":
            out[bit] = 0
        elif v == "upper":
            out[bit] = 1
        else:
            raise ValueError(f"corners.{k} 值 {v!r} 不在 {{'lower','upper'}}")
    return out


def pool_of(corners: dict[str, str]) -> int:
    """归池：多数角（平分归 lower=0）。corners 值 = 'lower'/'upper' 枚举。"""
    vals = [1 if corners[k] == "upper" else 0 for k in ("NW", "NE", "SW", "SE")]
    return 1 if sum(vals) >= 3 else 0


def pool_of_vertices(tl: int, tr: int, br: int, bl: int) -> int:
    """4 顶点值 → 候选池（= tile 归池同一规则：多数，平分归 lower=0）。

    pick_tile 按此 pool 过滤候选 tile；必须由 pattern（顶点）导出而非格自身
    terrain——否则少数角格会落错池被强制降级（评审 B1）。
    """
    return 1 if (tl + tr + br + bl) >= 3 else 0


def vertex_corners(terrain_grid: list[list[int]], x: int, y: int) -> tuple[int, int, int, int]:
    """terrain 场 → (x,y) 格的 4 顶点值 (tl, tr, br, bl)，各 ∈ {0,1}。

    顶点 = 四邻格（含自身）多数投票；2/2 平分时取左上优先序
    （self, top, left, top_left——与 Godot 顶点归属惯例对齐，W2 比对定稿）。
    terrain_grid 值：0=lower 1=upper。越界邻格不计入投票。
    """
    h = len(terrain_grid)
    w = len(terrain_grid[0]) if h else 0
    if not (0 <= x < w and 0 <= y < h):
        raise IndexError(f"({x},{y}) 越界 {w}x{h}")

    def at(cx: int, cy: int) -> int:
        return terrain_grid[cy][cx] if 0 <= cx < w and 0 <= cy < h else -1

    def vote(*cells: int) -> int:
        vals = [v for v in cells if v in (0, 1)]
        up = sum(vals)
        lo = len(vals) - up
        if up > lo:
            return 1
        if lo > up:
            return 0
        # 平分：按优先序取第一个有效格
        for v in cells:
            if v in (0, 1):
                return v
        return 0  # 全越界（不可能：self 恒有效）

    self_v = terrain_grid[y][x]
    top, left = at(x, y - 1), at(x - 1, y)
    topleft = at(x - 1, y - 1)
    bot, right = at(x, y + 1), at(x + 1, y)
    botright = at(x + 1, y + 1)
    topright = at(x + 1, y - 1)
    botleft = at(x - 1, y + 1)

    tl = vote(self_v, top, left, topleft)
    tr = vote(self_v, top, right, topright)
    br = vote(self_v, bot, right, botright)
    bl = vote(self_v, bot, left, botleft)
    return tl, tr, br, bl


def pattern_from_vertices(tl: int, tr: int, br: int, bl: int) -> dict[str, int]:
    """4 顶点值 → pick_tile 的 4 角 pattern（其余位由调用方填 -1）。"""
    return {
        "top_left_corner": tl,
        "top_right_corner": tr,
        "bottom_right_corner": br,
        "bottom_left_corner": bl,
    }
