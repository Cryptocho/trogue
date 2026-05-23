-- AI System
-- Turn-based AI for enemy movement and abilities
-- Gameplay Rule Pipeline Layer

local Coordinates = require("src.core.coordinates")

local AISystem = {
    priority = 3,
    name = "AISystem",
}

local DIRECTIONS = {
    {dx = -1, dy = 0},
    {dx = 1, dy = 0},
    {dx = 0, dy = -1},
    {dx = 0, dy = 1},
}

function AISystem:init(world)
    self.world = world
    self.events = world.eventBus
    self.waitingForPlayerTurn = true
    self.ruleEngine = nil

    if self.events then
        self.events:on("PlayerTurnEnd", function()
            self.waitingForPlayerTurn = true
        end)
    end
end

function AISystem:setRuleEngine(ruleEngine)
    self.ruleEngine = ruleEngine
end

function AISystem:update(world, dt)
    if not self.waitingForPlayerTurn then
        return
    end

    self.waitingForPlayerTurn = false

    local actors = world:query({"Actor", "Position"})

    for _, result in ipairs(actors) do
        local entity = result.id

        if world.components.Player[entity] then
            goto continue
        end

        if self.ruleEngine then
            self:tryUseAbility(entity)
        end

        if math.random() < 0.7 then
            local dir = DIRECTIONS[math.random(#DIRECTIONS)]

            if self.events then
                self.events:emit("MoveAttempt", {
                    entity = entity,
                    dx = dir.dx,
                    dy = dir.dy,
                    isPlayer = false
                })
            end
        end

        ::continue::
    end

    if self.events then
        self.events:emit("TurnEnd", {})
    end
end

function AISystem:tryUseAbility(entityId)
    if not self.ruleEngine or not self.events then
        return
    end

    local abilityComp = self.world.components.Ability and self.world.components.Ability[entityId]
    if not abilityComp or not abilityComp.abilities or not next(abilityComp.abilities) then
        return
    end

    local abilitiesList = {}
    for abilityId, _ in pairs(abilityComp.abilities) do
        table.insert(abilitiesList, abilityId)
    end

    if #abilitiesList == 0 then
        return
    end

    local abilityId = abilitiesList[math.random(#abilitiesList)]

    local canUse, reason = self.ruleEngine:canUse(entityId, abilityId)

    if canUse then
        local pos = self.world.components.Position[entityId]
        if pos then
            local players = self.world:query({"Player", "Position"})
            for _, playerResult in ipairs(players) do
                local playerPos = playerResult.components.Position
                local dist = Coordinates.manhattanDistance(pos.x, pos.y, playerPos.x, playerPos.y)

                if dist <= 1 then
                    self.events:emit("AbilityUse", {
                        entity = entityId,
                        abilityId = abilityId,
                        targetId = playerResult.id,
                    })
                    return
                end
            end
        end
    end
end

return AISystem