#!/usr/bin/env sh
# sync_from_source.sh —— 从 trogue 源仓库刷新 template/ 内的 vendored 快照。
#
# 权威源 = trogue 仓库；template/ 内的 vendored 文件只是快照，禁止手改——
# 一律改源仓库后重跑本脚本。策略：engine/pixellab/editor 整目录替换；
# tools/ 逐文件复制。模板自有文件（README/AGENTS/CMakeLists/scripts/game 等）
# 永不被覆盖（清单见脚本末尾 echo）。
#
# 用法：在 trogue 仓库根执行
#     ./template/scripts/sync_from_source.sh            # 自动定位源仓库根
#     ./template/scripts/sync_from_source.sh <源仓库根>  # 显式指定
set -eu

TPL="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$(cd "$TPL/.." && pwd)}"

echo "[sync] 源仓库 = $SRC"
echo "[sync] 模板   = $TPL"

copy_file() {
    rel="$1"
    mkdir -p "$TPL/$(dirname "$rel")"
    cp -f "$SRC/$rel" "$TPL/$rel"
}

# ── engine：整目录（引擎库是模板的核心）──
rm -rf "$TPL/engine"
cp -r "$SRC/engine" "$TPL/engine"

# ── pixellab：仅顶层转换脚本（不含 tests/fixtures——那是本仓库自用的测试素材）──
rm -rf "$TPL/pixellab"
mkdir -p "$TPL/pixellab"
for f in "$SRC"/pixellab/*.py; do
    copy_file "pixellab/$(basename "$f")"
done

# ── editor：只取 tracked 的视觉标注工程（.godot/ 与 assets/ 为本地生成）──
rm -rf "$TPL/editor"
mkdir -p "$TPL/editor/addons"
for f in project.godot README.md .editorconfig .gitignore; do
    copy_file "editor/$f"
done
cp -r "$SRC/editor/addons/." "$TPL/editor/addons/"

# ── tools：仅离线场景生成 CLI ──
copy_file "tools/scene_gen.cpp"

echo "[sync] 完成。模板自有文件未被触碰："
echo "       README.md / AGENTS.md / CMakeLists.txt / scripts/** / game/** / assets/**"
echo "       tools/CMakeLists.txt / tools/ipc_smoke.py"
