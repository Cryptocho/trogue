-- WeaponComponent: Entity's equipped weapon data
-- Pure data component
-- TODO: Future full equipment system — this component's data will migrate to EquipmentComponent.weapon slot
-- TODO: Weapon attribute bonuses (armorPenetration/physicalDamageBonus) will be dynamically applied to
--       StatsComponent.computed by WeaponSystem in the future

local WeaponComponent = {
    type = "fists",
    baseDamage = 2,
    armorPenetration = 0,
    physicalDamageBonus = 0,
}

return WeaponComponent