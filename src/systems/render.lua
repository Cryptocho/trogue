-- Render System
-- Renders the game world using LÖVE graphics

local System = require("src.ecs.system").System

local RenderSystem = System:extend("RenderSystem")

-- Tile rendering constants
local TILE_SIZE = 16
local TILES_PER_ROW = 8

function RenderSystem:new(opts)
    local instance = System.new(self, opts)
    instance.name = "RenderSystem"
    instance.tileset = nil
    instance.quads = {}  -- Quad cache
    instance.cameraX = 0
    instance.cameraY = 0
    return instance
end

function RenderSystem:init(world)
    self.world = world
    
    -- Load tileset image
    self.tileset = love.graphics.newImage("assets/tileset.png")
    self.tileset:setFilter("nearest", "nearest")  -- Pixel-perfect scaling
    
    -- Pre-create quads for each tile
    for i = 0, 7 do  -- We have 8 tiles (0-7)
        local tx = (i % TILES_PER_ROW) * TILE_SIZE
        local ty = math.floor(i / TILES_PER_ROW) * TILE_SIZE
        self.quads[i] = love.graphics.newQuad(tx, ty, TILE_SIZE, TILE_SIZE, 
                                              self.tileset:getDimensions())
    end
end

function RenderSystem:update(dt, world)
    -- Rendering happens in draw()
end

function RenderSystem:draw(world)
    -- Clear screen with black
    love.graphics.clear(0, 0, 0, 1)
    
    -- Center the camera on screen
    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()
    
    -- Find player position for camera
    local playerX, playerY = self:centerOnPlayer(world)
    
    -- Calculate offset to center the view
    local offsetX = screenWidth / 2 - playerX * TILE_SIZE - TILE_SIZE / 2
    local offsetY = screenHeight / 2 - playerY * TILE_SIZE - TILE_SIZE / 2
    
    -- First pass: draw tiles (floor and walls)
    self:drawTiles(world, offsetX, offsetY)
    
    -- Second pass: draw entities (player, enemies) on top
    self:drawEntities(world, offsetX, offsetY)
    
    -- Draw health bars
    self:drawHealthBars(world, offsetX, offsetY)
    
    -- Draw FPS
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.print("FPS: " .. love.timer.getFPS(), 10, love.graphics.getHeight() - 20)
end

function RenderSystem:centerOnPlayer(world)
    local players = world:query({"Player", "Position"})
    if #players > 0 then
        local pos = players[1].components.Position
        return pos.x, pos.y
    end
    return 0, 0
end

function RenderSystem:drawTiles(world, offsetX, offsetY)
    -- Find all renderable entities (tiles and entities)
    -- Use readOnly=false to allow safe position reading
    local entities = world:query({"Position", "Renderable"}, {readOnly = true})
    
    for _, result in ipairs(entities) do
        local pos = result.components.Position
        local renderable = result.components.Renderable
        
        if pos and renderable then
            local x = (pos.x - 1) * TILE_SIZE + offsetX
            local y = (pos.y - 1) * TILE_SIZE + offsetY
            
            -- Draw tile
            local quad = self.quads[renderable.tileIndex]
            if quad then
                love.graphics.draw(self.tileset, quad, x, y)
            end
        end
    end
end

function RenderSystem:drawEntities(world, offsetX, offsetY)
    -- Find all entities with Position and Renderable that are NOT tiles
    -- (tiles have neither Player nor Actor)
    local entities = world:query({"Position", "Renderable"}, {readOnly = true})
    
    for _, result in ipairs(entities) do
        local entity = result.id
        local pos = result.components.Position
        local renderable = result.components.Renderable
        
        -- Skip tiles (entities without Player or Actor)
        if result.components.Player or result.components.Actor then
            if pos and renderable then
                local x = (pos.x - 1) * TILE_SIZE + offsetX
                local y = (pos.y - 1) * TILE_SIZE + offsetY
                
                -- Draw entity
                local quad = self.quads[renderable.tileIndex]
                if quad then
                    love.graphics.draw(self.tileset, quad, x, y)
                end
            end
        end
    end
end

function RenderSystem:drawHealthBars(world, offsetX, offsetY)
    local entities = world:query({"Position", "Health", "Renderable"}, {readOnly = true})
    
    for _, result in ipairs(entities) do
        local entity = result.id
        local pos = result.components.Position
        local health = result.components.Health
        
        -- Only show health bars for actors (not tiles)
        if (result.components.Player or result.components.Actor) and pos and health then
            local x = (pos.x - 1) * TILE_SIZE + offsetX
            local y = (pos.y - 1) * TILE_SIZE + offsetY - 4
            
            -- Background bar
            love.graphics.setColor(0.3, 0.3, 0.3, 1)
            love.graphics.rectangle("fill", x, y, TILE_SIZE, 3)
            
            -- Health bar
            local healthPercent = health.current / health.max
            if healthPercent > 0.5 then
                love.graphics.setColor(0, 0.8, 0, 1)  -- Green
            elseif healthPercent > 0.25 then
                love.graphics.setColor(1, 0.8, 0, 1)  -- Yellow
            else
                love.graphics.setColor(1, 0, 0, 1)  -- Red
            end
            love.graphics.rectangle("fill", x, y, TILE_SIZE * healthPercent, 3)
        end
    end
end

return RenderSystem
