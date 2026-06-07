-- WeaponComponent: Entity's equipped weapon data
-- Pure data component
-- Fields are split into two layers:
--   1. weaponId  →  resolves to a WeaponDefinition (src/data/definitions/weapon.lua)
--   2. inline fields  →  per-entity overrides (shadow definition defaults)
-- Priority: inline > definition > fists fallback

local WeaponComponent = {
    -- Identity
    weaponId   = nil,    -- string  e.g. "greatsword", "fists"
    weaponType = nil,    -- string  "melee" | "ranged" | "magic"

    -- ── Fixed / inherent attributes ──
    baseDamage          = nil,   -- number  weapon damage
    armorPenetration    = nil,   -- number
    physicalDamageBonus = nil,   -- number  weapon damage bonus

    -- ── Variable / enchantment attributes ──
    critChance          = nil,   -- number  crit rate
    hitRate             = nil,   -- number  hit rate
    staggerRate         = nil,   -- number  stagger rate
    stunRate            = nil,   -- number  stun rate
    knockbackRate       = nil,   -- number  knockback rate
    immobilizeRate      = nil,   -- number  immobilize rate
    critDamageBonus     = nil,   -- number  crit damage bonus
    blockChance         = nil,   -- number  block chance
    blockPower          = nil,   -- number  block power
    bleedChance         = nil,   -- number  bleed chance
    enchantDamage       = nil,   -- number  enchant damage
    limbDamage          = nil,   -- number  limb damage
    magicDamage         = nil,   -- number  magic damage

    -- ── Future / reserved ──
    -- durability   = nil,   -- number  weapon durability
    -- enchantment  = nil,   -- string  current enchantment id
    -- runeSlots    = nil,   -- table   rune slot configuration
}

return WeaponComponent