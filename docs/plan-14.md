# 里程碑 14：项目模板（template/）—— 从 trogue 源仓库派生新游戏项目

- 状态：**计划中（第二稿，已按首轮审查 FAIL 意见修订）**
- 日期：2026-09-11
- 前置：里程碑 5~13 已落地（C++ 引擎 / 回合制 / 事件通道 / AI+规则 / 帧动画 / 查看器 / autotile / PixelLab 管线）

## 1. 目的

让「用 trogue 引擎从零自主开发一个游戏」成为一条**可复制**的路径：仓库新增顶层 `template/`，包含 `engine` / `editor` / `pixellab` / `game` 起步代码 / `tools` / 顶层 `CMakeLists.txt`。使用者（或 Agent）复制它即得到一个**能构建、能运行、能被 Agent 迭代**的项目骨架。随后用一个全新项目验证「Agent 完整自主开发一个游戏」是否可行，并暴露改进点。

## 2. 现状与事实（已勘察，含首轮审查复核）

- **游戏无关、可复用**：
  - `engine/`（自包含静态库；AGENTS 明言「可整体取走独立复用或拆库」）——头 `engine/include/trogue/*.hpp`、实现 `engine/src/*.cpp`、`engine/CMakeLists.txt`。
  - `pixellab/`（PixelLab → tro-* 转换层；`*.py` + `tests/` + `fixtures/`，均已 tracked）。
  - `editor/`：`git ls-files editor/` 确认 tracked 仅 `project.godot`、`README.md`、`.editorconfig`、`.gitignore`、`addons/scene_exporter/*`（含 `.uid`）；`editor/assets/` 由根 `.gitignore` 忽略、`editor/.godot/` 由 `editor/.gitignore` 忽略。
  - `tools/`（无窗口引擎测试 `tools/tests/*`、`tools/ipc_smoke.py`、`tools/scene_gen.cpp`）。
- **游戏相关**：
  - `game/`（roguelike demo：`game_core/nav/rules/ai/main/anim_viewer/anim_util`）。
  - `assets/`（`scenes/*`、`tilesets/*`、`textures/*`）。
  - `tools/CMakeLists.txt:117-131` 的 `trogue_game_core_test` 目标**直接编译 `${CMAKE_SOURCE_DIR}/game/src/{game_core,nav,rules,ai}.cpp`**（这是 tools 与 game 的耦合点）。
- **引擎测试的 fixture 依赖（逐一核对）**：
  - `oop_client_smoke` / `ecs_client_smoke` 加载 `assets/scenes/demo.json`、`soldier_animated_sprite_2d.json`；
  - `scene_schema_test` 读 `demo.json`/`soldier_...json`/`test.json`，临时场景引用 `tilesets/tile_set.json`；
  - `terrain_test` 匿名引用 `tilesets/test_tileset_1.json`；
  - **`pixellab_mapping_test`（`tools/CMakeLists.txt:69-75`）设置 `TROGUE_SCENE_GEN`，故 `test_scene_gen.py` 不 skip；`pixellab/fixtures/wang_pattern.json:2` 引用 `tilesets/pixellab/wang_grass_dirt.json`，`tools/scene_gen.cpp:85` 读 `assets/<该路径>`** → 模板**必须**携带 `assets/tilesets/pixellab/wang_grass_dirt.json`（首轮审查阻断项 A；贴图 PNG 非必需，terrain 解析与 scene_gen 不读纹理文件）。
  - `render_test` / `anim_tween_test` / `watcher_ipc_test` 自造临时文件（无固定 fixture）。

## 3. 关键决策

**D1. 模板形态：字面自带快照 + 同步脚本**
- `template/` 字面包含 `engine`/`pixellab`/`editor`/`tools` 的**白名单副本**，另附 `scripts/sync_from_source.sh` 从 trogue 源目录刷新这些 vendored 目录。**权威源仍是 trogue 仓库，模板只是快照**；模板内 vendored 文件**不得手改**，只经 sync 更新（此为约束，非仅注释）。
- **合规论证**：AGENTS.md「依赖管理」禁止 vendored 的是**第三方**（raylib/nlohmann/tl-expected 一律走系统包 / `find_package`，模板沿用不引入任何 vendored 第三方）；`engine/` 是本项目**自有源码**，AGENTS 明确其「可整体取走独立复用或拆库」。故模板内 vendored `engine/` **不违反依赖策略**。
- 唯一规则张力：模板 `AGENTS.md` 复述 schema 会与根 `AGENTS.md` 漂移（见 §11）。

**D2. 起步游戏形态：最小可扩展骨架**（非 roguelike demo 全量）
- 最小骨架：窗口 + 场景渲染 + WASD 移动（引擎 `TweenManager` + 精确落格）+ IPC 基础命令 + watcher 热重载。roguelike demo 留在 trogue 仓库作参考实现，**不进模板**（避免新项目「开局即成品」的误导）。

**D3. 引擎测试保留、game 测试剥离**
- 模板 `tools/tests/` 保留**引擎级**测试（schema/query/render/anim/tween/terrain/watcher/ipc/consumer smoke），**删除** `game_core_test.cpp`（它编译 game/src 的 roguelike 模块）；game 自己的测试由 Agent 在新项目内新建。
- 模板 `tools/CMakeLists.txt` 是**模板自有**文件（不随 sync 覆盖），从 canonical 版裁剪去 `trogue_game_core_test` 目标与 game 源引用。

**D4. `tools/ipc_smoke.py` 归模板自有（首轮审查阻断项 B 的裁定）**
- canonical `ipc_smoke.py` 断言 roguelike demo 的命令/实体（`turn/move/wait`、6 事件、`demo.json` 的 `ground/walls` 层与 `goblin_1`、hp/ai 快照），与最小骨架**必然不匹配**（原样 vendored → §10 无法通过；改写 → sync 覆盖）。
- 故 `ipc_smoke.py` **移出 sync 白名单，列为模板自有文件**（与 `tools/CMakeLists.txt` 同处理）：模板提供一份与起步游戏命令集一致的**精简 smoke**（覆盖 `ping`/`status`/`list_entities`/`get_entity`/`move`/`screenshot`/`quit` 与基础观测），供 Agent 起步即用。

## 4. 目录设计

```
template/
├── README.md                       # 模板自有：创建新项目 / 同步引擎说明
├── AGENTS.md                       # 模板自有：新项目 Agent 指南（§7）
├── .gitignore                      # 模板自有
├── CMakeLists.txt                  # 模板自有：工程根聚合（engine + tools + game）
├── scripts/
│   ├── new_project.sh              # 模板自有：复制 → 新目录（剥离白名单见 §6）
│   └── sync_from_source.sh         # 模板自有：从 trogue 源刷新 vendored 目录（§5）
├── engine/                         # vendored 快照
├── pixellab/                       # vendored 快照（去 __pycache__）
├── editor/                          # vendored 快照（仅 tracked 清单）
├── tools/
│   ├── CMakeLists.txt              # 模板自有（D3 裁剪版）
│   ├── ipc_smoke.py                # 模板自有（D4 精简版，匹配起步游戏命令集）
│   ├── scene_gen.cpp               # vendored
│   └── tests/                      # vendored（去 game_core_test.cpp）
├── game/                           # 模板自有：起步游戏
│   ├── CMakeLists.txt
│   └── src/main.cpp
└── assets/
    ├── scenes/starter.json         # 起步场景（palette，零美术依赖）
    ├── scenes/demo.json            # 引擎测试 fixture（palette）
    ├── scenes/test.json            # 引擎测试 fixture（图集）
    ├── scenes/soldier_animated_sprite_2d.json  # 引擎测试 fixture（bare + 动画）
    ├── tilesets/{tile_set,test_tileset,test_tileset_1}.json
    ├── tilesets/pixellab/wang_grass_dirt.json  # pixellab_mapping_test fixture（阻断项 A）
    └── textures/                   # fixture 贴图（Decorations/Tile Set/Soldier*）
```

> fixture 场景/图集/贴图保留原名，使引擎测试**零改动**通过；模板 README/AGENTS 说明「哪些是引擎测试 fixture、哪些是本游戏资产」。

## 5. 同步机制（防漂移）

- `scripts/sync_from_source.sh`：**显式文件清单式复制**（非 `rsync --delete`），只覆盖 vendored 文件，**永不触碰**模板自有文件。
- **vendored 白名单**：`engine/**`；`pixellab/**.py` + `pixellab/tests/**` + `pixellab/fixtures/**`；`editor/{project.godot,README.md,.editorconfig,.gitignore,addons/**}`；`tools/{scene_gen.cpp,tests/*}`（**排除 `game_core_test.cpp`**）；fixture 资产（§4 的 demo/test/soldier 场景、tilesets、textures、`tilesets/pixellab/wang_grass_dirt.json`）。
- **模板自有（sync 不覆盖）**：`README.md`、`AGENTS.md`、`.gitignore`、`CMakeLists.txt`、`scripts/**`、`tools/CMakeLists.txt`、`tools/ipc_smoke.py`、`game/**`、`assets/scenes/starter.json`。
- 脚本顶部集中维护两个清单，便于审计。

## 6. 起步游戏设计 与 `new_project.sh` 剥离白名单

**起步游戏**
- `game/src/main.cpp`：`InitWindow` → 载入 `assets/scenes/starter.json` → 每帧 `render_scene` + 显式绘制实体（sprite/色块）→ WASD 移动（引擎 `TweenManager` + 精确落格，符合 AGENTS「数值精度纪律」）→ `tg::Watcher` 热重载 → IPC handler（`status`/`list_entities`/`get_entity`/`move`/`screenshot`/`quit`；`hello` 由 engine 传输层负责）。
- `game/CMakeLists.txt`：`trogue` 可执行（输出 `build/bin/`）。
- `assets/scenes/starter.json`：palette 20×15，四面墙 + 单个 `player` 实体，零美术依赖。

**`new_project.sh <目标目录>`（剥离白名单，明确）**
- **剥离**：`scripts/sync_from_source.sh`（在独立新项目中会指向不存在的 trogue 源，误导/误伤）、`scripts/new_project.sh` 自身、`README.md`（模板使用说明）。
- **保留**：`AGENTS.md`（新项目核心交付物，内容已是与 trogue 仓库无关的自足版）、`CMakeLists.txt`、`engine/`、`pixellab/`、`editor/`、`tools/`、`game/`、`assets/`。

## 7. 模板 AGENTS.md（核心交付物）

新项目需要精简、准确、**自足**的 Agent 指南（源自根 AGENTS.md 的 schema 与工作流段落，剥离 trogue-orign / 历史里程碑 / Roadmap 内部细节）：
- tro-scene v2.1 / tro-tileset v2 / tro-animations v1 **权威字段表（精简版）**，**顶部声明「schema 权威源仍是 trogue 仓库，本文件为快照」**；
- 目录结构、开发命令、依赖（raylib 6.0 / nlohmann / tl::expected / 自研协程）；
- IPC 协议 + Agent 调试工作流（起服 / 冒烟 / 热重载 / 截图视觉+数值验收）；
- PixelLab 管线用法（MCP 纪律 + `pxlab.py` 子命令）；
- 引擎公共 API 边界（薄、机制进引擎、玩法归 game）；
- 模板同步说明 + vendored 文件禁令。

## 8. 非目标

- 不改 engine 公共 API / tro-* schema。
- 不把 roguelike 玩法概念带入模板。
- 不做多模板/参数化脚手架（语言/引擎变体、命名占位替换）。
- 不自动创建 git 仓库 / 远端 / 提交。

## 9. 步骤

1. 新建 `template/` 骨架与 vendored 复制（§5 白名单）。
2. 编写起步游戏 `template/game/**` + `template/assets/scenes/starter.json`。
3. 编写模板自有文件：`template/{CMakeLists.txt,.gitignore,README.md,AGENTS.md}`、`template/tools/{CMakeLists.txt,ipc_smoke.py}`。
4. 编写 `template/scripts/{new_project.sh,sync_from_source.sh}`（可执行位）。
5. **本地验证**（§10）。
6. **subagent 检查**未提交代码是否合理、优雅、风格统一、无逻辑问题（禁止自检）。
7. 检查之后更新 `CHANGELOG.md`（Unreleased，按模块）。
8. 更新根 `AGENTS.md`（目录结构 / 工作流 / Roadmap 增补模板条目）。
9. 询问用户是否写 commit message；给出**英文**预览待确认后提交 + push。

## 10. 验证

- **独立构建**：`scripts/new_project.sh /tmp/newgame` → `cd /tmp/newgame && cmake -B build -S . && cmake --build build && ctest --test-dir build --output-on-failure` 全绿（**覆盖 `new_project.sh` 自身的剥离逻辑**，非仅 `cp -r template`）。
- **无显示环境判据**：构建与 `ctest` 无窗口可行（引擎测试全程 `WindowUnavailable` 安全 no-op）。若环境无 X11/Wayland/GL，则**跳过**运行 + smoke + 截图步骤并明确记录（不得假装已跑）；有显示时按下条。
- **运行 + IPC**（有显示时）：`./build/bin/trogue` 起服 → `python3 tools/ipc_smoke.py`（**模板自有精简版**）通过；`screenshot` 拿图 → Agent 读图 + 数值自证。
- **同步脚本**：在 trogue 仓库内跑 `sync_from_source.sh`，确认仅覆盖 vendored 文件、模板自有文件不变；`git status` 复核。
- **根仓库无回归**：根 `CMakeLists.txt` 不 `add_subdirectory(template)`；根构建与既有 ctest 不受影响。

## 11. 遗留 / 风险

- 模板 `AGENTS.md` 的 schema 段落与根 `AGENTS.md` **双份维护**，可能漂移——靠 sync 的文档复核或后续自动化收敛（本期接受人工跟进，已在模板 AGENTS 顶部声明权威源）。
- `template/tools/CMakeLists.txt` 与 `template/tools/ipc_smoke.py` 需随 canonical 变化**手工跟进**（模板自有文件，sync 不覆盖）。
- fixture 场景/贴图使新项目携带若干与自身无关的示例资产（引擎测试需要）；文档已声明，删除会使引擎测试红。
- `scene_schema_test` / `terrain_test` 会写 `assets/` 根临时文件（既有行为），模板将该目录视为「可写工作目录 + 只读 fixture 混合」，非纯只读。
