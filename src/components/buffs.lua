-- BuffsComponent: 管理实体上的所有buff
-- ECS 组件，替代 RuleEngine 中动态创建的 Buffs 数据

local BuffsComponent = {
    activeBuffs = {}, -- {buffId = {duration, stacks, source, definition}}
}

function BuffsComponent:new(values)
    local instance = values or {}
    setmetatable(instance, BuffsComponent)
    
    if not instance.activeBuffs then instance.activeBuffs = {} end
    
    return instance
end

BuffsComponent.__index = BuffsComponent

-- 添加buff
function BuffsComponent:addBuff(buffId, data)
    self.activeBuffs[buffId] = {
        duration = data.duration or 0,
        stacks = data.stacks or 1,
        source = data.source,
        definition = data.definition,
    }
end

-- 获取buff
function BuffsComponent:getBuff(buffId)
    return self.activeBuffs[buffId]
end

-- 检查是否有buff
function BuffsComponent:hasBuff(buffId)
    return self.activeBuffs[buffId] ~= nil
end

-- 移除buff
function BuffsComponent:removeBuff(buffId)
    self.activeBuffs[buffId] = nil
end

-- 减少持续时间
function BuffsComponent:tick()
    for buffId, buffData in pairs(self.activeBuffs) do
        buffData.duration = buffData.duration - 1
        if buffData.duration <= 0 then
            self.activeBuffs[buffId] = nil
            return buffId  -- Return first expired buff for processing
        end
    end
    return nil
end

-- 获取所有buffId列表
function BuffsComponent:getAllBuffIds()
    local ids = {}
    for buffId, _ in pairs(self.activeBuffs) do
        table.insert(ids, buffId)
    end
    return ids
end

-- 获取buff数量
function BuffsComponent:count()
    local c = 0
    for _, _ in pairs(self.activeBuffs) do c = c + 1 end
    return c
end

return BuffsComponent