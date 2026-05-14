-- AbilityComponent: 存储实体的能力、冷却、资源
-- ECS 组件，替代 RuleEngine 中的 abilityComponents 缓存

local AbilityComponent = {
    -- 能力列表
    abilities = {},      -- {abilityId, ...}
    -- 冷却时间
    cooldowns = {},      -- {abilityId = remainingTurns}
    -- 资源（MP等）
    resources = {
        mp = 50,
        maxMp = 50,
    },
}

function AbilityComponent:new(values)
    local instance = values or {}
    setmetatable(instance, AbilityComponent)
    
    -- Default values
    if not instance.abilities then instance.abilities = {} end
    if not instance.cooldowns then instance.cooldowns = {} end
    if not instance.resources then
        instance.resources = {
            mp = 50,
            maxMp = 50,
        }
    end
    
    return instance
end

AbilityComponent.__index = AbilityComponent

-- 快捷方法：添加能力
function AbilityComponent:addAbility(abilityId)
    for _, id in ipairs(self.abilities) do
        if id == abilityId then return end
    end
    table.insert(self.abilities, abilityId)
end

-- 快捷方法：设置冷却
function AbilityComponent:setCooldown(abilityId, turns)
    self.cooldowns[abilityId] = turns
end

-- 快捷方法：获取冷却
function AbilityComponent:getCooldown(abilityId)
    return self.cooldowns[abilityId] or 0
end

-- 快捷方法：消耗资源
function AbilityComponent:consumeResource(resource, amount)
    if not self.resources[resource] then return false end
    if self.resources[resource] < amount then return false end
    self.resources[resource] = self.resources[resource] - amount
    return true
end

-- 快捷方法：恢复资源
function AbilityComponent:restoreResource(resource, amount)
    if not self.resources[resource] then return end
    self.resources[resource] = math.min(
        self.resources[resource] + amount,
        self.resources[resource .. "Max"] or self.resources.maxMp or 999
    )
end

return AbilityComponent