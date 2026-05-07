-- System Base Class Implementation
-- Pure Lua, no LÖVE dependencies

local System = {}
System.__index = System

--- Create a subclass of System
-- @param className string: name of the subclass
-- @return new class
function System:extend(className)
    local subclass = {}
    setmetatable(subclass, self)
    subclass.__index = subclass
    subclass.className = className
    return subclass
end

--- Create a new System
-- @param opts table: optional configuration
-- @return System instance
function System:new(opts)
    opts = opts or {}
    local instance = setmetatable({}, self)
    instance.priority = opts.priority or 0
    instance.enabled = opts.enabled ~= false
    instance.world = nil
    instance.name = opts.name or self.className or "System"
    return instance
end

--- Initialize the system (called when added to world)
-- Override in subclass
-- @param world World: the world this system belongs to
function System:init(world)
    self.world = world
end

--- Called when system is added to world
-- Alias for init for compatibility
function System:onAddToWorld(world)
    self.world = world
end

--- Update the system (called each frame)
-- Must be overridden in subclass
-- @param dt number: delta time in seconds
-- @param world World: the world to operate on
function System:update(dt, world)
    -- Override in subclass
end

--- Shutdown the system (called when world is destroyed)
-- Override in subclass for cleanup
function System:shutdown()
    self.world = nil
end

--- Called when system is removed from world
function System:onRemoveFromWorld()
    self.world = nil
end

--- Enable the system
function System:enable()
    self.enabled = true
end

--- Disable the system
function System:disable()
    self.enabled = false
end

--- Check if system is enabled
-- @return boolean
function System:isEnabled()
    return self.enabled
end

--- Get system name
-- @return string
function System:getName()
    return self.name
end

--- Set system name
-- @param name string
function System:setName(name)
    self.name = name
end

return {
    System = System
}