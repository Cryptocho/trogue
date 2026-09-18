#!/usr/bin/env python3
"""pxlab.py —— PixelLab → tro-* 转换 CLI。

子命令：
  import-character --meta <json> --name <n> [--fps 8] [--loop walk,idle] [--jobs 8]
  import-character-sheet --character-id <id> --sheet-url <zip> --name <n>
  import-tileset --meta <json> --image <png> --name <n>
  check-grid --image <png>
  verify

约定：CWD = 项目根；产物落 assets/（textures/pixellab/、animations/）；
manifest 落 assets/pixellab_manifest.json（upsert）。

PixelLab 的标准 16-tile tileset15 通过 import-tileset 转为 tro-tileset v2
的 dual_grid 角组合表；普通 cell-terrain 占位集仍由 tools/placeholder_tileset.py
生成，PixelLab 的 dual-grid 选择不伪装成 cell terrain pool。
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
from sheet import build as build_sheet
from tileset import build as build_tileset
from manifest import sha256_bytes, upsert


def _write(rel: str, data: bytes) -> str:
    full = os.path.join("assets", rel)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(data)
    return rel


def _json_bytes(doc: dict) -> bytes:
    return (json.dumps(doc, indent=1, ensure_ascii=False) + "\n").encode()


def _write_json(rel: str, doc: dict) -> str:
    return _write(rel, _json_bytes(doc))


def cmd_import_character(a: argparse.Namespace) -> int:
    meta = json.load(open(a.meta, encoding="utf-8"))
    loop_clips = {s.strip() for s in a.loop.split(",") if s.strip()}
    png, doc = build_character(meta, a.name, a.fps, loop_clips, a.jobs)
    png_rel = _write(f"textures/pixellab/{a.name}.png", png)
    anim_rel = _write_json(f"animations/{a.name}.json", doc)
    upsert("character", meta.get("id", a.name), {
        "name": a.name,
        "download_urls": [u for info in (meta.get("animations") or {}).values()
                          for us in info["directions"].values() for u in us],
        "outputs": [png_rel, anim_rel],
        "sha256": {png_rel: sha256_bytes(png), anim_rel: sha256_bytes(_json_bytes(doc))},
        "fps": a.fps, "loop": sorted(loop_clips),
    })
    n_clips = len(doc["animations"])
    print(f"OK {a.name}: {png_rel} + {anim_rel} ({n_clips} clips, fps={a.fps})")
    return 0


def cmd_import_tileset(a: argparse.Namespace) -> int:
    meta = json.load(open(a.meta, encoding="utf-8"))
    png = open(a.image, "rb").read()
    output_png, doc = build_tileset(meta, png, a.name)
    png_rel = _write(f"textures/pixellab/{a.name}.png", output_png)
    tileset_rel = _write_json(f"tilesets/{a.name}.json", doc)
    source_id = str(meta.get("id", a.name))
    upsert("tileset", source_id, {
        "name": a.name,
        "download_urls": [u for u in (a.metadata_url, a.image_url) if u],
        "outputs": [png_rel, tileset_rel],
        "sha256": {
            png_rel: sha256_bytes(output_png),
            tileset_rel: sha256_bytes(_json_bytes(doc)),
        },
    })
    print(f"OK {a.name}: {png_rel} + {tileset_rel} (16 dual-grid tiles)")
    return 0


def cmd_import_character_sheet(a: argparse.Namespace) -> int:
    raw = fetch_bytes(a.sheet_url)
    loop_clips = {s.strip() for s in a.loop.split(",") if s.strip()}
    png, doc = build_sheet(raw, a.name, a.fps, loop_clips)
    png_rel = _write(f"textures/pixellab/{a.name}.png", png)
    anim_rel = _write_json(f"animations/{a.name}.json", doc)
    upsert("character_sheet", a.character_id, {
        "name": a.name,
        "download_urls": [a.sheet_url],
        "outputs": [png_rel, anim_rel],
        "sha256": {png_rel: sha256_bytes(png), anim_rel: sha256_bytes(_json_bytes(doc))},
        "fps": a.fps, "loop": sorted(loop_clips),
    })
    print(f"OK {a.name}: {png_rel} + {anim_rel} ({len(doc['animations'])} clips, v2 sheet)")
    return 0


def cmd_verify(_a: argparse.Namespace) -> int:
    import manifest
    ok, bad = manifest.verify()
    for b in bad:
        print(f"FAIL {b}", file=sys.stderr)
    print(f"verify: {ok} ok, {len(bad)} failed")
    return 1 if bad else 0


def cmd_check_grid(a: argparse.Namespace) -> int:
    from PIL import Image
    import io
    raw = open(a.image, "rb").read()
    normalized, factor = detect_and_downscale(raw, a.image)
    with Image.open(io.BytesIO(normalized)) as image:
        width, height = image.size
    print(f"grid: factor={factor} size={width}x{height} "
          f"action={'downscale' if factor > 1 else 'accept-native'}")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(prog="pxlab")
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("import-character")
    c.add_argument("--meta", required=True)
    c.add_argument("--name", required=True)
    c.add_argument("--fps", type=int, default=8)
    c.add_argument("--loop", default="")
    c.add_argument("--jobs", type=int, default=8,
                   help="角色帧下载并发数（1..8，默认 8；输出顺序保持确定）")
    c.set_defaults(fn=cmd_import_character)

    t = sub.add_parser("import-tileset")
    t.add_argument("--meta", required=True)
    t.add_argument("--image", required=True)
    t.add_argument("--name", required=True)
    t.add_argument("--metadata-url", default="")
    t.add_argument("--image-url", default="")
    t.set_defaults(fn=cmd_import_tileset)

    s = sub.add_parser("import-character-sheet")
    s.add_argument("--character-id", required=True)
    s.add_argument("--sheet-url", required=True,
                   help="API v2 /characters/{id}/spritesheet ZIP URL")
    s.add_argument("--name", required=True)
    s.add_argument("--fps", type=int, default=8)
    s.add_argument("--loop", default="")
    s.set_defaults(fn=cmd_import_character_sheet)

    v = sub.add_parser("verify")
    v.set_defaults(fn=cmd_verify)

    g = sub.add_parser("check-grid")
    g.add_argument("--image", required=True, help="待检查的 PNG/JPEG")
    g.set_defaults(fn=cmd_check_grid)

    a = p.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
