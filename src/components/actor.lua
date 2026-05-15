-- Actor Component
-- Marker component: Identifies entities that can perform actions (mainly for enemy AI)
-- 
-- Usage:
--   - Entities with this component are processed by AI System
--   - Player entities should NOT have this component (distinguished by Player, not Actor)
--   - Use to query all entities requiring AI decisions: world:query({"Actor", "Position"})
--
-- Properties:
--   - moveDelay: Movement delay (seconds), affects AI movement frequency
--     (This field is reserved for future expansion, current AI uses random movement)
local ActorComponent = {
    moveDelay = 0,  -- Movement interval (seconds), 0 means can move every frame
}

return ActorComponent
