-- Input System
-- Unified handling of all player input (movement, abilities)
-- Architecture: main.lua love.keypressed delegates to this system

local Coordinates = require("src.core.coordinates")

local InputSystem = {
    priority = 0,
    name = "InputSystem",

    KEY_MOVEMENTS = {
        left = {dx = -1, dy = 0},
        right = {dx = 1, dy = 0},
        up = {dx = 0, dy = -1},
        down = {dx = 0, dy = 1},
        a = {dx = -1, dy = 0},
        d = {dx = 1, dy = 0},
        w = {dx = 0, dy = -1},
        s = {dx = 0, dy = 1},
        q = {dx = -1, dy = -1},
        e = {dx = 1, dy = -1},
        z = {dx = -1, dy = 1},
        c = {dx = 1, dy = 1},
    },

    KEY_ABILITIES = {
        ["1"] = "punch",
        ["2"] = "heal",
        ["3"] = "shield",
        ["4"] = "fireball",
    },

    enabled = true,
    pendingClickTarget = nil,
    keyBuffer = {},
    keyBufferWindow = 0.18,
    keyBufferTimer = 0,
}

function InputSystem:init(world, config)
    self.world = world
    self.events = world.eventBus
    self.turnSystem = nil
    self.ruleEngine = nil

    -- Enable system
    self.enabled = true

    -- Listen for AbilityUsed to end turn after successful ability use
    if self.events then
        self.events:on("AbilityUsed", function(data)
            if data and data.entity then
                -- Check if it's the player who used the ability
                local players = self.world:query({"Player"})
                for _, player in ipairs(players) do
                    if player.id == data.entity then
                        self.events:emit("PlayerTurnEnd", {})
                        break
                    end
                end
            end
        end)
    end
end

function InputSystem:update(world, dt)
    if self.keyBufferTimer > 0 then
        self.keyBufferTimer = self.keyBufferTimer - dt
        if self.keyBufferTimer <= 0 then
            self:processKeyBuffer()
        end
    end
end

-- Set system references (called by main.lua after initGameWorld)
function InputSystem:setTurnSystem(turnSystem)
    self.turnSystem = turnSystem
end

function InputSystem:setRuleEngine(ruleEngine)
    self.ruleEngine = ruleEngine
end

function InputSystem:setEnabled(enabled)
    self.enabled = enabled
end

function InputSystem:isEnabled()
    return self.enabled
end

-- Main entry point: called from main.lua love.keypressed
-- @param key string
-- @param scancode string
-- @param isrepeat boolean
function InputSystem:handleKey(key, scancode, isrepeat)
    if not self.enabled then
        return
    end

    if self.turnSystem and not self.turnSystem:isInputAllowed() then
        return
    end

    local movement = self.KEY_MOVEMENTS[key]
    if movement then
        self:addToKeyBuffer(movement)
        return
    end

    local abilityId = self.KEY_ABILITIES[key]
    if abilityId then
        if game then
            game.selectedAbility = abilityId
        end
        self:handleAbility(abilityId)
        return
    end

    if key == "space" then
        self:handleWait()
        return
    end
end

function InputSystem:addToKeyBuffer(movement)
    local isDiagonal = (movement.dx ~= 0 and movement.dy ~= 0)

    if isDiagonal then
        self:flushKeyBuffer()
        self:handleMove(movement)
        return
    end

    table.insert(self.keyBuffer, movement)
    self.keyBufferTimer = self.keyBufferWindow

    if #self.keyBuffer >= 2 then
        self:processKeyBuffer()
    end
end

function InputSystem:flushKeyBuffer()
    self.keyBuffer = {}
    self.keyBufferTimer = 0
end

local function clampDiagonal(dx, dy)
    local clampedDx = math.max(-1, math.min(1, dx))
    local clampedDy = math.max(-1, math.min(1, dy))
    return clampedDx, clampedDy
end

function InputSystem:processKeyBuffer()
    if #self.keyBuffer == 0 then
        return
    end

    local first = self.keyBuffer[1]
    local second = self.keyBuffer[2]

    self:flushKeyBuffer()

    if first and second then
        local combinedDx = first.dx + second.dx
        local combinedDy = first.dy + second.dy
        combinedDx, combinedDy = clampDiagonal(combinedDx, combinedDy)

        if combinedDx ~= 0 or combinedDy ~= 0 then
            self:handleMove({dx = combinedDx, dy = combinedDy})
        elseif first then
            self:handleMove(first)
        end
    elseif first then
        self:handleMove(first)
    end
end

-- Handle movement input
-- @param movement table: {dx, dy}
function InputSystem:handleMove(movement)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end

    local playerId = players[1].id

    -- Notify turn system to start turn
    if self.turnSystem then
        self.turnSystem:startTurn()
    end

    -- Emit move attempt event
    if self.events then
        self.events:emit("MoveAttempt", {
            entity = playerId,
            dx = movement.dx,
            dy = movement.dy,
            isPlayer = true
        })
    end
end

-- Handle ability usage
-- @param abilityId string
function InputSystem:handleAbility(abilityId)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end

    local playerId = players[1].id

    -- Check if ability is usable
    if self.ruleEngine then
        local canUse, reason = self.ruleEngine:canUse(playerId, abilityId)
        if not canUse then
            print("Cannot use: " .. reason)
            return
        end
    end

    -- Notify turn system to start turn
    if self.turnSystem then
        self.turnSystem:startTurn()
    end

    -- Emit ability use event (auto-select target)
    -- PlayerTurnEnd will be emitted in AbilityUsed event handler
    if self.events then
        self.events:emit("AbilityUse", {
            entity = playerId,
            abilityId = abilityId,
            targetId = nil  -- RuleEngine will auto-select target
        })
    end
end

-- Handle wait (skip turn)
function InputSystem:handleWait()
    if self.turnSystem then
        self.turnSystem:startTurn()
        self.turnSystem:endPlayerTurn()
    end
end

-- Handle mouse click for movement
-- @param x number: screen x position
-- @param y number: screen y position
function InputSystem:handleClick(x, y)
    if not self.enabled then
        return
    end

    if self.turnSystem and not self.turnSystem:isInputAllowed() then
        return
    end

    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end

    local playerId = players[1].id
    local playerPos = players[1].components.Position

    local mapRenderer = nil
    for _, sys in ipairs(self.world.systems) do
        if sys.name == "MapRenderer" then
            mapRenderer = sys
            break
        end
    end

    if not mapRenderer then
        return
    end

    local Config = require("src.config")
    local cameraX = playerPos.x
    local cameraY = playerPos.y
    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()

    local worldX, worldY = Coordinates.screenToTile(x, y, cameraX, cameraY,
        screenWidth, screenHeight, Config.SCALE)

    if not Coordinates.isInBounds(worldX, worldY, mapRenderer.width, mapRenderer.height) then
        return
    end

    if mapRenderer:isSolid(worldX, worldY) then
        return
    end

    local targetEntity = self:getEntityAt(worldX, worldY, playerId)
    if targetEntity then
        return
    end

    local dx = worldX - playerPos.x
    local dy = worldY - playerPos.y
    local distance = math.max(math.abs(dx), math.abs(dy))

    if self.turnSystem then
        self.turnSystem:startTurn()
    end

    local moveDx, moveDy
    local isDiagonalMove = (dx ~= 0 and dy ~= 0)

    if distance > 1 then
        local path = self:findPath(playerPos.x, playerPos.y, worldX, worldY, playerId, mapRenderer)
        if path and #path > 1 then
            local firstStep = path[2]
            moveDx = firstStep.x - playerPos.x
            moveDy = firstStep.y - playerPos.y
        else
            return
        end
    elseif isDiagonalMove then
        local targetX = playerPos.x + dx
        local targetY = playerPos.y + dy
        if not mapRenderer:isSolid(targetX, targetY) and not self:getEntityAt(targetX, targetY, playerId) then
            moveDx = dx
            moveDy = dy
        else
            return
        end
    else
        moveDx = dx
        moveDy = dy
    end

    if self.events then
        self.events:emit("MoveAttempt", {
            entity = playerId,
            dx = moveDx,
            dy = moveDy,
            isPlayer = true
        })
    end
end

-- Get entity at position (excluding player)
function InputSystem:getEntityAt(x, y, excludeEntity)
    local entities = self.world:query({"Position", "Actor"})
    for _, result in ipairs(entities) do
        if result.id ~= excludeEntity then
            local pos = result.components.Position
            if pos and pos.x == x and pos.y == y then
                return result.id
            end
        end
    end
    return nil
end

-- A* pathfinding
function InputSystem:findPath(startX, startY, goalX, goalY, excludeEntity, mapRenderer)
    local function isPassable(tx, ty)
        if not Coordinates.isInBounds(tx, ty, mapRenderer.width, mapRenderer.height) then
            return false
        end
        return not mapRenderer:isSolid(tx, ty)
    end

    local function getBlockingEntity(tx, ty)
        return self:getEntityAt(tx, ty, excludeEntity)
    end

    return Coordinates.findPath(startX, startY, goalX, goalY, isPassable, getBlockingEntity)
end

return InputSystem