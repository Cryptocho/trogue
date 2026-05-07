-- TurnSystem
-- Manages turn-based game state

local System = require("src.ecs.system").System

local TurnSystem = System:extend("TurnSystem")

function TurnSystem:new(opts)
    local instance = System.new(self, opts)
    instance.name = "TurnSystem"
    instance.turnInProgress = false
    instance.turnCount = 0
    return instance
end

function TurnSystem:init(world)
    self.world = world
    self.events = world.eventBus
    
    if self.events then
        -- Listen for player turn end
        self.events:on("PlayerTurnEnd", function(data)
            -- Player action complete, wait for AI
        end, 100)
        
        -- Listen for AI turn end (final turn end)
        self.events:on("TurnEnd", function(data)
            self.turnInProgress = false
            self.turnCount = self.turnCount + 1
        end, 1000)  -- Lowest priority = called last
    end
end

function TurnSystem:update(dt, world)
    -- TurnSystem doesn't process each frame, it manages state
end

-- Check if input should be accepted (turn not in progress)
function TurnSystem:isInputAllowed()
    return not self.turnInProgress
end

-- Start a new turn
function TurnSystem:startTurn()
    self.turnInProgress = true
end

-- End the turn manually (e.g., if no enemies exist)
function TurnSystem:endTurn()
    self.turnInProgress = false
    self.turnCount = self.turnCount + 1
    if self.events then
        self.events:emit("TurnEnd", {turnCount = self.turnCount})
    end
end

-- Get current turn count
function TurnSystem:getTurnCount()
    return self.turnCount
end

return TurnSystem
