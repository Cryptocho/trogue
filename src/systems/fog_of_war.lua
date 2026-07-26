-- FogOfWar System
-- Manages visibility and exploration state for fog of war rendering

local Coordinates = require("src.core.coordinates")

local FogOfWar = {
    priority = 0,
    name = "FogOfWar",

    explored = {},      -- explored[y][x] = true/false
    visible = {},       -- visible[y][x] = true/false (current frame)
    width = 0,
    height = 0,
    viewRange = 7,      -- chebyshev distance view range
}

function FogOfWar:init(world)
    self.world = world
end

function FogOfWar:loadMap(width, height)
    self.width = width
    self.height = height
    self.explored = {}
    self.visible = {}
    for y = 1, height do
        self.explored[y] = {}
        self.visible[y] = {}
        for x = 1, width do
            self.explored[y][x] = false
            self.visible[y][x] = false
        end
    end
end

function FogOfWar:update(world, dt)
    -- Skip if map not loaded
    if self.width == 0 or self.height == 0 then return end

    -- Clear current visibility
    for y = 1, self.height do
        for x = 1, self.width do
            self.visible[y][x] = false
        end
    end

    -- Get player position
    local players = world:query({"Player", "Position"})
    if #players == 0 then return end

    local playerPos = players[1].components.Position
    if not playerPos or not playerPos.x or not playerPos.y then return end
    local px, py = playerPos.x, playerPos.y

    -- Update visible area (15x15 grid centered on player)
    local minY = math.max(1, py - self.viewRange)
    local maxY = math.min(self.height, py + self.viewRange)
    local minX = math.max(1, px - self.viewRange)
    local maxX = math.min(self.width, px + self.viewRange)

    for y = minY, maxY do
        for x = minX, maxX do
            if Coordinates.chebyshevDistance(px, py, x, y) <= self.viewRange then
                self.visible[y][x] = true
                self.explored[y][x] = true
            end
        end
    end
end

function FogOfWar:isVisible(x, y)
    if not x or not y then return false end
    if not Coordinates.isInBounds(x, y, self.width, self.height) then
        return false
    end
    if not self.visible[y] then return false end
    return self.visible[y][x] or false
end

function FogOfWar:isExplored(x, y)
    if not x or not y then return false end
    if not Coordinates.isInBounds(x, y, self.width, self.height) then
        return false
    end
    if not self.explored[y] then return false end
    return self.explored[y][x] or false
end

-- Returns fog alpha: 0=visible, 0.5=explored, 1.0=unexplored
function FogOfWar:getFogAlpha(x, y)
    if self:isVisible(x, y) then
        return 0
    elseif self:isExplored(x, y) then
        return 0.5
    else
        return 1.0
    end
end

return FogOfWar
