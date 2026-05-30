-- WeaponSystem: Weapon system placeholder
-- TODO: Future extensions —
--   1. Weapon equip/unequip logic
--   2. Weapon attribute bonuses (crit rate, hit rate, etc. from weapon)
--   3. Integration with EquipmentSystem
--   4. Weapon durability system
--   5. Two-handed vs one-handed+shield detection
--   6. Dynamic application of weapon attributes to StatsComponent.computed

local WeaponSystem = {
    priority = 5,
    name = "WeaponSystem",
}

function WeaponSystem:init(world)
    self.world = world
end

function WeaponSystem:update(world, dt)
end

function WeaponSystem:getBaseDamage(world, entityId)
    local weapon = world:getComponent(entityId, "Weapon")
    if weapon then
        return weapon.baseDamage
    end
    return 2
end

function WeaponSystem:getWeaponType(world, entityId)
    local weapon = world:getComponent(entityId, "Weapon")
    if weapon then
        return weapon.type
    end
    return "fists"
end

function WeaponSystem:getArmorPenetration(world, entityId)
    local weapon = world:getComponent(entityId, "Weapon")
    if weapon then
        return weapon.armorPenetration or 0
    end
    return 0
end

function WeaponSystem:getPhysicalDamageBonus(world, entityId)
    local weapon = world:getComponent(entityId, "Weapon")
    if weapon then
        return weapon.physicalDamageBonus or 0
    end
    return 0
end

return WeaponSystem