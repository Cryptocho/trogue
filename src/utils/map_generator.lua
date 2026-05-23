local Perlin = require("src.utils.perlin")
local FBM = require("src.utils.fbm")
local Poisson = require("src.utils.poisson_disk")

--- Generate a procedural map based on type.
-- @param type string: Map type identifier ("forest" supported).
-- @param width number: Map width in tiles.
-- @param height number: Map height in tiles.
-- @param options table: Type-specific parameters.
--
-- Forest options:
--   - treeMinDist (number): Minimum distance between trees (default 2.0).
--   - densityThreshold (number): Minimum FBM density to place tree (default 0.3).
--   - fbmOctaves (number): FBM octave count (default 6).
--   - fbmPersistence (number): FBM persistence/lacunarity (default 0.5).
--   - fbmScale (number): FBM frequency scale (default 4.0).
--   - poissonMaxAttempts (number): Poisson sampling max attempts (default 30).
--   - poissonSeed (number): Random seed for Poisson sampling (default nil/os.time).
--
-- @return table: 2D array of characters (e.g., "." for floor, "^" for tree).
local function generateMap(type, width, height, options)
    if type == "forest" then
        local perlinNoise = Perlin.new(0.5, 2.0, false)
        local minDist = (options and options.treeMinDist) or 2.0
        local densityThreshold = (options and options.densityThreshold) or 0.3
        local fbmOctaves = (options and options.fbmOctaves) or 6
        local fbmPersistence = (options and options.fbmPersistence) or 0.5
        local fbmScale = (options and options.fbmScale) or 4.0
        local poissonMaxAttempts = (options and options.poissonMaxAttempts) or 30
        local poissonSeed = (options and options.poissonSeed)

        local densityMap = {}
        for y = 1, height do
            densityMap[y] = {}
            for x = 1, width do
                local nx = x / width
                local ny = y / height
                local value = FBM.fbm(perlinNoise, nx * fbmScale, ny * fbmScale, fbmOctaves, fbmPersistence)
                densityMap[y][x] = (value + 1) / 2
            end
        end

        local treePoints = Poisson.sampleWithDensity(
            densityMap, width, height,
            minDist,
            poissonMaxAttempts,
            densityThreshold,
            poissonSeed
        )

        local mapData = {}
        for y = 1, height do
            mapData[y] = string.rep(".", width)
        end

        for _, point in ipairs(treePoints) do
            local tx = math.floor(point.x)
            local ty = math.floor(point.y)
            if tx >= 1 and tx <= width and ty >= 1 and ty <= height then
                mapData[ty] = mapData[ty]:sub(1, tx - 1) .. "^" .. mapData[ty]:sub(tx + 1)
            end
        end

        return mapData
    end

    return "Not Implemented"
end

return { generateMap = generateMap }