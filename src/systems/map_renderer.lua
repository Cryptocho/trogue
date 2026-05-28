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
    floorImage = nil,   -- image.png used as tiled floor texture
    quads = {},
}

function MapRenderer:init(world)
    self.world = world

    -- Load tileset for non-floor tiles (walls, traps, etc.)
    self.tileset = love.graphics.newImage("assets/tileset.png")
    self.tileset:setFilter("nearest", "nearest")

    -- Load image.png as tiled floor texture
    self.floorImage = love.graphics.newImage("assets/pixel-set-library/dungen-tile/image.png")
    self.floorImage:setFilter("nearest", "nearest")
    local imgW, imgH = self.floorImage:getDimensions()
    -- Scale floor image so it fits within a single tile cell without stretching
    self.floorScale = math.min(Config.TILE_SIZE / imgW, Config.TILE_SIZE / imgH)
    -- Centered offset within each tile cell
    self.floorDrawW = imgW * self.floorScale
    self.floorDrawH = imgH * self.floorScale
    self.floorOffsetX = (Config.TILE_SIZE - self.floorDrawW) / 2
    self.floorOffsetY = (Config.TILE_SIZE - self.floorDrawH) / 2

    -- Pre-create quads for each tile (walls, traps use tileset)
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
            local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                screenWidth, screenHeight, SCALE)
            screenX = math.floor(screenX)
            screenY = math.floor(screenY)

            if tileIndex == 0 then
                -- Floor: draw image.png tiled, centered within the tile cell
                love.graphics.draw(self.floorImage, screenX + self.floorOffsetX, screenY + self.floorOffsetY,
                                   0, self.floorScale, self.floorScale)
            else
                -- Walls, traps, etc.: draw from tileset quad
                local quad = self.quads[tileIndex]
                if quad then
                    love.graphics.draw(self.tileset, quad, screenX, screenY)
                end
            end
        end
    end
end

return MapRenderer