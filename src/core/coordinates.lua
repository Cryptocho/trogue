-- Coordinates Module
-- Unified coordinate system for tile/pixel conversions and distance calculations
-- 1-based tile coordinates (consistent with Lua tables)

local TILE_SIZE = 16
local origin = "top-left"

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

local function isInArea(tx1, ty1, tx2, ty2, radius)
    return euclideanDistance(tx1, ty1, tx2, ty2) <= radius
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

            if not isPassable(neighborX, neighborY) then
                goto continue
            end

            local blockingEntity = getBlockingEntity and getBlockingEntity(neighborX, neighborY)
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
    isInArea = isInArea,
    getNeighbors = getNeighbors,
    findPath = findPath,
    diagonalCost = diagonalCost,
}