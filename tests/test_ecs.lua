-- Unit tests for ECS Core
-- Run with: lua tests/test_ecs.lua

local World = require("src.core.ecs").World

-- Test helper
local function assertEqual(actual, expected, msg)
    if actual ~= expected then
        error(msg .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual))
    end
end

local function assertTrue(value, msg)
    if not value then
        error(msg .. ": expected truthy, got " .. tostring(value))
    end
end

-- Test 1: World:new()
print("Test: World:new()")
local world = World:new()
assertTrue(world ~= nil, "World should not be nil")
assertTrue(world.nextEntityId == 1, "Initial entity ID should be 1")
print("  PASSED")

-- Test 2: World:spawn()
print("Test: World:spawn(components)")
local id = world:spawn({Position = {x = 1, y = 2}})
assertEqual(id, 1, "First entity should have ID 1")
assertEqual(world.nextEntityId, 2, "Next ID should be 2")
print("  PASSED")

-- Test 3: Entity has components
print("Test: Entity has spawned components")
local entity = world.entities[id]
assertTrue(entity ~= nil, "Entity should exist")
assertTrue(entity.Position ~= nil, "Entity should have Position")
assertEqual(entity.Position.x, 1, "Position.x should be 1")
assertEqual(entity.Position.y, 2, "Position.y should be 2")
print("  PASSED")

-- Test 4: World:query() with single component
print("Test: World:query() single component")
local results = world:query({"Position"})
assertEqual(#results, 1, "Should find 1 entity with Position")
assertEqual(results[1].id, 1, "Should return entity with correct ID")
print("  PASSED")

-- Test 5: World:spawn() multiple entities
print("Test: World:spawn() multiple entities")
local id2 = world:spawn({Position = {x = 3, y = 4}, Health = {hp = 100}})
local id3 = world:spawn({Position = {x = 5, y = 6}})
assertEqual(id2, 2, "Second entity should have ID 2")
assertEqual(id3, 3, "Third entity should have ID 3")
print("  PASSED")

-- Test 6: World:query() with multiple components
print("Test: World:query() multiple components")
local results = world:query({"Position", "Health"})
assertEqual(#results, 1, "Should find 1 entity with Position AND Health")
assertEqual(results[1].id, 2, "Should return entity ID 2")
print("  PASSED")

-- Test 7: World:query() non-existent component
print("Test: World:query() non-existent component")
local results = world:query({"NonExistent"})
assertEqual(#results, 0, "Should find 0 entities")
print("  PASSED")

-- Test 8: World:despawn() marks entity, processDespawns() destroys
print("Test: Despawn pattern (mark + process)")
world:despawn(2)
-- Entity 2 still exists with ShouldDespawn
local results = world:query({"ShouldDespawn"})
assertEqual(#results, 1, "Should find 1 entity marked for despawn")
-- Entity 2 still has Position (not destroyed yet)
local results2 = world:query({"Position", "ShouldDespawn"})
assertEqual(#results2, 1, "Entity 2 still has Position")

-- Process the despawns
local destroyed = world:processDespawns()
assertEqual(#destroyed, 1, "Should destroy 1 entity")
assertEqual(destroyed[1].id, 2, "Should be entity 2")

-- Now entity 2 is truly gone
local results3 = world:query({"Position", "Health"})
assertEqual(#results3, 0, "Entity 2 should no longer have Health")
local results4 = world:query({"Position"})
assertEqual(#results4, 2, "Should have 2 entities with Position (1 and 3)")
print("  PASSED")

-- Test 9: World:addSystem()
print("Test: World:addSystem()")
local systemCallCount = 0
local testSystem = {
    priority = 10,
    update = function(self, dt, world)
        systemCallCount = systemCallCount + 1
    end
}
world:addSystem(testSystem)
assertEqual(#world.systems, 1, "Should have 1 system registered")
print("  PASSED")

-- Test 10: World:update()
print("Test: World:update()")
world:update(0.016)
assertEqual(systemCallCount, 1, "System update should have been called once")
world:update(0.016)
assertEqual(systemCallCount, 2, "System update should have been called twice")
print("  PASSED")

-- Test 11: System priority ordering
print("Test: System priority ordering")
local world2 = World:new()
local order = {}
world2:addSystem({
    priority = 100,
    update = function(self, dt, w) table.insert(order, 3) end
})
world2:addSystem({
    priority = 1,
    update = function(self, dt, w) table.insert(order, 1) end
})
world2:addSystem({
    priority = 50,
    update = function(self, dt, w) table.insert(order, 2) end
})
world2:update(0)
assertEqual(order[1], 1, "Priority 1 should run first")
assertEqual(order[2], 2, "Priority 50 should run second")
assertEqual(order[3], 3, "Priority 100 should run third")
print("  PASSED")

-- Test 12: System init callback
print("Test: System init callback")
local initCalled = false
local initWorldRef = nil
local world3 = World:new()
world3:addSystem({
    init = function(self, world)
        initCalled = true
        initWorldRef = world
    end,
    update = function(self, dt, w) end
})
assertTrue(initCalled, "Init should be called")
assertTrue(initWorldRef == world3, "Init should receive world reference")
print("  PASSED")

-- Test 13: Multiple component additions
print("Test: World:addComponent() after spawn")
local world4 = World:new()
local id = world4:spawn({Position = {x = 0, y = 0}})
world4:addComponent(id, "Velocity", {dx = 1, dy = 2})
local results = world4:query({"Position", "Velocity"})
assertEqual(#results, 1, "Should find entity with both components")
print("  PASSED")

-- Test 14: Component removal
print("Test: World:removeComponent()")
local world5 = World:new()
local id = world5:spawn({Position = {x = 0}, Velocity = {dx = 1}})
world5:removeComponent(id, "Velocity")
local results = world5:query({"Position", "Velocity"})
assertEqual(#results, 0, "Should not find entity with Velocity after removal")
local results2 = world5:query({"Position"})
assertEqual(#results2, 1, "Should still find entity with Position")
print("  PASSED")

-- Test 15: CleanupSystem pattern
print("Test: CleanupSystem pattern")
local world6 = World:new()
local cleanupCalled = false
local CleanupSystem = {
    priority = 1000,  -- Run last
    update = function(self, dt, world)
        -- Custom cleanup logic before despawn
        local toCleanup = world:query({"ShouldDespawn"})
        for _, result in ipairs(toCleanup) do
            cleanupCalled = true
            -- Custom logic here (e.g., drop items, play sound)
        end
        -- Then process despawns
        world:processDespawns()
    end
}
world6:addSystem(CleanupSystem)
local enemyId = world6:spawn({Position = {x = 5, y = 5}, Health = {hp = 10}})
world6:despawn(enemyId, "killed by player")
world6:update(0.016)
assertTrue(cleanupCalled, "CleanupSystem should be called")
local remaining = world6:query({"Position", "Health"})
assertEqual(#remaining, 0, "Entity should be destroyed after cleanup")
print("  PASSED")

print("")
print("All tests passed!")
print("")

-- Acceptance criteria test
print("=== Acceptance Criteria Test ===")
local acceptanceWorld = World:new()
local acceptanceId = acceptanceWorld:spawn({Position = {x=1, y=2}})
local acceptanceResults = acceptanceWorld:query({"Position"})
assertTrue(#acceptanceResults == 1, "Should have 1 result")
assertEqual(acceptanceResults[1].id, acceptanceId, "Result ID should match spawned ID")
print("ACCEPTANCE TEST PASSED: World:spawn() and World:query() work correctly")
print("")

return true