# trogue 游戏项目模板

用 [trogue](https://github.com/Cryptocho/trogue) 引擎从零开发一个游戏的**起点**。
本目录是一个自包含的项目骨架：复制它、构建它、然后在 `game/` 里写你的游戏。

## 创建新项目

```bash
# 在任意空目录（不需要先克隆整个仓库）：
mkdir my-game && cd my-game
# 取到本脚本（二选一）：
#   A. 直接下载：
curl -fsSL https://raw.githubusercontent.com/Cryptocho/trogue/trogue-raylib/template/scripts/sync_from_source.sh -o sync_from_source.sh
#   B. 或从已克隆的 trogue 仓库拷：cp <repo>/template/scripts/sync_from_source.sh .
chmod +x sync_from_source.sh
./sync_from_source.sh                      # 拉取上游模板铺到当前目录
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/bin/trogue                         # 从项目根运行（资产按 CWD assets/ 约定读取）
```

脚本从上游仓库**临时克隆**（`--depth 1`，用完即删）取模板，你无需克隆整个仓库、
也无需事后清理。常用选项：

| 选项 | 作用 |
|------|------|
| `--url <repo>` | 上游仓库 URL（缺省内置；私有库可传带凭证的 URL） |
| `--ref <ref>` | 上游分支/标签（缺省 `trogue-raylib`） |
| `--source <dir>` | 用本地 trogue 源仓库代替克隆（离线/开发） |
| `--full` | 连项目自有文件也覆盖（整份模板重置；慎用） |

## 更新到最新引擎

上游修了引擎后，在你的项目根直接重跑同一个脚本：

```bash
./scripts/sync_from_source.sh
```

它只刷新 vendored 快照（`engine/`、`pixellab/`、`editor/`、`tools/scene_gen.cpp`
与更新器自身），**不动**你的 `game/`、`assets/`、`CMakeLists.txt`、`README.md`、
`.gitignore`、`AGENTS.md`、`tools/CMakeLists.txt`、`tools/ipc_smoke.py`。

## 目录

| 目录 | 内容 | 归属 |
|------|------|------|
| `engine/` | trogue 引擎静态库（C++20，依赖 raylib + nlohmann/json + tl::expected） | **快照**（勿手改） |
| `editor/` | Godot 4.7 可选视觉标注/导出工程（scene_exporter v4） | **快照**（勿手改） |
| `pixellab/` | PixelLab MCP → tro-* 资产转换层（Python） | **快照**（勿手改） |
| `tools/` | 离线场景生成 CLI `scene_gen` + `ipc_smoke.py` | 见下 |
| `game/` | **你的游戏**（起步骨架，随意改写） | 项目自有 |
| `assets/` | 你的资产（模板不含资产文件，按需自建） | 项目自有 |

## 快照与同步

`engine/`、`pixellab/`、`editor/`、`tools/scene_gen.cpp` 是**从 trogue 仓库复制来的
快照**，权威源是 trogue 仓库——**不要在本项目里手改**；上游更新后，在项目根重跑
`./scripts/sync_from_source.sh` 刷新（见上「更新到最新引擎」）。

模板自有（可自由修改）：`CMakeLists.txt`、`.gitignore`、`tools/CMakeLists.txt`、
`tools/ipc_smoke.py`、`game/**`、`assets/**`。

## 起步内容

- `game/src/main.cpp`：窗口 + 场景渲染 + WASD 单格移动（引擎 TweenManager 驱动、
  播完精确落格）+ 热重载 + IPC（`status`/`list_entities`/`get_entity`/`move`/
  `screenshot`/`log`/`quit`）。
- 内置起步场景：`game/src/main.cpp` 用 `tg::SceneAsset::load_json` **在内存里构造**一个
  20×15 palette 场景（四面墙 + 玩家 + 木箱），因此模板**零资产文件**即可运行；要换成
  磁盘场景，加 `assets/scenes/*.json` 并 `--scene` 指定（热重载随之启用）。

## 开发命令

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/bin/trogue [--scene assets/scenes/xxx.json] [--port 48764]
python3 tools/ipc_smoke.py                      # 游戏运行中时（DEBUG 构建）
```

依赖（系统包管理器或 CMake FetchContent）：raylib 6.0、nlohmann/json 3.11+、
tl::expected 1.x、支持 C++20 协程的编译器。

## 更多

面向 Agent 的开发指南见 `AGENTS.md`（引擎公共 API 边界、tro-* 资产 schema、
IPC 协议、调试工作流、PixelLab 管线）。
