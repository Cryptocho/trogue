-- Trogue ECS Demo - Main Entry Point
local ECS = require("src.core.ecs")
local EventBusModule = require("src.core.events")
local PrototypeManagerModule = require("src.utils.prototype")
local RuleEngineModule = require("src.core.rule_engine")
local TurnSystem = require("src.systems.turn")
local MovementSystem = require("src.systems.movement")
local CombatSystem = require("src.systems.combat")
local AISystem = require("src.systems.ai")
local InputSystem = require("src.systems.input")
local MapRenderer = require("src.systems.map_renderer")
local RenderSystem = require("src.systems.render")
local RuleEngineModule = require("src.core.rule_engine")

-- Load configuration
local Config = require("src.config")
TILE_SIZE = Config.TILE_SIZE
SCALE = Config.SCALE

local game = {
    world = nil,
    events = nil,
    prototypes = nil,
    turnSystem = nil,
    inputSystem = nil,
    ruleEngine = nil,
    selectedAbility = nil,
}

function love.load()
    -- Set global default filter for pixel-perfect scaling
    love.graphics.setDefaultFilter("nearest", "nearest")

    game.events = EventBusModule.createEventBus()
    game.world = ECS.createWorld()
    game.world.eventBus = game.events
    game.prototypes = PrototypeManagerModule.createPrototypeManager(game.world)
    game.prototypes:load("src.data.prototypes.entities")

    -- Create RuleEngine
    game.ruleEngine = RuleEngineModule.createRuleEngine(game.world, game.events)
    
game.skillIcons = {
        punch = love.graphics.newImage("assets/hit.png"),
        heal = love.graphics.newImage("assets/heal.png"),
        shield = love.graphics.newImage("assets/defend.png"),
        fireball = love.graphics.newImage("assets/fireball.png"),
    }

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
        local cameraX, cameraY = nil, nil
        local players = game.world:query({"Player", "Position"})
        if #players > 0 then
            cameraX = players[1].components.Position.x
            cameraY = players[1].components.Position.y
            -- Save last known player position
            game.lastCameraX = cameraX
            game.lastCameraY = cameraY
        else
            -- Player dead or not found, keep last position
            cameraX = game.lastCameraX or 0
            cameraY = game.lastCameraY or 0
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
        local mapRenderer = game:getSystem("MapRenderer")
        if mapRenderer then
            mapRenderer:draw(cameraX, cameraY, offsetX, offsetY)
        end
        
        -- Draw entities
        local renderSystem = game:getSystem("RenderSystem")
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

-- Draw UI (ability bar, health bar, mp bar)
function game:drawUI()
    love.graphics.setColor(1, 1, 1, 1)

    local screenWidth = love.graphics.getWidth()
    local screenHeight = love.graphics.getHeight()

    local playerId = self:getPlayerId()
    if not playerId then return end

    local abilities = {"punch", "heal", "shield", "fireball"}
    local boxSize = 48
    local boxPadding = 8
    local barHeight = 20
    local barWidth = 200
    local barPadding = 16
    local barY = screenHeight - barHeight - 10
    local skillY = barY - boxSize - boxPadding

    local totalBarWidth = barWidth * 2 + barPadding
    local barStartX = (screenWidth - totalBarWidth) / 2

    local totalSkillWidth = #abilities * boxSize + (#abilities - 1) * boxPadding
    local skillStartX = (screenWidth - totalSkillWidth) / 2

    for i, abilityId in ipairs(abilities) do
        local bx = skillStartX + (i - 1) * (boxSize + boxPadding)
        local info = self.ruleEngine:getAbilityInfo(playerId, abilityId)
        local cd = info.currentCooldown or 0
        local canUse = info.canUse

        if i == self.selectedAbility then
            love.graphics.setColor(0.3, 0.5, 1, 1)
            love.graphics.rectangle("fill", bx - 3, skillY - 3, boxSize + 6, boxSize + 6, 4, 4)
        end

        love.graphics.setColor(1, 1, 1, 1)
        local icon = self.skillIcons[abilityId]
        if icon then
            love.graphics.draw(icon, bx, skillY, 0, boxSize / icon:getWidth(), boxSize / icon:getHeight())
        end

        if cd > 0 then
            love.graphics.setColor(0, 0, 0, 0.6)
            love.graphics.rectangle("fill", bx, skillY, boxSize, boxSize)
            love.graphics.setColor(1, 1, 1, 1)
            love.graphics.printf(tostring(cd), bx, skillY + 14, boxSize, "center")
        elseif not canUse then
            love.graphics.setColor(1, 0, 0, 0.4)
            love.graphics.rectangle("fill", bx, skillY, boxSize, boxSize)
        end

        love.graphics.setColor(0, 0, 0, 0.7)
        love.graphics.rectangle("fill", bx + boxSize - 18, skillY + boxSize - 16, 16, 14, 3, 3)
        love.graphics.setColor(1, 1, 1, 1)
        love.graphics.printf( i , bx + boxSize - 18, skillY + boxSize - 14, 16, "center")
    end

    local healthComp = self.world:getComponent(playerId, "Health")
    local currentHealth = healthComp and healthComp.current or 0
    local maxHealth = healthComp and healthComp.max or 100

    local abilComp = self.ruleEngine:getAbilityComponent(playerId)
    local currentMp = abilComp and abilComp.resources and abilComp.resources.mp or 0
    local maxMp = abilComp and abilComp.resources and abilComp.resources.maxMp or 50

    love.graphics.setColor(0.8, 0.8, 0.8, 1)
    love.graphics.rectangle("fill", barStartX, barY, barWidth, barHeight, 4, 4)
    love.graphics.setColor(1, 0.2, 0.2, 1)
    love.graphics.rectangle("fill", barStartX, barY, barWidth * (currentHealth / maxHealth), barHeight, 4, 4)
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.printf(string.format("HP %d/%d", currentHealth, maxHealth), barStartX, barY + 3, barWidth, "center")

    local mpBarX = barStartX + barWidth + barPadding
    love.graphics.setColor(0.8, 0.8, 0.8, 1)
    love.graphics.rectangle("fill", mpBarX, barY, barWidth, barHeight, 4, 4)
    love.graphics.setColor(0.2, 0.4, 1, 1)
    love.graphics.rectangle("fill", mpBarX, barY, barWidth * (currentMp / maxMp), barHeight, 4, 4)
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.printf(string.format("MP %d/%d", currentMp, maxMp), mpBarX, barY + 3, barWidth, "center")
end

function game:getPlayerId()
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return nil end
    return players[1].id
end

-- Delegate all keyboard input to InputSystem
function love.keypressed(key, scancode, isrepeat)
    if game.inputSystem then
        game.inputSystem:handleKey(key, scancode, isrepeat)
    end
end

-- Delegate mouse input to InputSystem
function love.mousepressed(x, y, button)
    if game.inputSystem and button == 1 then
        game.inputSystem:handleClick(x, y)
    end
end

-- Get system by name
function game:getSystem(systemName)
    for _, sys in ipairs(self.world.systems) do
        if sys.name == systemName then
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
    game.world:addSystem(InputSystem)
    game.world:addSystem(AISystem)
    game.world:addSystem(RenderSystem)
    
    -- Initialize MapRenderer
    local mapRenderer = game:getSystem("MapRenderer")
    if mapRenderer then
        mapRenderer:loadMap(mapData)
    end
    
    -- Store system references and set up inter-system dependencies
    game.turnSystem = game:getSystem("TurnSystem")
    game.inputSystem = game:getSystem("InputSystem")
    
    -- Set up inter-system dependencies (via setter methods)
    if game.inputSystem then
        game.inputSystem:setTurnSystem(game.turnSystem)
        game.inputSystem:setRuleEngine(game.ruleEngine)
    end
    
    local aiSystem = game:getSystem("AISystem")
    if aiSystem then
        aiSystem:setRuleEngine(game.ruleEngine)
    end
end