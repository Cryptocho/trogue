-- Entity Prototypes
-- Player and enemy definitions

return {
    -- Player character (index 2 in tileset)
    player = {
        Renderable = {tileIndex = 2},
        Player = {},
        Stats = {
            base = {strength = 10, agility = 5, intelligence = 5, vitality = 10},
            current = {hp = 10, mp = 50},
            max = {hp = 10, mp = 50},
            computed = {physicalDamageBonus = 0, critChance = 0.05, critMultiplier = 1.5, poisonDurationMultiplier = 1.0},
            modifiers = {},
        },
        Actor = {moveDelay = 0},  -- Player doesn't use AI movement
        -- abilities use Set structure: {abilityId = true}
        Ability = {
            abilities = {punch = true, heal = true, shield = true, fireball = true},
            cooldowns = {},
        },
        Buffs = {activeBuffs = {}},  -- Required for DOT/HOT effects
    },
    
    -- Goblin enemy (index 3 in tileset)
    goblin = {
        Renderable = {tileIndex = 3},
        Actor = {moveDelay = 0.5},  -- Move every 0.5 seconds
        Stats = {
            base = {strength = 8, agility = 6, intelligence = 3, vitality = 8},
            current = {hp = 25, mp = 30},
            max = {hp = 25, mp = 30},
            computed = {physicalDamageBonus = 0, critChance = 0.05, critMultiplier = 1.5, poisonDurationMultiplier = 1.0},
            modifiers = {},
        },
        Ability = {
            abilities = {punch = true},
            cooldowns = {},
        },
        Buffs = {activeBuffs = {}},  -- Required for DOT/HOT effects
    },
    
    -- Rat enemy (index 4 in tileset)
    rat = {
        Renderable = {tileIndex = 4},
        Actor = {moveDelay = 0.3},  -- Faster than goblins
        Stats = {
            base = {strength = 4, agility = 8, intelligence = 2, vitality = 3},
            current = {hp = 3, mp = 20},
            max = {hp = 3, mp = 20},
            computed = {physicalDamageBonus = 0, critChance = 0.10, critMultiplier = 1.5, poisonDurationMultiplier = 1.0},
            modifiers = {},
        },
        Ability = {
            abilities = {punch = true},
            cooldowns = {},
        },
        Buffs = {activeBuffs = {}},  -- Required for DOT/HOT effects
    },
    
    -- Orc enemy (index 5 in tileset)
    orc = {
        Renderable = {tileIndex = 5},
        Actor = {moveDelay = 0.8},  -- Slower
        Stats = {
            base = {strength = 12, agility = 3, intelligence = 2, vitality = 12},
            current = {hp = 10, mp = 40},
            max = {hp = 10, mp = 40},
            computed = {physicalDamageBonus = 0, critChance = 0.03, critMultiplier = 1.5, poisonDurationMultiplier = 1.0},
            modifiers = {},
        },
        Ability = {
            abilities = {punch = true},
            cooldowns = {},
        },
        Buffs = {activeBuffs = {}},  -- Required for DOT/HOT effects
    },
    
    -- Poison pool (index 6 in tileset) - MVP prototype only, system not implemented
    poison_pool = {
        Position = {x = 0, y = 0},
        Renderable = {tileIndex = 6},
        Solid = {},
        EffectTile = {
            effectType = "poison",
            damage = 5,
            duration = 3,
            spreadChance = 0.3,
            tickRate = 1
        }
    },
    
    -- Fire pool (index 7 in tileset) - MVP prototype only, system not implemented
    fire_pool = {
        Position = {x = 0, y = 0},
        Renderable = {tileIndex = 7},
        Solid = {},
        EffectTile = {
            effectType = "fire",
            damage = 8,
            duration = 2,
            spreadChance = 0.5,
            tickRate = 1
        }
    },
}