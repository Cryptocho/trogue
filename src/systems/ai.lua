-- AI System
-- Turn-based AI for enemy movement

local AISystem = {
    priority = 3,
    name = "AISystem",
    
    init = function(self, world)
        self.world = world
        self.events = world.eventBus
        self.waitingForPlayerTurn = true
        
        if self.events then
            self.events:on("PlayerTurnEnd", function()
                self.waitingForPlayerTurn = true
            end)
        end
    end,
    
    update = function(self, world, dt)
        -- Only act when waiting for player turn
        if not self.waitingForPlayerTurn then
            return
        end
        
        self.waitingForPlayerTurn = false
        
        local actors = world:query({"Actor", "Position"})
        local moved = 0
        
        for _, result in ipairs(actors) do
            local entity = result.id
            
            -- Skip player
            if world.components.Player[entity] then
                goto continue
            end
            
            -- Random movement (70% chance)
            if math.random() < 0.7 then
                local dir = DIRECTIONS[math.random(#DIRECTIONS)]
                
                if self.events then
                    self.events:emit("MoveAttempt", {
                        entity = entity,
                        dx = dir.dx,
                        dy = dir.dy,
                        isPlayer = false
                    })
                    moved = moved + 1
                end
            end
            
            ::continue::
        end
        
        -- Turn complete
        if self.events then
            self.events:emit("TurnEnd", {})
        end
    end
}

-- Random directions
DIRECTIONS = {
    {dx = -1, dy = 0},
    {dx = 1, dy = 0},
    {dx = 0, dy = -1},
    {dx = 0, dy = 1},
}

return AISystem
