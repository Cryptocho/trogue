# Trogue 项目概览

## 项目目标

**Trogue** 是一个基于 LÖVE2D 引擎的可扩展 2D 回合制游戏框架，支持通过 Mod Layer 实现无限玩法变体（Roguelike、RPG、刷宝等）。

---

## 架构分层

```
┌─────────────────────────────────────────┐
│              Mod Layer                  │
│  所有玩法逻辑、技能、物品、怪物都在这里       │
└─────────────────┬───────────────────────┘
┌─────────────────▼───────────────────────┐
│        Gameplay Rule Pipeline Layer     │
│    事件系统、能力系统、效果系统、回合系统     │
└─────────────────┬───────────────────────┘
┌─────────────────▼───────────────────────┐
│               ECS Layer                 │
│  实体管理、组件存储、基础系统、渲染系统       │
└─────────────────┬───────────────────────┘
┌─────────────────▼───────────────────────┐
│              Engine Layer               │
│  LÖVE2D、输入、渲染、资源                  │
└─────────────────────────────────────────┘
```

```plaintext
================================================================================
【 第一层：Mod Layer (插件与声明层) 】
================================================================================
    [ Mod A ]       [ Mod B ]       [ Mod C ]
       │               │               │
       └───────┬───────┴───────────────┘
               ▼
    通过 API 向 Gameplay 层注册:
    - New Components (新属性: 如 "反伤", "虚空护盾")
    - Pipeline Interceptors (逻辑钩子: 如 "在计算伤害前先执行我的逻辑")
    - Event Listeners (监听器: 如 "当玩家死亡时，发送一条全服通告")

================================================================================
【 第二层：Gameplay Rule Layer (规则流水线) 】
================================================================================
    [ Action Request ] (例如: 玩家发起一次攻击)
           │
           ▼
    ┌──────────────────────────────────────────────────────────────────────┐
    │  COMBAT PIPELINE (战斗流水线)                                         │
    │                                                                      │
    │  1. Check Phase      [Mod Interceptors] -> 判定是否致盲、是否有能量执行  │
    │  2. Pre-Exec Phase   [Mod Interceptors] -> 计算暴击率、触发潜行加成      │
    │  3. Execution Phase  [Mod Interceptors] -> 计算基础伤害、防御减免       │
    │  4. Post-Exec Phase  [Mod Interceptors] -> 触发吸血、触发反伤、触发特效  │
    └──────────────────┬───────────────────────────────────────────────────┘
                       │
                       ▼ 
    [ Game State Mutator ] (唯一有权修改 ECS 数据的组件)
                       │
                       └───── 发送信号给 Event Bus ──────┐
                                                       │
================================================================================
【 第三层：ECS / Data Layer (数据存储层) 】
================================================================================
    ┌──────────────────────────┐      ┌──────────────────────────┐
    │  Entities (实体ID)        │      │  Systems (系统)           │
    │  - Player (ID: 1)        │ <─── │  - AI System             │
    │  - Orc (ID: 99)          │      │  - Status Effect System  │
    └──────────┬───────────────┘      └──────────┬───────────────┘
               │                                 │
    ┌──────────▼─────────────────────────────────▼──────────────┐
    │  Components (纯数据块)                                     │
    │  - Position {x, y}                                        │
    │  - Health {current, max}                                  │
    │  - ModdedAttr {key, value}  <-- Mod 注入的数据放在这里      │
    └───────────────────────────────────────────────────────────┘

================================================================================
【 第四层：Engine Layer (表现与驱动层) 】
================================================================================
    [ Input ] ───> 转化为 Action 传给第二层
    
    [ Event Bus ] (解耦总线)
           │
           ├──> [ Renderer ] -> 播放攻击动画、飘字、抖屏
           ├──> [ Sound ]    -> 播放音效
           └──> [ UI ]       -> 更新血条显示

    [ Underlying Engine ] (LÖVE2D)
================================================================================
```

---

## 当前状态

| 层级 | 状态 | 说明 |
|------|------|------|
| Engine Layer | ✅ 完整 | LÖVE2D 11.5，love-api 已获取 |
| ECS Layer | ✅ 完整 | 纯表实现，含 World/EventBus/SpatialHash/Prototype |
| Gameplay Rule Pipeline Layer | ✅ MVP | RuleEngine 核心，能力/效果/Buff 系统 |
| Mod Layer | 🚧 规划 | 玩法/技能/物品/怪物扩展 |

---

## 技术栈与依赖

### 核心引擎
- **LÖVE2D** 11.x - 游戏引擎
- **Lua** 5.x - 脚本语言
- **love-api** (`./love-api/`) - API 元数据，用于 IDE 补全

### 源码结构
```
src/
├── main.lua              # LÖVE 入口，委托模式
├── config.lua             # 游戏配置
├── conf.lua               # LÖVE 配置
├── core/
│   ├── ecs.lua           # World 类（spawn/despawn/query）
│   ├── events.lua        # EventBus（priority/dirty flag 优化）
│   └── rule_engine.lua   # 规则引擎（能力/效果/Buff 处理）
├── components/
│   ├── position.lua      # 位置 {x, y}
│   ├── health.lua        # 生命值 {current, max}
│   ├── actor.lua        # 标记可行动实体（moveDelay）
│   ├── player.lua       # 玩家标记
│   ├── ability.lua      # 能力集（Set 结构）、冷却、资源
│   ├── buffs.lua        # Buff 列表管理
│   ├── renderable.lua   # 渲染信息（tileIndex）
│   ├── solid.lua        # 固体标记
│   └── effect_tile.lua  # 动态效果（毒/火/冰）
├── systems/
│   ├── input.lua        # 统一输入处理（WASD/热键）
│   ├── movement.lua     # 移动系统
│   ├── combat.lua       # 战斗系统（碰撞伤害）
│   ├── ai.lua           # AI 系统（随机移动）
│   ├── turn.lua         # 回合系统
│   ├── render.lua       # 渲染系统（地图/实体/血条）
│   └── map_renderer.lua # 地图渲染
├── data/
│   ├── definitions/
│   │   ├── ability.lua  # 能力定义（punch/heal/shield/fireball）
│   │   ├── effect.lua  # 效果定义（damage/heal/buff）
│   │   └── buff.lua    # Buff 定义（shield/burning/strength）
│   └── prototypes/
│       └── entities.lua # 实体原型（player/rat/goblin/orc）
├── utils/
│   └── prototype.lua    # 原型管理器（深拷贝/覆盖）
└── assets/
    ├── tileset_info.lua # 瓦片映射
    └── tileset.png      # 瓦片图
```

---

## 开发指南

### 运行 Demo
```bash
love src
```

### 核心文件
| 文件 | 职责 |
|------|------|
| `src/core/ecs.lua` | ECS World 管理，SpatialHash 空间查询 |
| `src/core/events.lua` | EventBus（优先级/dirty flag 优化）|
| `src/core/rule_engine.lua` | 规则引擎（能力/效果/Buff 处理） |
| `src/systems/*.lua` | 系统实现（Input/Movement/Combat/AI/Turn/Render） |
| `src/data/definitions/*.lua` | 能力/效果/Buff 定义 |
| `src/data/prototypes/entities.lua` | 实体原型（player/rat/goblin/orc） |
| `src/utils/prototype.lua` | 原型管理器（深拷贝/覆盖） |

---

## 核心游戏系统（RuleEngine）

### 架构

RuleEngine 是 Gameplay Rule Pipeline Layer 的核心，通过事件驱动处理所有战斗逻辑：

```
[Action Request] → AbilityUse → RuleEngine:tryUseAbility()
                                    ↓
                           ┌─────────────────────────────────────┐
                           │  1. canUse() 检查 (冷却/资源/范围)  │
                           │  2. getValidTargets() 获取有效目标  │
                           │  3. 发射 DamageRequest/HealRequest │
                           │  4. 监听并处理响应                  │
                           └─────────────────────────────────────┘
```

### 事件模式

- **Request → Process**: UI 发射 AbilityUse，RuleEngine 发射 DamageRequest 并处理
- **集中处理**: 每种效果类型只有一个处理器，避免重复逻辑
- **Buff 生命周期**: BuffTickRequest 处理 DOT/HOT，BuffApplyRequest 处理应用

### 内置能力/效果/Buff

| 类型 | 内置定义 |
|------|----------|
| Abilities | punch, heal, shield, fireball |
| Effects | damage_physical, damage_fire, heal_minor, buff_shield, burn |
| Buffs | shield, burning, strength |

## 开发流程

1. 给出计划等待批准
2. 实现计划
3. 进行测试(如需要)
4. 更新CHANGELOG.md
5. 写commit message等待确认
6. 确认后提交所有变更(包括和这次计划无关的)并推送

---

## 编码风格规范

### 概述

所有代码统一为**纯数据驱动**风格，消除 OOP metatable 模式，保持组件/定义/原型为纯数据表。

### 规则

#### 1. 组件（Components）

必须是纯数据表，不使用 metatable/OOP：

```lua
-- 正确
local Health = { current = 100, max = 100 }

-- 错误 - 使用了 setmetatable
local Health = {}
setmetatable(Health, { __index = ... })
```

#### 2. 定义（Definitions）

工厂函数返回纯数据表，不使用 `setmetatable`：

```lua
-- 正确
local function createAbilityDef(id, config)
    return {
        id = id,
        name = config.name,
        cost = config.cost or {},
        cooldown = config.cooldown or 0,
    }
end

-- 错误
local Ability = {}
setmetatable(Ability, { __index = ... })
function Ability:new() ... end
```

#### 3. 原型（Prototypes）

纯数据表，不使用 OOP 模式。

#### 4. 核心类（Core Classes）

使用工厂函数而非 `:new()` 构造和 `__index` metatable：

```lua
-- 正确
local function createWorld()
    return {
        nextEntityId = 1,
        entities = {},
        components = {},
    }
end

-- 错误
local World = {}
World.__index = World
function World:new()
    return setmetatable({}, World)
end
```

#### 5. 系统（Systems）

统一使用 `function SystemName:methodName(self, ...)` 方法简写语法：

```lua
-- 正确
local MySystem = {
    priority = 1,
    name = "MySystem",
}

function MySystem:init(world)
    self.world = world
end

function MySystem:update(world, dt)
    -- ...
end

-- 错误 - 使用内联函数
local MySystem = {
    priority = 1,
    name = "MySystem",
    init = function(self, world)
        self.world = world
    end,
}
```

#### 6. Coordinates 模块

使用纯函数，不使用 `self` 和 OOP 方法语法：

```lua
-- 正确
local TILE_SIZE = 16

local function tileToWorld(tx, ty)
    return (tx - 1) * TILE_SIZE, (ty - 1) * TILE_SIZE
end

-- 错误
local Coordinates = { TILE_SIZE = 16 }
function Coordinates:tileToWorld(tx, ty)
    return (tx - 1) * self.TILE_SIZE, (ty - 1) * self.TILE_SIZE
end
```

#### 7. 禁止模式

`setmetatable(obj, some_metatable)` + `__index` 的 OOP 模式严格禁止使用。

#### 8. 回复风格

回复用户时总是使用中文，禁止使用mermaid