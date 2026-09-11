# 里程碑 15：引擎缺口修复与 Agent-first 原语补全（实战反馈驱动）

- 计划日期：2026-09-11
- 状态：计划中（第三轮审查通过）
- 前置：里程碑 5（C++ 引擎）、12（autotile/内存加载）、14（模板）已完成；一份**基于本模板从零开发的真实下游游戏**（俯视角实时动作冒险，本仓库外、非交付物）完成了对引擎公共 API 的首次全流程实战压测，产出一份缺陷与改进清单（本文档 §1 已把清单要点内联，读者无需访问外部文件）
- 参考：`AGENTS.md`「项目目标/交付物与验证台」「架构分层·功能准入判据」「引擎公共 API 边界」「编码规范·注释自足纪律」；`engine/include/trogue/{render,config,scene,tween,animation,ipc,coro}.hpp`、`engine/src/{render,animation,ipc}.cpp` 现状

## 1. 目的与问题

**本里程碑的起因**：一个**基于本模板从零开发的真实下游游戏**（俯视角实时动作冒险，本仓库外、非交付物）是「**game/ 作为引擎能力验证台**」这一架构的**首个外部消费方实证**：它只用引擎公共 API 从零做完一款游戏，挖出了引擎单测与 demo 覆盖不到的缺口。本里程碑把这些缺口**按归属三类**收口——引擎代码缺陷、引擎能力缺失、文档/一致性——并**显式排除**属于 game 层或模板层的内容。

**问题清单（本计划已逐条核对源码确认；编号为清单原文标签，非本仓库文档）**：

| 编号 | 内容 | 定性 | 本里程碑处置 |
|------|------|------|--------------|
| B1 | `AnimationPlayer::play()` 不重置 loop 覆盖 → 跨 clip 粘连（非循环动画 `done()` 永久挂起） | 🔴 引擎正确性缺陷 | **修**（§3.1） |
| B2 | `kMaxSpriteTextures` 是死常量（声明上限，实现无界缓存） | 🟡 文档/实现不一致 | **修**（§3.2，删常量 + 文档如实声明；`engine/` 范围验收） |
| B3 | IPC 版本号三处不一致（标题 v1.2 vs 线上 `version:1`） | 🟡 文档表述 | **修文档**（§3.6；线上值**不动**） |
| B4 | 独立贴图 `region` 缺省时锚点语义（`pos+offset` = 纹理左上角）未写明 | 🔵 文档 | **修文档**（§3.6） |
| D2 | `screenshot` 异步 + 无头不可用 | 🔵/能力缺口 | **修**（§3.3：同步落盘 + 离屏截图原语） |
| D3 | `RenderStats` 锁在 `TROGUE_TEST_SEAMS` 后，公共 API 不可见 | 🔵 能力缺口 | **修**（§3.4：提升为公共只读快照） |
| D4 | tile 查询只有单点，无批量 | 🔵 能力缺口 | **修**（§3.5：批量查询原语） |
| D5 | 缺 `TaskRunner`（引擎提供 `task` 却不提供推进容器） | 🔵 能力缺口 | **修**（§3.6：`tg::TaskRunner` + 修正 AGENTS.md 措辞） |
| D8 | autotile 与 `solid` 判据同源提醒缺失 | 🔵 文档防坑 | **修文档**（§3.6） |
| D1/D9 | 模板无 `--headless`/确定性步进、空转主循环兜底 | 🔵 模板层 | **本里程碑不做**（§6，属 `template/`，另起计划） |
| D6 | 标准化测试用 IPC 命令 schema | 🔵 文档/约定 | **本里程碑不做**（§6，需先有多个消费方样本；`game/` 已可作参考） |
| D7 | y-sort / 锚点惯用法辅助 API | 🔵 取舍 | **不做辅助 API**（违反「绘制显式」边界）；只做 B4 文档（§6） |

**范围（本里程碑实现）**：§3.1–§3.6 的引擎修复、能力原语与文档修订，含单测与回归。

**明确不在范围**：模板层 headless（D1/D9）、IPC 命令 schema 约定（D6）、`render_sprites_sorted` 类半隐式绘制辅助（D7）、动态实体碰撞规则/刚体物理（玩法决策，见 §6）。

## 2. 判据自检（本里程碑补齐/验证了引擎哪项通用能力）

每项都能回答准入判据（机制性、确定性、可无头测试）且**与美学/玩法决策无关**：

- **B1/B2**：修复既有原语的正确性/一致性缺陷——`AnimationPlayer` 与 `config.hpp` 常量都是引擎交付物本体。
- **D2（离屏）**：把「渲染到屏幕」的一般化为「渲染到可指定目标并取像」，服务 headless 视觉验收（Agent-first 的命门）；这是**渲染原语**，不是玩法。
- **D3**：渲染可观测性原语（「上帧画了几张、几张贴图缺失」），与「IPC 是 Agent 的眼睛」一致。
- **D4**：tile 查询原语的批量形态（把每游戏重写的 O(w·h) 采样循环收敛为一次调用）。
- **D5**：补全**既有**协程原语的推进容器——引擎已返回 `tg::task` 却不提供 pump，属能力半途；`TaskRunner` 是纯机械容器。

> 反向自检：本计划**没有**任何一项只能回答「让某款游戏更好玩」。碰撞**规则**、物理手感、y-sort、headless 主循环均被显式排除。

## 3. 方案

### 3.1 B1：`play()` 重置 loop 覆盖（正确性修复）

- **现状**（`engine/src/animation.cpp:62-81`）：`play()` 重置 `clip_index_/time_/playing_/paused_/finish_called_/last_frame_`，**不清** `has_loop_override_/loop_override_`；`advance()`（:165）与 `frame_index()`（:124）优先消费覆盖 → 上一 clip 的显式 `looping()` 粘到下一 clip。
- **修法**（取「新 clip 用自身 loop」方案，最符合直觉）：`play()` 成功切换 clip 的分支内加
  ```cpp
  has_loop_override_ = false;   // 新 clip 用自身 loop；显式覆盖不跨 play 保留
  loop_override_ = false;
  ```
  放在重置 `time_ = 0.0` 同一处（仅**实际切换**时清；`restart_if_same=false` 且同名在播的提前返回分支**不清**，语义正确——同一 clip 继续用原覆盖）。
- **文档**：`animation.hpp` 的 `looping()` 注释补一句契约——「覆盖仅作用于当前 clip；`play()` 换 clip 即复位为 clip 自身 loop」。
- **测试**（`tools/tests/anim_tween_test.cpp` 追加，纯公共 API）：
  1. loop clip 播过数周期后 `play(non-loop clip)` → `advance` 越过时长 → `playing()==false` 且 `on_finish` 触发一次；
  2. 在该 non-loop 播完后 `play(loop clip)`（**不**显式 `looping()`）→ 越过数周期仍 `playing()==true`（回归 B1：修复前此处为 false）；
  3. `restart_if_same=false` 路径：同名在播时再次 `play` 不清覆盖（语义锁）。
- **回归**：`anim_tween_test` 既有用例全绿（含 `looping()` 显式覆盖路径）。

### 3.2 B2：删除死常量 `kMaxSpriteTextures`

- **现状**：`config.hpp:30` 声明「缓存上限 16」，全引擎无引用；`render.cpp:62` 的 `g_texture_cache` 是无界 `unordered_map`，仅 `shutdown_render()` 全清。
- **决策**：**删常量 + 文档如实声明**（不实现容量上限）。理由：当前消费方规模（≤数张独立贴图）用不上 LRU/容量拒绝；为「可能有用」预造容量管理违反「薄 API」与「需求驱动」。
- **改动**：
  - `config.hpp`：删 `kMaxSpriteTextures` 定义与其注释；
  - `render.hpp`：在 `render_sprite`/`shutdown_render` 附近补一句生命周期契约——「独立贴图缓存**无上限**，路径去重、只增不减，生命周期 = 进程（`shutdown_render()` 释放）；图集贴图随 asset RAII。」
- **测试**：无新增（删常量）；确认删除后 **`engine/` 范围内** `grep -rn kMaxSpriteTextures` 零命中。

> **vendored 快照**：`template/engine/` 是上游快照（plan-14：模板内 vendored 文件禁止手改），它仍持有该常量。本里程碑**不改**模板（§4 步骤 10），故「全仓零命中」需等下游重跑 `template/scripts/sync_from_source.sh` 后才成立——验收项据此收窄为 engine/ 范围，不与之冲突。

### 3.3 D2：同步截图 + 无头离屏截图原语

分两半：**引擎原语**（离屏）与**game 接线**（同步 + headless 语义）。

#### 3.3.1 引擎新增离屏截图原语（`render.hpp` / `render.cpp`）

- **查证**（raylib 本地源码 `reference/raylib/`，遵守「行为查证约定」）：
  - `LoadImageFromScreen()` 读**默认帧缓冲**（`rlReadScreenPixels`），**依赖已创建 GL 上下文**；
  - `FLAG_WINDOW_HIDDEN (0x80)`（`raylib.h:558`）可隐藏窗口但**仍需 InitWindow 建上下文**；
  - `LoadRenderTexture(w,h)` 提供 FBO，可 `BeginTextureMode` 渲染到贴图后 `LoadImageFromTexture`，**同样需要 GL 上下文**。
  - **结论（预判，实施期以实机验证为准）**：纯 EGL/OSMesa 无窗口离屏 **不在本里程碑范围**（超工程量、跨平台脆弱）；本里程碑的「无头」= **隐藏窗口 / 虚拟显示（Xvfb/offscreen GLFW）下具备 GL 上下文**，此时离屏 API 可用。计划 §5 将该判定列为开工前置验证项。
- **新增公共 API**（自由函数，风格总纲：无 OOP 层级）：
  ```cpp
  // 把 asset 的全部 tile 层渲染到指定像素尺寸的离屏目标并导出为 PNG。
  // 语义：临时创建 RenderTexture(w,h) → 在其上 render_scene(asset)（自身
  // BeginTextureMode/EndTextureMode 区间内，调用方不需预置相机；相机为恒等）
  // → LoadImageFromTexture → ExportImage → 释放。不依赖屏幕/窗口是否可见，
  // 仅要求 GL 上下文已就绪（IsWindowReady）；否则 WindowUnavailable。
  // w/h 非法/非正 → Invalid。成功返回 Drawn，失败按 RenderResult 区分。
  RenderResult render_scene_to_png(const SceneAsset& asset, int w, int h,
                                   std::string_view path);
  ```
  - **不引入第二个「世界渲染」语义**：它内部复用 `render_scene`（同一绘制路径），只做「目标切换 + 取像 + 导出」。相机变换恒等（离屏默认无 game 相机）；带相机的离屏渲染**暂不提供**（game 可在自己的 `BeginTextureMode` 区间直接调 `render_scene`+`render_sprite`，本原语是「无相机场景快照」的便捷形式）。
  - 路径校验沿用 `util/path_check`（不安全路径 → Invalid）。
  - **明确边界**：本原语只画 tile 层（与 `render_scene` 一致，不含实体/sprite）——headless 验收「地形/资产是否正确」用它；要含实体的完整帧，game 仍需窗口或自建离屏区间（§6 记为后续按需）。
- **实现要点**：`RenderTexture` 必须成对 `UnloadRenderTexture`；异常路径（FBO 创建失败 = `texture.id==0`）→ `TextureMissing` 并记一次日志；导出前仍按既有纪律调用 `rlDrawRenderBatchActive`（`game/src/anim_util.cpp` 的 `export_screenshot` 已用此防「拍到残缺帧」）。
- **取像朝向（实测，非推导）**：离屏路径 `LoadImageFromTexture` → `glGetTexImage` **不翻转**，而屏幕路径 `LoadImageFromScreen` → `rlReadScreenPixels` **强制翻转**——两者相差一次垂直翻转，离屏导出 PNG **必须显式 `ImageFlipVertical(img)`**（raylib 官方示例亦如此），否则上下颠倒。实施后须以「离屏与窗口截图的 tile 层像素一致」验证该修正到位（见 §5）。

#### 3.3.2 demo/anim_viewer 同步截图（`game/`）——**实施实现**

- **现状**（`game/src/main.cpp` handler，帧末落盘）：handler 置 `shot_requested` 标志即返回 `path`，实际落盘在**下一渲染帧**——调用方拿到响应时文件常不存在。
- **实施拍板（与初稿不同，基于前置查证实测）**：把绘制抽为 `draw_frame(d)`（相机 + tile + 实体 + HUD），**窗口循环与截图共用它**；`screenshot` handler **同步**走**离屏 FBO** 渲染 `draw_frame` 并导出（共享工具 `game::capture_offscreen_png`），回 `{path,ok,w,h,bytes}`。
  - **为何不用屏幕路径**（前置查证实测，见 §5）：窗口模式下**帧后** `LoadImageFromScreen` 得**黑帧**（缓冲已交换）；`ipc.poll()` 早于 `BeginDrawing` 正落在该时段。屏幕路径不可靠 → 改用离屏（不依赖屏幕缓冲，headless 亦可）。
  - **好处**：同步落盘（响应时文件已存在）；截图含实体/HUD（因为 `draw_frame` 是完整帧，非仅 tile）；窗口与无头**共用一条绘制路径**（避免两套渲染漂移）。
  - **离屏翻转**：离屏取像不翻转→`capture_offscreen_png` 内做 `ImageFlipVertical`（否则上下颠倒）。
- **headless 分支（game 侧，非模板）**：demo 增加 `--headless`：`SetConfigFlags(FLAG_WINDOW_HIDDEN)` 后 `InitWindow`（仍建 GL 上下文）——保证离屏截图可用；逻辑推进仍由现有循环驱动。**完整「确定性步进主循环」属模板层（D1），本里程碑不做**（§6）。

> 注：初稿设想「`screenshot` handler 直接读屏（上一帧缓冲）」已按前置查证推翻；§3.3.1 的 `render_scene_to_png`（引擎 tile-only 便捷原语）仍保留（对其消费者有用），但 demo/viewer 的 `screenshot` 走的是 game 层 `capture_offscreen_png`（可画完整帧）。
> `anim_viewer` 同样改同步（共用 `draw_viewer` + `capture_offscreen_png`）。

### 3.4 D3：`RenderStats` 提升为公共 API

- **现状**：`detail::RenderStats{param_failures,window_checks,texture_attempts}` **类型定义无条件编译进生产库**（`render.cpp:30-36`，`g_render_stats` 于 `:104` 需要它）；但**公共访问器符号**仅在 `#ifdef TROGUE_TEST_SEAMS`（`render.cpp:304-315`）下经 `detail::render_test_stats()` 暴露——生产库拿不到该访问器。
- **改动**：
  - 新增公共值快照（`types.hpp` 或 `render.hpp`，建议 `render.hpp` 与渲染同域）：
    ```cpp
    // 渲染可观测性快照（进程内单调累计，不随调用清零）。
    struct RenderStats {
        int param_failures = 0;    // 参数/归属校验失败次数
        int window_checks = 0;     // 窗口就绪检查次数
        int texture_attempts = 0;  // 独立贴图加载尝试次数
    };
    // 公共只读访问器（自由函数）；重置供测试/长跑 game 分段观测。
    RenderStats render_stats();
    void render_reset_stats();
    ```
  - **符号去重**：`detail::RenderStats` 与 `detail::render_test_stats()` 是 seam 私有符号，公共 `tg::RenderStats`/`tg::render_stats()` 提升后：
    - 将 `render.cpp` 内部计数结构改为**直接复用 `tg::RenderStats`**（删除 `detail::RenderStats`）；
    - **保留 seam 兼容**：`TROGUE_TEST_SEAMS` 下 `detail::render_test_stats()`/`render_test_reset_stats()` 改为**转发**到公共实现（既有 `render_test.cpp` 零改动），或同步更新测试改调公共 API（二选一，实施时取「转发」以最小化 diff，并在 `scene_test_seams.hpp` 注释说明该 seam 已公共化）。
    - **实现约束（转换点）**：`scene_test_seams.hpp:29-33` 另有一份独立的 `tg::detail::RenderStats` 结构定义。删除 `render.cpp` 内的同名定义后，转发函数需要么把 `tg::RenderStats` 逐字段转成 `tg::detail::RenderStats`，要么把 seam 头的那份改为 `using detail::RenderStats = ::tg::RenderStats;` 别名（推荐别名，单一事实源）。实施时二选一并保持 `render_test.cpp` 断言不变。
  - **是否纳入 IPC**：引擎传输层**不**新增业务命令（IPC 无 world/命令语义归属 game）。**本仓库 demo 当前并无 `render_stats` 命令**（`game/src/main.cpp` 命令表无此项）——下游游戏是在其自建 game 层私有实现了一条。是否在本仓库 demo 加一条 `render_stats` 示例命令转发公共 API，属 **game 层**，可选、非引擎交付。
- **测试**（取「转发」方案时 `tools/tests/render_test.cpp` 无需改动；仅当别名方案不成立时才动）：公共 `render_stats()` 与 seam 转发值一致；无窗口调用渲染原语后 `param_failures/window_checks` 按既有断言演进。

### 3.5 D4：批量 tile 查询原语

- **现状**：只有单点 `is_solid_at` / `tile_at`；game 初始化碰撞网格须逐格采样（实测一个 44×44 地图 = 1936 次调用）。
- **新增公共 API**（`scene.hpp`，与既有 tile 查询同域，自由函数 + 值类型）：
  ```cpp
  // 区域以 tile 坐标表达（与 LayerInfo.width/height 同源；非像素）：
  // (tx,ty) = 区域左上 tile 坐标（可负）；w/h = tile 数（各 ≥1）。
  // 输出行主序：out[j*w + i] 对应 tile (tx+i, ty+j)。
  TileLookupResult tile_grid(const SceneAsset& asset, int layer_index, int tx,
                             int ty, int w, int h, int* out_values);
  // 全部 solid 层可走性合成掩码：out_mask[...] != 0 ⇔ 该格被任一 solid 层占据。
  // 语义 = 逐格 is_solid_at 的批量形态（多 solid 层短路）。
  TileQueryResult solid_mask(const SceneAsset& asset, int tx, int ty, int w,
                             int h, std::uint8_t* out_mask);
  ```
  - **粒度（实施拍板：tile 坐标，非像素矩形）**：输出尺寸恒为 `w*h`（无需 ceil 换算）、
    无浮点、与层网格直接对齐；「物化碰撞网格」的消费场景本就以 tile 为单位。
    层外/越界格 → 写 -1（与 `tile_at` 的 empty 语义一致）。
  - **数据来源**：直接读 `detail::SceneImpl` 的层 tiles 缓冲（已有 `friend` 机制，扩 friend 声明即可），**不**逐格调 `is_solid_at`（避免重复边界检查开销）。
  - **空指针/尺寸防御**：`w*h` 上限沿用层维度限额（`kLayerDimMax`），超限 → error（防整数溢出）。
- **测试**（`tools/tests/scene_query_test.cpp` 追加）：`tile_grid` 与逐格 `tile_at` 结果**逐位等价**（含负坐标/越界写 -1）；`solid_mask` 与逐格 `is_solid_at` 等价；多 solid 层短路、无 solid 层 clear、error 条件（空指针/非正/层越界/超层维度上限）。

### 3.6 D5：`tg::TaskRunner` + 文档一致性修订

#### 3.6.1 新增 `tg::TaskRunner`

- **动机**：`coro.hpp` 明说「演出协程由 game 主循环显式推进」；`AnimationPlayer::done()`/`TweenManager::wait()` 返回 `tg::task<>` 需被 pump。现状要求**每个** game 自建「task 容器 + 每帧 pump + 析构前 cancel」，引擎把 `task` 给了消费方却不给推进器，能力半途。（且与 B1 叠加时 `done()` 永久挂起，导致消费方**整个放弃协程**——引擎协程能力被闲置。）
- **归属裁定**：`TaskRunner` 是**纯机械容器**（无玩法决策、确定性、可无头测试），符合准入判据 → 进引擎。
- **新增公共 API**（新头 `engine/include/trogue/task_runner.hpp`，伞头 `trogue.hpp` 追加；实现可 header-only 或 `engine/src/task_runner.cpp`）：
  ```cpp
  // 单线程 task 容器：持有若干 tg::task<>，首次 pump_all 启动到首个挂起点，
  // 之后由等待的事件同步 resume（见 coro.hpp）；pump_all 每帧负责启动新
  // task 与回收已完成的。析构**不**隐式 cancel_all（协程体可能引用已失效
  // 宿主），调用方须在宿主销毁前显式 cancel_all()。
  class TaskRunner {
  public:
      TaskRunner() = default;
      ~TaskRunner() = default;  // 不隐式 cancel（见上）
      TaskRunner(const TaskRunner&) = delete;
      TaskRunner& operator=(const TaskRunner&) = delete;
      TaskRunner(TaskRunner&&) noexcept;  // 移动（可选，实施定）

      // 收编一个 task（其生命周期交给 runner）。
      void push(tg::task<> t);
      // 推进全部活动 task 一次（主循环每帧）；完成的即时回收。
      void pump_all();
      // 取消并销毁全部（须在宿主销毁前调用）。
      void cancel_all();
      int active_count() const;
  };
  ```
  - **析构语义**：`~TaskRunner()` **不**隐式 `cancel_all`（协程体可能引用已失效宿主，隐式销毁不安全）；文档明确「调用方负责在宿主销毁前 `cancel_all()`」。与既有 single_consumer_event 生命周期契约一致。
- **推进模型（实施拍板，重要）**：`pump_all` **不**每帧无条件 resume 全部 task——`tg::task` resume 一次即执行到下一挂起点，而挂起在 `single_consumer_event` 上的等待由事件 `set()` **同步** resume（coro.hpp）。若挂起期重复 pump 会把协程提前唤醒到事件未就绪的错误状态。故容器只「启动一次 + 回收」；每 task 记 `started` 位。
- **取消契约**：`cancel_all` 硬销毁协程帧 → 之后**不得**再由宿主 set() 其等待的事件（否则 resume 已销毁帧，UB）。安全顺序 = `cancel_all()` → 再销毁宿主；若需协程正常收尾，应改用宿主自身取消（`TweenManager::cancel`/`AnimationPlayer::stop` 先 set 事件），再 `pump_all` 回收。
- **与既有原语的关系**：不重复造轮子——启动调 `task::pump()`、完成判定调 `task::done()`；`cancel_all` 直接销毁 `task`（其析构 `coro_.destroy()`）。

> 归属说明：`coro.hpp:16-19` / `animation.hpp:13-15` 已声明的「宿主先于协程销毁，或先取消」由 game 保证；`TaskRunner` **不**替代该契约（它只能 cancel 它持有的 task + 硬销毁帧），仅提供启动/回收容器。
- **测试**（新增 `tools/tests/task_runner_test.cpp`，纯公共 API）：push → 首次 pump_all 启动 → 事件 set 同步完成 → pump_all 回收；多 task 并发；cancel_all 后容器空、pump 安全；`co_await player.done()`（非 loop 播完）与 `co_await tween.wait()` 经 runner 正确 resume（**与 B1 联合回归**：B1 未修时前者永不完成）。

#### 3.6.2 文档一致性修订（`AGENTS.md` + 头注释）

- **B3（IPC 版本）**：澄清命名**不改线上值**。`AGENTS.md`「IPC 协议」标题与 `config.hpp:57` 注释改为显式说明：协议版本号**恒为 1**（wire `hello`/`ping` 的 `version:1` 是能力探测值），标题「tro-ipc v1.2」中的 `.2` 是**文档修订号**（只增不改的记录），非 wire 版本。消除「标题版本 vs 线上版本」的误读。
- **B4（region 缺省锚点）**：`render.hpp` 的 `render_sprite` 注释补：「region 缺省（w/h==0）时按贴图尺寸补齐；**`pos` 始终为纹理左上角**（不因 region 缺省而改为居中）；需居中请由 game 设 `offset`。」
- **D8（autotile × solid 同源判据）**：`scene.hpp`（或 `terrain.hpp`）补一句防御提醒：「用 autotile 烘碰撞层时，请确保**布点判据与碰撞层判据同源**（同一可行走规则），否则会出现『视觉可行走、引擎判 solid』的错位。」
- **D5 措辞修正**：`coro.hpp`/`animation.hpp` 现有注释称「`spawn_task` 属 game 侧辅助，非 engine API」——新增 `TaskRunner` 后更新为「**引擎提供 `tg::TaskRunner` 作为推进容器**；宿主生命周期契约仍由 game 保证」。

### 3.7 引擎公共 API 边界（本里程碑后的增量）

`AGENTS.md`「引擎公共 API 边界」小节追加：
- **渲染可观测性**：`tg::render_stats()`（公共只读快照）。
- **离屏渲染**：`tg::render_scene_to_png()`（tile 层 → PNG，需 GL 上下文；无相机）。
- **批量 tile 查询**：`tg::tile_grid()` / `tg::solid_mask()`。
- **协程推进**：`tg::TaskRunner`（容器 + pump；不隐式 cancel）。
- 明确**不进引擎**：渲染排序/相机、动态实体碰撞规则、物理、模板 headless 主循环。

## 4. 步骤（含开发流程 3~7）

1. **§3.1 B1**：改 `animation.cpp` + `animation.hpp` 注释 → 加 `anim_tween_test` 用例 → 跑该测试确认修复前后行为差异（先复现挂起/停止，再修复）。
2. **§3.2 B2**：删 `config.hpp` 常量 + `render.hpp` 注释 → `grep` 零命中确认。
3. **§3.3 D2**：**先做前置查证**（§5 首项：隐藏窗口 + 离屏 API 在无显示器环境是否可用）→ 实机确认后再实现 `render_scene_to_png`（含 `ImageFlipVertical`）→ demo/anim_viewer `screenshot` 改同步 → 加 demo `--headless`（隐藏窗口）分支。
4. **§3.4 D3**：`render.hpp` 加公共 `RenderStats`/`render_stats()`/`render_reset_stats()` → `render.cpp` 去重 + seam 转发 → 调 `render_test.cpp`（能不改测试则不改）。
5. **§3.5 D4**：`scene.hpp` 加 `tile_grid`/`solid_mask` + friend → `scene_asset.cpp` 实现（直接读层缓冲）→ `scene_query_test` 加等价性用例。
6. **§3.6.1 D5**：新增 `task_runner.hpp`（+ 可选 cpp）→ 伞头追加 → `task_runner_test.cpp`（含与 B1 的联合回归）→ `tools/CMakeLists.txt` 注册。
7. **§3.6.2 文档**：`AGENTS.md`（IPC 版本表述、API 边界增量）、`render.hpp`/`scene.hpp`/`coro.hpp`/`animation.hpp` 注释。
8. **回归**：Debug + Release（`TROGUE_DEBUG=OFF`）构建**零告警**（`-Wall -Wextra -Wpedantic`）；`ctest` 全绿；`python3 tools/ipc_smoke.py` 61/61；无窗口单测不因新增公共 API 受影响。
9. **E2E 视觉验收**：起 demo（窗口模式）→ `screenshot` 同步命令 → 读图核对（Agent 自读，非推给用户）；`--headless`（隐藏窗口）下走 `render_scene_to_png` 出图并读图核对。
10. **模板同步**：本里程碑**不改** `template/`（D1/D9 不在范围）；但 §3.2 删常量、§3.4/3.5/3.6.1 新增头会影响 vendored 快照——**记录需在模板下游刷新时重跑 `template/scripts/sync_from_source.sh`**（本里程碑结束时留待办，或由用户决定是否顺带刷新）。
11. **subagent 审查**未提交代码（合理/优雅/风格统一/无逻辑问题；禁止自检）。
12. 更新 `CHANGELOG.md` 与 `AGENTS.md`（§3.7）。
13. 询问用户 commit message（英文预览，确认后提交**所有**变更，禁止直接提交）。

### 文件清单（新增 / 修改）

| 文件 | 动作 | 对应项 |
|------|------|--------|
| `engine/src/animation.cpp` | 改 | §3.1 B1 |
| `engine/include/trogue/animation.hpp` | 改（注释） | §3.1 B1、§3.6.2 D5 |
| `engine/include/trogue/config.hpp` | 改（删常量） | §3.2 B2 |
| `engine/include/trogue/render.hpp` | 改（新增 API + 注释） | §3.3 D2、§3.4 D3、§3.2 B2、§3.6.2 B4 |
| `engine/src/render.cpp` | 改（新增实现 + 去重） | §3.3 D2、§3.4 D3 |
| `engine/include/trogue/scene.hpp` | 改（新增 API + friend + 注释） | §3.5 D4、§3.6.2 D8 |
| `engine/src/scene_asset.cpp` | 改（批量查询实现） | §3.5 D4 |
| `engine/include/trogue/task_runner.hpp` | **新增** | §3.6.1 D5 |
| `engine/src/task_runner.cpp` | 新增（若 header-only 则省） | §3.6.1 D5 |
| `engine/include/trogue/trogue.hpp` | 改（伞头追加） | §3.6.1 D5 |
| `engine/src/scene_test_seams.hpp` | 改（seam 别名/注释） | §3.4 D3 |
| `tools/tests/anim_tween_test.cpp` | 改（追加用例） | §3.1 B1 |
| `tools/tests/render_test.cpp` | 不改（仅在别名方案不成立时才改） | §3.4 D3 |
| `tools/tests/scene_query_test.cpp` | 改（追加用例） | §3.5 D4 |
| `tools/tests/task_runner_test.cpp` | **新增** | §3.6.1 D5 |
| `tools/CMakeLists.txt` | 改（注册 task_runner_test） | §3.6.1 D5 |
| `game/src/main.cpp` | 改（`draw_frame` 抽取、screenshot 同步离屏、`--headless`） | §3.3.2 D2 |
| `game/src/anim_viewer.cpp` | 改（`draw_viewer` 抽取、screenshot 同步离屏） | §3.3.2 D2 |
| `game/src/anim_util.hpp` `game/src/anim_util.cpp` | 改（新增 `capture_offscreen_png`） | §3.3.2 D2 |
| `AGENTS.md` | 改（IPC 版本表述、API 边界增量） | §3.6.2、§3.7 |
| `CHANGELOG.md` | 改 | 步骤 12 |
| `docs/plan-15.md` | 新增（本文件） | — |

> `template/` 下 vendored 快照**本里程碑不手改**；受影响文件待下游 `sync_from_source.sh` 刷新（步骤 10）。

## 5. 验证清单

- [x] **前置查证（已完成）**：`FLAG_WINDOW_HIDDEN` + `InitWindow` 成功建 GL 上下文；`LoadRenderTexture`/`LoadImageFromTexture` 可用。关键发现：**窗口模式帧后读屏 = 黑帧**（缓冲已交换），只有帧内读屏有效；隐藏窗口下不交换故帧后亦可。→ 同步截图改走**离屏 FBO**（§3.3.2）。
- [ ] B1：新增 3 用例全过；修复前对应用例失败（证明抓到真 bug）；`anim_tween_test` 既有用例零回归。
- [ ] B2：`grep -rn kMaxSpriteTextures engine/`（engine 范围）零命中；`render.hpp` 生命周期注释就位。
      （template vendored 快照仍含该常量，属预期——待下游 sync；见 §3.2 注。）
- [ ] D2：`screenshot` 响应返回时文件**已存在**（`test -f`）、响应含 `{path,ok,w,h,bytes}`；窗口模式与 `--headless` 截图**逐像素一致**（实测 960×540 全 518400 像素零差异）。
- [ ] D3（非 B3）：公共 `render_stats()` 可链接、可调用（生产库）；seam 转发值与公共值一致；`render_test` 全绿。
- [ ] D4：`tile_grid`/`solid_mask` 与逐格单点查询**逐位等价**（含负坐标/层外/边界/多 solid 层）；error 条件全拒。
- [ ] D5：`task_runner_test` 全过（含 `co_await done()` / `wait()` 联合回归）；`active_count` 语义、启动/回收、取消契约均符合文档。
- [ ] 文档：B3 表述不再自相矛盾；B4/D8/D5 措辞就位；AGENTS.md API 边界增量与首尾术语一致。
- [ ] Debug/Release 零告警、`ctest` 全绿、`ipc_smoke` 61/61。
- [ ] E2E：demo 窗口 + headless 两路径截图验收通过（Agent 读图）。

## 6. 遗留与边界

- **D1/D9（模板 `--headless` 确定性步进、空转兜底）**：属 `template/` 层，另起模板计划（改 `template/game/src/main.cpp` 主循环语义：headless 不建窗口、逻辑仅经 IPC 显式推进、显式 sleep）。本里程碑仅在 demo 侧做「隐藏窗口」以保证离屏可用。
- **vendored 快照同步**：本里程碑新增/删除的头与实现（`task_runner.hpp`、`render.hpp`/`scene.hpp`/`config.hpp` 等）会与 `template/engine/` 快照漂移；**建议**在本里程碑收尾时执行一次 `template/scripts/sync_from_source.sh`（属模板维护动作，非本计划硬性交付；若不做，记入待办并在下次模板相关工作时一并刷新）。
- **D6（测试 IPC 命令 schema 约定）**：需先有≥2 个消费方样本（本仓库 demo + 下游游戏）再归纳；暂记文档待办。
- **D7（y-sort/锚点辅助 API）**：**不新增辅助 API**（违反「绘制显式、无隐式遍历」边界）；仅 B4 文档澄清。若未来多个消费方重复手写，再评估**文档惯用法片段**（非 API）。
- **离屏含实体渲染**：本里程碑 `render_scene_to_png` 只画 tile 层（与 `render_scene` 一致）。含实体/sprite 的完整无头帧需 game 自建离屏区间，或未来提供「离屏回调」形态（`render_to_png(w,h, fn)`）——按需再评估。
- **相机**：仍归 game。离屏原语用恒等变换；带相机的离屏不在范围。
- **碰撞几何原语（swept/AABB/线段）**：本里程碑**不含**。批量查询（D4）已覆盖「物化碰撞网格」的高频需求；`sweep_move`/`segment_hits_solid` 等几何检测原语因下游游戏未出现瓶颈、且需先定语义（轴分离 vs swept、法线约定），**留待下一里程碑按真实需求评估**。
- **动态实体碰撞规则、刚体物理、solver**：玩法决策，**明确不进引擎**（AGENTS.md 已固化）。
- **B2 的替代方案**：若未来独立贴图数量真实增长到需容量管理，再实现 LRU；当前选择「删常量 + 如实声明」。
