-- Unit tests for System base class
-- Run with: lua tests/test_system.lua

local World = require("src.core.ecs").World
local System = require("src.ecs.system").System

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

local function assertFalse(value, msg)
    if value then
        error(msg .. ": expected falsy, got " .. tostring(value))
    end
end

-- Test 1: System:new() with defaults
print("Test: System:new() with defaults")
local sys = System:new()
assertTrue(sys ~= nil, "System should not be nil")
assertEqual(sys.priority, 0, "Default priority should be 0")
assertTrue(sys.enabled, "Should be enabled by default")
assertEqual(sys.name, "System", "Default name should be 'System'")
print("  PASSED")

-- Test 2: System:new() with options
print("Test: System:new() with options")
local sys2 = System:new({priority = 50, enabled = false, name = "TestSys"})
assertEqual(sys2.priority, 50, "Priority should be 50")
assertFalse(sys2.enabled, "Should be disabled")
assertEqual(sys2.name, "TestSys", "Name should be 'TestSys'")
print("  PASSED")

-- Test 3: System subclass
print("Test: System subclass with override")
local UpdateCount = 0
local TestSystem = System:extend("TestSystem")
function TestSystem:update(dt, world)
    UpdateCount = UpdateCount + 1
end

local ts = TestSystem:new({name = "Counter"})
assertEqual(ts.name, "Counter", "Subclass should use custom name")
print("  PASSED")

-- Test 4: System:init() sets world
print("Test: System:init() sets world")
local sys3 = System:new()
local world = World:new()
sys3:init(world)
assertTrue(sys3.world == world, "World should be set")
print("  PASSED")

-- Test 5: System:onAddToWorld()
print("Test: System:onAddToWorld()")
local sys4 = System:new()
local world2 = World:new()
sys4:onAddToWorld(world2)
assertTrue(sys4.world == world2, "World should be set via onAddToWorld")
print("  PASSED")

-- Test 6: System:enable() / disable()
print("Test: System:enable() / disable()")
local sys5 = System:new({enabled = false})
assertFalse(sys5.enabled, "Should start disabled")
sys5:enable()
assertTrue(sys5.enabled, "Should be enabled after enable()")
sys5:disable()
assertFalse(sys5.enabled, "Should be disabled after disable()")
print("  PASSED")

-- Test 7: System:isEnabled()
print("Test: System:isEnabled()")
local sys6 = System:new({enabled = true})
assertTrue(sys6:isEnabled(), "Should return true when enabled")
sys6:disable()
assertFalse(sys6:isEnabled(), "Should return false when disabled")
print("  PASSED")

-- Test 8: System integrated with World
print("Test: System integrated with World")
local world3 = World:new()
local initCalled = false
local initWorldRef = nil
local TestSys = System:extend("TestSys")
function TestSys:init(w)
    initCalled = true
    initWorldRef = w
end

local ts2 = TestSys:new()
world3:addSystem(ts2)
assertTrue(initCalled, "Init should be called on addSystem")
assertTrue(initWorldRef == world3, "Init should receive world")
print("  PASSED")

-- Test 9: System update called via World:update()
print("Test: System update called via World:update()")
local world4 = World:new()
UpdateCount = 0
local UpdateSys = System:extend("UpdateSys")
function UpdateSys:update(dt, world)
    UpdateCount = UpdateCount + 1
end
local us = UpdateSys:new()
world4:addSystem(us)

world4:update(0.016)
assertEqual(UpdateCount, 1, "Update should be called once")
world4:update(0.016)
assertEqual(UpdateCount, 2, "Update should be called twice")
print("  PASSED")

-- Test 10: Disabled system skipped in update
print("Test: Disabled system skipped in update")
local world5 = World:new()
UpdateCount = 0
local DisabledSys = System:extend("DisabledSys")
function DisabledSys:update(dt, world)
    UpdateCount = UpdateCount + 1
end
local ds = DisabledSys:new({enabled = false})
world5:addSystem(ds)

world5:update(0.016)
assertEqual(UpdateCount, 0, "Update should not be called for disabled system")
print("  PASSED")

-- Test 11: System priority ordering
print("Test: System priority ordering")
local world6 = World:new()
local order = {}
local PrioritySys1 = System:extend("PrioritySys1")
function PrioritySys1:update(dt, w) table.insert(order, 1) end
local PrioritySys2 = System:extend("PrioritySys2")
function PrioritySys2:update(dt, w) table.insert(order, 2) end
local PrioritySys3 = System:extend("PrioritySys3")
function PrioritySys3:update(dt, w) table.insert(order, 3) end

world6:addSystem(PrioritySys1:new({priority = 100}))
world6:addSystem(PrioritySys2:new({priority = 1}))
world6:addSystem(PrioritySys3:new({priority = 50}))

world6:update(0)
assertEqual(order[1], 2, "Priority 1 should run first")
assertEqual(order[2], 3, "Priority 50 should run second")
assertEqual(order[3], 1, "Priority 100 should run third")
print("  PASSED")

-- Test 12: System:shutdown()
print("Test: System:shutdown()")
local sys7 = System:new()
sys7:init(world)
sys7:shutdown()
assertTrue(sys7.world == nil, "World should be nil after shutdown")
print("  PASSED")

-- Test 13: System name get/set
print("Test: System name get/set")
local sys8 = System:new({name = "Original"})
assertEqual(sys8:getName(), "Original", "getName should return 'Original'")
sys8:setName("NewName")
assertEqual(sys8:getName(), "NewName", "getName should return 'NewName'")
print("  PASSED")

-- Test 14: System:onRemoveFromWorld()
print("Test: System:onRemoveFromWorld()")
local world8 = World:new()
local removedSys = System:new()
world8:addSystem(removedSys)
assertTrue(removedSys.world ~= nil, "System should have world after add")
removedSys:onRemoveFromWorld()
assertTrue(removedSys.world == nil, "System should have nil world after onRemoveFromWorld")
print("  PASSED")

print("")
print("All tests passed!")
print("")

return true