-- AbilityComponent: Stores entity abilities and cooldowns
-- Pure data component, logic implemented in RuleEngine

local AbilityComponent = {
    -- Ability list (Set structure for O(1) lookup)
    abilities = {},      -- {abilityId = true, ...}
    -- Cooldown times
    cooldowns = {},      -- {abilityId = remainingTurns}
}

return AbilityComponent