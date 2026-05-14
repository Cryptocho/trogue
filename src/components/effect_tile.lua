-- EffectTileComponent: 动态效果实体组件（毒、火等）
-- 纯数据组件，用作特殊地形实体，支持蔓延、叠加等动态效果

local EffectTileComponent = {
    effectType = "",   -- "poison", "fire", "ice"...
    damage = 0,        -- 每回合伤害
    duration = 0,      -- 剩余回合
    spreadChance = 0,  -- 蔓延概率
    tickRate = 1,      -- 每回合触发
    owner = nil,       -- 创建者entityId
}

return EffectTileComponent