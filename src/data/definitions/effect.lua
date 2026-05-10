-- EffectDefinition: 效果定义
-- Mod Layer 数据结构

-- 效果类型
local EffectType = {
    DAMAGE = "damage",
    HEAL = "heal",
    BUFF = "buff",
    DEBUFF = "debuff",
}

-- 伤害类型
local DamageType = {
    PHYSICAL = "physical",
    FIRE = "fire",
    ICE = "ice",
    LIGHTNING = "lightning",
    POISON = "poison",
    ARCANE = "arcane",
}

-- 创建效果定义
-- @param def table: 效果定义数据
-- @return EffectDefinition
local function createEffectDefinition(def)
    return {
        -- 基本信息
        id = def.id or error("Effect id required"),
        name = def.name or def.id,
        description = def.description or "",
        
        -- 效果类型
        type = def.type or EffectType.DAMAGE,
        
        -- 伤害/治疗数值
        value = def.value or 0,
        valueScale = def.valueScale or {},  -- {stat = "strength", perLevel = 1}
        
        -- 伤害类型 (如果是damage)
        damageType = def.damageType or DamageType.PHYSICAL,
        
        -- Buff/Buf类型引用 (如果是buff/debuff)
        buffId = def.buffId or nil,
        
        -- 持续时间 (如果是持续效果)
        duration = def.duration or 0,
        
        -- 标签
        tags = def.tags or {},
    }
end

-- 默认导出
return {
    -- 常量
    Type = EffectType,
    DamageType = DamageType,
    
    -- 工厂函数
    create = createEffectDefinition,
    
    -- 内置效果 (MVP用)
    builtin = {
        -- 物理伤害
        damage_physical = createEffectDefinition({
            id = "damage_physical",
            name = "物理伤害",
            description = "造成物理伤害",
            type = EffectType.DAMAGE,
            value = 5,
            damageType = DamageType.PHYSICAL,
        }),
        
        -- 火焰伤害
        damage_fire = createEffectDefinition({
            id = "damage_fire",
            name = "火焰伤害",
            description = "造成火焰伤害",
            type = EffectType.DAMAGE,
            value = 8,
            damageType = DamageType.FIRE,
        }),
        
        -- 治愈
        heal_minor = createEffectDefinition({
            id = "heal_minor",
            name = "轻微治疗",
            description = "恢复少量生命",
            type = EffectType.HEAL,
            value = 10,
        }),
        
        -- 护盾Buff
        buff_shield = createEffectDefinition({
            id = "buff_shield",
            name = "护盾",
            description = "获得护盾保护",
            type = EffectType.BUFF,
            buffId = "shield",
            duration = 3,
        }),
        
        -- 燃烧Debuff
        burn = createEffectDefinition({
            id = "burn",
            name = "燃烧",
            description = "每回合受到火焰伤害",
            type = EffectType.DEBUFF,
            buffId = "burning",
            duration = 2,
        }),
        
        -- 燃烧伤害 (DOT tick)
        burn_damage = createEffectDefinition({
            id = "burn_damage",
            name = "燃烧伤害",
            description = "燃烧DOT伤害",
            type = EffectType.DAMAGE,
            value = 3,
            damageType = DamageType.FIRE,
        }),
    },
}