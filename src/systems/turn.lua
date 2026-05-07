-- Turn System
-- Manages turn-based game state

local TurnSystem = {
    priority = 0,
    name = "TurnSystem",
    
    init = function(self, world)
        self.world = world
        self.events = world.eventBus
        self.currentPhase = "player"
        self.turnInProgress = false
        self.turnCount = 0
        self.inputAllowed = true
        
        if self.events then
            self.events:on("MoveAttempt", function(data)
                self.turnInProgress = true
                self.inputAllowed = false
            end)
            
            self.events:on("TurnEnd", function()
                self.turnInProgress = false
                self.inputAllowed = true
                self.turnCount = self.turnCount + 1
            end)
        end
    end,
    
    update = function(self, world, dt)
        -- Turn logic handled by events
    end,
    
    startTurn = function(self)
        self.turnInProgress = true
        self.inputAllowed = false
    end,
    
    isInputAllowed = function(self)
        return self.inputAllowed
    end,
    
    getTurnCount = function(self)
        return self.turnCount
    end
}

return TurnSystem
