#import bevy_sprite::mesh2d_vertex_output::VertexOutput
#import "gloam/paper/light.wgsl"::{illuminate, air, noise, EYE, LAMP}

// The entire world surface is a Camera2d rectangle. Its UVs are recovered from
// map coordinates so the original 4096² bake cannot be stretched or mirrored.
@group(2) @binding(0) var<uniform> lighting: vec4<f32>;
@group(2) @binding(1) var albedo_map: texture_2d<f32>;
@group(2) @binding(2) var albedo_sampler: sampler;
@group(2) @binding(3) var normal_map: texture_2d<f32>;
@group(2) @binding(4) var normal_sampler: sampler;
@group(2) @binding(5) var orm_map: texture_2d<f32>;
@group(2) @binding(6) var orm_sampler: sampler;
@group(2) @binding(7) var water_map: texture_2d<f32>;
@group(2) @binding(8) var water_sampler: sampler;

fn ripple_normal(xz: vec2<f32>) -> vec3<f32> {
    // Sub-centimetre authored ripple field, aligned along the drainage channels.
    // The derivatives are analytic; no screen-space scene/reflection sampling.
    let a = 35.0*xz.x + 53.0*xz.y;
    let b = -19.0*xz.x + 79.0*xz.y;
    let c = 11.0*xz.x + 28.0*xz.y;
    let gx = 0.019*cos(a) - 0.008*cos(b) + 0.005*cos(c);
    let gz = 0.030*cos(a) + 0.034*cos(b) + 0.013*cos(c);
    return normalize(vec3<f32>(-gx, 1.0, -gz));
}

@fragment
fn fragment(mesh: VertexOutput) -> @location(0) vec4<f32> {
    let xz = vec2<f32>(mesh.world_position.x, -mesh.world_position.y/0.568 - 0.3);
    let uv = (xz - vec2<f32>(-24.0, -38.0)) / vec2<f32>(48.0, 52.0);
    let albedo = textureSample(albedo_map, albedo_sampler, uv).rgb;
    let tangent_normal = textureSample(normal_map, normal_sampler, uv).rgb*2.0-1.0;
    let orm = textureSample(orm_map, orm_sampler, uv).rgb;
    let water = textureSample(water_map, water_sampler, uv).r;
    // Original normal is OpenGL tangent space: T=+X, B=-map-Z, N=+Y.
    let n = normalize(vec3<f32>(tangent_normal.x, tangent_normal.z, -tangent_normal.y));
    let p = vec3<f32>(xz.x, 0.0, xz.y);
    let rough = clamp(orm.g, 0.25, 0.97);
    var color = illuminate(albedo, n, p, rough, 0.045, lighting.y);

    if water > 0.001 {
        let wn = ripple_normal(xz);
        let wp = vec3<f32>(xz.x, -0.013, xz.y);
        // The visible pebbly bed remains the same image as the banks. Only the
        // moist absorption and the water surface BRDF change across the shore.
        let bed = albedo * vec3<f32>(0.52, 0.65, 0.70);
        var wet_color = illuminate(bed, wn, wp, 0.095, 0.72, lighting.y);
        let fresnel = 0.02 + 0.98*pow(1.0-max(dot(wn, EYE), 0.0), 5.0);
        // A broken sky reflected between branches, not a uniformly blue puddle.
        let sky_gap = smoothstep(0.40, 0.79, noise(xz*vec2(1.35, 2.1) + wn.xz*5.2));
        let sky = mix(vec3(0.04, 0.075, 0.11), vec3(0.28, 0.42, 0.58), sky_gap);
        wet_color += sky*(0.082 + 0.54*fresnel);

        // Analytic reflection of the real lamp's luminous centre. Ripple facets
        // split its image into short warm shards. This is NOT SSR or a reflected
        // world render, and it never pretends to reflect actors hidden offscreen.
        let to_lamp = LAMP-wp;
        let half_vector = normalize(normalize(to_lamp)+EYE);
        let glint = pow(max(dot(wn, half_vector), 0.0), 1450.0);
        let variation = noise(xz*vec2(14.0, 33.0));
        let strand = 0.14 + (0.37 + 0.49*variation)*smoothstep(-0.32, 0.65,
            sin(xz.y*164.0 + sin(xz.x*61.0)*1.6 + variation*1.65));
        let lamp_distance = dot(to_lamp, to_lamp);
        wet_color += vec3<f32>(10.5, 6.0, 1.95)*glint*strand/(1.0+lamp_distance*0.19);
        color = mix(color, wet_color, water);
    }

    color = air(color, p, lighting.x);
    return vec4<f32>(color, 1.0);
}
