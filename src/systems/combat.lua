-- Combat System
-- Handles collision-based combat (touch damage)

local CombatSystem = {
    priority = 2,
    name = "CombatSystem",
    
    init = function(self, world)
        self.world = world
        self.events = world.eventBus
        
        -- Register for collision and move events
        if self.events then
            self.events:on("MoveSucceeded", function(data)
                self:onMoveSucceeded(data)
            end)
            
            self.events:on("CollisionDetected", function(data)
                self:onCollision(data)
            end)
        end
    end,
    
    update = function(self, world, dt)
        -- Combat handled by event callbacks
    end,
    
    -- When an entity moves, check if they collided with another
    onMoveSucceeded = function(self, data)
        local entity = data.entity
        local x = data.x
        local y = data.y
        local isPlayer = data.isPlayer
        
        -- Check for other actors at the same position
        self:checkCombat(entity, x, y)
        
        -- If player moved, trigger enemy turn
        if isPlayer and self.events then
            self.events:emit("PlayerTurnEnd", {})
        end
    end,
    
    -- When a collision is detected
    onCollision = function(self, data)
        local entity = data.entity
        local target = data.target
        local isPlayer = data.isPlayer
        
        -- Only combat with actors (enemies), not walls or tiles
        local targetIsActor = self.world.components.Actor and self.world.components.Actor[target]
        
        if not targetIsActor then
            return  -- No combat with non-actors (walls, etc.)
        end
        
        -- Both take damage (ramming damage)
        self:dealDamage(entity, target)
        self:dealDamage(target, entity)
        
        -- If player attacked, end turn
        if isPlayer and self.events then
            self.events:emit("PlayerTurnEnd", {})
        end
    end,
    
    checkCombat = function(self, entity, x, y)
        -- Find actors at the same position (enemies to fight)
        local entities = self.world:query({"Position", "Health", "Actor"})
        
        for _, result in ipairs(entities) do
            if result.id ~= entity then
                local pos = result.components.Position
                if pos and pos.x == x and pos.y == y then
                    -- Combat!
                    self:dealDamage(entity, result.id)
                end
            end
        end
    end,
    
    dealDamage = function(self, source, target)
        local sourceHealth = self.world.components.Health[source]
        local targetHealth = self.world.components.Health[target]
        
        if not targetHealth then
            return
        end
        
        -- Base damage
        local damage = 1
        
        -- Check if source is player (could have weapons later)
        if self.world.components.Player[source] then
            damage = 2  -- Player deals more damage
        end
        
        -- Apply damage
        targetHealth.current = targetHealth.current - damage
        
        -- Emit damage event
        if self.events then
            self.events:emit("DamageDealt", {
                source = source,
                target = target,
                amount = damage,
                newHealth = targetHealth.current
            })
        end
        
        -- Check for death
        if targetHealth.current <= 0 then
            if self.events then
                self.events:emit("EntityDied", {
                    entity = target,
                    killer = source
                })
            end
        end
    end
}

return CombatSystem
