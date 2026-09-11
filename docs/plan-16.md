# 里程碑 16：碰撞几何原语（静态 tile 层的谓词、线段与滑移解算）

- 计划日期：2026-09-11
- 状态：计划中（待审查）
- 前置：里程碑 5（C++ 引擎）、12（autotile/内存加载）、15（引擎缺口修复与 Agent-first 原语）已完成。里程碑 15 §6 把「碰撞几何原语（swept/AABB/线段）」列为**按真实需求评估后再决定是否进引擎**的候选；本里程碑基于**两个独立消费方的实证需求**把它落地。
- 参考：`AGENTS.md`「项目目标/交付物与验证台」「架构分层·功能准入判据」「引擎公共 API 边界」「编码规范」；`engine/include/trogue/{scene,types,render}.hpp`、`engine/src/scene_asset.cpp`（tile 查询现状）、`reference/raylib/src/rshapes.c`（几何谓词上游实现，查证用）

## 1. 目的与问题

### 1.1 判据自检（本里程碑补齐/验证了引擎哪项通用能力）

本里程碑补齐的能力 = **「静态 tile 层的低层碰撞几何解算」**。它逐条满足功能准入判据：

| 判据 | 本里程碑 |
|------|----------|
| 机制性 | 是：纯几何谓词 + 对只读 solid tile 层的确定性解算，不含任何玩法决策 |
| 确定性 | 是：无随机、无浮点累加漂移；同输入逐位同输出 |
| 可无头测试 | 是：不触窗口/GL，纯公共 API 可单测（含暴力对照） |
| 与美学/玩法无关 | 是：不含动态实体碰撞规则、刚体物理、solver、单向平台、分层碰撞矩阵 |

> 反向自检：本计划没有任何一项只能回答「让某款游戏更好玩」。**「谁和谁碰、碰撞后发生什么」仍完全归 game**：本模块只回答「一个矩形/线段与静态地形几何的关系」。

### 1.2 需求实证（为什么是现在，而不是预造）

两个**互相独立**的消费方各自手写了等价实现——这正是「需求驱动（≥2 个消费方样本）」所要求的证据，也直接命中「不重复造轮子」纪律：

| 消费方 | 手写实现 | 位置 | 内容 |
|--------|----------|------|------|
| 下游实时俯视角游戏（本仓库外，基于模板从零开发，非交付物） | `Grid::rect_blocked` | 其 `game/src/game_core.hpp` | 逐格扫描矩形是否压到阻挡 tile（O(w·h)） |
| 同上 | `collide_move` | 其 `game/src/game_core.cpp` | 轴分离（先 X 后 Y）矩形移动 + 沿墙滑动 |
| 本仓库回合制探针 `game/` | `nav::has_line_of_sight` | `game/src/nav.cpp` | Bresenham 视线（逐格走线判 solid） |

同时，下游清单（里程碑 15 已归档要点）明确反馈：`is_solid_at`/`rect_hits_solid` 只有「点/矩形是否压 solid」，**缺**「线段穿不穿过 solid」与「矩形沿方向能走到哪」两种**几何解算**形态；下游不得不用单步试探 + 自建网格来绕开。里程碑 15 的批量查询（`tile_grid`/`solid_mask`）解决了「物化网格」的批量采样，但**没有**解决「连续位移解算」与「射线/视线」这两种几何形态。

**准入证据逐项对号（每个进引擎的符号都必须有实证消费方）**：

| 拟入符号 | 消费方实证 |
|----------|------------|
| `segment_hits_solid` | 探针 `nav::has_line_of_sight`（视线）+ 下游自建网格射线试探 |
| `sweep_move` | 下游 `collide_move`（轴分离滑移） |
| `aabb_overlap` | 下游 `overlap()`（矩形相交判定） |

> 三个符号各有真实调用点：`segment_hits_solid`/`sweep_move` 各有**两个**独立消费方，`aabb_overlap` 为**单消费方**（下游 `overlap()`），保留理由是「tg 类型原生 + 不暴露 raylib 类型」。**圆相关谓词（圆-圆/圆-矩形）零消费方**（下游的圆判定是裸 `sqrt` 距离，非谓词），依「能用 raylib 直达的不进引擎 / 需求驱动」纪律**不进本里程碑**（移入 §5 遗留，待出现 ≥2 消费方再评估）。

### 1.3 明确不在范围

- **动态实体之间**的碰撞规则（互斥、推挤、分层碰撞矩阵、触发器回调）：归 game。
- **刚体物理**（质量、速度、弹性、摩擦、约束、solver、连续碰撞检测的物理语义）：归 game。
- **单向平台、斜坡、可破坏地形、导航网格/寻路**：玩法决策，归 game（寻路属图搜索，engine 只提供 `is_solid_at` 这类谓词作输入）。
- **相机**、**y-sort**、**绘制排序**：与碰撞无关，仍归 game。
- **重构 game/ 已有实现**：本里程碑不改 `game/src/nav.cpp` 的视线语义（Bresenham 与网格 DDA 在精确对角线上访问的单元集不同，改写会改变探针行为），仅由**新单测 + consumer 侧**验证新原语。

## 2. 方案

新增引擎模块 `engine/include/trogue/collision.hpp` + `engine/src/collision.cpp`，三类原语。全部为 **自由函数 / 纯值类型**，与既有 `tg::` 风格一致（无 OOP 层级、tg 类型入参、不暴露 raylib 类型）。

### 2.0 既有语义基线（新原语必须一致）

- 坐标：像素、y 向下；`Rect` 的 x/y = 左上角，w/h 约定非负。
- tile 查询只读 `solid==true` 的层；`tiles[..] != -1` 即阻挡；**层矩形外 = 无数据 = 不阻挡**。
- 像素 → tile：`floor((world - layer_origin) / tile_size)`；层可有**独立 origin**（可负）。
- 矩形与 tile 的相交约定 = **半开** `[x,x+w)×[y,y+h)`（与 `rect_hits_solid` 一致）。

### 2.1 几何谓词（与 asset 无关的纯值判断）

```cpp
// 轴对齐矩形相交：半开 [x,x+w)×[y,y+h)；仅边界相接（如 a.x+a.w == b.x）→ false。
// 任一 w/h <= 0 → false。
bool aabb_overlap(Rect a, Rect b) noexcept;
```

- **实现**：薄转换为 raylib `Rectangle` 后委托 `CheckCollisionRecs`（`reference/raylib/src/rshapes.c:2313-2321`，已查证用严格 `<`/`>` → 边界相接 = false，与半开语义一致），引擎**另加** `w<=0 || h<=0 → false` 的防御（raylib 不校验退化尺寸）。符合「引擎内部直接调 raylib C API、不重复造轮子」。
- **存在理由**：`AGENTS.md` 规定公共 API 不暴露 raylib 类型；game 用 `tg::Rect` 时无法直接调 `CheckCollisionRecs`（需自行转换并声明 `Rectangle`）。本谓词让公共 API 自洽（下游 `overlap()` 可直接复用）。**仅此一个谓词**——圆相关谓词无消费方，见 §1.2 与 §5。

### 2.2 线段 vs solid tile 层（视线 / 射线）

```cpp
// 线段命中描述：result==solid 表命中；clear 表未命中；error 表参数非法。
struct TileHit {
    TileQueryResult result = TileQueryResult::clear;
    int layer = -1;          // 命中的 solid 层索引（hit 时有效）
    int tx = -1, ty = -1;    // 命中单元在该层内的 tile 坐标（hit 时有效）
    Vec2 point{0.0f, 0.0f};  // 命中点（世界像素）
    float t = 0.0f;          // 沿 a→b 的参数（0=a，1=b）
};

TileHit segment_hits_solid(const SceneAsset& asset, Vec2 a, Vec2 b);
```

**语义（钉死，消除歧义）**：

1. **几何真理语义（闭区间）**：判定「线段 `[a,b]` 与 solid 几何是否相交」。线段**接触到的每个单元都参与判定，含 a、b 各自所在单元**；`a==b` → 由该点所在单元判定。
2. **首个命中**：按沿 `a→b` 的进入参数 `t` 递增返回**首个**命中单元；同 `t` 时按**层索引升序**取，同层同 `t` 时按 `(ty, tx)` 升序（确定性写全）。
3. **保守遍历（不设漏判）**：用 **Amanatides–Woo 的 supercover 变体**遍历线段实际穿过的单元——在 `tMaxX == tMaxY` 的**精确对角线**上**双轴同时步进并访问角格**（标准 Amanatides–Woo 在 tie 时按单一 tie-break 只走一格，会漏掉对角相邻格；本实现显式采用 supercover 行为）。这保证「线段确实穿过的单元绝不漏判」，不产生 Bresenham 式漏判。
4. **逐层 DDA**：每个 solid 层有**独立 origin**（可有不同 lattice 偏移），因此**逐层**做 DDA，取各层首个命中的最小 `t`。
5. **先裁剪再遍历**：每层先做线段 vs 该层 AABB 的 slab 裁剪（`t_enter/t_exit`）；无交集 → 跳过该层。这既保证正确（层外无数据），又把逐层遍历步数**界定在 O(width+height)**（单层 ≤ 4096+4096 = 8192；solid 层 ≤4，总量 ≤ 4×8192），杜绝长线段导致的迭代爆炸。
6. **误差条件**：`a`/`b` 任一坐标非有限 → `result=error`，其余字段无意义。
7. **「视线」惯用法（由调用方构造，不写进 API）**：需要「终点格不判定」的 LOS 语义时，把 `b` 沿 `a→b` 回缩一个极小量（或先自行判定端点单元）——几何原语不内置端点豁免策略。

> **为什么不做端点豁免开关**：豁免是**策略**（LOS 语义），不同消费方不同（下游的实时近战判定需要含端点，探针 LOS 需排除端点）。引擎给几何真理，策略留给 game，符合「机制归 engine、决策归 game」。

### 2.3 轴对齐矩形对 solid 层的滑移解算（swept）

```cpp
struct SweepResult {
    Rect box{0, 0, 0, 0};     // 解算后的位置（error 时 = 入参原值）
    bool blocked_x = false;
    bool blocked_y = false;
    TileQueryResult result = TileQueryResult::clear;  // error / clear / solid
};

// 把 box 沿 delta 移动，逐轴解算到与 solid **首次接触前**的位置（可沿墙滑动）。
SweepResult sweep_move(const SceneAsset& asset, Rect box, Vec2 delta);
```

**语义（钉死）**：

1. **轴分离、先 X 后 Y**：X 轴按 `delta.x` 解算；再以**解算后的 x** 按 `delta.y` 解算 Y。任一轴被阻挡 → 该轴停在该轴接触位置，另一轴照常移动（自然沿墙滑动）。这正是两个消费方各自选择的机制（下游 `collide_move` 同序），收敛为通用原语。
2. **swept（连续）而非单步试探**：解算结果是「前缘恰好**接触**首个 solid 单元边界」的位置；**不是**「整体位移被拒」也不是「传送到某处」。因此大 `delta` 不发生穿透、小 `delta` 与单步试探一致。下游原实现是「整体位移被拒」（大 `delta` 会少走），本原语严格更优且更确定。
3. **接触不算穿透（半开）**：停位使**被阻挡轴**的前缘与 solid 单元边界**相切**（如右移停于 `box.x+box.w == tile_left`）。故解算后 `rect_hits_solid(asset, box) == clear` **恒成立**（在满足下述不变式时）——这是本函数的核心不变式。
   > **「再朝该方向走 1px 即再被阻挡」仅在「该轴为最后解算轴、或另一轴本次无位移」时成立**。本函数先 X 后 Y：X 轴按**原始 y 跨度**判阻挡并停住；若 Y 随后移动到不同行，原先阻挡 X 的单元可能已不在新的行跨度内（这是轴分离滑动语义的固有结果，不是缺陷，也不改变上面的不变式）。测试与文档据此分轴断言，不对斜向位移强行断言「1px 即阻挡」。
4. **逐层取最紧约束**：对每个 solid 层独立求「该层允许的最大位移」，取最小值；无 solid 层 → 允许全量位移。
5. **层外不阻挡**：层矩形外的单元不参与（与 `is_solid_at` 一致）。
6. **不变式（明确写出）**：`box` 起始应**与 solid 不重叠**（game 在自由空间生成/移动实体，本函数维持该不变式）。起始即重叠时，该方向解算为 `0` 位移（不保证能脱出），但**不崩溃、不越界**。文档显式声明，避免调用方误用。
7. **参数校验**：`box`/`delta` 任一非有限，或 `box.w <= 0 || box.h <= 0` → `result=error`，返回的 `box` = 入参原值。

**实现要点（供审查）**：X 轴右移 `dx>0` 时，设 `lead = box.x + box.w`；行跨度 `ty ∈ [floor((y-oy)/th), ceil((y+h-oy)/th)-1]`（上界用 ceil-1 实现半开，避免 `y+h` 恰在边界时多算一行）；扫描列 `c` 从 `floor((lead-ox)/tw)` 起、`ox + c*tw <= lead + dx` 止，遇首个「跨度内存在 solid 单元」的列 `c` → 允许位移 `= ox + c*tw - lead`（负数则 clamp 0）；无阻挡 → 允许 `dx`。左/上/下方向为镜像。全程 `double` 计算，转 `int` 前做饱和 clamp，避免「极大但有限」坐标的 `float→int` UB（与 `rect_hits_solid` 的处理一致）。

### 2.4 公共 API 边界（本里程碑后的增量）

`AGENTS.md`「引擎公共 API 边界」追加：

- **碰撞几何**：`namespace tg` 下的自由函数（不新增子命名空间）——`aabb_overlap`（委托 raylib `CheckCollisionRecs` 的纯谓词）、`segment_hits_solid`（线段 vs solid 层的保守 supercover DDA，含 origin 逐层处理）、`sweep_move`（轴分离 swept 滑移解算）。
- 明确**不进引擎**：动态实体碰撞**规则**、刚体物理/solver、单向平台/斜坡、分层碰撞矩阵、寻路（图搜索）、相机与绘制排序。引擎只回答「矩形/线段与**静态地形几何**的关系」，**谁和谁碰、碰后如何**由 game 决定。

## 3. 步骤（含开发流程 3~7）

1. **前置查证**（已完成，记录于 §2.1）：`reference/raylib/src/rshapes.c` 核对 `CheckCollisionRecs`（严格 `<`，边界相接不算相交）；`engine/src/scene_asset.cpp:1283-1364` 核对 `world_to_tile`/`rect_hits_solid` 的 floor/半开/层外语义，作为一致性基线。
2. **实现 `engine/include/trogue/collision.hpp`**：三个函数声明（`aabb_overlap` + `segment_hits_solid` + `sweep_move`）+ 契约注释（注释自足：直接陈述语义与「为什么」，不引用内部文档/计划号）。
3. **实现 `engine/src/collision.cpp`**：几何谓词薄转换后委托 raylib；`segment_hits_solid` 逐层 AABB 裁剪 + Amanatides–Woo supercover 变体；`sweep_move` 逐轴 clamp。**只经公共查询 API 实现**（`layer_count()`/`layer(i)` 取 `LayerInfo`、`is_solid_at`/`rect_hits_solid` 或必要时 `tile_at`），**不新增 `scene.hpp` 的 friend、不读 `impl_`**（保持模块解耦，也让本模块可在生产库直测）。
4. **伞头**：`engine/include/trogue/trogue.hpp` 追加 `#include "trogue/collision.hpp"`。
5. **构建定义**：`engine/CMakeLists.txt` 的 `ENGINE_SOURCES` 追加 `src/collision.cpp`（生产库与测试库同源，自动生效）。
6. **单测** `tools/tests/collision_test.cpp`（**纯公共 API**，链接生产 `trogue_engine`）→ `tools/CMakeLists.txt` 注册为 `collision_test`。用例见 §4.1。
7. **IPC 探测命令（demo 探针，用于「实际使用」E2E）**：`game/src/main.cpp` 新增命令 `probe_collide`，参数（均可选、各自独立）：
   - `a:[x,y]`、`b:[x,y]` → 回 `{segment:{result,layer,tx,ty,t,point}}`；
   - `rect:[x,y,w,h]`、`delta:[dx,dy]` → 回 `{sweep:{box:[x,y,w,h],blocked_x,blocked_y,result}}`；
   - 非法参数 → 既有错误包络；未给参数的分区不出现。
   **并把 `probe_collide` 加入 `help` 命令列表**（`help` 是显式数组，冒烟也检查它）。这是把新原语接上「IPC 是 Agent 的眼睛」，让 Agent 无需写 C++ 即可实时验证几何解算。
8. **冒烟**：`tools/ipc_smoke.py` 追加 `probe_collide` 断言（segment 命中/未命中、sweep 全量/接触/层外、非法参数），并保持既有断言全绿。
9. **回归**：Debug + Release（`TROGUE_DEBUG=OFF`）构建**零告警**（`-Wall -Wextra -Wpedantic`）；`ctest` 全绿（含新 `collision_test`）；`python3 tools/ipc_smoke.py` 全绿。
10. **模板刷新**：`cd template && ./scripts/sync_from_source.sh`（维护者模式）→ 把新模块刷进 `template/engine/` 快照；验证模板独立副本可构建。
11. **文档**：`AGENTS.md`（架构分层 `collision` 模块、公共 API 边界增量 §2.4、Roadmap 勾选）。
12. **subagent 审查**未提交代码（合理/优雅/风格统一/无逻辑问题；**禁止自检**）。
13. 更新 `CHANGELOG.md`（审查通过后）。
14. 检查是否需要更新 `AGENTS.md`（同步骤 11，已含）。
15. 询问用户 commit message（**英文**预览，确认后提交**所有**变更并推送，禁止直接提交）。

### 文件清单（新增 / 修改）

| 文件 | 动作 | 对应 |
|------|------|------|
| `engine/include/trogue/collision.hpp` | **新增** | §2.1–2.3 |
| `engine/src/collision.cpp` | **新增** | §2.1–2.3 |
| `engine/include/trogue/trogue.hpp` | 改（伞头追加） | 步骤 4 |
| `engine/CMakeLists.txt` | 改（源文件） | 步骤 5 |
| `tools/tests/collision_test.cpp` | **新增** | §4.1 |
| `tools/CMakeLists.txt` | 改（注册测试） | 步骤 6 |
| `game/src/main.cpp` | 改（`probe_collide` 命令） | 步骤 7 |
| `tools/ipc_smoke.py` | 改（追加断言） | 步骤 8 |
| `template/engine/**` | 刷新（vendored 快照） | 步骤 10 |
| `AGENTS.md` | 改（分层/边界/Roadmap） | 步骤 11 |
| `CHANGELOG.md` | 改 | 步骤 13 |
| `docs/plan-16.md` | 新增（本文件） | — |

## 4. 验证清单

### 4.1 `collision_test`（无窗口、纯公共 API）

- **几何谓词**：`aabb_overlap` 分离/相交/仅边界相接（期望 false）/零尺寸（false）/包含。
- **`segment_hits_solid`**：
  - 水平/垂直线段穿墙 → 命中，`layer/tx/ty/t/point` 逐项核对；
  - 空区无命中 → `clear`；
  - 起点在 solid → `t==0` 命中；终点在 solid → 命中（**闭区间语义**，锁死）；
  - 精确对角线 → 验证 **supercover** 行为（擦到的相邻单元也算，不漏判）；
  - 多 solid 层（**不同 origin**，含负 origin）→ 取最小 `t`；同 `t` 取层序小者；
  - 层外线段 → 不命中；非有限坐标 → `error`。
- **`sweep_move`**（**暴力对照**，核心正确性证据）：
  - 随机夹具（小地图 + 大量种子）上，以 0.1px 步长线性扫描 `rect_hits_solid` 得到「首个阻挡位移」参考值，断言引擎解算结果落在参考值 ±步长内；覆盖 8 方向位移与斜向。**参考实现必须复刻「先 X（用原始 y）后 Y（用解算后 x）」的轴序**，否则参考值本身不符；
  - **接触语义**：阻挡时停位右/左/上/下前缘**恰好相切**solid 边界（断言 `==` 边界值），且 `rect_hits_solid(解算后) == clear`；「再走 1px 即阻挡」**仅在纯轴位移**（`delta.y==0` 或 `delta.x==0`）用例上断言（轴分离下斜向非恒真，见 §2.3 第 3 点）；
  - **沿墙滑动**：斜向撞墙 → 受阻轴停住、自由轴走满（两轴分别断言）；
  - 无 solid → 全量位移；sweep 后 `blocked_x/blocked_y` 与 `result` 组合正确（全清 = `clear`，任一挡 = `solid`）；
  - 多 solid 层不同 origin → 取最紧约束；
  - 层外不阻挡；
  - 大 `delta`（跨多格）不穿透、小 `delta` 与单步一致；
  - 退化/非法（`w<=0`、`h<=0`、非有限 `box`/`delta`）→ `error` 且 `box == 入参`；
  - **确定性**：同输入两次调用结果逐位相同。

### 4.2 E2E / 回归

- [ ] `probe_collide` 冒烟断言全绿；`ipc_smoke.py` 既有断言零回归。
- [ ] Debug + Release 构建**零告警**。
- [ ] `ctest` 全绿（Debug + Release）。
- [ ] 模板维护者模式刷新后，`template/` 独立副本能构建；且 `template/engine/CMakeLists.txt` 与 `template/engine/include/trogue/collision.hpp`、`template/engine/src/collision.cpp` 均在位（不倒依赖源仓库）。
- [ ] `git diff --name-only` 的本次改动文件中无内部文档指针；`game/src/` 的既有 "M6"/"M9" 等为**已知基线**，只要求**无新增**。

## 5. 遗留与边界

- **圆相关几何谓词（圆-圆/圆-矩形）**：当前**零消费方**（下游圆判定为裸 `sqrt` 距离），依「需求驱动 + 能用 raylib 直达的不进引擎」不进本里程碑。若未来出现 ≥2 个消费方需要「圆 vs 圆/矩形」的相交谓词（且不愿自行转换 raylib 类型），再评估纳入。
- **下游实际使用评估**：本里程碑交付原语并刷新模板快照；是否在 `engine_test` 用 `sweep_move` 替换手写 `collide_move`、用 `segment_hits_solid` 替换自建网格试探，由用户在**该项目**内重跑 `scripts/sync_from_source.sh` 后自行评估（属下游项目动作，非本仓库交付）。
- **探针 `game/` 的视线不改写**：`nav::has_line_of_sight` 保持 Bresenham 与其「端点不判定」策略（对齐原版 Lua 语义）；新原语由新单测与 `probe_collide` 验证，不强行统一（差异是策略，非缺陷）。若未来需统一，另起计划并同步更新 `game_core_test` 基线。
- **A* 寻路留在 game**：图搜索是算法与玩法决策（启发、代价、阻挡注入），engine 只提供 `is_solid_at`/`solid_mask` 作输入。
- **不做法线/接触点法向量**：`sweep_move` 只给停位与受阻轴，不返回法线（消歧成本高、当前无消费方需要）。若未来出现反弹/斜面需求再评估。
- **不做圆 swept**：当前两消费方均为 AABB 实体；圆 swept 无需求样本，不急。
- **不做 2D 物理/CCD 的物理语义**：明确归 game（可自接 Box2D 等），引擎不耦合。
