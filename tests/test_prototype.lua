-- Unit tests for PrototypeManager
-- Run with: lua tests/test_prototype.lua

local World = require("src.core.ecs").World
local PrototypeManager = require("src.utils.prototype").PrototypeManager

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

local function assertNil(value, msg)
    if value ~= nil then
        error(msg .. ": expected nil, got " .. tostring(value))
    end
end

-- Test 1: PrototypeManager:new()
print("Test: PrototypeManager:new()")
local pm = PrototypeManager:new()
assertTrue(pm ~= nil, "PrototypeManager should not be nil")
assertTrue(pm.prototypes ~= nil, "prototypes table should exist")
print("  PASSED")

-- Test 2: PrototypeManager:register()
print("Test: PrototypeManager:register()")
local pm2 = PrototypeManager:new()
pm2:register("test", {Position = {x = 1, y = 2}})
assertTrue(pm2:has("test"), "Should have 'test' prototype")
print("  PASSED")

-- Test 3: PrototypeManager:get()
print("Test: PrototypeManager:get()")
local pm3 = PrototypeManager:new()
pm3:register("player", {Position = {x = 10, y = 20}})
local proto = pm3:get("player")
assertTrue(proto ~= nil, "Should get prototype")
assertEqual(proto.Position.x, 10, "Position.x should be 10")
print("  PASSED")

-- Test 4: PrototypeManager:load() with module
print("Test: PrototypeManager:load()")
local pm4 = PrototypeManager:new()
local loaded = pm4:load("data.prototypes.entities")
assertTrue(loaded ~= nil, "Should load module")
assertTrue(loaded.player ~= nil, "Should have player prototype")
print("  PASSED")

-- Test 5: :has() and :getNames()
print("Test: :has() and :getNames()")
local pm5 = PrototypeManager:new()
pm5:register("a", {})
pm5:register("b", {})
local names = pm5:getNames()
assertEqual(#names, 2, "Should have 2 names")
assertTrue(pm5:has("a"), "Should have 'a'")
assertTrue(pm5:has("b"), "Should have 'b'")
assertTrue(not pm5:has("c"), "Should not have 'c'")
print("  PASSED")

-- Test 6: :spawn() with world
print("Test: :spawn() with world")
local pm6 = PrototypeManager:new()
local world = World:new()
pm6:setWorld(world)
pm6:register("enemy", {Position = {x = 5, y = 5}, Health = {hp = 50}})
local entityId = pm6:spawn("enemy")
assertTrue(entityId ~= nil, "Should spawn entity")
local results = world:query({"Position", "Health"})
assertEqual(#results, 1, "Should have 1 entity")
assertEqual(results[1].components.Position.x, 5, "Position.x should be 5")
assertEqual(results[1].components.Health.hp, 50, "Health.hp should be 50")
print("  PASSED")

-- Test 7: :spawn() with overrides
print("Test: :spawn() with overrides")
local pm7 = PrototypeManager:new()
local world2 = World:new()
pm7:setWorld(world2)
pm7:register("enemy", {Position = {x = 0, y = 0}, Health = {hp = 50}})
local entityId = pm7:spawn("enemy", {Position = {x = 100, y = 200}})
local results = world2:query({"Position"})
assertEqual(results[1].components.Position.x, 100, "Position.x should be overridden to 100")
assertEqual(results[1].components.Position.y, 200, "Position.y should be overridden to 200")
-- Health should be original
assertEqual(results[1].components.Health.hp, 50, "Health.hp should remain 50")
print("  PASSED")

-- Test 8: :spawn() with non-existent prototype
print("Test: :spawn() with non-existent prototype")
local pm8 = PrototypeManager:new()
local world3 = World:new()
pm8:setWorld(world3)
local entityId = pm8:spawn("nonexistent")
assertNil(entityId, "Should return nil for non-existent prototype")
print("  PASSED")

-- Test 9: Deep copy preserves original
print("Test: Deep copy preserves original")
local pm9 = PrototypeManager:new()
local world9 = World:new()
pm9:setWorld(world9)
pm9:register("test", {Position = {x = 1, y = 2}})
pm9:spawn("test", {Position = {x = 99}})
local original = pm9:get("test")
assertEqual(original.Position.x, 1, "Original should be unchanged")
print("  PASSED")

-- Test 10: :clear()
print("Test: :clear()")
local pm10 = PrototypeManager:new()
pm10:register("a", {})
pm10:clear()
assertTrue(not pm10:has("a"), "Should not have 'a' after clear")
print("  PASSED")

-- Test 11: Load actual entities file
print("Test: Load actual entities file")
local pm11 = PrototypeManager:new()
pm11:load("data.prototypes.entities")
assertTrue(pm11:has("player"), "Should have player")
assertTrue(pm11:has("rat"), "Should have rat")
assertTrue(pm11:has("goblin"), "Should have goblin")
assertTrue(pm11:has("orc"), "Should have orc")
local player = pm11:get("player")
assertTrue(player.Position ~= nil, "Player should have Position")
assertTrue(player.Health ~= nil, "Player should have Health")
print("  PASSED")

-- Test 12: Spawn from loaded entities
print("Test: Spawn from loaded entities")
local pm12 = PrototypeManager:new()
local world4 = World:new()
pm12:setWorld(world4)
pm12:load("data.prototypes.entities")

local playerId = pm12:spawn("player")
assertTrue(playerId ~= nil, "Should spawn player")
local ratId = pm12:spawn("rat", {Position = {x = 5, y = 10}})
assertTrue(ratId ~= nil, "Should spawn rat with override")

local allEntities = world4:query({"PlayerControlled"})
assertEqual(#allEntities, 1, "Should have 1 player controlled entity")

local allEnemies = world4:query({"AIControlled"})
assertEqual(#allEnemies, 1, "Should have 1 AI controlled entity")
print("  PASSED")

-- Test 13: loadModule() function
print("Test: loadModule() function")
local getter = PrototypeManager.loadModule("data.prototypes.entities")
assertTrue(getter ~= nil, "Should return getter function")
local player = getter("player")
assertTrue(player ~= nil, "Should get player via getter")
print("  PASSED")

print("")
print("All tests passed!")
print("")

return true