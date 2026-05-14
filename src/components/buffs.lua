-- BuffsComponent: 管理实体上的所有buff
-- 纯数据组件，方法逻辑在 RuleEngine 中实现

local BuffsComponent = {
    activeBuffs = {}, -- {buffId = {duration, stacks, source, definition}}
}

return BuffsComponent