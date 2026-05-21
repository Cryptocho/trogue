-- Coordinates Module
-- Unified coordinate system for tile/pixel conversions and distance calculations
-- 1-based tile coordinates (consistent with Lua tables)

local Coordinates = {
    TILE_SIZE = 16,
    origin = "top-left",
}

function Coordinates:tileToWorld(tx, ty)
    return (tx - 1) * self.TILE_SIZE, (ty - 1) * self.TILE_SIZE
end

function Coordinates:worldToTile(wx, wy)
    return math.floor(wx / self.TILE_SIZE) + 1,
           math.floor(wy / self.TILE_SIZE) + 1
end

function Coordinates:screenToTile(screenX, screenY, cameraX, cameraY, screenWidth, screenHeight, scale)
    local offsetX = screenWidth / 2 / scale - cameraX * self.TILE_SIZE - self.TILE_SIZE / 2
    local offsetY = screenHeight / 2 / scale - cameraY * self.TILE_SIZE - self.TILE_SIZE / 2
    return self:worldToTile(screenX / scale - offsetX, screenY / scale - offsetY)
end

function Coordinates:tileToScreen(tx, ty, cameraX, cameraY, screenWidth, screenHeight, scale)
    local offsetX = screenWidth / 2 / scale - cameraX * self.TILE_SIZE - self.TILE_SIZE / 2
    local offsetY = screenHeight / 2 / scale - cameraY * self.TILE_SIZE - self.TILE_SIZE / 2
    local wx, wy = self:tileToWorld(tx, ty)
    return wx + offsetX, wy + offsetY
end

function Coordinates:isInBounds(tx, ty, width, height)
    return tx >= 1 and tx <= width and ty >= 1 and ty <= height
end

function Coordinates:manhattanDistance(tx1, ty1, tx2, ty2)
    return math.abs(tx2 - tx1) + math.abs(ty2 - ty1)
end

function Coordinates:chebyshevDistance(tx1, ty1, tx2, ty2)
    return math.max(math.abs(tx2 - tx1), math.abs(ty2 - ty1))
end

function Coordinates:euclideanDistance(tx1, ty1, tx2, ty2)
    return math.sqrt((tx2 - tx1)^2 + (ty2 - ty1)^2)
end

function Coordinates:isInRange(tx1, ty1, tx2, ty2, range)
    return self:manhattanDistance(tx1, ty1, tx2, ty2) <= range
end

function Coordinates:isInArea(tx1, ty1, tx2, ty2, radius)
    return self:euclideanDistance(tx1, ty1, tx2, ty2) <= radius
end

function Coordinates:getNeighbors(tx, ty, width, height, diagonal)
    local neighbors = {}
    local dirs
    if diagonal then
        dirs = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}
    else
        dirs = {{0,-1},{0,1},{-1,0},{1,0}}
    end
    for _, d in ipairs(dirs) do
        local nx, ny = tx + d[1], ty + d[2]
        if self:isInBounds(nx, ny, width, height) then
            table.insert(neighbors, {x = nx, y = ny})
        end
    end
    return neighbors
end

function Coordinates:findPath(startX, startY, goalX, goalY, isPassable, getBlockingEntity, getEntityAt)
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
        return self:chebyshevDistance(x1, y1, x2, y2)
    end

    local function getMoveCost(dx, dy)
        return self:diagonalCost(dx, dy)
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

function Coordinates:diagonalCost(dx, dy)
    if dx ~= 0 and dy ~= 0 then return 1.414 end
    return 1
end

return Coordinates