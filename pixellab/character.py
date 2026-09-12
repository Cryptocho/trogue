"""character.py —— 角色帧序列 → tro-animations v1 + spritesheet。

spritesheet 布局（确定性）：每 clip 一行、行高 = 该行最大帧高、帧从左到右
紧密排列。任一边 > 4096px 拒绝。clip 命名：多方向 <anim>_<direction>，
单方向 <anim>。fps/loop 为 CLI 显式参数（PixelLab 不提供）。
"""

from __future__ import annotations

import io

from PIL import Image

from api import fetch_many
from gridcheck import detect_and_downscale

SHEET_MAX = 4096




def build(meta: dict, name: str, fps: int, loop_clips: set[str],
          max_workers: int = 8) -> tuple[bytes, dict]:
    """从角色元数据构建 spritesheet + tro-animations 文档。

    返回 (png_bytes, animations_doc)。帧 URL 来自 meta['animations']。
    """
    anims = meta.get("animations") or {}
    if not anims:
        raise ValueError(f"{name}: 元数据无 animations（先 animate_character）")

    # 先固定输入顺序，再并发下载；ThreadPoolExecutor.map 保持结果顺序。
    clip_specs: list[tuple[str, list[str]]] = []
    seen_clips: set[str] = set()
    urls: list[str] = []
    for anim_name, info in anims.items():
        dirs = info["directions"]
        multi = len(dirs) > 1
        for d in sorted(dirs):
            clip = f"{anim_name}_{d}" if multi else anim_name
            if clip in seen_clips:
                raise ValueError(f"{name}: clip 名重复 {clip!r}；请隔离 rotation/animation 命名")
            seen_clips.add(clip)
            frame_urls = list(dirs[d])
            clip_specs.append((clip, frame_urls))
            urls.extend(frame_urls)
    raw_frames = fetch_many(urls, max_workers=max_workers)
    frames_by_url: dict[int, Image.Image] = {}
    for index, (url, raw) in enumerate(zip(urls, raw_frames)):
        normalized, _factor = detect_and_downscale(raw, f"{name} ({url})")
        frames_by_url[index] = Image.open(io.BytesIO(normalized)).convert("RGBA")
    clips: list[tuple[str, list[Image.Image]]] = []
    cursor = 0
    for clip, frame_urls in clip_specs:
        frames = [frames_by_url[cursor + i] for i in range(len(frame_urls))]
        cursor += len(frame_urls)
        clips.append((clip, frames))

    # 布局：每 clip 一行
    row_heights = [max(f.height for f in fs) for _, fs in clips]
    sheet_w = max(sum(f.width for f in fs) for _, fs in clips)
    sheet_h = sum(row_heights)
    if sheet_w > SHEET_MAX or sheet_h > SHEET_MAX:
        raise ValueError(f"{name}: spritesheet {sheet_w}x{sheet_h} 超过 {SHEET_MAX} 上限")

    sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))
    textures = [f"textures/pixellab/{name}.png"]
    animations_doc = []
    y = 0
    for (clip, frames), rh in zip(clips, row_heights):
        frames_doc = []
        x = 0
        for i, f in enumerate(frames):
            sheet.paste(f, (x, y))
            frames_doc.append({"texture": 0, "region": [x, y, f.width, f.height]})
            x += f.width
        animations_doc.append({"name": clip, "fps": fps, "loop": clip in loop_clips,
                               "frames": frames_doc})
        y += rh

    out = io.BytesIO()
    sheet.save(out, "PNG")
    doc = {"format": "tro-animations", "version": 1, "textures": textures,
           "animations": animations_doc}
    return out.getvalue(), doc
