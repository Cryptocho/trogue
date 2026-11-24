"""Check Bevy window PNGs, not intermediate canvas textures, for pixel-grid leaks.

uv run --project tools/gloam/terrain tools/gloam/paper/check_grid.py reference/gloam-comparison/2d*.png
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from palette import RAMPS

INKS = {int(code, 16) for ramp in RAMPS for code in ramp.split()}


def inspect(path):
    with Image.open(path) as src:
        frame = np.asarray(src.convert('RGB'))
    h, w = frame.shape[:2]
    k = min(w//512, h//288)
    if not k:
        raise ValueError(f'{path} 小于 512×288；该尺寸在桌面上采用 1:1 裁切')
    x, y = (w-512*k)//2, (h-288*k)//2
    # A single supersampled fog pass, bilinear presentation, or half-pixel
    # origin changes the contents of at least one of the k×k squares.
    cells = frame[y:y+288*k, x:x+512*k].reshape(288,k,512,k,3)
    native = cells[:,0,:,0]
    mixed = np.any(cells != native[:,None,:,None,:], axis=(1,3,4)).sum()
    rgb = native.astype(np.uint32)
    used = {int(v) for v in np.unique((rgb[:,:,0]<<16) | (rgb[:,:,1]<<8) | rgb[:,:,2])}
    unexpected = used - INKS
    print(f'{path.name}: {w}×{h}, {k}×, {len(used)} inks, {mixed} mixed cells, {len(unexpected)} off-palette colors')
    if mixed or unexpected or len(used) == 1:
        raise ValueError(f'{path}: 主窗口没有输出预期的完整像素画')


if __name__ == '__main__':
    if len(sys.argv) == 1:
        raise SystemExit('用法：check_grid.py 游戏主窗口截图.png [...]')
    for filename in sys.argv[1:]:
        inspect(Path(filename))
