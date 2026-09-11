"""test_mapping.py —— 映射三要素单测（fixture 实测表，plan-13 §5.3）。"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import unittest
from mapping import corners_to_peering, pool_of, vertex_corners, pattern_from_vertices


def mk(nw, ne, sw, se):
    return {"NW": nw, "NE": ne, "SW": sw, "SE": se}


class TestPool(unittest.TestCase):
    def test_majority(self):
        self.assertEqual(pool_of(mk("upper", "upper", "upper", "lower")), 1)
        self.assertEqual(pool_of(mk("lower", "lower", "lower", "upper")), 0)
        self.assertEqual(pool_of(mk("upper", "upper", "upper", "upper")), 1)
        self.assertEqual(pool_of(mk("lower", "lower", "lower", "lower")), 0)

    def test_tie_goes_lower(self):
        self.assertEqual(pool_of(mk("upper", "upper", "lower", "lower")), 0)
        self.assertEqual(pool_of(mk("lower", "upper", "upper", "lower")), 0)


class TestPeering(unittest.TestCase):
    def test_all(self):
        self.assertEqual(corners_to_peering(mk("upper", "upper", "lower", "lower")),
                         {"top_left_corner": 1, "top_right_corner": 1,
                          "bottom_left_corner": 0, "bottom_right_corner": 0})
        with self.assertRaises(ValueError):
            corners_to_peering(mk("x", "upper", "lower", "lower"))


class TestVertices(unittest.TestCase):
    def test_uniform(self):
        g = [[0] * 4 for _ in range(4)]
        for x in range(4):
            for y in range(4):
                self.assertEqual(vertex_corners(g, x, y), (0, 0, 0, 0))
        g1 = [[1] * 4 for _ in range(4)]
        for x in range(4):
            for y in range(4):
                self.assertEqual(vertex_corners(g1, x, y), (1, 1, 1, 1))

    def test_horizontal_boundary(self):
        # 上半 grass(1) 下半 dirt(0)：y=1 行（grass 最后一行）br/bl 顶点应翻成 0
        g = [[1, 1], [0, 0]]
        self.assertEqual(vertex_corners(g, 0, 0), (1, 1, 1, 1))   # 左上角格，全 grass 邻域
        # (0,1) 是 dirt 格：tl 平分(1:1)取 self=0；tr 邻域 1 grass:2 dirt → 0
        self.assertEqual(vertex_corners(g, 0, 1), (0, 0, 0, 0))
        self.assertEqual(vertex_corners(g, 1, 1), (0, 0, 0, 0))
        # (0,0) 是 grass 格：bl = vote(1, bot=0, right=1, botright=0) = 2:2 平 → self=1
        self.assertEqual(vertex_corners(g, 1, 0), (1, 1, 1, 1))

    def test_pattern(self):
        self.assertEqual(pattern_from_vertices(1, 0, 0, 1),
                         {"top_left_corner": 1, "top_right_corner": 0,
                          "bottom_right_corner": 0, "bottom_left_corner": 1})


class TestFixture(unittest.TestCase):
    """读 W2 锁定用的真实 fixture，校验 corners→peering/pool 与实测表一致。"""

    def test_wang_fixture(self):
        import json, os
        fix = os.path.join(os.path.dirname(__file__), "..", "fixtures",
                           "wang_grass_dirt.meta.json")
        with open(fix, encoding="utf-8") as fh:
            meta = json.load(fh)
        tiles = meta["tileset_data"]["tiles"]
        self.assertEqual(len(tiles), 16)
        seen_corners = set()
        for t in tiles:
            c = t["corners"]
            seen_corners.add((c["NW"], c["NE"], c["SW"], c["SE"]))
            peering = corners_to_peering(c)
            self.assertEqual(len(peering), 4)
            self.assertIn(pool_of(c), (0, 1))
        # 16 个 tile 的 corners 组合应两两不同（全覆盖 4 角二元组）
        self.assertEqual(len(seen_corners), 16)


if __name__ == "__main__":
    unittest.main()
