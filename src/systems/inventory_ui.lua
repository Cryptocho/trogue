local ItemDef = require("src.data.definitions.item")
local Config = require("src.config")
local Coordinates = require("src.core.coordinates")

local SLOT_SIZE = 30
local SLOT_GAP   = 3
local PANEL_PAD  = 12
local TITLE_H    = 20
local GRID_COLS  = 10
local GRID_ROWS  = 8
local REF_W, REF_H = 640, 480  -- reference window size

local InventoryUI = {
    cursorRow = 1,
    cursorCol = 1,
    heldItemKey = nil,
    heldSource  = nil,
    heldItemId  = nil,
    heldSlot    = nil,
    _smallFont  = nil,
}

local function getSmallFont()
    if not InventoryUI._smallFont then
        InventoryUI._smallFont = love.graphics.newFont(8)
    end
    return InventoryUI._smallFont
end

local function getScaledSmallFont(scale)
    local size = math.max(8, math.floor(8 * scale))
    local key = "_font_" .. size
    if not InventoryUI[key] then
        InventoryUI[key] = love.graphics.newFont(size)
    end
    return InventoryUI[key]
end

local ITEM_COLORS = {
    weapon     = {0.85, 0.35, 0.25},
    consumable = {0.25, 0.75, 0.35},
    equipment  = {0.30, 0.40, 0.80},
}

local EQUIP_SLOTS = {"main_hand", "off_hand", "armor", "helmet"}
local EQUIP_LABELS = {
    main_hand = "Main Hand",
    off_hand  = "Off Hand",
    armor     = "Armor",
    helmet    = "Helmet",
}

-- Layout helpers

local function getScale()
    return math.min(love.graphics.getWidth() / REF_W, love.graphics.getHeight() / REF_H)
end

local function gridX(panelX, s)
    return panelX + PANEL_PAD * s
end

local function gridY(panelY, s)
    return panelY + PANEL_PAD * s + TITLE_H * s
end

local function slotScreenX(panelX, col, s)
    return gridX(panelX, s) + (col - 1) * (SLOT_SIZE * s + SLOT_GAP * s)
end

local function slotScreenY(panelY, row, s)
    return gridY(panelY, s) + (row - 1) * (SLOT_SIZE * s + SLOT_GAP * s)
end

local function gridPanelWidth(s)
    return GRID_COLS * SLOT_SIZE * s + (GRID_COLS - 1) * SLOT_GAP * s + PANEL_PAD * s * 2
end

local function gridPanelHeight(s)
    return GRID_ROWS * SLOT_SIZE * s + (GRID_ROWS - 1) * SLOT_GAP * s + PANEL_PAD * s * 2 + TITLE_H * s
end

local function equipPanelX(gridPanelX, s)
    return gridPanelX + gridPanelWidth(s) + 16 * s
end

local function equipPanelWidth(s)
    return 4 * (SLOT_SIZE * s + SLOT_GAP * s) + PANEL_PAD * s * 2
end

-- Main draw

function InventoryUI:draw(world)
    local players = world:query({"Player", "Inventory"})
    if #players == 0 then return end

    local player = players[1]
    local invComp = player.components.Inventory
    local equipComp = player.components.Equipment
    local playerId = player.id

    local s = getScale()
    local ss = SLOT_SIZE * s
    local sg = SLOT_GAP * s
    local sp = PANEL_PAD * s
    local st = TITLE_H * s

    -- Panel positions (centered)
    local eqPanelW = equipPanelWidth(s)
    local totalW = gridPanelWidth(s) + eqPanelW + 16 * s
    local totalH = gridPanelHeight(s)
    local startX = (love.graphics.getWidth() - totalW) / 2
    local startY = (love.graphics.getHeight() - totalH) / 2

    -- Inventory background
    love.graphics.setColor(0.06, 0.06, 0.06, 0.92)
    love.graphics.rectangle("fill", startX, startY, gridPanelWidth(s), totalH, 6 * s, 6 * s)
    love.graphics.setColor(0.35, 0.35, 0.35, 1)
    love.graphics.rectangle("line", startX, startY, gridPanelWidth(s), totalH, 6 * s, 6 * s)

    -- Title
    local titleFont = love.graphics.newFont(math.floor(12 * s))
    love.graphics.setFont(titleFont)
    love.graphics.setColor(0.9, 0.9, 0.9, 1)
    love.graphics.printf("Inventory", startX, startY + 5 * s, gridPanelWidth(s), "center")
    love.graphics.setFont(love.graphics.newFont())  -- reset to default

    -- Grid slots
    self:_drawGrid(startX, startY, invComp, s)

    -- Keyboard cursor
    if not self.heldItemKey then
        local csx = slotScreenX(startX, self.cursorCol, s)
        local csy = slotScreenY(startY, self.cursorRow, s)
        love.graphics.setColor(1, 0.85, 0.2, 0.7)
        love.graphics.rectangle("line", csx - 1, csy - 1, ss + 2, ss + 2)
    end

    -- Equipment panel
    if equipComp then
        local eqX = equipPanelX(startX, s)
        love.graphics.setColor(0.06, 0.06, 0.06, 0.92)
        love.graphics.rectangle("fill", eqX, startY, eqPanelW, totalH, 6 * s, 6 * s)
        love.graphics.setColor(0.35, 0.35, 0.35, 1)
        love.graphics.rectangle("line", eqX, startY, eqPanelW, totalH, 6 * s, 6 * s)
        love.graphics.setFont(titleFont)
        love.graphics.setColor(0.9, 0.9, 0.9, 1)
        love.graphics.printf("Equipment", eqX, startY + 5 * s, eqPanelW, "center")
        love.graphics.setFont(love.graphics.newFont())

        self:_drawEquipment(eqX, startY, invComp, equipComp, s)
    end

    -- Capacity text
    local used = 0
    for _ in pairs(invComp.items) do
        used = used + 1
    end
    local total = invComp.gridCols * invComp.gridRows
    local capFont = love.graphics.newFont(math.floor(10 * s))
    love.graphics.setFont(capFont)
    love.graphics.setColor(0.7, 0.7, 0.7, 1)
    love.graphics.printf(string.format("Items: %d  Cells: %d", used, total),
        startX, startY + totalH - 18 * s, gridPanelWidth(s), "center")
    love.graphics.setFont(love.graphics.newFont())

    -- Held item (mouse drag)
    if self.heldItemKey then
        local mx, my = love.mouse.getPosition()
        local itemDef = ItemDef.builtin[self.heldItemId]
        if itemDef then
            local w = itemDef.gridWidth * (ss + sg) - sg
            local h = itemDef.gridHeight * (ss + sg) - sg
            local c = ITEM_COLORS[itemDef.type] or {0.5, 0.5, 0.5}
            love.graphics.setColor(c[1], c[2], c[3], 0.8)
            love.graphics.rectangle("fill", mx - w / 2, my - h / 2, w, h, 3 * s, 3 * s)
            local dragFont = love.graphics.newFont(math.floor(10 * s))
            love.graphics.setFont(dragFont)
            love.graphics.setColor(1, 1, 1, 0.9)
            love.graphics.printf(itemDef.name, mx - w / 2, my - h / 2 + 8 * s, w, "center")
            love.graphics.setFont(love.graphics.newFont())
        end
    end

    -- Tooltip
    self:_drawTooltip(startX, startY, invComp, equipComp, playerId, s)

    love.graphics.setColor(1, 1, 1, 1)
end

function InventoryUI:_drawGrid(panelX, panelY, invComp, s)
    local ss = SLOT_SIZE * s
    local sg = SLOT_GAP * s

    for row = 1, GRID_ROWS do
        for col = 1, GRID_COLS do
            local sx = slotScreenX(panelX, col, s)
            local sy = slotScreenY(panelY, row, s)

            love.graphics.setColor(0.15, 0.15, 0.15, 1)
            love.graphics.rectangle("fill", sx, sy, ss, ss, 3 * s, 3 * s)
            love.graphics.setColor(0.3, 0.3, 0.3, 1)
            love.graphics.rectangle("line", sx, sy, ss, ss, 3 * s, 3 * s)
        end
    end

    -- Draw items (only those NOT held)
    local smallFont = getScaledSmallFont(s)
    for key, item in pairs(invComp.items) do
        if key ~= self.heldItemKey and item.row > 0 then
            local itemDef = ItemDef.builtin[item.itemId]
            if itemDef then
                local sx = slotScreenX(panelX, item.col, s)
                local sy = slotScreenY(panelY, item.row, s)
                local w = itemDef.gridWidth * (ss + sg) - sg
                local h = itemDef.gridHeight * (ss + sg) - sg
                local c = ITEM_COLORS[itemDef.type] or {0.5, 0.5, 0.5}

                love.graphics.setColor(c[1], c[2], c[3], 0.85)
                love.graphics.rectangle("fill", sx + 1, sy + 1, w - 2, h - 2, 3 * s, 3 * s)

                if itemDef.gridWidth > 1 or itemDef.gridHeight > 1 then
                    love.graphics.setColor(1, 1, 1, 0.15)
                    love.graphics.rectangle("line", sx + 1, sy + 1, w - 2, h - 2, 3 * s, 3 * s)
                end

                love.graphics.setColor(1, 1, 1, 0.95)
                love.graphics.setFont(smallFont)
                love.graphics.printf(self:_shortName(itemDef.name), sx, sy + h / 2 - 6 * s, w, "center")
            end
        end
    end
end

function InventoryUI:_drawEquipment(eqX, eqY, invComp, equipComp, s)
    local ss = SLOT_SIZE * s
    local sp = PANEL_PAD * s
    local st = TITLE_H * s
    local slotY = eqY + sp + st

    for i, slot in ipairs(EQUIP_SLOTS) do
        local y = slotY + (i - 1) * (ss * 2 + 8 * s)
        local sx = eqX + sp
        local sy = y

        love.graphics.setColor(0.25, 0.25, 0.25, 1)
        love.graphics.rectangle("fill", sx, sy, ss * 1.5, ss * 1.5, 4 * s, 4 * s)
        love.graphics.setColor(0.5, 0.5, 0.5, 1)
        love.graphics.rectangle("line", sx, sy, ss * 1.5, ss * 1.5, 4 * s, 4 * s)

        local labelFont = love.graphics.newFont(math.floor(8 * s))
        love.graphics.setFont(labelFont)
        love.graphics.setColor(0.6, 0.6, 0.6, 1)
        love.graphics.printf(EQUIP_LABELS[slot], sx, sy + ss * 1.5 + 2 * s, ss * 1.5, "center")
        love.graphics.setFont(love.graphics.newFont())

        local itemKey = equipComp.slots[slot]
        if itemKey and itemKey ~= self.heldItemKey then
            local item = invComp.items[itemKey]
            if item then
                local itemDef = ItemDef.builtin[item.itemId]
                if itemDef then
                    local c = ITEM_COLORS[itemDef.type] or {0.5, 0.5, 0.5}
                    love.graphics.setColor(c[1], c[2], c[3], 0.85)
                    love.graphics.rectangle("fill", sx + 2, sy + 2, ss * 1.5 - 4, ss * 1.5 - 4, 3 * s, 3 * s)
                    local smallFont = getScaledSmallFont(s)
                    love.graphics.setFont(smallFont)
                    love.graphics.setColor(1, 1, 1, 0.95)
                    love.graphics.printf(self:_shortName(itemDef.name), sx + 2, sy + ss * 0.5, ss * 1.5 - 4, "center")
                    love.graphics.setFont(love.graphics.newFont())
                end
            end
        end
    end
end

function InventoryUI:_drawTooltip(panelX, panelY, invComp, equipComp, playerId, s)
    local mx, my = love.mouse.getPosition()
    local ss = SLOT_SIZE * s
    local sg = SLOT_GAP * s
    local sp = PANEL_PAD * s
    local st = TITLE_H * s

    -- Check grid hover
    local gx = gridX(panelX, s)
    local gy = gridY(panelY, s)
    if mx >= gx and my >= gy and mx < gx + GRID_COLS * (ss + sg) and my < gy + GRID_ROWS * (ss + sg) then
        local col = math.floor((mx - gx) / (ss + sg)) + 1
        local row = math.floor((my - gy) / (ss + sg)) + 1
        if col >= 1 and col <= GRID_COLS and row >= 1 and row <= GRID_ROWS then
            if invComp.slots[row] and invComp.slots[row][col] then
                local key = invComp.slots[row][col]
                local item = invComp.items[key]
                if item and key ~= self.heldItemKey then
                    self:_showTooltip(item.itemId, mx, my, s)
                end
            end
        end
    end

    -- Check equipment hover
    local eqX = equipPanelX(panelX, s)
    if equipComp and mx >= eqX then
        local slotY = panelY + sp + st
        for i, slot in ipairs(EQUIP_SLOTS) do
            local y = slotY + (i - 1) * (ss * 2 + 8 * s)
            if my >= y and my < y + ss * 1.5 and mx >= eqX + sp and mx < eqX + sp + ss * 1.5 then
                local itemKey = equipComp.slots[slot]
                if itemKey and itemKey ~= self.heldItemKey then
                    local item = invComp.items[itemKey]
                    if item then
                        self:_showTooltip(item.itemId, mx, my, s)
                    end
                end
                return
            end
        end
    end
end

function InventoryUI:_showTooltip(itemId, mx, my, s)
    local itemDef = ItemDef.builtin[itemId]
    if not itemDef then return end

    local lines = {
        itemDef.name,
        "Type: " .. itemDef.type,
    }
    if itemDef.gridWidth > 1 or itemDef.gridHeight > 1 then
        table.insert(lines, string.format("Size: %dx%d", itemDef.gridWidth, itemDef.gridHeight))
    end
    if itemDef.description and itemDef.description ~= "" then
        table.insert(lines, itemDef.description)
    end
    if itemDef.weaponId then
        table.insert(lines, "Weapon: " .. itemDef.weaponId)
    end
    if itemDef.rarity then
        table.insert(lines, "Rarity: " .. itemDef.rarity)
    end

    local lineH = 14 * s
    local pad = 6 * s
    local tooltipFont = love.graphics.newFont(math.floor(10 * s))
    local maxW = 0
    for _, line in ipairs(lines) do
        local w = tooltipFont:getWidth(line)
        if w > maxW then maxW = w end
    end
    local tw = maxW + pad * 2
    local th = #lines * lineH + pad * 2
    local tx = mx + 12 * s
    local ty = my - th - 4 * s
    if tx + tw > love.graphics.getWidth() then
        tx = mx - tw - 12 * s
    end

    love.graphics.setColor(0.05, 0.05, 0.08, 0.92)
    love.graphics.rectangle("fill", tx, ty, tw, th, 4 * s, 4 * s)
    love.graphics.setColor(0.5, 0.5, 0.6, 1)
    love.graphics.rectangle("line", tx, ty, tw, th, 4 * s, 4 * s)

    love.graphics.setFont(tooltipFont)
    love.graphics.setColor(1, 1, 1, 1)
    for i, line in ipairs(lines) do
        love.graphics.print(line, tx + pad, ty + pad + (i - 1) * lineH)
    end
    love.graphics.setFont(love.graphics.newFont())
end

-- Keyboard handling

function InventoryUI:handleKey(key, world)
    if key == "tab" or key == "i" or key == "escape" then
        return
    end
    if key == "up" then
        self.cursorRow = math.max(1, self.cursorRow - 1)
    elseif key == "down" then
        self.cursorRow = math.min(GRID_ROWS, self.cursorRow + 1)
    elseif key == "left" then
        self.cursorCol = math.max(1, self.cursorCol - 1)
    elseif key == "right" then
        self.cursorCol = math.min(GRID_COLS, self.cursorCol + 1)
    elseif key == "return" then
        self:_keySelect(world)
    elseif key == "e" then
        self:_keyEquip(world)
    elseif key == "d" then
        self:_keyDrop(world)
    end
end

function InventoryUI:_keySelect(world)
    local players = world:query({"Player", "Inventory"})
    if #players == 0 then return end
    local playerId = players[1].id
    local inv = players[1].components.Inventory

    if self.heldItemKey then
        self:_tryPlaceHeld(inv, world, playerId)
    else
        if inv.slots[self.cursorRow] and inv.slots[self.cursorRow][self.cursorCol] then
            local key = inv.slots[self.cursorRow][self.cursorCol]
            local item = inv.items[key]
            if item then
                self.heldItemKey = key
                self.heldItemId = item.itemId
                self.heldSource = "inv"
                self:_removeFromGrid(inv, key)
            end
        end
    end
end

function InventoryUI:_keyEquip(world)
    local players = world:query({"Player", "Inventory", "Equipment"})
    if #players == 0 then return end
    local playerId = players[1].id
    local inv = players[1].components.Inventory

    local cursorKey
    if inv.slots[self.cursorRow] and inv.slots[self.cursorRow][self.cursorCol] then
        cursorKey = inv.slots[self.cursorRow][self.cursorCol]
    end
    if cursorKey and not self.heldItemKey then
        local item = inv.items[cursorKey]
        if item then
            local itemDef = ItemDef.builtin[item.itemId]
            if itemDef then
                if itemDef.type == ItemDef.ItemType.CONSUMABLE then
                    if world.eventBus then
                        world.eventBus:emit("InventoryUseItem", {
                            entity = playerId,
                            itemKey = cursorKey,
                        })
                    end
                elseif itemDef.equipSlot then
                    if world.eventBus then
                        world.eventBus:emit("InventoryEquipItem", {
                            entity = playerId,
                            itemKey = cursorKey,
                        })
                    end
                end
            end
        end
    end
end

function InventoryUI:_keyDrop(world)
    local players = world:query({"Player", "Inventory"})
    if #players == 0 then return end
    local playerId = players[1].id
    local inv = players[1].components.Inventory

    local cursorKey
    if inv.slots[self.cursorRow] and inv.slots[self.cursorRow][self.cursorCol] then
        cursorKey = inv.slots[self.cursorRow][self.cursorCol]
    end

    if self.heldItemKey then
        if world.eventBus then
            world.eventBus:emit("InventoryDropItem", {
                entity = playerId,
                itemKey = self.heldItemKey,
            })
        end
        self:_clearHeld()
    elseif cursorKey then
        if world.eventBus then
            world.eventBus:emit("InventoryDropItem", {
                entity = playerId,
                itemKey = cursorKey,
            })
        end
    end
end

function InventoryUI:_keyUse(world)
    local players = world:query({"Player", "Inventory"})
    if #players == 0 then return end
    local playerId = players[1].id
    local inv = players[1].components.Inventory

    if self.heldItemKey then return end

    local cursorKey
    if inv.slots[self.cursorRow] and inv.slots[self.cursorRow][self.cursorCol] then
        cursorKey = inv.slots[self.cursorRow][self.cursorCol]
    end
    if cursorKey then
        local item = inv.items[cursorKey]
        if item then
            local itemDef = ItemDef.builtin[item.itemId]
            if itemDef and itemDef.type == ItemDef.ItemType.CONSUMABLE then
                if world.eventBus then
                    world.eventBus:emit("InventoryUseItem", {
                        entity = playerId,
                        itemKey = cursorKey,
                    })
                end
            end
        end
    end
end

-- Mouse handling

function InventoryUI:handleMouse(x, y, button, world)
    local players = world:query({"Player", "Inventory", "Equipment"})
    if #players == 0 then return end
    local playerId = players[1].id
    local inv = players[1].components.Inventory
    local equip = players[1].components.Equipment

    if button == 1 then
        self:_mouseLeft(x, y, world, playerId, inv, equip)
    elseif button == 2 then
        self:_mouseRight(x, y, world, playerId, inv, equip)
    end
end

function InventoryUI:_mouseLeft(mx, my, world, playerId, inv, equip)
    -- Check equipment slots first
    if equip then
        local eSlot = self:_getEquipSlotAt(mx, my, equip)
        if eSlot then
            local itemKey = equip.slots[eSlot]
            if self.heldItemKey then
                return
            elseif itemKey then
                if world.eventBus then
                    world.eventBus:emit("InventoryUnequipItem", {
                        entity = playerId,
                        slot = eSlot,
                    })
                end
            end
            return
        end
    end

    -- Check grid
    local row, col = self:_gridPosAt(mx, my, inv)
    if not row then
        -- Click outside grid: drop held item
        if self.heldItemKey then
            if world.eventBus then
                world.eventBus:emit("InventoryDropItem", {
                    entity = playerId,
                    itemKey = self.heldItemKey,
                })
            end
            self:_clearHeld()
        end
        return
    end

    if self.heldItemKey then
        self:_tryPlaceHeld(inv, world, playerId, row, col)
    else
        if inv.slots[row] and inv.slots[row][col] then
            local key = inv.slots[row][col]
            local item = inv.items[key]
            if item then
                self.heldItemKey = key
                self.heldItemId = item.itemId
                self.heldSource = "inv"
                self:_removeFromGrid(inv, key)
            end
        end
    end
end

function InventoryUI:_mouseRight(mx, my, world, playerId, inv, equip)
    if equip then
        local eSlot = self:_getEquipSlotAt(mx, my, equip)
        if eSlot and equip.slots[eSlot] then
            if world.eventBus then
                world.eventBus:emit("InventoryUnequipItem", {
                    entity = playerId,
                    slot = eSlot,
                })
            end
            return
        end
    end

    local row, col = self:_gridPosAt(mx, my, inv)
    if row and inv.slots[row] and inv.slots[row][col] then
        local key = inv.slots[row][col]
        if world.eventBus then
            world.eventBus:emit("InventoryDropItem", {
                entity = playerId,
                itemKey = key,
            })
        end
    end
end

-- Helpers

function InventoryUI:_gridPosAt(mx, my, inv)
    local s = getScale()
    local ss = SLOT_SIZE * s
    local sg = SLOT_GAP * s

    local panelW = gridPanelWidth(s)
    local eqPanelW = equipPanelWidth(s)
    local totalW = panelW + eqPanelW + 16 * s
    local totalH = gridPanelHeight(s)
    local startX = (love.graphics.getWidth() - totalW) / 2
    local startY = (love.graphics.getHeight() - totalH) / 2

    local gx = gridX(startX, s)
    local gy = gridY(startY, s)
    if mx < gx or my < gy then return nil, nil end

    local col = math.floor((mx - gx) / (ss + sg)) + 1
    local row = math.floor((my - gy) / (ss + sg)) + 1
    if col < 1 or col > inv.gridCols or row < 1 or row > inv.gridRows then
        return nil, nil
    end
    return row, col
end

function InventoryUI:_getEquipSlotAt(mx, my, equip)
    local s = getScale()
    local ss = SLOT_SIZE * s
    local sp = PANEL_PAD * s
    local st = TITLE_H * s

    local panelW = gridPanelWidth(s)
    local eqPanelW = equipPanelWidth(s)
    local totalW = panelW + eqPanelW + 16 * s
    local totalH = gridPanelHeight(s)
    local startX = (love.graphics.getWidth() - totalW) / 2
    local startY = (love.graphics.getHeight() - totalH) / 2

    local eqX = equipPanelX(startX, s)
    if mx < eqX + sp or mx > eqX + sp + ss * 1.5 then return nil end

    local slotY = startY + sp + st
    for i, slot in ipairs(EQUIP_SLOTS) do
        local y = slotY + (i - 1) * (ss * 2 + 8 * s)
        if my >= y and my < y + ss * 1.5 then
            return slot
        end
    end
    return nil
end

function InventoryUI:_tryPlaceHeld(inv, world, playerId, row, col)
    if not self.heldItemKey or not self.heldItemId then return end

    local itemDef = ItemDef.builtin[self.heldItemId]
    if not itemDef then
        self:_clearHeld()
        return
    end

    local w, h = itemDef.gridWidth, itemDef.gridHeight
    if self:localCanPlace(inv, row, col, w, h) then
        self:localPlace(inv, self.heldItemKey, row, col, w, h)
        self:_clearHeld()
    end
end

function InventoryUI:_removeFromGrid(inv, key)
    local item = inv.items[key]
    if not item then return end
    local itemDef = ItemDef.builtin[item.itemId]
    if not itemDef then return end
    for r = item.row, item.row + itemDef.gridHeight - 1 do
        if inv.slots[r] then
            for c = item.col, item.col + itemDef.gridWidth - 1 do
                if inv.slots[r][c] == key then
                    inv.slots[r][c] = nil
                end
            end
        end
    end
    item.row = 0
    item.col = 0
end

function InventoryUI:localCanPlace(inv, row, col, w, h)
    if row < 1 or col < 1 or row + h - 1 > inv.gridRows or col + w - 1 > inv.gridCols then
        return false
    end
    for r = row, row + h - 1 do
        for c = col, col + w - 1 do
            if not inv.slots[r] then inv.slots[r] = {} end
            if inv.slots[r][c] and inv.slots[r][c] ~= self.heldItemKey then
                return false
            end
        end
    end
    return true
end

function InventoryUI:localPlace(inv, key, row, col, w, h)
    for r = row, row + h - 1 do
        if not inv.slots[r] then inv.slots[r] = {} end
        for c = col, col + w - 1 do
            inv.slots[r][c] = key
        end
    end
    inv.items[key].row = row
    inv.items[key].col = col
end

function InventoryUI:_clearHeld()
    self.heldItemKey = nil
    self.heldSource = nil
    self.heldItemId = nil
    self.heldSlot = nil
end

function InventoryUI:_shortName(name)
    if #name <= 8 then return name end
    return name:sub(1, 7) .. "."
end

function InventoryUI:resetCursor()
    self.cursorRow = 1
    self.cursorCol = 1
    self:_clearHeld()
end

function InventoryUI:isHolding()
    return self.heldItemKey ~= nil
end

return InventoryUI
