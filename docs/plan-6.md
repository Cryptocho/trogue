# 里程碑 6：移植 trogue-orign 最小闭环（移动 + 碰撞 + 回合制 + IPC 回合命令）

- 计划日期：2026-09-09
- 前置：里程碑 5（C++ 引擎迁移）已完成并提交（`eeea6cb`），依赖系统包就绪
- 参考：`AGENTS.md`「移植路线」「Roadmap」；原项目 `trogue-orign/src/`（只读）

## 1. 目的与范围

**目的**：把 trogue-orign（LÖVE2D 回合制 Roguelike）的**最小可玩闭环**移植到 C++ 引擎上：
玩家移动（含碰撞约束）→ 回合推进（玩家回合与敌方回合交替）→ 敌人执行简单行动 →
回合计数递增，形成可循环的回合制体验。同时为「非视觉 Agent 驱动」提供 IPC 回合命令，
让测试脚本可以无窗口地推进并断言回合状态。

**范围（本里程碑实现）**：

1. game 层回合制核心逻辑（`game/src/game_core.*`，纯逻辑、不依赖窗口）：
   - 回合状态机：玩家回合 → 敌方回合 → 回合计数 +1 → 回到玩家回合（对齐原版
     `turn.lua`：`turnCount` 初始 1，每次 TurnEnd +1）。
   - 移动与碰撞：单格步进（含 8 方向与斜对角切向约束，对齐原版
     `coordinates.lua:canDiagonalMove`：**仅当两个相邻正交格都被阻挡时才禁止斜切**，
     贴墙/贴树切角可走，同时保留目标格自身 solid/实体判定）。
   - 敌方回合：最小 AI（本里程碑为「静止」占位策略，随机移动作为配置项开关——
     不包含追击/攻击/视野，留后续）。
   - 平滑移动表现：玩家移动的视觉插值（帧间 lerp，非瞬移）。
2. 输入与渲染的 game 接入：键盘 WASD/方向键 + q/e/z/c 斜向移动、空格等待；tile 场景绘制
   （`render_scene`）+ 实体色块/贴图；可选的 y-sort（实体按逻辑 y/z 排序后绘制）。
3. IPC 回合命令（全部在 game handler，engine 仍是传输层）：
   - `turn`：查询当前回合阶段/回合数/玩家与实体状态。
   - `move`：`{dx, dy}`（-1..1）让玩家行动一步（受碰撞与回合门控），成功则结算整回合
     （玩家移动 → 敌方回合 → 回合 +1），返回结果。
   - `wait`：玩家跳过行动，直接结算敌方回合并推进。
4. 无窗口逻辑单测：回合推进、碰撞实体/地形、等待逻辑（加入 CTest，保持 Debug/Release 双配置）。

**明确不做（后续里程碑）**：
- 敌人 AI（视线/追击/状态机）、Combat/RuleEngine 能力系统、伤害与 HP 结算；
- 程序化地图生成（perlin/fbm/poisson）、A* 寻路、autotile、fog of war、物品/背包；
- 后置渲染（遮挡/精灵动画切换）、多场景热重载协调策略的改动。

## 2. 本里程碑语义提醒（与历史 trogue-orign 的关系）

- 地图来自 tro-scene 资产（`assets/scenes/` JSON），**不是**运行时生成：
  trogue-orign 的 `MapGenerator`（perlin/fbm/poisson 树生成）与 scene tile 不在本
  里程碑移植范围；本轮使用**手写森林关卡资产**（新 `assets/scenes/forest.json`）表达
  相同视觉效果（palette 色块地面 + 树/墙 solid 层 + 实体）。
- 实体模型为 game 自有的简单 struct（不引入 ECS；ECS 属 game 选择，本最小闭环用
  OOP 风格 Actor 表即可，后续如需再迁）。
- 引擎公共 API 不动、不加 seam；所有新逻辑全部落在 `game/` 与 `tools/` 测试侧。
- **实体单一所有权**：`game_core` 是 actor 表 + 回合状态的唯一所有权（单一 `GameState`
  结构持有全部实体与回合计数）；`main.cpp` 只通过快照查询/渲染/IPC 读取，避免双份
  状态漂移。既有 `set_entity` 作为 teleport 保持兼容（不改其行为、不走回合门控，
  以满足 ipc_smoke 既有位置保留断言与调试用途）。

## 3. 设计要点

### 3.1 坐标与网格

- tile 尺寸 = 16px（引擎 tile 查询统一像素坐标，见 `scene.hpp` 的 `is_solid_at`）。
- game 内部用「tile 坐标（grid, int）」，像素 = `tile * 16`；移动对齐 checkerboard：
  引擎判定用像素（中心点 `Vec2{tx*16 + 8, ty*16 + 8}` 或整格 `Rect{tx*16, ty*16, 16, 16}`）。
- 三态碰撞判定：目标格是否 solid（tile 层）→ 目标格是否有实体 → 斜向时检查切
  过两轴格（`canDiagonalMove` 语义对齐原版 `coordinates.lua`：**仅当两个相邻正交
  格都被阻挡时才禁止斜切**，贴墙/贴树切角可走；目标格本身仍须可通行）。移动判定
  以格子为单位（tile 粒度），不做亚格自由移动。

### 3.2 回合状态机（game 层）

```
phase: PlayerTurn → Submission(action) → EnemyTurn → EndTurn(+1) → PlayerTurn
```

- 输入门控：仅 `PlayerTurn` 阶段接受玩家移动指令（键盘或 IPC `move`）。
- 玩家每步（1 格 / 等待）都消耗 1 回合；移动被地形/实体阻挡 → 回合不前进。
- 敌方回合：所有敌人依次执行其当前 AI（本最小闭环为 0..n 顺序遍历的简单「静止」
  /随机移动策略，可作为配置项开关；结果确定性可测：静止策略下回合数可精确断言），
  随后回合计数 +1 回到玩家回合。

### 3.3 文件与模块结构（目标）

```
game/
├── CMakeLists.txt        # 目标：游戏可执行 trogue（game_core_test 放 tools/）
├── src/
│   ├── game_core.hpp/.cpp  # 回合状态机 + 移动裁决 + 简单敌人策略（纯逻辑 GameState；
│   │                       #   TilePos/方向/网格常量并入本头，不单列 types.hpp）
│   └── main.cpp            # 窗口/输入/渲染/相机/IPC handler（调用 game_core）
assets/scenes/
│   └── forest.json         # 新关卡：palette 地面 + solid 墙/树 + 玩家/敌人实体
tools/
│   ├── ipc_smoke.py        # 新增 turn/move/wait 断言组（场景无关断言）
│   └── tests/
│       └── game_core_test.cpp  # 无窗口单测（CTest；不依赖 raylib 窗口）
```

- `game_core` 设计为**无窗口、无引擎渲染依赖**（只 include `trogue/scene.hpp` 类型的
  tile 查询接口，不做渲染），因此可进 `tools/tests` 的无窗口测试；主程序仅做输入/渲染/IPC。
- **CMake 决策（评审修正）**：`game_core_test` 目标直接加进 **`tools/CMakeLists.txt`**
  （测试文件本就在 `tools/tests/`，与 AGENTS.md「测试与 consumer smoke 收敛于
  tools/」一致），用 `target_sources ... ${CMAKE_SOURCE_DIR}/game/src/game_core.cpp`
  引用 game 源码即可——顶层顺序 `engine → tools → game` 不影响源路径解析，无需在
  game 内建测试目标，也无循环依赖一说。门控沿用 tools 统一的
  `TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING`，并补
  `WORKING_DIRECTORY = ${CMAKE_SOURCE_DIR}`（测试若 `SceneAsset::load` 依赖 CWD=项目根）。

### 3.4 IPC 命令（tro-ipc v1.1 扩展，全部在 game handler）

- 对既有协议只增不改，命令表集中在 `help` 同步登记。
- `turn`：req `{}`。data = `{phase, turn_count, player: 实体快照, enemies: 实体快照数组}`；
  `phase` 枚举 `"player"`/`"enemy"`（对齐原版 `turn.lua` 相位语义），turn_count 初始 1。
- `move`：req `{dx, dy}`，整数值各在 -1..1（**允许斜向，即 dx、dy 可同时非零**；
  非整数 dx/dy 直接报错 `{"ok":false,"error":"move needs integer dx/dy"}`，不静默截断）。
  **包络约定**：`invalid`（`(0,0)`、dx/dy 值域越界）与 `blocked`（目标被占/固、目标格
  越出地图边界、回合不前进）走成功包络 `{"ok":true,"data":{"result":"invalid"|"blocked",
  ...}}`；仅「非玩家回合」用 `{"ok":false,"error":"not player turn"}`（当前同步结算
  下不可达，为未来异步敌回合分支预留）。`result=="moved"`：成功包络含
  `turn:{phase, turn_count}` 等，game 自动结算敌方（本里程碑为「静止」策略，无随机）
  回合并回合 turn+1。（确定性：**本里程碑敌人回合为静止策略**，故 smoke 可精确断言
  回合计数；随机移动留待后续 AI 里程碑。）
- `wait`：req `{}`。成功包络 `{"ok":true,"data":{"result":"waited", "turn":{phase, turn_count}, ...}}`，
  回合推进 1（敌方回合 + turn+1）。
- 保持既有命令（list_entities/set_entity/...）不变，兼容跳过。

### 3.5 场景资产（forest.json）

- palette 模式（无 tileset），2 层：地面层（非 solid，全 0）+ 墙层（solid，外框与
  若干内部障碍），尺寸约 24×16 或 32×20，便于一眼看清。
- 实体：`player`（id=player,type=player）与 2-3 个 `enemy`（type=goblin 之类，
  用色块/简单贴图区分）；origin 与 8 常量按引擎约定。

## 4. 实施步骤（按开发流程门禁 3–7）

1. 写计划书（本文件）→ 交 subagent 审查 → **PASS 才开工**；
2. 用户批准计划书；
3. 实现：
   a. 新增 `assets/scenes/forest.json`（手写关卡）；
   b. `game/src/game_core.hpp/.cpp`：回合状态机 + 移动裁决 + 敌方 AI（最小）+ 插值信息；
   c. `game/src/main.cpp` 增量改造：接入 grid 输入（WASD/方向/等待）、渲染 tile+实体、
      简易相机、IPC 增加 `turn/move/wait` 命令；`game/CMakeLists.txt` 把
      `game_core.cpp` 挂入 trogue 目标（源文件清单同步）；
   d. `tools/CMakeLists.txt` 加入无窗口测试目标（`game_core_test`，引用 game 源码）；
   e. `tools/tests/game_core_test.cpp`：无窗口断言（碰撞/回合推进/等待）；
   f. `tools/ipc_smoke.py` 新增回合断言组；
   g. 构建验证 Debug + Release（零告警基线），ctest 全绿，手动运行截图检查；
4. **subagent 检查**未提交代码（本流程规定禁止自检）；
5. 检查通过后更新 `CHANGELOG.md` + `AGENTS.md`（Roadmap、里程碑记录、移植路线表）；
6. 询问用户是否写 commit message → 给出英文 commit message 预览等待确认；
7. 确认后提交全部变更。

## 5. 验证与验收

- 构建：`cmake --build build`（Debug）+ `cmake --build build-release`
  （`-DTROGUE_DEBUG=OFF`），两套零 `-Wall -Wextra -Wpedantic` 告警。
- 测试：
  - `ctest --test-dir build` → 全部通过（含原 7 项 + 新增 game_core_test）。
  - `python3 tools/ipc_smoke.py` → 23 项原断言（默认 demo 场景）+ 新回合断言
    （场景无关：通过 `turn`/`move`/`wait` 返回结构断言，不硬编码实体数）全过。
- 手动：`./build/bin/trogue --scene assets/scenes/forest.json`，
  WASD/方向键走格子、**仅当两正交邻格都被阻挡时斜向被禁（贴单侧墙可切角）**、
  被实体挡住不动不消耗回合；IPC：`move` 后 `turn` 计数 +1、敌人可见状态，
  `wait` 正常消耗回合。

## 6. 文件变更清单（预期）

- 新增：`docs/plan-6.md`、`assets/scenes/forest.json`、
  `game/src/game_core.hpp`、`game/src/game_core.cpp`、`tools/tests/game_core_test.cpp`
- 修改：`game/src/main.cpp`、`game/CMakeLists.txt`、`tools/CMakeLists.txt`、
  `tools/ipc_smoke.py`、`CHANGELOG.md`、`AGENTS.md`（流程末尾）

## 7. 遗留 / 下一里程碑（草案）

- A* 寻路（点击目标格移动）、敌人目标/追击/视线/伤害与能力系统（后续里程碑）；
- 程序化地图生成（perlin/fbm/poisson）与 autotile 渲染；
- 物品/背包/技能系统；镜头缩放/后处理、Fog of War。