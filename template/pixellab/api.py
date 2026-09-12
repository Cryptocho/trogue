"""api.py —— 下载封装（urllib；URL→字节，无鉴权逻辑）。

下载 URL 免鉴权（UUID 即密钥）；可能过期不可重放——产物 + sha256 为权威。
"""

from __future__ import annotations

import urllib.request
from concurrent.futures import ThreadPoolExecutor


def fetch_bytes(url: str, timeout: int = 60) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "trogue-pixellab/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def fetch_many(urls: list[str], max_workers: int = 8,
               timeout: int = 60) -> list[bytes]:
    """并发下载并保持输入顺序；并发数限制在 1..8。"""
    workers = max(1, min(8, int(max_workers)))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(lambda url: fetch_bytes(url, timeout), urls))
