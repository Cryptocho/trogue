-- ECS Core Implementation

-- Spatial Hash for O(1) spatial queries
local function createSpatialHash(cellSize)
    local instance = {
        cellSize = cellSize or 1,
        cells = {},  -- "x,y" -> {entityId -> entityId}
    }

    instance.insert = function(self, entityId, x, y)
        local key = math.floor(x / self.cellSize) .. "," .. math.floor(y / self.cellSize)
        if not self.cells[key] then
            self.cells[key] = {}
        end
        self.cells[key][entityId] = entityId
    end

    instance.remove = function(self, entityId, x, y)
        local key = math.floor(x / self.cellSize) .. "," .. math.floor(y / self.cellSize)
        if self.cells[key] then
            self.cells[key][entityId] = nil
        end
    end

    instance.move = function(self, entityId, oldX, oldY, newX, newY)
        self:remove(entityId, oldX, oldY)
        self:insert(entityId, newX, newY)
    end

    instance.getAt = function(self, x, y, componentFilter, getComponentStorage)
        local key = math.floor(x / self.cellSize) .. "," .. math.floor(y / self.cellSize)
        local cell = self.cells[key]
        if not cell then
            return nil
        end

        local results = {}
        for entityId, _ in pairs(cell) do
            if not componentFilter or (getComponentStorage and getComponentStorage(entityId, componentFilter)) then
                table.insert(results, entityId)
            end
        end

        if #results == 0 then
            return nil
        else
            return results
        end
    end

    instance.getNeighbors = function(self, x, y, radius)
        local results = {}
        local minX = math.floor((x - radius) / self.cellSize)
        local maxX = math.floor((x + radius) / self.cellSize)
        local minY = math.floor((y - radius) / self.cellSize)
        local maxY = math.floor((y + radius) / self.cellSize)

        for gx = minX, maxX do
            for gy = minY, maxY do
                local key = gx .. "," .. gy
                local cell = self.cells[key]
                if cell then
                    for entityId, _ in pairs(cell) do
                        table.insert(results, entityId)
                    end
                end
            end
        end

        return results
    end

    instance.clear = function(self)
        self.cells = {}
    end

    return instance
end

-- Built-in component for marking entity destruction
local function createShouldDespawn(reason)
    return {
        reason = reason,
    }
end

-- Create a new World instance
local function createWorld()
    local instance = {
        nextEntityId = 1,
        entities = {},
        components = {},
        systems = {},
        componentInstances = {},
        eventBus = nil,
        spatialHash = createSpatialHash(1),
    }

    -- Create a new entity with the given components
    -- @param components table: {ComponentName = {...}, ...} or Component instances
    -- @return number: entity id
    instance.spawn = function(self, components)
        local id = self.nextEntityId
        self.nextEntityId = self.nextEntityId + 1

        self.entities[id] = {}
        self.componentInstances[id] = {}

        for name, data in pairs(components) do
            self:addComponent(id, name, data)
        end

        return id
    end

    -- Add a component to an entity
    -- @param entityId number
    -- @param componentName string
    -- @param componentData table or Component instance
    instance.addComponent = function(self, entityId, componentName, componentData)
        local isInstance = componentData and componentData._isComponent

        if not self.components[componentName] then
            self.components[componentName] = {}
        end

        if isInstance then
            self.components[componentName][entityId] = componentData.data
            if not self.componentInstances[entityId] then
                self.componentInstances[entityId] = {}
            end
            self.componentInstances[entityId][componentName] = componentData
            componentData:_attach(entityId, self)
        else
            self.components[componentName][entityId] = componentData
        end

        if not self.entities[entityId] then
            self.entities[entityId] = {}
        end
        self.entities[entityId][componentName] = self.components[componentName][entityId]

        -- Update spatial hash if this is a Position component
        if componentName == "Position" and componentData then
            local pos = isInstance and componentData.data or componentData
            self.spatialHash:insert(entityId, pos.x or 0, pos.y or 0)
        end
    end

    -- Remove a component from an entity
    -- @param entityId number
    -- @param componentName string
    instance.removeComponent = function(self, entityId, componentName)
        -- Update spatial hash before removing Position
        if componentName == "Position" then
            local oldPos = self.components[componentName] and self.components[componentName][entityId]
            if oldPos then
                self.spatialHash:remove(entityId, oldPos.x or 0, oldPos.y or 0)
            end
        end

        if self.componentInstances[entityId] and self.componentInstances[entityId][componentName] then
            local comp = self.componentInstances[entityId][componentName]
            comp:_detach()
            self.componentInstances[entityId][componentName] = nil
        end

        if self.components[componentName] then
            self.components[componentName][entityId] = nil
        end
        if self.entities[entityId] then
            self.entities[entityId][componentName] = nil
        end
    end

    -- Mark entity for destruction (add ShouldDespawn component)
    -- @param entityId number
    -- @param reason string: optional reason
    instance.despawn = function(self, entityId, reason)
        self:addComponent(entityId, "ShouldDespawn", createShouldDespawn(reason))
    end

    -- Actually destroy entities marked with ShouldDespawn
    -- Call this in a CleanupSystem or at end of frame
    -- @return array: {entityId, reason} of destroyed entities
    instance.processDespawns = function(self)
        local destroyed = {}
        local results = self:query({"ShouldDespawn"})

        for _, result in ipairs(results) do
            local id = result.id
            local reason = nil

            if result.components.ShouldDespawn then
                reason = result.components.ShouldDespawn.reason
            end

            self:_destroyEntity(id)
            table.insert(destroyed, {id = id, reason = reason})
        end

        return destroyed
    end

    -- Internal: Actually destroy an entity
    instance._destroyEntity = function(self, entityId)
        -- Remove from spatial hash first
        local pos = self.components.Position and self.components.Position[entityId]
        if pos then
            self.spatialHash:remove(entityId, pos.x or 0, pos.y or 0)
        end

        -- Remove all components
        if self.entities[entityId] then
            local componentNames = {}
            for componentName, _ in pairs(self.entities[entityId]) do
                table.insert(componentNames, componentName)
            end
            for _, componentName in ipairs(componentNames) do
                self:removeComponent(entityId, componentName)
            end
        end

        if self.componentInstances[entityId] then
            self.componentInstances[entityId] = nil
        end

        self.entities[entityId] = nil
    end

    -- Get a component by entity and name (encapsulated access)
    -- @param entityId number
    -- @param componentName string
    -- @return component data or nil
    instance.getComponent = function(self, entityId, componentName)
        if self.components[componentName] then
            return self.components[componentName][entityId]
        end
        return nil
    end

    -- Set a component data for an entity
    -- @param entityId number
    -- @param componentName string
    -- @param data new component data
    instance.setComponent = function(self, entityId, componentName, data)
        if not self.components[componentName] then
            self.components[componentName] = {}
        end

        -- Handle Position updates in spatial hash
        if componentName == "Position" then
            local oldPos = self.components[componentName] and self.components[componentName][entityId]
            if oldPos then
                self.spatialHash:remove(entityId, oldPos.x or 0, oldPos.y or 0)
            end
            if data then
                self.spatialHash:insert(entityId, data.x or 0, data.y or 0)
            end
        end

        self.components[componentName][entityId] = data

        if self.entities[entityId] then
            self.entities[entityId][componentName] = data
        end
    end

    -- Check if entity has a component
    -- @param entityId number
    -- @param componentName string
    -- @return boolean
    instance.hasComponent = function(self, entityId, componentName)
        return self.components[componentName] and self.components[componentName][entityId] ~= nil
    end

    -- Query entities that have all specified components
    -- @param componentNames array: {"Component1", "Component2", ...}
    -- @param options table: optional {readOnly = true} for read-only results
    -- @return array: {{id = number, components = table}, ...}
    instance.query = function(self, componentNames, options)
        options = options or {}
        local readOnly = options.readOnly ~= false  -- Default to true

        if #componentNames == 0 then
            local results = {}
            for id, _ in pairs(self.entities) do
                table.insert(results, {id = id, components = self.entities[id]})
            end
            return results
        end

        local smallestSet = nil
        local smallestSize = math.huge

        for _, componentName in ipairs(componentNames) do
            local componentSet = self.components[componentName]
            if componentSet then
                local size = 0
                for _, _ in pairs(componentSet) do
                    size = size + 1
                end
                if size < smallestSize then
                    smallestSize = size
                    smallestSet = componentSet
                end
            else
                return {}
            end
        end

        local results = {}

        for entityId, _ in pairs(smallestSet) do
            local hasAll = true

            if not self.entities[entityId] then
                hasAll = false
            else
                for _, componentName in ipairs(componentNames) do
                    if not self.components[componentName] or not self.components[componentName][entityId] then
                        hasAll = false
                        break
                    end
                end
            end

            if hasAll then
                local components = self.entities[entityId]

                if readOnly then
                    -- Create read-only proxy using metatable
                    components = setmetatable({}, {
                        __index = self.entities[entityId],
                        __newindex = function()
                            error("Cannot modify query result directly. Use World:setComponent() instead.")
                        end,
                        __metatable = false  -- Prevent getmetatable
                    })
                end

                table.insert(results, {
                    id = entityId,
                    components = components
                })
            end
        end

        return results
    end

    -- Add a system to the world
    -- @param system table
    instance.addSystem = function(self, system)
        if system.priority == nil then
            system.priority = 0
        end

        local inserted = false
        for i, existing in ipairs(self.systems) do
            if system.priority < existing.priority then
                table.insert(self.systems, i, system)
                inserted = true
                break
            end
        end

        if not inserted then
            table.insert(self.systems, system)
        end

        if system.init then
            system:init(self)
        end
    end

    -- Update all systems
    -- @param dt number: delta time in seconds
    instance.update = function(self, dt)
        for _, system in ipairs(self.systems) do
            if system.enabled ~= false and system.update then
                system:update(self, dt)
            end
        end
    end

    -- Get all entities (for internal use)
    instance.getEntities = function(self)
        return self.entities
    end

    -- Get component storage for a component type (internal use)
    instance.getComponentStorage = function(self, componentName)
        return self.components[componentName]
    end

    -- Get component instance (if it's a Component instance)
    -- @param entityId number
    -- @param componentName string
    -- @return Component instance or nil
    instance.getComponentInstance = function(self, entityId, componentName)
        if self.componentInstances[entityId] then
            return self.componentInstances[entityId][componentName]
        end
        return nil
    end

    -- Get spatial hash for O(1) spatial queries
    instance.getSpatialHash = function(self)
        return self.spatialHash
    end

    return instance
end

return {
    createWorld = createWorld,
    createSpatialHash = createSpatialHash,
    createShouldDespawn = createShouldDespawn,
}