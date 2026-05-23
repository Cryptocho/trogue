-- ECS Core Implementation

local function createSpatialHash(cellSize)
    local instance = {
        cellSize = cellSize or 1,
        cells = {},
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

local function createShouldDespawn(reason)
    return {
        reason = reason,
    }
end

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

        if componentName == "Position" and componentData then
            local pos = isInstance and componentData.data or componentData
            self.spatialHash:insert(entityId, pos.x or 0, pos.y or 0)
        end
    end

    instance.removeComponent = function(self, entityId, componentName)
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

    instance.despawn = function(self, entityId, reason)
        self:addComponent(entityId, "ShouldDespawn", createShouldDespawn(reason))
    end

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

    instance._destroyEntity = function(self, entityId)
        local pos = self.components.Position and self.components.Position[entityId]
        if pos then
            self.spatialHash:remove(entityId, pos.x or 0, pos.y or 0)
        end

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

    instance.getComponent = function(self, entityId, componentName)
        if self.components[componentName] then
            return self.components[componentName][entityId]
        end
        return nil
    end

    instance.setComponent = function(self, entityId, componentName, data)
        if not self.components[componentName] then
            self.components[componentName] = {}
        end

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

    instance.hasComponent = function(self, entityId, componentName)
        return self.components[componentName] and self.components[componentName][entityId] ~= nil
    end

    instance.query = function(self, componentNames, options)
        options = options or {}
        local readOnly = options.readOnly ~= false

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
                    components = setmetatable({}, {
                        __index = self.entities[entityId],
                        __newindex = function()
                            error("Cannot modify query result directly. Use World:setComponent() instead.")
                        end,
                        __metatable = false
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

    instance.update = function(self, dt)
        for _, system in ipairs(self.systems) do
            if system.enabled ~= false and system.update then
                system:update(self, dt)
            end
        end
    end

    instance.getEntities = function(self)
        return self.entities
    end

    instance.getComponentStorage = function(self, componentName)
        return self.components[componentName]
    end

    instance.getComponentInstance = function(self, entityId, componentName)
        if self.componentInstances[entityId] then
            return self.componentInstances[entityId][componentName]
        end
        return nil
    end

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