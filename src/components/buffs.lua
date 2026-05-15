-- BuffsComponent: Manages all buffs on an entity
-- Pure data component, logic implemented in RuleEngine

local BuffsComponent = {
    activeBuffs = {}, -- {buffId = {duration, stacks, source, definition}}
}

return BuffsComponent