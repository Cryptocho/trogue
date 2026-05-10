-- AbilityDefinition: 技能定义
-- Mod Layer 数据结构

-- 技能模式
local AbilityMode = {
    ACTIVATED = "activated",     -- 主动技能
    SUSTAINED = "sustained",    -- 持续技能
    PASSIVE = "passive",        -- 被动技能
}

-- 目标类型
local TargetType = {
    SELF = "self",              -- 自身
    SINGLE = "single",          -- 单体目标
    AREA = "area",              -- AOE区域
    LINE = "line",              -- 线性
    CONE = "cone",              -- 锥形
}

-- 效果类型
local EffectType = {
    DAMAGE = "damage",
    HEAL = "heal",
    BUFF = "buff",
    DEBUFF = "debuff",
    TELEPORT = "teleport",
}

-- 创建技能定义
-- @param def table: 技能定义数据
-- @return AbilityDefinition
local function createAbilityDefinition(def)
    return {
        -- 基本信息
        id = def.id or error("Ability id required"),
        name = def.name or def.id,
        description = def.description or "",
        
        -- 技能模式 (MVP只支持activated)
        mode = def.mode or AbilityMode.ACTIVATED,
        
        -- 冷却 (回合数)
        cooldown = def.cooldown or 0,
        minCooldown = def.minCooldown or 0,  -- 最小冷却(天赋减少)
        
        -- 消耗
        cost = def.cost or {},  -- {mp = 10, hp = 0, etc.}
        
        -- 目标选择
        targetType = def.targetType or TargetType.SINGLE,
        range = def.range or 1,        -- 技能范围
        radius = def.radius or 0,      -- AOE半径
        
        -- 效果列表
        effects = def.effects or {},    -- {effectId1, effectId2, ...}
        
        -- 图标/资源
        icon = def.icon or nil,
        
        -- 施放动画时间(可选)
        castTime = def.castTime or 0,
        
        -- 标签
        tags = def.tags or {},
    }
end

-- 默认导出
return {
    -- 常量
    Mode = AbilityMode,
    TargetType = TargetType,
    EffectType = EffectType,
    
    -- 工厂函数
    create = createAbilityDefinition,
    
    -- 内置技能 (MVP用)
    builtin = {
        -- 基础攻击
        punch = createAbilityDefinition({
            id = "punch",
            name = "拳击",
            description = "近战攻击，造成少量伤害",
            mode = AbilityMode.ACTIVATED,
            cooldown = 0,
            cost = {},
            targetType = TargetType.SINGLE,
            range = 1,
            effects = {"damage_physical"},
        }),
        
        -- 治疗
        heal = createAbilityDefinition({
            id = "heal",
            name = "治疗",
            description = "恢复生命值",
            mode = AbilityMode.ACTIVATED,
            cooldown = 3,
            cost = {mp = 5},
            targetType = TargetType.SINGLE,
            range = 3,
            effects = {"heal_minor"},
        }),
        
        -- 火球术
        fireball = createAbilityDefinition({
            id = "fireball",
            name = "火球术",
            description = "对一个区域造成火焰伤害",
            mode = AbilityMode.ACTIVATED,
            cooldown = 4,
            cost = {mp = 15},
            targetType = TargetType.AREA,
            range = 5,
            radius = 2,
            effects = {"damage_fire", "burn"},
        }),
        
        -- 护盾
        shield = createAbilityDefinition({
            id = "shield",
            name = "护盾",
            description = "给自己施加护盾Buff",
            mode = AbilityMode.ACTIVATED,
            cooldown = 5,
            cost = {mp = 10},
            targetType = TargetType.SELF,
            range = 0,
            effects = {"buff_shield"},
        }),
    },
}