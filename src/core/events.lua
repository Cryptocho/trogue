-- EventBus Implementation

local EventBus = {}
EventBus.__index = EventBus

function EventBus:new()
    local instance = {
        -- Raw listener lists (unsorted)
        listeners = {},
        -- Cached sorted handlers
        sortedHandlers = {},
        -- Dirty flags: eventName -> true means needs resort
        dirty = {},
    }
    setmetatable(instance, EventBus)
    return instance
end

--- Rebuild sorted handler cache for an event
function EventBus:_rebuild(eventName)
    local raw = self.listeners[eventName]
    if not raw then
        self.sortedHandlers[eventName] = {}
        return
    end
    
    -- Copy and sort by priority
    local sorted = {}
    for i, entry in ipairs(raw) do
        sorted[i] = entry
    end
    
    table.sort(sorted, function(a, b) return a.priority < b.priority end)
    
    self.sortedHandlers[eventName] = sorted
    self.dirty[eventName] = false
end

--- Get handlers for an event, optionally filtered by targetId
-- @param eventName string
-- @param targetId number|nil: if provided, only return handlers matching this targetId
-- @return array: handler entries
function EventBus:_getHandlers(eventName, targetId)
    if self.dirty[eventName] then
        self:_rebuild(eventName)
    end
    
    local all = self.sortedHandlers[eventName]
    if not all then
        return {}
    end
    
    if targetId == nil then
        return all
    end
    
    -- Filter by targetId (nil targetId means "all" handlers)
    local filtered = {}
    for _, entry in ipairs(all) do
        if entry.targetId == nil or entry.targetId == targetId then
            table.insert(filtered, entry)
        end
    end
    return filtered
end

--- Subscribe to an event
-- @param eventName string: name of the event
-- @param handler function: callback function(data)
-- @param priority number: optional, lower = called first (default 0)
-- @param targetId number: optional, if set, only emitTo(targetId) will trigger this handler
-- @return function: unsubscribe handler
function EventBus:on(eventName, handler, priority, targetId)
    if priority == nil then
        priority = 0
    end
    
    if not self.listeners[eventName] then
        self.listeners[eventName] = {}
    end
    
    table.insert(self.listeners[eventName], {
        handler = handler,
        priority = priority,
        targetId = targetId
    })
    
    -- Mark as dirty (needs resort)
    self.dirty[eventName] = true
    
    -- Return unsubscribe function
    local bus = self
    local event = eventName
    local listenerIndex = #self.listeners[eventName]
    return function()
        bus:offByIndex(event, listenerIndex)
    end
end

--- Unsubscribe a handler by event and index
-- @param eventName string
-- @param index number: handler index to remove
function EventBus:off(eventName, index)
    if self.listeners[eventName] then
        table.remove(self.listeners[eventName], index)
        self.dirty[eventName] = true  -- Mark dirty
    end
end

--- Unsubscribe by listener reference (used by returned function)
function EventBus:offByIndex(eventName, index)
    self:off(eventName, index)
end

--- Emit an event to all subscribers (broadcast)
-- @param eventName string
-- @param data table: event data passed to handlers
function EventBus:emit(eventName, data)
    -- Collect from self and parent chain, rebuilding caches as needed
    local allHandlers = {}
    local bus = self
    
    while bus do
        -- Rebuild cache if dirty for this bus
        if bus.dirty[eventName] then
            bus:_rebuild(eventName)
        end
        
        if bus.sortedHandlers[eventName] then
            for _, entry in ipairs(bus.sortedHandlers[eventName]) do
                table.insert(allHandlers, entry)
            end
        end
        bus = bus.parent
    end
    
    -- Emit to collected handlers
    for _, entry in ipairs(allHandlers) do
        entry.handler(data)
    end
end

--- Emit an event to a specific target entity (directed event)
-- Only handlers with matching targetId (or nil targetId) will be called
-- @param targetId number: entity id that should handle this event
-- @param eventName string
-- @param data table: event data passed to handlers
function EventBus:emitTo(targetId, eventName, data)
    local handlers = self:_getHandlers(eventName, targetId)
    if #handlers == 0 then
        return
    end
    
    -- Add target to data if not present
    if data and data.target == nil then
        data.target = targetId
    end
    
    for _, entry in ipairs(handlers) do
        entry.handler(data)
    end
end

--- Emit an event to multiple target entities (multi-directed)
-- Use for explosions: "All entities in range receive damage"
-- @param targetIds array: {entityId1, entityId2, ...}
-- @param eventName string
-- @param data table
function EventBus:emitToMany(targetIds, eventName, data)
    -- Rebuild cache once before the loop
    if self.dirty[eventName] then
        self:_rebuild(eventName)
    end
    
    local allHandlers = self.sortedHandlers[eventName] or {}
    
    for _, targetId in ipairs(targetIds) do
        -- Filter handlers for this target
        for _, entry in ipairs(allHandlers) do
            if entry.targetId == nil or entry.targetId == targetId then
                local d = data and {target = targetId} or {target = targetId}
                if data then
                    for k, v in pairs(data) do d[k] = v end
                end
                entry.handler(d)
            end
        end
    end
end

--- Clear all listeners
function EventBus:clear()
    self.listeners = {}
    self.sortedHandlers = {}
    self.dirty = {}
end

--- Clear listeners for a specific event
-- @param eventName string
function EventBus:clearEvent(eventName)
    self.listeners[eventName] = nil
    self.sortedHandlers[eventName] = nil
    self.dirty[eventName] = nil
end

--- Get listener count for an event
-- @param eventName string
-- @return number
function EventBus:count(eventName)
    if self.listeners[eventName] then
        return #self.listeners[eventName]
    end
    return 0
end

--- Create a child EventBus that inherits parent listeners
-- Useful for temporary event scopes
function EventBus:child()
    local child = EventBus:new()
    child.parent = self
    return child
end

return {
    EventBus = EventBus
}
