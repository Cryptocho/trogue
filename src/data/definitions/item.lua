-- ItemDefinition: Item definition registry
-- Gameplay Layer — follows the same pattern as weapon.lua / ability.lua / effect.lua / buff.lua
--
-- Each item has a grid footprint (gridWidth × gridHeight) for the Diablo-style backpack.
-- Weapons reference weaponId in weapon.lua; consumables reference effectId in effect.lua.

local ItemType = {
    WEAPON     = "weapon",
    CONSUMABLE = "consumable",
    EQUIPMENT  = "equipment",
}

local EquipSlot = {
    MAIN_HAND = "main_hand",
    OFF_HAND  = "off_hand",
    ARMOR     = "armor",
    HELMET    = "helmet",
}

local Rarity = {
    COMMON   = "common",
    UNCOMMON = "uncommon",
    RARE     = "rare",
}

local function createItemDef(def)
    return {
        id          = def.id          or error("Item id required"),
        name        = def.name        or def.id,
        description = def.description or "",
        type        = def.type        or ItemType.CONSUMABLE,
        gridWidth   = def.gridWidth   or 1,
        gridHeight  = def.gridHeight  or 1,
        weaponId    = def.weaponId    or nil,
        equipSlot   = def.equipSlot   or nil,
        effectId    = def.effectId    or nil,
        rarity      = def.rarity      or Rarity.COMMON,
        tags        = def.tags        or {},
    }
end

local builtin = {
    -- Weapons
    weapon_greatsword = createItemDef({
        id = "weapon_greatsword", name = "Greatsword",
        type = ItemType.WEAPON, gridWidth = 2, gridHeight = 1,
        weaponId = "greatsword", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.UNCOMMON, description = "A heavy two-handed sword",
    }),
    weapon_shortsword = createItemDef({
        id = "weapon_shortsword", name = "Shortsword",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 2,
        weaponId = "shortsword", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.COMMON, description = "A light blade favored by quick fighters",
    }),
    weapon_battle_axe = createItemDef({
        id = "weapon_battle_axe", name = "Battle Axe",
        type = ItemType.WEAPON, gridWidth = 2, gridHeight = 1,
        weaponId = "battle_axe", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.UNCOMMON, description = "A heavy axe that cleaves through armor",
    }),
    weapon_dagger = createItemDef({
        id = "weapon_dagger", name = "Dagger",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 1,
        weaponId = "dagger", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.COMMON, description = "A fast, lightweight stabbing weapon",
    }),
    weapon_spear = createItemDef({
        id = "weapon_spear", name = "Spear",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 2,
        weaponId = "spear", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.COMMON, description = "A long shafted weapon with reach",
    }),
    weapon_mace = createItemDef({
        id = "weapon_mace", name = "Mace",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 1,
        weaponId = "mace", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.COMMON, description = "A blunt weapon effective against armored foes",
    }),
    weapon_longbow = createItemDef({
        id = "weapon_longbow", name = "Longbow",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 3,
        weaponId = "longbow", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.UNCOMMON, description = "A longbow with greater range and power",
    }),
    weapon_fire_wand = createItemDef({
        id = "weapon_fire_wand", name = "Fire Wand",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 1,
        weaponId = "fire_wand", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.UNCOMMON, description = "Channels fire magic",
    }),
    weapon_longsword = createItemDef({
        id = "weapon_longsword", name = "Longsword",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 2,
        weaponId = "longsword", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.COMMON, description = "A balanced one-handed sword",
    }),
    weapon_iron_sword = createItemDef({
        id = "weapon_iron_sword", name = "Iron Sword",
        type = ItemType.WEAPON, gridWidth = 1, gridHeight = 2,
        weaponId = "iron_sword", equipSlot = EquipSlot.MAIN_HAND,
        rarity = Rarity.COMMON, description = "A standard iron sword",
    }),

    -- Consumables
    consumable_health_potion = createItemDef({
        id = "consumable_health_potion", name = "Health Potion",
        type = ItemType.CONSUMABLE, gridWidth = 1, gridHeight = 1,
        effectId = "heal_minor",
        rarity = Rarity.COMMON, description = "Restores a small amount of HP",
    }),
    consumable_energy_potion = createItemDef({
        id = "consumable_energy_potion", name = "Energy Potion",
        type = ItemType.CONSUMABLE, gridWidth = 1, gridHeight = 1,
        effectId = "heal_minor",
        rarity = Rarity.COMMON, description = "Restores a small amount of Energy",
    }),

    -- Equipment
    equipment_leather_armor = createItemDef({
        id = "equipment_leather_armor", name = "Leather Armor",
        type = ItemType.EQUIPMENT, gridWidth = 2, gridHeight = 2,
        equipSlot = EquipSlot.ARMOR,
        rarity = Rarity.COMMON, description = "Basic leather protection",
    }),
    equipment_iron_helmet = createItemDef({
        id = "equipment_iron_helmet", name = "Iron Helmet",
        type = ItemType.EQUIPMENT, gridWidth = 2, gridHeight = 2,
        equipSlot = EquipSlot.HELMET,
        rarity = Rarity.COMMON, description = "A sturdy iron helmet",
    }),
}

-- Weighted loot tables for death drops
local LootTable = {
    goblin = {
        items = {"weapon_shortsword", "weapon_dagger", "consumable_health_potion"},
        weights = {3, 2, 4},
        minDrop = 0,
        maxDrop = 1,
    },
    rat = {
        items = {"consumable_health_potion"},
        weights = {1},
        minDrop = 0,
        maxDrop = 1,
    },
    orc = {
        items = {"weapon_battle_axe", "weapon_mace", "equipment_leather_armor", "consumable_health_potion"},
        weights = {3, 3, 2, 3},
        minDrop = 0,
        maxDrop = 2,
    },
}

return {
    ItemType  = ItemType,
    EquipSlot = EquipSlot,
    Rarity    = Rarity,
    create    = createItemDef,
    builtin   = builtin,
    LootTable = LootTable,
}
