-- Input System
-- 统一处理所有玩家输入（移动、技能）
-- 架构：main.lua 的 love.keypressed 委托给此系统

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
    
    -- 启用系统
    self.enabled = true
end

function InputSystem:update(world, dt)
    -- 输入由 love.keypressed 回调处理，此处仅做状态检查
end

-- 设置系统引用（由 main.lua 在 initGameWorld 后调用）
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

-- 主入口：从 main.lua love.keypressed 调用
-- @param key string
-- @param scancode string
-- @param isrepeat boolean
function InputSystem:handleKey(key, scancode, isrepeat)
    if not self.enabled then
        return
    end
    
    -- 检查回合系统是否允许输入
    if self.turnSystem and not self.turnSystem:isInputAllowed() then
        return
    end
    
    -- 处理移动
    local movement = self.KEY_MOVEMENTS[key]
    if movement then
        self:handleMove(movement)
        return
    end
    
    -- 处理技能
    local abilityId = self.KEY_ABILITIES[key]
    if abilityId then
        self:handleAbility(abilityId)
        return
    end
end

-- 处理移动输入
-- @param movement table: {dx, dy}
function InputSystem:handleMove(movement)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end
    
    local playerId = players[1].id
    
    -- 通知回合系统开始回合
    if self.turnSystem then
        self.turnSystem:startTurn()
    end
    
    -- 发出移动尝试事件
    if self.events then
        self.events:emit("MoveAttempt", {
            entity = playerId,
            dx = movement.dx,
            dy = movement.dy,
            isPlayer = true
        })
    end
end

-- 处理技能使用
-- @param abilityId string
function InputSystem:handleAbility(abilityId)
    local players = self.world:query({"Player", "Position"})
    if #players == 0 then
        return
    end
    
    local playerId = players[1].id
    
    -- 检查技能是否可用
    if self.ruleEngine then
        local canUse, reason = self.ruleEngine:canUse(playerId, abilityId)
        if not canUse then
            print("Cannot use: " .. reason)
            return
        end
    end
    
    -- 通知回合系统开始回合
    if self.turnSystem then
        self.turnSystem:startTurn()
    end
    
    -- 发出技能使用事件（自动选择目标）
    if self.events then
        self.events:emit("AbilityUse", {
            entity = playerId,
            abilityId = abilityId,
            targetId = nil  -- RuleEngine 将自动选择目标
        })
    end
end

return InputSystem