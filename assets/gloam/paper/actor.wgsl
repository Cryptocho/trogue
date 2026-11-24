#import bevy_sprite::mesh2d_vertex_output::VertexOutput
#import "gloam/paper/light.wgsl"::{illuminate, air}

struct Actor {
    root: vec4<f32>,
    anchor: vec4<f32>,
    surface: vec4<f32>,
    options: vec4<f32>,
};

@group(2) @binding(0) var<uniform> actor: Actor;
@group(2) @binding(1) var color_map: texture_2d<f32>;
@group(2) @binding(2) var color_sampler: sampler;
@group(2) @binding(3) var normal_map: texture_2d<f32>;
@group(2) @binding(4) var normal_sampler: sampler;
@group(2) @binding(5) var emission_map: texture_2d<f32>;
@group(2) @binding(6) var emission_sampler: sampler;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    // Mirror sampling and the normal's X axis together. Mirroring the image
    // alone would leave highlights on the wrong side of every mirrored rock.
    let uv = vec2(mix(1. - in.uv.x, in.uv.x, step(0., actor.anchor.z)), in.uv.y);
    let texel = textureSample(color_map, color_sampler, uv);
    if texel.a < 0.5 {
        discard;
    }
    let relief = textureSampleLevel(normal_map, normal_sampler, uv, 0.).xyz * 2. - 1.;
    let art_normal = normalize(mix(relief, vec3(0., 0., 1.), actor.options.y));
    // Art +Y is picture-up, art +Z faces a viewer 35 degrees above the map.
    // The vectors are orthonormal and use map +Z toward the screen foreground.
    let n = normalize(vec3(
        art_normal.x * actor.anchor.z,
        art_normal.y * 0.819152 + art_normal.z * 0.573576,
        -art_normal.y * 0.573576 + art_normal.z * 0.819152,
    ));
    let rise = (actor.anchor.y - uv.y) * actor.root.w;
    let p = vec3(
        actor.root.x + (uv.x - actor.anchor.x) * actor.root.z * actor.anchor.z,
        max(0., rise * cos(actor.surface.w) - actor.anchor.w),
        actor.root.y - rise * sin(actor.surface.w),
    );
    // Damp exposed stone is less rough than its moss. No baked directional
    // self-shadow multiplier: the shared lights and per-pixel normals do it.
    let moss = smoothstep(0.24, 0.65, (texel.g - texel.b) / max(0.001, texel.g));
    let roughness = mix(actor.surface.x, 0.94, moss);
    var color = illuminate(
        texel.rgb, n, p, roughness, actor.surface.y, actor.options.z,
    );
    if actor.surface.z > 0.5 {
        let glass = textureSampleLevel(emission_map, emission_sampler, uv, 0.).r;
        color += vec3(4.8, 2.65, 0.85) * glass;
    }
    return vec4(air(color, p, actor.options.x), 1.);
}
