-- Input System
-- Handles keyboard input for turn-based movement

local System = require("src.ecs.system").System

local InputSystem = System:extend("InputSystem")

-- Key mappings for movement
local KEY_MOVEMENTS = {
    -- Arrow keys
    left = {dx = -1, dy = 0},
    right = {dx = 1, dy = 0},
    up = {dx = 0, dy = -1},
    down = {dx = 0, dy = 1},
    -- WASD
    a = {dx = -1, dy = 0},
    d = {dx = 1, dy = 0},
    w = {dx = 0, dy = -1},
    s = {dx = 0, dy = 1},
}

function InputSystem:new(opts)
    local instance = System.new(self, opts)
    instance.name = "InputSystem"
    return instance
end

function InputSystem:init(world)
    self.world = world
    self.events = world.eventBus
end

function InputSystem:update(dt, world)
    -- Input is processed in love.keypressed callback
    -- This system just waits for key events
end

return InputSystem
