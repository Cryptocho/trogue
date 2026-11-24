"""Measure the 32° forest shot, not arbitrary pictures.

uv run --project tools/gloam/terrain tools/gloam/measure_focus.py DEEP_FOCUS.png TILT_FOCUS.png
Only switch the lens between captures. This measures lost fine edges, not visual quality.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

# Pixel rectangles in the original 2560 × 1440 render (left, top, right, bottom).
REGIONS = {
    '前景橡树冠': (50, 280, 295, 450),
    '旅人': (1292, 580, 1390, 770),
    '人物旁的路面': (1190, 725, 1280, 805),
    '远处冷杉': (732, 36, 1022, 174),
}


def edge_strength(path):
    with Image.open(path) as image:
        if image.size != (2560, 1440):
            raise ValueError(f'{path}: 需要 2560×1440 的原始主窗口截图')
        result = {}
        for name, box in REGIONS.items():
            # Display-referred 8-bit luma (Pillow L), equally weighted X/Y gradient.
            y = np.asarray(image.crop(box).convert('L'), dtype=np.float32)
            result[name] = (abs(np.diff(y, axis=0)).mean() + abs(np.diff(y, axis=1)).mean()) * 0.5
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Compare the same forest frame with and without depth of field')
    parser.add_argument('sharp', type=Path, help='--deep-focus 截图')
    parser.add_argument('focused', type=Path, help='有景深的默认截图')
    args = parser.parse_args()
    base, lens = edge_strength(args.sharp), edge_strength(args.focused)
    print('区域\t关景深\t开景深\t边缘强度保留率')
    for name in REGIONS:
        ratio = f'{100*lens[name]/base[name]:.1f}%' if base[name] > 0 else '无可测边缘'
        print(f'{name}\t{base[name]:.3f}\t{lens[name]:.3f}\t{ratio}')
