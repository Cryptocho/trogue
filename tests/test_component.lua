-- Unit tests for Component
-- Run with: lua tests/test_component.lua

local World = require("src.core.ecs").World
local Component = require("src.ecs.component").Component

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

-- Test 1: Component:new()
print("Test: Component:new()")
local comp = Component:new("TestComp")
assertTrue(comp ~= nil, "Component should not be nil")
assertEqual(comp.name, "TestComp", "Component name should match")
assertTrue(comp._isComponent, "Should have _isComponent marker")
print("  PASSED")

-- Test 2: Component:extend() subclass
print("Test: Component:extend() subclass")
local HealthComp = Component:extend("Health")
function HealthComp:new(hp, maxHp)
    local instance = Component.new(self, "Health", {hp = hp, maxHp = maxHp})
    return instance
end

local health = HealthComp:new(100, 100)
assertTrue(health ~= nil, "Subclass should instantiate")
assertEqual(health.name, "Health", "Name should be set")
assertEqual(health.data.hp, 100, "Data should be accessible")
print("  PASSED")

-- Test 3: Component instance with World
print("Test: Component instance with World")
local world = World:new()
local posComp = Component:new("Position", {x = 10, y = 20})
local entityId = world:spawn({Position = posComp})

local results = world:query({"Position"})
assertEqual(#results, 1, "Should find entity with Position")
assertEqual(results[1].components.Position.x, 10, "Component data should be accessible")
print("  PASSED")

-- Test 4: Component attached to entity
print("Test: Component attached to entity")
assertTrue(posComp.entityId == entityId, "Component should be attached to entity")
assertTrue(posComp.world == world, "Component should have world reference")
print("  PASSED")

-- Test 5: Component with plain data tables
print("Test: Component with plain data tables")
local world2 = World:new()
world2:spawn({
    Position = {x = 1, y = 2},  -- plain table
    Health = Component:new("Health", {hp = 50})  -- Component instance
})
local results = world2:query({"Position", "Health"})
assertEqual(#results, 1, "Should find entity with both")
print("  PASSED")

-- Test 6: Component.create() factory
print("Test: Component.create() factory")
local created = Component.create("Velocity", {dx = 5, dy = 10})
assertTrue(created ~= nil, "Created component should not be nil")
assertEqual(created.name, "Velocity", "Name should match")
assertTrue(created.data.dx == 5, "Data should be accessible")
print("  PASSED")

-- Test 7: Despawn pattern (mark + process)
print("Test: Despawn pattern")
local world3 = World:new()
local enemyId = world3:spawn({Position = {x = 0, y = 0}, Health = {hp = 10}})
local playerId = world3:spawn({Position = {x = 1, y = 0}, Health = {hp = 100}})

-- Player kills enemy
world3:despawn(enemyId, "killed by player")

-- Enemy still exists with ShouldDespawn
local marked = world3:query({"ShouldDespawn"})
assertEqual(#marked, 1, "Should have 1 entity marked")

-- Process despawns
local destroyed = world3:processDespawns()
assertEqual(#destroyed, 1, "Should destroy 1 entity")
assertEqual(destroyed[1].id, enemyId, "Should be enemy")

-- Player still alive
local players = world3:query({"Health"})
assertEqual(#players, 1, "Player should still exist")
print("  PASSED")

-- Test 8: getComponentInstance()
print("Test: getComponentInstance()")
local world4 = World:new()
local healthComp = Component:new("Health", {hp = 100})
local playerId = world4:spawn({Health = healthComp})

local retrieved = world4:getComponentInstance(playerId, "Health")
assertTrue(retrieved ~= nil, "Should retrieve component instance")
assertEqual(retrieved.data.hp, 100, "Data should match")
print("  PASSED")

print("")
print("All tests passed!")
print("")

return true