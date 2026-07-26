-- AI System
-- Turn-based AI with vision detection, state machine, and pathfinding
-- Gameplay Rule Pipeline Layer

local Coordinates = require("src.core.coordinates")

local VISION_RANGE = 5  -- chebyshev distance (11x11 grid)
local ALERT_DELAY = 1   -- turns to wait after spotting player before chasing

local AISystem = {
    priority = 3,
    name = "AISystem",
}

function AISystem:init(world)
    self.world = world
    self.events = world.eventBus
    self.waitingForPlayerTurn = true
    self.ruleEngine = nil
    self.turnCount = 0

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
    self.turnCount = self.turnCount + 1

    local actors = world:query({"Actor", "Position", "AIState"})
    local players = world:query({"Player", "Position"})
    if #players == 0 then
        self:_endTurn()
        return
    end
    local playerPos = players[1].components.Position

    for _, result in ipairs(actors) do
        local entity = result.id
        if world.components.Player[entity] then
            goto continue
        end

        local pos = result.components.Position
        local aiState = result.components.AIState
        if not pos or not aiState then
            goto continue
        end

        local canSee = self:_canSeePlayer(pos.x, pos.y, playerPos.x, playerPos.y)
        local dist = Coordinates.chebyshevDistance(pos.x, pos.y, playerPos.x, playerPos.y)

        -- State transitions
        if aiState.state == "idle" then
            if canSee then
                aiState.state = "alerted"
                aiState.alertedTurn = self.turnCount
                aiState.targetX = playerPos.x
                aiState.targetY = playerPos.y
            end
        elseif aiState.state == "alerted" then
            if self.turnCount - aiState.alertedTurn >= ALERT_DELAY then
                aiState.state = "chasing"
            end
            if canSee then
                aiState.targetX = playerPos.x
                aiState.targetY = playerPos.y
            end
        elseif aiState.state == "chasing" then
            if canSee then
                aiState.targetX = playerPos.x
                aiState.targetY = playerPos.y
            end
            -- If lost sight and reached last known position, go idle
            if not canSee and pos.x == aiState.targetX and pos.y == aiState.targetY then
                aiState.state = "idle"
                aiState.targetX = nil
                aiState.targetY = nil
            end
        end

        -- Actions based on state
        if aiState.state == "idle" then
            self:_randomMove(entity)
        elseif aiState.state == "alerted" then
            -- Wait one turn (do nothing)
        elseif aiState.state == "chasing" then
            self:_chaseAndAttack(entity, pos, playerPos, dist)
        end

        ::continue::
    end

    self:_endTurn()
end

function AISystem:_canSeePlayer(ex, ey, px, py)
    local dist = Coordinates.chebyshevDistance(ex, ey, px, py)
    if dist > VISION_RANGE then
        return false
    end
    local mapRenderer = self.world:getSystem("MapRenderer")
    if not mapRenderer then
        return false
    end
    local function isSolid(x, y)
        return mapRenderer:isSolid(x, y)
    end
    return Coordinates.hasLineOfSight(ex, ey, px, py, isSolid)
end

function AISystem:_randomMove(entity)
    if math.random() < 0.7 then
        local dirs = {{dx=-1,dy=0},{dx=1,dy=0},{dx=0,dy=-1},{dx=0,dy=1}}
        local dir = dirs[math.random(#dirs)]
        if self.events then
            self.events:emit("MoveAttempt", {
                entity = entity,
                dx = dir.dx,
                dy = dir.dy,
                isPlayer = false
            })
        end
    end
end

function AISystem:_chaseAndAttack(entity, pos, playerPos, dist)
    -- Adjacent: attack
    if dist <= 1 then
        self:_tryAttack(entity, playerPos)
        return
    end

    -- Use A* to move toward player
    local mapRenderer = self.world:getSystem("MapRenderer")
    if not mapRenderer then return end

    local function isPassable(x, y)
        if not Coordinates.isInBounds(x, y, mapRenderer.width, mapRenderer.height) then
            return false
        end
        if mapRenderer:isSolid(x, y) then
            return false
        end
        return true
    end

    local function getBlockingEntity(x, y)
        local entities = self.world:getSpatialHash():getAt(x, y)
        if entities then
            for _, eid in ipairs(entities) do
                if eid ~= entity and self.world:hasComponent(eid, "Position") then
                    if self.world:hasComponent(eid, "Solid") or self.world:hasComponent(eid, "Actor") then
                        return eid
                    end
                end
            end
        end
        return nil
    end

    local path = Coordinates.findPath(pos.x, pos.y, playerPos.x, playerPos.y, isPassable, getBlockingEntity)
    if path and #path >= 2 then
        local nextTile = path[2]
        local dx = nextTile.x - pos.x
        local dy = nextTile.y - pos.y
        if self.events then
            self.events:emit("MoveAttempt", {
                entity = entity,
                dx = dx,
                dy = dy,
                isPlayer = false
            })
        end
    end
end

function AISystem:_tryAttack(entity, playerPos)
    if not self.ruleEngine or not self.events then return end

    local abilityComp = self.world.components.Ability and self.world.components.Ability[entity]
    if not abilityComp or not abilityComp.abilities or not next(abilityComp.abilities) then
        return
    end

    local abilitiesList = {}
    for abilityId, _ in pairs(abilityComp.abilities) do
        local abilityDef = self.ruleEngine:getAbilityDef(abilityId)
        if abilityDef and abilityDef.mode ~= "passive" then
            table.insert(abilitiesList, abilityId)
        end
    end
    if #abilitiesList == 0 then return end

    local abilityId = abilitiesList[math.random(#abilitiesList)]
    local canUse = self.ruleEngine:canUse(entity, abilityId)
    if canUse then
        self.events:emit("AbilityUse", {
            entity = entity,
            abilityId = abilityId,
            targetX = playerPos.x,
            targetY = playerPos.y,
        })
    end
end

function AISystem:_endTurn()
    if self.events then
        self.events:emit("TurnEnd", {})
    end
end

return AISystem
