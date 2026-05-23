-- MapRenderer System
-- Renders static map tiles from 2D array instead of ECS entities

local Config = require("src.config")
local Coordinates = require("src.core.coordinates")

local MapRenderer = {
    priority = 0,
    name = "MapRenderer",

    tiles = {},
    width = 0,
    height = 0,

    tileset = nil,
    quads = {},
}

function MapRenderer:init(world)
    self.world = world

    self.tileset = love.graphics.newImage("assets/tileset.png")

    for i = 0, 7 do
        local tx = (i % Config.TILES_PER_ROW) * Config.TILE_SIZE
        local ty = math.floor(i / Config.TILES_PER_ROW) * Config.TILE_SIZE
        self.quads[i] = love.graphics.newQuad(tx, ty, Config.TILE_SIZE, Config.TILE_SIZE,
                                              self.tileset:getDimensions())
    end
end

function MapRenderer:loadMap(mapData)
    self.height = #mapData
    self.width = #mapData[1]

    for y, row in ipairs(mapData) do
        self.tiles[y] = {}
        for x = 1, #row do
            local char = row:sub(x, x)
            local tileIndex = 0

            if char == "#" then
                tileIndex = 1
            end

            self.tiles[y][x] = tileIndex
        end
    end
end

function MapRenderer:update(world, dt)
end

function MapRenderer:isSolid(x, y)
    if not Coordinates.isInBounds(x, y, self.width, self.height) then
        return false
    end
    return self.tiles[y][x] == 1
end

function MapRenderer:draw(cameraX, cameraY, offsetX, offsetY)
    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()

    local viewWidth = screenWidth / SCALE / Config.TILE_SIZE
    local viewHeight = screenHeight / SCALE / Config.TILE_SIZE

    local startX = math.max(1, math.floor(cameraX - viewWidth / 2))
    local endX = math.min(self.width, math.ceil(cameraX + viewWidth / 2))
    local startY = math.max(1, math.floor(cameraY - viewHeight / 2))
    local endY = math.min(self.height, math.ceil(cameraY + viewHeight / 2))

    for y = startY, endY do
        for x = startX, endX do
            local tileIndex = self.tiles[y][x]
            local quad = self.quads[tileIndex]

            if quad then
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, SCALE)
                love.graphics.draw(self.tileset, quad, screenX, screenY)
            end
        end
    end
end

return MapRenderer