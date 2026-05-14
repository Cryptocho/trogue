-- RuleEngine: 规则判定与效果应用
-- Gameplay Rule Pipeline Layer 核心
-- MVP: 效果处理逻辑暂时保留在 RuleEngine 内部，通过事件驱动
-- Phase 1: 已迁移使用 ECS 组件 Ability 和 Buffs

local AbilityDef = require("src.data.definitions.ability")
local EffectDef = require("src.data.definitions.effect")
local BuffDef = require("src.data.definitions.buff")
local AbilityComponent = require("src.components.ability")
local BuffsComponent = require("src.components.buffs")

local RuleEngine = {}
RuleEngine.__index = RuleEngine

function RuleEngine:new(world, eventBus)
    local instance = {
        world = world,
        events = eventBus,
        
        -- Registry: 技能/效果/buff 定义
        abilities = {},   -- abilityId -> AbilityDefinition
        effects = {},     -- effectId -> EffectDefinition
        buffs = {},       -- buffId -> BuffDefinition
    }
    setmetatable(instance, RuleEngine)
    
    -- Load built-in definitions
    for id, ability in pairs(AbilityDef.builtin) do
        instance.abilities[id] = ability
    end
    for id, effect in pairs(EffectDef.builtin) do
        instance.effects[id] = effect
    end
    for id, buff in pairs(BuffDef.builtin) do
        instance.buffs[id] = buff
    end
    
    -- Register event handlers
    instance:registerEvents()
    
    return instance
end

-- Register event handlers
function RuleEngine:registerEvents()
    if not self.events then return end
    
    -- Listen for ability use request
    self.events:on("AbilityUse", function(data)
        self:tryUseAbility(data.entity, data.abilityId, data.targetId)
    end, 0)
    
    -- Listen for damage request (processed internally)
    self.events:on("DamageRequest", function(data)
        self:_processDamage(data)
    end, 100)
    
    -- Listen for heal request
    self.events:on("HealRequest", function(data)
        self:_processHeal(data)
    end, 100)
    
    -- Listen for buff apply request
    self.events:on("BuffApplyRequest", function(data)
        self:_processBuffApply(data)
    end, 100)
    
    -- Listen for buff tick request
    self.events:on("BuffTickRequest", function(data)
        self:_processBuffTick(data)
    end, 100)
    
    -- Listen for turn end
    self.events:on("TurnEnd", function()
        self:onTurnEnd()
    end, 100)
end

-- Get entity ability component from ECS (懒创建)
-- 注意: Ability 组件应在原型中预定义，此处仅处理未预定义的情况
function RuleEngine:getAbilityComponent(entityId)
    -- 首先确保 Ability 组件存储存在
    if not self.world.components.Ability then
        self.world.components.Ability = {}
    end
    
    -- 如果不存在，懒创建 (适用于原型未定义 Ability 的情况)
    if not self.world.components.Ability[entityId] then
        self.world.components.Ability[entityId] = AbilityComponent:new()
    end
    
    return self.world.components.Ability[entityId]
end

-- Ensure entity has Buffs component
function RuleEngine:getOrCreateBuffsComponent(entityId)
    if not self.world.components.Buffs then
        self.world.components.Buffs = {}
    end
    
    if not self.world.components.Buffs[entityId] then
        self.world.components.Buffs[entityId] = BuffsComponent:new()
    end
    
    return self.world.components.Buffs[entityId]
end

-- Check if ability can be used
-- @param entityId number
-- @param abilityId string
-- @return boolean, string (canUse, reason)
function RuleEngine:canUse(entityId, abilityId)
    local ability = self.abilities[abilityId]
    if not ability then
        return false, "Ability not found: " .. abilityId
    end
    
    local comp = self:getAbilityComponent(entityId)
    
    -- Check if entity has this ability
    local hasAbility = false
    for _, id in ipairs(comp.abilities) do
        if id == abilityId then
            hasAbility = true
            break
        end
    end
    if not hasAbility then
        return false, "Ability not learned"
    end
    
    -- Check cooldown
    local cd = comp.cooldowns[abilityId] or 0
    if cd > 0 then
        return false, "On cooldown (" .. cd .. ")"
    end
    
    -- Check resource cost
    for resource, cost in pairs(ability.cost) do
        local current = comp.resources[resource] or 0
        if current < cost then
            return false, "Not enough " .. resource
        end
    end
    
    return true, "Can use"
end

-- Get valid targets for ability
-- @param entityId number
-- @param abilityId string
-- @return table, number ({entityId, ...}, targetCount)
function RuleEngine:getValidTargets(entityId, abilityId)
    local ability = self.abilities[abilityId]
    if not ability then return {}, 0 end
    
    local pos = self.world.components.Position[entityId]
    if not pos then return {}, 0 end
    
    local targets = {}
    
    if ability.targetType == AbilityDef.TargetType.SELF then
        table.insert(targets, entityId)
        
    elseif ability.targetType == AbilityDef.TargetType.SINGLE then
        local range = ability.range
        local actors = self.world:query({"Position", "Health", "Actor"})
        for _, result in ipairs(actors) do
            if result.id ~= entityId then
                local actorPos = result.components.Position
                local dist = math.abs(actorPos.x - pos.x) + math.abs(actorPos.y - pos.y)
                if dist <= range then
                    table.insert(targets, result.id)
                end
            end
        end
        
    elseif ability.targetType == AbilityDef.TargetType.AREA then
        local range = ability.range
        local entities = self.world:query({"Position"})
        for _, result in ipairs(entities) do
            local entityPos = result.components.Position
            local dist = math.abs(entityPos.x - pos.x) + math.abs(entityPos.y - pos.y)
            if dist <= range then
                table.insert(targets, result.id)
            end
        end
    end
    
    return targets, #targets
end

-- Try to use ability
-- @param entityId number
-- @param abilityId string
-- @param targetId number (optional)
-- @return boolean, string
function RuleEngine:tryUseAbility(entityId, abilityId, targetId)
    -- Check if can use
    local canUse, reason = self:canUse(entityId, abilityId)
    if not canUse then
        if self.events then
            self.events:emit("AbilityUseFailed", {
                entity = entityId,
                abilityId = abilityId,
                reason = reason,
            })
        end
        return false, reason
    end
    
    local ability = self.abilities[abilityId]
    local comp = self:getAbilityComponent(entityId)
    
    -- Deduct resources
    for resource, cost in pairs(ability.cost) do
        comp.resources[resource] = comp.resources[resource] - cost
    end
    
    -- Set cooldown
    if ability.cooldown > 0 then
        comp.cooldowns[abilityId] = ability.cooldown
        -- Only emit cooldown update when value actually changes
        if self.events then
            self.events:emit("CooldownsUpdated", {
                entity = entityId,
                abilityId = abilityId,
                cooldown = ability.cooldown
            })
        end
    end
    
    -- Execute effects via events
    self:applyAbility(entityId, ability, targetId)
    
    -- Emit success event
    if self.events then
        self.events:emit("AbilityUsed", {
            entity = entityId,
            abilityId = abilityId,
            target = targetId,
        })
    end
    
    return true, "Success"
end

-- Apply ability effects
-- @param sourceId number
-- @param ability AbilityDefinition
-- @param targetId number (optional)
function RuleEngine:applyAbility(sourceId, ability, targetId)
    local pos = self.world.components.Position[sourceId]
    if not pos then
        print("[RuleEngine] No position for source entity: " .. sourceId)
        return
    end
    
    local targets = {}
    
    -- Determine targets
    if ability.targetType == AbilityDef.TargetType.SELF then
        table.insert(targets, sourceId)
        
    elseif ability.targetType == AbilityDef.TargetType.SINGLE then
        -- Auto-select nearest valid target if none specified
        if not targetId then
            local validTargets, count = self:getValidTargets(sourceId, ability.id)
            if count > 0 then
                targetId = validTargets[1]  -- Select first valid target
            end
        end
        if targetId then
            table.insert(targets, targetId)
        end
        
    elseif ability.targetType == AbilityDef.TargetType.AREA then
        local range = ability.range
        local entities = self.world:query({"Position"})
        for _, result in ipairs(entities) do
            local entityPos = result.components.Position
            local dist = math.abs(entityPos.x - pos.x) + math.abs(entityPos.y - pos.y)
            if dist <= range then
                table.insert(targets, result.id)
            end
        end
    end
    
    -- Emit effect requests for each target
    for _, targetId in ipairs(targets) do
        for _, effectId in ipairs(ability.effects) do
            self:applyEffect(effectId, sourceId, targetId)
        end
    end
end

-- Apply effect (emit request event)
-- @param effectId string
-- @param sourceId number
-- @param targetId number
function RuleEngine:applyEffect(effectId, sourceId, targetId)
    local effect = self.effects[effectId]
    if not effect then
        print("[RuleEngine] Effect not found: " .. effectId)
        return
    end
    
    if not self.events then return end
    
    -- Emit appropriate request event based on effect type
    if effect.type == EffectDef.Type.DAMAGE then
        self.events:emit("DamageRequest", {
            source = sourceId,
            target = targetId,
            effectId = effectId,
            baseValue = effect.value,
            damageType = effect.damageType,
        })
        
    elseif effect.type == EffectDef.Type.HEAL then
        self.events:emit("HealRequest", {
            source = sourceId,
            target = targetId,
            effectId = effectId,
            baseValue = effect.value,
        })
        
    elseif effect.type == EffectDef.Type.BUFF or effect.type == EffectDef.Type.DEBUFF then
        if effect.buffId then
            self.events:emit("BuffApplyRequest", {
                source = sourceId,
                target = targetId,
                effectId = effectId,
                buffId = effect.buffId,
                duration = effect.duration or 0,
            })
        end
    end
end

-- Private: Process damage
function RuleEngine:_processDamage(data)
    local targetHealth = self.world.components.Health[data.target]
    if not targetHealth then
        print("[RuleEngine] No Health component for target: " .. data.target)
        return
    end
    
    -- Get buffs component (ECS)
    local buffs = self.world.components.Buffs and self.world.components.Buffs[data.target]
    
    -- Check for shield absorption
    local shieldAbsorb = 0
    if buffs and buffs.activeBuffs then
        local shieldBuff = buffs.activeBuffs["shield"]
        if shieldBuff and shieldBuff.stacks > 0 then
            shieldAbsorb = shieldBuff.stacks * 10  -- Each stack absorbs 10 damage
        end
    end
    
    -- Calculate final damage
    local damage = data.baseValue
    if shieldAbsorb > 0 then
        if shieldAbsorb >= damage then
            -- Shield absorbs all
            shieldAbsorb = shieldAbsorb - damage
            damage = 0
            -- Update shield
            if buffs and buffs.activeBuffs["shield"] then
                if shieldAbsorb <= 0 then
                    buffs.activeBuffs["shield"] = nil
                    if self.events then
                        self.events:emit("BuffExpired", {
                            entity = data.target,
                            buffId = "shield",
                        })
                    end
                else
                    buffs.activeBuffs["shield"].stacks = math.ceil(shieldAbsorb / 10)
                end
            end
        else
            -- Partial absorption
            damage = damage - shieldAbsorb
            buffs.activeBuffs["shield"] = nil
            if self.events then
                self.events:emit("BuffExpired", {
                    entity = data.target,
                    buffId = "shield",
                })
            end
        end
    end
    
    -- Apply damage
    if damage > 0 then
        targetHealth.current = targetHealth.current - damage
    end
    
    -- Emit damage done event
    if self.events then
        self.events:emit("DamageDealt", {
            source = data.source,
            target = data.target,
            amount = data.baseValue,
            actualDamage = damage,
            damageType = data.damageType,
            blocked = shieldAbsorb,
            newHealth = targetHealth.current,
        })
    end
    
    -- Check for death
    self:_checkDeath(data.target, data.source)
end

-- Private: Process heal
function RuleEngine:_processHeal(data)
    local targetHealth = self.world.components.Health[data.target]
    if not targetHealth then
        print("[RuleEngine] No Health component for target: " .. data.target)
        return
    end
    
    -- Calculate actual heal amount (cap at max health)
    local healAmount = math.min(data.baseValue, targetHealth.max - targetHealth.current)
    targetHealth.current = targetHealth.current + healAmount
    
    -- Emit heal applied event
    if self.events then
        self.events:emit("HealingApplied", {
            source = data.source,
            target = data.target,
            amount = healAmount,
            newHealth = targetHealth.current,
        })
    end
end

-- Private: Process buff apply
function RuleEngine:_processBuffApply(data)
    -- 使用 ECS 组件
    local buffs = self:getOrCreateBuffsComponent(data.target)
    local buffDef = self.buffs[data.buffId]
    
    if not buffDef then
        print("[RuleEngine] Buff definition not found: " .. data.buffId)
        return
    end
    
    local existing = buffs.activeBuffs[data.buffId]
    
    if existing then
        -- Handle stack type
        if buffDef.stackType == BuffDef.StackType.REPLACE then
            existing.duration = data.duration
            existing.stacks = 1
        elseif buffDef.stackType == BuffDef.StackType.STACK then
            if existing.stacks < buffDef.maxStack then
                existing.stacks = existing.stacks + 1
            end
            existing.duration = data.duration
        elseif buffDef.stackType == BuffDef.StackType.REFRESH then
            existing.duration = data.duration
        end
    else
        -- New buff
        buffs.activeBuffs[data.buffId] = {
            duration = data.duration,
            stacks = 1,
            source = data.source,
            definition = buffDef,
        }
    end
    
    -- Emit buff added event
    if self.events then
        self.events:emit("BuffAdded", {
            entity = data.target,
            buffId = data.buffId,
            stacks = buffs.activeBuffs[data.buffId].stacks,
            duration = buffs.activeBuffs[data.buffId].duration,
        })
    end
end

-- Private: Process buff tick (DOT/HOT effects)
function RuleEngine:_processBuffTick(data)
    local buffs = self.world.components.Buffs and self.world.components.Buffs[data.entity]
    if not buffs or not buffs.activeBuffs then
        return
    end
    
    local buffData = buffs.activeBuffs[data.buffId]
    if not buffData then
        return
    end
    
    -- Emit buff tick event
    if self.events and data.effectId then
        self.events:emit("BuffTick", {
            entity = data.entity,
            buffId = data.buffId,
            effectId = data.effectId,
            source = data.source,
            stacks = data.stacks,
        })
    end
end

-- Private: Check and handle death
function RuleEngine:_checkDeath(entityId, killerId)
    local health = self.world.components.Health[entityId]
    if not health then return end
    
    if health.current <= 0 then
        if self.events then
            self.events:emit("EntityDied", {
                entity = entityId,
                killer = killerId,
            })
        end
    end
end

-- Turn end processing
function RuleEngine:onTurnEnd()
    -- Reduce cooldowns (使用 ECS Ability 组件)
    if self.world.components.Ability then
        for entityId, comp in pairs(self.world.components.Ability) do
            for abilityId, cd in pairs(comp.cooldowns) do
                if cd > 0 then
                    local newCd = cd - 1
                    comp.cooldowns[abilityId] = newCd
                    if newCd == 0 and self.events then
                        -- Cooldown just finished
                        self.events:emit("CooldownFinished", {
                            entity = entityId,
                            abilityId = abilityId,
                        })
                    end
                end
            end
        end
    end
    
    -- Reduce buff durations and process ticks (使用 ECS Buffs 组件)
    if self.world.components.Buffs then
        for entityId, buffs in pairs(self.world.components.Buffs) do
            if buffs.activeBuffs then
                for buffId, buffData in pairs(buffs.activeBuffs) do
                    -- Process tick effect
                    if buffData.definition and buffData.definition.tickEffect then
                        if self.events then
                            self.events:emit("BuffTickRequest", {
                                entity = entityId,
                                buffId = buffId,
                                effectId = buffData.definition.tickEffect,
                                source = buffData.source,
                                stacks = buffData.stacks,
                            })
                        end
                    end
                    
                    -- Reduce duration
                    buffData.duration = buffData.duration - 1
                    
                    -- Check expiration
                    if buffData.duration <= 0 then
                        buffs.activeBuffs[buffId] = nil
                        if self.events then
                            self.events:emit("BuffExpired", {
                                entity = entityId,
                                buffId = buffId,
                            })
                        end
                    end
                end
            end
        end
    end
    
    if self.events then
        self.events:emit("CooldownsUpdated", {})
    end
end

-- Get ability info
function RuleEngine:getAbilityInfo(entityId, abilityId)
    local ability = self.abilities[abilityId]
    local comp = self:getAbilityComponent(entityId)
    local canUse, reason = self:canUse(entityId, abilityId)
    
    return {
        definition = ability,
        currentCooldown = comp.cooldowns[abilityId] or 0,
        canUse = canUse,
        reason = reason,
    }
end

return {RuleEngine = RuleEngine}