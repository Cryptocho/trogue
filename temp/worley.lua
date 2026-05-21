local floor = math.floor
local sqrt = math.sqrt
local max32 = 1073741824

local function band(a, b)
    local result = 0
    local mul = 1
    for i = 0, 31 do
        if (a % 2 == 1) and (b % 2 == 1) then result = result + mul end
        a = floor(a / 2)
        b = floor(b / 2)
        mul = mul * 2
        if a == 0 and b == 0 then break end
    end
    return result
end

local function bxor(a, b)
    local result = 0
    local mul = 1
    for i = 0, 31 do
        local ab = a % 2
        local bb = b % 2
        if ab ~= bb then result = result + mul end
        a = floor(a / 2)
        b = floor(b / 2)
        mul = mul * 2
        if a == 0 and b == 0 then break end
    end
    return result
end

local function rshift(a, b)
    return floor(a / (2 ^ b))
end

local function new(seed)
    return { seed = seed or 12345 }
end

local function hash(x, y, s)
    local h = (x * 1619 + y * 31337 + s * 7919) % 1073741824
    h = bxor(h, rshift(h, 16)) * 2747636419
    h = bxor(h, rshift(h, 16))
    return h
end

local function noise2D(s, x, y)
    local cellX = floor(x)
    local cellY = floor(y)
    local minDist = 1e10

    for dx = -1, 1 do
        for dy = -1, 1 do
            local cx = cellX + dx
            local cy = cellY + dy
            local h1 = hash(cx, cy, s.seed)
            local h2 = hash(cx, cy, s.seed)
            local px = cx + band(h1, 0x3FFFFFFF) / max32
            local py = cy + band(h2, 0x3FFFFFFF) / max32
            local dist = sqrt((x - px) ^ 2 + (y - py) ^ 2)
            if dist < minDist then minDist = dist end
        end
    end

    return minDist / sqrt(0.5)
end

local function noise2D_F2_F1(s, x, y)
    local cellX = floor(x)
    local cellY = floor(y)
    local dists = {}

    for dx = -1, 1 do
        for dy = -1, 1 do
            local cx = cellX + dx
            local cy = cellY + dy
            local h1 = hash(cx, cy, s.seed)
            local h2 = hash(cx, cy, s.seed)
            local px = cx + band(h1, 0x3FFFFFFF) / max32
            local py = cy + band(h2, 0x3FFFFFFF) / max32
            table.insert(dists, sqrt((x - px) ^ 2 + (y - py) ^ 2))
        end
    end

    table.sort(dists)
    return (dists[2] - dists[1]) / sqrt(0.5)
end

return { new = new, noise2D = noise2D, noise2D_F2_F1 = noise2D_F2_F1 }