-- RuleEngine: Rule and Effect Application Engine
-- Gameplay Rule Pipeline Layer Core

local AbilityDef = require("src.data.definitions.ability")
local EffectDef = require("src.data.definitions.effect")
local BuffDef = require("src.data.definitions.buff")
local BuffsComponent = require("src.components.buffs")
local Coordinates = require("src.core.coordinates")

local function createRuleEngine(world, eventBus)
    local instance = {
        world = world,
        events = eventBus,
        abilities = {},
        effects = {},
        buffs = {},
    }

    for id, ability in pairs(AbilityDef.builtin) do
        instance.abilities[id] = ability
    end
    for id, effect in pairs(EffectDef.builtin) do
        instance.effects[id] = effect
    end
    for id, buff in pairs(BuffDef.builtin) do
        instance.buffs[id] = buff
    end

    _ruleEngineRegisterEvents(instance)

    instance.canUse = function(self, entityId, abilityId)
        return _ruleEngineCanUse(self, entityId, abilityId)
    end

    instance.tryUseAbility = function(self, entityId, abilityId, targetId)
        return _ruleEngineTryUseAbility(self, entityId, abilityId, targetId)
    end

    instance.applyAbility = function(self, sourceId, ability, targetId)
        return _ruleEngineApplyAbility(self, sourceId, ability, targetId)
    end

    instance.applyEffect = function(self, effectId, sourceId, targetId)
        _ruleEngineApplyEffect(self, effectId, sourceId, targetId)
    end

    instance.onTurnEnd = function(self)
        _ruleEngineOnTurnEnd(self)
    end

    instance.getAbilityInfo = function(self, entityId, abilityId)
        return _ruleEngineGetAbilityInfo(self, entityId, abilityId)
    end

    instance.getAbilityComponent = function(self, entityId)
        return _ruleEngineGetAbilityComponent(self, entityId)
    end

    instance.getBuffsComponent = function(self, entityId)
        return _ruleEngineGetBuffsComponent(self, entityId)
    end

    return instance
end

function _ruleEngineRegisterEvents(self)
    if not self.events then return end

    self.events:on("AbilityUse", function(data)
        _ruleEngineTryUseAbility(self, data.entity, data.abilityId, data.targetId)
    end, 0)

    self.events:on("DamageRequest", function(data)
        _processDamage(self, data)
    end, 100)

    self.events:on("HealRequest", function(data)
        _processHeal(self, data)
    end, 100)

    self.events:on("BuffApplyRequest", function(data)
        _processBuffApply(self, data)
    end, 100)

    self.events:on("TurnEnd", function()
        _ruleEngineOnTurnEnd(self)
    end, 100)

    self.events:on("BuffTickRequest", function(data)
        _processBuffTick(self, data)
    end, 100)
end

function _ruleEngineGetAbilityComponent(self, entityId)
    if not self.world.components.Ability then
        return nil
    end
    return self.world.components.Ability[entityId]
end

function _ruleEngineGetBuffsComponent(self, entityId)
    if not self.world.components.Buffs then
        return nil
    end
    return self.world.components.Buffs[entityId]
end

function _ruleEngineCanUse(self, entityId, abilityId)
    local ability = self.abilities[abilityId]
    if not ability then
        return false, "Ability not found: " .. abilityId
    end

    local comp = _ruleEngineGetAbilityComponent(self, entityId)
    if not comp then
        return false, "Entity has no Ability component"
    end

    if not comp.abilities[abilityId] then
        return false, "Ability not learned"
    end

    local cd = comp.cooldowns[abilityId] or 0
    if cd > 0 then
        return false, "On cooldown (" .. cd .. ")"
    end

    for resource, cost in pairs(ability.cost) do
        local current = comp.resources[resource] or 0
        if current < cost then
            return false, "Not enough " .. resource
        end
    end

    return true, "Can use"
end

function _ruleEngineTryUseAbility(self, entityId, abilityId, targetId)
    local canUse, reason = _ruleEngineCanUse(self, entityId, abilityId)
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
    local comp = _ruleEngineGetAbilityComponent(self, entityId)

    for resource, cost in pairs(ability.cost) do
        comp.resources[resource] = comp.resources[resource] - cost
    end

    if ability.cooldown > 0 then
        comp.cooldowns[abilityId] = ability.cooldown
    end

    local success = _ruleEngineApplyAbility(self, entityId, ability, targetId)

    if success and self.events then
        self.events:emit("AbilityUsed", {
            entity = entityId,
            abilityId = abilityId,
            target = targetId,
        })
    end

    return true, "Success"
end

function _ruleEngineApplyAbility(self, sourceId, ability, targetId)
    local pos = self.world.components.Position[sourceId]
    if not pos then
        print("[RuleEngine] No position for source entity: " .. sourceId)
        return
    end

    local targets = {}

    if ability.targetType == AbilityDef.TargetType.SELF then
        table.insert(targets, sourceId)

    elseif ability.targetType == AbilityDef.TargetType.SINGLE then
        if not targetId then
            local range = ability.range
            local actors = self.world:query({"Position", "Health", "Actor"})
            local nearest = nil
            local nearestDist = math.huge
            for _, result in ipairs(actors) do
                if result.id ~= sourceId then
                    local actorPos = result.components.Position
                    local dist = Coordinates.manhattanDistance(actorPos.x, actorPos.y, pos.x, pos.y)
                    if dist <= range and dist < nearestDist then
                        nearestDist = dist
                        nearest = result.id
                    end
                end
            end
            targetId = nearest
        end
        if targetId then
            table.insert(targets, targetId)
        end

    elseif ability.targetType == AbilityDef.TargetType.AREA then
        local range = ability.range
        local entities = self.world:query({"Position"})
        for _, result in ipairs(entities) do
            local entityPos = result.components.Position
            local dist = Coordinates.manhattanDistance(entityPos.x, entityPos.y, pos.x, pos.y)
            if dist <= range and result.id ~= sourceId then
                table.insert(targets, result.id)
            end
        end
    end

    if #targets == 0 then
        print("[RuleEngine] No valid targets for ability: " .. ability.id)
        return false
    end

    for _, targetId in ipairs(targets) do
        for _, effectId in ipairs(ability.effects) do
            _ruleEngineApplyEffect(self, effectId, sourceId, targetId)
        end
    end

    return true
end

function _ruleEngineApplyEffect(self, effectId, sourceId, targetId)
    local effect = self.effects[effectId]
    if not effect then
        print("[RuleEngine] Effect not found: " .. effectId)
        return
    end

    if not self.events then return end

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

function _processDamage(self, data)
    local targetHealth = self.world.components.Health[data.target]
    if not targetHealth then
        print("[RuleEngine] No Health component for target: " .. data.target)
        return
    end

    local buffs = self.world.components.Buffs and self.world.components.Buffs[data.target]

    local shieldAbsorb = 0
    if buffs and buffs.activeBuffs then
        local shieldBuff = buffs.activeBuffs["shield"]
        if shieldBuff and shieldBuff.stacks > 0 then
            shieldAbsorb = shieldBuff.stacks * 10
        end
    end

    local damage = data.baseValue
    if shieldAbsorb > 0 then
        if shieldAbsorb >= damage then
            shieldAbsorb = shieldAbsorb - damage
            damage = 0
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

    if damage > 0 then
        targetHealth.current = targetHealth.current - damage
    end

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

    _ruleEngineCheckDeath(self, data.target, data.source)
end

function _processHeal(self, data)
    local targetHealth = self.world.components.Health[data.target]
    if not targetHealth then
        print("[RuleEngine] No Health component for target: " .. data.target)
        return
    end

    local healAmount = math.min(data.baseValue, targetHealth.max - targetHealth.current)
    targetHealth.current = targetHealth.current + healAmount

    if self.events then
        self.events:emit("HealingApplied", {
            source = data.source,
            target = data.target,
            amount = healAmount,
            newHealth = targetHealth.current,
        })
    end
end

function _processBuffApply(self, data)
    local buffs = _ruleEngineGetBuffsComponent(self, data.target)
    if not buffs then
        print("[RuleEngine] No Buffs component for entity: " .. data.target)
        return
    end

    local buffDef = self.buffs[data.buffId]

    if not buffDef then
        print("[RuleEngine] Buff definition not found: " .. data.buffId)
        return
    end

    local existing = buffs.activeBuffs[data.buffId]

    if existing then
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
        buffs.activeBuffs[data.buffId] = {
            duration = data.duration,
            stacks = 1,
            source = data.source,
            definition = buffDef,
        }
    end

    if self.events then
        self.events:emit("BuffAdded", {
            entity = data.target,
            buffId = data.buffId,
            stacks = buffs.activeBuffs[data.buffId].stacks,
            duration = buffs.activeBuffs[data.buffId].duration,
        })
    end
end

function _ruleEngineCheckDeath(self, entityId, killerId)
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

function _processBuffTick(self, data)
    if not data.entity or not data.effectId then
        print("[RuleEngine] BuffTickRequest missing entity or effectId")
        return
    end

    _ruleEngineApplyEffect(self, data.effectId, data.source, data.entity)
end

function _ruleEngineOnTurnEnd(self)
    _reduceCooldowns(self)
    _processBuffTicks(self)

    if self.events then
        self.events:emit("CooldownsUpdated", {})
    end
end

function _reduceCooldowns(self)
    if not self.world.components.Ability then return end

    for entityId, comp in pairs(self.world.components.Ability) do
        for abilityId, cd in pairs(comp.cooldowns) do
            if cd > 0 then
                local newCd = cd - 1
                comp.cooldowns[abilityId] = newCd
                if newCd == 0 and self.events then
                    self.events:emit("CooldownFinished", {
                        entity = entityId,
                        abilityId = abilityId,
                    })
                end
            end
        end
    end
end

function _processBuffTicks(self)
    if not self.world.components.Buffs then
        print("[RuleEngine] No Buffs component found")
        return
    end

    for entityId, buffs in pairs(self.world.components.Buffs) do
        if not buffs.activeBuffs then goto continue end

        for buffId, buffData in pairs(buffs.activeBuffs) do
            buffData.duration = buffData.duration - 1

            if buffData.duration >= 0 then
                if buffData.definition and buffData.definition.tickEffect and self.events then
                    self.events:emit("BuffTickRequest", {
                        entity = entityId,
                        buffId = buffId,
                        effectId = buffData.definition.tickEffect,
                        source = buffData.source,
                        stacks = buffData.stacks,
                    })
                end
            end

            if buffData.duration < 0 then
                buffs.activeBuffs[buffId] = nil
                if self.events then
                    self.events:emit("BuffExpired", {
                        entity = entityId,
                        buffId = buffId,
                    })
                end
            end
        end

        ::continue::
    end
end

function _ruleEngineGetAbilityInfo(self, entityId, abilityId)
    local ability = self.abilities[abilityId]
    local comp = _ruleEngineGetAbilityComponent(self, entityId)

    if not comp then
        return {
            definition = ability,
            currentCooldown = 0,
            canUse = false,
            reason = "Entity has no Ability component",
        }
    end

    local canUse, reason = _ruleEngineCanUse(self, entityId, abilityId)

    return {
        definition = ability,
        currentCooldown = comp.cooldowns[abilityId] or 0,
        canUse = canUse,
        reason = reason,
    }
end

return {
    createRuleEngine = createRuleEngine,
}