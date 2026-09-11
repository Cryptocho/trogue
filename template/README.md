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

`new_project.sh` 会复制模板并剥离仅服务模板本体的文件（`scripts/`、`README.md`）。
产出的项目保留 `AGENTS.md`（Agent 开发指南）与全部引擎/编辑器/资产管线。

## 目录

| 目录 | 内容 | 归属 |
|------|------|------|
| `engine/` | trogue 引擎静态库（C++20，依赖 raylib + nlohmann/json + tl::expected） | **快照**（勿手改） |
| `editor/` | Godot 4.7 可选视觉标注/导出工程（scene_exporter v4） | **快照**（勿手改） |
| `pixellab/` | PixelLab MCP → tro-* 资产转换层（Python） | **快照**（勿手改） |
| `tools/` | 引擎级无窗口测试 + `ipc_smoke.py` + `scene_gen` | 混合（见下） |
| `game/` | **你的游戏**（起步骨架，随意改写） | 项目自有 |
| `assets/` | 场景/图集/贴图（含引擎测试 fixture） | 项目自有 |

## 快照（vendored）与同步

`engine/`、`pixellab/`、`editor/`、`tools/` 的引擎级测试与 fixture 是**从 trogue
仓库复制来的快照**。权威源是 trogue 仓库本身——**不要在本项目里手改这些文件**；
上游更新后，在 trogue 仓库内重跑 `template/scripts/sync_from_source.sh` 刷新。

模板自有（可自由修改）：`CMakeLists.txt`、`.gitignore`、`tools/CMakeLists.txt`、
`tools/ipc_smoke.py`、`game/**`、`assets/**`。

## 起步内容

- `game/src/main.cpp`：窗口 + 场景渲染 + WASD 单格移动（引擎 TweenManager 驱动）
  + 热重载 + IPC（`status`/`list_entities`/`get_entity`/`move`/`screenshot`/`quit`）。
- `assets/scenes/starter.json`：20×15 palette 场景（四面墙 + 玩家 + 木箱）。
- `assets/scenes/{demo,test,soldier_animated_sprite_2d}.json`、`tilesets/*`、
  `textures/*`：**引擎测试 fixture**（`ctest` 需要，勿随意删除；删除会让引擎
  单测变红）。你的游戏资产请另建文件。

## 开发命令

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/bin/trogue [--scene assets/scenes/starter.json] [--port 48764]
ctest --test-dir build --output-on-failure      # 引擎级测试
python3 tools/ipc_smoke.py                      # 游戏运行中时（DEBUG 构建）
```

依赖（系统包管理器或 CMake FetchContent）：raylib 6.0、nlohmann/json 3.11+、
tl::expected 1.x、支持 C++20 协程的编译器。

## 更多

面向 Agent 的开发指南见 `AGENTS.md`（引擎公共 API 边界、tro-* 资产 schema、
IPC 协议、调试工作流、PixelLab 管线）。
