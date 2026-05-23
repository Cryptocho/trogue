-- Combat System
-- Handles collision-based combat (touch damage)
-- Now uses event-driven damage via RuleEngine

local CombatSystem = {
    priority = 2,
    name = "CombatSystem",
}

function CombatSystem:init(world)
    self.world = world
    self.events = world.eventBus

    if self.events then
        self.events:on("MoveSucceeded", function(data)
            self:onMoveSucceeded(data)
        end)

        self.events:on("CollisionDetected", function(data)
            self:onCollision(data)
        end)
    end
end

function CombatSystem:update(world, dt)
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

    -- If player moved, trigger enemy turn
    if isPlayer and self.events then
        self.events:emit("PlayerTurnEnd", {})
    end
end

-- When a collision is detected
function CombatSystem:onCollision(data)
    local entity = data.entity
    local target = data.target
    local isPlayer = data.isPlayer

    -- Only combat with actors (enemies), not walls or tiles
    local targetIsActor = self.world.components.Actor and self.world.components.Actor[target]

    if not targetIsActor then
        return  -- No combat with non-actors (walls, etc.)
    end

    -- Emit damage requests for both parties (ramming damage)
    if self.events then
        -- Entity damages target
        local damage = self:calcMeleeDamage(entity, true)
        self.events:emit("DamageRequest", {
            source = entity,
            target = target,
            effectId = "melee_attack",
            baseValue = damage,
            damageType = "physical",
        })

        -- Target damages entity
        damage = self:calcMeleeDamage(target, false)
        self.events:emit("DamageRequest", {
            source = target,
            target = entity,
            effectId = "melee_attack",
            baseValue = damage,
            damageType = "physical",
        })
    end

    -- If player attacked, end turn
    if isPlayer and self.events then
        self.events:emit("PlayerTurnEnd", {})
    end
end

function CombatSystem:checkCombat(entity, x, y)
    -- Find actors at the same position (enemies to fight)
    local entities = self.world:query({"Position", "Health", "Actor"})

    for _, result in ipairs(entities) do
        if result.id ~= entity then
            local pos = result.components.Position
            if pos and pos.x == x and pos.y == y then
                -- Combat! Emit damage request
                if self.events then
                    local damage = self:calcMeleeDamage(entity, self.world.components.Player[entity])
                    self.events:emit("DamageRequest", {
                        source = entity,
                        target = result.id,
                        effectId = "melee_attack",
                        baseValue = damage,
                        damageType = "physical",
                    })
                end
            end
        end
    end
end

-- Calculate melee damage based on source entity
function CombatSystem:calcMeleeDamage(source, isPlayer)
    -- Base damage
    local damage = 1

    -- Player deals more damage
    if isPlayer then
        damage = 2
    end

    -- TODO: Could add buffs, weapons, etc. here
    -- For now, just return base damage
    return damage
end

return CombatSystem