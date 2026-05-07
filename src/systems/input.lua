-- Input System
-- Handles keyboard input

local InputSystem = {
    priority = 0,
    name = "InputSystem",
    
    KEY_MOVEMENTS = {
        left = {dx = -1, dy = 0},
        right = {dx = 1, dy = 0},
        up = {dx = 0, dy = -1},
        down = {dx = 0, dy = 1},
        a = {dx = -1, dy = 0},
        d = {dx = 1, dy = 0},
        w = {dx = 0, dy = -1},
        s = {dx = 0, dy = 1},
    },
    
    init = function(self, world)
        self.world = world
        self.events = world.eventBus
        self.enabled = true
    end,
    
    update = function(self, world, dt)
        -- Input handled in love.keypressed callback
    end,
    
    setEnabled = function(self, enabled)
        self.enabled = enabled
    end,
    
    isEnabled = function(self)
        return self.enabled
    end,
    
    handleKey = function(self, key)
        if not self.enabled then
            return
        end
        
        local movement = self.KEY_MOVEMENTS[key]
        if not movement then
            return
        end
        
        local players = self.world:query({"Player", "Position"})
        if #players == 0 then
            return
        end
        
        local playerId = players[1].id
        
        if self.events then
            self.events:emit("MoveAttempt", {
                entity = playerId,
                dx = movement.dx,
                dy = movement.dy,
                isPlayer = true
            })
        end
    end
}

return InputSystem
