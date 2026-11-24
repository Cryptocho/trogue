// Linear HDR lighting for both 2D terrain and cutouts. Coordinates are shared
// map metres, not screen pixels. Height bends the light vector, not the quad.
const MOON: vec3<f32> = vec3(-0.60971, 0.73165, -0.30486);
const EYE: vec3<f32> = vec3(0.0, 0.568, 0.823);
const LAMP: vec3<f32> = vec3(1.5, 1.48, 0.83);

fn hash(p: vec2<f32>) -> f32 {
    let q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    let v = q + dot(q, q.yzx + 33.33);
    return fract((v.x + v.y) * v.z);
}

fn noise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0)), u.x), u.y);
}

fn lobe(n: vec3<f32>, l: vec3<f32>, rough: f32) -> f32 {
    let half_dir = normalize(l + EYE);
    return pow(max(dot(n, half_dir), 0.0), mix(82.0, 7.0, rough * rough));
}

// Flatten a curved normal into a few lit planes without flattening the stone:
// opposite facets still move onto different steps when the key changes sides.
fn planes(v: f32) -> f32 {
    return floor(saturate(v) * 8.0 + 0.5) * 0.125;
}

fn illuminate(albedo: vec3<f32>, n: vec3<f32>, p: vec3<f32>, rough: f32, specular: f32, key: f32) -> vec3<f32> {
    // Stationary pools through the branches, not a vignette tied to the lens.
    let canopy_uv = p.xz - p.y * MOON.xz / MOON.y;
    let leaf = noise(canopy_uv * 0.39) * 0.77 + noise(canopy_uv * 0.96 + vec2(3.7)) * 0.23;
    let opening = (p.xz - vec2(-2.6, -2.2) - p.y * MOON.xz / MOON.y) / vec2(2.75, 3.75);
    let break_light = exp(-dot(opening, opening));
    // The darks lean into indigo; an opening in the canopy brings the left
    // side of the waystone up into a different blue-green ink ramp.
    let sky = vec3(0.146, 0.205, 0.40) * (0.86 + 0.14 * n.y);
    let moon = vec3(0.31, 0.47, 0.76) * (0.28 + 0.61 * smoothstep(0.20, 0.78, leaf))
             + vec3(0.53, 0.73, 1.02) * break_light;
    let dl = LAMP - p;
    let d2 = dot(dl, dl);
    let l = dl * inverseSqrt(max(d2, 0.002));
    let spread = (dl.xz + vec2(0.16, 0.38)) / vec2(0.90, 1.4);
    let below = 1.0 - smoothstep(0.24, 1.40, p.y);
    let hood = below * exp(-dot(spread, spread)) * 1.26;
    let falloff = (10.4 / (d2 + 0.88)) * (1.0 - smoothstep(9.0, 43.0, d2));
    let amber = vec3(1.0, 0.615, 0.275) * (falloff + hood);
    let gloss = clamp(specular, 0.0, 1.0) * (0.018 + 0.27 * (1.0 - rough) * (1.0 - rough));
    let moon_face = max(dot(n, MOON), 0.0);
    let lamp_face = max(dot(n, l), 0.0);
    // Stone and upright cloth retain defined planes. Flat trail and water
    // don't get a second set of contour rings from the Lambert steps.
    let level = smoothstep(0.86, 0.97, n.y);
    var color = albedo * (sky + moon * mix(planes(moon_face), moon_face, level)
                             + amber * mix(planes(lamp_face), lamp_face, level));
    color += gloss * (moon * lobe(n, MOON, rough) + amber * lobe(n, l, rough));
    if abs(key) > 0.5 {
        let stone_center = vec3(-3.15, 0.90, -1.30);
        let light_pos = stone_center + vec3(key * 2.4, 1.40, 1.45);
        let ray = light_pos - p;
        let k2 = dot(ray, ray);
        let k = ray * inverseSqrt(max(0.002, k2));
        let cone = smoothstep(0.80, 0.93, dot(k, normalize(light_pos - stone_center)));
        color += (albedo * planes(dot(n, k)) + gloss * lobe(n, k, rough)) * (23.0 * cone / (k2 + 0.75));
    }
    return color;
}

fn air(color: vec3<f32>, p: vec3<f32>, fog: f32) -> vec3<f32> {
    if fog < 0.5 { return color; }
    // A blue rear bank and broken low streamers, with soft edges sampled
    // on the native pixel grid. The near silhouettes and light pool stay clear.
    let bank = smoothstep(3.0, 14.0, -p.z);
    let wisp = noise(p.xz * vec2(0.24, 0.54) + vec2(6.1, 3.7));
    let drift = p.z + 5.9 + 0.16 * p.x + 2.9 * (wisp - 0.5);
    let shelf = exp(-abs(drift) * 0.93);
    let ankle = exp(-max(p.y - 0.18, 0.0) * 1.46);
    let density = bank * (0.25 + 0.16 * wisp) + (0.067 + 0.085 * wisp) * shelf * ankle;
    let mist = mix(vec3(0.044, 0.071, 0.135), vec3(0.084, 0.129, 0.209), wisp);
    return mix(color, mist, min(density, 0.54));
}
