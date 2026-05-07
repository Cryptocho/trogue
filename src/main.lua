-- Trogue ECS Demo - Main Entry Point
local World = require("src.core.ecs").World
local EventBus = require("src.core.events").EventBus
local PrototypeManager = require("src.utils.prototype").PrototypeManager
local TurnSystem = require("src.systems.turn")
local MovementSystem = require("src.systems.movement")
local CombatSystem = require("src.systems.combat")
local AISystem = require("src.systems.ai")
local RenderSystem = require("src.systems.render")

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
    game.events = EventBus:new()
    game.world = World:new()
    game.world.eventBus = game.events
    game.prototypes = PrototypeManager:new(game.world)
    game.prototypes:load("src.data.prototypes.tiles")
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
        local renderSystem = game:getRenderSystem()
        if renderSystem then
            renderSystem:draw(game.world)
        end
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
    
    for y, row in ipairs(mapData) do
        for x = 1, #row do
            local char = row:sub(x, x)
            local tileType = "floor"
            
            if char == "#" then
                tileType = "wall"
            elseif char == "@" then
                tileType = "floor"
                game.prototypes:spawn("player", {Position = {x = x, y = y}})
            elseif char == "g" then
                tileType = "floor"
                game.prototypes:spawn("goblin", {Position = {x = x, y = y}})
            end
            
            game.prototypes:spawn(tileType, {Position = {x = x, y = y}})
        end
    end
    
    -- Add systems in priority order
    -- TurnSystem must be added first to track turn state
    game.turnSystem = TurnSystem:new({priority = 0, name = "TurnSystem"})
    game.world:addSystem(game.turnSystem)
    
    game.world:addSystem(MovementSystem:new({priority = 1, name = "MovementSystem"}))
    game.world:addSystem(CombatSystem:new({priority = 2, name = "CombatSystem"}))
    game.world:addSystem(AISystem:new({priority = 3, name = "AISystem"}))
    game.world:addSystem(RenderSystem:new({priority = 4, name = "RenderSystem"}))
end
