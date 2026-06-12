-- WeaponSystem: Resolve weapon stats from definitions + component overrides
-- Priority: component inline values > weapon definition defaults
-- Supports primary/secondary weapon slots

local WeaponDef = require("src.data.definitions.weapon")

local WeaponSystem = {
    priority = 5,
    name     = "WeaponSystem",
}

function WeaponSystem:init(world)
    self.world = world

    -- Load built-in weapon definitions
    self.definitions = {}
    if WeaponDef.builtin then
        for id, def in pairs(WeaponDef.builtin) do
            self.definitions[id] = def
        end
    end

    -- Fallback: unarmed / fists
    self._fistsDef = self.definitions["fists"]
end

function WeaponSystem:update(world, dt)
end

-- Lookup weapon definition by weaponId
-- @param weaponId string
-- @return WeaponDefinition or nil
function WeaponSystem:getDefinition(weaponId)
    if not weaponId then return self._fistsDef end
    return self.definitions[weaponId] or self._fistsDef
end

-- Resolve final weapon stats for an entity
-- Priority: component inline field > weapon definition default
-- @param entityId number
-- @return table resolved weapon stats (merged from definition + component)
function WeaponSystem:getResolvedStats(entityId)
    local comp = self.world:getComponent(entityId, "Weapon")
    local weaponId = comp and comp.weaponId or "fists"
    local def = self:getDefinition(weaponId)

    if not def then
        return { weaponId = weaponId, weaponType = "melee", damageType = "physical" }
    end

    -- Merge: component inline values take priority over definition defaults
    local resolved = {
        weaponId           = weaponId,
        weaponType         = comp.weaponType         or def.weaponType,
        damageType         = comp.damageType         or def.damageType,

        -- Fixed / inherent
        baseDamage         = comp.baseDamage          or def.baseDamage,
        armorPenetration   = comp.armorPenetration    or def.armorPenetration,
        physicalDamageBonus= comp.physicalDamageBonus or def.physicalDamageBonus,

        -- Variable / enchantment
        critChance         = comp.critChance          or def.critChance,
        hitRate            = comp.hitRate             or def.hitRate,
        staggerRate        = comp.staggerRate         or def.staggerRate,
        stunRate           = comp.stunRate            or def.stunRate,
        knockbackRate      = comp.knockbackRate       or def.knockbackRate,
        immobilizeRate     = comp.immobilizeRate      or def.immobilizeRate,
        critDamageBonus    = comp.critDamageBonus     or def.critDamageBonus,
        blockChance        = comp.blockChance         or def.blockChance,
        blockPower         = comp.blockPower          or def.blockPower,
        bleedChance        = comp.bleedChance         or def.bleedChance,
        enchantDamage      = comp.enchantDamage       or def.enchantDamage,
        limbDamage         = comp.limbDamage          or def.limbDamage,
        magicDamage        = comp.magicDamage         or def.magicDamage,
        burnChance         = comp.burnChance          or def.burnChance,
        poisonChance       = comp.poisonChance        or def.poisonChance,
        slowRate           = comp.slowRate            or def.slowRate,
        chainChance        = comp.chainChance         or def.chainChance,
        magicPenetration   = comp.magicPenetration    or def.magicPenetration,
    }

    return resolved
end

-- Convenience: get weapon definition by id
function WeaponSystem:getDefinitionById(weaponId)
    return self:getDefinition(weaponId)
end

-- Convenience: get base damage only  (kept for backward compatibility with RuleEngine)
function WeaponSystem:getBaseDamage(world, entityId)
    local stats = self:getResolvedStats(entityId)
    return stats.baseDamage
end

-- Convenience: get weapon type string  (kept for backward compatibility)
function WeaponSystem:getWeaponType(world, entityId)
    local stats = self:getResolvedStats(entityId)
    return stats.weaponType
end

-- Convenience: get armor penetration  (kept for backward compatibility)
function WeaponSystem:getArmorPenetration(world, entityId)
    local stats = self:getResolvedStats(entityId)
    return stats.armorPenetration
end

-- Convenience: get physical damage bonus  (kept for backward compatibility)
function WeaponSystem:getPhysicalDamageBonus(world, entityId)
    local stats = self:getResolvedStats(entityId)
    return stats.physicalDamageBonus
end

return WeaponSystem