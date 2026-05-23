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
end

function CombatSystem:onMoveSucceeded(data)
    local entity = data.entity
    local x = data.x
    local y = data.y
    local isPlayer = data.isPlayer

    self:checkCombat(entity, x, y)

    if isPlayer and self.events then
        self.events:emit("PlayerTurnEnd", {})
    end
end

function CombatSystem:onCollision(data)
    local entity = data.entity
    local target = data.target
    local isPlayer = data.isPlayer

    local targetIsActor = self.world.components.Actor and self.world.components.Actor[target]

    if not targetIsActor then
        return
    end

    if self.events then
        local damage = self:calcMeleeDamage(entity, true)
        self.events:emit("DamageRequest", {
            source = entity,
            target = target,
            effectId = "melee_attack",
            baseValue = damage,
            damageType = "physical",
        })

        damage = self:calcMeleeDamage(target, false)
        self.events:emit("DamageRequest", {
            source = target,
            target = entity,
            effectId = "melee_attack",
            baseValue = damage,
            damageType = "physical",
        })
    end

    if isPlayer and self.events then
        self.events:emit("PlayerTurnEnd", {})
    end
end

function CombatSystem:checkCombat(entity, x, y)
    local entities = self.world:query({"Position", "Health", "Actor"})

    for _, result in ipairs(entities) do
        if result.id ~= entity then
            local pos = result.components.Position
            if pos and pos.x == x and pos.y == y then
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

function CombatSystem:calcMeleeDamage(source, isPlayer)
    local damage = 1

    if isPlayer then
        damage = 2
    end

    return damage
end

return CombatSystem