"""manifest.py —— 来源 manifest 读写（upsert）。

assets/pixellab_manifest.json：按 (源类型, 源 id) upsert，重跑不产生重复条目。
字段：source_type / source_id / download_urls / outputs / sha256 / imported_at。
导入时间只进 manifest 不进产物（保产物确定性）；verify 按 sha256 复核。
"""

from __future__ import annotations

import hashlib
import json
import os
import time


MANIFEST_PATH = os.path.join("assets", "pixellab_manifest.json")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load() -> dict:
    if not os.path.exists(MANIFEST_PATH):
        return {"entries": []}
    with open(MANIFEST_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def upsert(source_type: str, source_id: str, entry: dict) -> None:
    doc = load()
    entries = doc.setdefault("entries", [])
    key = (source_type, source_id)
    for i, e in enumerate(entries):
        if (e.get("source_type"), e.get("source_id")) == key:
            entries[i] = {**e, **entry, "source_type": source_type,
                          "source_id": source_id,
                          "imported_at": time.strftime("%Y-%m-%dT%H:%M:%S")}
            break
    else:
        entries.append({**entry, "source_type": source_type, "source_id": source_id,
                        "imported_at": time.strftime("%Y-%m-%dT%H:%M:%S")})
    tmp = MANIFEST_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, MANIFEST_PATH)


def verify() -> tuple[int, list[str]]:
    """按 sha256 复核产物；返回 (通过数, 失败描述列表)。

    manifest 中的产物路径为 assets 相对路径（与 tro-* 引用一致），
    复核时拼 assets/ 前缀。
    """
    doc = load()
    ok, bad = 0, []
    for e in doc.get("entries", []):
        for rel, digest in (e.get("sha256") or {}).items():
            full = os.path.join("assets", rel)
            if not os.path.exists(full):
                bad.append(f"{rel}: 缺失")
            elif sha256_bytes(open(full, "rb").read()) != digest:
                bad.append(f"{rel}: sha256 不符")
            else:
                ok += 1
    return ok, bad
