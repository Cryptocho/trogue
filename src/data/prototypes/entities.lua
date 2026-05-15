-- Entity Prototypes
-- Player and enemy definitions

return {
    -- Player character (index 2 in tileset)
    player = {
        Renderable = {tileIndex = 2},
        Player = {},
        Health = {current = 10, max = 10},
        Actor = {moveDelay = 0},  -- Player doesn't use AI movement
        -- abilities use Set structure: {abilityId = true}
        Ability = {
            abilities = {punch = true, heal = true, shield = true, fireball = true},
            cooldowns = {},
            resources = {mp = 50, maxMp = 50}
        },
        Buffs = {activeBuffs = {}},  -- Required for DOT/HOT effects
    },
    
    -- Goblin enemy (index 3 in tileset)
    goblin = {
        Renderable = {tileIndex = 3},
        Actor = {moveDelay = 0.5},  -- Move every 0.5 seconds
        Health = {current = 25, max = 25},
        Ability = {
            abilities = {punch = true},
            cooldowns = {},
            resources = {mp = 30, maxMp = 30}
        },
        Buffs = {activeBuffs = {}},  -- Required for DOT/HOT effects
    },
    
    -- Rat enemy (index 4 in tileset)
    rat = {
        Renderable = {tileIndex = 4},
        Actor = {moveDelay = 0.3},  -- Faster than goblins
        Health = {current = 3, max = 3},
        Ability = {
            abilities = {punch = true},
            cooldowns = {},
            resources = {mp = 20, maxMp = 20}
        },
        Buffs = {activeBuffs = {}},  -- Required for DOT/HOT effects
    },
    
    -- Orc enemy (index 5 in tileset)
    orc = {
        Renderable = {tileIndex = 5},
        Actor = {moveDelay = 0.8},  -- Slower
        Health = {current = 10, max = 10},
        Ability = {
            abilities = {punch = true},
            cooldowns = {},
            resources = {mp = 40, maxMp = 40}
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