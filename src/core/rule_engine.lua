    -- RuleEngine: Rule and Effect Application Engine
    -- Gameplay Rule Pipeline Layer Core
    -- MVP: Effect processing logic is temporarily kept inside RuleEngine, driven by events

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
            
            -- Registry: Ability/effect/buff definitions
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
        
        -- Listen for turn end
        self.events:on("TurnEnd", function()
            self:onTurnEnd()
        end, 100)
        
        -- Listen for buff tick (DOT/HOT damage)
        self.events:on("BuffTickRequest", function(data)
            self:_processBuffTick(data)
        end, 100)
    end

    -- Get entity ability component from ECS
    -- @param entityId number
    -- @return AbilityComponent or nil if not defined
    function RuleEngine:getAbilityComponent(entityId)
        if not self.world.components.Ability then
            return nil
        end
        return self.world.components.Ability[entityId]
    end

    -- Get entity buffs component from ECS
    -- @param entityId number
    -- @return BuffsComponent or nil if not defined
    function RuleEngine:getBuffsComponent(entityId)
        if not self.world.components.Buffs then
            return nil
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
        for resource, cost in pairs(ability.cost) do
            local current = comp.resources[resource] or 0
            if current < cost then
                return false, "Not enough " .. resource
            end
        end
        
        return true, "Can use"
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
        end
        
        -- Execute effects via events
        local success = self:applyAbility(entityId, ability, targetId)
        
        -- Only emit success if ability had valid targets
        if success and self.events then
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
                local range = ability.range
                local actors = self.world:query({"Position", "Health", "Actor"})
                local nearest = nil
                local nearestDist = math.huge
                for _, result in ipairs(actors) do
                    if result.id ~= sourceId then
                        local actorPos = result.components.Position
                        local dist = math.abs(actorPos.x - pos.x) + math.abs(actorPos.y - pos.y)
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
                local dist = math.abs(entityPos.x - pos.x) + math.abs(entityPos.y - pos.y)
                if dist <= range and result.id ~= sourceId then
                    table.insert(targets, result.id)
                end
            end
        end
        
        -- If no targets, don't emit success (ability fizzled)
        if #targets == 0 then
            print("[RuleEngine] No valid targets for ability: " .. ability.id)
            return false
        end
        
        -- Emit effect requests for each target
        for _, targetId in ipairs(targets) do
            for _, effectId in ipairs(ability.effects) do
                self:applyEffect(effectId, sourceId, targetId)
            end
        end
        
        return true
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
        -- Use buffs component
        local buffs = self:getBuffsComponent(data.target)
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

    -- Private: Process buff tick (DOT/HOT damage)
    function RuleEngine:_processBuffTick(data)
        if not data.entity or not data.effectId then
            print("[RuleEngine] BuffTickRequest missing entity or effectId")
            return
        end
        
        -- Apply the tick effect (damage or heal)
        self:applyEffect(data.effectId, data.source, data.entity)
    end

    -- Turn end processing (Orchestrator, emit events only)
    function RuleEngine:onTurnEnd()
        self:_reduceCooldowns()
        self:_processBuffTicks()
        
        if self.events then
            self.events:emit("CooldownsUpdated", {})
        end
    end

    -- Private: Reduce cooldowns for all entities
    function RuleEngine:_reduceCooldowns()
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
    function RuleEngine:_processBuffTicks()
        if not self.world.components.Buffs then 
            print("[RuleEngine] No Buffs component found")
            return 
        end
        
        for entityId, buffs in pairs(self.world.components.Buffs) do
            if not buffs.activeBuffs then goto continue end
            
            for buffId, buffData in pairs(buffs.activeBuffs) do
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

    -- Get ability info
    function RuleEngine:getAbilityInfo(entityId, abilityId)
        local ability = self.abilities[abilityId]
        local comp = self:getAbilityComponent(entityId)
        
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
        local canUse, reason = self:canUse(entityId, abilityId)
        
        return {
            definition = ability,
            currentCooldown = comp.cooldowns[abilityId] or 0,
            canUse = canUse,
            reason = reason,
        }
    end

    return {RuleEngine = RuleEngine}