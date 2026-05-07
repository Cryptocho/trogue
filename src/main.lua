-- Trogue ECS Demo - Main Entry Point
local World = require("src.core.ecs").World
local EventBus = require("src.core.events").EventBus
local PrototypeManager = require("src.utils.prototype").PrototypeManager
local TurnSystem = require("src.systems.turn")
local MovementSystem = require("src.systems.movement")
local CombatSystem = require("src.systems.combat")
local AISystem = require("src.systems.ai")
local MapRenderer = require("src.systems.map_renderer")
local RenderSystem = require("src.systems.render")

-- Load configuration
local Config = require("src.config")
TILE_SIZE = Config.TILE_SIZE
SCALE = Config.SCALE

local KEY_MOVEMENTS = {
    left = {dx = -1, dy = 0},
    right = {dx = 1, dy = 0},
    up = {dx = 0, dy = -1},
    down = {dx = 0, dy = 1},
    a = {dx = -1, dy = 0},
    d = {dx = 1, dy = 0},
    w = {dx = 0, dy = -1},
    s = {dx = 0, dy = 1},
}

local game = {
    world = nil,
    events = nil,
    prototypes = nil,
    turnSystem = nil,
}

function love.load()
    -- Set global default filter for pixel-perfect scaling
    love.graphics.setDefaultFilter("nearest", "nearest")
    
    game.events = EventBus:new()
    game.world = World:new()
    game.world.eventBus = game.events
    game.prototypes = PrototypeManager:new(game.world)
    game.prototypes:load("src.data.prototypes.entities")
    
    initGameWorld()
    
    -- Register debug handlers (could be in a DebugSystem instead)
    game.events:on("DamageDealt", function(data)
        print("Damage: " .. data.amount .. " dealt to " .. data.target)
    end)
    
    game.events:on("EntityDied", function(data)
        print("Entity " .. data.entity .. " died")
        game.world:despawn(data.entity, "death")
    end)
    
    print("Game ready!")
end

function love.update(dt)
    if game.world then
        game.world:update(dt)
        game.world:processDespawns()
    end
end

function love.draw()
    if game.world then
        -- Get camera position from player
        local cameraX, cameraY = 0, 0
        local players = game.world:query({"Player", "Position"})
        if #players > 0 then
            cameraX = players[1].components.Position.x
            cameraY = players[1].components.Position.y
        end
        
        -- Clear screen with black
        love.graphics.clear(0, 0, 0, 1)
        
        local screenWidth = love.graphics.getWidth()
        local screenHeight = love.graphics.getHeight()
        
        -- Calculate offset to center the view (using global constants)
        local offsetX = screenWidth / 2 / SCALE - cameraX * TILE_SIZE - TILE_SIZE / 2
        local offsetY = screenHeight / 2 / SCALE - cameraY * TILE_SIZE - TILE_SIZE / 2
        
        -- Push transform and scale
        love.graphics.push()
        love.graphics.scale(SCALE)
        
        -- Draw map tiles via MapRenderer
        local mapRenderer = nil
        for _, sys in ipairs(game.world.systems) do
            if sys.name == "MapRenderer" then
                mapRenderer = sys
                break
            end
        end
        if mapRenderer then
            mapRenderer:draw(cameraX, cameraY, offsetX, offsetY)
        end
        
        -- Draw entities via RenderSystem
        local renderSystem = game:getRenderSystem()
        if renderSystem then
            renderSystem:drawEntities(game.world, offsetX, offsetY)
            renderSystem:drawHealthBars(game.world, offsetX, offsetY)
        end
        
        -- Pop transform
        love.graphics.pop()
        
        -- Draw FPS (unaffected by scale)
        love.graphics.setColor(1, 1, 1, 1)
        love.graphics.print("FPS: " .. love.timer.getFPS(), 10, love.graphics.getHeight() - 20)
    end
    
    love.graphics.setColor(1, 1, 1, 1)
    local turnStatus = game.turnSystem and (" Turn: " .. game.turnSystem:getTurnCount()) or ""
    local inputStatus = (game.turnSystem and game.turnSystem:isInputAllowed()) and "" or " (processing...)"
    love.graphics.print("Trogue - WASD/Arrows" .. turnStatus .. inputStatus, 10, 10)
end

function love.keypressed(key, scancode, isrepeat)
    -- Check via TurnSystem if input is allowed
    if game.turnSystem and not game.turnSystem:isInputAllowed() then
        return
    end
    
    local movement = KEY_MOVEMENTS[key]
    if not movement then
        return
    end
    
    local players = game.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end
    
    local playerId = players[1].id
    game.turnSystem:startTurn()
    
    if game.events then
        game.events:emit("MoveAttempt", {
            entity = playerId,
            dx = movement.dx,
            dy = movement.dy,
            isPlayer = true
        })
    end
end

function game:getRenderSystem()
    for _, sys in ipairs(self.world.systems) do
        if sys.name == "RenderSystem" then
            return sys
        end
    end
    return nil
end

function initGameWorld()
    local mapData = {
        "################",
        "#..............#",
        "#..............#",
        "#....@.........#",
        "#..............#",
        "#......g.......#",
        "#..............#",
        "#..............#",
        "################",
    }
    
    -- Spawn only dynamic entities (player and enemies)
    for y, row in ipairs(mapData) do
        for x = 1, #row do
            local char = row:sub(x, x)
            
            if char == "@" then
                game.prototypes:spawn("player", {Position = {x = x, y = y}})
            elseif char == "g" then
                game.prototypes:spawn("goblin", {Position = {x = x, y = y}})
            end
        end
    end
    
    -- Add MapRenderer system first (renders static tiles)
    game.world:addSystem(MapRenderer)
    
    -- Add other systems in priority order
    game.world:addSystem(TurnSystem)
    game.world:addSystem(MovementSystem)
    game.world:addSystem(CombatSystem)
    game.world:addSystem(AISystem)
    game.world:addSystem(RenderSystem)
    
    -- Initialize MapRenderer with map data
    local mapRenderer = nil
    for _, sys in ipairs(game.world.systems) do
        if sys.name == "MapRenderer" then
            mapRenderer = sys
            break
        end
    end
    if mapRenderer then
        mapRenderer:loadMap(mapData)
    end
    
    -- Store reference to TurnSystem
    for _, sys in ipairs(game.world.systems) do
        if sys.name == "TurnSystem" then
            game.turnSystem = sys
            break
        end
    end
end
