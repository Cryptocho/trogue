-- EventBus Implementation

local function createEventBus()
    local instance = {
        listeners = {},
        sortedHandlers = {},
        dirty = {},
    }

    instance.on = function(self, eventName, handler, priority, targetId)
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

        self.dirty[eventName] = true

        local bus = self
        local event = eventName
        local handlerRef = handler
        return function()
            for i, entry in ipairs(bus.listeners[event]) do
                if entry.handler == handlerRef then
                    bus:off(event, i)
                    break
                end
            end
        end
    end

    instance.off = function(self, eventName, index)
        if self.listeners[eventName] then
            table.remove(self.listeners[eventName], index)
            self.dirty[eventName] = true
        end
    end

    instance._rebuild = function(self, eventName)
        local raw = self.listeners[eventName]
        if not raw then
            self.sortedHandlers[eventName] = {}
            return
        end

        local sorted = {}
        for i, entry in ipairs(raw) do
            sorted[i] = entry
        end

        table.sort(sorted, function(a, b) return a.priority < b.priority end)

        self.sortedHandlers[eventName] = sorted
        self.dirty[eventName] = false
    end

    instance._getHandlers = function(self, eventName, targetId)
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

        local filtered = {}
        for _, entry in ipairs(all) do
            if entry.targetId == nil or entry.targetId == targetId then
                table.insert(filtered, entry)
            end
        end
        return filtered
    end

    instance.emit = function(self, eventName, data)
        local allHandlers = {}
        local bus = self

        while bus do
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

        for _, entry in ipairs(allHandlers) do
            entry.handler(data)
        end
    end

    instance.emitTo = function(self, targetId, eventName, data)
        local handlers = self:_getHandlers(eventName, targetId)
        if #handlers == 0 then
            return
        end

        if data and data.target == nil then
            data.target = targetId
        end

        for _, entry in ipairs(handlers) do
            entry.handler(data)
        end
    end

    instance.emitToMany = function(self, targetIds, eventName, data)
        if self.dirty[eventName] then
            self:_rebuild(eventName)
        end

        local allHandlers = self.sortedHandlers[eventName] or {}

        for _, targetId in ipairs(targetIds) do
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

    instance.clear = function(self)
        self.listeners = {}
        self.sortedHandlers = {}
        self.dirty = {}
    end

    instance.clearEvent = function(self, eventName)
        self.listeners[eventName] = nil
        self.sortedHandlers[eventName] = nil
        self.dirty[eventName] = nil
    end

    instance.count = function(self, eventName)
        if self.listeners[eventName] then
            return #self.listeners[eventName]
        end
        return 0
    end

    instance.child = function(self)
        local child = createEventBus()
        child.parent = self
        return child
    end

    return instance
end

return {
    createEventBus = createEventBus,
}