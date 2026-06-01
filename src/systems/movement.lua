-- Movement System
-- Handles entity movement and collision detection

local MovementSystem = {
    priority = 1,
    name = "MovementSystem",
}

function MovementSystem:init(world)
    self.world = world
    self.events = world.eventBus

    if self.events then
        self.events:on("MoveAttempt", function(data)
            self:onMoveAttempt(data)
        end, 0)
    end

    -- Cache TweenSystem reference immediately after systems are registered
    self.tweenSystem = world:getSystem("TweenSystem")
end

function MovementSystem:update(world, dt)
    -- Movement handled by event callbacks
end

function MovementSystem:onMoveAttempt(data)
    local entity = data.entity
    local dx = data.dx or 0
    local dy = data.dy or 0

    -- Get current position
    local pos = self.world.components.Position[entity]
    if not pos then
        return
    end

    local newX = pos.x + dx
    local newY = pos.y + dy

    -- Check for collision with Solid entities at target position
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

    -- Move is clear - update logic position via setComponent to sync spatialHash
    local oldX, oldY = pos.x, pos.y
    self.world:setComponent(entity, "Position", {x = newX, y = newY})

    -- Start visual tween so entity slides instead of teleporting
    if self.tweenSystem then
        self.tweenSystem:startTween(entity, oldX, oldY)
    end

    -- Emit success event
    if self.events then
        self.events:emit("MoveSucceeded", {
            entity = entity,
            x = newX,
            y = newY,
            dx = dx,
            dy = dy,
            isPlayer = data.isPlayer
        })
    end
end

function MovementSystem:checkCollision(entity, x, y)
    -- Check collision with map tiles first
    local mapRenderer = self.world:getSystem("MapRenderer")

    if mapRenderer and mapRenderer:isSolid(x, y) then
        return -1  -- Use -1 to indicate map tile collision
    end

    -- Also check for solid entities (if any exist)
    local solids = self.world:query({"Solid", "Position"})

    for _, result in ipairs(solids) do
        if result.id ~= entity then
            local solidPos = result.components.Position
            if solidPos and solidPos.x == x and solidPos.y == y then
                return result.id
            end
        end
    end

    return nil
end

function MovementSystem:getEntityAt(x, y)
    -- Find any entity with Position at coordinates (excluding tiles)
    local entities = self.world:query({"Position"})

    for _, result in ipairs(entities) do
        local pos = result.components.Position
        if pos and pos.x == x and pos.y == y then
            -- Skip tile entities (no Player, Actor, etc.)
            if not result.components.Player and not result.components.Actor then
                -- This is a tile
            elseif result.components.Player or result.components.Actor then
                return result.id
            end
        end
    end

    return nil
end

return MovementSystem