-- Render System
-- Renders dynamic entities (player, enemies) - tiles handled by MapRenderer

local Config = require("src.config")

local RenderSystem = {
    priority = 4,
    name = "RenderSystem",
    
    -- Quads for entity sprites
    tileset = nil,
    quads = {},
}

function RenderSystem:init(world)
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

function RenderSystem:update(world, dt)
    -- Rendering happens in draw()
end

function RenderSystem:drawEntities(world, offsetX, offsetY)
    local entities = world:query({"Position", "Renderable"})
    
    for _, result in ipairs(entities) do
        local pos = result.components.Position
        local renderable = result.components.Renderable
        
        if result.components.Player or result.components.Actor then
            if pos and renderable then
                local x = (pos.x - 1) * Config.TILE_SIZE + offsetX
                local y = (pos.y - 1) * Config.TILE_SIZE + offsetY
                
                local quad = self.quads[renderable.tileIndex]
                if quad then
                    love.graphics.draw(self.tileset, quad, x, y)
                end
            end
        end
    end
end

function RenderSystem:drawHealthBars(world, offsetX, offsetY)
    local entities = world:query({"Position", "Health", "Renderable"})
    
    for _, result in ipairs(entities) do
        local pos = result.components.Position
        local health = result.components.Health
        
        if (result.components.Player or result.components.Actor) and pos and health then
            local x = (pos.x - 1) * Config.TILE_SIZE + offsetX
            local y = (pos.y - 1) * Config.TILE_SIZE + offsetY - 4
            
            -- Background bar
            love.graphics.setColor(0.3, 0.3, 0.3, 1)
            love.graphics.rectangle("fill", x, y, Config.TILE_SIZE, 3)
            
            -- Health bar
            local healthPercent = health.current / health.max
            if healthPercent > 0.5 then
                love.graphics.setColor(0, 0.8, 0, 1)
            elseif healthPercent > 0.25 then
                love.graphics.setColor(1, 0.8, 0, 1)
            else
                love.graphics.setColor(1, 0, 0, 1)
            end
            love.graphics.rectangle("fill", x, y, Config.TILE_SIZE * healthPercent, 3)
        end
    end
end

return RenderSystem
