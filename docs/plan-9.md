# 里程碑 9：敌人 AI + RuleEngine 最小子集 + 首批游戏事件（引入 EventBus）

- 计划日期：2026-09-09（r2：按第一轮审查意见修订——管线事件序对齐原版、smoke filter 断言重设计、GameOver 相位口径钉死、AI 资格原型门控、签名兼容策略、事件命名统一）
- 前置：里程碑 6（回合制最小闭环）、7（IPC 事件通道）已完成；M6 遗留的「敌方回合为静止占位」正是本里程碑的接入点
- 参考：`trogue-orign/src/systems/ai.lua`（224 行）、`trogue-orign/src/core/events.lua`（EventBus）、`trogue-orign/src/core/rule_engine.lua`（928 行，本里程碑只取最小子集）、`trogue-orign/src/core/coordinates.lua`（chebyshev/Bresenham LOS/A*）、`trogue-orign/src/data/definitions/{ability,effect}.lua`、`trogue-orign/src/data/prototypes/entities.lua`；`AGENTS.md`「引擎公共 API 边界」「Agent-first 开发模型」「IPC 事件通道语义」「不重复造轮子」「数值精度纪律」；`docs/plan-6.md`、`docs/plan-7.md`

## 1. 目的与问题

1. **敌方回合是占位**：M6 的 `resolve_enemy_turn` 只做「回合 +1」静止结算。原版 AI（idle/alerted/chasing 三态 + 视野 + 行为表 + A*）语义完全现成，触发点同为 PlayerTurnEnd，接入点干净。
2. **事件注册表是空的**：M7 建好了传输层通道（subscribe/filter/publish），但 game 层没有任何真实事件。AI 调试正是当时 filter 单实体观测的动机——Agent 订阅 `filter:{"entity":"goblin_1"}` 即可看它的状态迁移/移动，无需逐回合轮询 `list_entities`。
3. **玩法逻辑需要一个进程内事件总线**：原版架构中 AI 只 `emit("AbilityUse", ...)`，不直接调用 RuleEngine；本里程碑引入 game 层 **EventBus**，规则管线（AbilityUse → 伤害结算 → AbilityUsed / EntityDied）真正事件驱动。引擎仍只提供传输（`tg::Ipc::publish`），不识任何事件语义——EventBus 归 game 层，符合「引擎公共 API 边界」。

**范围（本里程碑实现）**：

1. game 层 EventBus（对齐原版 `events.lua` 的最小核心：on/off/emit + priority）。
2. RuleEngine 最小子集：1 能力（punch）+ 1 效果（damage_physical）+ 冷却 + 伤害结算 + 死亡事件；原型门控内 Actor 增加 HP。
3. 敌人 AI：三态状态机 + 视野（chebyshev ≤ 5 + Bresenham LOS，solid 层遮挡）+ 行为表 + A* 寻路（8 向 + 切角约束）+ 固定种子随机（可测试）。
4. 首批对外事件（6 个，经 `tg::Ipc::publish` 广播）+ IPC `events` 注册表从空表变实表；实体快照按原型门控注入 `hp`/`ai` 观察字段。
5. 无窗口单测 + 冒烟扩展 + 文档更新。

**明确不在范围**：玩家攻击输入（本里程碑玩家只能被咬）、武器/公式/暴击/资源（energy）、Buff/持续效果、rat/orc 等更多原型、趁手打击/击退、逐实体 fieldOfView 属性（用常量 5，见 §2 注）、`emitTo`/`child` 等 EventBus 高级 API、异步敌方回合、攻击动画（Soldier 帧表暂不消费）。

## 2. 原版语义对齐明细（全部读自源码，非记忆；差异逐条声明）

### 2.1 常量

| 常量 | 值 | 原版出处 |
|------|----|---------|
| `VISION_RANGE` | 5（chebyshev 距离，11×11 格） | ai.lua:7 |
| `ALERT_DELAY` | 1（发现玩家后停 1 个敌方回合再追） | ai.lua:8 |
| idle 游走概率 | 70%（`math.random() < 0.7`） | ai.lua:124 |
| 游走方向 | 4 向均匀随机（上下左右） | ai.lua:125 |
| 攻击距离 | `dist <= 1`（chebyshev，含斜邻） | ai.lua:62,140 |
| A* 迭代上限 | 1000 | coordinates.lua:119 |
| 对角步代价 | 1.414（启发函数 = chebyshev） | coordinates.lua:76,101 |
| punch | cooldown=0、cost={}、targetType=SINGLE、range=1、effects={damage_physical} | ability.lua:81 |
| damage_physical | type=damage、value=5 | effect.lua:77 |
| player HP | 100/100 | entities.lua player.Stats |
| goblin HP | 25/25、abilities={punch=true} | entities.lua goblin |

### 2.2 状态机（ai.lua:61-100，逐条对齐）

设本回合敌方阶段开始时 `turn = gs.turn_count`（每完成一次敌方阶段 +1，与原版 AI 自维护 `turnCount` 每阶段 +1 语义等价）。**先迁移后行动**（原版同一次循环内先改状态、再按**新状态**取动作）：

| 现态 | 迁移条件 | 本回合动作 |
|------|---------|-----------|
| idle | `canSee` → alerted，记 `alerted_turn = turn`、`target = 玩家格` | 迁移发生则本回合动作 = alerted 的「停」；未迁移才游走 |
| alerted | `turn - alerted_turn >= 1` → chasing；期间 `canSee` 持续刷新 target | 停（不动、不攻击） |
| chasing | `canSee` 刷新 target；`!canSee && pos == target` → idle（清 target） | 贴脸（chebyshev ≤1）攻击；否则 A* 走一步 |

推论（对齐 ai.lua:86-95 的「先迁移后行动」）：chasing→idle 的**当回合**按 idle 行动（即参与 70% 游走）；idle→alerted 的当回合**停**。单测按此断言。

### 2.3 视野（ai.lua:108-121 + coordinates.lua:194-217）

`canSee = chebyshev(enemy, player) <= 5 && hasLineOfSight(...)`。Bresenham 逐格检查：

- 起点与终点**都不检查** solid（coordinates.lua:212 `if (x ~= x2 or y ~= y2) and isSolid(x,y)`——终点格不判定，贴墙玩家仍可见）；
- 遮挡物 = solid tile 层。实现用引擎 `tg::is_solid_at`（世界像素 = 格 × 16 + 8 中心点），仅 `TileQueryResult::solid` 视为阻挡；层矩形之外按引擎语义「无数据 = 不阻挡」，与原版一致（原版 `map_renderer:isSolid` 对界外返回 false，map_renderer.lua:120-125）。

### 2.4 A* 寻路（coordinates.lua:81-187 + ai.lua:138-187）

- 8 向邻接；启发 = chebyshev；对角步代价 1.414；到目标返回路径（含起点），取 `path[2]` 与当前位置的差为一步；无路径（open 空或 1000 次迭代）→ 本回合原地不动；
- `isPassable(x,y)` = 界内 && 非 solid tile；
- `getBlockingEntity(x,y)` = 该格有**其他敌人**则该节点不可扩展。**玩家格不是阻挡**：原版 player 原型没有 `Actor` 组件（只有 `Player`），而 blocking 只查 `Solid or Actor`（ai.lua:159-171）——玩家所在格是合法寻路节点，路径可达目标；玩家最终由移动系统的实体互斥挡住（走到玩家格的移动必 Blocked）。移植 blocking 谓词 =「非玩家 actor 占据」；
- **切角约束差异声明**：原版 A* 内切角谓词 `isObstacle = not isPassable` **只看地形**（coordinates.lua:70-74），其移动系统切角才含实体；M6 的 `can_diagonal_move`（game_core.cpp:124-132）**含全部实体**。本实现 A* 切角 = **地形 + 战斗实体**（注入的 `blocked_by_actor`；coin 等惰性实体不参与）——比原版 A* 宽、比 M6 `try_move` 窄：A* 可能给出被 coin 挡切角的斜步而遭 `try_move` 拒绝，敌原地一站（与原版行为一致，仅影响绕路选择、不影响正确性），记录为有意差异；
- 实现形态：只返回下一步格（`astar_step`），调用方零状态。

### 2.5 攻击与规则管线（rule_engine.lua:96-135, 183-268 + ai.lua:189-216）

原版 `tryUseAbility` 的真实顺序（r1 审查修正点）：**资源/冷却先扣，效果结算次之，AbilityUsed 最后且仅成功时发布**——

```
AI(贴脸) → emit AbilityUse{entity, abilityId, targetX, targetY}
  → RuleEngine(AbilityUse handler):
      校验（能力存在/已学/冷却/非 passive/cost 可付；最小子集仅取：存在 + 冷却 0 + 目标有效）
        失败 → emit AbilityUseFailed（原版 rule_engine.lua:232；内部事件）
      通过 → 设冷却（原版仅 cooldown>0 才写 cooldowns 表，rule_engine.lua:252-255；
              punch=0 不写，C++ 同此——不登记 0 冷却）
      → applyAbility/applyEffects：emit DamageRequest{source, target, amount}
          → RuleEngine(DamageRequest handler, priority 100)：target.hp -= amount
            → emit DamageDealt{source, target, amount, hp, max_hp}
            → hp <= 0 → emit EntityDied{entity}（rule_engine.lua:669-681）
      → 最后 emit AbilityUsed（rule_engine.lua:261-268，仅 success/有有效目标时）
TurnEnded（敌方阶段收尾）→ RuleEngine(handler, priority 100)：全体冷却 -1（下限 0，rule_engine.lua:779-796）
```

**观察者看到的 wire 顺序因此是 `DamageDealt` 先于 `AbilityUsed`**——与原版一致，有意保留，勿「修正」。

最小子集的有意差异与加固（逐条声明）：

- 省略原版 canUse 的 learned/passive/cost 检查（rule_engine.lua:183-219）——子集内唯一能力 punch 无 cost、必然已学；
- **射程校验（chebyshev(源,目标) ≤ range）是 C++ 加固**，原版 RuleEngine 不校验 range；
- `AbilityUsed` 载荷字段 `{entity, ability, target:[gx,gy], turn}` 为自有设计（原版为 `abilityId/targetX/targetY`），与首批事件注册表统一；
- 原版 AI 攻击前的 `ruleEngine:canUse` 预检（ai.lua:207-208）省略——punch 冷却恒 0，行为等价；失败路径由单测直接驱动 `try_use` 覆盖；
- 「无有效目标不发 AbilityUsed」的 C++ 等价：`try_use` 入口先校验目标存在/存活/在射程内，任一不满足 → 仅 `AbilityUseFailed`，不扣冷却、不发 DamageRequest、不发 AbilityUsed；
- 伤害为固定值 5（原版 valueFormula/weapon/暴击全部不在子集）；
- 死亡处理对齐原版延迟销毁：EntityDied 只打 `pending_despawn` 标记，敌方阶段收尾统一清除（避免 emit 同步回调中 erase 破坏遍历；对齐原版 ShouldDespawn/processDespawns）；
- **玩家 hp ≤ 0**：不 despawn，`gs.phase = GameOver`（新相位，语义见 §3.3）；`reload`/热重载按既有语义重建即复位；
- 载荷差异补充（r2 审查 N4）：原版 `DamageDealt = {source,target,amount,actualDamage,damageType,blocked,newHealth}`（rule_engine.lua:543-551）、`EntityDied = {entity,killer}`（:675-678）；本子集无盾格挡/无 killer 概念，载荷收敛为 `{source,target,amount,hp,max_hp}` / `{entity,turn}`，语义等价。

### 2.6 事件流架构差异（有意为之，写入文档）

原版 TurnSystem 靠 MoveAttempt/MoveSucceeded/CollisionDetected 事件做 `inputAllowed` 异步门控；本项目 M6 已是**同步直接调用**结算（`player_move` 内联敌方回合），该门控机制无存在必要。因此本里程碑**不引入** `MoveAttempt`/`PlayerTurnEnd`/`CollisionDetected` 内部事件（无消费者，不造空事件）：移动裁决保持直接调用，`MoveSucceeded` 由 `try_move` 在成功点直接 emit；只有**规则管线**走事件驱动（AI 不知道 RuleEngine 存在，只 emit AbilityUse）。这与 M6 同步架构一致，避免为对齐而对齐。

## 3. 方案

### 3.1 EventBus（`game/src/event_bus.hpp`，header-only，约百行；纯逻辑、零 IPC 依赖，无窗口可测）

```cpp
namespace game {
using EventData = tg::Json;                       // 事件载荷 = tg::Json（与 IPC wire 同构，桥接零转换）
using EventHandler = std::function<void(const EventData&)>;
using Subscription = std::uint64_t;               // 0 = 无效

class EventBus {
public:
    Subscription on(std::string_view name, EventHandler handler, int priority = 0);
    void off(Subscription id);                    // 按 handle 注销
    void emit(std::string_view name, const EventData& data);
    std::size_t count(std::string_view name) const;
};
}
```

- **载荷用 `tg::Json`**：单一表示，main 层桥接 `tg::Ipc::publish(name, data)` 零转换；事件 data 顶层 `entity`/`source`/`target` = 字符串 id，与 M7 filter「顶层字段等值匹配」口径直接兼容；
- **priority 越小越先执行**（对齐原版），同优先级按注册序（稳定排序，内部记单调 seq）；dirty 标记 O(1) 注册/注销、emit 时延迟重建有序表（对齐原版优化）；
- **emit 先快照后调用**：拷贝 handler 列表再逐个调用，handler 内 on/off/emit 重入不迭代器失效（M5 tween 回调重入 UB 教训的直接应用）；
- 单线程主循环内使用，无锁；不实现 emitTo/emitToMany/child（原版有，本里程碑无消费者，遗留）。

### 3.2 导航原语（`game/src/nav.hpp/.cpp`，纯函数，可无窗口单测）

```cpp
namespace game::nav {
int chebyshev(int x1, int y1, int x2, int y2);
bool has_line_of_sight(int x1, int y1, int x2, int y2,
                       const std::function<bool(int,int)>& is_solid);   // Bresenham，两端点不判定
// 单一数据来源 = GameState（gs.asset 做 is_solid_at、gs.map_w/h 做界内判定；
// 实体阻挡经注入回调，保持寻路逻辑本身可注入可测）
std::optional<TilePos> astar_step(const GameState& gs, TilePos from, TilePos goal,
                                  const std::function<bool(int,int)>& blocked_by_actor);
}
```

- `astar_step`：8 向 + chebyshev 启发 + 对角代价 1.414 + 切角约束（复用 `can_diagonal_move`，差异见 §2.4）+ 地形经 `gs.asset` 的 `tg::is_solid_at` + `blocked_by_actor` 节点过滤（敌人互挡、玩家格不挡，§2.4）+ 迭代上限 1000；返回**下一步格**（无路径 → nullopt）；
- `has_line_of_sight` 不依赖任何引擎类型（`is_solid` 注入：AI 注入 `tg::is_solid_at` 包装，单测注入字面地图）；
- 单测经由公共 `SceneAsset::load` 临时文件构建 `GameState`（既有测试模式），nav 不新增引擎依赖。

### 3.3 回合与移动重构（`game_core.hpp/.cpp`）

**原型门控（r1 审查 M4）**：AI/战斗资格按 type 原型表决定，唯一入口：

```cpp
// game_core.hpp：战斗原型表（本里程碑仅 goblin）
bool is_combat_archetype(std::string_view type);   // type == "goblin"
```

- `import_scene`：`type=="player"` → hp 100/100；`is_combat_archetype(type)`（goblin）→ hp 25/25 + `AiState` + 冷却表 + AI 资格；**其余非玩家实体（如 demo.json 的 coin_1/coin_2）为惰性实体：无 AI、无 hp、无冷却，不参与战斗、不发事件，保留 M6 既有行为（占格阻挡）**——对齐原版「AI 只遍历带 Actor+AIState 的实体」（ai.lua:41），演示场景的硬币不会变成主动攻击单位；
- `struct Hp { int cur; int max; }`；`Actor` 增加 `Hp hp`、`std::map<std::string,int> cooldowns`、`AiState ai`（§3.5，仅战斗原型初始化）；`GameState` 增加 `std::vector<std::string> pending_despawn`、`Phase` 增加 `GameOver`；热重载重建按原型表复位（AI 状态回 idle，descriptor 导入语义，文档注明）；
- **系统上下文与签名兼容（r1 审查 M5）**：

```cpp
struct GameSystems { EventBus* bus = nullptr; RuleEngine* rules = nullptr; AiSystem* ai = nullptr; };

// 共享实现：sys == nullptr 时退化为 M6 静止结算语义（无事件、无 AI）——单一代码路径
ActionResult player_move(GameState&, int dx, int dy);                    // M6 兼容签名（既有测试原样调用）
ActionResult player_move(GameState&, GameSystems&, int dx, int dy);     // main/IPC 用（完整 AI 敌方阶段）
ActionResult player_wait(GameState&);                                    // M6 兼容签名
ActionResult player_wait(GameState&, GameSystems&);                      // main/IPC 用
void resolve_enemy_turn(GameState&, const GameSystems* sys = nullptr);   // 收尾（含 GameOver 分支）；r1→r2 签名由 (GameState&) 扩展，缺省 nullptr 保持可独立调用
ActionResult try_move(GameState&, EventBus* bus, const std::string& actor_id, int dx, int dy);
```

  两条 `player_move` 重载汇入同一内部实现（`sys` 可空），M6 既有测试（13 个测试函数：8 回合/移动 + 5 输入缓冲）**零改动通过**：无 sys 时敌方阶段仍「回合 +1、回玩家回合」，只是不跑 AI、不发事件；
- **phase 守卫层级（r2 审查 N1）**：GameSystems 版 `player_move`/`player_wait` **入口**校验 `phase != PlayerTurn`（含 GameOver）→ `Invalid`——单测断言「GameOver 下拒绝」落在 game_core 层而非 IPC handler；M6 兼容重载不新增守卫（M6 测试从不在非玩家回合调用，零改动不受影响）；IPC 层既有 `phase != PlayerTurn` error 包络继续兜底，双层一致；
- `try_move`：M6 玩家裁决逻辑泛化（地形 solid / 实体互斥 / 斜切切角，全部现成），成功改 `pos` 并 `emit("MoveSucceeded", {entity, from:[gx,gy], to:[gx,gy]})`（bus 非空才 emit）；玩家路径成功后走既有 end-player-turn 流程；
- **收尾与 GameOver（r1 审查 M3）**：`resolve_enemy_turn(gs, sys)`——

  1. `sys` 非空且玩家存活：`ai.run_enemy_phase(...)`（§3.5；阶段内玩家死亡则**立即停止剩余敌人行动**，对齐原版「无玩家则直接收尾」ai.lua:43-46 的精神）；
  2. 统一清除 `pending_despawn`（GameOver 也清除——尸体移除与相位无关）；
  3. **若 `gs.phase == GameOver`：不 +1 回合、不发 TurnEnded、保持 GameOver**（否则敌方阶段内死亡会被收尾覆盖回玩家回合）；否则 +1、回 PlayerTurn、`emit("TurnEnded", {turn})`；
- **GameOver 的 wire 口径**：`Phase` 序列化新增 `"game_over"`（`turn` 命令的 `phase` 字段与 `status` 新增 `phase` 字段同口径）；`move`/`wait` 在 GameOver 下走既有「非玩家回合」error 包络（`phase != PlayerTurn` 检查现成，无新代码）；smoke 基线中 `phase ∈ {player, enemy}` 断言不受影响——冒烟流程不会打死玩家（§3.7 说明）。

### 3.4 RuleEngine 最小子集（`game/src/rules.hpp/.cpp`）

```cpp
namespace game {
struct AbilityDef { std::string id; int cooldown = 0; int range = 1; std::vector<std::string> effects; };
struct EffectDef  { std::string id; int value = 0; };          // damage 固定值（公式/武器不在子集）

class RuleEngine {
public:
    RuleEngine();                         // 内置 punch / damage_physical 定义
    void bind(GameState&, EventBus&);     // 订阅 AbilityUse / DamageRequest / TurnEnded（AbilityUse=0，其余=100）；
                                          // 内部持有 gs_/bus_（生命周期契约见 rules.hpp：Demo 成员级/单测同作用域）
    bool can_use(const Actor&, const std::string& ability) const;   // 存在 + 冷却 0
    bool try_use(const std::string& entity, const std::string& ability,
                 TilePos target);         // 失败已 emit AbilityUseFailed（供 handler 与单测直驱）
    std::map<std::string, AbilityDef> abilities;  // punch（测试可注册新条目）
    std::map<std::string, EffectDef> effects;     // damage_physical
};
}
```

管线**严格按 §2.5 原版顺序**：AbilityUse → 校验（失败仅 AbilityUseFailed）→ 设冷却（仅 >0 才记）→ DamageRequest → （handler）DamageDealt → EntityDied → 最后 AbilityUsed。冷却递减订阅**对外事件 `TurnEnded`**（不另设内部 `TurnEnd`，事件名单一，r1 审查 M6）。

### 3.5 AI 系统（`game/src/ai.hpp/.cpp`）

```cpp
namespace game {
enum class AiPhase { idle, alerted, chasing };
struct AiState { AiPhase state = AiPhase::idle; int alerted_turn = 0; bool has_target = false; TilePos target; };

struct AiSystem {
    explicit AiSystem(std::uint32_t seed);        // 固定种子（默认常量 20260909），单测/复现可另设
    void run_enemy_phase(GameState&, EventBus&);  // 攻击经 bus 发 AbilityUse（rules.bind 已订阅），不直接依赖规则引擎
    std::mt19937 rng;
};
}
```

- 仅遍历**战斗原型** actor（§3.3 门控；对齐原版 ai.lua:41 只查 AIState 实体），id 序遍历（确定性）；
- 逐敌人：视野判定 → §2.2 状态迁移（仅迁移时 emit `StateChanged{entity, from, to, turn}`）→ 按新状态行动；
- 行动：idle → 70% 概率 4 向均匀游走（`try_move`；失败即原地，不重试）；alerted → 无动作；chasing → 贴脸 `bus.emit("AbilityUse", {entity, ability:"punch", target:玩家格})`；否则 `nav::astar_step` → `try_move`；
- 目标恒为玩家；无玩家/玩家已死/GameOver → 跳过整个 AI 阶段直接收尾；阶段中玩家死亡 → 立即停止剩余敌人（§3.3）；
- 视线遮挡回调 = 引擎 `tg::is_solid_at`（solid 层，§2.3）。

### 3.6 接线与观测（`game/src/main.cpp`）

- `Demo` 持有 `EventBus bus`、`RuleEngine rules`、`AiSystem ai`、`GameSystems sys{&bus,&rules,&ai}`；启动时 `rules.bind(bus)`；
- **IPC 桥接**（main 层完成，EventBus 保持零 IPC 依赖）：对白名单 6 事件逐个 `bus.on(name, [..](d){ ipc.publish(name, d); })`，事件名与 wire 同名；
- **首批对外事件与注册表**（`events` 命令从空表变实表；`entity`/`source`/`target` 均为字符串 id）：

| 事件 | data 顶层字段 | 可 filter 字段 | 何时发布 |
|------|--------------|---------------|---------|
| `StateChanged` | entity, from, to, turn | `entity` | 敌人 AI 状态迁移 |
| `MoveSucceeded` | entity, from:[gx,gy], to:[gx,gy] | `entity` | 任何 actor 移动成功 |
| `AbilityUsed` | entity, ability, target:[gx,gy], turn | `entity` | 规则层成功释放（伤害结算**之后**，§2.5） |
| `DamageDealt` | source, target, amount, hp, max_hp | `source`、`target` | 伤害结算 |
| `EntityDied` | entity, turn | `entity` | hp≤0（敌人在收尾才真正移除） |
| `TurnEnded` | turn | —（无实体字段，仅全量订阅可收） | 敌方阶段完成、回合数 +1 后（GameOver 不发，§3.3） |

  内部事件（不发布）：`AbilityUse`、`AbilityUseFailed`、`DamageRequest`。**没有**其它内部事件（r1 审查 S9/M6：MoveAttempt/CollisionDetected/PlayerTurnEnd/TurnEnd 均不引入）；
- **实体快照注入**：经既有 `Demo::extra_entity_fields` 注入点，仅对有 hp 的 actor 追加 `hp:[cur,max]`（数组形态，与 wire 既有 `[vx,vy]` 风格一致）、仅对战斗原型追加 `ai:{state, target:[gx,gy]|null}`（惰性实体两字段皆无）——`list_entities`/`get_entity` 直接可读；
- **敌人视觉移动**：复用引擎 `tg::TweenManager::add_vec2`（0.12s quad_out，与玩家同参数），Demo 内 `std::map<std::string, EnemyView{vx,vy,tween_id}>` 承载；`MoveSucceeded` handler（敌人且非玩家）驱动；despawn/GameOver 清理对应条目；**热重载时随既有 `tween.cancel_all()` 一并清空该 map**（r1 审查 S8）；transform 视图扩展为「任意 actor 静止时 visual == 逻辑格像素」，播完精确落格（数值精度纪律沿用）；
- HUD：GameOver 时显示提示；`status` 增加 `phase` 字段；`turn` 命令 `phase` 字段三值化（§3.3）。

### 3.7 冒烟与测试

- **`tools/tests/game_core_test.cpp` 新增**（全部固定种子/字面地图/临时场景 JSON，确定性）：
  1. nav：chebyshev；LOS 直线/斜线可见、墙格遮挡、终点在墙内仍可见（端点不判定）；
  2. nav：A* 绕墙取步正确、被完全围死 → nullopt、目标为玩家格可达（玩家不挡）；
  3. 状态机：视野内 idle→alerted（记 target、当回合**停**不游走）；alerted 停一回合；下一回合→chasing；chasing 丢视线且抵达记忆位→idle（target 清空，**当回合按 idle 游走**，§2.2 推论）；
  4. 追击：走廊径直逼近（每回合一步）；地形阻挡 → 原地；
  5. 攻击：贴脸 `wait` 两回合（alerted 延迟）后 hp 100→95，观察序 DamageDealt 先于 AbilityUsed（§2.5）；hp≤5 敌人被击→EntityDied + 收尾后从 actors 移除；玩家 hp≤0 → phase=GameOver、**收尾不 +1 回合、不发 TurnEnded**、`player_move/wait` 拒绝；
  6. 游走：固定种子下逐回合位置可复现（确定性断言）；
  7. 冷却：注册 cooldown=2 的测试能力 → 用一次后 can_use=false，两次 TurnEnded 后恢复；punch（cooldown=0）不写 cooldowns；
  8. 门控：coin 类惰性实体无 hp/ai、不行动、不挡 A*（玩家不挡同理覆盖）；
  9. EventBus：priority 顺序、off 生效、emit 内 off 自身不崩溃（快照语义）；
  10. 兼容：M6 既有 13 个测试函数零改动通过（无 sys 重载路径）。
- **`tools/ipc_smoke.py` 扩展**（44 项基线之上只增）：
  1. `events` 注册表非空且含 6 事件条目（修订 M7 的「注册表为空」断言，ipc_smoke.py:209-211）；
  2. 连接 A（无 filter）订阅 + `move`/`wait` 驱动：收到 `TurnEnded`、玩家 `MoveSucceeded`；`status.phase=="player"`；
  3. **filter 单实体观测实战用例**（r1 审查 M2 重设计——按字段订阅，M7 同连接多 filter 并存）：连接 B 订阅两组：事件名 {StateChanged, MoveSucceeded, AbilityUsed, EntityDied} × filter `{"entity":"goblin_1"}`，事件名 {DamageDealt} × filter `{"target":"player"}` → `solid_at` 选玩家邻格通行格 → `set_entity` 传送 goblin_1 → `wait` ×2 → 连接 B 应收到：`StateChanged`(idle→alerted)、`StateChanged`(alerted→chasing)、`DamageDealt`(source=goblin_1, target=player, hp 100→95)、`AbilityUsed`(entity=goblin_1，且**后到**——wire 序 DamageDealt→AbilityUsed 的第二个实驱观测点，r2 审查 N2)；**反向断言**：连接 B 收不到 `TurnEnded`（filter 缺对应键 → 缺键不匹配的正面验证）、收不到其他实体的事件；连接 A 全程照收 `TurnEnded`（证明发布与 filter 隔离并存）；
  4. `list_entities` 出现 `hp`/`ai` 注入字段（goblin 有、coin 无）。
- **回归门禁**：Debug/Release 零告警（`-Wall -Wextra -Wpedantic`）、ctest 全绿（新旧用例）、smoke 全过（基线 44 项中原 `phase` 断言不受影响——冒烟不击杀玩家，`turn.phase` 三值化只在 GameOver 出现新值）。

## 4. 文件清单

| 文件 | 动作 |
|------|------|
| `game/src/event_bus.hpp` | 新增（header-only，纯逻辑零 IPC 依赖） |
| `game/src/nav.hpp` / `game/src/nav.cpp` | 新增 |
| `game/src/rules.hpp` / `game/src/rules.cpp` | 新增 |
| `game/src/ai.hpp` / `game/src/ai.cpp` | 新增 |
| `game/src/game_core.hpp` / `game/src/game_core.cpp` | 修改（Hp/AiState/pending_despawn/GameOver/原型门控/try_move/GameSystems 重载） |
| `game/src/main.cpp` | 修改（bus/rules/ai 接线、IPC 桥接、注册表、快照注入、敌人 tween、GameOver HUD/wire） |
| `game/CMakeLists.txt` / `tools/CMakeLists.txt` | 修改（新源文件；测试 include 路径维持 `game/src` 不分叉） |
| `tools/tests/game_core_test.cpp` | 修改（新增用例） |
| `tools/ipc_smoke.py` | 修改（§3.7） |
| `docs/plan-9.md` | 新增（本文档） |
| `CHANGELOG.md` / `AGENTS.md` | 步骤 8 更新（IPC 事件注册表小节、移植路线、Roadmap） |

## 5. 步骤（含开发流程 3~7）

1. `game/src/event_bus.hpp` + EventBus 单测（priority/off/快照重入）。
2. `game/src/nav` + LOS/A* 单测。
3. `game_core` 重构（Hp/原型门控/try_move/GameSystems 重载/GameOver/延迟销毁），M6 既有测试零改动保绿。
4. `game/src/rules` + 管线单测（含 wire 顺序断言）；`game/src/ai` + 状态机/行为单测（固定种子）。
5. `main.cpp` 接线：桥接 + 注册表 + 快照注入 + 敌人 tween + GameOver HUD/wire。
6. smoke 扩展 + 全量回归（零告警/ctest/smoke）+ §3.7.3 订阅场景实驱验证。
7. **subagent 检查**未提交代码（合理/优雅/风格统一/无逻辑问题；禁止自检）。
8. 更新 `CHANGELOG.md` 与 `AGENTS.md`（IPC 事件注册表小节、移植路线表、Roadmap 勾选）。
9. 询问用户 commit message（英文预览，确认后提交所有变更，禁止直接提交）。

## 6. 验证清单

- [ ] 新增单测全绿：EventBus / nav / 状态机 / 攻击管线（wire 序 DamageDealt→AbilityUsed）/ 死亡 / GameOver 收尾 / 冷却 / 固定种子游走 / 惰性实体门控
- [ ] M6 既有 13 个测试函数零改动通过；Debug/Release 零告警；ctest 全绿
- [ ] smoke：44 基线 + 新增断言全过（含 filter 单实体观测实战用例与缺键不匹配反向断言）
- [ ] `events` 注册表实表（6 事件）；`list_entities` goblin 有 hp/ai、coin 皆无
- [ ] 脚本场景：goblin_1 传送至玩家旁 → 两次 `wait` → 连接 B 收到 StateChanged×2 + DamageDealt(hp 100→95)，连接 A 照收 TurnEnded
- [x] GameOver：玩家 hp 压到 ≤5 后被咬 → phase=game_over（status/turn 一致）、HUD 提示、收尾不 +1 回合、reload 复位（wire 唯一手段：tro-scene 无 hp 字段、set_entity 无 hp 键 → 用连续 ~19 次 `wait` 被咬至 ≤5，r2 审查 N3）
  - **2026-09-10 实测通过（脚本驱动）**：传送 goblin_1 至玩家邻格后连续 `wait`，第 21 次咬死（hp 100→0，每咬 5）→ `status.phase` 与 `turn.phase` 均为 `"game_over"` ✓；`turn.player` 按 §2.5「不 despawn」语义留场，快照 `hp:[0,100]`（非 null，设计如此）；HUD 提示、move/wait 拒绝均已验（审查 M9 后补验，覆盖此前单测未触达的 wire 序列化路径）

## 7. 遗留与边界

- 玩家攻击/技能输入、能量/武器/公式/暴击、Buff、趁手打击、击退：后续里程碑按需扩 RuleEngine（结构已预留 abilities/effects 表与冷却）；
- 战斗原型表仅 goblin：rat/orc 等随玩法扩展登记；惰性实体（coin 等）永远不参与 AI/战斗，除非入表；
- `emitTo`/`emitToMany`/child bus：无消费者不做；
- 逐实体视野属性（computed.fieldOfView）：现用常量 VISION_RANGE=5（原版同样硬编码）；
- 异步敌方回合（演出不阻塞结算）：现同步结算保持 M6 语义；
- 攻击/移动动画帧（tro-animations/Soldier 帧表）未消费：表现层后续接入 `tg::AnimationPlayer`；
- A* 切角 = 地形 + 战斗实体（惰性实体不参与），比 M6 `try_move`（地形 + 全部实体）窄、比原版 A*（纯地形）宽（§2.4）：被 coin 挡切角的斜步会被 `try_move` 拒绝、敌原地一站；仅影响绕路选择，如需逐字节对齐原版再收敛。
