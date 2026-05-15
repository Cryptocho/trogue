-- Input System
-- Unified handling of all player input (movement, abilities)
-- Architecture: main.lua love.keypressed delegates to this system

local InputSystem = {
    priority = 0,
    name = "InputSystem",
    
    -- Movement key mappings
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
    
    -- Ability hotkey mappings
    KEY_ABILITIES = {
        ["1"] = "punch",
        ["2"] = "heal",
        ["3"] = "shield",
        ["4"] = "fireball",
    },
    
    enabled = true,
}

function InputSystem:init(world, config)
    self.world = world
    self.events = world.eventBus
    self.turnSystem = nil
    self.ruleEngine = nil
    
    -- Enable system
    self.enabled = true
    
    -- Listen for AbilityUsed to end turn after successful ability use
    if self.events then
        self.events:on("AbilityUsed", function(data)
            if data and data.entity then
                -- Check if it's the player who used the ability
                local players = self.world:query({"Player"})
                for _, player in ipairs(players) do
                    if player.id == data.entity then
                        self.events:emit("PlayerTurnEnd", {})
                        break
                    end
                end
            end
        end)
    end
end

function InputSystem:update(world, dt)
    -- Input handled by love.keypressed callback, only status checks here
end

-- Set system references (called by main.lua after initGameWorld)
function InputSystem:setTurnSystem(turnSystem)
    self.turnSystem = turnSystem
end

function InputSystem:setRuleEngine(ruleEngine)
    self.ruleEngine = ruleEngine
end

function InputSystem:setEnabled(enabled)
    self.enabled = enabled
end

function InputSystem:isEnabled()
    return self.enabled
end

-- Main entry point: called from main.lua love.keypressed
-- @param key string
-- @param scancode string
-- @param isrepeat boolean
function InputSystem:handleKey(key, scancode, isrepeat)
    if not self.enabled then
        return
    end
    
    -- Check if turn system allows input
    if self.turnSystem and not self.turnSystem:isInputAllowed() then
        return
    end
    
    -- Handle movement
    local movement = self.KEY_MOVEMENTS[key]
    if movement then
        self:handleMove(movement)
        return
    end
    
    -- Handle abilities
    local abilityId = self.KEY_ABILITIES[key]
    if abilityId then
        self:handleAbility(abilityId)
        return
    end
end

-- Handle movement input
-- @param movement table: {dx, dy}
function InputSystem:handleMove(movement)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end
    
    local playerId = players[1].id
    
    -- Notify turn system to start turn
    if self.turnSystem then
        self.turnSystem:startTurn()
    end
    
    -- Emit move attempt event
    if self.events then
        self.events:emit("MoveAttempt", {
            entity = playerId,
            dx = movement.dx,
            dy = movement.dy,
            isPlayer = true
        })
    end
end

-- Handle ability usage
-- @param abilityId string
function InputSystem:handleAbility(abilityId)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end
    
    local playerId = players[1].id
    
    -- Check if ability is usable
    if self.ruleEngine then
        local canUse, reason = self.ruleEngine:canUse(playerId, abilityId)
        if not canUse then
            print("Cannot use: " .. reason)
            return
        end
    end
    
    -- Notify turn system to start turn
    if self.turnSystem then
        self.turnSystem:startTurn()
    end
    
    -- Emit ability use event (auto-select target)
    -- PlayerTurnEnd will be emitted in AbilityUsed event handler
    if self.events then
        self.events:emit("AbilityUse", {
            entity = playerId,
            abilityId = abilityId,
            targetId = nil  -- RuleEngine will auto-select target
        })
    end
end

return InputSystem