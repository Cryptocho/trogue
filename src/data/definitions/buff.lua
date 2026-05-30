-- BuffDefinition: Buff definitions
-- Mod Layer data structure

-- Buff types
local BuffType = {
    BUFF = "buff",          -- Buff (positive effect)
    DEBUFF = "debuff",       -- Debuff (negative effect)
    DOT = "dot",             -- Damage over time
    HOT = "hot",             -- Healing over time
    SHIELD = "shield",       -- Shield
}

-- Buff stacking behavior
local BuffStackType = {
    REPLACE = "replace",    -- Replace, don't stack
    STACK = "stack",        -- Stack layers
    REFRESH = "refresh",    -- Refresh duration
}

-- Create buff definition
-- @param def table: Buff definition data
-- @return BuffDefinition
local function createBuffDefinition(def)
    return {
        -- Basic info
        id = def.id or error("Buff id required"),
        name = def.name or def.id,
        description = def.description or "",
        
        -- Buff type
        type = def.type or BuffType.BUFF,
        
        -- Stacking behavior
        stackType = def.stackType or BuffStackType.REPLACE,
        maxStack = def.maxStack or 1,
        
        -- Stat modifiers
        statModifiers = def.statModifiers or {},  -- {speed = 1.5, damage = +2}
        
        -- Per-turn effect
        tickEffect = def.tickEffect or nil,  -- effectId, applied once per turn
        
        -- Immunity tag (used to determine which buffs cannot coexist)
        immunityTag = def.immunityTag or nil,
        
        -- Icon
        icon = def.icon or nil,
        
        -- Color (for UI display)
        color = def.color or {1, 1, 1, 1},
    }
end

-- Default export
return {
    -- Constants
    Type = BuffType,
    StackType = BuffStackType,
    
    -- Factory function
    create = createBuffDefinition,
    
    -- Built-in buffs (for MVP)
    builtin = {
        -- Shield
        shield = createBuffDefinition({
            id = "shield",
            name = "Shield",
            description = "Absorbs damage",
            type = BuffType.SHIELD,
            stackType = BuffStackType.REPLACE,
            statModifiers = {
                damageAbsorb = 10,
            },
        }),
        
        -- Burning
        burning = createBuffDefinition({
            id = "burning",
            name = "Burning",
            description = "Takes fire damage each turn",
            type = BuffType.DOT,
            stackType = BuffStackType.REFRESH,
            tickEffect = "burn_damage",
        }),
        
        -- Strength
        strength = createBuffDefinition({
            id = "strength",
            name = "Strength",
            description = "Increases physical damage",
            type = BuffType.BUFF,
            stackType = BuffStackType.STACK,
            maxStack = 3,
            statModifiers = {
                physicalDamageBonus = 3,
            },
        }),

        -- Passive Strength buff (permanent, applied by passive ability)
        passive_strength_buff = createBuffDefinition({
            id = "passive_strength_buff",
            name = "Passive Strength",
            description = "+3 physical damage (passive)",
            type = BuffType.BUFF,
            stackType = BuffStackType.REPLACE,
            maxStack = 1,
            statModifiers = {physicalDamageBonus = 3},
        }),
    },
}
