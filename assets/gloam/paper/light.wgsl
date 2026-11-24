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
    return floor(saturate(v) * 5.0 + 0.5) * 0.2;
}

fn illuminate(albedo: vec3<f32>, n: vec3<f32>, p: vec3<f32>, rough: f32, specular: f32, key: f32) -> vec3<f32> {
    // Stationary pools through the branches, not a vignette tied to the lens.
    let canopy_uv = p.xz - p.y * MOON.xz / MOON.y;
    let leaf = noise(canopy_uv * 0.39) * 0.77 + noise(canopy_uv * 0.96 + vec2(3.7)) * 0.23;
    let opening = (p.xz - vec2(-2.6, -2.2) - p.y * MOON.xz / MOON.y) / vec2(2.75, 3.75);
    let break_light = exp(-dot(opening, opening));
    // The darks lean into indigo; an opening in the canopy brings the left
    // side of the waystone up into a different blue-green ink ramp.
    let sky = vec3(0.15, 0.24, 0.45) * (0.86 + 0.14 * n.y);
    let moon = vec3(0.26, 0.48, 0.82) * (0.29 + 0.60 * smoothstep(0.24, 0.76, leaf))
             + vec3(0.46, 0.71, 1.02) * break_light;
    let dl = LAMP - p;
    let d2 = dot(dl, dl);
    let l = dl * inverseSqrt(max(d2, 0.002));
    let amber = vec3(1.0, 0.57, 0.22) * (10.8 / (d2 + 0.78)) * (1.0 - smoothstep(10.0, 44.0, d2));
    let gloss = clamp(specular, 0.0, 1.0) * (0.018 + 0.27 * (1.0 - rough) * (1.0 - rough));
    var color = albedo * (sky + moon * planes(dot(n, MOON)) + amber * planes(dot(n, l)));
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
    // A blue rear tree bank and broken low streamers. The near silhouette
    // stays clear and the lantern does not paint a white veil across the path.
    let bank = smoothstep(1.4, 12.0, -p.z);
    let wisp = noise(p.xz * vec2(0.27, 0.74) + vec2(6.1, 3.7));
    let shelf = exp(-abs(p.z + 4.8 + 0.23 * p.x + 1.7 * (wisp - 0.5)) * 0.80);
    let ankle = exp(-max(p.y - 0.18, 0.0) * 1.45);
    let density = bank * (0.28 + 0.16 * wisp) + (0.12 + 0.11 * wisp) * shelf * ankle;
    let mist = mix(vec3(0.062, 0.114, 0.181), vec3(0.110, 0.176, 0.233), wisp);
    return mix(color, mist, min(density, 0.55));
}
