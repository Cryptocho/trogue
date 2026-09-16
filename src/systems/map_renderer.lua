-- MapRenderer System
-- Renders static map tiles from 2D array instead of ECS entities

local Config = require("src.config")
local Coordinates = require("src.core.coordinates")
local Autotile = require("src.utils.autotile")

local TILE_FLOOR = 0
local TILE_WALL = 1
local TILE_GRASS = 9   -- grass/mushroom (walkable, floor pass)
local TILE_BUSH = 10   -- bush (solid, tree pass)
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

    -- Scene tile resources — multiple variants per category
    treeVariants = {},
    treeVariantMap = {},
    grassVariants = {},
    grassVariantMap = {},
    bushVariants = {},
    bushVariantMap = {},
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

    -- Load scene tiles (tree/grass/bush variants) from tileset.lua
    self.treeVariants = {}
    self.grassVariants = {}
    self.bushVariants = {}
    local imageCache = {}
    if tileset.scene_tiles and #tileset.scene_tiles > 0 then
        for _, tile in ipairs(tileset.scene_tiles) do
            local path = "assets/" .. tile.texture_path
            if not imageCache[path] then
                local ok, img = pcall(love.graphics.newImage, path)
                if ok then
                    img:setFilter("nearest", "nearest")
                    imageCache[path] = img
                end
            end
            local img = imageCache[path]
            if img then
                local r = tile.region
                local quad = love.graphics.newQuad(r.x, r.y, r.w, r.h, img:getDimensions())
                local variant = {
                    image = img,
                    quad = quad,
                    regionW = r.w,
                    regionH = r.h,
                    offsetX = tile.offset.x,
                    offsetY = tile.offset.y,
                }
                local category = tile.scene_category
                if category == "grass" or category == "mushroom" then
                    table.insert(self.grassVariants, variant)
                elseif category == "bush" then
                    table.insert(self.bushVariants, variant)
                else
                    table.insert(self.treeVariants, variant)
                end
            end
        end
    end
end

function MapRenderer:loadMap(mapData)
    self.height = #mapData
    self.width = #mapData[1]
    self.treeVariantMap = {}
    self.grassVariantMap = {}
    self.bushVariantMap = {}

    for y, row in ipairs(mapData) do
        self.tiles[y] = {}
        for x = 1, #row do
            local char = row:sub(x, x)
            local tileIndex = TILE_FLOOR  -- default floor

            if char == "#" then
                tileIndex = TILE_WALL
            elseif char == "^" then
                tileIndex = TILE_TREE
            elseif char == "+" then
                tileIndex = TILE_BUSH
            elseif char == "," then
                tileIndex = TILE_GRASS
            end

            self.tiles[y][x] = tileIndex

            if tileIndex == TILE_TREE and #self.treeVariants > 0 then
                self.treeVariantMap[y] = self.treeVariantMap[y] or {}
                self.treeVariantMap[y][x] = math.random(#self.treeVariants)
            elseif tileIndex == TILE_BUSH and #self.bushVariants > 0 then
                self.bushVariantMap[y] = self.bushVariantMap[y] or {}
                self.bushVariantMap[y][x] = math.random(#self.bushVariants)
            elseif tileIndex == TILE_GRASS and #self.grassVariants > 0 then
                self.grassVariantMap[y] = self.grassVariantMap[y] or {}
                self.grassVariantMap[y][x] = math.random(#self.grassVariants)
            end
        end
    end

    self.floorBitmasks = {}
    local matchFn = function(px, py)
        if px < 1 or px > self.width or py < 1 or py > self.height then
            return false
        end
        local t = self.tiles[py][px]
        return t == TILE_FLOOR or t == TILE_GRASS
    end

    for y = 1, self.height do
        self.floorBitmasks[y] = {}
        for x = 1, self.width do
            local t = self.tiles[y][x]
            if t == TILE_FLOOR or t == TILE_GRASS then
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
    local t = self.tiles[y][x]
    return t == TILE_WALL or t == TILE_TREE or t == TILE_BUSH
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
            if tileIndex ~= TILE_TREE and tileIndex ~= TILE_BUSH then
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, Config.SCALE)
                screenX = math.floor(screenX)
                screenY = math.floor(screenY)

                -- Always draw floor autotile for floor and grass tiles
                if tileIndex == TILE_FLOOR or tileIndex == TILE_GRASS then
                    local bm = self.floorBitmasks[y] and self.floorBitmasks[y][x]
                    local floorQuad = bm and self.floorQuads[bm]
                    if floorQuad then
                        love.graphics.draw(self.floorTilesetImage, floorQuad, screenX, screenY)
                    end

                    -- Draw grass/mushroom decoration on top of floor
                    if tileIndex == TILE_GRASS then
                        local fogAlpha = fogOfWar and fogOfWar:getFogAlpha(x, y) or 0
                        if fogAlpha >= 1.0 then
                            -- Unexplored: tile-sized black fog covers floor
                            love.graphics.setColor(0, 0, 0, 1.0)
                            love.graphics.rectangle("fill", screenX, screenY, Config.TILE_SIZE, Config.TILE_SIZE)
                            love.graphics.setColor(1, 1, 1, 1)
                        else
                            -- Visible or partially explored: draw grass sprite
                            local vi = (self.grassVariantMap[y] and self.grassVariantMap[y][x]) or 0
                            if vi > 0 then
                                local v = self.grassVariants[vi]
                                if v then
                                    local drawX = screenX + Config.TILE_SIZE / 2 - v.regionW / 2 + v.offsetX
                                    local drawY = screenY + Config.TILE_SIZE / 2 - v.regionH / 2 + v.offsetY
                                    love.graphics.draw(v.image, v.quad, drawX, drawY)

                                    -- Fog covers the full sprite area (extends beyond tile)
                                    if fogAlpha > 0 then
                                        love.graphics.setColor(0, 0, 0, fogAlpha)
                                        love.graphics.rectangle("fill", drawX, drawY, v.regionW, v.regionH)
                                        love.graphics.setColor(1, 1, 1, 1)
                                    end
                                end
                            end
                        end
                    end
                else
                    local quad = self.quads[tileIndex]
                    if quad then
                        love.graphics.draw(self.tileset, quad, screenX, screenY)
                    end
                end

                -- Fog overlay for non-grass tiles (grass handles its own fog via sprite area)
                if fogOfWar and tileIndex ~= TILE_GRASS then
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
    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()
    local viewWidth = screenWidth / Config.SCALE / Config.TILE_SIZE
    local viewHeight = screenHeight / Config.SCALE / Config.TILE_SIZE
    local startX = math.max(1, math.floor(cameraX - viewWidth / 2))
    local endX = math.min(self.width, math.ceil(cameraX + viewWidth / 2))
    local startY = math.max(1, math.floor(cameraY - viewHeight / 2))
    local endY = math.min(self.height, math.ceil(cameraY + viewHeight / 2))

    local objects = {}
    for y = startY, endY do
        for x = startX, endX do
            local tile = self.tiles[y][x]
            local variants, variantMap
            if tile == TILE_TREE then
                variants = self.treeVariants
                variantMap = self.treeVariantMap
            elseif tile == TILE_BUSH then
                variants = self.bushVariants
                variantMap = self.bushVariantMap
            end

            if variants and #variants > 0 then
                local vi = (variantMap[y] and variantMap[y][x]) or 1
                local v = variants[vi] or variants[1]
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, Config.SCALE)
                screenX = math.floor(screenX)
                screenY = math.floor(screenY)
                local drawX = screenX + Config.TILE_SIZE / 2 - v.regionW / 2 + v.offsetX
                local drawY = screenY + Config.TILE_SIZE / 2 - v.regionH / 2 + v.offsetY
                table.insert(objects, {x = x, y = y, drawX = drawX, drawY = drawY, variant = vi, tileType = tile})
            end
        end
    end
    return objects
end

function MapRenderer:drawSingleTree(tree, alpha)
    local variants
    if tree.tileType == TILE_BUSH then
        variants = self.bushVariants
    else
        variants = self.treeVariants
    end
    local v = variants[tree.variant] or variants[1]
    if not v then return end
    love.graphics.setColor(1, 1, 1, alpha or 1.0)
    love.graphics.draw(v.image, v.quad, tree.drawX, tree.drawY)
    love.graphics.setColor(1, 1, 1, 1)
end

return MapRenderer
