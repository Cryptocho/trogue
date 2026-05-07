-- Render System
-- Renders the game world using LÖVE graphics

local RenderSystem = {
    priority = 4,
    name = "RenderSystem",
    
    -- Tile rendering constants
    TILE_SIZE = 16,
    TILES_PER_ROW = 8,
    
    -- Scale factor for zooming the game view
    SCALE = 2,
    
    init = function(self, world)
        self.world = world
        self.tileset = nil
        self.quads = {}
        
        -- Load tileset image
        self.tileset = love.graphics.newImage("assets/tileset.png")
        
        -- Pre-create quads for each tile
        for i = 0, 7 do
            local tx = (i % self.TILES_PER_ROW) * self.TILE_SIZE
            local ty = math.floor(i / self.TILES_PER_ROW) * self.TILE_SIZE
            self.quads[i] = love.graphics.newQuad(tx, ty, self.TILE_SIZE, self.TILE_SIZE, 
                                                  self.tileset:getDimensions())
        end
    end,
    
    update = function(self, world, dt)
        -- Rendering happens in draw()
    end,
    
    draw = function(self, world)
        -- Clear screen with black
        love.graphics.clear(0, 0, 0, 1)
        
        local screenWidth = love.graphics.getWidth()
        local screenHeight = love.graphics.getHeight()
        
        -- Find player position for camera
        local playerX, playerY = self:centerOnPlayer(world)
        
        -- Push current transform and apply scale for zooming
        love.graphics.push()
        love.graphics.scale(self.SCALE)
        
        -- Calculate offset to center the view (adjusted for scale)
        local offsetX = screenWidth / 2 / self.SCALE - playerX * self.TILE_SIZE - self.TILE_SIZE / 2
        local offsetY = screenHeight / 2 / self.SCALE - playerY * self.TILE_SIZE - self.TILE_SIZE / 2
        
        -- First pass: draw tiles
        self:drawTiles(world, offsetX, offsetY)
        
        -- Second pass: draw entities
        self:drawEntities(world, offsetX, offsetY)
        
        -- Draw health bars
        self:drawHealthBars(world, offsetX, offsetY)
        
        -- Pop transform to restore original scale for UI
        love.graphics.pop()
        
        -- Draw FPS (unaffected by scale)
        love.graphics.setColor(1, 1, 1, 1)
        love.graphics.print("FPS: " .. love.timer.getFPS(), 10, love.graphics.getHeight() - 20)
        love.graphics.print("Scale: " .. self.SCALE .. "x", 10, love.graphics.getHeight() - 40)
    end,
    
    centerOnPlayer = function(self, world)
        local players = world:query({"Player", "Position"})
        if #players > 0 then
            local pos = players[1].components.Position
            return pos.x, pos.y
        end
        return 0, 0
    end,
    
    drawTiles = function(self, world, offsetX, offsetY)
        local entities = world:query({"Position", "Renderable"})
        
        for _, result in ipairs(entities) do
            local pos = result.components.Position
            local renderable = result.components.Renderable
            
            if pos and renderable then
                local x = (pos.x - 1) * self.TILE_SIZE + offsetX
                local y = (pos.y - 1) * self.TILE_SIZE + offsetY
                
                local quad = self.quads[renderable.tileIndex]
                if quad then
                    love.graphics.draw(self.tileset, quad, x, y)
                end
            end
        end
    end,
    
    drawEntities = function(self, world, offsetX, offsetY)
        local entities = world:query({"Position", "Renderable"})
        
        for _, result in ipairs(entities) do
            local entity = result.id
            local pos = result.components.Position
            local renderable = result.components.Renderable
            
            if result.components.Player or result.components.Actor then
                if pos and renderable then
                    local x = (pos.x - 1) * self.TILE_SIZE + offsetX
                    local y = (pos.y - 1) * self.TILE_SIZE + offsetY
                    
                    local quad = self.quads[renderable.tileIndex]
                    if quad then
                        love.graphics.draw(self.tileset, quad, x, y)
                    end
                end
            end
        end
    end,
    
    drawHealthBars = function(self, world, offsetX, offsetY)
        local entities = world:query({"Position", "Health", "Renderable"})
        
        for _, result in ipairs(entities) do
            local entity = result.id
            local pos = result.components.Position
            local health = result.components.Health
            
            if (result.components.Player or result.components.Actor) and pos and health then
                local x = (pos.x - 1) * self.TILE_SIZE + offsetX
                local y = (pos.y - 1) * self.TILE_SIZE + offsetY - 4
                
                -- Background bar
                love.graphics.setColor(0.3, 0.3, 0.3, 1)
                love.graphics.rectangle("fill", x, y, self.TILE_SIZE, 3)
                
                -- Health bar
                local healthPercent = health.current / health.max
                if healthPercent > 0.5 then
                    love.graphics.setColor(0, 0.8, 0, 1)
                elseif healthPercent > 0.25 then
                    love.graphics.setColor(1, 0.8, 0, 1)
                else
                    love.graphics.setColor(1, 0, 0, 1)
                end
                love.graphics.rectangle("fill", x, y, self.TILE_SIZE * healthPercent, 3)
            end
        end
    end
}

return RenderSystem
