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
    aimMode = false,
    pendingAbilityId = nil,
    showInventoryUI = false,
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

    if self.showInventoryUI then
        if key == "escape" or key == "i" then
            self:toggleInventoryUI()
        end
        return
    end

    if key == "i" then
        self:toggleInventoryUI()
        return
    end

    if key == "p" then
        self:handlePickup()
        return
    end

    if self.turnSystem and not self.turnSystem:isInputAllowed() then
        return
    end

    if self.aimMode then
        local abilityId = self.KEY_ABILITIES[key]
        if abilityId then
            if abilityId == self.pendingAbilityId then
                self:cancelAim()
            else
                self.pendingAbilityId = abilityId
                if game then
                    game.selectedAbility = abilityId
                end
            end
        end
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

function InputSystem:toggleInventoryUI()
    self.showInventoryUI = not self.showInventoryUI
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

-- Handle ability usage (enter aim mode)
-- @param abilityId string
function InputSystem:handleAbility(abilityId)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end

    local playerId = players[1].id

    if self.ruleEngine then
        local canUse, reason = self.ruleEngine:canUse(playerId, abilityId)
        if not canUse then
            print("Cannot use: " .. reason)
            return
        end
    end

    self.aimMode = true
    self.pendingAbilityId = abilityId
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

    local worldX, worldY = self:_screenToWorldTile(x, y)
    if not worldX then
        return
    end

    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end

    local playerId = players[1].id
    local playerPos = players[1].components.Position

    local mapRenderer = self:_getMapRenderer()
    if not mapRenderer then
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
            if Coordinates.canDiagonalMove(playerPos.x, playerPos.y, dx, dy, function(tx, ty)
                local movementSystem = self.world:getSystem("MovementSystem")
                return movementSystem and movementSystem:isBlocked(tx, ty)
            end) then
                moveDx = dx
                moveDy = dy
            else
                return
            end
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

-- Handle aim click (release ability at target tile)
-- @param x number: screen x position
-- @param y number: screen y position
function InputSystem:handleAimClick(x, y)
    if not self.aimMode or not self.pendingAbilityId then
        return
    end

    if not self.enabled then return end
    if self.turnSystem and not self.turnSystem:isInputAllowed() then return end

    local tileX, tileY = self:_screenToWorldTile(x, y)
    if not tileX then
        self:cancelAim()
        return
    end

    local mapRenderer = self:_getMapRenderer()
    if mapRenderer and mapRenderer:isSolid(tileX, tileY) then
        return
    end

    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return end
    local playerId = players[1].id
    local playerPos = players[1].components.Position

    if mapRenderer and not Coordinates.hasLineOfSight(playerPos.x, playerPos.y, tileX, tileY, function(x, y) return mapRenderer:isSolid(x, y) end) then
        return
    end

    if self.ruleEngine then
        local abilityDef = self.ruleEngine:getAbilityDef(self.pendingAbilityId)
        if abilityDef then
            local spatialHash = self.world:getSpatialHash()

            -- Check range: is the clicked tile within casting range?
            if abilityDef.rangeFunc then
                local rangeTiles = abilityDef.rangeFunc(playerPos.x, playerPos.y, tileX, tileY, mapRenderer.width, mapRenderer.height)
                local inRange = false
                for _, rt in ipairs(rangeTiles) do
                    if rt.x == tileX and rt.y == tileY then
                        inRange = true
                        break
                    end
                end
                if not inRange then
                    self:cancelAim()
                    return
                end
            end

            -- Check effect area for valid targets
            local hasTarget = false
            if abilityDef.effectAreaFunc then
                local effectTiles = abilityDef.effectAreaFunc(playerPos.x, playerPos.y, tileX, tileY, mapRenderer.width, mapRenderer.height)
                for _, tile in ipairs(effectTiles) do
                    local entities = spatialHash:getAt(tile.x, tile.y)
                    if entities and #entities > 0 then
                        hasTarget = true
                        break
                    end
                end
            end

            if not hasTarget then
                self:cancelAim()
                return
            end
        end
    end

    local abilityId = self.pendingAbilityId
    self:cancelAim()

    if self.turnSystem then
        self.turnSystem:startTurn()
    end

    if self.events then
        self.events:emit("AbilityUse", {
            entity = playerId,
            abilityId = abilityId,
            targetX = tileX,
            targetY = tileY,
        })
    end
end

-- Cancel aim mode
function InputSystem:cancelAim()
    self.aimMode = false
    self.pendingAbilityId = nil
end

-- Check if currently in aim mode
function InputSystem:isInAimMode()
    return self.aimMode
end

-- Toggle inventory UI
function InputSystem:toggleInventoryUI()
    self.showInventoryUI = not self.showInventoryUI
end

-- Check if inventory UI is open
function InputSystem:isInventoryUIOpen()
    return self.showInventoryUI
end

-- Get pending ability in aim mode
function InputSystem:getPendingAbility()
    return self.pendingAbilityId
end

-- Convert screen coordinates to world tile coordinates
-- Returns tileX, tileY or nil, nil (if out of bounds or no player/map)
function InputSystem:_screenToWorldTile(screenX, screenY)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return nil, nil end
    local playerPos = players[1].components.Position

    local mapRenderer = self:_getMapRenderer()
    if not mapRenderer then return nil, nil end

    local Config = require("src.config")
    local cameraX = playerPos.x
    local cameraY = playerPos.y
    local screenW = love.graphics.getWidth()
    local screenH = love.graphics.getHeight()

    local tileX, tileY = Coordinates.screenToTile(screenX, screenY, cameraX, cameraY,
        screenW, screenH, Config.SCALE)

    if not Coordinates.isInBounds(tileX, tileY, mapRenderer.width, mapRenderer.height) then
        return nil, nil
    end

    return tileX, tileY
end

-- Get MapRenderer system (lazy cached)
function InputSystem:_getMapRenderer()
    if not self._mapRenderer then
        self._mapRenderer = self.world:getSystem("MapRenderer")
    end
    return self._mapRenderer
end

function InputSystem:handlePickup()
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return end

    local playerId = players[1].id
    local playerPos = players[1].components.Position

    local spatialHash = self.world:getSpatialHash()
    local entities = spatialHash:getAt(playerPos.x, playerPos.y)
    if not entities or #entities == 0 then return end

    local hasItem = false
    for _, eid in ipairs(entities) do
        local invItem = self.world:getComponent(eid, "InventoryItem")
        if invItem then hasItem = true; break end
    end

    if hasItem and self.events then
        self.events:emit("PickupRequest", {
            entity = playerId,
            targetX = playerPos.x,
            targetY = playerPos.y,
        })
    end
end

return InputSystem
