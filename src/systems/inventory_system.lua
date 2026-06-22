local ItemDef = require("src.data.definitions.item")
local Coordinates = require("src.core.coordinates")

local InventorySystem = {
    priority = 2,
    name = "InventorySystem",
}

function InventorySystem:init(world)
    self.world = world
    self.events = world.eventBus
    self.itemDefs = ItemDef.builtin
    self.enabled = true

    if self.events then
        self.events:on("PickupRequest", function(data)
            self:_handlePickup(data)
        end)
        self.events:on("EntityDied", function(data)
            self:_handleDeathDrop(data)
        end, 10)
        self.events:on("InventoryDropItem", function(data)
            self:_handleDrop(data)
        end)
        self.events:on("InventoryUseItem", function(data)
            self:_handleUse(data)
        end)
        self.events:on("InventoryEquipItem", function(data)
            self:_handleEquip(data)
        end)
        self.events:on("InventoryUnequipItem", function(data)
            self:_handleUnequip(data)
        end)
    end
end

function InventorySystem:update(world, dt)
end

function InventorySystem:getItemDef(itemId)
    return self.itemDefs[itemId]
end

-- Grid operations (public for InventoryUI)

function InventorySystem:canPlace(inv, row, col, w, h)
    if row < 1 or col < 1 or row + h - 1 > inv.gridRows or col + w - 1 > inv.gridCols then
        return false
    end
    for r = row, row + h - 1 do
        for c = col, col + w - 1 do
            if not inv.slots[r] then
                inv.slots[r] = {}
            end
            if inv.slots[r][c] then
                return false
            end
        end
    end
    return true
end

function InventorySystem:findFreeSlot(inv, w, h)
    for r = 1, inv.gridRows - h + 1 do
        for c = 1, inv.gridCols - w + 1 do
            if self:canPlace(inv, r, c, w, h) then
                return r, c
            end
        end
    end
    return nil, nil
end

function InventorySystem:placeItem(inv, itemId, row, col, w, h)
    if not self:canPlace(inv, row, col, w, h) then
        return nil
    end
    local key = inv.nextKey
    inv.nextKey = inv.nextKey + 1
    for r = row, row + h - 1 do
        if not inv.slots[r] then
            inv.slots[r] = {}
        end
        for c = col, col + w - 1 do
            inv.slots[r][c] = key
        end
    end
    inv.items[key] = {itemId = itemId, row = row, col = col}
    return key
end

function InventorySystem:removeItem(inv, itemKey)
    local item = inv.items[itemKey]
    if not item then return nil end
    local itemDef = self.itemDefs[item.itemId]
    if not itemDef then
        inv.items[itemKey] = nil
        return nil
    end
    for r = item.row, item.row + itemDef.gridHeight - 1 do
        if inv.slots[r] then
            for c = item.col, item.col + itemDef.gridWidth - 1 do
                if inv.slots[r][c] == itemKey then
                    inv.slots[r][c] = nil
                end
            end
        end
    end
    inv.items[itemKey] = nil
    return item.itemId
end

function InventorySystem:clearGridOnly(inv, itemKey)
    local item = inv.items[itemKey]
    if not item then return end
    local itemDef = self.itemDefs[item.itemId]
    if not itemDef then return end
    for r = item.row, item.row + itemDef.gridHeight - 1 do
        if inv.slots[r] then
            for c = item.col, item.col + itemDef.gridWidth - 1 do
                if inv.slots[r][c] == itemKey then
                    inv.slots[r][c] = nil
                end
            end
        end
    end
    item.row = 0
    item.col = 0
end

function InventorySystem:getTotalUsed(inv)
    local count = 0
    for _ in pairs(inv.items) do
        count = count + 1
    end
    return count
end

function InventorySystem:getTotalCapacity(inv)
    return inv.gridCols * inv.gridRows
end

-- Pickup

function InventorySystem:_handlePickup(data)
    if not data.entity or not data.targetX or not data.targetY then
        self:_fail("invalid request")
        return
    end

    local inv = self.world:getComponent(data.entity, "Inventory")
    if not inv then self:_fail("no inventory") return end

    local spatialHash = self.world:getSpatialHash()
    local entities = spatialHash:getAt(data.targetX, data.targetY)
    if not entities or #entities == 0 then self:_fail("nothing to pick up") return end

    local itemEntityId = nil
    local invItem = nil
    for _, eid in ipairs(entities) do
        local ii = self.world:getComponent(eid, "InventoryItem")
        if ii then itemEntityId = eid; invItem = ii; break end
    end

    if not invItem or not itemEntityId then self:_fail("no item here") return end

    local itemId = invItem.itemId
    local itemDef = self.itemDefs[itemId]
    if not itemDef then self:_fail("unknown item") return end

    local w, h = itemDef.gridWidth, itemDef.gridHeight
    local row, col = self:findFreeSlot(inv, w, h)
    if not row then self:_fail("inventory full") return end

    local key = self:placeItem(inv, itemId, row, col, w, h)
    self.world:despawn(itemEntityId, "picked_up")

    if self.events then
        self.events:emit("PickupSucceeded", {
            entity = data.entity,
            itemId = itemId,
            itemKey = key,
        })
    end
end

-- Death drop

function InventorySystem:_handleDeathDrop(data)
    if not data.entity or not data.killer then return end

    local weaponComp = self.world:getComponent(data.entity, "Weapon")
    if not weaponComp then return end

    local enemyType = self:_enemyTypeFromWeaponEntity(data.entity)
    if not enemyType then return end

    local lootTable = ItemDef.LootTable[enemyType]
    if not lootTable then return end

    local pos = self.world:getComponent(data.entity, "Position")
    if not pos then return end

    local playerId = data.killer
    local playerInv = self.world:getComponent(playerId, "Inventory")
    if not playerInv then return end

    local dropCount = math.random(lootTable.minDrop, lootTable.maxDrop)
    if dropCount <= 0 then return end

    local totalWeight = 0
    for _, w in ipairs(lootTable.weights) do
        totalWeight = totalWeight + w
    end

    for i = 1, dropCount do
        local r = math.random(totalWeight)
        local cumulative = 0
        local itemId
        for j, w in ipairs(lootTable.weights) do
            cumulative = cumulative + w
            if r <= cumulative then
                itemId = lootTable.items[j]
                break
            end
        end
        if itemId then
            self.world:spawn({
                Position = {x = pos.x, y = pos.y},
                InventoryItem = {itemId = itemId},
                Renderable = {tileIndex = 9},
            })
        end
    end
end

function InventorySystem:_enemyTypeFromWeaponEntity(entityId)
    local weaponComp = self.world:getComponent(entityId, "Weapon")
    if not weaponComp then return nil end

    if weaponComp.weaponId == "shortsword" then return "goblin" end
    if weaponComp.weaponId == "fangs" then return "rat" end
    if weaponComp.weaponId == "battle_axe" then return "orc" end
    return nil
end

-- Drop from inventory

function InventorySystem:_handleDrop(data)
    if not data.entity or not data.itemKey then return end

    local inv = self.world:getComponent(data.entity, "Inventory")
    if not inv then return end

    local item = inv.items[data.itemKey]
    if not item then return end

    local pos = self.world:getComponent(data.entity, "Position")
    if not pos then return end

    local itemId = self:removeItem(inv, data.itemKey)
    if not itemId then return end

    self.world:spawn({
        Position = {x = pos.x, y = pos.y},
        InventoryItem = {itemId = itemId},
        Renderable = {tileIndex = 9},
    })

    if self.events then
        self.events:emit("ItemDropped", {
            entity = data.entity,
            itemId = itemId,
            x = pos.x,
            y = pos.y,
        })
    end
end

-- Use consumable

function InventorySystem:_handleUse(data)
    if not data.entity or not data.itemKey then return end

    local inv = self.world:getComponent(data.entity, "Inventory")
    if not inv then return end

    local item = inv.items[data.itemKey]
    if not item then return end

    local itemDef = self.itemDefs[item.itemId]
    if not itemDef then return end

    if itemDef.type ~= ItemDef.ItemType.CONSUMABLE then return end

    if itemDef.effectId then
        if self.events then
            if itemDef.effectId == "heal_minor" then
                self.events:emit("HealRequest", {
                    source = data.entity,
                    target = data.entity,
                    effectId = itemDef.effectId,
                    baseValue = 20,
                })
            end
        end
    end

    self:removeItem(inv, data.itemKey)

    if self.events then
        self.events:emit("ItemUsed", {
            entity = data.entity,
            itemId = item.itemId,
        })
    end
end

-- Equip

function InventorySystem:_handleEquip(data)
    if not data.entity or not data.itemKey then return end

    local inv = self.world:getComponent(data.entity, "Inventory")
    if not inv then return end

    local equip = self.world:getComponent(data.entity, "Equipment")
    if not equip then return end

    local item = inv.items[data.itemKey]
    if not item then return end

    local itemDef = self.itemDefs[item.itemId]
    if not itemDef or not itemDef.equipSlot then return end

    local slot = itemDef.equipSlot
    local oldItemKey = equip.slots[slot]
    if oldItemKey then
        local unequipData = {entity = data.entity, slot = slot}
        self:_handleUnequip(unequipData)
    end

    self:clearGridOnly(inv, data.itemKey)
    equip.slots[slot] = data.itemKey

    if itemDef.type == ItemDef.ItemType.WEAPON and itemDef.weaponId then
        self.world:setComponent(data.entity, "Weapon", {weaponId = itemDef.weaponId})
    end

    if self.events then
        self.events:emit("ItemEquipped", {
            entity = data.entity,
            itemId = item.itemId,
            itemKey = data.itemKey,
            slot = slot,
        })
    end
end

-- Unequip

function InventorySystem:_handleUnequip(data)
    if not data.entity or not data.slot then return end

    local inv = self.world:getComponent(data.entity, "Inventory")
    if not inv then return end

    local equip = self.world:getComponent(data.entity, "Equipment")
    if not equip then return end

    local itemKey = equip.slots[data.slot]
    if not itemKey then return end

    local item = inv.items[itemKey]
    local itemId, itemDef
    if item then
        itemId = item.itemId
        itemDef = self.itemDefs[itemId]
    end

    equip.slots[data.slot] = nil

    if not item then
        if self.events then
            self.events:emit("ItemUnequipped", {
                entity = data.entity,
                slot = data.slot,
            })
        end
        return
    end

    local w, h = 1, 1
    if itemDef then
        w = itemDef.gridWidth
        h = itemDef.gridHeight
    end

    local row, col = self:findFreeSlot(inv, w, h)
    if not row then
        inv.items[itemKey] = nil
        local pos = self.world:getComponent(data.entity, "Position")
        if pos then
            self.world:spawn({
                Position = {x = pos.x, y = pos.y},
                InventoryItem = {itemId = itemId},
                Renderable = {tileIndex = 9},
            })
        end
        if self.events then
            self.events:emit("ItemUnequipped", {
                entity = data.entity,
                itemId = itemId,
                slot = data.slot,
                dropped = true,
            })
        end
        return
    end

    inv.items[itemKey].row = row
    inv.items[itemKey].col = col
    for r = row, row + h - 1 do
        if not inv.slots[r] then
            inv.slots[r] = {}
        end
        for c = col, col + w - 1 do
            inv.slots[r][c] = itemKey
        end
    end

    self.world:setComponent(data.entity, "Weapon", {weaponId = "fists"})

    if self.events then
        self.events:emit("ItemUnequipped", {
            entity = data.entity,
            itemId = itemId,
            itemKey = itemKey,
            slot = data.slot,
        })
    end
end

function InventorySystem:_fail(reason)
    if self.events then
        self.events:emit("PickupFailed", {reason = reason})
    end
end

return InventorySystem
