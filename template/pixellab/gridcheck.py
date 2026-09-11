"""gridcheck.py —— 像素网格检测（本地 Pillow，plan-13 §5.4）。

PixelLab 下载产物常被放大（1 艺术像素 = N×N 块）。本模块检测整数倍放大并
还原真实网格。

语义（评审 B2 修正）：
- 检测到整数倍放大（因子 ≥2）→ 还原真实网格，返回 (png, factor)。
- 未检测到（factor=1）→ **原生图，接受**（多数 PixelLab 输出即 16/32px 原生）。
  「非整数倍放大/混叠」无法用块一致性证明，故不做拒绝——这是本工具的检测上限，
  已在 AGENTS.md/plan-13 如实记录（此前「一律拒绝」属过度承诺，已改）。
- 因子搜索上限 8×（更大放大的艺术像素块超过检测收益，且 8× 已覆盖常见情况）。
"""

from __future__ import annotations

import io

from PIL import Image

MAX_FACTOR = 8


def _is_block_uniform(im: Image.Image, factor: int) -> bool:
    """图像是否为 factor 整数倍块放大（每块内像素全同）。"""
    w, h = im.size
    px = im.load()
    for by in range(0, h, factor):
        for bx in range(0, w, factor):
            base = px[bx, by]
            for dy in range(factor):
                for dx in range(factor):
                    if px[bx + dx, by + dy] != base:
                        return False
    return True


def detect_and_downscale(data: bytes, name: str) -> tuple[bytes, int]:
    """检测放大倍数并还原；返回 (还原后 PNG bytes, factor)。

    factor=1 = 未检测到整数倍放大（原生图，接受）。factor≥2 = 已还原。
    尺寸过小（<8px）→ ValueError。
    """
    im = Image.open(io.BytesIO(data)).convert("RGBA")
    w, h = im.size
    if w < 8 or h < 8:
        raise ValueError(f"{name}: 尺寸 {w}x{h} 过小（<8px）")
    # 从大到小试因子（大因子优先，避免 2x 误判 4x 图）
    for factor in range(MAX_FACTOR, 1, -1):
        if w % factor == 0 and h % factor == 0 and _is_block_uniform(im, factor):
            small = im.resize((w // factor, h // factor), Image.NEAREST)
            out = io.BytesIO()
            small.save(out, "PNG")
            return out.getvalue(), factor
    return data, 1
