#!/usr/bin/env sh
# new_project.sh —— 由本模板创建一个新的独立游戏项目。
#
# 复制 template/（引擎 + editor + pixellab + tools + 起步 game + 资产）到目标
# 目录，并剥离**仅对模板本体有意义**的文件（见下方 STRIP 清单）。产出的项目
# 可立即构建、运行，并可交给 Agent 继续开发。
#
# 用法：
#     ./scripts/new_project.sh <目标目录>
# 例：
#     ./scripts/new_project.sh ../my-roguelike
set -eu

TPL="$(cd "$(dirname "$0")/.." && pwd)"

if [ $# -ne 1 ]; then
    echo "用法: $0 <目标目录>" >&2
    exit 2
fi
DST="$1"

if [ -e "$DST" ]; then
    echo "[new] 目标已存在，拒绝覆盖: $DST" >&2
    exit 1
fi

echo "[new] 模板   = $TPL"
echo "[new] 目标   = $DST"
mkdir -p "$DST"

# 复制模板全部内容（含 .gitignore 等点文件）
cp -r "$TPL/." "$DST/"

# ── 剥离清单（这些只服务模板本体，独立项目不需要）──
#   scripts/sync_from_source.sh —— 在独立项目里指向不存在的 trogue 源仓库
#   scripts/new_project.sh      —— 自举脚本，派生项目不再需要
#   README.md                   —— 模板使用说明（独立项目应自写 README）
rm -f "$DST/scripts/sync_from_source.sh"
rm -f "$DST/scripts/new_project.sh"
rm -f "$DST/README.md"
rmdir "$DST/scripts" 2>/dev/null || true

# 清掉构建产物 / Python 缓存（若模板工作区曾构建过）
rm -rf "$DST/build" "$DST/build-release"
find "$DST" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true

echo "[new] 完成。下一步："
echo "       cd $DST"
echo "       cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
echo "       ./build/bin/trogue"
echo "     Agent 指南见 $DST/AGENTS.md"
