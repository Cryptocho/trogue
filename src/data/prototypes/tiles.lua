-- Tile Prototypes
-- Floor and wall tile definitions

return {
    -- Floor tile (index 0 in tileset)
    floor = {
        Renderable = {tileIndex = 0}
    },
    
    -- Wall tile (index 1 in tileset)
    wall = {
        Renderable = {tileIndex = 1},
        Solid = {}
    },
}
