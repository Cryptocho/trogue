-- EffectTileComponent: Dynamic effect entity component (poison, fire, etc.)
-- Pure data component, used as special terrain entities, supports spread, stacking, etc.

local EffectTileComponent = {
    effectType = "",   -- "poison", "fire", "ice"...
    damage = 0,        -- Damage per turn
    duration = 0,      -- Remaining turns
    spreadChance = 0,  -- Spread probability
    tickRate = 1,      -- Trigger per turn
    owner = nil,       -- Creator entityId
}

return EffectTileComponent