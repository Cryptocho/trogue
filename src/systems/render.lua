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

    local okTileset, tileset = pcall(love.graphics.newImage, "assets/tileset.png")
    self.tileset = okTileset and tileset or nil

    local okPlayer, playerImg = pcall(love.graphics.newImage, "assets/image.png")
    self.playerImage = okPlayer and playerImg or nil

    if self.playerImage then
        local imgW, imgH = self.playerImage:getDimensions()
        self.playerScale = math.min(Config.TILE_SIZE / imgW, Config.TILE_SIZE / imgH)
        self.playerDrawW = imgW * self.playerScale
        self.playerDrawH = imgH * self.playerScale
        self.playerOffsetX = (Config.TILE_SIZE - self.playerDrawW) / 2
        self.playerOffsetY = Config.TILE_SIZE - self.playerDrawH
    else
        self.playerScale = 1
        self.playerDrawW = Config.TILE_SIZE
        self.playerDrawH = Config.TILE_SIZE
        self.playerOffsetX = 0
        self.playerOffsetY = 0
    end

    self.quads = {}
    if self.tileset then
        for i = 0, 7 do
            local tx = (i % Config.TILES_PER_ROW) * Config.TILE_SIZE
            local ty = math.floor(i / Config.TILES_PER_ROW) * Config.TILE_SIZE
            self.quads[i] = love.graphics.newQuad(tx, ty, Config.TILE_SIZE, Config.TILE_SIZE,
                                                  self.tileset:getDimensions())
        end
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

        if (result.components.Player or result.components.Actor) and pos and renderable then
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
                if self.playerImage then
                    love.graphics.draw(self.playerImage, x + self.playerOffsetX, y + self.playerOffsetY, 0, self.playerScale, self.playerScale)
                else
                    love.graphics.setColor(0, 1, 1, 1)
                    love.graphics.rectangle("fill", x, y, Config.TILE_SIZE, Config.TILE_SIZE)
                    love.graphics.setColor(1, 1, 1, 1)
                end
            else
                if self.tileset and self.quads[renderable.tileIndex] then
                    love.graphics.draw(self.tileset, self.quads[renderable.tileIndex], x, y)
                else
                    love.graphics.setColor(1, 0.5, 0.5, 1)
                    love.graphics.rectangle("fill", x + 1, y + 1, Config.TILE_SIZE - 2, Config.TILE_SIZE - 2)
                    love.graphics.setColor(1, 1, 1, 1)
                end
            end
        elseif result.components.InventoryItem and pos then
            local wx, wy = Coordinates.tileToWorld(pos.x, pos.y)
            local x = wx + offsetX
            local y = wy + offsetY
            love.graphics.setColor(1, 0.85, 0.2, 0.7)
            love.graphics.circle("fill", x + Config.TILE_SIZE / 2, y + Config.TILE_SIZE / 2, Config.TILE_SIZE / 3)
            love.graphics.setColor(1, 1, 1, 1)
        end
    end
end

function RenderSystem:getEntityPositions(world)
    local entityList = {}
    local entities = world:query({"Position", "Renderable"})

    for _, result in ipairs(entities) do
        local pos = result.components.Position
        local renderable = result.components.Renderable
        local tween = result.components.PositionTween

        if (result.components.Player or result.components.Actor) and pos and renderable then
            local renderX, renderY
            if tween and tween.active then
                renderX, renderY = tween.visualX, tween.visualY
            else
                renderX, renderY = pos.x, pos.y
            end
            table.insert(entityList, {
                entityId = result.entityId,
                renderX = renderX,
                renderY = renderY,
                logicY = pos.y,  -- Use logic position for sorting
                isPlayer = result.components.Player ~= nil,
                tileIndex = renderable.tileIndex
            })
        end
    end

    return entityList
end

function RenderSystem:drawSingleEntity(entity, offsetX, offsetY)
    local wx, wy = Coordinates.tileToWorld(entity.renderX, entity.renderY)
    local x = wx + offsetX
    local y = wy + offsetY

    if entity.isPlayer then
        if self.playerImage then
            love.graphics.draw(self.playerImage, x + self.playerOffsetX, y + self.playerOffsetY, 0, self.playerScale, self.playerScale)
        else
            love.graphics.setColor(0, 1, 1, 1)
            love.graphics.rectangle("fill", x, y, Config.TILE_SIZE, Config.TILE_SIZE)
            love.graphics.setColor(1, 1, 1, 1)
        end
    else
        if self.tileset and self.quads[entity.tileIndex] then
            love.graphics.draw(self.tileset, self.quads[entity.tileIndex], x, y)
        else
            love.graphics.setColor(1, 0.5, 0.5, 1)
            love.graphics.rectangle("fill", x + 1, y + 1, Config.TILE_SIZE - 2, Config.TILE_SIZE - 2)
            love.graphics.setColor(1, 1, 1, 1)
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
        self._inputSystem = self.world:getSystem("InputSystem")
        inputSystem = self._inputSystem
    end
    if not inputSystem or not inputSystem.isInAimMode or not inputSystem:isInAimMode() then return end

    local ruleEngine = inputSystem.ruleEngine
    if not ruleEngine then return end

    local abilityId = inputSystem:getPendingAbility()
    if not abilityId then return end

    local abilityDef = ruleEngine:getAbilityDef(abilityId)
    if not abilityDef or not abilityDef.rangeFunc or not abilityDef.effectAreaFunc then return end

    local players = self.world:query({"Player", "Position"})
    if #players == 0 then return end
    local playerPos = players[1].components.Position

    local mapRenderer = self._mapRenderer
    if not mapRenderer then
        self._mapRenderer = self.world:getSystem("MapRenderer")
        mapRenderer = self._mapRenderer
    end
    if not mapRenderer then return end

    local mx, my = love.mouse.getPosition()
    local screenW = love.graphics.getWidth()
    local screenH = love.graphics.getHeight()
    local tileX, tileY = Coordinates.screenToTile(mx, my, cameraX, cameraY, screenW, screenH, Config.SCALE)

    local rangeTiles = abilityDef.rangeFunc(playerPos.x, playerPos.y, tileX, tileY, mapRenderer.width, mapRenderer.height)
    local mouseInRange = false
    local mouseBlocked = false

    local function isSolid(x, y) return mapRenderer:isSolid(x, y) end

    for _, tile in ipairs(rangeTiles) do
        local blocked = mapRenderer:isSolid(tile.x, tile.y)
            or not Coordinates.hasLineOfSight(playerPos.x, playerPos.y, tile.x, tile.y, isSolid)
        if tile.x == tileX and tile.y == tileY then
            mouseInRange = true
            mouseBlocked = blocked
        end
        local wx, wy = Coordinates.tileToWorld(tile.x, tile.y)
        if blocked then
            love.graphics.setColor(1, 0, 0, 0.5)
        else
            love.graphics.setColor(0.3, 0.5, 1, 0.5)
        end
        love.graphics.rectangle("fill", wx + offsetX, wy + offsetY, Config.TILE_SIZE, Config.TILE_SIZE)
    end

    if not mouseInRange or mouseBlocked then return end

    local mwx, mwy = Coordinates.tileToWorld(tileX, tileY)
    love.graphics.setColor(1, 1, 0, 0.5)
    love.graphics.rectangle("fill", mwx + offsetX, mwy + offsetY, Config.TILE_SIZE, Config.TILE_SIZE)

    local effectTiles = abilityDef.effectAreaFunc(playerPos.x, playerPos.y, tileX, tileY, mapRenderer.width, mapRenderer.height)
    local halfTile = Config.TILE_SIZE / 2
    local quarterTile = Config.TILE_SIZE / 4
    for _, tile in ipairs(effectTiles) do
        local blocked = mapRenderer:isSolid(tile.x, tile.y)
            or not Coordinates.hasLineOfSight(tileX, tileY, tile.x, tile.y, isSolid)
        if not blocked then
            local ewx, ewy = Coordinates.tileToWorld(tile.x, tile.y)
            love.graphics.setColor(1, 1, 0, 0.5)
            love.graphics.rectangle("fill", ewx + offsetX + quarterTile, ewy + offsetY + quarterTile, halfTile, halfTile)
        end
    end
end

return RenderSystem
