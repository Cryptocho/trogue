-- AbilityDefinition: Ability definitions
-- Mod Layer data structure

-- Ability modes
local AbilityMode = {
    ACTIVATED = "activated",     -- Activated ability
    SUSTAINED = "sustained",     -- Sustained ability
    PASSIVE = "passive",         -- Passive ability
}

-- Target types
local TargetType = {
    SELF = "self",               -- Self
    SINGLE = "single",           -- Single target
    AREA = "area",               -- AOE area
    LINE = "line",               -- Linear
    CONE = "cone",               -- Cone
}

-- Effect types
local EffectType = {
    DAMAGE = "damage",
    HEAL = "heal",
    BUFF = "buff",
    DEBUFF = "debuff",
    TELEPORT = "teleport",
}

-- Create ability definition
-- @param def table: Ability definition data
-- @return AbilityDefinition
local function createAbilityDefinition(def)
    return {
        -- Basic info
        id = def.id or error("Ability id required"),
        name = def.name or def.id,
        description = def.description or "",
        
        -- Ability mode (MVP only supports activated)
        mode = def.mode or AbilityMode.ACTIVATED,
        
        -- Cooldown (turns)
        cooldown = def.cooldown or 0,
        minCooldown = def.minCooldown or 0,  -- Min cooldown (talent reduction)
        
        -- Cost
        cost = def.cost or {},  -- {mp = 10, hp = 0, etc.}
        
        -- Target selection
        targetType = def.targetType or TargetType.SINGLE,
        range = def.range or 1,        -- Ability range
        radius = def.radius or 0,       -- AOE radius
        
        -- Effect list
        effects = def.effects or {},    -- {effectId1, effectId2, ...}
        
        -- Icon/resource
        icon = def.icon or nil,
        
        -- Cast animation time (optional)
        castTime = def.castTime or 0,
        
        -- Tags
        tags = def.tags or {},
    }
end

-- Default export
return {
    -- Constants
    Mode = AbilityMode,
    TargetType = TargetType,
    EffectType = EffectType,
    
    -- Factory function
    create = createAbilityDefinition,
    
    -- Built-in abilities (for MVP)
    builtin = {
        -- Basic attack
        punch = createAbilityDefinition({
            id = "punch",
            name = "Punch",
            description = "Melee attack, deals minor damage",
            mode = AbilityMode.ACTIVATED,
            cooldown = 0,
            cost = {},
            targetType = TargetType.SINGLE,
            range = 1,
            effects = {"damage_physical"},
        }),
        
        -- Heal
        heal = createAbilityDefinition({
            id = "heal",
            name = "Heal",
            description = "Restore health to self",
            mode = AbilityMode.ACTIVATED,
            cooldown = 3,
            cost = {mp = 5},
            targetType = TargetType.SELF,
            range = 0,
            effects = {"heal_minor"},
        }),
        
        -- Fireball
        fireball = createAbilityDefinition({
            id = "fireball",
            name = "Fireball",
            description = "Deal fire damage to an area",
            mode = AbilityMode.ACTIVATED,
            cooldown = 4,
            cost = {mp = 15},
            targetType = TargetType.AREA,
            range = 5,
            radius = 2,
            effects = {"damage_fire", "burn"},
        }),
        
        -- Shield
        shield = createAbilityDefinition({
            id = "shield",
            name = "Shield",
            description = "Apply shield buff to self",
            mode = AbilityMode.ACTIVATED,
            cooldown = 5,
            cost = {mp = 10},
            targetType = TargetType.SELF,
            range = 0,
            effects = {"buff_shield"},
        }),
    },
}
