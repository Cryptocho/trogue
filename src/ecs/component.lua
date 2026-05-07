-- Component Base Class Implementation
-- Pure Lua, no LÖVE dependencies

local Component = {}
Component.__index = Component
Component._isComponent = true  -- Marker for instanceof check

--- Create a subclass of Component
-- @param className string: name of the subclass
-- @return new class
function Component:extend(className)
    local subclass = {}
    setmetatable(subclass, self)
    subclass.__index = subclass
    subclass.className = className
    return subclass
end

--- Create a component with data
-- @param name string: component name
-- @param data table: component data
-- @return Component instance
function Component:new(name, data)
    local instance = setmetatable({}, self)
    instance.name = name or self.className or "Unknown"
    instance.data = data or {}
    instance.entityId = nil
    instance.world = nil
    return instance
end

--- Create a component with data (factory method)
-- @param name string: component name
-- @param data table: component data
-- @return Component instance
function Component.create(name, data)
    return Component:new(name, data)
end

-- Lifecycle stages (for reference, not enforced)
Component.LifeStage = {
    ADDED = "Added",
    INITIALIZED = "Initialized",
    RUNNING = "Running",
}

-- Internal methods

function Component:_attach(entityId, world)
    self.entityId = entityId
    self.world = world
end

function Component:_detach()
    self.entityId = nil
    self.world = nil
end

return {
    Component = Component
}