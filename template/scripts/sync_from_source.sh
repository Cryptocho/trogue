#!/usr/bin/env sh
# sync_from_source.sh —— 从 trogue 源仓库刷新 template/ 内的 vendored 快照。
#
# 权威源 = trogue 仓库；template/ 内的 vendored 文件只是快照，禁止手改——
# 一律改源仓库后重跑本脚本。策略：整目录 vendored（engine/pixellab/editor）
# 用整体替换；tools 与 fixture 资产按显式清单逐文件复制（支持含空格文件名）。
# 模板自有文件（README/AGENTS/CMakeLists/scripts/game 起步代码等）永不被覆盖
#（清单见脚本末尾 echo）。
#
# 用法：在 trogue 仓库根执行
#     ./template/scripts/sync_from_source.sh            # 自动定位源仓库根
#     ./template/scripts/sync_from_source.sh <源仓库根>  # 显式指定
set -eu

TPL="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$(cd "$TPL/.." && pwd)}"

echo "[sync] 源仓库 = $SRC"
echo "[sync] 模板   = $TPL"

# ── 复制单个文件（相对源根的路径；支持含空格的文件名）──
copy_file() {
    rel="$1"
    mkdir -p "$TPL/$(dirname "$rel")"
    cp -f "$SRC/$rel" "$TPL/$rel"
}

# ── 目录整体复制：engine / pixellab（pixellab 去 __pycache__）──
for d in engine pixellab; do
    rm -rf "$TPL/$d"
    cp -r "$SRC/$d" "$TPL/$d"
done
find "$TPL/pixellab" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true

# ── editor：只取 tracked 的视觉标注工程（.godot/ 与 assets/ 为本地生成，不入模板）──
rm -rf "$TPL/editor"
mkdir -p "$TPL/editor/addons"
for f in project.godot README.md .editorconfig .gitignore; do
    copy_file "editor/$f"
done
cp -r "$SRC/editor/addons/." "$TPL/editor/addons/"

# ── tools：scene_gen + 引擎级测试（剔除 game_core_test.cpp——它编译 game/src 玩法模块）──
mkdir -p "$TPL/tools/tests"
copy_file "tools/scene_gen.cpp"
for f in "$SRC"/tools/tests/*; do
    base="$(basename "$f")"
    [ "$base" = "game_core_test.cpp" ] && continue
    copy_file "tools/tests/$base"
done

# ── fixture 资产（引擎测试与 pixellab 单测依赖；逐行读取以支持含空格文件名）──
while IFS= read -r rel; do
    [ -z "$rel" ] && continue
    copy_file "$rel"
done <<'FIXTURES'
assets/scenes/demo.json
assets/scenes/test.json
assets/scenes/soldier_animated_sprite_2d.json
assets/tilesets/tile_set.json
assets/tilesets/test_tileset.json
assets/tilesets/test_tileset_1.json
assets/tilesets/pixellab/wang_grass_dirt.json
assets/textures/Decorations.png
assets/textures/Tile Set.png
assets/textures/Soldier.png
assets/textures/Soldier_Attack01.png
assets/textures/Soldier_Attack02.png
assets/textures/Soldier_Attack03.png
assets/textures/Soldier_Death.png
assets/textures/Soldier_Hurt.png
assets/textures/Soldier_Idle.png
assets/textures/Soldier_Walk.png
assets/textures/pixellab/wang_grass_dirt.png
FIXTURES

echo "[sync] 完成。模板自有文件未被触碰："
echo "       README.md / AGENTS.md / .gitignore / CMakeLists.txt / scripts/**"
echo "       tools/CMakeLists.txt / tools/ipc_smoke.py / game/** / assets/scenes/starter.json"
