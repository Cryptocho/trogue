"""test_scene_gen.py —— scene_gen 确定性回归。

同一 pattern fixture 跑两次，断言输出逐字节一致（机制采样全确定）。
scene_gen 二进制不存在时跳过（未构建场景不阻塞单测）。
"""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "pixellab"))
SCENE_GEN = os.environ.get("TROGUE_SCENE_GEN",
                          os.path.join(ROOT, "build", "tools", "trogue_scene_gen"))
FIXTURE = os.path.join(ROOT, "pixellab", "fixtures", "wang_pattern.json")


class TestSceneGenDeterministic(unittest.TestCase):
    def test_double_run_identical(self):
        if not os.path.exists(SCENE_GEN):
            self.skipTest("scene_gen 未构建")
        outs = []
        for _ in range(2):
            fd, path = tempfile.mkstemp(suffix=".json"); os.close(fd)
            r = subprocess.run([SCENE_GEN, FIXTURE, path, "--name", "det_test"],
                               capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(path, "rb") as fh:
                outs.append(fh.read())
            os.unlink(path)
        self.assertEqual(outs[0], outs[1], "两次运行输出不一致（非确定性）")


if __name__ == "__main__":
    unittest.main()
