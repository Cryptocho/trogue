-- AbilityComponent: 存储实体的能力、冷却、资源
-- 纯数据组件，方法逻辑在 AbilitySystem 中实现

local AbilityComponent = {
    -- 能力列表（Set结构，用于O(1)查询）
    abilities = {},      -- {abilityId = true, ...}
    -- 冷却时间
    cooldowns = {},      -- {abilityId = remainingTurns}
    -- 资源（MP等）
    resources = {
        mp = 50,
        maxMp = 50,
    },
}

return AbilityComponent