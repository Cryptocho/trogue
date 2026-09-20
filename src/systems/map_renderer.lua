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

-- Church tile types
local TILE_CHURCH_FLOOR = 20
local TILE_CHURCH_WALL = 21
local TILE_EMPTY = 30        -- void area: not rendered, solid

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

    -- Church tile resources
    isChurchMap = false,
    churchFloorTilesetImage = nil,   -- floor tileset (Wang tiles)
    churchFloorQuads = {},           -- quads[cornerKey] = quad (0-15)
    churchFloorCornerKeys = {},      -- cornerKeys[y][x] = 4-bit key
    churchObjectMap = {},     -- churchObjectMap[y][x] = objectChar
    churchObjectImages = {},  -- images[char] = {image, quad, regionW, regionH}

    -- Entrance/exit tracking
    entrancePos = nil,  -- {x, y} for forest entrance
    exitPos = nil,      -- {x, y} for church exit
}

function MapRenderer:init(world)
    self.world = world

    -- Load tileset for non-floor tiles (walls, traps, etc.)
    self.tileset = love.graphics.newImage("assets/tileset.png")
    self.tileset:setFilter("nearest", "nearest")

    -- Load floor tileset for autotile rendering (forest grass)
    self.floorTilesetImage = love.graphics.newImage("assets/TopDownFantasy-Forest/Tiles/Tileset.png")
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

    -- Load church floor tileset (Wang tiles - wood floor with stone border edges)
    local churchFloorOk, churchFloorImg = pcall(love.graphics.newImage, "assets/church/tileset/tileset.png")
    if churchFloorOk then
        churchFloorImg:setFilter("nearest", "nearest")
        self.churchFloorTilesetImage = churchFloorImg

        -- Build Wang tile quads from metadata
        -- Each tile has corners (NW, NE, SW, SE) = "lower" or "upper"
        -- Encode as 4-bit key: bit0=NW(bit set=upper), bit1=NE, bit2=SW, bit3=SE
        -- lower = base floor (wood plank A), upper = variant floor (wood plank B)
        local f = io.open("src/assets/church/tileset/metadata.json", "r")
        if f then
            local jsonStr = f:read("*a")
            f:close()
            local pos = 1
            while true do
                local idStart, idEnd, id = jsonStr:find('"id"%s*:%s*"([^"]+)"', pos)
                if not idStart then break end
                
                -- Find corners block
                local cornersStart, cornersEnd = jsonStr:find('"corners"%s*:%s*%{[^%}]+%}', idEnd)
                if cornersStart then
                    local cornersBlock = jsonStr:sub(cornersStart, cornersEnd)
                    local nw = cornersBlock:find('"NW"%s*:%s*"upper"') and 1 or 0
                    local ne = cornersBlock:find('"NE"%s*:%s*"upper"') and 1 or 0
                    local sw = cornersBlock:find('"SW"%s*:%s*"upper"') and 1 or 0
                    local se = cornersBlock:find('"SE"%s*:%s*"upper"') and 1 or 0
                    local key = nw + ne * 2 + sw * 4 + se * 8
                    
                    -- Find bounding_box
                    local bbStart, bbEnd, bb_x, bb_y, bb_w, bb_h = jsonStr:find('"bounding_box"%s*:%s*%{%s*"x"%s*:%s*(%d+)%s*,%s*"y"%s*:%s*(%d+)%s*,%s*"width"%s*:%s*(%d+)%s*,%s*"height"%s*:%s*(%d+)', cornersEnd)
                    if bbStart then
                        self.churchFloorQuads[key] = love.graphics.newQuad(
                            tonumber(bb_x), tonumber(bb_y), tonumber(bb_w), tonumber(bb_h),
                            churchFloorImg:getDimensions()
                        )
                        pos = bbEnd
                    else
                        pos = idEnd
                    end
                else
                    pos = idEnd
                end
            end
        end
    end

    -- Load church object sprites
    local churchObjectDefs = {
        A = { file = "altar.png",        targetW = 64, targetH = 64 },
        P = { file = "pew_bench.png",    targetW = 32, targetH = 32 },
        C = { file = "cross.png",        targetW = 64, targetH = 64 },
        S = { file = "stained_glass.png",targetW = 32, targetH = 32 },
        L = { file = "candelabra.png",   targetW = 32, targetH = 32 },
        T = { file = "pulpit.png",       targetW = 32, targetH = 32 },
        O = { file = "pillar.png",       targetW = 64, targetH = 64 },
    }
    for char, def in pairs(churchObjectDefs) do
        local ok, img = pcall(love.graphics.newImage, "assets/church/objects/" .. def.file)
        if ok then
            img:setFilter("nearest", "nearest")
            local srcW = img:getWidth()
            local srcH = img:getHeight()
            local scaleX = def.targetW / srcW
            local scaleY = def.targetH / srcH
            self.churchObjectImages[char] = {
                image = img,
                regionW = def.targetW,
                regionH = def.targetH,
                scaleX = scaleX,
                scaleY = scaleY,
            }
        end
    end

end

function MapRenderer:loadMap(mapData)
    self.height = #mapData
    self.width = #mapData[1]
    self.treeVariantMap = {}
    self.grassVariantMap = {}
    self.bushVariantMap = {}
    self.churchObjectMap = {}

    -- Detect if this is a church map (contains '#' and '.')
    self.isChurchMap = false
    self.entrancePos = nil
    self.exitPos = nil
    for y, row in ipairs(mapData) do
        for x = 1, #row do
            local char = row:sub(x, x)
            if char == "#" then
                self.isChurchMap = true
                break
            end
        end
        if self.isChurchMap then break end
    end

    for y, row in ipairs(mapData) do
        self.tiles[y] = {}
        self.churchObjectMap[y] = self.churchObjectMap[y] or {}
        for x = 1, #row do
            local char = row:sub(x, x)
            local tileIndex

            if self.isChurchMap then
                if char == "#" then
                    tileIndex = TILE_CHURCH_WALL
                elseif char == "." or char == "E" then
                    tileIndex = TILE_CHURCH_FLOOR
                    if char == "E" then self.exitPos = {x = x, y = y} end
                elseif char == "x" then
                    tileIndex = TILE_EMPTY
                else
                    -- Object tile (A, P, C, S, L, T, O) — floor underneath
                    tileIndex = TILE_CHURCH_FLOOR
                    self.churchObjectMap[y][x] = char
                end
            else
                tileIndex = TILE_FLOOR  -- default floor
                if char == "#" then
                    tileIndex = TILE_WALL
                elseif char == "^" then
                    tileIndex = TILE_TREE
                elseif char == "+" then
                    tileIndex = TILE_BUSH
                elseif char == "," then
                    tileIndex = TILE_GRASS
                elseif char == "E" then
                    tileIndex = TILE_FLOOR  -- entrance is walkable floor
                    self.entrancePos = {x = x, y = y}
                end
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

    if self.isChurchMap then
        -- Church: corner-based Wang tile matching for FLOOR tiles
        -- lower=0 base floor variant, upper=1 alternate floor variant
        -- For a floor tile, check diagonal neighbors: if NOT floor, set corner bit
        -- NW=bit0, NE=bit1, SW=bit2, SE=bit3
        self.churchFloorCornerKeys = {}
        local isFloorTile = function(px, py)
            if px < 1 or px > self.width or py < 1 or py > self.height then
                return false  -- out of bounds = non-floor (use wall variant)
            end
            local t = self.tiles[py][px]
            return t == TILE_CHURCH_FLOOR
        end
        for y = 1, self.height do
            self.churchFloorCornerKeys[y] = {}
            for x = 1, self.width do
                if self.tiles[y][x] == TILE_CHURCH_FLOOR then
                    -- For floor at (x,y), each corner is shared with a diagonal neighbor.
                    -- NW corner: shared with (x-1, y-1), NE: (x, y-1), SW: (x-1, y), SE: (x, y)
                    -- If the neighbor is NOT floor, that corner is "upper" (border meeting wall)
                    local nw = isFloorTile(x - 1, y - 1) and 0 or 1
                    local ne = isFloorTile(x, y - 1) and 0 or 1
                    local sw = isFloorTile(x - 1, y) and 0 or 1
                    local se = isFloorTile(x, y) and 0 or 1
                    self.churchFloorCornerKeys[y][x] = nw + ne * 2 + sw * 4 + se * 8
                end
            end
        end
    else
        -- Forest: edge-based bitmask matching
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
    if self.isChurchMap then
        if t == TILE_CHURCH_WALL or t == TILE_EMPTY then return true end
        local obj = self.churchObjectMap[y] and self.churchObjectMap[y][x]
        if obj and obj ~= "E" then return true end  -- objects are solid, exit is not
        return false
    end
    return t == TILE_WALL or t == TILE_TREE or t == TILE_BUSH
end

function MapRenderer:isExitTile(x, y)
    if not self.isChurchMap then return false end
    local obj = self.churchObjectMap[y] and self.churchObjectMap[y][x]
    return obj == "E"
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

    if self.isChurchMap then
        -- Church rendering: two-pass to handle large object overlap
        -- Pass 1: floors, walls, empty (all ground layer)
        for y = startY, endY do
            for x = startX, endX do
                local tileIndex = self.tiles[y][x]
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, Config.SCALE)
                screenX = math.floor(screenX)
                screenY = math.floor(screenY)

                if tileIndex == TILE_CHURCH_FLOOR then
                    -- Draw wooden floor using Wang tile corner matching
                    local key = self.churchFloorCornerKeys[y] and self.churchFloorCornerKeys[y][x]
                    if key and self.churchFloorQuads[key] then
                        love.graphics.draw(self.churchFloorTilesetImage, self.churchFloorQuads[key], screenX, screenY)
                    else
                        -- Fallback: dark wood color
                        love.graphics.setColor(0.30, 0.22, 0.16, 1)
                        love.graphics.rectangle("fill", screenX, screenY, Config.TILE_SIZE, Config.TILE_SIZE)
                        love.graphics.setColor(1, 1, 1, 1)
                    end
                elseif tileIndex == TILE_CHURCH_WALL then
                    -- Draw wall: solid dark grey stone
                    love.graphics.setColor(0.35, 0.32, 0.30, 1)
                    love.graphics.rectangle("fill", screenX, screenY, Config.TILE_SIZE, Config.TILE_SIZE)
                    love.graphics.setColor(1, 1, 1, 1)
                elseif tileIndex == TILE_EMPTY then
                    -- Void: nothing rendered (solid black background)
                end
            end
        end

        -- Pass 2: objects sorted by y (larger objects drawn first at same y, smaller on top)
        local objectDrawList = {}
        for y = startY, endY do
            for x = startX, endX do
                local obj = self.churchObjectMap[y] and self.churchObjectMap[y][x]
                if obj and obj ~= "E" then
                    local objData = self.churchObjectImages[obj]
                    if objData then
                        local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                            screenWidth, screenHeight, Config.SCALE)
                        screenX = math.floor(screenX)
                        screenY = math.floor(screenY)
                        local drawX = screenX + Config.TILE_SIZE / 2 - objData.regionW / 2
                        local drawY = screenY + Config.TILE_SIZE / 2 - objData.regionH / 2
                        objectDrawList[#objectDrawList + 1] = {
                            x = drawX, y = drawY,
                            sortY = screenY + Config.TILE_SIZE / 2,
                            sortH = objData.regionH,
                            image = objData.image,
                            scaleX = objData.scaleX,
                            scaleY = objData.scaleY,
                        }
                    end
                end
            end
        end
        table.sort(objectDrawList, function(a, b)
            if a.sortY ~= b.sortY then return a.sortY < b.sortY end
            return a.sortH > b.sortH
        end)
        for _, objDraw in ipairs(objectDrawList) do
            love.graphics.draw(objDraw.image, objDraw.x, objDraw.y, 0, objDraw.scaleX, objDraw.scaleY)
        end

        -- Pass 3: exit markers and fog
        for y = startY, endY do
            for x = startX, endX do
                local screenX, screenY = Coordinates.tileToScreen(x, y, cameraX, cameraY,
                    screenWidth, screenHeight, Config.SCALE)
                screenX = math.floor(screenX)
                screenY = math.floor(screenY)

                local obj = self.churchObjectMap[y] and self.churchObjectMap[y][x]
                if obj == "E" then
                    -- Exit marker: green glowing square
                    love.graphics.setColor(0.2, 0.8, 0.3, 0.6)
                    love.graphics.rectangle("fill", screenX, screenY, Config.TILE_SIZE, Config.TILE_SIZE)
                    love.graphics.setColor(1, 1, 1, 1)
                end

                -- Fog overlay
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
        return
    end

    -- Forest rendering (original logic)
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

                -- Draw entrance marker in forest map
                if self.entrancePos and x == self.entrancePos.x and y == self.entrancePos.y then
                    love.graphics.setColor(0.9, 0.7, 0.2, 0.7)
                    love.graphics.rectangle("fill", screenX, screenY, Config.TILE_SIZE, Config.TILE_SIZE)
                    love.graphics.setColor(1, 1, 1, 1)
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
