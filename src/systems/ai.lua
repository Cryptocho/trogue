-- AI System
-- Turn-based AI for enemy movement and abilities
-- Gameplay Rule Pipeline Layer

local AISystem = {
    priority = 3,
    name = "AISystem",
}

-- 随机方向
local DIRECTIONS = {
    {dx = -1, dy = 0},
    {dx = 1, dy = 0},
    {dx = 0, dy = -1},
    {dx = 0, dy = 1},
}

function AISystem:init(world)
    self.world = world
    self.events = world.eventBus
    self.waitingForPlayerTurn = true
    self.ruleEngine = nil  -- 将在main.lua中设置
    
    if self.events then
        self.events:on("PlayerTurnEnd", function()
            self.waitingForPlayerTurn = true
        end)
    end
end

function AISystem:update(world, dt)
    -- Only act when waiting for player turn
    if not self.waitingForPlayerTurn then
        return
    end
    
    self.waitingForPlayerTurn = false
    
    local actors = world:query({"Actor", "Position"})
    
    for _, result in ipairs(actors) do
        local entity = result.id
        
        -- Skip player
        if world.components.Player[entity] then
            goto continue
        end
        
        -- 尝试使用技能（如果有RuleEngine和技能）
        if self.ruleEngine then
            self:tryUseAbility(entity)
        end
        
        -- 随机移动（70%几率）
        if math.random() < 0.7 then
            local dir = DIRECTIONS[math.random(#DIRECTIONS)]
            
            if self.events then
                self.events:emit("MoveAttempt", {
                    entity = entity,
                    dx = dir.dx,
                    dy = dir.dy,
                    isPlayer = false
                })
            end
        end
        
        ::continue::
    end
    
    -- Turn complete
    if self.events then
        self.events:emit("TurnEnd", {})
    end
end

-- 尝试使用技能
-- @param entityId number
function AISystem:tryUseAbility(entityId)
    if not self.ruleEngine or not self.events then
        return
    end
    
    -- 敌人技能列表（可以从组件读取，这里用简单的随机选择）
    local enemyAbilities = {"punch"}
    
    -- 随机选择一个技能尝试使用
    local abilityId = enemyAbilities[math.random(#enemyAbilities)]
    
    -- 检查技能是否可用
    local canUse, reason = self.ruleEngine:canUse(entityId, abilityId)
    
    if canUse then
        -- 查找范围内的玩家作为目标
        local pos = self.world.components.Position[entityId]
        if pos then
            local players = self.world:query({"Player", "Position"})
            for _, playerResult in ipairs(players) do
                local playerPos = playerResult.components.Position
                local dist = math.abs(playerPos.x - pos.x) + math.abs(playerPos.y - pos.y)
                
                -- 如果玩家在技能范围内
                if dist <= 1 then
                    self.events:emit("AbilityUse", {
                        entity = entityId,
                        abilityId = abilityId,
                        targetId = playerResult.id,
                    })
                    return
                end
            end
        end
    end
end

return AISystem