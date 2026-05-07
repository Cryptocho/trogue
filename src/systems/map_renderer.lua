-- MapRenderer System
-- Renders static map tiles from 2D array instead of ECS entities

local Config = require("src.config")

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
            local tileIndex = 0  -- default floor
            
            if char == "#" then
                tileIndex = 1  -- wall
            end
            
            self.tiles[y][x] = tileIndex
        end
    end
end

function MapRenderer:update(world, dt)
    -- TODO: 动态地图支持
    -- 如果需要支持动态修改的地图（如开门、破坏墙壁等），在这里更新 tiles 数组
    -- 可能的实现方式：
    -- 1. 标记需要更新的瓦片位置
    -- 2. 订阅事件系统，在收到地图变更事件时修改 tiles
    -- 3. 对于需要动画的瓦片（如流水、草地摆动），存储动画状态
end

function MapRenderer:isSolid(x, y)
    -- Check if tile at position is solid (wall)
    -- Returns true for walls, false for floors
    if y >= 1 and y <= self.height and x >= 1 and x <= self.width then
        return self.tiles[y][x] == 1  -- 1 = wall
    end
    return false
end

function MapRenderer:draw(cameraX, cameraY, offsetX, offsetY)
    -- Calculate visible area based on camera position
    -- Uses Config constants
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
                local screenX = (x - 1) * Config.TILE_SIZE + offsetX
                local screenY = (y - 1) * Config.TILE_SIZE + offsetY
                love.graphics.draw(self.tileset, quad, screenX, screenY)
            end
        end
    end
end

return MapRenderer
