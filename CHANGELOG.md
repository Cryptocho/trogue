# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Y 坐标排序渲染（透视效果）

- 影响的文件: `src/main.lua`, `src/systems/map_renderer.lua`, `src/systems/render.lua`
- 渲染顺序从固定顺序（实体→树）改为按 y 坐标分层绘制：每层先画树再画实体，y 大的后画（在前）
- `MapRenderer` 新增 `getTreePositions()` 返回视口内树位置列表，`drawSingleTree(tree, alpha)` 支持透明度参数
- `RenderSystem` 新增 `getEntityPositions(world)` 返回实体位置列表（含 `logicY` 字段），`drawSingleEntity(entity, offsetX, offsetY)` 绘制单个实体
- 透视效果：玩家与树视觉重叠且玩家 y < 树 y 时，树透明度降至 30%
- 鼠标悬停透明：鼠标在树的整个绘制区域内时，树透明度降至 30%，显示树后敌人

### TileSet 导出器支持 Scene Tiles

- 影响的文件: `tools/addons/tileset_exporter/tileset_exporter.gd`
- 新增 `_extract_scene_tiles()` 和 `_find_sprite2d()` 函数，从 `TileSetScenesCollectionSource` 中提取场景瓦片数据
- 支持 `Sprite2D` + `AtlasTexture` 或 `Texture2D` 的场景，提取 texture_path、region、offset、centered、z_index
- 导出格式新增顶层 `scene_tiles` 数组（仅非空时写入），JSON/Lua 序列化自动处理新字段
- 不支持的场景（无 Sprite2D、无纹理、非支持纹理类型）输出 `push_warning` 并跳过

### 树精灵渲染（Scene Tile 集成）

- 影响的文件: `src/systems/map_renderer.lua`, `src/main.lua`, `src/assets/tileset.lua` (新建), `src/assets/Decorations.png` (新建), `src/assets/Tile Set.png` (迁移)
- `MapRenderer:init()` 从 `tileset.lua` 读取 `scene_tiles` 数据，加载 `Decorations.png`，按 region 创建 tree quad 并存储 offset
- `MapRenderer:draw()` 跳过 TILE_TREE（树在独立 pass 绘制）
- 新增 `MapRenderer:drawTrees(cameraX, cameraY, offsetX, offsetY)`：以 tile 中心为基准应用 region 居中 + offset 偏移绘制树精灵
- 绘制顺序：地面 → 实体 → 树（z_index=1）→ 血条 → 瞄准预览
- 地板瓦片路径从 `pixel-set-library/dungen-tile/` 迁移至 `assets/Tile Set.png`
- **注意**: 当前 z_index 为全局整数排序，未来需要引入遮蔽区域（occlusion region）概念，用于精确控制 asset 的哪一部分绘制在其它之上（例如树冠遮挡角色但树干不遮挡）

### AutoTile 地板渲染系统

- 影响的文件: `src/utils/autotile.lua` (新建), `src/assets/pixel-set-library/dungen-tile/tileset.lua` (新建), `src/systems/map_renderer.lua` (修改), `temp/addons/tileset_exporter/tileset_exporter.gd` (新建)
- 新建 `Autotile` 模块，提供 4-bit bitmask 计算和 LÖVE Quad 构建功能
- 从 `tileset_exporter` 导出 tileset 数据文件 `tileset.lua`，使用整数键 bitmask_map 支持 O(1) 查找
- `MapRenderer` 地板渲染从平铺 `image.png` 替换为 autotile quad 系统，wall/tree 渲染保持不变
- 新建 `tileset_exporter`，支持 JSON/Lua 双格式导出，共享构建逻辑

### 背包快捷键合并（E 键统一使用/装备）

- 影响的文件: `src/systems/inventory_ui.lua`
- 移除独立的 `U` 键使用逻辑，合并到 `E` 键：光标处物品为消耗品时自动使用、为装备时自动装备

### 异常机制设计文档

- 影响的文件: `doc/System/Exception-mechanism.md` (新建)
- 定义五种异常机制设计：眩晕（禁止技能/减控制抗性/减准度/减闪避）、中毒（每回合 hp×1%+1）、流血（每回合 hp×3%+2）、击退（反方向移动/碰撞伤害）、虚弱（减伤/增加技能消耗/增加受击伤害）

### 组件文件语法修复

- 影响的文件: `src/components/inventory.lua`, `src/components/inventory_item.lua`, `src/components/equipment.lua`
- 文件开头从 `{` 改为 `return {`，消除 linter 警告

### 大背包（Grid Inventory）系统

- 影响的文件: `src/data/definitions/item.lua` (新建), `src/components/inventory.lua` (重写), `src/components/equipment.lua` (新建), `src/components/inventory_item.lua` (新建), `src/systems/inventory_system.lua` (新建), `src/systems/inventory_ui.lua` (重写), `src/systems/input.lua`, `src/systems/render.lua`, `src/systems/pickup_system.lua` (删除), `src/data/prototypes/entities.lua`, `src/utils/map_generator.lua`, `src/main.lua`, `doc/selfplan.md` (新建)

### 趁手打击（Opportunity Attack）机制

- 影响的文件: `src/data/definitions/effect.lua`, `src/core/rule_engine.lua`, `src/systems/movement.lua`
- 新增 `opportunity_attack` 内置效果（`type=DAMAGE`，`damageType=PHYSICAL`，`chance=0.5`，`valueFormula={basePercent=0.5, statScaling={{strength, 2}}}`）
- `RuleEngine` 新增 `OpportunityAttack` 事件监听（priority=0），处理函数 `_ruleEngineProcessOpportunityAttack` 直接复用 `_ruleEngineApplyEffect` 的概率判定和伤害管道
- `MovementSystem` 新增两个监听器：
  - `MoveSucceeded`（玩家移动后，根据 `dx/dy` 推算旧位置，检查旧邻格敌人是否不再相邻）
  - `KnockbackApplied`（玩家被击退后，检查 from/to 邻格敌人是否不再相邻）
- 新增 `_getAdjacentActors(x, y, excludeEntity)`：查询指定位置四方向 spatial hash，返回带 `Actor` 组件且非自身的实体
- 新增 `_checkOpportunityAttack(entity, oldX, oldY, newX, newY)`：对旧位置邻格每个敌人，若旧曼哈顿距离 ≤1 且新距离 >1，则发射 `OpportunityAttack` 事件
- 事件时序：`MoveSucceeded/KnockbackApplied` → `OpportunityAttack` → `DamageRequest` → 伤害/护盾/死亡处理 → `MoveSucceeded` 继续 → `endPlayerTurn()` → `PlayerTurnEnd`，确保发生在敌方回合开始前

### 武器库定义层与 WeaponSystem 重构

- 影响的文件: `src/data/definitions/weapon.lua` (新建), `src/systems/weapon_system.lua` (重写), `src/components/weapon.lua` (重写), `src/data/prototypes/entities.lua`
- 新建 `src/data/definitions/weapon.lua`：武器定义注册表，22 种预置武器（ fists / greatsword / longsword / iron_sword / flame_blade / ice_blade / storm_edge / battle_axe / hand_axe / dagger / poison_dagger / spear / mace / quarterstaff / fire_staff / ice_staff / lightning_staff / arcane_staff / shortbow / longbow / fire_bow / crossbow / throwing_knife / javelin / fire_wand / ice_wand ）
- 武器定义结构对齐 Ability/Effect/Buff 模式：`createWeaponDef()` 工厂函数 + `builtin` 注册表
- 包含全部 6 种伤害类型（physical/fire/ice/lightning/poison/arcane）+ 3 种武器类别（melee/ranged/magic）(这三种为暂定,以后肯定要细分)
- `WeaponComponent` 升级为全量 22 字段（`weaponId / weaponType / baseDamage / armorPenetration / physicalDamageBonus / critChance / hitRate / staggerRate / stunRate / knockbackRate / immobilizeRate / critDamageBonus / blockChance / blockPower / bleedChance / enchantDamage / limbDamage / magicDamage / burnChance / poisonChance / slowRate / chainChance / magicPenetration`）
- `WeaponSystem` 升级：加载武器定义、提供 `getResolvedStats(entityId)` 合并逻辑（组件内联值 > 定义默认值）、向后兼容 4 个旧查询方法
- 实体原型迁移：`Weapon = {type = "greatsword", baseDamage = 20, ...}` → `Weapon = {weaponId = "greatsword"}`，属性由定义层统一管理
- 新增武器：shortsword（goblin 装备）、fangs（rat 装备）、battle_axe（orc 装备）

### isBlocked 方法优化

- 影响的文件: `src/systems/movement.lua`, `src/systems/input.lua`
- `MovementSystem:isBlocked()` 新增 `Coordinates.isInBounds` 越界检查，越界坐标视为阻挡
- 实体障碍物检测从 `world:query({"Solid", "Position"})` 全表遍历改为 `SpatialHash:getAt()` + `hasComponent()` O(1) 查找
- `InputSystem:handleClick()` 切角检测中移除冗余的 `mapRenderer:isSolid()` 调用（`isBlocked` 内部已包含）

### 击退系统（Knockback）

- 影响的文件: `src/core/rule_engine.lua`, `src/data/definitions/effect.lua`
- 新增 `EffectType.KNOCKBACK` 效果类型，`knockback_1` 效果定义（击退 3 格）
- `_ruleEngineApplyEffect()` 中新增 KNOCKBACK 分支，发射 `KnockbackRequest` 事件
- 新增 `_ruleEngineProcessKnockback()` 处理器：沿远离 source 方向逐格推进，遇墙壁/实体/边界停止
- 击退后同步更新 Position + SpatialHash + Tween 动画，发射 `KnockbackApplied` 事件
- 新增 `_ruleEngineGetTweenSystem()` 懒缓存引用

### 能力范围/效果区域拆分（rangeFunc / effectAreaFunc）

- 影响的文件: `src/data/definitions/ability.lua`, `src/core/rule_engine.lua`, `src/systems/input.lua`, `src/systems/render.lua`
- `AbilityDefinition` 新增 `effectAreaFunc` 字段，与 `rangeFunc` 分离
  - `rangeFunc`：瞄准 UI 中显示的可选范围（鼠标悬停预览）
  - `effectAreaFunc`：技能实际释放时影响的目标 tile 集合
- `fireball`：`rangeFunc` 扩展为 5 格半径（瞄准范围），`effectAreaFunc` 保持 2 格半径（爆炸范围），爆炸中心改为目标点（原为施法者中心）
- `punch`：`rangeFunc` 为 3×3（瞄准），`effectAreaFunc` 为单格（只打击目标）
- `heal` / `shield`：两者均返回自身 tile
- `_ruleEngineApplyAbility()` 使用 `effectAreaFunc` 而非 `rangeFunc` 确定实际目标
- `RenderSystem:drawAimPreview()` 使用 `effectAreaFunc` 渲染伤害预览区域
- `InputSystem:handleAimClick()` 分离范围校验（`rangeFunc`）和目标校验（`effectAreaFunc`）

### 禁止切角移动（Corner Cutting）

- 影响的文件: `src/core/coordinates.lua`, `src/systems/movement.lua`, `src/systems/input.lua`
- 新增 `Coordinates.canDiagonalMove()` 工具函数，判断对角线移动是否合法（不切角穿过障碍物）
- `Coordinates.findPath()` A* 寻路中增加对角线方向切角检查，`isPassable` 视为障碍物函数
- `MovementSystem` 新增 `isBlocked()` 方法（组合地图+实体障碍物），`onMoveAttempt()` 中对角线移动追加切角检测
- `InputSystem.handleClick()` 中单步对角线移动追加切角检测，使用 `MovementSystem:isBlocked()` 统一校验（地图+实体障碍物）

### 程序化地图敌人生成

- 影响的文件: `src/utils/map_generator.lua`, `src/main.lua`
- `generateMap()` 新增第二个返回值 `enemySpawns`（`{x, y, type}` 数组），向后兼容
- 树木放置后从距离地图中心 `enemySpawnMinDist`（默认 8）外的地板 tile 中按 `enemyDensity`（默认 0.008）随机选取位置
- 敌人类型按权重分布：goblin(5/14), rat(6/14), orc(3/14)
- `initGameWorld()` 遍历 `enemySpawns` 通过 `prototypes:spawn()` 生成敌人实体

### ECS 架构清理与 getSystem 优化

- 影响的文件: `src/core/ecs.lua`, `src/core/events.lua`, `src/systems/input.lua`, `src/systems/render.lua`, `src/systems/movement.lua`, `src/systems/ai.lua`, `src/main.lua`
- `World` 移除 `componentInstances` 内部存储（OOP 组件生命周期残留），所有组件统一为纯数据存取
- 新增 `World:getSystem(name)` 方法，内置缓存实现 O(1) 查询，附带 `_systemCache` 懒初始化
- 所有系统（InputSystem、RenderSystem、MovementSystem、AISystem、main.game）统一使用 `world:getSystem()` 替代手动 `for` 循环遍历
- `main.lua` 中 `game:getSystem()` 委托给 `world:getSystem()`
- `EventBus:once()` 改为 handler 引用匹配替代 index 匹配，移除 `offByIndex` 方法

### A* 寻路性能优化 (MinHeap)

- 影响的文件: `src/utils/minheap.lua` (新建), `src/core/coordinates.lua`
- 新增 `MinHeap` 最小堆数据结构（push/pop/isEmpty/clear），用于优先级队列
- `Coordinates.findPath()` 中 `openSet` 从普通数组 + `table.sort()` O(n log n) 替代为 MinHeap O(log n)
- 新增 `openSetKeys` 哈希表，O(1) 判断节点是否已在 openSet（替代原来的遍历查找）
- `findPath` 最大迭代次数 1000 不变，`heuristic` 从曼哈顿改为切比雪夫距离（适应八向移动代价）

### 回合系统重构

- 影响的文件: `src/systems/turn.lua`, `src/systems/movement.lua`, `src/systems/combat.lua`
- 玩家回合结束逻辑从分布式（MovementSystem 碰撞时直接 emit PlayerTurnEnd）统一收束到 TurnSystem
- `TurnSystem` 新增 `MoveSucceeded` / `CollisionDetected` 事件监听，由 TurnSystem 统一驱动回合生命周期
- 移除 `turnInProgress` 状态字段，改为 `inputAllowed` + `currentPhase` 两状态模型
- `MovementSystem` 不再 emit `PlayerTurnEnd`（碰撞/墙体的回合逻辑由 TurnSystem 统一处理）
- 玩家撞墙/撞实体时 `inputAllowed = true`（保持原地等待新输入，不结束回合）

### RuleEngine 代码规范化

- 影响的文件: `src/core/rule_engine.lua`
- 内部函数统一添加 `_ruleEngine` 前缀：`_ruleEngineProcessDamage` / `_ruleEngineProcessHeal` / `_ruleEngineProcessBuffApply` / `_ruleEngineProcessBuffTick` 等
- `print()` 替换为 `debugPrint()` 宏（`DEBUG = false` 时静默，生产环境无日志噪音）
- 新增 `PERMANENT_BUFF_DURATION = -1` 常量，替代硬编码 `-1`
- `_getWeaponSystem()` → `_ruleEngineGetWeaponSystem()`，使用 `world:getSystem("WeaponSystem")` 优化
- `_evaluateFormula` / `_evaluateChanceFormula` / `_recalcComputed` 添加 `_ruleEngine` 前缀
- 移除未使用的 `_getArmorPenetration` 占位函数
- `_ruleEngineApplyAbility` 中 MapRenderer 查询从手动遍历改用 `world:getSystem("MapRenderer")`
- buff tick 中 `duration` 递减逻辑注释从 "+1 so tick starts from NEXT turn" 更正为 "3 ticks total (decremented first, ticked if >= 0)"

### CombatSystem 退化为战斗占位

- 影响的文件: `src/systems/combat.lua`
- CombatSystem（碰撞碰撞伤害）完全移除内部逻辑（曾负责 bump combat 和 PlayerTurnEnd 发射）
- 系统架构保留为占位框架（priority=2，name="CombatSystem"），为未来战斗机制预留
- 战斗现在完全由 RuleEngine 能力系统驱动（punch/fireball 等），碰撞伤害功能已淘汰

### 组件清理与平衡调整

- 影响的文件: `src/components/actor.lua`, `src/components/stats.lua`, `src/components/renderable.lua`, `src/components/solid.lua`, `src/components/player.lua`, `src/components/effect_tile.lua`, `src/data/prototypes/entities.lua`, `src/systems/map_renderer.lua`, `src/systems/movement.lua`, `src/conf.lua`, `src/assets/tileset_info.lua`
- `ActorComponent` 移除 `moveDelay` 字段，退化为纯标记组件（所有原型 `Actor = {}` 统一）
- `StatsComponent.computed` 新增 `damageAbsorb` 字段（为 shield 的 modifier 管道预留计算槽位）
- `StatsComponent` 注释从 "megastruct" 更正为 "entity attribute values"
- Orc 原型 HP 从 10 提升至 35（对齐中后期敌人强度），所有原型 Stats computed 补充 `damageAbsorb = 0`
- `poison_pool` / `fire_pool` 原型移除冗余 `Position` 字段（生成时动态设置）
- `RenderableComponent` / `SolidComponent` / `PlayerComponent` / `EffectTileComponent` 统一采用 `local X = {}; return X` 模板格式
- `MapRenderer:isSolid` 使用 `TILE_WALL` / `TILE_TREE` 常量替代硬编码 `1` / `8`，新增 `TILE_FLOOR` 常量
- `MovementSystem:onMoveAttempt` 位置更新从直接赋值改为 `world:setComponent(entity, "Position", {x = newX, y = newY})`，确保 SpatialHash 同步
- `conf.lua` 注释清理（移除 Windows console / FPS debug 等过时注释）
- `tileset_info.lua` 标注为参考文件（注明实际 tile 映射在 map_renderer.lua）
- `Coordinates` 模块移除未使用的 `isInArea` 函数
- `Coordinates.TILE_SIZE` 从硬编码 16 改为 `require("src.config")` 引用 `Config.TILE_SIZE`

### 技能鼠标瞄准系统

- 影响的文件: `src/data/definitions/ability.lua`, `src/core/rule_engine.lua`, `src/systems/input.lua`, `src/systems/render.lua`, `src/main.lua`, `src/systems/ai.lua`, `src/core/coordinates.lua`
- 每个能力持有 `rangeFunc(sourceX, sourceY, targetX, targetY, mapW, mapH) → {{x,y},...}` 内联函数，定义于 `ability.lua`
- 4 个内置技能添加 rangeFunc：punch 自身周围 8 格、heal/shield 自身单格、fireball 自身周围 Chebyshev 距离 ≤2 方形（24 格，不含自身）
- 按下数字键进入瞄准模式：受影响的图格绘制半透明绿色方形，障碍物或视线被遮挡的图格绘制红色方形，鼠标在可通行且无遮挡的图格时绘制黄色圆形
- 点击左键释放技能：先检测 solid 图格和 Bresenham 视线遮挡（均阻止释放），再检测 rangeFunc 覆盖范围内是否有实体存在，无实体时自动取消瞄准避免空放
- 右键/ESC/同数字键取消瞄准，瞄准模式下 WASD 忽略、其他数字键切换技能
- `hasLineOfSight` Bresenham 视线检测，新增于 `coordinates.lua`，用于判定目标图格是否被障碍物遮挡（起点图格不计入检测）
- `_ruleEngineApplyAbility` 重写：用 `rangeFunc` + `SpatialHash` 查找目标实体，移除旧的 SELF/SINGLE/AREA 分支
- `tryUseAbility`/`applyAbility` 签名从 `targetId` 改为 `targetX, targetY`，`AbilityUse` 事件数据同步更新
- AI 系统 `AbilityUse` 事件改为发射 `targetX, targetY`（目标实体坐标）
- `drawAimPreview` 新增瞄准预览渲染，摄像机位置与主渲染同步（tween-aware），障碍物/视线遮挡判定与 `handleAimClick` 保持一致
- `_screenToWorldTile` 提取公共 screen-to-tile 方法，消除 `handleClick`/`handleAimClick` 重复代码
- `_getMapRenderer` 惰性缓存 MapRenderer 引用，`_ruleEngineApplyAbility` 缓存地图尺寸
- 移除 `rangeFunc` fallback，所有技能必须定义 rangeFunc

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