-- Movement System
-- Handles entity movement and collision detection

local System = require("src.ecs.system").System

local MovementSystem = System:extend("MovementSystem")

function MovementSystem:new(opts)
    local instance = System.new(self, opts)
    instance.name = "MovementSystem"
    return instance
end

function MovementSystem:init(world)
    self.world = world
    self.events = world.eventBus
    
    -- Register for MoveAttempt events
    if self.events then
        self.events:on("MoveAttempt", function(data)
            self:onMoveAttempt(data)
        end, 0)  -- Higher priority = called first for this event
    end
end

function MovementSystem:update(dt, world)
    -- Movement handled by event callbacks
end

function MovementSystem:onMoveAttempt(data)
    local entity = data.entity
    local dx = data.dx or 0
    local dy = data.dy or 0
    
    -- Get current position using encapsulated access
    local pos = self.world:getComponent(entity, "Position")
    if not pos then
        return
    end
    
    local newX = pos.x + dx
    local newY = pos.y + dy
    
    -- Check for collision with Solid entities at target position using SpatialHash
    local collision = self:checkCollision(entity, newX, newY)
    
    if collision then
        -- Emit collision event
        if self.events then
            self.events:emit("CollisionDetected", {
                entity = entity,
                target = collision,
                x = newX,
                y = newY,
                isPlayer = data.isPlayer
            })
        end
        return
    end
    
    -- Check if another entity (non-solid) is at target position
    local targetEntity = self:getEntityAt(newX, newY)
    if targetEntity and targetEntity ~= entity then
        -- Non-solid entity blocking - emit collision
        if self.events then
            self.events:emit("CollisionDetected", {
                entity = entity,
                target = targetEntity,
                x = newX,
                y = newY,
                isPlayer = data.isPlayer
            })
        end
        return
    end
    
    -- Move is clear - update position
    self.world:setComponent(entity, "Position", {x = newX, y = newY})
    
    -- Emit success event
    if self.events then
        self.events:emit("MoveSucceeded", {
            entity = entity,
            x = newX,
            y = newY,
            dx = dx,
            dy = dy,
            isPlayer = data.isPlayer  -- Pass through from MoveAttempt
        })
    end
end

function MovementSystem:checkCollision(entity, x, y)
    -- Use SpatialHash for O(1) lookup instead of O(n) query
    local spatialHash = self.world:getSpatialHash()
    local entitiesAtPos = spatialHash:getAt(x, y)
    
    if not entitiesAtPos then
        return nil
    end
    
    -- Check if any entity at position has Solid component
    local entities = type(entitiesAtPos) == "table" and entitiesAtPos or {entitiesAtPos}
    for _, id in ipairs(entities) do
        if id ~= entity and self.world:hasComponent(id, "Solid") then
            return id
        end
    end
    
    return nil
end

function MovementSystem:getEntityAt(x, y)
    -- Use SpatialHash for O(1) lookup
    local spatialHash = self.world:getSpatialHash()
    local entitiesAtPos = spatialHash:getAt(x, y)
    
    if not entitiesAtPos then
        return nil
    end
    
    -- Find actor entity (player or enemy) at position
    local entities = type(entitiesAtPos) == "table" and entitiesAtPos or {entitiesAtPos}
    for _, id in ipairs(entities) do
        if id and (self.world:hasComponent(id, "Player") or self.world:hasComponent(id, "Actor")) then
            return id
        end
    end
    
    return nil
end

return MovementSystem
