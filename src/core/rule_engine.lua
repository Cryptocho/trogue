-- RuleEngine: Rule and Effect Application Engine
-- Gameplay Rule Pipeline Layer Core
-- MVP: Effect processing logic is temporarily kept inside RuleEngine, driven by events

local AbilityDef = require("src.data.definitions.ability")
local EffectDef = require("src.data.definitions.effect")
local BuffDef = require("src.data.definitions.buff")
local BuffsComponent = require("src.components.buffs")
local Coordinates = require("src.core.coordinates")

local DEBUG = false
local function debugPrint(...)
    if DEBUG then print(...) end
end

local PERMANENT_BUFF_DURATION = -1

local function createRuleEngine(world, eventBus)
    local instance = {
        world = world,
        events = eventBus,

        -- Registry: Ability/effect/buff definitions
        abilities = {},   -- abilityId -> AbilityDefinition
        effects = {},     -- effectId -> EffectDefinition
        buffs = {},       -- buffId -> BuffDefinition
    }

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
    _ruleEngineRegisterEvents(instance)

    -- Bind methods
    instance.canUse = function(self, entityId, abilityId)
        return _ruleEngineCanUse(self, entityId, abilityId)
    end

    instance.tryUseAbility = function(self, entityId, abilityId, targetX, targetY)
        return _ruleEngineTryUseAbility(self, entityId, abilityId, targetX, targetY)
    end

    instance.applyAbility = function(self, sourceId, ability, targetX, targetY)
        return _ruleEngineApplyAbility(self, sourceId, ability, targetX, targetY)
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

    instance.applyPassiveAbilities = function(self, entityId)
        return _ruleEngineApplyPassiveAbilities(self, entityId)
    end

    instance.removePassiveAbilities = function(self, entityId)
        return _ruleEngineRemovePassiveAbilities(self, entityId)
    end

    instance.getAbilityDef = function(self, abilityId)
        return _ruleEngineGetAbilityDef(self, abilityId)
    end

    return instance
end

-- Register event handlers
function _ruleEngineRegisterEvents(self)
    if not self.events then return end

    -- Listen for ability use request
    self.events:on("AbilityUse", function(data)
        _ruleEngineTryUseAbility(self, data.entity, data.abilityId, data.targetX, data.targetY)
    end, 0)

    -- Listen for damage request (processed internally)
    self.events:on("DamageRequest", function(data)
        _ruleEngineProcessDamage(self, data)
    end, 100)

    -- Listen for heal request
    self.events:on("HealRequest", function(data)
        _ruleEngineProcessHeal(self, data)
    end, 100)

    -- Listen for buff apply request
    self.events:on("BuffApplyRequest", function(data)
        _ruleEngineProcessBuffApply(self, data)
    end, 100)

    -- Listen for turn end
    self.events:on("TurnEnd", function()
        _ruleEngineOnTurnEnd(self)
    end, 100)

    -- Listen for buff tick (DOT/HOT damage)
    self.events:on("BuffTickRequest", function(data)
        _ruleEngineProcessBuffTick(self, data)
    end, 100)
end

-- Get ability definition by ID
-- @param abilityId string
-- @return AbilityDefinition or nil
function _ruleEngineGetAbilityDef(self, abilityId)
    return self.abilities[abilityId]
end

-- Get entity ability component from ECS
-- @param entityId number
-- @return AbilityComponent or nil if not defined
function _ruleEngineGetAbilityComponent(self, entityId)
    if not self.world.components.Ability then
        return nil
    end
    return self.world.components.Ability[entityId]
end

-- Get entity buffs component from ECS
-- @param entityId number
-- @return BuffsComponent or nil if not defined
function _ruleEngineGetBuffsComponent(self, entityId)
    if not self.world.components.Buffs then
        return nil
    end
    return self.world.components.Buffs[entityId]
end

-- Get entity Stats component from ECS
-- @param entityId number
-- @return StatsComponent or nil if not defined
function _ruleEngineGetStatsComponent(self, entityId)
    if not self.world.components.Stats then
        return nil
    end
    return self.world.components.Stats[entityId]
end

-- Check if ability can be used
-- @param entityId number
-- @param abilityId string
-- @return boolean, string (canUse, reason)
function _ruleEngineCanUse(self, entityId, abilityId)
    local ability = self.abilities[abilityId]
    if not ability then
        return false, "Ability not found: " .. abilityId
    end

    if ability.mode == AbilityDef.Mode.PASSIVE then
        return false, "Passive ability cannot be activated"
    end

    local comp = _ruleEngineGetAbilityComponent(self, entityId)
    if not comp then
        return false, "Entity has no Ability component"
    end

    -- Check if entity has this ability (O(1) lookup with Set)
    if not comp.abilities[abilityId] then
        return false, "Ability not learned"
    end

    -- Check cooldown
    local cd = comp.cooldowns[abilityId] or 0
    if cd > 0 then
        return false, "On cooldown (" .. cd .. ")"
    end

    -- Check resource cost
    local stats = _ruleEngineGetStatsComponent(self, entityId)
    for resource, cost in pairs(ability.cost) do
        local current = stats and stats.current[resource] or 0
        if current < cost then
            return false, "Not enough " .. resource
        end
    end

    return true, "Can use"
end

-- Try to use ability
-- @param entityId number
-- @param abilityId string
-- @param targetX number
-- @param targetY number
-- @return boolean, string
function _ruleEngineTryUseAbility(self, entityId, abilityId, targetX, targetY)
    -- Check if can use
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

    -- Deduct resources
    local stats = _ruleEngineGetStatsComponent(self, entityId)
    for resource, cost in pairs(ability.cost) do
        if stats then
            stats.current[resource] = stats.current[resource] - cost
        end
    end

    -- Set cooldown
    if ability.cooldown > 0 then
        comp.cooldowns[abilityId] = ability.cooldown
    end

    -- Execute effects via events
    local success = _ruleEngineApplyAbility(self, entityId, ability, targetX, targetY)

    -- Only emit success if ability had valid targets
    if success and self.events then
        self.events:emit("AbilityUsed", {
            entity = entityId,
            abilityId = abilityId,
            targetX = targetX,
            targetY = targetY,
        })
    end

    return true, "Success"
end

-- Apply ability effects
-- @param sourceId number
-- @param ability AbilityDefinition
-- @param targetX number
-- @param targetY number
function _ruleEngineApplyAbility(self, sourceId, ability, targetX, targetY)
    local pos = self.world.components.Position[sourceId]
    if not pos then
        debugPrint("[RuleEngine] No position for source entity: " .. sourceId)
        return
    end

    local mapRenderer = self.world:getSystem("MapRenderer")
    local mapW = mapRenderer and mapRenderer.width or 0
    local mapH = mapRenderer and mapRenderer.height or 0

    local rangeFunc = ability.rangeFunc
    if not rangeFunc then
        debugPrint("[RuleEngine] Ability has no rangeFunc: " .. ability.id)
        return false
    end

    local tiles = rangeFunc(pos.x, pos.y, targetX, targetY, mapW, mapH)
    local targets = {}
    local spatialHash = self.world:getSpatialHash()
    for _, tile in ipairs(tiles) do
        local entities = spatialHash:getAt(tile.x, tile.y)
        if entities then
            for _, eid in ipairs(entities) do
                targets[eid] = true
            end
        end
    end

    local targetList = {}
    for eid in pairs(targets) do
        table.insert(targetList, eid)
    end

    if #targetList == 0 then
        debugPrint("[RuleEngine] No valid targets for ability: " .. ability.id)
        return false
    end

    for _, targetId in ipairs(targetList) do
        for _, effectId in ipairs(ability.effects) do
            _ruleEngineApplyEffect(self, effectId, sourceId, targetId)
        end
    end

    return true
end

-- Apply effect (emit request event)
-- @param effectId string
-- @param sourceId number
-- @param targetId number
function _ruleEngineApplyEffect(self, effectId, sourceId, targetId)
    local effect = self.effects[effectId]
    if not effect then
        debugPrint("[RuleEngine] Effect not found: " .. effectId)
        return
    end

    -- 概率判定
    local triggerChance = nil
    if effect.chanceFormula then
        triggerChance = _ruleEngineEvaluateChanceFormula(self, sourceId, effect.chanceFormula)
    elseif effect.chance then
        triggerChance = effect.chance
    end
    if triggerChance and math.random() >= triggerChance then
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

-- Get WeaponSystem reference from world (lazy cache)
-- @param self RuleEngine instance
-- @return WeaponSystem table or nil
function _ruleEngineGetWeaponSystem(self)
    if not self._weaponSystem then
        self._weaponSystem = self.world:getSystem("WeaponSystem")
    end
    return self._weaponSystem
end

-- Evaluate damage formula
-- Formula: weaponBaseDamage * basePercent + sum(statValue * multiplier) + flatBonus + weaponPhysicalDamageBonus
-- @param sourceId number Attack source entity
-- @param formula table { basePercent, statScaling, flatBonus }
-- @return number Final damage value
function _ruleEngineEvaluateFormula(self, sourceId, formula)
    local damage = 0
    local stats = _ruleEngineGetStatsComponent(self, sourceId)
    local ws = _ruleEngineGetWeaponSystem(self)

    if formula.basePercent then
        local baseDamage = ws and ws:getBaseDamage(self.world, sourceId) or 2
        damage = damage + baseDamage * formula.basePercent
    end

    if formula.statScaling and stats then
        for _, scale in ipairs(formula.statScaling) do
            local statValue = stats.base[scale.stat] or 0
            damage = damage + statValue * (scale.multiplier or 1)
        end
    end

    if formula.flatBonus then
        damage = damage + formula.flatBonus
    end

    if ws then
        damage = damage + ws:getPhysicalDamageBonus(self.world, sourceId)
    end

    return math.floor(damage)
end

-- Evaluate chance formula (probability 0~1)
-- @param entityId number Source entity for stat lookup
-- @param formula table { basePercent, statScaling }
-- @return number Probability in 0~1 range
function _ruleEngineEvaluateChanceFormula(self, entityId, formula)
    local chance = formula.basePercent or 0
    if formula.statScaling then
        local stats = _ruleEngineGetStatsComponent(self, entityId)
        if stats then
            for _, scale in ipairs(formula.statScaling) do
                chance = chance + (stats.base[scale.stat] or 0) * (scale.multiplier or 0)
            end
        end
    end
    return math.min(chance, 1.0)
end

-- Evaluate damage formula
function _ruleEngineProcessDamage(self, data)
    local stats = _ruleEngineGetStatsComponent(self, data.target)
    if not stats then
        debugPrint("[RuleEngine] No Stats component for target: " .. data.target)
        return
    end

    -- Get buffs component (ECS)
    local buffs = self.world.components.Buffs and self.world.components.Buffs[data.target]

    -- Check for shield absorption
    local shieldAbsorb = 0
    local shieldPerStack = 0
    if stats.modifiers and stats.modifiers["shield"] then
        shieldAbsorb = stats.modifiers["shield"].damageAbsorb or 0
        local shieldDef = self.buffs["shield"]
        if shieldDef and shieldDef.statModifiers then
            shieldPerStack = shieldDef.statModifiers.damageAbsorb or 10
        end
    end

    -- Calculate final damage
    local damage = data.baseValue

    -- Formula-based damage override (when effect has valueFormula)
    local effect = self.effects[data.effectId]
    if effect and effect.valueFormula and data.source then
        damage = _ruleEngineEvaluateFormula(self, data.source, effect.valueFormula)
    end

    local rawDamage = damage

    if shieldAbsorb > 0 then
        if shieldAbsorb >= damage then
            -- Shield absorbs all
            shieldAbsorb = shieldAbsorb - damage
            damage = 0
            -- Update shield
            if shieldAbsorb <= 0 then
                buffs.activeBuffs["shield"] = nil
                stats.modifiers["shield"] = nil
                _ruleEngineRecalcComputed(stats)
                if self.events then
                    self.events:emit("BuffExpired", {
                        entity = data.target,
                        buffId = "shield",
                    })
                end
            else
                stats.modifiers["shield"].damageAbsorb = shieldAbsorb
                if shieldPerStack > 0 then
                    buffs.activeBuffs["shield"].stacks = math.ceil(shieldAbsorb / shieldPerStack)
                end
            end
        else
            -- Partial absorption
            damage = damage - shieldAbsorb
            buffs.activeBuffs["shield"] = nil
            stats.modifiers["shield"] = nil
            _ruleEngineRecalcComputed(stats)
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
        stats.current.hp = stats.current.hp - damage
    end

    -- Emit damage done event
    if self.events then
        self.events:emit("DamageDealt", {
            source = data.source,
            target = data.target,
            amount = rawDamage,
            actualDamage = damage,
            damageType = data.damageType,
            blocked = shieldAbsorb,
            newHealth = stats.current.hp,
        })
    end

    -- Check for death
    _ruleEngineCheckDeath(self, data.target, data.source)
end

-- Private: Process heal
function _ruleEngineProcessHeal(self, data)
    local stats = _ruleEngineGetStatsComponent(self, data.target)
    if not stats then
        debugPrint("[RuleEngine] No Stats component for target: " .. data.target)
        return
    end

    -- Calculate actual heal amount (cap at max health)
    local healAmount = math.min(data.baseValue, stats.max.hp - stats.current.hp)
    stats.current.hp = stats.current.hp + healAmount

    -- Emit heal applied event
    if self.events then
        self.events:emit("HealingApplied", {
            source = data.source,
            target = data.target,
            amount = healAmount,
            newHealth = stats.current.hp,
        })
    end
end

function _ruleEngineRecalcComputed(stats)
    if not stats._baseComputed then
        stats._baseComputed = {}
        for k, v in pairs(stats.computed) do
            stats._baseComputed[k] = v
        end
    end

    for k, v in pairs(stats._baseComputed) do
        stats.computed[k] = v
    end

    for _, mod in pairs(stats.modifiers) do
        for field, value in pairs(mod) do
            if stats.computed[field] ~= nil then
                stats.computed[field] = stats.computed[field] + value
            end
        end
    end
end

-- Private: Process buff apply
function _ruleEngineProcessBuffApply(self, data)
    -- Use buffs component
    local buffs = _ruleEngineGetBuffsComponent(self, data.target)
    local stats = _ruleEngineGetStatsComponent(self, data.target)
    if not buffs then
        debugPrint("[RuleEngine] No Buffs component for entity: " .. data.target)
        return
    end

    local buffDef = self.buffs[data.buffId]

    if not buffDef then
        debugPrint("[RuleEngine] Buff definition not found: " .. data.buffId)
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
        existing.permanent = data.permanent or existing.permanent
    else
        -- New buff
        buffs.activeBuffs[data.buffId] = {
            duration = data.duration,
            stacks = 1,
            source = data.source,
            definition = buffDef,
            permanent = data.permanent or false,
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

    if buffDef.statModifiers and next(buffDef.statModifiers) and stats then
        local active = buffs.activeBuffs[data.buffId]
        local stacks = active and active.stacks or 1
        local effective = {}
        for field, value in pairs(buffDef.statModifiers) do
            effective[field] = value * stacks
        end
        stats.modifiers[data.buffId] = effective
        _ruleEngineRecalcComputed(stats)
    end
end

-- Private: Check and handle death
function _ruleEngineCheckDeath(self, entityId, killerId)
    local stats = _ruleEngineGetStatsComponent(self, entityId)
    if not stats then return end

    if stats.current.hp <= 0 then
        if self.events then
            self.events:emit("EntityDied", {
                entity = entityId,
                killer = killerId,
            })
        end
    end
end

-- Private: Process buff tick (DOT/HOT damage)
function _ruleEngineProcessBuffTick(self, data)
    if not data.entity or not data.effectId then
        debugPrint("[RuleEngine] BuffTickRequest missing entity or effectId")
        return
    end

    -- Apply the tick effect (damage or heal)
    _ruleEngineApplyEffect(self, data.effectId, data.source, data.entity)
end

-- Turn end processing (Orchestrator, emit events only)
function _ruleEngineOnTurnEnd(self)
    _ruleEngineReduceCooldowns(self)
    _ruleEngineProcessBuffTicks(self)

    if self.events then
        self.events:emit("CooldownsUpdated", {})
    end
end

-- Private: Reduce cooldowns for all entities
function _ruleEngineReduceCooldowns(self)
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

-- Private: Process buff ticks and expiration
function _ruleEngineProcessBuffTicks(self)
    if not self.world.components.Buffs then
        debugPrint("[RuleEngine] No Buffs component found")
        return
    end

    for entityId, buffs in pairs(self.world.components.Buffs) do
        if not buffs.activeBuffs then goto continue end

        for buffId, buffData in pairs(buffs.activeBuffs) do
            if not buffData.permanent then
                -- First reduce duration and check expiration
                buffData.duration = buffData.duration - 1

                -- Only emit tick and keep buff if duration is still >= 0 after reduction
                if buffData.duration >= 0 then
                    -- Emit tick event if buff has tickEffect (tick happens AFTER this turn)
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

                -- Remove buff if duration expired
                if buffData.duration < 0 then
                    buffs.activeBuffs[buffId] = nil
                    local stats = _ruleEngineGetStatsComponent(self, entityId)
                    if stats and stats.modifiers and stats.modifiers[buffId] then
                        stats.modifiers[buffId] = nil
                        _ruleEngineRecalcComputed(stats)
                    end
                    if self.events then
                        self.events:emit("BuffExpired", {
                            entity = entityId,
                            buffId = buffId,
                        })
                    end
                end
            end
        end

        ::continue::
    end
end

-- Get ability info
function _ruleEngineGetAbilityInfo(self, entityId, abilityId)
    local ability = self.abilities[abilityId]
    local comp = _ruleEngineGetAbilityComponent(self, entityId)

    -- Handle missing Ability component
    if not comp then
        return {
            definition = ability,
            currentCooldown = 0,
            canUse = false,
            reason = "Entity has no Ability component",
        }
    end

    -- Check if can use (already handles nil comp internally)
    local canUse, reason = _ruleEngineCanUse(self, entityId, abilityId)

    return {
        definition = ability,
        currentCooldown = comp.cooldowns[abilityId] or 0,
        canUse = canUse,
        reason = reason,
    }
end

-- Apply passive abilities for an entity (called when entity spawns)
-- Scans entity's abilities for PASSIVE mode abilities and applies their permanent buffs
-- @param entityId number
function _ruleEngineApplyPassiveAbilities(self, entityId)
    local comp = _ruleEngineGetAbilityComponent(self, entityId)
    if not comp then return end

    for abilityId in pairs(comp.abilities) do
        local ability = self.abilities[abilityId]
        if ability and ability.mode == AbilityDef.Mode.PASSIVE then
            local buffId = ability.passiveBuff
            if buffId and self.buffs[buffId] then
                if self.events then
                    self.events:emit("BuffApplyRequest", {
                        source = entityId,
                        target = entityId,
                        buffId = buffId,
                        duration = PERMANENT_BUFF_DURATION,
                        permanent = true,
                    })
                end
            end
        end
    end
end

-- Remove passive abilities buffs for an entity (called before entity death/despawn)
-- Cleans up modifiers and recalculates computed stats
-- @param entityId number
function _ruleEngineRemovePassiveAbilities(self, entityId)
    local stats = _ruleEngineGetStatsComponent(self, entityId)
    if not stats then return end

    local buffs = _ruleEngineGetBuffsComponent(self, entityId)
    if not buffs or not buffs.activeBuffs then return end

    local comp = _ruleEngineGetAbilityComponent(self, entityId)
    if not comp then return end

    for abilityId in pairs(comp.abilities) do
        local ability = self.abilities[abilityId]
        if ability and ability.mode == AbilityDef.Mode.PASSIVE and ability.passiveBuff then
            local buffId = ability.passiveBuff
            if buffs.activeBuffs[buffId] and buffs.activeBuffs[buffId].permanent then
                buffs.activeBuffs[buffId] = nil
                if stats.modifiers and stats.modifiers[buffId] then
                    stats.modifiers[buffId] = nil
                    _ruleEngineRecalcComputed(stats)
                end
            end
        end
    end
end

return {createRuleEngine = createRuleEngine}