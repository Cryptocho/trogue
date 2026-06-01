-- Player Component
-- Marker + progression data for the player-controlled entity
-- reserved for progression system (future)

local PlayerComponent = {
    level = 1,
    currentXP = 0,
    nextLevelXP = 50,
    attributePoints = 0,
    skillPoints = 0,
}

return PlayerComponent