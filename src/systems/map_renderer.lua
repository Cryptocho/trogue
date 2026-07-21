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

    -- Scene tile (tree) resources
    treeImage = nil,
    treeQuad = nil,
    treeRegionW = 0,
    treeRegionH = 0,
    treeOffsetX = 0,
    treeOffsetY = 0,
}

function MapRenderer:init(world)
    self.world = world

    -- Load tileset for non-floor tiles (walls, traps, etc.)
    self.tileset = love.graphics.newImage("assets/tileset.png")
    self.tileset:setFilter("nearest", "nearest")

    -- Load floor tileset for autotile rendering
    self.floorTilesetImage = love.graphics.newImage("assets/Tile Set.png")
    self.floorTilesetImage:setFilter("nearest", "nearest")
    local tileset = require("assets.tileset")
    self.floorQuads = Autotile.buildQuads(tileset, self.floorTilesetImage:getDimensions())

    -- Pre-create quads for each tile (walls, traps use tileset)
    for i = 0, 8 do
        local tx = (i % Config.TILES_PER_ROW) * Config.TILE_SIZE
        local ty = math.floor(i / Config.TILES_PER_ROW) * Config.TILE_SIZE
        self.quads[i] = love.graphics.newQuad(tx, ty, Config.TILE_SIZE, Config.TILE_SIZE,
                                              self.tileset:getDimensions())
    end

    -- Load scene tiles (tree) from tileset.lua
    local tilesetDef = require("assets.tileset")
    if tilesetDef.scene_tiles and #tilesetDef.scene_tiles > 0 then
        local treeTile = tilesetDef.scene_tiles[1]
        local ok, img = pcall(love.graphics.newImage, "assets/" .. treeTile.texture_path)
        if ok then
            img:setFilter("nearest", "nearest")
            self.treeImage = img
            local r = treeTile.region
            self.treeRegionW = r.w
            self.treeRegionH = r.h
            self.treeQuad = love.graphics.newQuad(r.x, r.y, r.w, r.h, img:getDimensions())
            self.treeOffsetX = treeTile.offset.x
            self.treeOffsetY = treeTile.offset.y
        end
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
    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()

    local viewWidth = screenWidth / Config.SCALE / Config.TILE_SIZE
    local viewHeight = screenHeight / Config.SCALE / Config.TILE_SIZE

    local startX = math.max(1, math.floor(cameraX - viewWidth / 2))
    local endX = math.min(self.width, math.ceil(cameraX + viewWidth / 2))
    local startY = math.max(1, math.floor(cameraY - viewHeight / 2))
    local endY = math.min(self.height, math.ceil(cameraY + viewHeight / 2))

    for y = startY, endY do
        for x = startX, endX do
            local tileIndex = self.tiles[y][x]
            if tileIndex ~= TILE_TREE then
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, Config.SCALE)
                screenX = math.floor(screenX)
                screenY = math.floor(screenY)

                if tileIndex == TILE_FLOOR then
                    local bm = self.floorBitmasks[y] and self.floorBitmasks[y][x]
                    local floorQuad = bm and self.floorQuads[bm]
                    if floorQuad then
                        love.graphics.draw(self.floorTilesetImage, floorQuad, screenX, screenY)
                    end
                else
                    local quad = self.quads[tileIndex]
                    if quad then
                        love.graphics.draw(self.tileset, quad, screenX, screenY)
                    end
                end
            end
        end
    end
end

function MapRenderer:getTreePositions(cameraX, cameraY)
    if not self.treeImage then return {} end

    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()
    local viewWidth = screenWidth / Config.SCALE / Config.TILE_SIZE
    local viewHeight = screenHeight / Config.SCALE / Config.TILE_SIZE
    local startX = math.max(1, math.floor(cameraX - viewWidth / 2))
    local endX = math.min(self.width, math.ceil(cameraX + viewWidth / 2))
    local startY = math.max(1, math.floor(cameraY - viewHeight / 2))
    local endY = math.min(self.height, math.ceil(cameraY + viewHeight / 2))

    local trees = {}
    for y = startY, endY do
        for x = startX, endX do
            if self.tiles[y][x] == TILE_TREE then
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, Config.SCALE)
                screenX = math.floor(screenX)
                screenY = math.floor(screenY)
                local drawX = screenX + Config.TILE_SIZE / 2 - self.treeRegionW / 2 + self.treeOffsetX
                local drawY = screenY + Config.TILE_SIZE / 2 - self.treeRegionH / 2 + self.treeOffsetY
                table.insert(trees, {x = x, y = y, drawX = drawX, drawY = drawY})
            end
        end
    end
    return trees
end

function MapRenderer:drawSingleTree(tree, alpha)
    love.graphics.setColor(1, 1, 1, alpha or 1.0)
    love.graphics.draw(self.treeImage, self.treeQuad, tree.drawX, tree.drawY)
    love.graphics.setColor(1, 1, 1, 1)
end

return MapRenderer
