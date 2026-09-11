#!/usr/bin/env python3
"""pxlab.py —— PixelLab → tro-* 转换 CLI（plan-13 §5.1）。

子命令：
  import-character --meta <json> --name <n> [--fps 8] [--loop walk,idle]
  import-tileset   --meta <json> --name <n> --image <png> --lower <l> --upper <u>
  verify

约定：CWD = 项目根；产物落 assets/（textures/pixellab/、animations/、
tilesets/pixellab/）；manifest 落 assets/pixellab_manifest.json（upsert）。
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from api import fetch_bytes
from character import build as build_character
from gridcheck import detect_and_downscale
from manifest import sha256_bytes, upsert
from tileset import build as build_tileset


def _write(rel: str, data: bytes) -> str:
    full = os.path.join("assets", rel)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(data)
    return rel


def _write_json(rel: str, doc: dict) -> str:
    return _write(rel, (json.dumps(doc, indent=1, ensure_ascii=False) + "\n").encode())


def cmd_import_character(a: argparse.Namespace) -> int:
    meta = json.load(open(a.meta, encoding="utf-8"))
    loop_clips = {s.strip() for s in a.loop.split(",") if s.strip()}
    png, doc = build_character(meta, a.name, a.fps, loop_clips)
    png_rel = _write(f"textures/pixellab/{a.name}.png", png)
    anim_rel = _write_json(f"animations/{a.name}.json", doc)
    upsert("character", meta.get("id", a.name), {
        "name": a.name,
        "download_urls": [u for info in (meta.get("animations") or {}).values()
                          for us in info["directions"].values() for u in us],
        "outputs": [png_rel, anim_rel],
        "sha256": {png_rel: sha256_bytes(png), anim_rel: sha256_bytes(
            (json.dumps(doc, indent=1, ensure_ascii=False) + "\n").encode())},
        "fps": a.fps, "loop": sorted(loop_clips),
    })
    n_clips = len(doc["animations"])
    print(f"OK {a.name}: {png_rel} + {anim_rel} ({n_clips} clips, fps={a.fps})")
    return 0


def cmd_import_tileset(a: argparse.Namespace) -> int:
    meta = json.load(open(a.meta, encoding="utf-8"))
    doc = build_tileset(meta, a.name, a.lower, a.upper)
    raw = fetch_bytes(a.image_url)
    png, _factor = detect_and_downscale(raw, a.name)
    png_rel = _write(f"textures/pixellab/{a.name}.png", png)
    ts_rel = _write_json(f"tilesets/pixellab/{a.name}.json", doc)
    upsert("tileset", meta.get("id", a.name), {
        "name": a.name,
        "download_urls": [a.image_url],
        "outputs": [png_rel, ts_rel],
        "sha256": {png_rel: sha256_bytes(png), ts_rel: sha256_bytes(
            (json.dumps(doc, indent=1, ensure_ascii=False) + "\n").encode())},
        "lower": a.lower, "upper": a.upper,
    })
    print(f"OK {a.name}: {png_rel} + {ts_rel} "
          f"({len(doc['tiles'])} tiles, corners mode)")
    return 0


def cmd_import_map(a: argparse.Namespace) -> int:
    import scene
    grid_text = open(a.grid, encoding="utf-8").read() if a.grid != "-" else sys.stdin.read()
    msg = scene.generate(grid_text, a.tileset, a.scene_name, a.out)
    print(msg)
    return 0


def cmd_verify(_a: argparse.Namespace) -> int:
    import manifest
    ok, bad = manifest.verify()
    for b in bad:
        print(f"FAIL {b}", file=sys.stderr)
    print(f"verify: {ok} ok, {len(bad)} failed")
    return 1 if bad else 0


def main() -> int:
    p = argparse.ArgumentParser(prog="pxlab")
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("import-character")
    c.add_argument("--meta", required=True)
    c.add_argument("--name", required=True)
    c.add_argument("--fps", type=int, default=8)
    c.add_argument("--loop", default="")
    c.set_defaults(fn=cmd_import_character)

    t = sub.add_parser("import-tileset")
    t.add_argument("--meta", required=True)
    t.add_argument("--name", required=True)
    t.add_argument("--image-url", required=True,
                   help="tileset PNG 下载 URL（metadata 不内嵌图像）")
    t.add_argument("--lower", required=True, help="lower terrain 名（=terrain 0）")
    t.add_argument("--upper", required=True, help="upper terrain 名（=terrain 1）")
    t.set_defaults(fn=cmd_import_tileset)

    m = sub.add_parser("import-map")
    m.add_argument("--grid", required=True,
                   help="ASCII 网格文件路径，或 '-' 读 stdin（1/#/G=upper, 0/./D=lower）")
    m.add_argument("--tileset", required=True,
                   help="已导入的 tro-tileset（assets 相对路径）")
    m.add_argument("--scene", required=True, dest="scene_name")
    m.add_argument("--out", required=True, help="产物场景 assets 相对路径")
    m.set_defaults(fn=cmd_import_map)

    v = sub.add_parser("verify")
    v.set_defaults(fn=cmd_verify)

    a = p.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
