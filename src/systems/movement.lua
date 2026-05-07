-- Movement System
-- Handles entity movement and collision detection

local MovementSystem = {
    priority = 1,
    name = "MovementSystem",
    
    init = function(self, world)
        self.world = world
        self.events = world.eventBus
        
        if self.events then
            self.events:on("MoveAttempt", function(data)
                self:onMoveAttempt(data)
            end, 0)
        end
    end,
    
    update = function(self, world, dt)
        -- Movement handled by event callbacks
    end,
    
    onMoveAttempt = function(self, data)
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
                
                -- Player bump into wall = turn ends
                if data.isPlayer then
                    self.events:emit("PlayerTurnEnd", {})
                end
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
                
                -- Player bump into entity = turn ends
                if data.isPlayer then
                    self.events:emit("PlayerTurnEnd", {})
                end
            end
            return
        end
        
        -- Move is clear - update position
        pos.x = newX
        pos.y = newY
        
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
    end,
    
    checkCollision = function(self, entity, x, y)
        -- Find solid entities at position
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
    end,
    
    getEntityAt = function(self, x, y)
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
}

return MovementSystem
