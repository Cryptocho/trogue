-- Combat System
-- Handles collision-based combat (touch damage)

local System = require("src.ecs.system").System

local CombatSystem = System:extend("CombatSystem")

function CombatSystem:new(opts)
    return System.new(self, opts)
end

function CombatSystem:init(world)
    self.world = world
    self.events = world.eventBus
    
    -- Register for collision and move events
    if self.events then
        self.events:on("MoveSucceeded", function(data)
            self:onMoveSucceeded(data)
        end, 0)  -- Priority 0, called after MovementSystem
        
        self.events:on("CollisionDetected", function(data)
            self:onCollision(data)
        end, 0)
    end
end

function CombatSystem:update(dt, world)
    -- Combat handled by event callbacks
end

-- When an entity moves, check if they collided with another
function CombatSystem:onMoveSucceeded(data)
    local entity = data.entity
    local x = data.x
    local y = data.y
    local isPlayer = data.isPlayer
    
    -- Check for other actors at the same position
    self:checkCombat(entity, x, y)
    
    -- If player moved, trigger player turn end
    if isPlayer and self.events then
        self.events:emit("PlayerTurnEnd", {})
    end
end

-- When a collision is detected
function CombatSystem:onCollision(data)
    local entity = data.entity
    local target = data.target
    local isPlayer = data.isPlayer
    
    -- Only deal damage if BOTH entities are living (have Health)
    -- Wall/solid entities without Health don't take damage
    local entityHealth = self.world:getComponent(entity, "Health")
    local targetHealth = self.world:getComponent(target, "Health")
    
    if entityHealth and targetHealth then
        -- Both are living entities - ramming damage
        self:dealDamage(entity, target)
        self:dealDamage(target, entity)
    end
    
    -- If player attacked, end turn
    if isPlayer and self.events then
        self.events:emit("PlayerTurnEnd", {})
    end
end

function CombatSystem:checkCombat(entity, x, y)
    -- Use SpatialHash for efficient lookup
    local spatialHash = self.world:getSpatialHash()
    local entitiesAtPos = spatialHash:getAt(x, y)
    
    if not entitiesAtPos then
        return
    end
    
    -- Find other actors at the same position
    local entities = type(entitiesAtPos) == "table" and entitiesAtPos or {entitiesAtPos}
    for _, targetId in ipairs(entities) do
        if targetId ~= entity and self.world:hasComponent(targetId, "Health") then
            -- Combat!
            self:dealDamage(entity, targetId)
        end
    end
end

function CombatSystem:dealDamage(source, target)
    -- Use encapsulated access
    local targetHealth = self.world:getComponent(target, "Health")
    
    if not targetHealth then
        return
    end
    
    -- Base damage
    local damage = 1
    
    -- Check if source is player (could have weapons later)
    if self.world:hasComponent(source, "Player") then
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

return CombatSystem
