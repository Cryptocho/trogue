# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

ECS-based traditional roguelike with LÖVE2D

### 概率判定系统

- 影响的文件: `src/data/definitions/effect.lua`, `src/core/rule_engine.lua`, `src/systems/ai.lua`
- `EffectDefinition` 新增 `chance` / `chanceFormula` 可选字段，支持效果概率触发
- `_ruleEngineApplyEffect` 在 event emit 前插入概率判定位，`chanceFormula` 优先于 `chance`，两者未设置时保持 100% 向后兼容
- 新增 `_evaluateChanceFormula(entityId, formula)` 辅助函数：`basePercent + sum(stats.base[stat] * multiplier)`，cap 1.0，源实体无 StatsComponent 时退化为 `basePercent`
- `chanceFormula` 读取 `stats.base`（原始属性），不受 buff/modifier 影响
- 6 个内置效果定义（damage_physical/damage_fire/heal_minor/buff_shield/burn/burn_damage）保持 chance=nil 默认 100% 行为
- Code Review 修复：新增 `RuleEngine:getAbilityDef(abilityId)` 公开 getter，`AISystem:tryUseAbility` 通过该方法查询能力定义替代直接访问私有字段

### 精力值与MP统一为能量

- 影响的文件: `src/components/stats.lua`, `src/data/prototypes/entities.lua`, `src/data/definitions/ability.lua`, `src/main.lua`, `doc/Attributes_and_Grouth.md`, `doc/SkillTree/Two-handed_sword.md`, `AGENTS.md`, `temp.md`
- `StatsComponent.current/max` 中 `mp` + `stamina` 合并为 `energy`，移除死代码 `stamina`
- 4 个原型（player/goblin/rat/orc）的 Stats 统一迁移至 `energy` 字段
- 能力消耗 `cost = {mp = N}` 改为 `cost = {energy = N}`
- UI 渲染：MP 条改为 Energy 条，标签使用英文 `"Energy %d/%d"`
- 设计文档 `Attributes_and_Grouth.md` 中"精力值 (MP)"统一为"能量 (Energy)"
- 技能树文档 `Two-handed_sword.md` 中 mp/魔力/精力 统一为能量
- `rule_engine.lua` 使用通用 `pairs(ability.cost)` 迭代，无需修改

### 被动技能系统

- 影响的文件: `src/data/definitions/buff.lua`, `src/data/definitions/ability.lua`, `src/core/rule_engine.lua`, `src/data/prototypes/entities.lua`, `src/main.lua`, `src/systems/ai.lua`
- `_ruleEngineCanUse` 拒绝 `AbilityMode.PASSIVE` 模式的能力（被动技能无法手动释放）
- 新增 `_ruleEngineApplyPassiveAbilities(entityId)`：实体生成后扫描 PASSIVE 能力，通过内部 `BuffApplyRequest` 施加 `permanent=true` 的永久 Buff
- 新增 `_ruleEngineRemovePassiveAbilities(entityId)`：实体死亡前清理永久 Buff 及其 `stats.modifiers` 条目并重算 `computed`
- `_processBuffTicks` 跳过 `buffData.permanent` 为 true 的 Buff（不递减 duration、不移除、不发射 tick）
- `_processBuffApply` 创建和更新 Buff 时传播 `permanent` 标记
- `createAbilityDefinition` 新增 `passiveBuff` 字段，指向被动能力对应的 Buff ID
- 新增 `passive_strength` 被动能力 + `passive_strength_buff` 永久 Buff（`physicalDamageBonus = 3`），作为管道验证占位
- Player 原型 `Ability.abilities` 新增 `passive_strength = true`
- `AISystem:tryUseAbility` 过滤 `mode == "passive"` 的能力，避免 AI 尝试使用被动技能
- `main.lua` 玩家 spawn 后调用 `applyPassiveAbilities`，`EntityDied` 前调用 `removePassiveAbilities`

### statModifiers 管道与 Shield 去硬编码

- 影响的文件: `src/components/stats.lua`, `src/core/rule_engine.lua`
- `StatsComponent` 新增 `_baseComputed` 内部字段，由 `_recalcComputed` 首次调用时懒初始化
- 新增 `_recalcComputed(stats)` 纯函数：重置 computed 到基准值，遍历 `modifiers` 累加，`computed[field] ~= nil` 过滤非 computed 字段
- `_processBuffApply` 在 buff 应用后写入 `stats.modifiers[buffId]`，STACK 类型乘以 stacks 后再调用 `_recalcComputed`
- `_processBuffTicks` 在 buff 过期移除后清理 `modifiers[buffId]` 并重算
- `_processDamage` shield 吸收值从 `stats.modifiers["shield"].damageAbsorb` 读取，替代硬编码 `10`；破盾时同步清理 `modifiers` 并调用 `_recalcComputed`

### 武器系统占位与公式化伤害

- 影响的文件: `src/components/weapon.lua` (新建), `src/systems/weapon_system.lua` (新建), `src/components/stats.lua`, `src/data/definitions/effect.lua`, `src/data/prototypes/entities.lua`, `src/core/rule_engine.lua`, `src/main.lua`
- 新增 `WeaponComponent` 纯数据组件：type/baseDamage/armorPenetration/physicalDamageBonus，为装备系统预留占位
- 新增 `WeaponSystem` 占位系统（priority=5）：提供 getBaseDamage/getWeaponType/getArmorPenetration/getPhysicalDamageBonus 四个查询方法
- `StatsComponent` 扩展：base 新增 tenacity，current/max 新增 energy，computed 新增 counterChance/magicResistance/darkResistance/heroicChance/armorPenetration/damageReduction
- `EffectDefinition` 新增 `valueFormula` 可选字段，支持公式化动态伤害计算
- `RuleEngine._evaluateFormula()` 实现武器伤害公式：`weaponBaseDamage * basePercent + sum(statValue * multiplier) + flatBonus + weaponPhysicalDamageBonus`
- `RuleEngine._processDamage()` 在护盾吸收前执行公式求值，`DamageDealt.amount` 改为原始公式伤害值
- `RuleEngine._evaluateFormula()` / `_getArmorPenetration()` 委托 `WeaponSystem` 查询武器属性，消除重复代码
- 4 个原型（player/goblin/rat/orc）均添加 Weapon 组件（玩家大剑 baseDamage=20，敌人 fists baseDamage=2），Stats 同步扩展新字段
- `main.lua` 清理重复 RuleEngineModule require，注册 WeaponSystem

### 属性与玩家系统扩展

- 影响的文件: `src/components/stats.lua`, `src/components/player.lua`, `src/data/prototypes/entities.lua`, `doc/Attributes_and_Grouth.md`
- `StatsComponent.base` 从 2 属性扩展为 5 属性体系（strength/agility/sensing/spirit/magic），移除 intelligence/vitality
- `StatsComponent.computed` 从 4 字段扩展为 16 字段（新增 blockChance/blockPower/dodge/hitRate/handsPower/fieldOfView/sanPower/naturalResistance/cooling/magicPower/magicDownFloat/magicCooling/magicUpFloat），移除 poisonDurationMultiplier
- `PlayerComponent` 从空表标记扩展为进度数据（level/currentXP/nextLevelXP/attributePoints/skillPoints）
- 4 个原型（player/goblin/rat/orc）的 Stats 全部迁移至新属性体系，player HP 10→100 对齐 Lv1 设计值
- 设计文档 `Attributes_and_Grouth.md` 重命名（空格→下划线）

### 移动 tween 动画

- 影响的文件: `src/utils/tween.lua` (新建), `src/components/position_tween.lua` (新建), `src/systems/tween_system.lua` (新建), `src/config.lua`, `src/systems/movement.lua`, `src/systems/render.lua`, `src/main.lua`
- 引入 kikito/tween.lua v2.1.1（ outQuad + linear easing），实现实体移动平滑过渡
- 新增 `TweenSystem`（priority=0）：每帧对 active 实体做 outQuad 插值更新 `visualX/Y`
- `MovementSystem` 移动逻辑位置立即跳终点，同时调用 `TweenSystem:startTween()` 触发视觉插值
- `RenderSystem` 绘制实体和血条时优先读 `PositionTween.visualX/Y`，否则 fallback 到 `Position.x/y`
- `main.lua` 摄像头跟随改为优先读 `PositionTween.visualX/Y`，移动过程平滑跟随不再跳跃
- `config.lua` 新增 `MOVE_DURATION = 0.12`（单格移动完成时间，单位：秒）

### UI Update

- 影响的文件: `src/main.lua`, `src/systems/input.lua`
- 重构技能栏 UI:
  - 技能框改为小方块图标，横向排列于屏幕下方
  - 血条和能量条移至屏幕最下方居中显示，同一行排列
  - 冷却状态：图标上覆盖半透明黑色层并显示剩余回合数
  - 资源不足状态：图标上覆盖红色半透明层
  - 按键编号 `1`/`2`/`3`/`4` 移至技能框右下角
- 新增技能图标加载:
  - `hit.png` → punch
  - `heal.png` → heal
  - `defend.png` → shield
  - `fireball.png` → fireball
- InputSystem 按键 1-4 现在会设置 `game.selectedAbility` 标记选中技能

### 坐标系统提取

- 影响的文件: `src/core/coordinates.lua` (新建), `src/systems/map_renderer.lua`, `src/systems/render.lua`, `src/systems/ai.lua`, `src/systems/input.lua`, `src/core/rule_engine.lua`
- 新建 `Coordinates` 模块，统一坐标计算逻辑:
  - `tileToWorld()` / `worldToTile()` - 瓦片与像素世界坐标互转
  - `screenToTile()` / `tileToScreen()` - 屏幕像素与瓦片坐标互转（考虑相机偏移）
  - `manhattanDistance()` / `chebyshevDistance()` / `euclideanDistance()` - 三种距离计算
  - `isInRange()` / `isInArea()` - 范围判断
  - `getNeighbors()` - 获取相邻格子（支持8方向）
  - `findPath()` - 通用 A* 寻路算法
- 消除距离计算重复代码（曼哈顿距离分散在 movement/ai/rule_engine/input 4处）
- `MapRenderer:isSolid()` 改用 `Coordinates:isInBounds()` 做边界检查
- `input.lua` 的 A* 寻路委托给 `Coordinates:findPath()`，保留 `isPassable` / `getBlockingEntity` 回调接口

### 八向移动

- 影响的文件: `src/systems/input.lua`, `src/systems/movement.lua`
- 添加斜向移动
- 键盘组合移动: 0.18秒窗口期内连续按两个方向键，合并为斜向移动
  - 直接按斜向键立即执行，不走缓冲区
  - 缓冲区最多取前两个按键组合
- 鼠标点击移动: 支持直接斜向移动到相邻目标格
- A*寻路扩展: 支持八方向寻路

### 点击与寻路

- 影响的文件: `src\systems\input.lua`, `src\systems\movement.lua`, `src\main.lua`
- 添加鼠标点击移动功能: 点击屏幕任意位置移动到对应方格
- 添加 A* 寻路算法: 非相邻方格自动寻路接近目标
- 移动规则:
  - 目标方格是墙体或有事生存在上方时，无法移动
  - 相邻方格(曼哈顿距离=1)直接移动
  - 非相邻方格使用 A* 寻路，每次点击只移动一格
- 相关函数:
  - `InputSystem:handleClick(x, y)` - 处理鼠标点击
  - `InputSystem:findPath()` - A* 寻路实现
  - `InputSystem:getEntityAt()` - 检查位置是否有其他生物

### 加入跳过回合机制

- 影响的文件: `src\systems\input.lua`

### 组件规范化

#### Added

- `src/components/position.lua` - Position 组件定义，用于存储实体位置
- `src/components/health.lua` - Health 组件定义，用于存储生命值和存活状态
- Buffs 组件添加到所有实体原型 (player, goblin, rat, orc)

#### Bug Fixes

- 治疗技能改为 SELF 目标类型（heal → targetType = SELF）
- 玩家死亡后摄像机保持最后位置（保存 lastCameraX/Y）
- Fireball DOT 伤害从 3 改为 8
- 释放技能后正确进入下一个回合（监听 AbilityUsed 事件后触发 PlayerTurnEnd）
- Fireball 范围伤害不包含施法者自身（排除 sourceId）
- Punch 无目标时不消耗回合（applyAbility 返回 false 时不发 AbilityUsed）
- DOT/BuffTick 正确处理（先减 duration 再触发 tick）
- burn buff duration 从 2 改为 3（确保从下一回合开始结算）

#### Refactored

- `src/components/ability.lua` - 移除 `new()` 和实例方法，改为纯数据组件:
  - 方法逻辑移至对应 System（AbilitySystem/RuleEngine）
  - 符合 ECS "组件是数据、系统是逻辑" 原则

- `src/components/buffs.lua` - 移除 `new()` 和实例方法，改为纯数据组件:
  - 方法逻辑由 RuleEngine 实现

- `src/components/effect_tile.lua` - 移除工厂方法，改为纯数据组件:
  - 移除 createPoison/createFire/createIce 工厂方法
  - 特殊效果实体使用预定义原型

#### Architecture

- **统一组件风格**: 所有组件为纯数据（raw data）
- **无懒创建**: 组件必须预定义在原型中，系统不创建缺失的组件
- **数据与逻辑分离**: 组件只存储数据，业务逻辑在 System 中实现

### 输入系统重构

#### Refactored

- `src/systems/input.lua` - 完全重构为统一输入处理系统:
  - 添加能力热键处理（KEY_ABILITIES）
  - 提供 setTurnSystem() / setRuleEngine() 方法注入依赖
  - 移除重复的移动键位定义（移至 InputSystem 唯一定义）
  - handleKey() 统一处理移动和技能使用

- `src/main.lua` - 简化为委托模式:
  - love.keypressed 委托给 InputSystem:handleKey()
  - 删除 handleMove() / handleAbility() 方法（移至 InputSystem）
  - 添加 game:getSystem() 辅助方法
  - InputSystem 和 AISystem 通过 setter 接收 RuleEngine 引用

#### Architecture

- **单一输入入口**: 所有玩家输入（移动、技能）统一由 InputSystem 处理
- **委托模式**: main.lua 只负责初始化和渲染，业务逻辑委托给系统
- **依赖注入**: 系统间依赖通过 setter 方法传递，避免手动设置

### 组件优化

#### Refactored

- `src/components/ability.lua` - abilities 从数组改为 Set 结构:
  - 格式从 `{abilityId1, abilityId2, ...}` 改为 `{abilityId = true, ...}`
  - 新增 hasAbility() 方法实现 O(1) 查询
  - 新增 removeAbility() / getAllAbilities() 辅助方法
  - addAbility() 简化为 `abilities[abilityId] = true`

- `src/components/actor.lua` - 添加详细文档注释:
  - 说明组件用途（标记可执行动作的实体）
  - 说明 Player 不应拥有此组件
  - 添加 moveDelay 属性说明

- `src/data/prototypes/entities.lua` - abilities 改为 Set 格式:
  - `abilities = {punch = true, heal = true, ...}`

- `src/core/rule_engine.lua` - canUse() 优化:
  - 从数组遍历改为 Set 直接查询: `comp.abilities[abilityId]`
  - 查询复杂度从 O(n) 降为 O(1)

- `src/systems/ai.lua` - AISystem 优化:
  - 添加 setRuleEngine() 方法支持依赖注入
  - abilities 检查从 `#abilities == 0` 改为 `not next(abilities)`

### 数据迁移

#### Added

- `src/components/ability.lua` - AbilityComponent:
  - 存储实体的 abilities/cooldowns/resources
  - 提供 addAbility, setCooldown, getCooldown, consumeResource, restoreResource 方法

- `src/components/buffs.lua` - BuffsComponent:
  - 管理实体的 activeBuffs 列表
  - 提供 addBuff, getBuff, hasBuff, removeBuff, tick 方法

- `src/components/effect_tile.lua` - EffectTileComponent:
  - 动态效果实体（毒、火、冰等）
  - 包含 effectType, damage, duration, spreadChance 等字段
  - 提供 createPoison, createFire, createIce 工厂方法

#### Refactored

- `src/core/rule_engine.lua` - 迁移至 ECS 组件:
  - 删除 abilityComponents 内部缓存
  - 改用 `world.components.Ability[entityId]` 查询
  - 新增 `getOrCreateBuffsComponent()` 方法
  - `getAbilityComponent()` 改为纯懒创建模式

- `src/data/prototypes/entities.lua` - 原型预定义:
  - Player 原型添加 Ability 组件
  - 符合"组件在实体创建时预定义"原则

#### Architecture

- **数据位置统一**: 所有数据存储在 ECS 组件中，无外部缓存
- **组件预定义原则**: Ability/Buffs 组件在原型中定义，系统仅处理懒创建
- **RuleEngine 职责**: 规则判定与效果应用，通过事件驱动

### Core Features

- Pure Lua ECS core (World, Entity, Component, System)
- Spatial hash for O(1) spatial queries
- Event bus with priority-based handlers
- Turn-based combat system
- AI system with random movement
- Prototype-based entity spawning

### Architecture

- Encapsulated component access
- Read-only query results
- Event-driven turn management
- Pure data components

---

## 核心游戏系统（RuleEngine）

### Added

- `src/core/rule_engine.lua` - RuleEngine (core gameplay system):
  - `RuleEngine:new(world, events)` - Initialize with world and event bus
  - `canUse(entityId, abilityId)` - Check if ability can be used (cost, cooldown, range)
  - `tryUseAbility(entityId, abilityId, targetId)` - Use ability, emit Request events
  - `getValidTargets(entityId, abilityId)` - Get valid targets for ability
  - `getAbilityInfo(entityId, abilityId)` - Get ability info (cooldown, canUse, cost)
  - `getAbilityComponent(entityId)` - Get Ability component for entity
  - Private processors: `_processDamage()`, `_processHeal()`, `_processBuffApply()`, `_processBuffTick()`, `_checkDeath()`
  - Event handlers for DamageRequest, HealRequest, BuffApplyRequest, BuffTickRequest

- `src/data/definitions/ability.lua` - Ability definitions:
  - AbilityDefinition with Mode (ACTIVE/PASSIVE), TargetType (SELF/ENEMY/ALLY/AREA), EffectType
  - Built-in abilities: punch, heal, shield, fireball

- `src/data/definitions/effect.lua` - Effect definitions:
  - EffectDefinition with DAMAGE/HEAL/BUFF/DEBUFF types
  - Built-in effects: damage_physical, damage_fire, heal_minor, buff_shield, burn

- `src/data/definitions/buff.lua` - Buff definitions:
  - BuffDefinition with BUFF/DEBUFF/DOT/HOT/SHIELD types
  - Stack types: REPLACE/STACK/REFRESH
  - Built-in buffs: shield, burning, strength

### Refactored

- `src/systems/combat.lua` - Now emits DamageRequest events instead of direct damage
- `src/systems/ai.lua` - Uses RuleEngine.tryUseAbility() for enemies
- `src/systems/buff.lua` - DELETED, logic merged into RuleEngine
- `src/main.lua` - Removed BuffSystem reference
- `src/core/events.lua` - performance improvements

### Architecture

- **Event-driven pattern**: RuleEngine emits XxxRequest events, listens for them, processes
- **Buff system integration**: Buffs stored in Health component, tick via BuffTickRequest
- **Shield absorption**: Damage reduced by shield before HP

### Design Decisions

- **Request → Process pattern**: UI emits AbilityUse, RuleEngine emits DamageRequest, RuleEngine processes
- **Buff lifecycle**: BuffTickRequest for DOT/HOT ticking, BuffApplyRequest for application
- **Single processor per effect type**: Centralized processing prevents duplicate logic

### Breaking Changes

- CombatSystem no longer deals direct damage - emits DamageRequest
- BuffSystem removed - use RuleEngine for all buff operations

### Bug Fixes

- Fixed SINGLE target abilities not auto-selecting target when targetId is nil
- Fixed applyAbility using undefined `entityId` variable - now uses `sourceId`
- Fixed ability.id not used in getValidTargets call
- Added missing burn_damage effect definition for burning DOT tick

---

## ECS 架构重构

### Refactored

- `src/core/ecs.lua` - ECS core improvements:
  - Added `World:getComponent(entityId, componentName)` - encapsulated component access
  - Added `World:setComponent(entityId, componentName, data)` - encapsulated component mutation
  - Added `World:hasComponent(entityId, componentName)` - component existence check
  - Added `World:getSpatialHash()` - spatial hash for O(1) queries
  - `World:query()` now returns read-only proxies by default (prevents accidental mutation)
  - Integrated `SpatialHash` for efficient spatial queries

- `src/core/ecs.lua` - SpatialHash implementation:
  - `SpatialHash:insert/remove/move()` - entity position tracking
  - `SpatialHash:getAt(x, y)` - O(1) lookup of entities at position
  - `SpatialHash:getNeighbors(x, y, radius)` - area queries
  - Auto-updates when entities spawn/move/despawn

- `src/utils/prototype.lua` - Deep copy improvements:
  - Added cycle detection to prevent infinite loops
  - Proper metatable preservation
  - Handles nested tables correctly

### Added

- `src/systems/turn.lua` - TurnSystem:
  - Manages turn-based game state
  - `startTurn()` / `endTurn()` for turn lifecycle
  - `isInputAllowed()` - checks if player can act
  - `getTurnCount()` - current turn number
  - Listens for PlayerTurnEnd and TurnEnd events

### Design Decisions

- **Encapsulation**: Components should only be accessed via World methods, never directly
- **Read-only queries**: `query()` returns proxy that throws on modification attempts
- **Spatial Hash**: Replaces O(n) queries with O(1) lookups for collision detection
- **Pure data components**: Components are plain tables, no behavior methods
- **Event-driven AI**: AISystem responds to PlayerTurnEnd rather than polling

### Breaking Changes

- `world.components[Name][id]` direct access deprecated, use `world:getComponent(id, "Name")`
- Components with behavior methods (Health, Position) removed - use Systems for logic

---

## 回合制战斗系统

### Added

- `src/systems/combat.lua` - CombatSystem:
  - Collision-based touch damage
  - Player deals 2 damage, enemies deal 1 damage
  - Both parties take damage on collision
  - PlayerTurnEnd event after combat

- `src/systems/ai.lua` - AISystem:
  - Random movement (70% chance per turn)
  - Listens for PlayerTurnEnd to act
  - Emits TurnEnd when enemies finish

- `src/systems/movement.lua` - MovementSystem updates:
  - Emits CollisionDetected when blocked
  - Emits MoveSucceeded when moved
  - Passes isPlayer flag through events

### Turn Flow

1. Player presses direction → MoveAttempt(isPlayer=true)
2. MovementSystem → CollisionDetected OR MoveSucceeded
3. CombatSystem → dealDamage to both parties → PlayerTurnEnd
4. AISystem (waitingForPlayerTurn=true) → enemies move → TurnEnd
5. TurnEnd → unlock input

### Bug Fixes

- Fixed isPlayer field not passed through events
- Fixed CollisionDetected not triggering player turn end
- Fixed enemies not dying causing turn lock

---

## LÖVE 渲染集成

### Added

- `src/systems/render.lua` - RenderSystem class with:
  - `RenderSystem:init(world)` - Load tileset image, pre-create quads
  - `RenderSystem:draw(world)` - Main draw loop
  - `RenderSystem:drawTiles(world, offsetX, offsetY)` - Draw floor/wall tiles
  - `RenderSystem:drawEntities(world, offsetX, offsetY)` - Draw actors on tiles
  - `RenderSystem:drawHealthBars(world, offsetX, offsetY)` - Draw HP bars
  - `RenderSystem:centerOnPlayer(world)` - Camera centering logic

- `src/components/renderable.lua` - Renderable component:
  - `tileIndex` - Index in tileset.png for this entity

- `src/assets/tileset_info.lua` - Auto-generated tileset mapping:
  - tile_0 (.), tile_1 (#), tile_2 (@), g, r, O, tile_6, tile_7

- `src/data/prototypes/tiles.lua` - Tile prototypes:
  - floor (tileIndex 0, walkable)
  - wall (tileIndex 1, Solid)

- `src/main.lua` - LÖVE entry point with:
  - love.load() - Initialize world, prototypes, systems
  - love.update(dt) - Update ECS world
  - love.draw() - Render game
  - love.keypressed() - Handle WASD/Arrow input
  - Turn-based game loop with turnInProgress flag

### Design Decisions

- TILE_SIZE = 16 pixels for classic roguelike look
- TILES_PER_ROW = 8 for tileset layout
- Pixel-perfect scaling with nearest filter
- Camera centered on player position
- Two-pass rendering: tiles first, then entities
- Health bars shown above actors (green/yellow/red)
- Turn-based input locking to prevent action overlap

---

## 原型系统

### Added

- `src/utils/prototype.lua` - PrototypeManager class with:
  - `PrototypeManager:new(world)` - Create manager
  - `PrototypeManager:load(moduleName)` - Load Lua module as prototypes
  - `PrototypeManager:register(name, components)` - Register single prototype
  - `PrototypeManager:get(name)` - Get prototype by name
  - `PrototypeManager:has(name)` - Check if prototype exists
  - `PrototypeManager:spawn(name, overrides)` - Spawn entity from prototype
  - `PrototypeManager:getNames()` - Get all prototype names
  - `PrototypeManager:clear()` - Clear all prototypes
  - `PrototypeManager.loadModule(moduleName)` - Simple function alternative

- `data/prototypes/entities.lua` - Example prototypes:
  - player, rat, goblin, orc (entities)
  - health_potion, gold_pile (items)
  - wall, floor (tiles)

### Design Decisions

- Uses Lua module (require) instead of YAML parsing
- Zero dependencies, native LÖVE support
- Deep copy on spawn to preserve original prototypes
- Overrides parameter allows per-spawn customization

### Lua 替代 YAML

**为什么用 Lua 而非 YAML 定义原型？**

| 方面 | Lua | YAML |
|------|-----|------|
| 实现成本 | 0（原生 require） | 需要解析器 |
| 动态数据 | 随时可以 computed value | 静态 |
| 轻量逻辑 | 可包含函数/计算 | 仅数据 |
| 性能 | 最好（Lua 原生） | 需要解析 |
| 代码集成 | 无缝 | 需要工具桥接 |

**优势**：

- 实现成本为 0
- 支持"动态数据"和"轻量逻辑"
- 性能最好
- 和代码无缝集成

---

## 系统基类

### Added

- `src/ecs/system.lua` - System base class with:
  - `System:new(opts)` - Create system with priority, enabled, name options
  - `System:extend(className)` - Create subclass
  - `System:init(world)` - Initialize when added to world
  - `System:onAddToWorld(world)` - Called when added to world
  - `System:update(dt, world)` - Frame update (override in subclass)
  - `System:shutdown()` - Cleanup when world destroyed
  - `System:onRemoveFromWorld()` - Called when removed from world
  - `System:enable()` / `disable()` - Enable/disable system
  - `System:isEnabled()` - Check if enabled
  - `System:getName()` / `setName()` - Name management

### Design Decisions

- Systems are added to World with priority ordering
- Disabled systems are skipped during update
- init/shutdown lifecycle for proper resource management
- Subclass pattern using :extend() for custom systems

---

## 组件生命周期

### Added

- `src/ecs/component.lua` - Component base class with:
  - `Component:new(name, data)` - Create component with data
  - `Component:extend(className)` - Create subclass
  - `Component.create(name, data)` - Factory helper
  - Lifecycle stages: PRE_ADD → ADDING → ADDED → INITIALIZING → INITIALIZED → STARTING → RUNNING → STOPPING → STOPPED → REMOVING → DELETED

### Lifecycle Hooks

- `OnPreAdd()` - Called before component added to entity
- `OnAdd()` - Called when component is being added
- `OnAdded()` - Called after component is added
- `OnInitialize()` - Called when all components added (entity init phase)
- `OnStartup()` - Called when entity fully initialized
- `OnShutdown()` - Called when entity is shutting down (before removal)
- `OnRemove()` - Called when component is being removed
- `OnDeleted()` - Called after component is removed

### Design Decisions

- Supports both plain table components and Component instances

### 架构重构：ShouldDespawn 模式

最初实现的生命周期回调存在设计问题：执行时机微妙、访问已销毁组件导致崩溃。

**新设计**：

- `World:despawn(entityId, reason)` - 标记实体添加 ShouldDespawn 组件
- `World:processDespawns()` - 批量处理销毁
- 用户通过 CleanupSystem 在销毁前执行自定义逻辑

```lua
local CleanupSystem = {
    priority = 1000,
    update = function(self, dt, world)
        local toCleanup = world:query({"ShouldDespawn"})
        for _, result in ipairs(toCleanup) do
            -- 自定义清理逻辑
        end
        world:processDespawns()
    end
}
```

---

## 事件系统

### Added

- `src/core/events.lua` - EventBus implementation with:
  - `EventBus:new()` - Create a new event bus
  - `EventBus:on(eventName, handler, priority)` - Subscribe to event
  - `EventBus:emit(eventName, data)` - Emit broadcast event to all subscribers
  - `EventBus:emitTo(targetId, eventName, data)` - Emit directed event to specific entity
  - `EventBus:emitToMany(targetIds, eventName, data)` - Emit to multiple entities
  - `EventBus:off(eventName, index)` - Unsubscribe handler
  - `EventBus:clear()` - Clear all listeners
  - `EventBus:count(eventName)` - Get listener count
  - `EventBus:child()` - Create child bus inheriting parent listeners

### Design Decisions

- Handlers stored in priority order (lower = called first)
- emit() collects listeners from self and all ancestors
- emitTo() for entity-specific events ("Entity 5 received 10 damage")
- emitToMany() for multi-target events (explosions, AoE)
- emit vs emitTo: broadcast vs directed event pattern
- Returns unsubscribe function from :on()
- Copy listeners during emit to avoid modification issues

### EventBus 优化

#### Dirty Flag 优化

Lua 的表天生适合实现 Dirty Flag 数据结构，平衡注册/注销和 emit 的开销。

**数据结构**：

- `listeners` - 原始未排序的处理器列表
- `sortedHandlers` - 缓存的排序后处理器
- `dirty` - 脏标记表，eventName -> true 表示需要重建缓存

**工作流程**：

1. `on()` 注册时：添加处理器，设置 dirty 标记
2. `off()` 注销时：移除处理器，设置 dirty 标记
3. `emit()` 时：
   - 检查 dirty 标记
   - 若 dirty，调用 `_rebuild()` 重建排序缓存
   - 使用缓存分发事件

**好处**：注册/注销 O(1)，多次 emit 只在必要时重建缓存

### Refactored

- `src/core/events.lua` - EventBus 性能与功能优化：
  - **emitTo targetId 过滤**：新增 `_getHandlers(eventName, targetId)` 方法，`emitTo` 现在只调用匹配 targetId 的 handler（targetId=nil 表示通用 handler）
  - **排序优化**：用 `table.sort()` 替换手写插入排序，更简洁高效
  - **emitToMany 优化**：在循环前统一 rebuild cache 一次，避免 N 次重复检查 dirty 和 rebuild

### Breaking Changes

- `EventBus:on()` 新增可选参数 `targetId`，用于注册定向 handler：`bus:on("damage", handler, 0, entityId)`
- 只有匹配 targetId（或 targetId=nil）的 handler 会被 `emitTo(targetId, ...)` 调用

---

## ECS Core 基础

### Added

- `src/core/ecs.lua` - ECS core implementation with:
  - `World:new()` - Create a new world instance
  - `World:spawn(components)` - Create entity with components, returns id
  - `World:despawn(id)` - Destroy entity by id
  - `World:query(componentNames)` - Query entities by required components
  - `World:addSystem(system)` - Register a system to the world
  - `World:update(dt)` - Update all systems with delta time

### Design Decisions

- Entity IDs use incremental integers starting from 1
- Component storage uses array per component type for O(1) lookup
- Query returns array of {id, components} objects
- Systems are stored in priority order for update sequence
- No LÖVE dependencies - pure Lua implementation for testability

### Bug Fixes

#### 架构重构：OOP → 纯表 ECS

**问题**：原设计使用OOP基类（System:extend, Component:extend），没有实际好处，增加复杂度。

**变更**：

- 删除 `src/ecs/system.lua` - OOP基类
- 删除 `src/ecs/component.lua` - OOP基类
- 所有系统改为纯表定义（6个文件）

**新系统格式**：

```lua
local MovementSystem = {
    priority = 1,
    name = "MovementSystem",
    init = function(self, world) ... end,
    update = function(self, world, dt) ... end
}
return MovementSystem
```

**好处**：

- 无元表开销
- 更简洁，符合ECS原则
- 代码更易理解和维护

**Breaking Changes**：

- 系统update函数签名改为 `update(self, world, dt)`
- 移除 `:new()` 调用，直接传表给 `world:addSystem()`