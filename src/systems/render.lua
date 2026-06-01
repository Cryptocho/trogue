-- Render System
-- Renders dynamic entities (player, enemies) - tiles handled by MapRenderer

local Config = require("src.config")
local Coordinates = require("src.core.coordinates")

local RenderSystem = {
    priority = 4,
    name = "RenderSystem",

    tileset = nil,      -- tileset.png (enemies + map)
    playerImage = nil,  -- image.png (player sprite, scaled to TILE_SIZE)
    quads = {},
}

function RenderSystem:init(world)
    self.world = world

    -- Load tileset for enemies + map (unchanged)
    self.tileset = love.graphics.newImage("assets/tileset.png")

    -- Load image.png for player sprite and scale it to TILE_SIZE
    self.playerImage = love.graphics.newImage("assets/image.png")
    local imgW, imgH = self.playerImage:getDimensions()
    -- Scale factor: fit within TILE_SIZE while preserving aspect ratio
    self.playerScale = math.min(Config.TILE_SIZE / imgW, Config.TILE_SIZE / imgH)
    -- Centered offset so the image aligns at the bottom of the tile cell
    self.playerDrawW = imgW * self.playerScale
    self.playerDrawH = imgH * self.playerScale
    self.playerOffsetX = (Config.TILE_SIZE - self.playerDrawW) / 2
    self.playerOffsetY = Config.TILE_SIZE - self.playerDrawH

    -- Pre-create quads for each tile (enemies use tileset)
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
        local tween = result.components.PositionTween

        if result.components.Player or result.components.Actor then
            if pos and renderable then
                -- Use tweened visual position if available, otherwise tile-based
                local renderX, renderY
                if tween and tween.active then
                    renderX, renderY = tween.visualX, tween.visualY
                else
                    renderX, renderY = pos.x, pos.y
                end
                local wx, wy = Coordinates.tileToWorld(renderX, renderY)
                local x = wx + offsetX
                local y = wy + offsetY

                if result.components.Player then
                    -- Player: draw image.png scaled to fit TILE_SIZE (preserve aspect ratio)
                    local drawX = x + self.playerOffsetX
                    local drawY = y + self.playerOffsetY
                    love.graphics.draw(self.playerImage, drawX, drawY, 0, self.playerScale, self.playerScale)
                else
                    -- Enemy: draw from tileset quad as before
                    local quad = self.quads[renderable.tileIndex]
                    if quad then
                        love.graphics.draw(self.tileset, quad, x, y)
                    end
                end
            end
        end
    end
end

function RenderSystem:drawHealthBars(world, offsetX, offsetY)
    local entities = world:query({"Position", "Stats", "Renderable"})

    for _, result in ipairs(entities) do
        local pos = result.components.Position
        local stats = result.components.Stats
        local tween = result.components.PositionTween

        if (result.components.Player or result.components.Actor) and pos and stats then
            local renderX, renderY = pos.x, pos.y
            if tween and tween.active then
                renderX, renderY = tween.visualX, tween.visualY
            end
            local wx, wy = Coordinates.tileToWorld(renderX, renderY)
            local x = wx + offsetX
            local y = wy + offsetY - 4

            -- Background bar
            love.graphics.setColor(0.3, 0.3, 0.3, 1)
            love.graphics.rectangle("fill", x, y, Config.TILE_SIZE, 3)

            -- Health bar
            local healthPercent = stats.current.hp / stats.max.hp
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

function RenderSystem:drawAimPreview(offsetX, offsetY, cameraX, cameraY)
    local inputSystem = self._inputSystem
    if not inputSystem then
        for _, sys in ipairs(self.world.systems) do
            if sys.name == "InputSystem" then
                self._inputSystem = sys
                inputSystem = sys
                break
            end
        end
    end
    if not inputSystem or not inputSystem:isInAimMode() then return end

    local ruleEngine = inputSystem.ruleEngine
    if not ruleEngine then return end

    local abilityId = inputSystem:getPendingAbility()
    if not abilityId then return end

    local abilityDef = ruleEngine:getAbilityDef(abilityId)
    if not abilityDef or not abilityDef.rangeFunc then return end

    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return end
    local playerPos = players[1].components.Position

    local mapRenderer = self._mapRenderer
    if not mapRenderer then
        for _, sys in ipairs(self.world.systems) do
            if sys.name == "MapRenderer" then
                self._mapRenderer = sys
                mapRenderer = sys
                break
            end
        end
    end
    if not mapRenderer then return end

    local mx, my = love.mouse.getPosition()
    local screenW = love.graphics.getWidth()
    local screenH = love.graphics.getHeight()
    local tileX, tileY = Coordinates.screenToTile(mx, my, cameraX, cameraY, screenW, screenH, Config.SCALE)

    local tiles = abilityDef.rangeFunc(playerPos.x, playerPos.y, tileX, tileY, mapRenderer.width, mapRenderer.height)

    love.graphics.setColor(0, 1, 0, 0.3)
    for _, tile in ipairs(tiles) do
        local wx, wy = Coordinates.tileToWorld(tile.x, tile.y)
        love.graphics.rectangle("fill", wx + offsetX, wy + offsetY, Config.TILE_SIZE, Config.TILE_SIZE)
    end

    for _, tile in ipairs(tiles) do
        if tile.x == tileX and tile.y == tileY then
            local mwx, mwy = Coordinates.tileToWorld(tileX, tileY)
            love.graphics.setColor(1, 1, 0, 0.6)
            love.graphics.circle("fill", mwx + offsetX + Config.TILE_SIZE / 2, mwy + offsetY + Config.TILE_SIZE / 2, Config.TILE_SIZE / 3)
            break
        end
    end
end

return RenderSystem
