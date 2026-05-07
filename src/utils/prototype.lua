-- PrototypeManager Implementation

local PrototypeManager = {}
PrototypeManager.__index = PrototypeManager

--- Create a new PrototypeManager
-- @param world World: optional world to register with
-- @return PrototypeManager
function PrototypeManager:new(world)
    local instance = {
        prototypes = {},  -- name -> component table
        world = world,
    }
    setmetatable(instance, PrototypeManager)
    return instance
end

--- Set the world reference
-- @param world World
function PrototypeManager:setWorld(world)
    self.world = world
end

--- Load prototypes from a Lua module
-- @param moduleName string: require path, e.g. "data.prototypes.entities"
-- @return table: loaded prototypes
function PrototypeManager:load(moduleName)
    local ok, result = pcall(require, moduleName)
    if not ok then
        error("Failed to load prototype module '" .. moduleName .. "': " .. tostring(result))
    end
    
    if type(result) ~= "table" then
        error("Prototype module '" .. moduleName .. "' must return a table")
    end
    
    -- Merge into prototypes
    for name, components in pairs(result) do
        self.prototypes[name] = components
    end
    
    return result
end

--- Register a prototype directly
-- @param name string: prototype name
-- @param components table: component data
function PrototypeManager:register(name, components)
    self.prototypes[name] = components
end

--- Get a prototype by name
-- @param name string
-- @return table or nil
function PrototypeManager:get(name)
    return self.prototypes[name]
end

--- Check if a prototype exists
-- @param name string
-- @return boolean
function PrototypeManager:has(name)
    return self.prototypes[name] ~= nil
end

--- Spawn an entity from a prototype
-- @param name string: prototype name
-- @param overrides table: optional component overrides
-- @return number: entity id or nil if prototype not found
function PrototypeManager:spawn(name, overrides)
    if not self.world then
        error("No world set in PrototypeManager")
    end
    
    local proto = self:get(name)
    if not proto then
        return nil
    end
    
    -- Merge components with overrides
    local components = {}
    for compName, compData in pairs(proto) do
        -- Deep copy the component data
        components[compName] = self:_deepCopy(compData)
    end
    
    -- Apply overrides
    if overrides then
        for compName, compData in pairs(overrides) do
            components[compName] = self:_deepCopy(compData)
        end
    end
    
    return self.world:spawn(components)
end

--- Deep copy a table with cycle detection and metatable support
-- @param t table
-- @param seen table: internal use for cycle detection
-- @return table
function PrototypeManager:_deepCopy(t, seen)
    seen = seen or {}
    
    if type(t) ~= "table" then
        return t
    end
    
    -- Cycle detection
    if seen[t] then
        return seen[t]
    end
    
    -- Create copy with metatable
    local meta = getmetatable(t)
    local copy = {}
    seen[t] = copy
    
    for k, v in pairs(t) do
        -- Copy key (simple keys only)
        local newKey = k
        if type(k) == "table" then
            newKey = self:_deepCopy(k, seen)
        end
        
        -- Copy value
        local newValue = v
        if type(v) == "table" then
            newValue = self:_deepCopy(v, seen)
        end
        
        copy[newKey] = newValue
    end
    
    -- Preserve metatable if it exists
    if meta then
        local newMeta = self:_deepCopy(meta, seen)
        setmetatable(copy, newMeta)
    end
    
    return copy
end

--- Get all prototype names
-- @return array of string
function PrototypeManager:getNames()
    local names = {}
    for name, _ in pairs(self.prototypes) do
        table.insert(names, name)
    end
    return names
end

--- Clear all prototypes
function PrototypeManager:clear()
    self.prototypes = {}
end

--- Create a prototype loader function
-- Simple alternative to the class-based approach
-- @param moduleName string
-- @return function: prototype getter
function PrototypeManager.loadModule(moduleName)
    local prototypes = require(moduleName)
    return function(name)
        return prototypes[name]
    end
end

return {
    PrototypeManager = PrototypeManager
}
