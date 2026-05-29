-- StatsComponent: Entity attributes (base, current, max, computed, modifiers)
-- Pure data component

local StatsComponent = {
    base = {
        strength = 10,
        agility = 5,
    },
    current = {
        hp = 10,
        mp = 50,
    },
    max = {
        hp = 10,
        mp = 50,
    },
    computed = {
        physicalDamageBonus = 0,
        critChance = 0.05,
        critMultiplier = 1.5,
        poisonDurationMultiplier = 1.0,
    },
    modifiers = {},
}

return StatsComponent