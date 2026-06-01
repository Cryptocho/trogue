-- Coordinates Module
-- Unified coordinate system for tile/pixel conversions and distance calculations
-- 1-based tile coordinates (consistent with Lua tables)

local Config = require("src.config")
local TILE_SIZE = Config.TILE_SIZE
local MinHeap = require("src.utils.minheap")

local function tileToWorld(tx, ty)
    return (tx - 1) * TILE_SIZE, (ty - 1) * TILE_SIZE
end

local function worldToTile(wx, wy)
    return math.floor(wx / TILE_SIZE) + 1,
           math.floor(wy / TILE_SIZE) + 1
end

local function screenToTile(screenX, screenY, cameraX, cameraY, screenWidth, screenHeight, scale)
    local offsetX = screenWidth / 2 / scale - cameraX * TILE_SIZE - TILE_SIZE / 2
    local offsetY = screenHeight / 2 / scale - cameraY * TILE_SIZE - TILE_SIZE / 2
    return worldToTile(screenX / scale - offsetX, screenY / scale - offsetY)
end

local function tileToScreen(tx, ty, cameraX, cameraY, screenWidth, screenHeight, scale)
    local offsetX = screenWidth / 2 / scale - cameraX * TILE_SIZE - TILE_SIZE / 2
    local offsetY = screenHeight / 2 / scale - cameraY * TILE_SIZE - TILE_SIZE / 2
    local wx, wy = tileToWorld(tx, ty)
    return wx + offsetX, wy + offsetY
end

local function isInBounds(tx, ty, width, height)
    return tx >= 1 and tx <= width and ty >= 1 and ty <= height
end

local function manhattanDistance(tx1, ty1, tx2, ty2)
    return math.abs(tx2 - tx1) + math.abs(ty2 - ty1)
end

local function chebyshevDistance(tx1, ty1, tx2, ty2)
    return math.max(math.abs(tx2 - tx1), math.abs(ty2 - ty1))
end

local function euclideanDistance(tx1, ty1, tx2, ty2)
    return math.sqrt((tx2 - tx1)^2 + (ty2 - ty1)^2)
end

local function isInRange(tx1, ty1, tx2, ty2, range)
    return manhattanDistance(tx1, ty1, tx2, ty2) <= range
end



local function getNeighbors(tx, ty, width, height, diagonal)
    local neighbors = {}
    local dirs
    if diagonal then
        dirs = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}
    else
        dirs = {{0,-1},{0,1},{-1,0},{1,0}}
    end
    for _, d in ipairs(dirs) do
        local nx, ny = tx + d[1], ty + d[2]
        if isInBounds(nx, ny, width, height) then
            table.insert(neighbors, {x = nx, y = ny})
        end
    end
    return neighbors
end

local function diagonalCost(dx, dy)
    if dx ~= 0 and dy ~= 0 then return 1.414 end
    return 1
end

local function findPath(startX, startY, goalX, goalY, isPassable, getBlockingEntity)
    local openSet = MinHeap.createMinHeap()
    local openSetKeys = {}
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
        return chebyshevDistance(x1, y1, x2, y2)
    end

    local function getMoveCost(dx, dy)
        return diagonalCost(dx, dy)
    end

    local function nodeKey(x, y)
        return x .. "," .. y
    end

    local startKey = nodeKey(startX, startY)
    gScore[startKey] = 0
    fScore[startKey] = heuristic(startX, startY, goalX, goalY)
    openSet:push({x = startX, y = startY}, fScore[startKey])
    openSetKeys[startKey] = true

    local iterations = 0
    local maxIterations = 1000

    while not openSet:isEmpty() and iterations < maxIterations do
        iterations = iterations + 1

        local current = openSet:pop()
        local currentKey = nodeKey(current.x, current.y)
        openSetKeys[currentKey] = nil

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

            if not isPassable(neighborX, neighborY) then
                goto continue
            end

            local blockingEntity = getBlockingEntity and getBlockingEntity(neighborX, neighborY)
            if blockingEntity then
                goto continue
            end

            local moveCost = getMoveCost(dir.dx, dir.dy)
            local tentativeG = (gScore[currentKey] or math.huge) + moveCost

            local inOpen = openSetKeys[neighborKey]

            if not inOpen then
                openSetKeys[neighborKey] = true
            end

            if tentativeG < (gScore[neighborKey] or math.huge) then
                cameFrom[neighborKey] = current
                gScore[neighborKey] = tentativeG
                fScore[neighborKey] = tentativeG + heuristic(neighborX, neighborY, goalX, goalY)
                openSet:push({x = neighborX, y = neighborY}, fScore[neighborKey])
            end

            ::continue::
        end
    end

    return nil
end

-- Check if there is a clear line of sight between two tiles (Bresenham)
-- @param x1, y1 number: start tile
-- @param x2, y2 number: end tile
-- @param isSolid function(x, y) -> boolean: returns true if tile is solid
-- @return boolean: true if no solid tiles block the line (excluding start)
local function hasLineOfSight(x1, y1, x2, y2, isSolid)
    local dx = math.abs(x2 - x1)
    local dy = math.abs(y2 - y1)
    local sx = x1 < x2 and 1 or -1
    local sy = y1 < y2 and 1 or -1
    local err = dx - dy

    local x, y = x1, y1
    while x ~= x2 or y ~= y2 do
        local e2 = 2 * err
        if e2 > -dy then
            err = err - dy
            x = x + sx
        end
        if e2 < dx then
            err = err + dx
            y = y + sy
        end
        if (x ~= x2 or y ~= y2) and isSolid(x, y) then
            return false
        end
    end
    return true
end

return {
    TILE_SIZE = TILE_SIZE,
    tileToWorld = tileToWorld,
    worldToTile = worldToTile,
    screenToTile = screenToTile,
    tileToScreen = tileToScreen,
    isInBounds = isInBounds,
    manhattanDistance = manhattanDistance,
    chebyshevDistance = chebyshevDistance,
    euclideanDistance = euclideanDistance,
    isInRange = isInRange,
    hasLineOfSight = hasLineOfSight,
    getNeighbors = getNeighbors,
    findPath = findPath,
    diagonalCost = diagonalCost,
}