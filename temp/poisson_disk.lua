local floor = math.floor

local function new(minDist, maxDist, seed)
    return {
        minDist = minDist or 3.0,
        maxDist = maxDist or minDist * 2,
        cellSize = (minDist or 3.0) / math.sqrt(2),
        seed = seed or os.time(),
        points = {},
        grid = {},
        width = 0,
        height = 0
    }
end

local function rng(s)
    s.seed = (s.seed * 1103515245 + 12345) % 2147483648
    return s.seed / 2147483648
end

local function randomInAnnulus(s, px, py, minD, maxD)
    local angle = rng(s) * 2 * math.pi
    local radius = math.sqrt(minD * minD + rng(s) * (maxD * maxD - minD * minD))
    return px + radius * math.cos(angle), py + radius * math.sin(angle)
end

local function getGridCell(s, x, y)
    return floor(x / s.cellSize), floor(y / s.cellSize)
end

local function gridKey(gx, gy)
    return gx .. "," .. gy
end

local function isInBounds(s, x, y)
    return x >= 1 and x <= s.width and y >= 1 and y <= s.height
end

local function isValidPoint(s, px, py, minDist)
    if not isInBounds(s, px, py) then return false end

    local gx, gy = getGridCell(s, px, py)
    local cellRange = floor(minDist / s.cellSize) + 1

    for dx = -cellRange, cellRange do
        for dy = -cellRange, cellRange do
            local key = gridKey(gx + dx, gy + dy)
            local cellPoints = s.grid[key]
            if cellPoints then
                for _, p in ipairs(cellPoints) do
                    local dx = px - p.x
                    local dy = py - p.y
                    if dx * dx + dy * dy < minDist * minDist then
                        return false
                    end
                end
            end
        end
    end
    return true
end

local function addPoint(s, px, py)
    local gx, gy = getGridCell(s, px, py)
    local key = gridKey(gx, gy)
    if not s.grid[key] then s.grid[key] = {} end
    table.insert(s.grid[key], {x = px, y = py})
    table.insert(s.points, {x = px, y = py})
    return {x = px, y = py}
end

local function sampleRegion(s, width, height, maxAttempts)
    s.points = {}
    s.grid = {}
    s.width = width
    s.height = height

    local firstX = floor(rng(s) * width) + 1
    local firstY = floor(rng(s) * height) + 1
    addPoint(s, firstX, firstY)

    local active = {{x = firstX, y = firstY}}

    while #active > 0 do
        local idx = floor(rng(s) * #active) + 1
        local point = active[idx]
        local found = false

        for _ = 1, maxAttempts do
            local newX, newY = randomInAnnulus(s, point.x, point.y, s.minDist, s.maxDist)
            if isValidPoint(s, newX, newY, s.minDist) then
                table.insert(active, addPoint(s, newX, newY))
                found = true
                break
            end
        end

        if not found then table.remove(active, idx) end
    end

    return s.points
end

local function sampleWithDensity(s, densityMap, width, height, minDist, maxAttempts, threshold)
    s.points = {}
    s.grid = {}
    s.width = width
    s.height = height
    s.minDist = minDist or s.minDist
    s.cellSize = s.minDist / math.sqrt(2)
    threshold = threshold or 0.0

    local function getDensity(tx, ty)
        if tx < 1 or tx > width or ty < 1 or ty > height then return 0 end
        local row = densityMap[ty]
        return row and (row[tx] or 0) or 0
    end

    local candidates = {}
    for ty = 1, height do
        for tx = 1, width do
            local d = getDensity(tx, ty)
            if d > threshold then table.insert(candidates, {x = tx, y = ty, density = d}) end
        end
    end

    if #candidates == 0 then return s.points end

    local totalWeight = 0
    for _, c in ipairs(candidates) do totalWeight = totalWeight + c.density end

    local firstX, firstY
    local r = rng(s) * totalWeight
    local cumsum = 0
    for _, c in ipairs(candidates) do
        cumsum = cumsum + c.density
        if r <= cumsum then firstX, firstY = c.x, c.y; break end
    end
    if not firstX then firstX, firstY = candidates[#candidates].x, candidates[#candidates].y end

    addPoint(s, firstX, firstY)
    local active = {{x = firstX, y = firstY}}

    while #active > 0 do
        local idx = floor(rng(s) * #active) + 1
        local point = active[idx]
        local found = false

        for _ = 1, maxAttempts do
            local newX, newY = randomInAnnulus(s, point.x, point.y, s.minDist, s.maxDist)
            if isValidPoint(s, newX, newY, s.minDist) then
                local d = getDensity(newX, newY)
                if d > threshold and rng(s) < d then
                    table.insert(active, addPoint(s, newX, newY))
                    found = true
                    break
                end
            end
        end

        if not found then table.remove(active, idx) end
    end

    return s.points
end

return {
    new = new,
    sampleRegion = function(width, height, minDist, maxAttempts, seed)
        local s = new(minDist, minDist * 2, seed or os.time())
        return sampleRegion(s, width, height, maxAttempts or 30)
    end,
    sampleWithDensity = function(densityMap, width, height, minDist, maxAttempts, threshold, seed)
        local s = new(minDist, minDist * 2, seed or os.time())
        return sampleWithDensity(s, densityMap, width, height, minDist, maxAttempts or 30, threshold)
    end
}