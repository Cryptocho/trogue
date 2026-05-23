local floor = math.floor
local cubic = function(r) return 3 * r * r - 2 * r * r * r end
local lerp = function(a, b, t) return a + (b - a) * t end
local clamp = function(v, mn, mx) return math.min(math.max(v, mn), mx) end

local default_permutation = {
    151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225,
    140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148,
    247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
    57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175,
    74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122,
    60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54,
    65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
    200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64,
    52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212,
    207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213,
    119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
    129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104,
    218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241,
    81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157,
    184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93,
    222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
}

local function new(hurst, lacunarity, fractal)
    local perm = {}
    for i = 1, 512 do
        perm[i] = default_permutation[((i - 1) % 256) + 1]
    end

    return {
        perm = perm,
        hurst = hurst or 0.5,
        lacunarity = lacunarity or 2.0,
        fractal = fractal or false
    }
end

local GRAD2 = {
    { 1, 1}, {-1, 1}, { 1,-1}, {-1,-1},
    { 1, 0}, {-1, 0}, { 0, 1}, { 0,-1}
}

local function grad2(hash, x, y)
    local idx = (hash % 8) + 1
    local g = GRAD2[idx]
    return g[1] * x + g[2] * y
end

local function mod256(n)
    return ((n % 256) + 256) % 256
end

local function noise2d(data, x, y)
    local n0 = floor(x)
    local r0 = x - n0
    local w0 = cubic(r0)

    local n1 = floor(y)
    local r1 = y - n1
    local w1 = cubic(r1)

    local function g(ix, iy)
        local idx = data.perm[mod256(ix) + 1]
        idx = data.perm[(idx + mod256(iy)) % 256 + 1]
        local dx = r0 - (ix - n0)
        local dy = r1 - (iy - n1)
        return grad2(idx, dx, dy)
    end

    return lerp(
        lerp(g(n0, n1),     g(n0 + 1, n1),     w0),
        lerp(g(n0, n1 + 1), g(n0 + 1, n1 + 1), w0),
        w1
    )
end

local function perlin(data, x, y)
    local value = noise2d(data, x, y)

    if data.fractal then
        local result = 0
        local amp = 1
        local freq = 1
        local total_amp = 0

        for i = 1, 5 do
            result = result + noise2d(data, x * freq, y * freq) * amp
            total_amp = total_amp + amp
            amp = amp * data.lacunarity ^ (-i * data.hurst)
            freq = freq * 2
        end

        value = result / total_amp
    end

    return (clamp(value, -1, 1) + 1) * 0.5
end

return { new = new, perlin = perlin }
