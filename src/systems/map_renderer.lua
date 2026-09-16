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

    -- Scene tile (tree) resources — multiple variants
    treeVariants = {},     -- {{image, quad, regionW, regionH, offsetX, offsetY}, ...}
    treeVariantMap = {},   -- treeVariantMap[y][x] = variant index
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

    -- Load scene tiles (tree variants) from tileset.lua
    self.treeVariants = {}
    local imageCache = {}
    if tileset.scene_tiles and #tileset.scene_tiles > 0 then
        for _, treeTile in ipairs(tileset.scene_tiles) do
            local path = "assets/" .. treeTile.texture_path
            if not imageCache[path] then
                local ok, img = pcall(love.graphics.newImage, path)
                if ok then
                    img:setFilter("nearest", "nearest")
                    imageCache[path] = img
                end
            end
            local img = imageCache[path]
            if img then
                local r = treeTile.region
                local quad = love.graphics.newQuad(r.x, r.y, r.w, r.h, img:getDimensions())
                table.insert(self.treeVariants, {
                    image = img,
                    quad = quad,
                    regionW = r.w,
                    regionH = r.h,
                    offsetX = treeTile.offset.x,
                    offsetY = treeTile.offset.y,
                })
            end
        end
    end
end

function MapRenderer:loadMap(mapData)
    self.height = #mapData
    self.width = #mapData[1]
    self.treeVariantMap = {}

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

            if tileIndex == TILE_TREE and #self.treeVariants > 0 then
                self.treeVariantMap[y] = self.treeVariantMap[y] or {}
                self.treeVariantMap[y][x] = math.random(#self.treeVariants)
            end
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

function MapRenderer:draw(cameraX, cameraY, offsetX, offsetY, fogOfWar)
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

                -- Draw fog overlay
                if fogOfWar then
                    local fogAlpha = fogOfWar:getFogAlpha(x, y)
                    if fogAlpha > 0 then
                        love.graphics.setColor(0, 0, 0, fogAlpha)
                        love.graphics.rectangle("fill", screenX, screenY, Config.TILE_SIZE, Config.TILE_SIZE)
                        love.graphics.setColor(1, 1, 1, 1)
                    end
                end
            end
        end
    end
end

function MapRenderer:getTreePositions(cameraX, cameraY)
    if #self.treeVariants == 0 then return {} end

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
                local vi = (self.treeVariantMap[y] and self.treeVariantMap[y][x]) or 1
                local v = self.treeVariants[vi] or self.treeVariants[1]
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, Config.SCALE)
                screenX = math.floor(screenX)
                screenY = math.floor(screenY)
                local drawX = screenX + Config.TILE_SIZE / 2 - v.regionW / 2 + v.offsetX
                local drawY = screenY + Config.TILE_SIZE / 2 - v.regionH / 2 + v.offsetY
                table.insert(trees, {x = x, y = y, drawX = drawX, drawY = drawY, variant = vi})
            end
        end
    end
    return trees
end

function MapRenderer:drawSingleTree(tree, alpha)
    local v = self.treeVariants[tree.variant] or self.treeVariants[1]
    if not v then return end
    love.graphics.setColor(1, 1, 1, alpha or 1.0)
    love.graphics.draw(v.image, v.quad, tree.drawX, tree.drawY)
    love.graphics.setColor(1, 1, 1, 1)
end

return MapRenderer
