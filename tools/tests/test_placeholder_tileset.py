"""test_placeholder_tileset.py —— 占位瓦片集生成器单测（纯函数，无 IO）。

契约：32 tile（2 地形 × 16 角组合）、标注与角组合公式一致、两个地形池各覆盖
全部 16 个角组合（scene_gen 的 pick_tile 零降级前提）、角块像素与标注同源、
同参重跑逐字节一致、非法 name/tile_size 拒绝。

期望值全部在测试内**硬编码**（不复用实现的 CORNERS / 公式），实现若把键名、
位权或象限写错，这里必须失败而不是跟着一起错。
"""

import os
import struct
import sys
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from placeholder_tileset import COMBO_COUNT, TERRAIN_COUNT, build  # noqa: E402

ARGS = ("ph", 16, "ground", "wall", "#2f3542", "#c8d0e0")
LOWER = (0x2F, 0x35, 0x42)
UPPER = (0xC8, 0xD0, 0xE0)

# 独立期望表：角组合 → peering_bits（位权 tl=8, tr=4, br=2, bl=1）
EXPECTED_BITS = {
    0:  {"top_left_corner": 0, "top_right_corner": 0, "bottom_right_corner": 0,
         "bottom_left_corner": 0},
    1:  {"top_left_corner": 0, "top_right_corner": 0, "bottom_right_corner": 0,
         "bottom_left_corner": 1},
    2:  {"top_left_corner": 0, "top_right_corner": 0, "bottom_right_corner": 1,
         "bottom_left_corner": 0},
    3:  {"top_left_corner": 0, "top_right_corner": 0, "bottom_right_corner": 1,
         "bottom_left_corner": 1},
    4:  {"top_left_corner": 0, "top_right_corner": 1, "bottom_right_corner": 0,
         "bottom_left_corner": 0},
    5:  {"top_left_corner": 0, "top_right_corner": 1, "bottom_right_corner": 0,
         "bottom_left_corner": 1},
    6:  {"top_left_corner": 0, "top_right_corner": 1, "bottom_right_corner": 1,
         "bottom_left_corner": 0},
    7:  {"top_left_corner": 0, "top_right_corner": 1, "bottom_right_corner": 1,
         "bottom_left_corner": 1},
    8:  {"top_left_corner": 1, "top_right_corner": 0, "bottom_right_corner": 0,
         "bottom_left_corner": 0},
    9:  {"top_left_corner": 1, "top_right_corner": 0, "bottom_right_corner": 0,
         "bottom_left_corner": 1},
    10: {"top_left_corner": 1, "top_right_corner": 0, "bottom_right_corner": 1,
         "bottom_left_corner": 0},
    11: {"top_left_corner": 1, "top_right_corner": 0, "bottom_right_corner": 1,
         "bottom_left_corner": 1},
    12: {"top_left_corner": 1, "top_right_corner": 1, "bottom_right_corner": 0,
         "bottom_left_corner": 0},
    13: {"top_left_corner": 1, "top_right_corner": 1, "bottom_right_corner": 0,
         "bottom_left_corner": 1},
    14: {"top_left_corner": 1, "top_right_corner": 1, "bottom_right_corner": 1,
         "bottom_left_corner": 0},
    15: {"top_left_corner": 1, "top_right_corner": 1, "bottom_right_corner": 1,
         "bottom_left_corner": 1},
}


def png_pixels(png: bytes) -> list[list[tuple[int, int, int]]]:
    """极简 PNG 解码（8bpp RGB、filter 0，即本工具的输出格式）。"""
    pos = 8
    width = height = 0
    idat = b""
    while pos < len(png):
        size = struct.unpack(">I", png[pos:pos + 4])[0]
        tag = png[pos + 4:pos + 8]
        data = png[pos + 8:pos + 8 + size]
        if tag == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", data[:10])
            assert (depth, color) == (8, 2), (depth, color)
        elif tag == b"IDAT":
            idat += data
        pos += 12 + size
    raw = zlib.decompress(idat)
    stride = width * 3
    rows = []
    for y in range(height):
        line = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        assert line[0] == 0, f"第 {y} 行 filter 非 0（{line[0]}）"
        px = line[1:]
        rows.append([tuple(px[x * 3:x * 3 + 3]) for x in range(width)])
    return rows


def tile_at(rows, terrain: int, combo: int, tile_size: int = 16):
    """切片出图集里某个 tile 的像素矩阵（行 = terrain，列 = combo）。"""
    out = []
    for y in range(tile_size):
        row = rows[terrain * tile_size + y]
        out.append(row[combo * tile_size:(combo + 1) * tile_size])
    return out


class TestPlaceholderTileset(unittest.TestCase):
    def test_structure(self):
        _png, doc = build(*ARGS)
        self.assertEqual(doc["format"], "tro-tileset")
        self.assertEqual(doc["version"], 2)
        self.assertEqual(doc["texture"], "textures/ph.png")
        self.assertEqual((doc["tile_width"], doc["tile_height"]), (16, 16))
        self.assertEqual((doc["columns"], doc["rows"]), (16, 2))
        ts = doc["terrain_sets"]
        self.assertEqual(len(ts), 1)
        self.assertEqual(ts[0]["mode"], "corners")
        self.assertEqual([t["name"] for t in ts[0]["terrains"]], ["ground", "wall"])
        self.assertEqual([t["color"] for t in ts[0]["terrains"]],
                         ["#2f3542", "#c8d0e0"])
        self.assertEqual(len(doc["tiles"]), 32)

    def test_annotation_matches_combo(self):
        """对照硬编码期望表逐 tile 核对 id/col/row/terrain/peering_bits。"""
        _png, doc = build(*ARGS)
        for tile in doc["tiles"]:
            tid = tile["id"]
            terrain, c = divmod(tid, COMBO_COUNT)
            self.assertEqual(tile["terrain"], terrain)
            self.assertEqual(tile["terrain_set"], 0)
            self.assertEqual((tile["col"], tile["row"]), (c, terrain))
            self.assertEqual(tile["peering_bits"], EXPECTED_BITS[c])
        self.assertEqual([t["id"] for t in doc["tiles"]], list(range(32)))

    def test_pools_cover_all_combos(self):
        """每个地形池都含全部 16 个角组合 → 任意 pattern 在池内精确命中。"""
        _png, doc = build(*ARGS)
        for terrain in (0, 1):
            combos = {tile["col"] for tile in doc["tiles"]
                      if tile["terrain"] == terrain}
            self.assertEqual(combos, set(range(COMBO_COUNT)))

    def test_pixels_match_annotation(self):
        """「视觉与标注同源」：角块颜色必须与 peering_bits 一致（逐点核对）。"""
        png, _doc = build(*ARGS)
        rows = png_pixels(png)
        self.assertEqual((len(rows), len(rows[0])), (2 * 16, 16 * 16))
        notch = 16 // 4

        # terrain 0 / combo 0：四角皆同地形 → 整块底色
        t = tile_at(rows, 0, 0)
        self.assertTrue(all(px == LOWER for row in t for px in row))

        # terrain 0 / combo 8：仅左上角是异地形 → 左上 notch 块 = upper 色，其余 = 底色
        t = tile_at(rows, 0, 8)
        for y in range(16):
            for x in range(16):
                expected = UPPER if (x < notch and y < notch) else LOWER
                self.assertEqual(t[y][x], expected, f"(0,8) 像素 ({x},{y})")

        # terrain 1 / combo 0：四角皆异地形 → 四个 notch 块 = lower 色，其余 = upper 色
        t = tile_at(rows, 1, 0)
        for y in range(16):
            for x in range(16):
                in_notch = ((x < notch or x >= 16 - notch) and
                            (y < notch or y >= 16 - notch))
                expected = LOWER if in_notch else UPPER
                self.assertEqual(t[y][x], expected, f"(1,0) 像素 ({x},{y})")

        # terrain 1 / combo 15：四角皆同地形 → 整块底色
        t = tile_at(rows, 1, 15)
        self.assertTrue(all(px == UPPER for row in t for px in row))

    def test_png_dimensions(self):
        """贴图尺寸 = 列数×行数 个 tile（读 IHDR，不依赖第三方库）。"""
        png, _doc = build(*ARGS)
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        width, height = struct.unpack(">II", png[16:24])
        self.assertEqual((width, height), (16 * 16, 2 * 16))
        self.assertEqual((width, height),
                         (COMBO_COUNT * 16, TERRAIN_COUNT * 16))
        # 尾部必须是 IEND，且总长约在预期量级（防截断）
        self.assertTrue(png.endswith(b"IEND\xaeB`\x82"))

    def test_deterministic(self):
        """同参同进程两次构建逐字节一致（无时间戳/随机；跨进程确定性同源）。"""
        png_a, doc_a = build(*ARGS)
        png_b, doc_b = build(*ARGS)
        self.assertEqual(png_a, png_b)
        self.assertEqual(doc_a, doc_b)

    def test_bad_tile_size(self):
        for size in (0, 15, -4, 257, 4000):
            with self.assertRaises(ValueError):
                build("ph", size, "ground", "wall", "#000000", "#ffffff")
        # 边界合法值：4 的整数倍且 ≤ 上限
        for size in (4, 8, 256):
            png, doc = build("ph", size, "ground", "wall", "#000000", "#ffffff")
            self.assertEqual(doc["tile_width"], size)
            self.assertEqual(struct.unpack(">II", png[16:24]),
                             (16 * size, 2 * size))

    def test_bad_name(self):
        for name in ("", ".", "..", "../x", "a/b", "/abs", "a\\b", ".hidden"):
            with self.assertRaises(ValueError):
                build(name, 16, "ground", "wall", "#000000", "#ffffff")


if __name__ == "__main__":
    unittest.main()
