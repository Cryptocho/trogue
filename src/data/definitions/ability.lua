-- AbilityDefinition: Ability definitions
-- Ability definitions (Gameplay Layer)

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
        cost = def.cost or {},  -- {energy = 10, hp = 0, etc.}
        
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

        -- Passive buff ID (used by applyPassiveAbilities to auto-apply permanent buff)
        passiveBuff = def.passiveBuff or nil,

        -- Range function for mouse aiming: (sourceX, sourceY, targetX, targetY, mapW, mapH) -> {{x,y},...}
        rangeFunc = def.rangeFunc or nil,
    }
end

-- Default export
return {
    -- Constants
    Mode = AbilityMode,
    TargetType = TargetType,
    
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
            rangeFunc = function(sx, sy, tx, ty, mapW, mapH)
                local tiles = {}
                for dy = -1, 1 do
                    for dx = -1, 1 do
                        if dx ~= 0 or dy ~= 0 then
                            local nx, ny = sx + dx, sy + dy
                            if nx >= 1 and nx <= mapW and ny >= 1 and ny <= mapH then
                                table.insert(tiles, {x = nx, y = ny})
                            end
                        end
                    end
                end
                return tiles
            end,
        }),
        
        -- Heal
        heal = createAbilityDefinition({
            id = "heal",
            name = "Heal",
            description = "Restore health to self",
            mode = AbilityMode.ACTIVATED,
            cooldown = 3,
            cost = {energy = 5},
            targetType = TargetType.SELF,
            range = 0,
            effects = {"heal_minor"},
            rangeFunc = function(sx, sy, tx, ty, mapW, mapH)
                return {{x = sx, y = sy}}
            end,
        }),
        
        -- Fireball
        fireball = createAbilityDefinition({
            id = "fireball",
            name = "Fireball",
            description = "Deal fire damage to an area",
            mode = AbilityMode.ACTIVATED,
            cooldown = 4,
            cost = {energy = 15},
            targetType = TargetType.AREA,
            range = 5,
            radius = 2,
            effects = {"damage_fire", "burn"},
            rangeFunc = function(sx, sy, tx, ty, mapW, mapH)
                local tiles = {}
                for dy = -2, 2 do
                    for dx = -2, 2 do
                        if math.max(math.abs(dx), math.abs(dy)) <= 2 and not (dx == 0 and dy == 0) then
                            local nx, ny = sx + dx, sy + dy
                            if nx >= 1 and nx <= mapW and ny >= 1 and ny <= mapH then
                                table.insert(tiles, {x = nx, y = ny})
                            end
                        end
                    end
                end
                return tiles
            end,
        }),
        
        -- Shield
        shield = createAbilityDefinition({
            id = "shield",
            name = "Shield",
            description = "Apply shield buff to self",
            mode = AbilityMode.ACTIVATED,
            cooldown = 5,
            cost = {energy = 10},
            targetType = TargetType.SELF,
            range = 0,
            effects = {"buff_shield"},
            rangeFunc = function(sx, sy, tx, ty, mapW, mapH)
                return {{x = sx, y = sy}}
            end,
        }),

        -- Passive Strength (permanent buff applied on spawn)
        passive_strength = createAbilityDefinition({
            id = "passive_strength",
            name = "Passive Strength",
            description = "Increases physical damage by 3",
            mode = AbilityMode.PASSIVE,
            tags = {"passive"},
            passiveBuff = "passive_strength_buff",
        }),
    },
}
