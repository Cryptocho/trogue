# trogue 游戏项目模板

用 [trogue](https://github.com/Cryptocho/trogue) 引擎从零开发一个游戏的**起点**。
本目录是一个自包含的项目骨架：检出这个 template branch、构建它、然后在 `game/` 里写你的游戏。模板中的 `AGENTS.md` 是体验优先的游戏开发指南，引擎 API 只在附录中作为参考。

## 开始开发

在项目根目录执行：

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/bin/trogue                         # 从项目根运行（资产按 CWD assets/ 约定读取）
```

模板 branch 本身就是完整起点，不需要额外安装器、同步脚本或上游仓库。

## 分支更新

引擎、工具和 PixelLab 转换层与该 template branch 一起维护。更新分支后重新配置并构建即可：

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

## 目录

| 目录 | 内容 | 归属 |
|------|------|------|
| `engine/` | trogue 引擎静态库（C++20，依赖 raylib + nlohmann/json + tl::expected） | 内置引擎代码 |
| `pixellab/` | PixelLab MCP → tro-* 资产转换层（Python） | 内置资产工具 |
| `tools/` | 普通 cell-terrain、PixelLab dual-grid 和占位资产工具 + `gen_font.py` / `ipc_smoke.py` | 内置工具 |
| `game/` | **你的游戏**（起步骨架，随意改写） | 项目自有 |
| `game/examples/` | 两个范式范例（类幸存者=ECS 风格、平台跳跃=OOP 风格；起手式参考，可整目录删除） | 项目自有（模板自带） |
| `assets/` | 你的资产（模板自带 `textures/pixellab/` 离线贴图与 `pixellab_manifest.json`，按需扩展） | 项目自有 |

## 目录归属

`engine/`、`pixellab/` 和 `tools/` 是 template branch 的内置通用能力。游戏项目可以直接使用，也可以在明确理解引擎契约后扩展；不要把游戏玩法反向移入 `engine/`。

模板自有（可自由修改）：`CMakeLists.txt`、`.gitignore`、`tools/CMakeLists.txt`、
`tools/gen_font.py`、`tools/ipc_smoke.py`、`game/**`、`assets/**`、本 README 和 `AGENTS.md`。

## 起步内容

- `game/src/main.cpp`：窗口 + 场景渲染 + WASD 单格移动（引擎 TweenManager 驱动、
  播完精确落格）+ 热重载 + IPC（`status`/`list_entities`/`get_entity`/`move`/
  `screenshot`/`log`/`quit`）。
- 内置起步场景：`game/src/main.cpp` 用 `tg::SceneAsset::load_json` **在内存里构造**一个
  20×15 palette 场景（四面墙 + 玩家 + 木箱），因此模板**零资产文件**即可运行；要换成
  磁盘场景，加 `assets/scenes/*.json` 并 `--scene` 指定（**Linux 上**热重载随之启用；
  无 inotify 的平台用 F5 或 IPC `reload` 手动重载）。
- **两个范式范例**（`game/examples/`，可选；不需要就整个删掉）：同一份引擎公共 API 的
  两种消费方式——`swarm/`（类幸存者，实体上百个、逐系统遍历 → ECS 风格）与
  `platformer/`（单角色平台跳跃，富状态机 + 多态敌人 → OOP 风格）。两者都使用
  `assets/textures/pixellab/` 下的离线 PixelLab 贴图（PNG + sha256 manifest），场景
  在内存构造，可直接跑：

  ```bash
  ./build/bin/swarm        # WASD/方向键移动，Shift 冲刺，R 重开
  ./build/bin/platformer   # A/D 移动，空格跳，R 重开
  ./build/bin/swarm --headless --seconds 20      # 固定步跑 20 秒并打印状态摘要
  ./build/bin/platformer --shot /tmp/frame.png   # 离屏整帧截图（Agent/无显示环境可用）
  ```

  `game/examples/common/harness.hpp` 是两者共用的最小骨架（参数解析、窗口、离屏整帧截图、
  内存场景构造）。范例是**起手式参考**，不是模板推荐的架构；它们的纯逻辑部分各有
  ctest 用例（`ctest` 一起跑）。

## 开发命令

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/bin/trogue [--scene assets/scenes/xxx.json] [--port 48764]
python3 tools/ipc_smoke.py                      # 游戏运行中时（DEBUG 构建）
```

依赖（系统包管理器或 CMake FetchContent）：raylib 6.0、nlohmann/json 3.11+、
tl::expected 1.x、支持 C++20 协程的编译器。

## 更多

游戏开发指南见 `AGENTS.md`（先定体验与深度，再实现、试玩和迭代；引擎边界、tro-* 资产 schema、IPC 与调试流程收在附录）。
