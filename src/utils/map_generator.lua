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
--   - enemySpawnMinDist (number): Minimum distance from center for enemy spawn (default 8).
--   - enemyDensity (number): Enemy density per tile, ~enemyCount = width*height*density (default 0.008).
--
-- @return table: 2D array of characters (e.g., "." for floor, "^" for tree).
-- @return table: Array of enemy spawns {x, y, type}, or nil if no enemies placed.
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

        local enemySpawnMinDist = (options and options.enemySpawnMinDist) or 8
        local enemyDensity = (options and options.enemyDensity) or 0.008
        local centerX = math.floor(width / 2)
        local centerY = math.floor(height / 2)

        local floorTiles = {}
        for y = 1, height do
            for x = 1, width do
                if mapData[y]:sub(x, x) == "." then
                    local dist = math.abs(x - centerX) + math.abs(y - centerY)
                    if dist >= enemySpawnMinDist then
                        floorTiles[#floorTiles + 1] = {x = x, y = y}
                    end
                end
            end
        end

        local enemyCount = math.floor(width * height * enemyDensity)
        enemyCount = math.min(enemyCount, #floorTiles)

        local enemySpawns = nil
        if enemyCount > 0 then
            for i = #floorTiles, 2, -1 do
                local j = math.random(i)
                floorTiles[i], floorTiles[j] = floorTiles[j], floorTiles[i]
            end

            local enemyTypes = {"goblin", "rat", "orc"}
            local enemyWeights = {5, 6, 3}
            local totalWeight = 14

            enemySpawns = {}
            for i = 1, enemyCount do
                local tile = floorTiles[i]
                local r = math.random(totalWeight)
                local cumulative = 0
                local enemyType
                for j, w in ipairs(enemyWeights) do
                    cumulative = cumulative + w
                    if r <= cumulative then
                        enemyType = enemyTypes[j]
                        break
                    end
                end
                enemySpawns[#enemySpawns + 1] = {x = tile.x, y = tile.y, type = enemyType}
            end
        end

        return mapData, enemySpawns
    end

    return "Not Implemented"
end

return { generateMap = generateMap }