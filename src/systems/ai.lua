-- AI System
-- Turn-based AI for enemy movement

local System = require("src.ecs.system").System

local AISystem = System:extend("AISystem")

-- Random directions
local DIRECTIONS = {
    {dx = -1, dy = 0},
    {dx = 1, dy = 0},
    {dx = 0, dy = -1},
    {dx = 0, dy = 1},
}

function AISystem:new(opts)
    local instance = System.new(self, opts)
    instance.name = "AISystem"
    return instance
end

function AISystem:init(world)
    self.world = world
    self.events = world.eventBus
    
    if self.events then
        -- Listen for PlayerTurnEnd to trigger enemy actions
        self.events:on("PlayerTurnEnd", function(data)
            self:onPlayerTurnEnd()
        end, 200)  -- Priority after CombatSystem
    end
end

function AISystem:update(dt, world)
    -- AI acts based on PlayerTurnEnd event, not every frame
end

-- Called after player turn ends
function AISystem:onPlayerTurnEnd()
    local actors = self.world:query({"Actor", "Position"})
    local moved = 0
    
    for _, result in ipairs(actors) do
        local entity = result.id
        
        -- Skip player (has Player component, not just Actor)
        if self.world:hasComponent(entity, "Player") then
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
    
    if moved > 0 then
        print("AI: " .. moved .. " enemies moved")
    end
    
    -- All enemies have acted, turn ends
    if self.events then
        self.events:emit("TurnEnd", {enemiesMoved = moved})
    end
end

return AISystem
