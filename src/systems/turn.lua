-- Turn System
-- Manages turn-based game state
-- Gameplay Rule Pipeline Layer

local TurnSystem = {
    priority = 0,
    name = "TurnSystem",
}

function TurnSystem:init(world)
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
end

function TurnSystem:update(world, dt)
    -- Turn logic primarily event-driven
end

-- Start player turn
function TurnSystem:startTurn()
    self.currentPhase = "player"
    self.turnInProgress = true
    self.inputAllowed = true

    if self.events then
        self.events:emit("PlayerTurnStart", {})
    end
end

-- End player turn
function TurnSystem:endPlayerTurn()
    if self.currentPhase ~= "player" then return end

    self.inputAllowed = false

    if self.events then
        self.events:emit("PlayerTurnEnd", {})
    end
end

-- Check if input is allowed
function TurnSystem:isInputAllowed()
    return self.inputAllowed
end

-- Get turn count
function TurnSystem:getTurnCount()
    return self.turnCount
end

-- Get current phase
function TurnSystem:getPhase()
    return self.currentPhase
end

return TurnSystem