-- MapRenderer System
-- Renders static map tiles from 2D array instead of ECS entities

local Config = require("src.config")
local Coordinates = require("src.core.coordinates")

local MapRenderer = {
    priority = 0,  -- Run before RenderSystem
    name = "MapRenderer",

    -- 2D tile array
    tiles = {},  -- tiles[y][x] = tileIndex
    width = 0,
    height = 0,

    -- Graphics resources
    tileset = nil,
    quads = {},
}

function MapRenderer:init(world)
    self.world = world

    -- Load tileset image
    self.tileset = love.graphics.newImage("assets/tileset.png")

    -- Pre-create quads for each tile
    for i = 0, 8 do
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
            local tileIndex = 0  -- default floor

            if char == "#" then
                tileIndex = 1  -- wall
            elseif char == "^" then
                tileIndex = 8
            end

            self.tiles[y][x] = tileIndex
        end
    end
end

function MapRenderer:update(world, dt)
    -- TODO: Dynamic map support
    -- TBD
    -- Dynamically modify map (e.g. open doors, destroy walls), update tiles array here (or as entities?)
end

function MapRenderer:isSolid(x, y)
    if not Coordinates.isInBounds(x, y, self.width, self.height) then
        return false
    end
    return self.tiles[y][x] == 1 or self.tiles[y][x] == 8
end

function MapRenderer:draw(cameraX, cameraY, offsetX, offsetY)
    -- Calculate visible area based on camera position
    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()

    -- Visible tile range
    local viewWidth = screenWidth / SCALE / Config.TILE_SIZE
    local viewHeight = screenHeight / SCALE / Config.TILE_SIZE

    local startX = math.max(1, math.floor(cameraX - viewWidth / 2))
    local endX = math.min(self.width, math.ceil(cameraX + viewWidth / 2))
    local startY = math.max(1, math.floor(cameraY - viewHeight / 2))
    local endY = math.min(self.height, math.ceil(cameraY + viewHeight / 2))

    -- Draw only visible tiles
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