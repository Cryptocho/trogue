-- BuffDefinition: Buff定义
-- Mod Layer 数据结构

-- Buff类型
local BuffType = {
    BUFF = "buff",          -- 增益
    DEBUFF = "debuff",      -- 减益
    DOT = "dot",            -- 持续伤害
    HOT = "hot",            -- 持续治疗
    SHIELD = "shield",      -- 护盾
}

-- Buff效果方式
local BuffStackType = {
    REPLACE = "replace",    -- 覆盖，不叠加
    STACK = "stack",        -- 叠加层数
    REFRESH = "refresh",    -- 刷新持续时间
}

-- 创建Buff定义
-- @param def table: Buff定义数据
-- @return BuffDefinition
local function createBuffDefinition(def)
    return {
        -- 基本信息
        id = def.id or error("Buff id required"),
        name = def.name or def.id,
        description = def.description or "",
        
        -- Buff类型
        type = def.type or BuffType.BUFF,
        
        -- 叠加方式
        stackType = def.stackType or BuffStackType.REPLACE,
        maxStack = def.maxStack or 1,
        
        -- 属性修改
        statModifiers = def.statModifiers or {},  -- {speed = 1.5, damage = +2}
        
        -- 每回合效果
        tickEffect = def.tickEffect or nil,  -- effectId, 每回合应用一次
        
        -- 免疫类型 (用于判断哪些Buff不能同时存在)
        immunityTag = def.immunityTag or nil,
        
        -- 图标
        icon = def.icon or nil,
        
        -- 颜色 (用于UI显示)
        color = def.color or {1, 1, 1, 1},
    }
end

-- 默认导出
return {
    -- 常量
    Type = BuffType,
    StackType = BuffStackType,
    
    -- 工厂函数
    create = createBuffDefinition,
    
    -- 内置Buff (MVP用)
    builtin = {
        -- 护盾
        shield = createBuffDefinition({
            id = "shield",
            name = "护盾",
            description = "吸收伤害",
            type = BuffType.SHIELD,
            stackType = BuffStackType.REPLACE,
            statModifiers = {
                damageAbsorb = 10,
            },
        }),
        
        -- 燃烧
        burning = createBuffDefinition({
            id = "burning",
            name = "燃烧",
            description = "每回合受到火焰伤害",
            type = BuffType.DOT,
            stackType = BuffStackType.REFRESH,
            tickEffect = "burn_damage",
        }),
        
        -- 力量增强
        strength = createBuffDefinition({
            id = "strength",
            name = "力量增强",
            description = "增加物理伤害",
            type = BuffType.BUFF,
            stackType = BuffStackType.STACK,
            maxStack = 3,
            statModifiers = {
                physicalDamageBonus = 3,
            },
        }),
    },
}