#!/usr/bin/env sh
# sync_from_source.sh —— 从上游 trogue 仓库取模板，安装/更新到**当前目录**。
#
# 一份脚本覆盖两种场景：
#   1) 新建项目：在空目录里运行 → 把上游模板铺到当前目录（含起步 game/）。
#   2) 更新引擎：在已有项目里运行 → 只刷新 vendored 快照
#      （engine/、tools/scene_gen.cpp 及已安装的可选快照），**不动**你的游戏
#      代码、资产与项目自有文件（game/、assets/、CMakeLists.txt、README.md、
#      .gitignore、AGENTS.md、tools/CMakeLists.txt、tools/ipc_smoke.py）。
#
# 上游以**临时克隆**方式获取（默认 --depth 1），用完即删——你不需要先克隆整个
# 仓库，也不用事后清理。
#
# 用法：
#   ./scripts/sync_from_source.sh [选项]
#     --url <repo>     上游仓库 URL（缺省 $TROGUE_URL 或内置默认）
#     --ref <ref>      上游分支/标签（缺省 $TROGUE_REF 或内置默认）
#     --source <dir>   用本地 trogue 源仓库根代替克隆（开发/离线用）
#     --with-pixellab  安装/刷新 pixellab/ 转换工具快照（可选，缺省不装）
#     --with-editor    安装/刷新 editor/ Godot 工程快照（可选，缺省不装）
#     --full           连项目自有文件也覆盖（整份模板重置；会覆盖 game/ 等，谨慎）
#     -h | --help
#
# 不依赖第三方工具：仅需 git 与基本 POSIX 工具（cp/rm/mktemp/find）。
#
# 维护者模式：若当前目录**就是**源仓库里的 template/，则把源仓库根的 vendored
# 文件（engine/、pixellab/… ）刷进 template/（供上游维护者更新快照）。
set -eu

# ── 内置默认（可用 --url/--ref 或环境变量覆盖）──
DEF_URL="https://github.com/Cryptocho/trogue.git"
DEF_REF="trogue-raylib"   # 注意：远端 main 是历史 Lua 项目，C++ 引擎在 trogue-raylib

URL="${TROGUE_URL:-$DEF_URL}"
REF="${TROGUE_REF:-$DEF_REF}"
SOURCE_ARG=""
FULL=0
WITH_PIXELLAB=0
WITH_EDITOR=0

while [ $# -gt 0 ]; do
    case "$1" in
        --url)    URL="$2"; shift 2 ;;
        --ref)    REF="$2"; shift 2 ;;
        --source) SOURCE_ARG="$2"; shift 2 ;;
        --with-pixellab) WITH_PIXELLAB=1; shift ;;
        --with-editor)   WITH_EDITOR=1; shift ;;
        --full)   FULL=1; shift ;;
        -h|--help)
            sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "[sync] 未知参数: $1（用 --help 查看用法）" >&2
            exit 2 ;;
    esac
done

# 遮盖 URL 内嵌凭证（https://user:token@host/... → https://***@host/...），防日志外泄。
mask_url() {
    printf '%s\n' "$1" | sed 's#\(://\)[^/@]*@#\1***@#'
}

DEST="$(pwd -P)"

# ── 安全护栏（先于任何网络/写入）：DEST 不得是源仓库根 ──
# 源仓库根同时含 engine/ 与 template/；误跑会把快照反向写回真实源码树，最危险。
if [ -d "$DEST/engine" ] && [ -d "$DEST/template" ]; then
    echo "[sync] 拒绝：当前目录看起来是 trogue 源仓库根（同时含 engine/ 与 template/）。" >&2
    echo "       本脚本应作用于**你的游戏项目**目录；维护者刷新快照请 cd template 后运行。" >&2
    exit 1
fi

# ── 解析源仓库根 SRC（含 template/ 的 trogue 仓库）──
TMP=""
cleanup() { if [ -n "$TMP" ]; then rm -rf "$TMP"; fi; }
trap cleanup EXIT INT TERM

resolve_src() {
    if [ -n "$SOURCE_ARG" ]; then
        printf '%s\n' "$SOURCE_ARG"
    elif [ -n "${TROGUE_SOURCE:-}" ]; then
        printf '%s\n' "$TROGUE_SOURCE"
    elif [ -d "$DEST/../template" ] && [ -d "$DEST/../engine" ]; then
        # 本地同仓（template/ 的兄弟目录即源仓库根）
        (cd "$DEST/.." && pwd -P)
    else
        TMP="$(mktemp -d)"
        # 进度信息走 stderr：stdout 被命令替换捕获为 SRC，绝不能污染。
        echo "[sync] 克隆上游 $(mask_url "$URL") ($REF) 到临时目录…" >&2
        if ! git clone --depth 1 --branch "$REF" "$URL" "$TMP/trogue" >/dev/null 2>&1; then
            echo "[sync] 克隆失败。若仓库为私有/需鉴权，请用 --url 传入带凭证的 URL。" >&2
            exit 1
        fi
        printf '%s\n' "$TMP/trogue"
    fi
}

SRC="$(resolve_src)"
SRC="$(cd "$SRC" && pwd -P)"
TPL="$SRC/template"

if [ ! -d "$TPL" ]; then
    echo "[sync] 源仓库缺少 template/：$TPL" >&2
    exit 1
fi

# 维护者模式：当前目录就是源仓库里的 template/ 本体
SRC_IS_SELF=no
if [ "$(cd "$DEST/.." 2>/dev/null && pwd -P)" = "$SRC" ] && [ "$(basename "$DEST")" = "template" ]; then
    SRC_IS_SELF=yes
fi

# 是否已有项目（决定「新建铺陈」还是「仅刷新快照」；先于任何写入判定）。
# 仅有空目录不算项目：安装器可能先创建 game/ 等目录，空目录应由模板内容覆盖。
HAS_PROJECT=no
if [ -f "$DEST/CMakeLists.txt" ] ||
   [ -n "$(find "$DEST/game" -type f -print -quit 2>/dev/null)" ]; then
    HAS_PROJECT=yes
fi

# ── 拷贝助手 ──
copy_file() {  # $1=源绝对路径  $2=目标相对路径（相对 DEST）
    mkdir -p "$DEST/$(dirname "$2")"
    cp -f "$1" "$DEST/$2"
}

if [ "$SRC_IS_SELF" = "yes" ]; then
    # ── 维护者模式：源仓库根 → template/（刷新自身 vendored 快照）──
    echo "[sync] 维护者模式：刷新 $DEST 的快照（源 = $SRC）"
    VROOT="$SRC"
    VENDORED_FROM_ROOT=1
else
    # ── 下游模式：上游 template/ → 当前项目 ──
    VROOT="$TPL"
    VENDORED_FROM_ROOT=0
    if [ "$HAS_PROJECT" = "yes" ]; then
        echo "[sync] 更新模式：刷新 vendored 快照到 $DEST（保留项目自有文件）"
    else
        echo "[sync] 新建模式：把上游模板铺到 $DEST"
    fi
fi

# ── 硬性 vendored 集合：整目录替换 / 逐文件覆盖 ──
# engine（整目录，避免残留旧文件）
rm -rf "$DEST/engine"
cp -r "$VROOT/engine" "$DEST/engine"

# pixellab：仅顶层转换脚本（与模板约定一致；不含 tests/fixtures）——可选快照
if [ "$WITH_PIXELLAB" = "1" ]; then
    rm -rf "$DEST/pixellab"
    mkdir -p "$DEST/pixellab"
    for f in "$VROOT"/pixellab/*.py; do
        [ -e "$f" ] || continue
        copy_file "$f" "pixellab/$(basename "$f")"
    done
fi

# editor：视觉标注工程（仅 tracked 内容）——可选快照
if [ "$WITH_EDITOR" = "1" ]; then
    rm -rf "$DEST/editor"
    mkdir -p "$DEST/editor/addons"
    for f in project.godot README.md .editorconfig .gitignore; do
        [ -e "$VROOT/editor/$f" ] && copy_file "$VROOT/editor/$f" "editor/$f" || true
    done
    [ -d "$VROOT/editor/addons" ] && cp -r "$VROOT/editor/addons/." "$DEST/editor/addons/" || true
fi

# tools：仅离线场景生成 CLI
copy_file "$VROOT/tools/scene_gen.cpp" "tools/scene_gen.cpp"

# 下游模式：刷新更新器自身（脚本是工具，非项目自有；保证下次仍能自更新）
if [ "$VENDORED_FROM_ROOT" = "0" ] && [ -e "$TPL/scripts/sync_from_source.sh" ]; then
    mkdir -p "$DEST/scripts"
    copy_file "$TPL/scripts/sync_from_source.sh" "scripts/sync_from_source.sh"
fi

# ── 模板铺陈（新建/--full 时）：项目自有文件一并铺上 ──
if [ "$VENDORED_FROM_ROOT" = "0" ] && { [ "$HAS_PROJECT" = "no" ] || [ "$FULL" = "1" ]; }; then
    # 铺整份模板，再剥离仅服务模板本体的文件
    mkdir -p "$DEST"
    for entry in "$TPL"/* "$TPL"/.[!.]*; do
        [ -e "$entry" ] || continue
        base="$(basename "$entry")"
        case "$base" in
            engine|pixellab|editor) continue ;;  # 已按 vendored 规则处理
            scripts) continue ;;                  # 单独处理（见下）
        esac
        if [ -d "$entry" ]; then
            rm -rf "$DEST/$base"
            cp -r "$entry" "$DEST/$base"
        else
            cp -f "$entry" "$DEST/$base"
        fi
    done
    # scripts/：只保留更新器；剥离引导脚本与模板 README
    mkdir -p "$DEST/scripts"
    [ -e "$TPL/scripts/sync_from_source.sh" ] && \
        copy_file "$TPL/scripts/sync_from_source.sh" "scripts/sync_from_source.sh"
    rm -f "$DEST/scripts/new_project.sh"
    rm -f "$DEST/README.md"   # 模板使用说明；独立项目自写 README
    rmdir "$DEST/scripts" 2>/dev/null || true
    echo "[sync] 已铺陈模板自有文件（CMakeLists/.gitignore/AGENTS.md/tools/game 等）"
else
    if [ "$VENDORED_FROM_ROOT" = "0" ]; then
        echo "[sync] 已保留项目自有文件（未覆盖）：game/ assets/ CMakeLists.txt README.md .gitignore AGENTS.md tools/CMakeLists.txt tools/ipc_smoke.py"
    fi
fi

# 清掉可能带入的构建产物 / Python 缓存
rm -rf "$DEST/build" "$DEST/build-release"
find "$DEST" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true

echo "[sync] 完成。"
if [ "$VENDORED_FROM_ROOT" = "1" ]; then
    echo "       下一步（上游维护者）：git add template/ && 提交"
else
    if [ "$WITH_PIXELLAB" = "0" ] || [ "$WITH_EDITOR" = "0" ]; then
        echo "       可选快照：pixellab/（--with-pixellab）、editor/（--with-editor）未安装/未刷新。"
    fi
    echo "       下一步：cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
fi
