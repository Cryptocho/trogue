-- Turn System
-- Manages turn-based game state
-- Gameplay Rule Pipeline Layer

local TurnSystem = {
    priority = 0,
    name = "TurnSystem",
    
    init = function(self, world)
        self.world = world
        self.events = world.eventBus
        self.currentPhase = "player"  -- player / enemy / processing
        self.turnInProgress = false
        self.turnCount = 1
        self.inputAllowed = true
        
        if self.events then
            -- Listen for move attempt to start turn
            self.events:on("MoveAttempt", function(data)
                if data.isPlayer then
                    self.turnInProgress = true
                    self.inputAllowed = false
                end
            end, 0)
            
            -- Listen for player turn end
            self.events:on("PlayerTurnEnd", function()
                self.inputAllowed = false
            end, 0)
            
            -- Listen for turn end
            self.events:on("TurnEnd", function(data)
                self.turnInProgress = false
                self.inputAllowed = true
                self.currentPhase = "player"
                self.turnCount = self.turnCount + 1
            end, 100)
        end
    end,
    
    update = function(self, world, dt)
        -- Turn logic primarily event-driven
    end,
    
    -- Start player turn
    startTurn = function(self)
        self.currentPhase = "player"
        self.turnInProgress = true
        self.inputAllowed = true
        
        if self.events then
            self.events:emit("PlayerTurnStart", {})
        end
    end,
    
    -- End player turn
    endPlayerTurn = function(self)
        if self.currentPhase ~= "player" then return end
        
        self.inputAllowed = false
        
        if self.events then
            self.events:emit("PlayerTurnEnd", {})
        end
    end,
    
    -- Check if input is allowed
    isInputAllowed = function(self)
        return self.inputAllowed
    end,
    
    -- Get turn count
    getTurnCount = function(self)
        return self.turnCount
    end,
    
    -- Get current phase
    getPhase = function(self)
        return self.currentPhase
    end,
}

return TurnSystem