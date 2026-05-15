-- AbilityComponent: Stores entity abilities, cooldowns, and resources
-- Pure data component, logic implemented in AbilitySystem

local AbilityComponent = {
    -- Ability list (Set structure for O(1) lookup)
    abilities = {},      -- {abilityId = true, ...}
    -- Cooldown times
    cooldowns = {},      -- {abilityId = remainingTurns}
    -- Resources (MP, etc.)
    resources = {
        mp = 50,
        maxMp = 50,
    },
}

return AbilityComponent