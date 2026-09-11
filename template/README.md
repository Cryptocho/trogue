# trogue 游戏项目模板

用 [trogue](https://github.com/Cryptocho/trogue) 引擎从零开发一个游戏的**起点**。
本目录是一个自包含的项目骨架：复制它、构建它、然后在 `game/` 里写你的游戏。

## 创建新项目

```bash
# 在 trogue 仓库内：
./template/scripts/new_project.sh ../my-game
cd ../my-game
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/bin/trogue            # 从项目根运行（资产按 CWD assets/ 约定读取）
```

`new_project.sh` 复制模板并剥离仅服务模板本体的文件（`scripts/`、`README.md`），
产出的项目保留 `AGENTS.md`（Agent 开发指南）与全部引擎/编辑器/资产管线。

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
快照**，权威源是 trogue 仓库——**不要在本项目里手改**；上游更新后，在 trogue 仓库内
重跑 `template/scripts/sync_from_source.sh` 刷新。

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
