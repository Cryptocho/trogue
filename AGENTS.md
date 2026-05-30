# Trogue 项目概览

## 项目目标

**Trogue** 是一个基于 LÖVE2D 引擎的古典 Roguelike 游戏。

---

## 架构分层

```
┌─────────────────────────────────────────┐
│         Gameplay Layer (玩法层)          │
│  能力定义、效果定义、Buff 定义、实体原型     │
│  关卡生成、AI 行为、技能系统              │
└─────────────────┬───────────────────────┘
┌─────────────────▼───────────────────────┐
│        Rule Pipeline Layer (规则层)      │
│  RuleEngine: 事件驱动能力/效果/Buff 处理   │
│  战斗流水线: Check → Pre → Exec → Post   │
└─────────────────┬───────────────────────┘
┌─────────────────▼───────────────────────┐
│               ECS Layer                 │
│  World/Entity/SpatialHash/EventBus      │
│  纯数据组件 (Stats/Position/Actor/...)   │
│  系统 (Input/Movement/Combat/AI/Turn/   │
│        Render/MapRenderer)              │
└─────────────────┬───────────────────────┘
┌─────────────────▼───────────────────────┐
│              Engine Layer               │
│  LÖVE2D、输入、渲染、资源                 │
└─────────────────────────────────────────┘
```

### Gameplay Layer (玩法层)

所有 Roguelike 玩法内容集中于此层：能力定义、效果定义、Buff 定义、实体原型、关卡生成。

通过 `src/data/definitions/` 声明式定义游戏内容，通过 `src/data/prototypes/` 定义实体模板。

### Rule Pipeline Layer (规则层)

RuleEngine 是核心，通过事件驱动处理战斗逻辑：

```
[Action Request] → RuleEngine:tryUseAbility()
    ├── canUse() 检查 (冷却/资源/范围)
    ├── getValidTargets() 获取有效目标
    ├── 发射 DamageRequest/HealRequest → 效果处理
    └── 发射 AbilityUsed/AbilityUseFailed 事件 → UI 更新
```

事件模式：
- **Request → Process**: UI 发射 AbilityUse，RuleEngine 发射 DamageRequest 并处理
- **集中处理**: 每种效果类型只有一个处理器
- **Buff 生命周期**: BuffTickRequest 处理 DOT/HOT，BuffApplyRequest 处理应用

---

## 当前状态

| 层级 | 状态 | 说明 |
|------|------|------|
| Engine Layer | ✅ 完整 | LÖVE2D 11.x |
| ECS Layer | ✅ 完整 | 纯表实现，含 World/EventBus/SpatialHash/Prototype |
| Rule Pipeline Layer | ✅ MVP | RuleEngine 核心，能力/效果/Buff 系统 |
| Gameplay Layer | 🚧 开发中 | 程序化地图生成、更多能力/敌人/物品 |

---

## 技术栈与依赖

- **LÖVE2D** 11.x - 游戏引擎
- **Lua** 5.x - 脚本语言
- **love-api** (`./love-api/`) - API 元数据，用于 IDE 补全

---

## 源码结构

```
src/
├── main.lua              # LÖVE 入口，委托模式
├── config.lua             # 全局常量 (TILE_SIZE=16, SCALE=2)
├── conf.lua               # LÖVE 窗口配置
├── core/
│   ├── ecs.lua           # World 类 (spawn/despawn/query)，SpatialHash 空间查询
│   ├── events.lua        # EventBus (priority/dirty flag 优化)
│   ├── rule_engine.lua   # 规则引擎 (能力/效果/Buff 处理)
│   └── coordinates.lua   # 坐标系统 (tile↔world↔screen 转换，距离计算，A* 寻路)
├── components/
│   ├── position.lua      # 位置 {x, y}
│   ├── stats.lua         # 属性 {base, current, max, computed, modifiers} (hp/energy/strength/...)
│   ├── actor.lua        # 标记可行动实体 (moveDelay)
│   ├── player.lua       # 玩家标记
│   ├── ability.lua      # 能力集 (Set 结构)、冷却、资源
│   ├── buffs.lua        # Buff 列表管理
│   ├── renderable.lua   # 渲染信息 (tileIndex)
│   ├── solid.lua        # 固体标记 (不可通行)
│   └── effect_tile.lua  # 地面效果 (毒/火/冰)
├── systems/
│   ├── input.lua        # 统一输入处理 (WASD+斜向/点击移动+A*/数字键选技能)
│   ├── movement.lua     # 移动系统 (碰撞检测)
│   ├── combat.lua       # 战斗系统 (碰撞伤害→发射 DamageRequest)
│   ├── ai.lua           # AI 系统 (使用 RuleEngine 释放技能)
│   ├── turn.lua         # 回合系统 (PlayerTurnEnd → 敌人行动 → TurnEnd)
│   ├── render.lua       # 渲染系统 (实体绘制/血条)
│   └── map_renderer.lua # 地图渲染 (从 2D tile 数组渲染)
├── data/
│   ├── definitions/
│   │   ├── ability.lua  # 能力定义 (punch/heal/shield/fireball)
│   │   ├── effect.lua  # 效果定义 (damage/heal/buff)
│   │   └── buff.lua    # Buff 定义 (shield/burning/strength)
│   └── prototypes/
│       └── entities.lua # 实体原型 (player/goblin/rat/orc/poison_pool/fire_pool)
├── utils/
│   ├── prototype.lua    # 原型管理器 (深拷贝/覆盖)
│   ├── map_generator.lua # 程序化地图生成器 (Perlin+FBM+Poisson → forest)
│   ├── perlin.lua       # Perlin 噪声
│   ├── fbm.lua          # Fractional Brownian Motion
│   └── poisson_disk.lua # Poisson 圆盘采样
├── assets/
│   ├── tileset_info.lua # 瓦片映射
│   ├── tileset.png      # 瓦片图
│   ├── defend.png       # shield 图标
│   ├── fireball.png     # fireball 图标
│   ├── heal.png         # heal 图标
│   └── hit.png          # punch 图标
tests/
├── test_ecs.lua         # ECS 核心测试
├── test_events.lua      # EventBus 测试
├── test_prototype.lua   # 原型管理器测试
├── test_component.lua   # 组件测试
└── test_system.lua      # 系统测试 (可能已过期)
tools/
└── generate_tileset.py  # 瓦片图生成脚本
```

---

## 开发命令

### 运行游戏
```bash
love src
```

### 运行测试(对于需要启动love进行的测试必需给出详细步骤指导用户进行而不是自己运行love src)
```bash
lua tests/test_ecs.lua
lua tests/test_events.lua
lua tests/test_prototype.lua
```

### 开发流程
1. 给出计划等待批准 (当前处于unreleased阶段所以可以大胆地进行计划,包括架构上的更改建议等)
2. 实现计划
3. Review Uncommitted Changes
4. 在review之后或用户要求时, 更新 CHANGELOG.md
5. 询问用户是否写 commit message ,如果是则给出 commit message(英文) 等待用户确认
6. 确认后提交所有变更并推送

### CHANGELOG 格式规范
在 `## [Unreleased]` 下按功能模块组织变更，每个模块使用 `### 功能描述` 标题。

必填字段：`- 影响的文件:` 列出所有变更文件路径（用反引号包裹）。新的修改写在最前面。

常用子标题：
- `### Added` — 新增功能/文件
- `### Refactored` — 重构
- `### Bug Fixes` — 修复
- `### Architecture` — 架构决策
- `### Breaking Changes` — 破坏性变更

示例：
```markdown
### 坐标系统提取

- 影响的文件: `src/core/coordinates.lua` (新建), `src/systems/input.lua`
- 新建 `Coordinates` 模块，统一坐标计算逻辑
- 消除距离计算重复代码（曼哈顿距离分散在 4 处）
```

---

## 编码规范

### 组件：纯数据表，无 metatable/OOP
```lua
-- 正确
local Stats = { base = {}, current = {hp=10}, max = {hp=10} }

-- 错误
local Health = setmetatable({}, {__index = ...})
```

### 定义：工厂函数返回纯数据表
```lua
local function createAbilityDef(id, config)
    return { id = id, name = config.name, cost = config.cost or {}, cooldown = config.cooldown or 0 }
end
```

### 核心类：使用工厂函数，非 `:new()` + `__index`
```lua
-- 正确
local function createWorld() return { nextEntityId = 1, entities = {}, components = {} } end

-- 错误
local World = {}; World.__index = World; function World:new() ... end
```

### 系统：使用 `function SystemName:methodName(self, ...)` 语法
```lua
local MySystem = { priority = 1, name = "MySystem" }
function MySystem:init(world) self.world = world end
function MySystem:update(world, dt) -- ...
```

### Coordinates 模块：纯函数，无 `self`
```lua
local function tileToWorld(tx, ty) return (tx - 1) * TILE_SIZE, (ty - 1) * TILE_SIZE end
```

### 严格禁止
- `setmetatable(obj, mt)` + `__index` OOP 模式
- 组件中写业务逻辑 (逻辑必须在 System 中)

### 坐标系统注意
- `config.lua` 定义 `Config.TILE_SIZE = 16`，这是权威值
- `coordinates.lua` 内部硬编码 `TILE_SIZE = 16`，两者需保持一致
- 瓦片坐标是 1-based (与 Lua 表索引一致)

### 回复风格
回复用户时总是使用中文，禁止使用 mermaid。