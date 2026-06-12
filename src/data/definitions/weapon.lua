-- WeaponDefinition: Weapon definition registry
-- Gameplay Layer — follows the same pattern as ability.lua / effect.lua / buff.lua
--
-- Weapon data is split into two layers:
--   1. Definition (this file): canonical template per weaponId
--   2. Component override  : per-entity inline values that shadow definition defaults
-- Priority: component > definition

-- Damage types (shared with EffectDefinition.damageType)
local DamageType = {
    PHYSICAL   = "physical",
    FIRE       = "fire",
    ICE        = "ice",
    LIGHTNING  = "lightning",
    POISON     = "poison",
    ARCANE     = "arcane",
}

-- Weapon category (used by UI / equipment slots)
local WeaponCategory = {
    MELEE  = "melee",
    RANGED = "ranged",
    MAGIC  = "magic",
}

-- Stack type (shared with BuffDefinition.stackType)
local StackType = {
    REPLACE = "replace",
    STACK   = "stack",
    REFRESH = "refresh",
}

-- Create weapon definition
-- @param def table: weapon definition data
-- @return WeaponDefinition
local function createWeaponDef(def)
    return {
        -- Identity
        id        = def.id        or error("Weapon id required"),
        name      = def.name      or def.id,
        description = def.description or "",
        weaponType  = def.weaponType or WeaponCategory.MELEE,   -- melee / ranged / magic
        damageType  = def.damageType or DamageType.PHYSICAL,    -- physical / fire / ice / …

        -- ── Fixed / inherent attributes ──
        baseDamage        = def.baseDamage         or 1,   -- weapon damage
        armorPenetration  = def.armorPenetration   or 0,   -- armor penetration
        physicalDamageBonus = def.physicalDamageBonus or 0, -- weapon damage bonus

        -- ── Variable / enchantment attributes ──
        critChance        = def.critChance          or 0,   -- crit rate
        hitRate           = def.hitRate             or 0,   -- hit rate
        staggerRate       = def.staggerRate         or 0,   -- stagger rate
        stunRate          = def.stunRate            or 0,   -- stun rate
        knockbackRate     = def.knockbackRate       or 0,   -- knockback rate
        immobilizeRate    = def.immobilizeRate      or 0,   -- immobilize rate
        critDamageBonus   = def.critDamageBonus     or 0,   -- crit damage bonus
        blockChance       = def.blockChance         or 0,   -- block chance
        blockPower        = def.blockPower          or 0,   -- block power
        bleedChance       = def.bleedChance         or 0,   -- bleed chance
        enchantDamage     = def.enchantDamage       or 0,   -- enchant damage
        limbDamage        = def.limbDamage          or 0,   -- limb damage
        magicDamage       = def.magicDamage         or 0,   -- magic damage
        burnChance        = def.burnChance          or 0,   -- burn chance
        poisonChance      = def.poisonChance        or 0,   -- poison chance
        slowRate          = def.slowRate            or 0,   -- slow rate
        chainChance       = def.chainChance         or 0,   -- chain lightning chance
        magicPenetration  = def.magicPenetration    or 0,   -- magic penetration

        -- Tags (for filtering / UI)
        tags = def.tags or {},
    }
end

-- Built-in weapon definitions
local builtin = {
    -- ── Unarmed ──
    fists = createWeaponDef({
        id          = "fists",
        name        = "Fists",
        description = "Bare hands — no weapon equipped",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 2,
        armorPenetration  = 0,
        physicalDamageBonus = 0,
        tags        = {"unarmed"},
    }),

    -- ── Misc Melee ──
    shortsword = createWeaponDef({
        id          = "shortsword",
        name        = "Shortsword",
        description = "A light blade favored by quick fighters",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 9,
        armorPenetration  = 4,
        physicalDamageBonus = 2,
        hitRate           = 5,
        critChance        = 0.07,
        bleedChance       = 0.08,
        tags        = {"melee", "sword", "one-handed"},
    }),

    fangs = createWeaponDef({
        id          = "fangs",
        name        = "Fangs",
        description = "Natural razor-sharp teeth",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 3,
        armorPenetration  = 1,
        physicalDamageBonus = 0,
        bleedChance       = 0.20,
        critChance        = 0.08,
        tags        = {"melee", "unarmed", "natural"},
    }),

    -- ── Melee — Swords ──
    greatsword = createWeaponDef({
        id          = "greatsword",
        name        = "Greatsword",
        description = "A heavy two-handed sword that deals massive damage",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 18,
        armorPenetration  = 10,
        physicalDamageBonus = 5,
        staggerRate       = 0.15,
        critChance        = 0.05,
        critDamageBonus   = 0.30,
        limbDamage        = 3,
        tags        = {"melee", "sword", "two-handed"},
    }),

    longsword = createWeaponDef({
        id          = "longsword",
        name        = "Longsword",
        description = "A balanced one-handed sword",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 12,
        armorPenetration  = 5,
        physicalDamageBonus = 3,
        critChance        = 0.05,
        critDamageBonus   = 0.25,
        blockChance       = 0.10,
        blockPower        = 3,
        tags        = {"melee", "sword", "one-handed"},
    }),

    iron_sword = createWeaponDef({
        id          = "iron_sword",
        name        = "Iron Sword",
        description = "A standard iron sword",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 10,
        armorPenetration  = 3,
        physicalDamageBonus = 2,
        bleedChance       = 0.10,
        tags        = {"melee", "sword"},
    }),

    flame_blade = createWeaponDef({
        id          = "flame_blade",
        name        = "Flame Blade",
        description = "A sword wreathed in everlasting flame",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.FIRE,
        baseDamage  = 14,
        armorPenetration  = 4,
        physicalDamageBonus = 3,
        enchantDamage     = 6,
        magicDamage       = 2,
        critChance        = 0.05,
        staggerRate       = 0.10,
        tags        = {"melee", "sword", "fire"},
    }),

    ice_blade = createWeaponDef({
        id          = "ice_blade",
        name        = "Ice Blade",
        description = "A sword infused with freezing energy",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.ICE,
        baseDamage  = 14,
        armorPenetration  = 4,
        physicalDamageBonus = 3,
        enchantDamage     = 6,
        magicDamage       = 2,
        slowRate          = 0.15,
        critChance        = 0.05,
        tags        = {"melee", "sword", "ice"},
    }),

    storm_edge = createWeaponDef({
        id          = "storm_edge",
        name        = "Storm Edge",
        description = " crackles with arcane lightning",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.LIGHTNING,
        baseDamage  = 14,
        armorPenetration  = 4,
        physicalDamageBonus = 3,
        enchantDamage     = 6,
        magicDamage       = 2,
        stunRate          = 0.10,
        critChance        = 0.05,
        tags        = {"melee", "sword", "lightning"},
    }),

    -- ── Melee — Axes ──
    battle_axe = createWeaponDef({
        id          = "battle_axe",
        name        = "Battle Axe",
        description = "A heavy axe that cleaves through armor",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 16,
        armorPenetration  = 8,
        physicalDamageBonus = 4,
        staggerRate       = 0.20,
        bleedChance       = 0.15,
        limbDamage        = 4,
        critChance        = 0.04,
        tags        = {"melee", "axe", "two-handed"},
    }),

    hand_axe = createWeaponDef({
        id          = "hand_axe",
        name        = "Hand Axe",
        description = "A light throwing axe, also usable up close",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 8,
        armorPenetration  = 2,
        physicalDamageBonus = 1,
        bleedChance       = 0.10,
        critChance        = 0.06,
        tags        = {"melee", "axe", "one-handed"},
    }),

    -- ── Melee — Daggers ──
    dagger = createWeaponDef({
        id          = "dagger",
        name        = "Dagger",
        description = "A fast, lightweight stabbing weapon",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 6,
        armorPenetration  = 6,
        physicalDamageBonus = 1,
        critChance        = 0.12,
        critDamageBonus   = 0.40,
        hitRate           = 5,
        poisonChance      = 0.10,
        tags        = {"melee", "dagger", "one-handed", "fast"},
    }),

    poison_dagger = createWeaponDef({
        id          = "poison_dagger",
        name        = "Poison Dagger",
        description = "A blade soaked in deadly toxin",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.POISON,
        baseDamage  = 5,
        armorPenetration  = 5,
        physicalDamageBonus = 1,
        enchantDamage     = 8,
        critChance        = 0.10,
        poisonChance      = 0.40,
        tags        = {"melee", "dagger", "one-handed", "poison"},
    }),

    -- ── Melee — Spears ──
    spear = createWeaponDef({
        id          = "spear",
        name        = "Spear",
        description = "A long shafted weapon with reach",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 10,
        armorPenetration  = 4,
        physicalDamageBonus = 2,
        knockbackRate     = 0.20,
        staggerRate       = 0.10,
        limbDamage        = 2,
        critChance        = 0.05,
        tags        = {"melee", "spear", "two-handed"},
    }),

    -- ── Melee — Maces ──
    mace = createWeaponDef({
        id          = "mace",
        name        = "Mace",
        description = "A blunt weapon effective against armored foes",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 11,
        armorPenetration  = 6,
        physicalDamageBonus = 2,
        stunRate          = 0.15,
        staggerRate       = 0.20,
        critChance        = 0.04,
        tags        = {"melee", "mace", "one-handed"},
    }),

    -- ── Melee — Staves (melee mode) ──
    quarterstaff = createWeaponDef({
        id          = "quarterstaff",
        name        = "Quarterstaff",
        description = "A sturdy wooden staff",
        weaponType  = WeaponCategory.MELEE,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 8,
        armorPenetration  = 2,
        physicalDamageBonus = 1,
        blockChance       = 0.20,
        blockPower        = 4,
        knockbackRate     = 0.15,
        staggerRate       = 0.15,
        tags        = {"melee", "staff", "one-handed"},
    }),

    -- ── Magic Staves ──
    fire_staff = createWeaponDef({
        id          = "fire_staff",
        name        = "Fire Staff",
        description = "Channels searing flame through its wielder",
        weaponType  = WeaponCategory.MAGIC,
        damageType  = DamageType.FIRE,
        baseDamage  = 8,
        armorPenetration  = 0,
        physicalDamageBonus = 0,
        magicDamage       = 10,
        enchantDamage     = 8,
        critChance        = 0.06,
        staggerRate       = 0.10,
        burnChance        = 0.25,
        tags        = {"magic", "staff", "fire"},
    }),

    ice_staff = createWeaponDef({
        id          = "ice_staff",
        name        = "Ice Staff",
        description = "Freezes enemies with chilling force",
        weaponType  = WeaponCategory.MAGIC,
        damageType  = DamageType.ICE,
        baseDamage  = 8,
        armorPenetration  = 0,
        physicalDamageBonus = 0,
        magicDamage       = 10,
        enchantDamage     = 8,
        critChance        = 0.06,
        immobilizeRate     = 0.20,
        tags        = {"magic", "staff", "ice"},
    }),

    lightning_staff = createWeaponDef({
        id          = "lightning_staff",
        name        = "Lightning Staff",
        description = "Calls down arcs of storm energy",
        weaponType  = WeaponCategory.MAGIC,
        damageType  = DamageType.LIGHTNING,
        baseDamage  = 8,
        armorPenetration  = 0,
        physicalDamageBonus = 0,
        magicDamage       = 10,
        enchantDamage     = 8,
        critChance        = 0.06,
        stunRate          = 0.15,
        chainChance       = 0.20,
        tags        = {"magic", "staff", "lightning"},
    }),

    arcane_staff = createWeaponDef({
        id          = "arcane_staff",
        name        = "Arcane Staff",
        description = "Concentrates raw arcane power",
        weaponType  = WeaponCategory.MAGIC,
        damageType  = DamageType.ARCANE,
        baseDamage  = 8,
        armorPenetration  = 0,
        physicalDamageBonus = 0,
        magicDamage       = 12,
        enchantDamage     = 6,
        critChance        = 0.07,
        critDamageBonus   = 0.35,
        magicPenetration  = 0.15,
        tags        = {"magic", "staff", "arcane"},
    }),

    -- ── Ranged — Bows ──
    shortbow = createWeaponDef({
        id          = "shortbow",
        name        = "Shortbow",
        description = "A quick and nimble ranged weapon",
        weaponType  = WeaponCategory.RANGED,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 8,
        armorPenetration  = 2,
        physicalDamageBonus = 1,
        hitRate           = 5,
        critChance        = 0.07,
        tags        = {"ranged", "bow"},
    }),

    longbow = createWeaponDef({
        id          = "longbow",
        name        = "Longbow",
        description = "A longbow with greater range and power",
        weaponType  = WeaponCategory.RANGED,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 12,
        armorPenetration  = 4,
        physicalDamageBonus = 2,
        hitRate           = 3,
        critChance        = 0.08,
        critDamageBonus   = 0.20,
        staggerRate       = 0.10,
        limbDamage        = 2,
        tags        = {"ranged", "bow", "two-handed"},
    }),

    fire_bow = createWeaponDef({
        id          = "fire_bow",
        name        = "Fire Bow",
        description = "Arrows ignited with magical fire",
        weaponType  = WeaponCategory.RANGED,
        damageType  = DamageType.FIRE,
        baseDamage  = 9,
        armorPenetration  = 2,
        physicalDamageBonus = 1,
        enchantDamage     = 6,
        magicDamage       = 3,
        hitRate           = 3,
        burnChance        = 0.30,
        tags        = {"ranged", "bow", "fire"},
    }),

    -- ── Ranged — Crossbows ──
    crossbow = createWeaponDef({
        id          = "crossbow",
        name        = "Crossbow",
        description = "A slow but powerful mechanical bow",
        weaponType  = WeaponCategory.RANGED,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 14,
        armorPenetration  = 8,
        physicalDamageBonus = 2,
        hitRate           = 8,
        critChance        = 0.06,
        staggerRate       = 0.15,
        tags        = {"ranged", "crossbow", "two-handed"},
    }),

    -- ── Ranged — Throwing ──
    throwing_knife = createWeaponDef({
        id          = "throwing_knife",
        name        = "Throwing Knife",
        description = "A light balanced blade for throwing",
        weaponType  = WeaponCategory.RANGED,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 5,
        armorPenetration  = 4,
        physicalDamageBonus = 0,
        hitRate           = 10,
        critChance        = 0.10,
        bleedChance       = 0.15,
        tags        = {"ranged", "throwing"},
    }),

    javelin = createWeaponDef({
        id          = "javelin",
        name        = "Javelin",
        description = "A weighted throwing spear",
        weaponType  = WeaponCategory.RANGED,
        damageType  = DamageType.PHYSICAL,
        baseDamage  = 9,
        armorPenetration  = 3,
        physicalDamageBonus = 2,
        hitRate           = 5,
        knockbackRate     = 0.20,
        critChance        = 0.06,
        limbDamage        = 3,
        tags        = {"ranged", "throwing", "spear"},
    }),

    -- ── Ranged — Wands / Rods ──
    fire_wand = createWeaponDef({
        id          = "fire_wand",
        name        = "Fire Wand",
        description = "Channels fire magic through concentrated bolts",
        weaponType  = WeaponCategory.MAGIC,
        damageType  = DamageType.FIRE,
        baseDamage  = 5,
        armorPenetration  = 0,
        physicalDamageBonus = 0,
        magicDamage       = 12,
        enchantDamage     = 5,
        critChance        = 0.06,
        burnChance        = 0.35,
        tags        = {"magic", "wand", "fire"},
    }),

    ice_wand = createWeaponDef({
        id          = "ice_wand",
        name        = "Ice Wand",
        description = "Emits shards of frozen magic",
        weaponType  = WeaponCategory.MAGIC,
        damageType  = DamageType.ICE,
        baseDamage  = 5,
        armorPenetration  = 0,
        physicalDamageBonus = 0,
        magicDamage       = 12,
        enchantDamage     = 5,
        critChance        = 0.06,
        immobilizeRate     = 0.25,
        tags        = {"magic", "wand", "ice"},
    }),
}

-- Default export
return {
    -- Constants
    DamageType     = DamageType,
    WeaponCategory = WeaponCategory,
    StackType      = StackType,

    -- Factory function
    create = createWeaponDef,

    -- Built-in weapon registry
    builtin = builtin,
}
