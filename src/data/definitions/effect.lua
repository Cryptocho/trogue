-- EffectDefinition: Effect definitions
-- Effect definitions (Gameplay Layer)

-- Effect types
local EffectType = {
    DAMAGE = "damage",
    HEAL = "heal",
    BUFF = "buff",
    DEBUFF = "debuff",
    KNOCKBACK = "knockback",
}

-- Damage types
local DamageType = {
    PHYSICAL = "physical",
    FIRE = "fire",
    ICE = "ice",
    LIGHTNING = "lightning",
    POISON = "poison",
    ARCANE = "arcane",
}

-- Create effect definition
-- @param def table: Effect definition data
-- @return EffectDefinition
local function createEffectDefinition(def)
    return {
        -- Basic info
        id = def.id or error("Effect id required"),
        name = def.name or def.id,
        description = def.description or "",
        
        -- Effect type
        type = def.type or EffectType.DAMAGE,
        
        -- Damage/heal value
        value = def.value or 0,
        valueScale = def.valueScale or {},  -- {stat = "strength", perLevel = 1}

        -- Formula-based damage (optional, overrides value when present)
        -- Structure: { basePercent = 0.5, statScaling = {{stat="strength", multiplier=3}, ...}, flatBonus = 0 }
        -- Formula: weaponBaseDamage * basePercent + sum(statValue * multiplier) + flatBonus + weaponPhysicalDamageBonus
        valueFormula = def.valueFormula or nil,
        
        -- Damage type (if damage)
        damageType = def.damageType or DamageType.PHYSICAL,
        
        -- Buff/debuff type reference (if buff/debuff)
        buffId = def.buffId or nil,
        
        -- Duration (if persistent effect)
        duration = def.duration or 0,
        
        -- Tags
        tags = def.tags or {},

        -- Probability check (optional)
        -- chance: number | nil — fixed probability 0~1, nil = 100% trigger
        -- chanceFormula: table | nil — formula-based probability, overrides chance when present
        chance = def.chance or nil,
        chanceFormula = def.chanceFormula or nil,
    }
end

-- Default export
return {
    -- Constants
    Type = EffectType,
    DamageType = DamageType,
    
    -- Factory function
    create = createEffectDefinition,
    
    -- Built-in effects (for MVP)
    builtin = {
        -- Physical damage
        damage_physical = createEffectDefinition({
            id = "damage_physical",
            name = "Physical Damage",
            description = "Deals physical damage",
            type = EffectType.DAMAGE,
            value = 5,
            damageType = DamageType.PHYSICAL,
        }),
        
        -- Fire damage
        damage_fire = createEffectDefinition({
            id = "damage_fire",
            name = "Fire Damage",
            description = "Deals fire damage",
            type = EffectType.DAMAGE,
            value = 8,
            damageType = DamageType.FIRE,
        }),
        
        -- Minor heal
        heal_minor = createEffectDefinition({
            id = "heal_minor",
            name = "Minor Heal",
            description = "Restores minor health",
            type = EffectType.HEAL,
            value = 10,
        }),
        
        -- Shield buff
        buff_shield = createEffectDefinition({
            id = "buff_shield",
            name = "Shield",
            description = "Grants shield protection",
            type = EffectType.BUFF,
            buffId = "shield",
            duration = 3,
        }),
        
        -- Burn debuff
        burn = createEffectDefinition({
            id = "burn",
            name = "Burn",
            description = "Takes fire damage each turn",
            type = EffectType.DEBUFF,
            buffId = "burning",
            duration = 3,  -- 3 ticks total (decremented first, ticked if >= 0)
        }),
        
        -- Burn damage (DOT tick)
        burn_damage = createEffectDefinition({
            id = "burn_damage",
            name = "Burn Damage",
            description = "Burn DOT damage",
            type = EffectType.DAMAGE,
            value = 8,
            damageType = DamageType.FIRE,
        }),

        -- Knockback
        knockback_1 = createEffectDefinition({
            id = "knockback_1",
            name = "Knockback",
            description = "Knocks target back 3 tiles",
            type = EffectType.KNOCKBACK,
            value = 3,
        }),
    },
}
