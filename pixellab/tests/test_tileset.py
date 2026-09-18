import io
import os
import sys
import unittest

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tileset import build


def metadata():
    tiles = []
    for c in range(16):
        x, y = (c % 4) * 16, (c // 4) * 16
        tiles.append({
            "id": f"wang_{c}",
            "name": f"wang_{c}",
            "corners": {
                "NW": "upper" if c & 8 else "lower",
                "NE": "upper" if c & 4 else "lower",
                "SW": "upper" if c & 1 else "lower",
                "SE": "upper" if c & 2 else "lower",
            },
            "bounding_box": {"x": x, "y": y, "width": 16, "height": 16},
        })
    return {
        "id": "fixture-tileset",
        "format": "tileset15",
        "tileset_data": {
            "tile_size": {"width": 16, "height": 16},
            "tiles": tiles,
        },
    }


def png():
    image = Image.new("RGB", (64, 64))
    for y in range(64):
        for x in range(64):
            image.putpixel((x, y), ((x // 16) * 40, (y // 16) * 40, 80))
    out = io.BytesIO()
    image.save(out, "PNG")
    return out.getvalue()


class TestTileset(unittest.TestCase):
    def test_converts_all_corner_combinations(self):
        raw, doc = build(metadata(), png(), "terrain")
        self.assertEqual(raw, png())
        self.assertEqual(doc["dual_grid"]["tile_count"], 16)
        self.assertEqual(len(doc["tiles"]), 16)
        keys = {
            tuple(tile["dual_grid_corners"][corner] for corner in ("NW", "NE", "SW", "SE"))
            for tile in doc["tiles"]
        }
        self.assertEqual(len(keys), 16)
        self.assertEqual(doc["tiles"][0]["col"], 0)
        self.assertEqual(doc["tiles"][15]["row"], 3)

    def test_rejects_duplicate_corner_combination(self):
        meta = metadata()
        meta["tileset_data"]["tiles"][1]["corners"] = meta["tileset_data"]["tiles"][0]["corners"]
        with self.assertRaises(ValueError):
            build(meta, png(), "terrain")

    def test_rejects_transition_label(self):
        meta = metadata()
        meta["tileset_data"]["tiles"][0]["corners"]["NW"] = "transition"
        with self.assertRaises(ValueError):
            build(meta, png(), "terrain")

    def test_rejects_unaligned_box_and_unsafe_name(self):
        meta = metadata()
        meta["tileset_data"]["tiles"][0]["bounding_box"]["x"] = 1
        with self.assertRaises(ValueError):
            build(meta, png(), "terrain")
        with self.assertRaises(ValueError):
            build(metadata(), png(), "../terrain")

    def test_rejects_box_bounds_shape_and_tile_count(self):
        for change in (
            lambda m: m["tileset_data"]["tiles"][0]["bounding_box"].update(width=8),
            lambda m: m["tileset_data"]["tiles"][0]["bounding_box"].update(x=64),
            lambda m: m["tileset_data"]["tiles"].pop(),
        ):
            meta = metadata()
            change(meta)
            with self.assertRaises(ValueError):
                build(meta, png(), "terrain")

    def test_output_is_byte_stable(self):
        first = build(metadata(), png(), "terrain")
        second = build(metadata(), png(), "terrain")
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
