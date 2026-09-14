#!/usr/bin/env python3
"""Bake a UTF-8 character set into a raylib-friendly PNG atlas and metrics JSON."""
import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True,
                        help="字体文件（.ttf/.otf/.ttc；.ttc 为多字体集合，用 --index 选择）")
    parser.add_argument("--chars", required=True, help="字符集文本文件，按 UTF-8 读取")
    parser.add_argument("--out", required=True, help="输出 PNG 路径")
    parser.add_argument("--metrics", required=True, help="输出 JSON 路径")
    parser.add_argument("--size", type=int, default=32)
    parser.add_argument("--columns", type=int, default=16)
    parser.add_argument("--index", type=int, default=0,
                        help=".ttc 内字体索引（Noto CJK 全集：0=JP, 1=KR, 2=SC, 3=TC；缺省 0）")
    args = parser.parse_args()

    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as exc:
        raise SystemExit("需要 Pillow：python3 -m pip install Pillow") from exc

    if args.size <= 0 or args.columns <= 0:
        raise SystemExit("--size 与 --columns 必须为正数")
    if args.index < 0:
        raise SystemExit("--index 必须为非负整数")
    chars = list(dict.fromkeys(Path(args.chars).read_text(encoding="utf-8")))
    if not chars:
        raise SystemExit("字符集为空")
    font = ImageFont.truetype(args.font, args.size, index=args.index)
    cell_w = max(font.getlength(ch) for ch in chars)
    ascent, descent = font.getmetrics()
    cell_w = max(1, int(cell_w + 2))
    cell_h = max(1, ascent + descent + 2)
    rows = (len(chars) + args.columns - 1) // args.columns
    image = Image.new("RGBA", (cell_w * args.columns, cell_h * rows), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    glyphs = {}
    for i, ch in enumerate(chars):
        x, y = (i % args.columns) * cell_w, (i // args.columns) * cell_h
        draw.text((x + 1, y + 1), ch, font=font, fill=(255, 255, 255, 255))
        glyphs[ch] = {"region": [x, y, cell_w, cell_h], "advance": font.getlength(ch)}
    out = Path(args.out)
    metrics = Path(args.metrics)
    out.parent.mkdir(parents=True, exist_ok=True)
    metrics.parent.mkdir(parents=True, exist_ok=True)
    image.save(out)
    metrics.write_text(json.dumps({"font_size": args.size, "glyphs": glyphs}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
