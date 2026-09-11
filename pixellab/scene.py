"""scene.py —— terrain 网格 → 顶点 pattern → scene_gen CLI → tro-scene（plan-13 §5.2 C）。

地形指派/顶点采样归本模块（调用方角色）；bits→tile id 归 tools/scene_gen
（engine pick_tile）。场景 tiles 烤死（plan-12 决策）。
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile

from mapping import pool_of_vertices, vertex_corners

SCENE_GEN = os.path.join("build", "tools", "trogue_scene_gen")


def parse_grid(text: str) -> list[list[int]]:
    """ASCII 网格 → terrain 指派。'1'/'#'/'G' = upper(1)，'0'/'.'/'D' = lower(0)。"""
    rows = [ln.rstrip("\n") for ln in text.splitlines() if ln.strip()]
    if not rows:
        raise ValueError("空网格")
    w = len(rows[0])
    upper_chars = {"1", "#", "G"}
    lower_chars = {"0", ".", "D"}
    grid: list[list[int]] = []
    for y, row in enumerate(rows):
        if len(row) != w:
            raise ValueError(f"第 {y} 行宽 {len(row)} != {w}（网格须等宽）")
        line = []
        for ch in row:
            if ch in upper_chars:
                line.append(1)
            elif ch in lower_chars:
                line.append(0)
            else:
                raise ValueError(f"非法字符 {ch!r}（可用 {''.join(sorted(upper_chars | lower_chars))}）")
        grid.append(line)
    return grid


def build_patterns(grid: list[list[int]]) -> list[dict]:
    """terrain 场 → 行主序顶点 pattern。

    terrain 入参 = 顶点 pattern 的归池（多数顶点，平分归 lower），与 tile 归池
    同一规则——这样每个 pattern 都能在池内精确命中（零降级）。不用格自身
    terrain：少数角格会因此落错池（评审 B1）。
    """
    h, w = len(grid), len(grid[0])
    out = []
    for y in range(h):
        for x in range(w):
            tl, tr, br, bl = vertex_corners(grid, x, y)
            out.append({"tl": tl, "tr": tr, "br": br, "bl": bl,
                        "terrain": pool_of_vertices(tl, tr, br, bl)})
    return out


def generate(grid_text: str, tileset_rel: str, scene_name: str,
             out_rel: str) -> str:
    """全流程：网格 → pattern JSON → scene_gen → assets/<out_rel>。"""
    grid = parse_grid(grid_text)
    h, w = len(grid), len(grid[0])
    payload = {"tileset": tileset_rel, "w": w, "h": h,
               "patterns": build_patterns(grid)}
    if not os.path.exists(SCENE_GEN):
        raise FileNotFoundError(f"{SCENE_GEN} 不存在（先 cmake --build build）")
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(payload, f)
        pat_path = f.name
    try:
        out_full = os.path.join("assets", out_rel)
        os.makedirs(os.path.dirname(out_full), exist_ok=True)
        r = subprocess.run([SCENE_GEN, pat_path, out_full, "--name", scene_name],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"scene_gen 失败: {r.stderr.strip()}")
        return r.stdout.strip()
    finally:
        os.unlink(pat_path)
