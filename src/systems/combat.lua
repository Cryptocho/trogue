-- Combat System
-- Placeholder for future combat mechanics (formerly collision-based bump combat)
-- Combat is now driven exclusively through the RuleEngine ability system

local CombatSystem = {
    priority = 2,
    name = "CombatSystem",
}

function CombatSystem:init(world)
    self.world = world
    self.events = world.eventBus
end

function CombatSystem:update(world, dt)
end

return CombatSystem