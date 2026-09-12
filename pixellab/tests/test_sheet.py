"""API v2 spritesheet ZIP importer tests."""
import io
import json
import os
import sys
import unittest
import zipfile

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sheet import build
from pxlab import _json_bytes


class TestSheet(unittest.TestCase):
    def test_imports_animation_rows_and_skips_rotations(self):
        image = Image.new("RGBA", (32, 24), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rectangle((0, 0, 7, 7), fill=(255, 0, 0, 255))
        draw.rectangle((8, 0, 15, 7), fill=(0, 255, 0, 255))
        draw.rectangle((0, 8, 7, 15), fill=(0, 0, 255, 255))
        png = io.BytesIO()
        image.save(png, "PNG")
        layout = {
            "cell_size": {"width": 8, "height": 8},
            "columns": 4,
            "rows": [
                {"kind": "rotations", "row": 0},
                {"animation": "walk", "direction": "south", "row": 1,
                 "frame_count": 2},
                {"animation": "idle", "direction": "south", "row": 2,
                 "frame_count": 1},
            ],
        }
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as zf:
            zf.writestr("hero.png", png.getvalue())
            zf.writestr("hero.json", json.dumps(layout))
        sheet, doc = build(archive.getvalue(), "hero", 8, {"walk"})
        self.assertEqual(sheet, png.getvalue())
        self.assertEqual([a["name"] for a in doc["animations"]],
                         ["walk_south", "idle_south"])
        self.assertTrue(doc["animations"][0]["loop"])
        self.assertEqual(doc["animations"][0]["frames"][1]["region"], [8, 8, 8, 8])

    def test_json_hash_bytes_use_real_newline(self):
        self.assertEqual(_json_bytes({"x": 1})[-1:], b"\n")
        self.assertNotEqual(_json_bytes({"x": 1})[-2:], b"\\n")

    def test_rejects_missing_layout(self):
        with self.assertRaises(ValueError):
            build(b"not a zip", "broken", 8, set())


if __name__ == "__main__":
    unittest.main()
