"""test_scene_gen.py —— scene_gen + 占位瓦片集的端到端回归（无窗口）。

链路：tools/placeholder_tileset.py 生成占位 tileset → scene_gen 按网格采样
（引擎 pick_tile + load_json 自检）→ tro-scene。断言：确定性（两次逐字节一致）、
分池正确（每格 tile id 落在本格地形池内）、具体格子取值、输出父目录自动创建、
错误路径（spec 字段类型 / 网格 / tileset 模式 / 地形数 / 池缺角组合 / 缺 tileset）。

产物（assets 下的临时 tileset、贴图与变体）用后即删；残留即测试失败。
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCENE_GEN = os.environ.get("TROGUE_SCENE_GEN",
                           os.path.join(ROOT, "build", "tools", "trogue_scene_gen"))

GRID = ["#####", "#...#", "#...#", "#...#", "#####"]
CELLS = len(GRID) * len(GRID[0])


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


class TestSceneGen(unittest.TestCase):
    # 名字带 PID，避免并行 ctest 互相踩
    name = f"__tmp_scene_gen_{os.getpid()}"

    @classmethod
    def setUpClass(cls):
        # 二进制缺失必须**失败**而不是 skip：skip 让 unittest 以 0 退出，
        # ctest 会在「目标没构建」时记 PASS，端到端用例静默消失。
        if not os.path.exists(SCENE_GEN):
            raise AssertionError(
                f"scene_gen 未构建: {SCENE_GEN}（先 cmake --build build）")
        r = run([sys.executable, "tools/placeholder_tileset.py", "--name", cls.name])
        if r.returncode != 0:
            raise AssertionError(f"占位 tileset 生成失败: {r.stderr}")
        cls.tileset = f"tilesets/{cls.name}.json"
        cls.tmpdir = tempfile.TemporaryDirectory()
        cls.aux = []  # 变体 tileset 的 assets 相对路径

    @classmethod
    def tearDownClass(cls):
        cls.tmpdir.cleanup()
        rels = [f"assets/tilesets/{cls.name}.json",
                f"assets/textures/{cls.name}.png"] + [f"assets/{a}" for a in cls.aux]
        for rel in rels:
            path = os.path.join(ROOT, rel)
            if os.path.exists(path):
                os.remove(path)
        left = [rel for rel in rels if os.path.exists(os.path.join(ROOT, rel))]
        if left:
            raise AssertionError(f"测试残留产物未清理: {left}")

    # ── 助手 ──
    def variant(self, tag, mutate):
        """在占位集基础上改一份变体写入 assets/tilesets/，返回 assets 相对路径。"""
        with open(os.path.join(ROOT, "assets", self.tileset), encoding="utf-8") as f:
            doc = json.load(f)
        mutate(doc)
        rel = f"tilesets/__tmp_{self.name[2:]}_{tag}.json"
        with open(os.path.join(ROOT, "assets", rel), "w", encoding="utf-8") as f:
            json.dump(doc, f, ensure_ascii=False)
        self.aux.append(rel)
        return rel

    def spec_path(self, spec):
        fd, path = tempfile.mkstemp(suffix=".json", dir=self.tmpdir.name)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(spec, f)
        return path

    def fresh_out(self, tag):
        return os.path.join(self.tmpdir.name,
                            f"{tag}_{len(os.listdir(self.tmpdir.name))}.json")

    def generate(self, spec, tag="out"):
        out = self.fresh_out(tag)
        r = run([SCENE_GEN, self.spec_path(spec), out])
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(out, encoding="utf-8") as f:
            return f.read()

    # ── 正常路径 ──
    def test_generates_pooled_layers(self):
        text = self.generate({"name": "ph", "tileset": self.tileset, "grid": GRID})
        scene = json.loads(text)
        self.assertEqual(scene["format"], "tro-scene")
        self.assertEqual(scene["version"], 2)
        tm = scene["tilemap"]
        self.assertEqual(tm["tilesets"], [{"name": "terrain", "path": self.tileset}])
        self.assertEqual((tm["tile_width"], tm["tile_height"]), (16, 16))
        ground, walls = tm["layers"]
        self.assertEqual((ground["name"], ground["solid"]),
                         ("ground", False))
        self.assertEqual((walls["name"], walls["solid"]), ("walls", True))
        self.assertEqual(len(ground["tiles"]), CELLS)
        self.assertEqual(len(walls["tiles"]), CELLS)
        # 分池：'.' 格落 ground 层且 id < 16（terrain 0 池）；'#' 格落 walls 层且 id ≥ 16
        for y, row in enumerate(GRID):
            for x, ch in enumerate(row):
                i = y * len(row) + x
                if ch == "#":
                    self.assertEqual(ground["tiles"][i], -1)
                    self.assertGreaterEqual(walls["tiles"][i], 16)
                else:
                    self.assertEqual(walls["tiles"][i], -1)
                    self.assertLess(ground["tiles"][i], 16)
        # 具体取值：内部孤立地格无墙角 → 池 0 组合 0；左上角墙格四面皆墙 → 池 1 组合 15
        self.assertEqual(ground["tiles"][2 * len(GRID[0]) + 2], 0)
        self.assertEqual(walls["tiles"][0], 31)
        self.assertEqual(scene["entities"], [])

    def test_deterministic(self):
        spec = {"name": "ph", "tileset": self.tileset, "grid": GRID}
        self.assertEqual(self.generate(spec), self.generate(spec))

    def test_output_parent_dirs_created(self):
        """输出路径的父目录不存在时自动创建（模板项目的 assets/ 常缺 scenes/）。"""
        out = os.path.join(self.tmpdir.name, "sub", "deep", "scene.json")
        r = run([SCENE_GEN, self.spec_path({"tileset": self.tileset, "grid": GRID}), out])
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(os.path.exists(out))

    def test_passthrough(self):
        entities = [{"id": "player", "type": "player", "x": 16.0, "y": 16.0}]
        text = self.generate({"name": "ph", "tileset": self.tileset, "grid": GRID,
                              "background": "#101018", "entities": entities})
        scene = json.loads(text)
        self.assertEqual(scene["meta"]["background"], "#101018")
        self.assertEqual(scene["entities"], entities)

    def test_name_flag_overrides_spec_and_default_is_empty(self):
        """--name 优先于 spec.name；两者都缺时 meta 不写 name 键。"""
        out = self.fresh_out("named")
        spec = self.spec_path({"name": "from_spec", "tileset": self.tileset, "grid": GRID})
        r = run([SCENE_GEN, spec, out, "--name", "from_flag"])
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(out, encoding="utf-8") as f:
            self.assertEqual(json.load(f)["meta"]["name"], "from_flag")

        out2 = self.fresh_out("anon")
        r = run([SCENE_GEN, self.spec_path({"tileset": self.tileset, "grid": GRID}), out2])
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(out2, encoding="utf-8") as f:
            self.assertEqual(json.load(f)["meta"], {})

    # ── 负向路径：spec / 网格（退出码 2，不产生输出）──
    def assert_rejected(self, spec, code, needle=""):
        out = self.fresh_out("never")
        r = run([SCENE_GEN, self.spec_path(spec), out])
        self.assertEqual(r.returncode, code, r.stderr)
        if needle:
            self.assertIn(needle, r.stderr)
        self.assertFalse(os.path.exists(out), "被拒绝的 spec 不应产生输出文件")

    def test_bad_char_rejected(self):
        self.assert_rejected({"tileset": self.tileset, "grid": ["..x"]}, 2, "非法")

    def test_ragged_grid_rejected(self):
        self.assert_rejected({"tileset": self.tileset, "grid": ["...", ".."]}, 2, "等宽")

    def test_empty_grid_rejected(self):
        self.assert_rejected({"tileset": self.tileset, "grid": [""]}, 2, "越界")

    def test_bad_spec_field_types_rejected(self):
        for field, value in (("name", 123), ("background", True), ("entities", "nope")):
            self.assert_rejected({"tileset": self.tileset, "grid": GRID, field: value},
                                 2, field)

    def test_missing_keys_rejected(self):
        self.assert_rejected({"grid": GRID}, 2)
        self.assert_rejected({"tileset": self.tileset}, 2)

    # ── 负向路径：tileset 语义（退出码 1，不产生输出）──
    def test_sides_mode_rejected(self):
        rel = self.variant("sides", lambda d: d["terrain_sets"][0].update(mode="sides"))
        self.assert_rejected({"tileset": rel, "grid": GRID}, 1)

    def test_one_terrain_rejected(self):
        def mutate(d):
            # 只留 1 个地形：terrains / tiles / peering_bits 三者同步降到「单地形」
            # 自洽（否则引擎先以越界拒绝，就走不到 scene_gen 自己的「地形数」判定）
            d["terrain_sets"][0]["terrains"] = d["terrain_sets"][0]["terrains"][:1]
            d["tiles"] = [t for t in d["tiles"] if t["terrain"] == 0]
            d["rows"] = 1
            for t in d["tiles"]:
                t["peering_bits"] = {k: 0 for k in t["peering_bits"]}
        rel = self.variant("one_terrain", mutate)
        self.assert_rejected({"tileset": rel, "grid": GRID}, 1, "地形数")

    def test_pool_missing_combo_rejected(self):
        """上位地形池无 tile → pick_tile 取不到 → 拒绝写出（不产带空洞的地图）。"""
        def mutate(d):
            d["tiles"] = [t for t in d["tiles"] if t["terrain"] == 0]
            d["rows"] = 1
        rel = self.variant("only0", mutate)
        self.assert_rejected({"tileset": rel, "grid": GRID}, 1, "取不到 tile")

    def test_missing_tileset_rejected(self):
        self.assert_rejected({"tileset": "tilesets/__no_such__.json", "grid": GRID}, 1)


if __name__ == "__main__":
    unittest.main()
