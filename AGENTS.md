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
│   ├── effect_tile.lua  # 地面效果 (毒/火/冰)
│   ├── weapon.lua       # 武器属性 {type, baseDamage, armorPenetration, physicalDamageBonus}
│   └── position_tween.lua # 移动动画插值状态
├── systems/
│   ├── input.lua        # 统一输入处理 (WASD+斜向/点击移动+A*/数字键选技能)
│   ├── movement.lua     # 移动系统 (碰撞检测)
│   ├── combat.lua       # 战斗系统 (碰撞伤害→发射 DamageRequest)
│   ├── ai.lua           # AI 系统 (使用 RuleEngine 释放技能)
│   ├── turn.lua         # 回合系统 (PlayerTurnEnd → 敌人行动 → TurnEnd)
│   ├── render.lua       # 渲染系统 (实体绘制/血条)
│   ├── map_renderer.lua # 地图渲染 (从 2D tile 数组渲染)
│   ├── weapon_system.lua # 武器属性查询
│   └── tween_system.lua  # 移动动画插值 (outQuad easing)
├── data/
│   ├── definitions/
│   │   ├── ability.lua  # 能力定义 (punch/heal/shield/fireball/passive_strength)
│   │   ├── effect.lua  # 效果定义 (damage/heal/buff)
│   │   └── buff.lua    # Buff 定义 (shield/burning/strength)
│   └── prototypes/
│       └── entities.lua # 实体原型 (player/goblin/rat/orc/poison_pool/fire_pool)
├── utils/
│   ├── prototype.lua    # 原型管理器 (深拷贝/覆盖)
│   ├── map_generator.lua # 程序化地图生成器 (Perlin+FBM+Poisson → forest)
│   ├── perlin.lua       # Perlin 噪声
│   ├── fbm.lua          # Fractional Brownian Motion
│   ├── poisson_disk.lua # Poisson 圆盘采样
│   ├── minheap.lua      # 最小堆 (A* 寻路用)
│   └── tween.lua        # kikito/tween.lua (保留但未直接使用)
├── assets/              # 游戏资源 (瓦片图、图标、音效等)
│
tools/                   # 辅助开发工具脚本
```

---

## 回合流程

```
玩家输入 (WASD/点击/技能键)
  → InputSystem 检查 turnSystem:isInputAllowed()
  → [移动] 发射 MoveAttempt(isPlayer=true)
       → TurnSystem: inputAllowed=false
       → MovementSystem: 检查碰撞
           → 碰撞: 发射 CollisionDetected(isPlayer=true) → TurnSystem: inputAllowed=true (不消耗回合)
           → 成功: setComponent Position → startTween → 发射 MoveSucceeded(isPlayer=true)
                → TurnSystem: endPlayerTurn() → 发射 PlayerTurnEnd
  → [技能] 进入瞄准模式 → 左键点击目标
       → 发射 AbilityUse(entity, abilityId, targetX, targetY)
       → RuleEngine: tryUseAbility → applyEffects → 发射 AbilityUsed
       → InputSystem 监听器: 如果玩家使用了能力 → 发射 PlayerTurnEnd
  → [空格] 跳过回合：直接调用 turnSystem:startTurn() + endPlayerTurn()

PlayerTurnEnd
  → TurnSystem: inputAllowed=false
  → AISystem: 进入 AI 回合
  → AISystem: 对每个 AI 实体:
       → tryUseAbility (如果在玩家 2 格内，随机选择可用技能)
       → 70% 概率随机 4 方向移动 (发射 MoveAttempt) -- 临时mock
  → 所有敌人处理完毕后 → 发射 TurnEnd

TurnEnd (priority=100)
  → TurnSystem: inputAllowed=true, currentPhase="player", turnCount++
  → RuleEngine: reduceCooldowns (所有实体冷却 -1) + processBuffTicks (DOT/HOT 触发)
```

详见 [回合流程](#14-回合流程完整) 和 [事件类型表](#15-事件类型与流动)。

---

## 开发命令

### 运行游戏
```bash
love src
```

### 运行测试
对于需要启动love进行的测试必需给出详细步骤指导用户进行而不是自己运行love src

### 开发流程
1. 给出计划等待批准 (当前处于unreleased阶段所以可以大胆地进行计划,包括架构上的更改建议等)
2. 实现计划
3. 检查未提交代码是否合理,优雅,风格统一,无逻辑问题
4. 在检查之后或用户要求时, 更新 CHANGELOG.md, 检查之前禁止修改CHANGELOG.md
5. 检查是否需要更新AGENTS.md
6. 询问用户是否写 commit message ,如果是则给出 commit message(英文) 预览等待用户确认,禁止直接提交
7. 确认后提交所有变更并推送

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

---

## 配置常量

| 常量 | 值 | 位置 | 说明 |
|------|-----|------|------|
| `TILE_SIZE` | 16 | `src/config.lua` | 像素每 tile |
| `SCALE` | 2 | `src/config.lua` | 窗口缩放因子 |
| `TILES_PER_ROW` | 8 | `src/config.lua` | 瓦片图布局 (每行 8 个 tile) |
| `MOVE_DURATION` | 0.12 | `src/config.lua` | 移动动画时长 (秒) |
| `window.width` | 640 | `src/conf.lua` | 窗口宽度 |
| `window.height` | 480 | `src/conf.lua` | 窗口高度 |
| `window.resizable` | true | `src/conf.lua` | 可调整大小 |
| `window.minwidth/minheight` | 320/240 | `src/conf.lua` | 最小窗口尺寸 |
| `modules.audio` | false | `src/conf.lua` | 禁用音频 |

---

## 核心 API 参考

### World (`src/core/ecs.lua`)

```lua
local world = createWorld()
-- 实体管理
local entityId = world:spawn(components)              -- 创建实体，返回 ID
world:despawn(entityId, reason)                       -- 标记销毁 (延迟)
local list = world:processDespawns()                  -- 批量销毁，返回 {{id, reason},...}

-- 查询
local results = world:query({"Position", "Stats"})    -- 返回只读代理数组
local results = world:query({"Position"}, {includeDead=true})  -- 包含待销毁实体
local comp = world:getComponent(entityId, "Position") -- 获取组件
world:setComponent(entityId, "Position", data)        -- 设置组件 (自动更新 SpatialHash)
world:addComponent(entityId, "Solid", data)           -- 添加组件 (Position 自动注册)
world:removeComponent(entityId, "Solid")              -- 移除组件 (Position 自动注销)
local ok = world:hasComponent(entityId, "Position")   -- 检查组件存在

-- 系统管理
world:addSystem(system)                               -- 按 priority 插入排序，调用 init(world)
world:update(dt)                                      -- 按 priority 顺序调用各系统 update
local sys = world:getSystem("InputSystem")            -- 懒缓存查找 (首次 O(n), 后续 O(1))

-- 工具
local sh = world:getSpatialHash()                     -- 获取 SpatialHash 实例
local storage = world:getComponentStorage("Position") -- 获取组件存储表 (内部用)
```

查询结果使用只读代理 (metatable `__newindex=error`)，防止意外修改。在 ECS 层这是**唯一使用 metatable 的地方**。

### EventBus (`src/core/events.lua`)

```lua
local bus = createEventBus()
-- 注册/注销
local unsubscribe = bus:on("DamageRequest", handler, priority, targetId)  -- priority 越小越先执行
bus:off("DamageRequest", index)                      -- 按索引移除

-- 发射
bus:emit("TurnEnd", data)                            -- 广播给所有监听者
bus:emitTo(targetId, "DamageDealt", data)            -- 定向事件 (只发给匹配 targetId 的 handler)
bus:emitToMany({id1, id2}, "HealRequest", data)      -- 多目标事件

-- 工具
bus:clear() / bus:clearEvent("DamageRequest")
local n = bus:count("DamageRequest")
local childBus = bus:child()                         -- 创建子 EventBus (emit 同时调用自身和 parent)
```

Dirty flag 优化：`on()`/`off()` 设置 dirty 标记为 O(1)，`emit()` 时延迟重建排序列表。

### SpatialHash (`src/core/ecs.lua`)

```lua
sh:insert(entityId, x, y)
sh:remove(entityId, x, y)
sh:move(entityId, oldX, oldY, newX, newY)
local entities = sh:getAt(x, y, componentFilter, getComponentStorage)  -- 获取该位置的实体
local neighbors = sh:getNeighbors(x, y, radius)                         -- 获取附近实体
sh:clear()
```

World 的 `setComponent`/`addComponent`/`removeComponent` 对 Position 组件会自动同步 SpatialHash。

### Coordinates (`src/core/coordinates.lua`)

```lua
-- 坐标转换
local wx, wy = Coordinates.tileToWorld(tx, ty)           -- tile → 世界像素
local tx, ty = Coordinates.worldToTile(wx, wy)           -- 世界像素 → tile
local tx, ty = Coordinates.screenToTile(sx, sy, camX, camY, sw, sh, scale)
local px, py = Coordinates.tileToScreen(tx, ty, camX, camY, sw, sh, scale)

-- 距离计算
local d = Coordinates.manhattanDistance(x1, y1, x2, y2)  -- |dx| + |dy|
local d = Coordinates.chebyshevDistance(x1, y1, x2, y2)  -- max(|dx|, |dy|)
local d = Coordinates.euclideanDistance(x1, y1, x2, y2)  -- sqrt(dx² + dy²)
local inRange = Coordinates.isInRange(x1, y1, x2, y2, range)  -- 曼哈顿距离

-- 寻路 & 视野
local path = Coordinates.findPath(sx, sy, gx, gy, isPassable, getBlockingEntity)  -- A*, 返回 {x,y} 数组或 nil
local visible = Coordinates.hasLineOfSight(x1, y1, x2, y2, isSolid)  -- Bresenham 直线

-- 工具
local ok = Coordinates.isInBounds(tx, ty, mapW, mapH)   -- 1-based 边界检查
local neighbors = Coordinates.getNeighbors(tx, ty, mapW, mapH, diagonal)
```

---

## 组件完整定义

### 1. Position (`src/components/position.lua`)
```lua
{ x = tileX, y = tileY }  -- 1-based tile 坐标
```
World 的 setComponent/addComponent/removeComponent 会自动同步 SpatialHash。

### 2. Stats (`src/components/stats.lua`)
```lua
{
    base = { strength, agility, sensing, spirit, magic, tenacity },
    current = { hp, energy },
    max = { hp, energy },
    computed = {
        physicalDamageBonus=0, blockChance=0, blockPower=0,
        dodge=0, hitRate=0, handsPower=0,
        critChance=0.05, critMultiplier=1.5, fieldOfView=8,
        sanPower=0, naturalResistance=0, cooling=0,
        magicPower=0, magicDownFloat=0, magicCooling=0,
        magicUpFloat=0, counterChance=0, magicResistance=0,
        darkResistance=0, heroicChance=0, damageAbsorb=0,
        armorPenetration=0, damageReduction=0
    },
    modifiers = {},           -- {[buffId] = {computedField = value}}
    _baseComputed = {},       -- 内部快照 (computed 重算基准)
}
```

### 3. Actor (`src/components/actor.lua`)
```lua
{}  -- 纯标记组件。存在即表示该实体可行动 (用于 AI 目标选择、MovementSystem 碰撞检测)
```

### 4. Player (`src/components/player.lua`)
```lua
{ level, currentXP, nextLevelXP, attributePoints, skillPoints }
```

### 5. Ability (`src/components/ability.lua`)
```lua
{
    abilities = { [abilityId] = true, ... },  -- Set 结构，O(1) 查找
    cooldowns = { [abilityId] = remainingTurns, ... },
    energyRegen = 0,        -- 每回合能量恢复 (未使用)
    regenCounter = 0,
}
```

### 6. Buffs (`src/components/buffs.lua`)
```lua
{
    activeBuffs = {
        [buffId] = {
            id = buffId,
            duration = remainingTurns,
            stacks = currentStacks,
            source = sourceEntityId,    -- 谁施加的
            definition = buffDef,       -- BuffDefinition 引用
            permanent = false,
        },
        ...
    }
}
```

### 7. Renderable (`src/components/renderable.lua`)
```lua
{ tileIndex = 0 }  -- 通过原型覆盖传入，用于 enemy 渲染时从 tileset 选取 quad
```

### 8. Solid (`src/components/solid.lua`)
```lua
{}  -- 标记组件。存在即不可通行。
```

### 9. EffectTile (`src/components/effect_tile.lua`)
```lua
{ effectType, damage, duration, spreadChance, tickRate, owner }
```
预留组件，尚未在系统中使用。

### 10. Weapon (`src/components/weapon.lua`)
```lua
{
    weaponId   = "greatsword",  -- 指向武器定义 (weapon.lua)
    weaponType = "melee",        -- "melee" | "ranged" | "magic"
    baseDamage, armorPenetration, physicalDamageBonus,
    critChance, hitRate, staggerRate, stunRate, knockbackRate,
    immobilizeRate, critDamageBonus, blockChance, blockPower,
    bleedChance, enchantDamage, limbDamage, magicDamage,
}
```
 WeaponSystem 负责合并定义默认值 + 组件实例覆盖值（`getResolvedStats(entityId)`）。

### 11. PositionTween (`src/components/position_tween.lua`)
```lua
{ active, startX, startY, targetX, targetY, visualX, visualY, clock }
```
由 TweenSystem 管理插值。RenderSystem 优先使用 visualX/visualY 实现平滑移动。

### 内部组件

- **ShouldDespawn** (`{reason}`)：`world:despawn()` 添加此标记，`processDespawns()` 批量移除。

---

## 系统优先级与职责

| Priority | 系统 | 文件 | 职责 |
|----------|------|------|------|
| 0 | **TweenSystem** | `tween_system.lua` | 每帧更新 PositionTween，outQuad 缓动 |
| 0 | **MapRenderer** | `map_renderer.lua` | 渲染地图 tile，isSolid() 查询 |
| 0 | **TurnSystem** | `turn.lua` | 回合状态机 (inputAllowed + currentPhase) |
| 0 | **InputSystem** | `input.lua` | 键盘/鼠标输入，A* 寻路，技能瞄准 |
| 1 | **MovementSystem** | `movement.lua` | 碰撞检测 + 位置更新 + tween 启动 |
| 2 | **CombatSystem** | `combat.lua` | **空实现** (占位，业务逻辑在 RuleEngine) |
| 3 | **AISystem** | `ai.lua` | 敌方 AI (PlayerTurnEnd → 行动 → TurnEnd) |
| 4 | **RenderSystem** | `render.lua` | 实体绘制 + 血条 + 瞄准预览 |
| 5 | **WeaponSystem** | `weapon_system.lua` | Weapon 组件查询 (getBaseDamage 等) |

注册顺序 (main.lua): MapRenderer → TurnSystem → TweenSystem → MovementSystem → CombatSystem → WeaponSystem → InputSystem → AISystem → RenderSystem

### 系统间依赖注入

系统通过 setter 方法建立依赖 (而非直接字段赋值)：
```lua
inputSystem:setTurnSystem(turnSystem)
inputSystem:setRuleEngine(ruleEngine)
aiSystem:setRuleEngine(ruleEngine)
mapRenderer:setWorld(world)
renderSystem:setInputSystem(inputSystem)
renderSystem:setRuleEngine(ruleEngine)
```

---

## 能力/效果/Buff 定义结构

### AbilityDefinition (`src/data/definitions/ability.lua`)
```lua
{
    id, name, description,
    mode = "activated" | "sustained" | "passive",
    cooldown = 0,              -- 回合数
    minCooldown = 0,
    cost = {},                 -- {energy = N, hp = N}
    targetType = "single" | "self" | "area" | "line" | "cone",
    range = 1,                 -- 遗留字段 (rangeFunc 优先)
    radius = 0,                -- 遗留字段
    effects = {},              -- [effectId, ...]
    icon = nil,                -- 技能栏图标路径
    castTime = 0,
    tags = {},
    passiveBuff = nil,         -- passive 模式的 buff ID
    rangeFunc = nil,           -- (sx, sy, tx, ty, mapW, mapH) → {{x,y},...}  瞄准范围（UI 预览）
    effectAreaFunc = nil,      -- (sx, sy, tx, ty, mapW, mapH) → {{x,y},...}  实际影响区域
}
```

### EffectDefinition (`src/data/definitions/effect.lua`)
```lua
{
    id, name, description,
    type = "damage" | "heal" | "buff" | "debuff" | "knockback",
    value = 0,                 -- 基础数值 (valueFormula 优先)
    valueScale = {},
    valueFormula = nil,        -- {basePercent, statScaling{{stat, multiplier}}, flatBonus}
    damageType = "physical" | "fire" | "ice" | "lightning" | "poison" | "arcane",
    buffId = nil,              -- type=buff/debuff 时指定 buff ID
    duration = 0,              -- type=buff/debuff 时指定持续回合
    tags = {},
    chance = nil,              -- 固定概率 0~1 (chanceFormula 优先)
    chanceFormula = nil,       -- {basePercent, statScaling{{stat, multiplier}}}
}
```

### BuffDefinition (`src/data/definitions/buff.lua`)
```lua
{
    id, name, description,
    type = "buff" | "debuff" | "dot" | "hot" | "shield",
    stackType = "replace" | "stack" | "refresh",
    maxStack = 1,
    statModifiers = {},         -- {computedField = value}
    tickEffect = nil,           -- 每回合触发的 effect ID (DOT/HOT)
    immunityTag = nil,
    icon = nil,
    color = {1, 1, 1, 1},
}
```

### 内置内容清单

**能力 (5 个)**: punch, heal, shield, fireball, passive_strength
**效果 (7 个)**: damage_physical, damage_fire, heal_minor, buff_shield, burn, burn_damage, opportunity_attack
**Buff (4 个)**: shield (SHIELD, damageAbsorb=10), burning (DOT, tickEffect=burn_damage), strength (BUFF, physicalDamageBonus=3), passive_strength_buff (BUFF, physicalDamageBonus=3)

---

## 关键设计模式

### 1. 延迟销毁 (Deferred Despawn)
`world:despawn(id, reason)` 只添加 ShouldDespawn 组件，不立即移除实体。`world:processDespawns()` 在回合结束时批量销毁，并返回被销毁实体列表供外部处理。

### 2. Dirty Flag 优化 (EventBus)
`bus:on()`/`bus:off()` O(1) 标记 dirty，`bus:emit()` 延迟重建排序列表。适合高频注册/低频发射的场景。

### 3. 懒缓存 (Lazy Cache)
`world:getSystem(name)` 首次遍历 O(n) 并缓存结果，后续调用 O(1)。RuleEngine 内部也懒初始化 `_weaponSystem`/`_mapRenderer`/`_inputSystem` 等引用。

### 4. Set-based 能力查找
`Ability.abilities` 是 `{[id]=true}` Set 结构，O(1) 检查能力是否存在。

### 5. 最小组件集查询
`world:query()` 选择最小数量的组件集进行迭代，减少遍历开销。

### 6. 只读代理
query 结果返回带 `__newindex=error` metatable 的代理表，防止系统代码意外修改组件数据。ECS 层唯一使用 metatable 的地方。

### 7. RangeFunc / EffectAreaFunc 分离
每个能力定义内联 `rangeFunc(sx, sy, tx, ty, mapW, mapH)` 和 `effectAreaFunc(sx, sy, tx, ty, mapW, mapH)`：
- `rangeFunc`：瞄准 UI 中显示的可选范围，用于鼠标悬停预览和点击合法性校验
- `effectAreaFunc`：技能实际释放时影响的 tile 集合，用于 `_ruleEngineApplyAbility` 确定目标
两者分离使瞄准范围（如 fireball 5 格施法距离）和效果范围（如 fireball 2 格爆炸半径）可以独立定义。

### 8. 公式求值系统
```
damage = weaponBaseDamage * basePercent
       + sum(stats.base[stat] * multiplier)  -- 每个 statScaling 条目
       + flatBonus
       + WeaponSystem:getPhysicalDamageBonus()
-- 结果 math.floor()
```

### 9. Computed 属性重算
修改 modifiers 后调用 `_ruleEngineRecalcComputed()`：重置 computed 到 `_baseComputed` 快照，遍历所有 modifiers 累加对应字段。不存在于 computed 中的字段会被跳过 (防御性)。

### 10. 事件驱动 Request → Process
Input/AI 发射请求事件 (AbilityUse/AbilityUseFailed)，RuleEngine 消费并发射处理事件 (DamageRequest/DamageDealt)。核心循环不包含直接函数调用。

### 11. 击退流水线 (Knockback Pipeline)
Knockback 效果通过 `KnockbackRequest` → `KnockbackApplied` 事件链处理：
- `_ruleEngineApplyEffect` 检测 KNOCKBACK 类型，发射 `KnockbackRequest`
- `_ruleEngineProcessKnockback` 处理击退：沿远离 source 方向逐格推进，遇墙壁/实体/边界停止
- 位移后同步更新 Position + SpatialHash + Tween 动画
- 发射 `KnockbackApplied` 事件通知 UI / 其他系统

### 12. 趁手打击流水线 (Opportunity Attack Pipeline)
玩家移出敌人邻格时，敌人概率性发动免费攻击：
- `MoveSucceeded` / `KnockbackApplied` → `MovementSystem` 检查旧位置邻格敌人是否不再相邻
- 满足条件则发射 `OpportunityAttack`（包含 attacker/target/fromX/Y/toX/Y）
- `RuleEngine` 监听 `OpportunityAttack`（priority=0），调用 `_ruleEngineApplyEffect("opportunity_attack", attacker, target)`
- 效果自带 `chance=0.5` 概率判定和 `valueFormula` 伤害公式，复用 `DamageRequest` → `DamageDealt` 完整管道
- 事件发生在 `PlayerTurnEnd` 之前，确保敌方回合开始前完成所有趁手打击判定

---

## 程序化地图生成管道

```
Perlin Noise → FBM (多倍频叠加) → 归一化到 [0,1] 密度图
    → PoissonDisk.sampleWithDensity (基于密度的采样)
    → 字符数组 ('.' = floor, '^' = tree/wall)
    → 敌人放置: 从距离中心 enemySpawnMinDist 外的地板 tile 中按 enemyDensity 随机选取,
      按权重分配类型 (goblin:5/14, rat:6/14, orc:3/14)
      返回 mapData + enemySpawns ({x,y,type} 数组)
```

Forest 地图参数 (main.lua):
```lua
MapGenerator.generateMap("forest", 60, 60, {
    treeMinDist = 2.0, densityThreshold = 0.5,
    fbmOctaves = 6, fbmPersistence = 0.5, fbmScale = 4.0,
    poissonMaxAttempts = 5, poissonSeed = nil,
    enemySpawnMinDist = 8, enemyDensity = 0.008
})
```

玩家生成在距离地图中心 (30,30) 最近的 floor tile。

---

## 测试状态

| 文件 | 状态 | 说明 |
|------|------|------|
| `test_ecs.lua` | ⚠️ 过期 | 使用旧的 OOP API (`World:new()` 而非 `createWorld()`) |
| `test_events.lua` | ⚠️ 过期 | 同上 |
| `test_prototype.lua` | ⚠️ 过期 | 多处 API 调用过期 (`.new()` 应为工厂函数) |
| `test_component.lua` | ❌ 损坏 | 引用已删除的 `src.ecs.component` |
| `test_system.lua` | ❌ 损坏 | 引用已删除的 `src.ecs.system` 和 `System:extend()` |

测试文件需要更新以匹配当前纯表工厂函数风格。当前无法直接运行。

---

## 已知注意点

1. **SpatialHash 自动同步**: `world:setComponent("Position", ...)` 会自动更新 SpatialHash，不需要手动管理
2. **Despawn 非即时**: `despawn()` 只是标记，需调用 `processDespawns()` 才真正销毁
3. **Query 只读**: `world:query()` 返回的是只读代理，不能直接修改结果中的组件数据
4. **Buff duration 语义**: duration=3 表示 4 次 tick (duration 3,2,1,0)，递减至 -1 时移除
5. **Permanent buff**: duration=-1 的 buff 不会被 tick 系统处理，不会自动移除
6. **被动能力**: mode=PASSIVE 的能力不通过 tryUseAbility，而是在实体生成后通过 `applyPassiveAbilities()` 施加 permanent buff
7. **能力消耗先于效果**: tryUseAbility 先扣除资源和设冷却，再执行效果。即使没有有效目标，资源也已消耗。
8. **TILE_SIZE 同步**: `config.lua` (Config.TILE_SIZE) 和 `coordinates.lua` (局部 TILE_SIZE) 都硬编码为 16，修改时需同步
9. **Component 添加**: `addComponent("Position")` 自动注册 SpatialHash；`removeComponent("Position")` 自动注销
10. **System enable/disable**: 设置 `system.enabled = false` 可跳过该系统的 update 调用