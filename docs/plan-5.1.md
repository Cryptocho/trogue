# 里程碑 5 分卷 1：C++ API 形态与工程基线

> 前置阅读：`docs/plan-5.md`（综述）。本卷定案语言/API/依赖/目录/构建基线，后续分卷以此为骨架展开各模块。凡与本卷冲突处以本卷为准并回改综述。

## 1. 目标

1. engine 从 C11（`tg_*`/`Tg*`、纯 struct + 自由函数）整体迁移为 **C++20**，公共 API 为**纯 C++**：`namespace tg`、值类型快照 + RAII 资源类。
2. JSON 解析统一 nlohmann/json；协程演出为**自研最小原语**（`tg::task`/`tg::generator`/事件 awaiter，决策记录见 §5.2）。
3. 建立目录/公共头/CMake/测试基线，供 5.2~5.6 直接落文件。

## 2. 语言与标准

- C++20（`-std=c++20`）。需要 GCC 12+ / Clang 15+ 等价协程支持（AGENTS 依赖表已列）。
- 编译器告警：`-Wall -Wextra -Wpedantic`（或 MSVC 等价）；库代码零告警。
- 不使用 RTTI（无需 dynamic_cast/typeid）；异常策略见 §6；`noexcept` 用在移动/交换/析构等不抛路径。

## 3. 命名空间与类型策略

### 3.1 命名空间

- 公共：全部在 `namespace tg { ... }`。
- 私有：`namespace tg::detail`（跨翻译单元的内部共享）与匿名命名空间（文件内 static）。
- 禁止在公共头 `using namespace std;` 或引入全局污染。

### 3.2 类型三分类（公共 API 只用前两类）

| 分类 | 说明 | 例（符号名以分卷 5.2~5.5 定稿为准） |
|---|---|---|
| 值类型（快照/数据） | 可复制/移动的普通 struct，自带默认/拷贝/移动；不含资源句柄 | `SceneEntity`、`LayerInfo`、`SpriteDesc`、`Color`、`Vec2`、`Rect`、`TileQueryResult` |
| RAII 资源类 | 不可拷贝（可移动），构造取得资源、析构释放；内部 `std::unique_ptr`/`shared_ptr` 或自有句柄 | `SceneAsset`、`Texture`、`Ipc`、`Watcher`、`Animation`(播放器句柄)、`TweenManager` |
| 私有实现类 | 仅 `detail` 可见 | 资产解析器、连接槽等 |

- 资源类统一：禁拷贝构造/赋值；提供移动；`operator bool`/`valid()` 表示是否持有资源（可选，按模块定）。
- 不把资源类当作「实体」：engine 无 `Entity`/`World` 运行时类型（历史迁移见分卷 5.6）。

### 3.3 头文件与伞头

- 公共头扩展名 `.hpp`，位于 `engine/include/trogue/`。
- 伞头 `trogue/trogue.hpp`：只 include 各公共头；**不 include** 任何第三方头到伞接口外（nlohmann 仅在需要其类型的接口出现——默认避免：IPC callback 用 `tg::Json` 别名？定案见 §5）。

## 4. 目录与文件布局（目标）

```
engine/
├── CMakeLists.txt
├── include/trogue/
│   ├── trogue.hpp          # 伞
│   ├── config.hpp          # 版本/限额/常量
│   ├── types.hpp           # 基础值类型 Color/Vec2/Rect/Result 枚举
│   ├── scene.hpp           # SceneAsset/SceneEntity/LayerInfo/查询 API
│   ├── render.hpp          # 渲染原语/Texture
│   ├── animation.hpp       # Animation 帧播放器
│   ├── tween.hpp           # Tween/TweenManager
│   ├── hotreload.hpp       # Watcher
│   ├── ipc.hpp             # Ipc + handler 契约
│   └── coro.hpp            # 自研最小协程原语 tg::task/tg::generator/事件 awaiter
└── src/
    ├── scene_asset.cpp/.hpp(私有) ...
    ├── render.cpp/.hpp ...
    ├── animation.cpp ...
    ├── tween.cpp ...
    ├── hotreload.cpp ...
    ├── ipc.cpp ...
    └── util/...            # json 校验、路径、颜色解析等 detail 工具
```

- 历史 C11 文件（`world.c`、`scene.c`、`tileset.c`、`tileset.h`、`world.h` 等）在本里程碑删除/替换（清单见分卷 5.6 迁移矩阵）。
- 第三方：nlohmann/json（`#include <nlohmann/json.hpp>`）。**协程为自研、无第三方依赖**（决策记录见 §5.2）。依赖引入方式见 §7。

## 5. JSON 与协程依赖封装

### 5.1 nlohmann/json

- 唯一 JSON 类型 = `nlohmann::json`（可 `using tg::Json = nlohmann::json` 仅限公共头需要处；**默认避免**在公共 API 泄漏第三方类型，若 IPC handler 需要则在 `ipc.hpp` 内定义别名并注释「类型别名、非继承」）。
- 解析与校验实现全部在 `detail`；schema 校验**语义**保持既有规则（分卷 5.2 全量列出），**禁止**因换库放宽（如未知键忽略、数值非有限、深度/大小无上限）。
- nlohmann 的异常：解析用 `json::parse(str, nullptr, false)`（不抛）或 `parse(..., /*allow_exceptions*/false)` 捕获错误；API 边界不抛 json 异常。

### 5.2 协程原语（自研最小集）与 `tg::task` 封装

> **决策记录（2026-09-07，实施前置验证触发）**：原计划 vendor **cppcoro**（submodule）。实施第 1 步前做编译验证，发现 cppcoro 上游 master 停留在 **C++17 TS 时代**（`#include <experimental/coroutine>`、`std::experimental` 命名空间），该头自 **GCC 12 起被移除**，在 GCC 15 下直接编译失败。经用户拍板：**自研最小协程原语，不引入第三方协程库**。本小节替代原「cppcoro 薄封装」小节，作为权威协程约定。

- `trogue/coro.hpp`（header-only，约 300–400 行，零第三方依赖）提供：
  - `tg::task<T>`（协程载体，`co_return T`）与 `tg::task<>`；`co_await` 返回 T（若需要）。
  - `tg::generator<T>`：惰性值产出（若 5.4 需要时实现）。
  - 事件/完成信号 awaiter：单消费者语义（供 `co_await anim.done()` / `tween.wait(id)`），**恰为 cppcoro `single_consumer_event` 的等价物**。
- 单线程推进：演出协程在 game 主循环**显式推进**（不另起线程、无调度器依赖）；具体推进 API 由分卷 5.4 的 awaiter 契约给出（如 `co_await anim.done()`/`tween.wait()` 返回 `tg::task<void>`，game 每帧 pump）。
- 约束：协程仅用于**演出脚本/顺序编排**；数据驱动播放器（Animation/Tween 核心）不依赖协程；协程代码不得持有悬垂引用（game 负责在销毁 asset/manager/播放器前取消/完成）。
- 生命周期安全：等待者持有对 player/manager 的非拥有引用；契约 = 宿主（asset/player/manager）先于协程销毁，或先取消使等待即时完成；宿主析构时未完成等待者不得悬垂（实现以「宿主存活期标志」使已析构后 resume 安全完成）。文档+评审重点。
- **等待者纪律**：同一完成事件（`anim.done()`/`tween.wait(id)`）**原则上至多一个等待协程**（single_consumer 语义）；若实现支持多等待者，行为须在头文件明示（「全部已注册等待者均 resume」或「只唤醒第一个」二选一），禁止未写明即放任。
- **`spawn_task` 归属**：属于 **game 侧辅助**（把 `tg::task<>` 收进持有列表、主循环每帧 pump），不是 engine 公共 API；engine 只提供 `tg::task`/awaitable 原语。捕获生命周期由 game 自己保证（捕获的 `this`/对象引用在演出任务期间存活）。
- 若实施发现自研原语某处语义缺陷、需引入第三方替代：**与 5.4 同口径**——先记录到本卷（写明替代方案与理由）并纳入代码评审，不静默改依赖。

## 6. 错误处理与日志（公共 API 约定）

- 可预期失败：统一用 **`tl::expected<T, Error>`**（第三方库，vendored，见 §7），在 `types.hpp` 提供公共别名 `template<class T, class E> using expected = tl::expected<T,E>;` 与 `template<class T> using ErrorOr = tl::expected<T, tg::Error>;`；**不使用** `std::expected`（C++23 才有，本工程定 C++20）。`tg::Error`（基础错误类型，定义于 `types.hpp`）形态：`enum class ErrorCode { ... } code; std::string message;` 提供构造/诊断文本；各模块用 `using AssetError = Error;` 等具名别名或子类型。解析/加载类一律返回 `ErrorOr<T>`；查询类返回带区分枚举或 optional（按模块定并写在该模块头）。任何操作失败均不产生半成品副作用（沿用「失败不修改旧对象/无半成品输出」契约）。
- 异常：公共 API 默认不抛（除分配失败等极端）；`detail` 内部可用 `try/catch` 兜底转错误码/日志；禁止以异常作主控制流。
- 日志：`tg::log::info/warn/error` 或对接 raylib TraceLog（分卷定稿）；格式 `[模块] 消息`，用户可见中文。
- 所有返回失败的操作：不产生半成品副作用（沿用既有「失败不修改旧对象/无半成品输出」契约）。

## 7. CMake 与依赖

### 7.1 顶层 CMakeLists（顺序固定，沿用 C 版审查定案）

```
cmake_minimum_required(...)
project(trogue CXX)
set(CMAKE_CXX_STANDARD 20 REQUIRED)
include(CTest)                                   # 先于一切（BUILD_TESTING）
option(TROGUE_BUILD_CONSUMER_SMOKES "..." ON)    # 与 BUILD_TESTING 共同门控
option(TROGUE_DEBUG "debug tooling (ipc/watcher 桩化；Animation/Tween 为通用功能不桩化)" ON)
add_subdirectory(engine)
if(TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING)
  add_subdirectory(tools)
endif()
add_subdirectory(game)
```

- raylib：`find_package(raylib ...)`（沿用现有）或 FetchContent 后备，按当前构建方式保留。
- nlohmann_json：`find_package(nlohmann_json ...)` / 系统包。
- **协程：自研**（`trogue/coro.hpp` header-only，零第三方依赖；决策记录见 §5.2——cppcoro 前置验证失败，GCC 15 不可编译）。
- **tl::expected（错误载体）**：vendor（`third_party/tl-expected`，header-only）或包管理；默认 vendor via submodule。`types.hpp` 提供 `tg::expected`/`tg::ErrorOr`/`tg::Error`（见 §6）。
- 库 `trogue_engine`：`add_library(trogue_engine STATIC ...)`，`target_include_directories(... PUBLIC engine/include PRIVATE third_party/include)`（`third_party` 供 engine 内部与测试使用，不向 consumer 泄漏），`target_link_libraries(... PUBLIC raylib nlohmann_json::nlohmann_json)`（PUBLIC/PRIVATE 以实际需要为准，供外部 consumer 继承）。
- `TROGUE_DEBUG=OFF`：**仅 ipc 与 watcher** 编译为桩（沿用「API 形状不变、release 零开销」），**Tween/Animation 是通用功能，无 Release 桩**；具体宏与文件见分卷 5.5。

### 7.2 tools（consumer smoke / 测试）与测试 seam 契约

- `tools/` 仅当 `TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING` 加入（沿用定案）。
- 测试体系骨架（各分卷补断言）：
  - 无窗口单测/路径 smoke（链接 `trogue_engine`，纯逻辑）：资产解析、查询、限额、路径、协程/Tween 推进（虚拟时钟）。
  - consumer smoke：OOP 风格与 ECS 风格各一个最小程序，证明使用者可在不依赖 engine 对象模型前提下链接使用。
- working directory = `${CMAKE_SOURCE_DIR}`；`add_test` 注册。

**测试 seam 契约（阻塞项 2 定案；沿用 C11 形态的 C++ 对应）**：

1. **测试专用库**：当 `TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING` 时，engine 以同一源文件列表额外构建 **`trogue_engine_test`**（`add_library(trogue_engine_test STATIC ${ENGINE_SOURCES})` + `target_compile_definitions(... PRIVATE TROGUE_TEST_SEAMS=1)`）。生产 `trogue_engine` **不带** `TROGUE_TEST_SEAMS`、不含测试符号。只有 `trogue_engine_test` 供测试目标链接；consumer smoke 链接生产库（验证真实使用），含私有 detail 访问的单测链接测试库。
2. **私有 detail 可达性**：需测 engine 私有实现的单测（watcher 分类、render 三段计数等）通过链接 `trogue_engine_test` 并 **include 引擎私有目录**（engine 把 `src/` 加入测试目标的私有 include，仅测试用；生产 consumer 不可见）。`detail` 命名空间符号在同一测试库内可达。
3. **seam 最小清单**（各分卷引用）：
   - render 三段计数（5.3 §8 需要）：`TROGUE_TEST_SEAMS` 下 render 暴露 `struct RenderStats { int param_failures, window_checks, texture_attempts; }; RenderStats render_test_stats();`（进程内单调累计，不随调用清零）。
   - watcher 分类（5.5）：`detail::classify_event_name` 为私有但经测试库可达，**无需额外 seam 函数**（不设 `tg_watcher_test_classify`；删除残留 C 前缀假设名）。
   - asset_id 耗尽（5.2 §6 若需断言单调不回绕）：`TROGUE_TEST_SEAMS` 下提供 `asset_test_seed_id(std::uint64_t)`/`asset_test_reset_id()`（见 5.2 §6）。
4. **生产不导出**：`nm`/符号门禁——生产库符号仅公共 API；测试库相对生产库多出的符号 = **精确 seam 白名单**：`render_test_stats`、`asset_test_seed_id`、`asset_test_reset_id`（及 5.5 若新增并经 5.1 §7.2 追加者）。5.6 §7 引用此白名单。
5. **无 seam 的测试**：纯公共 API 逻辑（解析拒绝、查询、限额、Tween 推进）链接生产库即可，不需 seam。

### 7.3 构建命令（Debug/Release）

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DTROGUE_DEBUG=ON -DTROGUE_BUILD_CONSUMER_SMOKES=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
# Release 同参数 -DCMAKE_BUILD_TYPE=Release -DTROGUE_DEBUG=OFF（构建目录 build-release）
```

## 8. 编码规范落点（C++ 版，与 AGENTS「编码规范」一致）

### 8.1 公共 API 风格总纲（2026-09-07 拍板，代码评审硬依据）

引擎公共 API **不用 OOP 层级**，采用「值类型 + RAII 资源类 + 自由函数」：

- **无继承、无虚函数、无抽象接口类**：公共类型不构成类层级；RAII 类不做基类/多态（引擎内部 detail 亦不引入 OOP 层级，除非确有复用收益并先记录）。
- **自由函数优先**：查询/渲染/推进/采样等**操作**走 `tg::` 自由函数（如 `is_solid_at(asset, pos)`、`render_scene(...)`、`tween.tick(...)` 的等价自由函数或薄方法由各分卷定）；RAII 类**只持有资源与生命周期**，方法为资源操作的薄封装（load/查询入口/推进），**不承载玩法或业务逻辑**。
- **值类型 = 纯数据**：public 字段、无封装 getter/setter；可复制/移动（§3.2）。
- **引擎内部直接调 raylib C API**，不引入 raylib-cpp（决策见 AGENTS「引擎实现语言决策」）；公共 API 不暴露 raylib 类型。
- game 层**不受此约束**：使用者自选 OOP/ECS（模型无关）。

### 8.2 编码规范落点

- 纯值类型不封装无意义 getter（用 public 字段）；资源类用私有成员 + 方法。
- 命名：类型 `UpperCamel`，函数/变量 `lower_snake`，常量 `kXxx` 或全大写下划线（统一为 `kXxx`，公共宏除外）。
- 注释中文、解释"为什么"；`[[nodiscard]]` 用于易忽略错误的结果类型。
- 移动/拷贝语义遵循 §3.2；`= default` 优先，自定义析构时考虑 Rule of Five。
- 头文件最小化 include（前向声明优先），编译墙友好。

## 9. 文件/符号迁移与验证基线

- 旧 C 符号删除以分卷 5.6 迁移矩阵为准；本卷只定骨架：`.c/.h` → `.cpp/.hpp`，旧公共头最终不存在于 `include/trogue/`。
- 无窗口基线验证：一个最小程序 include `trogue/trogue.hpp` 并链接 `trogue_engine` 即编译通过（无隐藏依赖）；Debug/Release 均零告警。

## 10. 本卷决策清单（审查时核对）

| # | 决策 |
|---|---|
| 1 | C++20、`-Wall -Wextra -Wpedantic` 零告警 |
| 2 | 公共头仅 `.hpp`，伞 `trogue.hpp` |
| 3 | 值类型/RAII/私有三类策略，engine 无 Entity/World 运行时类型 |
| 4 | nlohmann/json 唯一 JSON 依赖，校验语义不放宽 |
| 5 | 协程：**自研最小原语**（`tg::task`/`tg::generator`/事件 awaiter，`trogue/coro.hpp`，单线程显式推进）；cppcoro 前置验证失败（C++17 TS 的 experimental/coroutine，GCC 12+ 不可编译）→ 记录替代方案与理由并重送审查 |
| 6 | 错误：**tl::expected**（vendored）→ `tg::expected`/`ErrorOr` + `tg::Error`，公共 API 不抛裸异常 |
| 7 | CMake 顺序/选项/测试门控固定；`trogue_engine_test` 测试库 + seam 契约（§7.2） |
| 8 | 旧 C11 文件与符号迁移由 5.6 矩阵执行 |
| 9 | **不引入 raylib-cpp**：引擎内部直接调 raylib C API，公共 API 不暴露 raylib 类型；RAII 由 tg 资源类自管（同步 AGENTS「引擎实现语言决策」「依赖与环境」） |
| 10 | **公共 API 风格总纲（§8.1）**：不用 OOP 层级（无继承/虚函数/抽象接口类）、自由函数优先、RAII 类只持资源+薄封装、值类型纯数据；game 层不受约束 |
