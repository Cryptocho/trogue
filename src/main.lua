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
local RuleEngineModule = require("src.core.rule_engine")

-- Load configuration
local Config = require("src.config")
TILE_SIZE = Config.TILE_SIZE
SCALE = Config.SCALE

-- Movement keys
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

-- Ability hotkeys
local KEY_ABILITIES = {
    ["1"] = "punch",
    ["2"] = "heal",
    ["3"] = "shield",
    ["4"] = "fireball",
}

local game = {
    world = nil,
    events = nil,
    prototypes = nil,
    turnSystem = nil,
    ruleEngine = nil,
    selectedAbility = nil,
}

function love.load()
    -- Set global default filter for pixel-perfect scaling
    love.graphics.setDefaultFilter("nearest", "nearest")
    
    game.events = EventBus:new()
    game.world = World:new()
    game.world.eventBus = game.events
    game.prototypes = PrototypeManager:new(game.world)
    game.prototypes:load("src.data.prototypes.entities")
    
    -- Create RuleEngine
    game.ruleEngine = RuleEngineModule.RuleEngine:new(game.world, game.events)
    
    initGameWorld()
    
    -- Register debug event handlers
    game.events:on("DamageDealt", function(data)
        -- print("Damage: " .. data.amount .. " dealt to " .. data.target)
    end)
    
    game.events:on("HealingApplied", function(data)
        -- print("Heal: " .. data.amount .. " applied to " .. data.target)
    end)
    
    game.events:on("EntityDied", function(data)
        print("Entity " .. data.entity .. " died")
        game.world:despawn(data.entity, "death")
    end)
    
    game.events:on("AbilityUsed", function(data)
        print("Used ability: " .. data.abilityId)
    end)
    
    game.events:on("AbilityUseFailed", function(data)
        print("Ability failed: " .. data.reason)
    end)
    
    game.events:on("BuffAdded", function(data)
        print("Buff added: " .. data.buffId)
    end)
    
    -- PlayerTurnEnd triggers enemy turn
    game.events:on("PlayerTurnEnd", function()
        -- Enemy turn is handled by AISystem
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
        
        -- Calculate offset to center the view
        local offsetX = screenWidth / 2 / SCALE - cameraX * TILE_SIZE - TILE_SIZE / 2
        local offsetY = screenHeight / 2 / SCALE - cameraY * TILE_SIZE - TILE_SIZE / 2
        
        -- Push transform and scale
        love.graphics.push()
        love.graphics.scale(SCALE)
        
        -- Draw map tiles
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
        
        -- Draw entities
        local renderSystem = game:getRenderSystem()
        if renderSystem then
            renderSystem:drawEntities(game.world, offsetX, offsetY)
            renderSystem:drawHealthBars(game.world, offsetX, offsetY)
        end
        
        -- Pop transform
        love.graphics.pop()
        
        -- Draw UI
        game:drawUI()
    end
    
    -- Draw FPS
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.print("FPS: " .. love.timer.getFPS(), 10, love.graphics.getHeight() - 20)
end

-- Draw UI (ability bar, etc.)
function game:drawUI()
    love.graphics.setColor(1, 1, 1, 1)
    
    -- Get player ID
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return end
    local playerId = players[1].id
    
    -- Draw ability bar
    local abilities = {"punch", "heal", "shield", "fireball"}
    local abilityNames = {"[1]Punch", "[2]Heal", "[3]Shield", "[4]Fireball"}
    local y = 50
    
    love.graphics.print("=== ABILITIES ===", 10, y)
    y = y + 20
    
    for i, abilityId in ipairs(abilities) do
        local info = self.ruleEngine:getAbilityInfo(playerId, abilityId)
        local cd = info.currentCooldown or 0
        local status = ""
        
        if cd > 0 then
            status = " [CD:" .. cd .. "]"
        elseif not info.canUse then
            status = " [disabled]"
        end
        
        love.graphics.print(abilityNames[i] .. status, 10, y)
        y = y + 18
    end
    
    -- Show MP
    local comp = self.ruleEngine:getAbilityComponent(playerId)
    local mp = comp and comp.resources and comp.resources.mp or 0
    local maxMp = comp and comp.resources and comp.resources.maxMp or 50
    love.graphics.print("MP: " .. mp .. "/" .. maxMp, 10, y + 10)
    
    -- Show turn status
    local turnStatus = self.turnSystem and (" Turn: " .. self.turnSystem:getTurnCount()) or ""
    love.graphics.print("Trogue - WASD move, 1-4 abilities" .. turnStatus, 10, 10)
    
    -- Show selected ability
    if self.selectedAbility then
        love.graphics.print("Selected: " .. self.selectedAbility, 10, y + 30)
    end
end

function love.keypressed(key, scancode, isrepeat)
    -- Check if turn system allows input
    if game.turnSystem and not game.turnSystem:isInputAllowed() then
        return
    end
    
    -- Handle movement
    local movement = KEY_MOVEMENTS[key]
    if movement then
        game:handleMove(movement)
        return
    end
    
    -- Handle ability hotkeys
    local abilityId = KEY_ABILITIES[key]
    if abilityId then
        game:handleAbility(abilityId)
        return
    end
end

-- Handle movement
function game:handleMove(movement)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return end
    
    local playerId = players[1].id
    self.turnSystem:startTurn()
    
    if self.events then
        self.events:emit("MoveAttempt", {
            entity = playerId,
            dx = movement.dx,
            dy = movement.dy,
            isPlayer = true
        })
    end
end

-- Handle ability use
function game:handleAbility(abilityId)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return end
    
    local playerId = players[1].id
    
    -- Check if ability is usable
    local canUse, reason = self.ruleEngine:canUse(playerId, abilityId)
    if not canUse then
        print("Cannot use: " .. reason)
        return
    end
    
    -- Start turn
    self.turnSystem:startTurn()
    
    -- Use ability - auto-select target
    if self.events then
        self.events:emit("AbilityUse", {
            entity = playerId,
            abilityId = abilityId,
            targetId = nil  -- RuleEngine will select target automatically
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
    
    -- Spawn entities
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
    
    -- Add systems (by priority)
    game.world:addSystem(MapRenderer)
    game.world:addSystem(TurnSystem)
    game.world:addSystem(MovementSystem)
    game.world:addSystem(CombatSystem)
    game.world:addSystem(AISystem)
    game.world:addSystem(RenderSystem)
    
    -- Initialize MapRenderer
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
    
    -- Store system references
    for _, sys in ipairs(game.world.systems) do
        if sys.name == "TurnSystem" then
            game.turnSystem = sys
        elseif sys.name == "AISystem" then
            -- Set RuleEngine reference
            sys.ruleEngine = game.ruleEngine
        end
    end
    
    -- Initialize actor abilities
    local actors = game.world:query({"Actor", "Position"})
    for _, result in ipairs(actors) do
        local comp = game.ruleEngine:getAbilityComponent(result.id)
        if game.world.components.Player[result.id] then
            -- Player has all abilities
            comp.abilities = {"punch", "heal", "shield", "fireball"}
        else
            -- Enemies only have melee attack
            comp.abilities = {"punch"}
        end
    end
end