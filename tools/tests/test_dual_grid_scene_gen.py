import json
import os
import subprocess
import tempfile
import unittest


@unittest.skipUnless("TROGUE_DUAL_GRID_SCENE_GEN" in os.environ,
                     "dual-grid CLI path is only available in the dedicated CTest")
class TestDualGridSceneGen(unittest.TestCase):
    def test_vertex_grid_maps_directly_to_corner_tiles(self):
        exe = os.environ["TROGUE_DUAL_GRID_SCENE_GEN"]
        tileset_rel = "__tmp_dual_grid_test.json"
        tileset_path = os.path.join("assets", tileset_rel)
        with open(tileset_path, "w", encoding="utf-8") as handle:
            tiles = []
            for c in range(16):
                tiles.append({
                    "id": c,
                    "col": c % 4,
                    "row": c // 4,
                    "dual_grid_corners": {
                        "NW": (c >> 3) & 1,
                        "NE": (c >> 2) & 1,
                        "SW": c & 1,
                        "SE": (c >> 1) & 1,
                    },
                })
            json.dump({
                "format": "tro-tileset", "version": 2,
                "texture": "textures/floor.png",
                "tile_width": 16, "tile_height": 16,
                "columns": 4, "rows": 4,
                "dual_grid": {"mode": "corners", "terrains": ["lower", "upper"], "tile_count": 16},
                "tiles": tiles,
            }, handle)
        spec_path = None
        output_path = None
        try:
            with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as spec:
                spec_path = spec.name
                json.dump({
                    "name": "dual_grid_test",
                    "tileset": tileset_rel,
                    "vertex_grid": [[0, 1, 0], [1, 1, 0], [0, 0, 1]],
                }, spec)
            with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as output:
                output_path = output.name
            result = subprocess.run([exe, spec_path, output_path], text=True,
                                    capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            scene = json.load(open(output_path, encoding="utf-8"))
            tiles_out = scene["tilemap"]["layers"][0]["tiles"]
            self.assertEqual(tiles_out, [7, 9, 12, 10])
            self.assertEqual(scene["tilemap"]["layers"][0]["width"], 2)
            self.assertEqual(scene["tilemap"]["layers"][0]["height"], 2)
        finally:
            os.remove(tileset_path)
            if spec_path and os.path.exists(spec_path):
                os.remove(spec_path)
            if output_path and os.path.exists(output_path):
                os.remove(output_path)


if __name__ == "__main__":
    unittest.main()
