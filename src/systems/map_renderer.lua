-- MapRenderer System
-- Renders static map tiles from 2D array instead of ECS entities

local Config = require("src.config")
local Coordinates = require("src.core.coordinates")
local Autotile = require("src.utils.autotile")

local TILE_FLOOR = 0
local TILE_WALL = 1
local TILE_TREE = 8

local MapRenderer = {
    priority = 0,  -- Run before RenderSystem
    name = "MapRenderer",

    -- 2D tile array
    tiles = {},  -- tiles[y][x] = tileIndex
    width = 0,
    height = 0,

    -- Graphics resources
    tileset = nil,
    floorTilesetImage = nil,
    floorQuads = {},
    floorBitmasks = {},
    quads = {},
}

function MapRenderer:init(world)
    self.world = world

    -- Load tileset for non-floor tiles (walls, traps, etc.)
    self.tileset = love.graphics.newImage("assets/tileset.png")
    self.tileset:setFilter("nearest", "nearest")

    -- Load floor tileset for autotile rendering
    self.floorTilesetImage = love.graphics.newImage("assets/pixel-set-library/dungen-tile/Tile Set.png")
    self.floorTilesetImage:setFilter("nearest", "nearest")
    local tileset = require("assets.pixel-set-library.dungen-tile.tileset")
    self.floorQuads = Autotile.buildQuads(tileset, self.floorTilesetImage:getDimensions())

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
            local tileIndex = TILE_FLOOR  -- default floor

            if char == "#" then
                tileIndex = TILE_WALL  -- wall
            elseif char == "^" then
                tileIndex = TILE_TREE
            end

            self.tiles[y][x] = tileIndex
        end
    end

    self.floorBitmasks = {}
    local matchFn = function(px, py)
        if px < 1 or px > self.width or py < 1 or py > self.height then
            return false
        end
        return self.tiles[py][px] == TILE_FLOOR
    end

    for y = 1, self.height do
        self.floorBitmasks[y] = {}
        for x = 1, self.width do
            if self.tiles[y][x] == TILE_FLOOR then
                self.floorBitmasks[y][x] = Autotile.computeBitmask(x, y, matchFn)
            end
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
    return self.tiles[y][x] == TILE_WALL or self.tiles[y][x] == TILE_TREE
end

function MapRenderer:draw(cameraX, cameraY, offsetX, offsetY)
    -- Calculate visible area based on camera position
    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()

    -- Visible tile range
    local viewWidth = screenWidth / Config.SCALE / Config.TILE_SIZE
    local viewHeight = screenHeight / Config.SCALE / Config.TILE_SIZE

    local startX = math.max(1, math.floor(cameraX - viewWidth / 2))
    local endX = math.min(self.width, math.ceil(cameraX + viewWidth / 2))
    local startY = math.max(1, math.floor(cameraY - viewHeight / 2))
    local endY = math.min(self.height, math.ceil(cameraY + viewHeight / 2))

    -- Draw only visible tiles
    for y = startY, endY do
        for x = startX, endX do
            local tileIndex = self.tiles[y][x]
            local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                screenWidth, screenHeight, Config.SCALE)
            screenX = math.floor(screenX)
            screenY = math.floor(screenY)

            local quad = self.quads[tileIndex]

            if tileIndex == TILE_FLOOR then
                local bm = self.floorBitmasks[y] and self.floorBitmasks[y][x]
                local floorQuad = bm and self.floorQuads[bm]
                if floorQuad then
                    love.graphics.draw(self.floorTilesetImage, floorQuad, screenX, screenY)
                end
            elseif quad then
                -- Walls, traps, etc.: draw from tileset quad
                love.graphics.draw(self.tileset, quad, screenX, screenY)
            end
        end
    end
end

return MapRenderer
