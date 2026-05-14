-- Entity Prototypes
-- Player and enemy definitions

return {
    -- Player character (index 2 in tileset)
    player = {
        Renderable = {tileIndex = 2},
        Player = {},
        Health = {current = 20, max = 20},
        Actor = {moveDelay = 0},  -- Player doesn't use AI movement
        Ability = {abilities = {"punch", "heal", "shield", "fireball"}, cooldowns = {}, resources = {mp = 50, maxMp = 50}}
    },
    
    -- Goblin enemy (index 3 in tileset)
    goblin = {
        Renderable = {tileIndex = 3},
        Actor = {moveDelay = 0.5},  -- Move every 0.5 seconds
        Health = {current = 5, max = 5}
    },
    
    -- Rat enemy (index 4 in tileset)
    rat = {
        Renderable = {tileIndex = 4},
        Actor = {moveDelay = 0.3},  -- Faster than goblins
        Health = {current = 3, max = 3}
    },
    
    -- Orc enemy (index 5 in tileset)
    orc = {
        Renderable = {tileIndex = 5},
        Actor = {moveDelay = 0.8},  -- Slower
        Health = {current = 10, max = 10}
    },
}
