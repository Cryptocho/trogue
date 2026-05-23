-- PrototypeManager Implementation

local function createPrototypeManager(world)
    local instance = {
        prototypes = {},
        world = world,
    }

    instance.setWorld = function(self, world) self.world = world end
    instance.load = function(self, moduleName) return _prototypeManagerLoad(self, moduleName) end
    instance.register = function(self, name, components) self.prototypes[name] = components end
    instance.get = function(self, name) return self.prototypes[name] end
    instance.has = function(self, name) return self.prototypes[name] ~= nil end
    instance.spawn = function(self, name, overrides) return _prototypeManagerSpawn(self, name, overrides) end
    instance.getNames = function(self)
        local names = {}
        for name, _ in pairs(self.prototypes) do
            table.insert(names, name)
        end
        return names
    end
    instance.clear = function(self) self.prototypes = {} end

    return instance
end

function _prototypeManagerLoad(self, moduleName)
    local ok, result = pcall(require, moduleName)
    if not ok then
        error("Failed to load prototype module '" .. moduleName .. "': " .. tostring(result))
    end

    if type(result) ~= "table" then
        error("Prototype module '" .. moduleName .. "' must return a table")
    end

    for name, components in pairs(result) do
        self.prototypes[name] = components
    end

    return result
end

function _prototypeManagerSpawn(self, name, overrides)
    if not self.world then
        error("No world set in PrototypeManager")
    end

    local proto = self:get(name)
    if not proto then
        return nil
    end

    local components = {}
    for compName, compData in pairs(proto) do
        components[compName] = _deepCopy(compData)
    end

    if overrides then
        for compName, compData in pairs(overrides) do
            components[compName] = _deepCopy(compData)
        end
    end

    return self.world:spawn(components)
end

function _deepCopy(t, seen)
    seen = seen or {}

    if type(t) ~= "table" then
        return t
    end

    if seen[t] then
        return seen[t]
    end

    local meta = getmetatable(t)
    local copy = {}
    seen[t] = copy

    for k, v in pairs(t) do
        local newKey = k
        if type(k) == "table" then
            newKey = _deepCopy(k, seen)
        end

        local newValue = v
        if type(v) == "table" then
            newValue = _deepCopy(v, seen)
        end

        copy[newKey] = newValue
    end

    if meta then
        local newMeta = _deepCopy(meta, seen)
        setmetatable(copy, newMeta)
    end

    return copy
end

return {
    createPrototypeManager = createPrototypeManager,
}