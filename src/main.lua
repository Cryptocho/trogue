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
local InventorySystemModule = require("src.systems.inventory_system")
local MapGenerator = require("src.utils.map_generator")
local TweenSystem = require("src.systems.tween_system")
local InventoryUI = require("src.systems.inventory_ui")

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
    inventoryVisible = false,
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
    
    game.skillIcons = {}
    local iconFiles = {
        punch = "assets/hit.png",
        heal = "assets/heal.png",
        shield = "assets/defend.png",
        fireball = "assets/fireball.png",
    }
    for key, path in pairs(iconFiles) do
        local ok, img = pcall(love.graphics.newImage, path)
        if ok then game.skillIcons[key] = img end
    end

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
    -- Inventory toggle via polling (Tab or I key)
    for _, testKey in ipairs({"tab", "i"}) do
        if love.keyboard.isDown(testKey) then
            if not game["_down_" .. testKey] then
                game["_down_" .. testKey] = true
                game.inventoryVisible = not game.inventoryVisible
                if game.inputSystem then
                    game.inputSystem.showInventoryUI = game.inventoryVisible
                end
                if not game.inventoryVisible then
                    InventoryUI:resetCursor()
                end
            end
        else
            game["_down_" .. testKey] = false
        end
    end

    -- Pickup via polling (P key)
    if love.keyboard.isDown("p") then
        if not game._down_p then
            game._down_p = true
            if game.inputSystem then
                game.inputSystem:handlePickup()
            end
        end
    else
        game._down_p = false
    end

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
        
        -- Draw trees and entities sorted by y coordinate
        local renderSystem = game:getSystem("RenderSystem")
        if mapRenderer and renderSystem then
            local trees = mapRenderer:getTreePositions(cameraX, cameraY)
            local entities = renderSystem:getEntityPositions(game.world)
            
            -- Get mouse screen position for hover transparency
            local mx, my = love.mouse.getPosition()
            mx = mx / Config.SCALE
            my = my / Config.SCALE
            
            -- Group by y: trees[y] and ents[y]
            local treesByY = {}
            local entsByY = {}
            local allY = {}
            
            for _, tree in ipairs(trees) do
                local y = tree.y
                if not treesByY[y] then treesByY[y] = {}; allY[y] = true end
                table.insert(treesByY[y], tree)
            end
            for _, entity in ipairs(entities) do
                local y = entity.logicY
                if not entsByY[y] then entsByY[y] = {}; allY[y] = true end
                table.insert(entsByY[y], entity)
            end
            
            -- Sort y values ascending
            local yList = {}
            for y in pairs(allY) do table.insert(yList, y) end
            table.sort(yList)
            
            -- Draw by y level: trees first, then entities (entities on top)
            for _, y in ipairs(yList) do
                -- Draw trees at this y
                local treeGroup = treesByY[y]
                if treeGroup then
                    for _, tree in ipairs(treeGroup) do
                        -- Check if player is behind this tree
                        local alpha = 1.0
                        for _, entity in ipairs(entities) do
                            if entity.isPlayer then
                                local treeScreenX = tree.drawX
                                local treeScreenY = tree.drawY
                                local playerWx, playerWy = Coordinates.tileToWorld(entity.renderX, entity.renderY)
                                local playerScreenX = playerWx + offsetX
                                local playerScreenY = playerWy + offsetY
                                
                                if playerScreenX + Config.TILE_SIZE > treeScreenX and
                                   playerScreenX < treeScreenX + mapRenderer.treeRegionW and
                                   playerScreenY + Config.TILE_SIZE > treeScreenY and
                                   playerScreenY < treeScreenY + mapRenderer.treeRegionH then
                                    if entity.logicY < y then
                                        alpha = 0.3
                                        break
                                    end
                                end
                            end
                        end
                        
                        -- Mouse hover transparency (check entire tree draw area)
                        if mx >= tree.drawX and mx <= tree.drawX + mapRenderer.treeRegionW and
                           my >= tree.drawY and my <= tree.drawY + mapRenderer.treeRegionH then
                            alpha = 0.3
                        end
                        
                        mapRenderer:drawSingleTree(tree, alpha)
                    end
                end
                
                -- Draw entities at this y
                local entGroup = entsByY[y]
                if entGroup then
                    for _, entity in ipairs(entGroup) do
                        renderSystem:drawSingleEntity(entity, offsetX, offsetY)
                    end
                end
            end
        end
        
        if renderSystem then
            renderSystem:drawHealthBars(game.world, offsetX, offsetY)
            renderSystem:drawAimPreview(offsetX, offsetY, cameraX, cameraY)
        end
        
        -- Pop transform
        love.graphics.pop()
        
        -- Draw UI
        game:drawUI()

        -- Inventory UI
        if game.inventoryVisible then
            InventoryUI:draw(game.world)
        end
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
    if game.inventoryVisible then
        InventoryUI:handleKey(key, game.world)
        return
    end

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
    if game.inventoryVisible then
        InventoryUI:handleMouse(x, y, button, game.world)
        return
    end
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
    local mapData, enemySpawns = MapGenerator.generateMap("forest", 60, 60, {
        treeMinDist = 2.0,
        densityThreshold = 0.5,
        fbmOctaves = 6,
        fbmPersistence = 0.5,
        fbmScale = 4.0,
        poissonMaxAttempts = 5,
        poissonSeed = nil,
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
    
    if enemySpawns then
        for _, spawn in ipairs(enemySpawns) do
            game.prototypes:spawn(spawn.type, {Position = {x = spawn.x, y = spawn.y}})
        end
    end
    
    -- Add systems (by priority)
    game.world:addSystem(MapRenderer)
    game.world:addSystem(TurnSystem)
    game.world:addSystem(InventorySystemModule)
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