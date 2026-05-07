# Changelog

All notable changes to this project will be documented in this file.

## [0.1.0] - Initial Release

ECS-based traditional roguelike with LÖVE2D

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

## [Unreleased]

#### Task 8: ECS架构重构

##### Refactored
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

##### Added
- `src/systems/turn.lua` - TurnSystem:
  - Manages turn-based game state
  - `startTurn()` / `endTurn()` for turn lifecycle
  - `isInputAllowed()` - checks if player can act
  - `getTurnCount()` - current turn number
  - Listens for PlayerTurnEnd and TurnEnd events

##### Design Decisions
- **Encapsulation**: Components should only be accessed via World methods, never directly
- **Read-only queries**: `query()` returns proxy that throws on modification attempts
- **Spatial Hash**: Replaces O(n) queries with O(1) lookups for collision detection
- **Pure data components**: Components are plain tables, no behavior methods
- **Event-driven AI**: AISystem responds to PlayerTurnEnd rather than polling

##### Breaking Changes
- `world.components[Name][id]` direct access deprecated, use `world:getComponent(id, "Name")`
- Components with behavior methods (Health, Position) removed - use Systems for logic

---

#### Task 1: ECS Core 基础

##### Added
- `src/core/ecs.lua` - ECS core implementation with:
  - `World:new()` - Create a new world instance
  - `World:spawn(components)` - Create entity with components, returns id
  - `World:despawn(id)` - Destroy entity by id
  - `World:query(componentNames)` - Query entities by required components
  - `World:addSystem(system)` - Register a system to the world
  - `World:update(dt)` - Update all systems with delta time

##### Design Decisions
- Entity IDs use incremental integers starting from 1
- Component storage uses array per component type for O(1) lookup
- Query returns array of {id, components} objects
- Systems are stored in priority order for update sequence
- No LÖVE dependencies - pure Lua implementation for testability

#### Task 2: 事件系统

##### Added
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

##### Design Decisions
- Handlers stored in priority order (lower = called first)
- emit() collects listeners from self and all ancestors
- emitTo() for entity-specific events ("Entity 5 received 10 damage")
- emitToMany() for multi-target events (explosions, AoE)
- emit vs emitTo: broadcast vs directed event pattern
- Returns unsubscribe function from :on()
- Copy listeners during emit to avoid modification issues

##### Dirty Flag 优化
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

#### Task 3: 组件生命周期

##### Added
- `src/ecs/component.lua` - Component base class with:
  - `Component:new(name, data)` - Create component with data
  - `Component:extend(className)` - Create subclass
  - `Component.create(name, data)` - Factory helper
  - Lifecycle stages: PRE_ADD → ADDING → ADDED → INITIALIZING → INITIALIZED → STARTING → RUNNING → STOPPING → STOPPED → REMOVING → DELETED

##### Lifecycle Hooks
- `OnPreAdd()` - Called before component added to entity
- `OnAdd()` - Called when component is being added
- `OnAdded()` - Called after component is added
- `OnInitialize()` - Called when all components added (entity init phase)
- `OnStartup()` - Called when entity fully initialized
- `OnShutdown()` - Called when entity is shutting down (before removal)
- `OnRemove()` - Called when component is being removed
- `OnDeleted()` - Called after component is removed

##### Design Decisions
- Supports both plain table components and Component instances

##### 架构重构：ShouldDespawn 模式
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

#### Task 4: 系统基类

##### Added
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

##### Design Decisions
- Systems are added to World with priority ordering
- Disabled systems are skipped during update
- init/shutdown lifecycle for proper resource management
- Subclass pattern using :extend() for custom systems

#### Task 5: 原型系统

##### Added
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

##### Design Decisions
- Uses Lua module (require) instead of YAML parsing
- Zero dependencies, native LÖVE support
- Deep copy on spawn to preserve original prototypes
- Overrides parameter allows per-spawn customization

##### 原型系统：Lua 替代 YAML

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

#### Task 6: LÖVE 渲染集成

##### Added
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

##### Design Decisions
- TILE_SIZE = 16 pixels for classic roguelike look
- TILES_PER_ROW = 8 for tileset layout
- Pixel-perfect scaling with nearest filter
- Camera centered on player position
- Two-pass rendering: tiles first, then entities
- Health bars shown above actors (green/yellow/red)
- Turn-based input locking to prevent action overlap

#### Task 7: 回合制战斗系统

##### Added
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

##### Turn Flow
1. Player presses direction → MoveAttempt(isPlayer=true)
2. MovementSystem → CollisionDetected OR MoveSucceeded
3. CombatSystem → dealDamage to both parties → PlayerTurnEnd
4. AISystem (waitingForPlayerTurn=true) → enemies move → TurnEnd
5. TurnEnd → unlock input

##### Bug Fixes
- Fixed isPlayer field not passed through events
- Fixed CollisionDetected not triggering player turn end
- Fixed enemies not dying causing turn lock
