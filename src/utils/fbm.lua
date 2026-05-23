local perlin = require("src.utils.perlin")

local function clamp(v, mn, mx)
    return math.min(math.max(v, mn), mx)
end

local function fbm(noise, x, y, octaves, persistence)
    local p = persistence or 0.5
    local int_octaves = math.floor(octaves)
    local value = 0
    local amplitude = 1.0
    local total_amp = 0.0

    for i = 1, int_octaves do
        value = value + perlin.perlin(noise, x, y) * amplitude
        total_amp = total_amp + amplitude
        amplitude = amplitude * p
        x = x * noise.lacunarity
        y = y * noise.lacunarity
    end

    local remainder = octaves - int_octaves
    if remainder > 0.0001 then
        value = value + (remainder * perlin.perlin(noise, x, y)) * amplitude
        total_amp = total_amp + remainder * amplitude
    end

    if total_amp > 0 then value = value / total_amp end
    return clamp(value, -0.99999, 0.99999)
end

return { fbm = fbm }