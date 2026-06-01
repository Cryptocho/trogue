-- StatsComponent: Entity attributes (base, current, max, computed, modifiers)
-- Pure data component
-- Pure data component: entity attribute values

local StatsComponent = {
    base = {
        strength = 10,
        agility = 5,
        sensing = 5,
        spirit = 5,
        magic = 5,
        tenacity = 0, -- reserved
    },
    current = {
        hp = 10,
        energy = 50,
    },
    max = {
        hp = 10,
        energy = 50,
    },
    computed = {
        physicalDamageBonus = 0,
        blockChance = 0,
        blockPower = 0,
        dodge = 0,
        hitRate = 0,
        handsPower = 0,
        critChance = 0.05,
        critMultiplier = 1.5,
        fieldOfView = 8,
        sanPower = 0,
        naturalResistance = 0,
        cooling = 0,
        magicPower = 0,
        magicDownFloat = 0,
        magicCooling = 0,
        magicUpFloat = 0,
        counterChance = 0,
        magicResistance = 0,
        darkResistance = 0,
        heroicChance = 0,
        damageAbsorb = 0,
        armorPenetration = 0,
        damageReduction = 0,
    },
    modifiers = {},
    _baseComputed = nil,
}

return StatsComponent