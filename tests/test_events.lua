-- Unit tests for EventBus
-- Run with: lua tests/test_events.lua

local EventBus = require("src.core.events").EventBus

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

-- Test 1: EventBus:new()
print("Test: EventBus:new()")
local bus = EventBus:new()
assertTrue(bus ~= nil, "EventBus should not be nil")
assertTrue(bus.listeners ~= nil, "Listeners table should exist")
print("  PASSED")

-- Test 2: EventBus:on() and emit
print("Test: EventBus:on() and emit()")
local called = false
local receivedData = nil
bus:on("TestEvent", function(data)
    called = true
    receivedData = data
end)
bus:emit("TestEvent", {value = 42})
assertTrue(called, "Handler should have been called")
assertEqual(receivedData.value, 42, "Data should be passed to handler")
print("  PASSED")

-- Test 3: Multiple handlers
print("Test: Multiple handlers")
local callCount = 0
local bus2 = EventBus:new()
bus2:on("MultiEvent", function() callCount = callCount + 1 end)
bus2:on("MultiEvent", function() callCount = callCount + 10 end)
bus2:emit("MultiEvent")
assertEqual(callCount, 11, "Both handlers should be called")
print("  PASSED")

-- Test 4: Emit to non-existent event
print("Test: Emit to non-existent event (no crash)")
local bus3 = EventBus:new()
bus3:emit("NonExistentEvent", {})  -- Should not crash
print("  PASSED")

-- Test 5: Multiple subscribers
print("Test: Multiple subscribers different events")
local results = {}
local bus4 = EventBus:new()
bus4:on("EventA", function(d) results.A = d.value end)
bus4:on("EventB", function(d) results.B = d.value end)
bus4:emit("EventA", {value = 1})
bus4:emit("EventB", {value = 2})
assertEqual(results.A, 1, "EventA handler should receive 1")
assertEqual(results.B, 2, "EventB handler should receive 2")
print("  PASSED")

-- Test 6: EventBus:clear()
print("Test: EventBus:clear()")
local bus5 = EventBus:new()
local clearCalled = false
bus5:on("ClearEvent", function() clearCalled = true end)
bus5:clear()
bus5:emit("ClearEvent")
assertTrue(not clearCalled, "Handler should not be called after clear")
print("  PASSED")

-- Test 7: EventBus:count()
print("Test: EventBus:count()")
local bus6 = EventBus:new()
assertEqual(bus6:count("NoListeners"), 0, "Count should be 0 for no listeners")
bus6:on("HasOne", function() end)
assertEqual(bus6:count("HasOne"), 1, "Count should be 1")
bus6:on("HasTwo", function() end)
bus6:on("HasTwo", function() end)
assertEqual(bus6:count("HasTwo"), 2, "Count should be 2")
print("  PASSED")

-- Test 8: Priority ordering
print("Test: Priority ordering")
local order = {}
local bus7 = EventBus:new()
bus7:on("PriorityEvent", function() table.insert(order, 3) end, 100)
bus7:on("PriorityEvent", function() table.insert(order, 1) end, 1)
bus7:on("PriorityEvent", function() table.insert(order, 2) end, 50)
bus7:emit("PriorityEvent")
assertEqual(order[1], 1, "Priority 1 should run first")
assertEqual(order[2], 2, "Priority 50 should run second")
assertEqual(order[3], 3, "Priority 100 should run third")
print("  PASSED")

-- Test 9: Unsubscribe via returned function
print("Test: Unsubscribe via returned function")
local bus8 = EventBus:new()
local callCount8 = 0
local handler = function() callCount8 = callCount8 + 1 end
local unsubscribe = bus8:on("UnsubEvent", handler)
bus8:emit("UnsubEvent")
assertEqual(callCount8, 1, "Should be called once before unsubscribe")
unsubscribe()
bus8:emit("UnsubEvent")
assertEqual(callCount8, 1, "Should still be 1 after unsubscribe")
print("  PASSED")

-- Test 10: Child EventBus inherits parent
print("Test: Child EventBus inherits parent")
local parentBus = EventBus:new()
local childBus = parentBus:child()
local parentCalled = false
parentBus:on("ParentEvent", function() parentCalled = true end)
childBus:emit("ParentEvent")
assertTrue(parentCalled, "Child should emit to parent's listeners")
print("  PASSED")

-- Test 11: emitTo (directed event)
print("Test: emitTo (directed event)")
local directedBus = EventBus:new()
local targetReceived = nil
directedBus:on("Damage", function(data) targetReceived = data end)
directedBus:emitTo(5, "Damage", {amount = 10})
assertTrue(targetReceived ~= nil, "Handler should be called")
assertEqual(targetReceived.amount, 10, "Data should be passed")
assertEqual(targetReceived.target, 5, "Target should be set")
print("  PASSED")

-- Test 12: emitToMany (multi-directed event)
print("Test: emitToMany (multi-directed)")
local multiBus = EventBus:new()
local hitTargets = {}
multiBus:on("Explosion", function(data) 
    table.insert(hitTargets, data.target) 
end)
multiBus:emitToMany({1, 2, 3}, "Explosion", {})
assertEqual(#hitTargets, 3, "Should have 3 targets")
print("  PASSED")

print("")
print("All tests passed!")
print("")

-- Acceptance criteria test
print("=== Acceptance Criteria Test ===")
local acceptanceBus = EventBus:new()
local acceptanceCalled = false
acceptanceBus:on("TestEvent", function(d) acceptanceCalled = true end)
acceptanceBus:emit("TestEvent", {})
assertTrue(acceptanceCalled, "AC: called should be true after emit")
print("ACCEPTANCE TEST PASSED: EventBus works correctly")
print("")

return true