local Autotile = {}

function Autotile.computeBitmask(x, y, matchFn)
    local bm = 0
    if matchFn(x + 1, y) then bm = bm + 1 end
    if matchFn(x, y + 1) then bm = bm + 2 end
    if matchFn(x - 1, y) then bm = bm + 4 end
    if matchFn(x, y - 1) then bm = bm + 8 end
    return bm
end

function Autotile.buildQuads(tileset, imageW, imageH)
    local quads = {}
    local tw, th = tileset.tile_width, tileset.tile_height
    local terrainMap = tileset.bitmask_map[0][0]
    for bitmask, coords in pairs(terrainMap) do
        local col, row = coords[1], coords[2]
        quads[bitmask] = love.graphics.newQuad(
            col * tw, row * th, tw, th, imageW, imageH
        )
    end
    return quads
end

return Autotile