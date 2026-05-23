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
end

function MovementSystem:update(world, dt)
end

function MovementSystem:onMoveAttempt(data)
    local entity = data.entity
    local dx = data.dx or 0
    local dy = data.dy or 0

    local pos = self.world.components.Position[entity]
    if not pos then
        return
    end

    local newX = pos.x + dx
    local newY = pos.y + dy

    local collision = self:checkCollision(entity, newX, newY)

    if collision then
        if self.events then
            self.events:emit("CollisionDetected", {
                entity = entity,
                target = collision,
                x = newX,
                y = newY,
                isPlayer = data.isPlayer
            })

            if data.isPlayer then
                self.events:emit("PlayerTurnEnd", {})
            end
        end
        return
    end

    local targetEntity = self:getEntityAt(newX, newY)
    if targetEntity and targetEntity ~= entity then
        if self.events then
            self.events:emit("CollisionDetected", {
                entity = entity,
                target = targetEntity,
                x = newX,
                y = newY,
                isPlayer = data.isPlayer
            })

            if data.isPlayer then
                self.events:emit("PlayerTurnEnd", {})
            end
        end
        return
    end

    pos.x = newX
    pos.y = newY

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
    local mapRenderer = nil
    for _, sys in ipairs(self.world.systems) do
        if sys.name == "MapRenderer" then
            mapRenderer = sys
            break
        end
    end

    if mapRenderer and mapRenderer:isSolid(x, y) then
        return -1
    end

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
    local entities = self.world:query({"Position"})

    for _, result in ipairs(entities) do
        local pos = result.components.Position
        if pos and pos.x == x and pos.y == y then
            if not result.components.Player and not result.components.Actor then
            elseif result.components.Player or result.components.Actor then
                return result.id
            end
        end
    end

    return nil
end

return MovementSystem