-- Input System
-- Unified handling of all player input (movement, abilities)
-- Architecture: main.lua love.keypressed delegates to this system

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
    
    local offsetX = screenWidth / 2 / Config.SCALE - cameraX * Config.TILE_SIZE - Config.TILE_SIZE / 2
    local offsetY = screenHeight / 2 / Config.SCALE - cameraY * Config.TILE_SIZE - Config.TILE_SIZE / 2
    
    local worldX = math.floor((x / Config.SCALE - offsetX) / Config.TILE_SIZE) + 1
    local worldY = math.floor((y / Config.SCALE - offsetY) / Config.TILE_SIZE) + 1
    
    
    
    if worldX < 1 or worldY < 1 or worldX > mapRenderer.width or worldY > mapRenderer.height then
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
    local openSet = {}
    local closedSet = {}
    local cameFrom = {}
    local gScore = {}
    local fScore = {}

    local directions = {
        {dx = 0, dy = -1},
        {dx = 0, dy = 1},
        {dx = -1, dy = 0},
        {dx = 1, dy = 0},
        {dx = -1, dy = -1},
        {dx = 1, dy = -1},
        {dx = -1, dy = 1},
        {dx = 1, dy = 1},
    }

    local function heuristic(x1, y1, x2, y2)
        return math.max(math.abs(x1 - x2), math.abs(y1 - y2))
    end

    local function getMoveCost(dx, dy)
        if dx ~= 0 and dy ~= 0 then
            return 1.414
        end
        return 1
    end

    local function nodeKey(x, y)
        return x .. "," .. y
    end

    local startKey = nodeKey(startX, startY)
    gScore[startKey] = 0
    fScore[startKey] = heuristic(startX, startY, goalX, goalY)
    table.insert(openSet, {x = startX, y = startY})

    local iterations = 0
    local maxIterations = 1000

    while #openSet > 0 and iterations < maxIterations do
        iterations = iterations + 1

        table.sort(openSet, function(a, b)
            local fA = fScore[nodeKey(a.x, a.y)] or math.huge
            local fB = fScore[nodeKey(b.x, b.y)] or math.huge
            return fA < fB
        end)

        local current = table.remove(openSet, 1)
        local currentKey = nodeKey(current.x, current.y)

        if current.x == goalX and current.y == goalY then
            local path = {}
            local curr = current
            while curr do
                table.insert(path, 1, curr)
                curr = cameFrom[nodeKey(curr.x, curr.y)]
            end
            return path
        end

        closedSet[currentKey] = true

        for _, dir in ipairs(directions) do
            local neighborX = current.x + dir.dx
            local neighborY = current.y + dir.dy
            local neighborKey = nodeKey(neighborX, neighborY)

            if closedSet[neighborKey] then
                goto continue
            end

            if neighborX < 1 or neighborY < 1 or neighborX > mapRenderer.width or neighborY > mapRenderer.height then
                goto continue
            end

            if mapRenderer:isSolid(neighborX, neighborY) then
                goto continue
            end

            local blockingEntity = self:getEntityAt(neighborX, neighborY, excludeEntity)
            if blockingEntity then
                goto continue
            end

            local moveCost = getMoveCost(dir.dx, dir.dy)
            local tentativeG = (gScore[currentKey] or math.huge) + moveCost

            local inOpen = false
            for _, node in ipairs(openSet) do
                if node.x == neighborX and node.y == neighborY then
                    inOpen = true
                    break
                end
            end

            if not inOpen then
                table.insert(openSet, {x = neighborX, y = neighborY})
            end

            if tentativeG < (gScore[neighborKey] or math.huge) then
                cameFrom[neighborKey] = current
                gScore[neighborKey] = tentativeG
                fScore[neighborKey] = tentativeG + heuristic(neighborX, neighborY, goalX, goalY)
            end

            ::continue::
        end
    end

return nil
end

return InputSystem