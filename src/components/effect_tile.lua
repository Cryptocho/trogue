-- EffectTileComponent: 动态效果实体组件（毒、火等）
-- 用作特殊地形实体，支持蔓延、叠加等动态效果

local EffectTileComponent = {
    effectType = "",   -- "poison", "fire", "ice"...
    damage = 0,        -- 每回合伤害
    duration = 0,      -- 剩余回合
    spreadChance = 0,  -- 蔓延概率
    tickRate = 1,      -- 每回合触发
    owner = nil,       -- 创建者entityId
}

function EffectTileComponent:new(values)
    local instance = values or {}
    setmetatable(instance, EffectTileComponent)
    
    -- Default values
    if not instance.effectType then instance.effectType = "" end
    if not instance.damage then instance.damage = 0 end
    if not instance.duration then instance.duration = 0 end
    if not instance.spreadChance then instance.spreadChance = 0 end
    if not instance.tickRate then instance.tickRate = 1 end
    
    return instance
end

EffectTileComponent.__index = EffectTileComponent

-- 创建常见效果类型
function EffectTileComponent:createPoison(damage, duration, spreadChance)
    return EffectTileComponent:new({
        effectType = "poison",
        damage = damage or 5,
        duration = duration or 3,
        spreadChance = spreadChance or 0.3,
    })
end

function EffectTileComponent:createFire(damage, duration, spreadChance)
    return EffectTileComponent:new({
        effectType = "fire",
        damage = damage or 8,
        duration = duration or 2,
        spreadChance = spreadChance or 0.5,
    })
end

function EffectTileComponent:createIce(damage, duration, owner)
    return EffectTileComponent:new({
        effectType = "ice",
        damage = damage or 3,
        duration = duration or 1,
        owner = owner,
    })
end

-- 减少持续时间
function EffectTileComponent:tick()
    self.duration = self.duration - 1
    return self.duration > 0
end

-- 获取效果信息
function EffectTileComponent:getInfo()
    return {
        effectType = self.effectType,
        damage = self.damage,
        duration = self.duration,
        spreadChance = self.spreadChance,
    }
end

return EffectTileComponent