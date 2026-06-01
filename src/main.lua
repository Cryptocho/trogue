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
local WeaponSystem = require("src.systems.weapon_system")
local MapGenerator = require("src.utils.map_generator")
local TweenSystem = require("src.systems.tween_system")

-- Load configuration
local Config = require("src.config")
local Coordinates = require("src.core.coordinates")
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
        game.ruleEngine:removePassiveAbilities(data.entity)
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
            local pos = players[1].components.Position
            local tween = players[1].components.PositionTween
            -- Use tweened visual position if currently moving, else logic position
            if tween and tween.active then
                cameraX = tween.visualX
                cameraY = tween.visualY
            else
                cameraX = pos.x
                cameraY = pos.y
            end
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
        local offsetX = screenWidth / 2 / Config.SCALE - cameraX * Config.TILE_SIZE - Config.TILE_SIZE / 2
        local offsetY = screenHeight / 2 / Config.SCALE - cameraY * Config.TILE_SIZE - Config.TILE_SIZE / 2
        
        -- Push transform and scale
        love.graphics.push()
        love.graphics.scale(Config.SCALE)
        
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
            renderSystem:drawAimPreview(offsetX, offsetY, cameraX, cameraY)
        end
        
        -- Pop transform
        love.graphics.pop()
        
        -- Draw UI
        game:drawUI()
    end
    
    -- Draw turn count
    love.graphics.setColor(1, 1, 1, 1)
    local screenH = love.graphics.getHeight()
    local turnCount = game.turnSystem and game.turnSystem:getTurnCount() or 0
    love.graphics.print("Turn: " .. turnCount, 10, screenH - 20)

    -- Draw mouse tile coordinates
    if game.lastCameraX then
        local mx, my = love.mouse.getPosition()
        local screenW = love.graphics.getWidth()
        local tileX, tileY = Coordinates.screenToTile(mx, my, game.lastCameraX, game.lastCameraY, screenW, screenH, Config.SCALE)
        love.graphics.print(string.format("Mouse: %d,%d", tileX, tileY), 10, screenH - 40)
    end
end

-- Draw UI (ability bar, health bar, energy bar)
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

    local statsComp = self.world:getComponent(playerId, "Stats")
    local currentHealth = statsComp and statsComp.current and statsComp.current.hp or 0
    local maxHealth = statsComp and statsComp.max and statsComp.max.hp or 100

    local currentEnergy = statsComp and statsComp.current and statsComp.current.energy or 0
    local maxEnergy = statsComp and statsComp.max and statsComp.max.energy or 50

    love.graphics.setColor(0.8, 0.8, 0.8, 1)
    love.graphics.rectangle("fill", barStartX, barY, barWidth, barHeight, 4, 4)
    love.graphics.setColor(1, 0.2, 0.2, 1)
    love.graphics.rectangle("fill", barStartX, barY, barWidth * (currentHealth / maxHealth), barHeight, 4, 4)
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.printf(string.format("HP %d/%d", currentHealth, maxHealth), barStartX, barY + 3, barWidth, "center")

    local energyBarX = barStartX + barWidth + barPadding
    love.graphics.setColor(0.8, 0.8, 0.8, 1)
    love.graphics.rectangle("fill", energyBarX, barY, barWidth, barHeight, 4, 4)
    love.graphics.setColor(0.2, 0.4, 1, 1)
    love.graphics.rectangle("fill", energyBarX, barY, barWidth * (currentEnergy / maxEnergy), barHeight, 4, 4)
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.printf(string.format("Energy %d/%d", currentEnergy, maxEnergy), energyBarX, barY + 3, barWidth, "center")
end

function game:getPlayerId()
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return nil end
    return players[1].id
end

-- Delegate keyboard input to InputSystem
function love.keypressed(key, scancode, isrepeat)
    if key == "escape" and game.inputSystem and game.inputSystem:isInAimMode() then
        game.inputSystem:cancelAim()
        return
    end
    if game.inputSystem then
        game.inputSystem:handleKey(key, scancode, isrepeat)
    end
end

-- Delegate mouse input to InputSystem
function love.mousepressed(x, y, button)
    if game.inputSystem then
        if button == 1 then
            if game.inputSystem:isInAimMode() then
                game.inputSystem:handleAimClick(x, y)
            else
                game.inputSystem:handleClick(x, y)
            end
        elseif button == 2 then
            if game.inputSystem:isInAimMode() then
                game.inputSystem:cancelAim()
            end
        end
    end
end

-- Get system by name
function game:getSystem(systemName)
    return self.world:getSystem(systemName)
end

function initGameWorld()
    local mapData = MapGenerator.generateMap("forest", 60, 60, {
        treeMinDist = 2.0,
        densityThreshold = 0.5,
        fbmOctaves = 6,
        fbmPersistence = 0.5,
        fbmScale = 4.0,
        poissonMaxAttempts = 5,
        poissonSeed = nil
    })
    
    -- Find nearest floor tile to center and spawn player
    local centerX, centerY = 30, 30
    local bestX, bestY = centerX, centerY
    local bestDist = math.huge
    
    for y = 1, 60 do
        for x = 1, 60 do
            if mapData[y]:sub(x, x) == "." then
                local d = math.abs(x - centerX) + math.abs(y - centerY)
                if d < bestDist then
                    bestDist = d
                    bestX, bestY = x, y
                end
            end
        end
    end
    
    game.prototypes:spawn("player", {Position = {x = bestX, y = bestY}})
    local playerId = game:getPlayerId()
    if playerId then
        game.ruleEngine:applyPassiveAbilities(playerId)
    end
    
    -- Add systems (by priority)
    game.world:addSystem(MapRenderer)
    game.world:addSystem(TurnSystem)
    game.world:addSystem(TweenSystem)
    game.world:addSystem(MovementSystem)
    game.world:addSystem(CombatSystem)
    game.world:addSystem(WeaponSystem)
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