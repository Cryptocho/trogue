// A sprite is a silhouette in the depth buffer but not a flat sheet in the
// lighting buffer. Keep Bevy's depth, alpha test, PBR lights and shadow passes.
#import bevy_pbr::{
    prepass_io::{VertexOutput, FragmentOutput},
    pbr_functions::alpha_discard,
    pbr_fragment::pbr_input_from_standard_material,
    pbr_deferred_functions::deferred_output,
}

@group(2) @binding(100) var relief: texture_2d<f32>;
@group(2) @binding(101) var nearest_sampler: sampler;
// x = bare roughness, yz = sin/cos of art-to-card pitch, w = relief on/off.
@group(2) @binding(102) var<uniform> surface: vec4<f32>;

@fragment
fn fragment(in: VertexOutput, @builtin(front_facing) front: bool) -> FragmentOutput {
    var pbr = pbr_input_from_standard_material(in, front);
    pbr.material.base_color = alpha_discard(pbr.material, pbr.material.base_color);
    let sample = mix(vec3(0., 0., 1.), textureSampleLevel(relief, nearest_sampler, in.uv, 0.).xyz * 2. - 1., surface.w);
    let n = normalize(in.world_normal);
    let t = normalize(in.world_tangent.xyz);
    let b = in.world_tangent.w * cross(n, t);
    // PixelLab normals are +Y up, relative to the drawn three-quarter view.
    // The geometry leans less (so feet and cast shadows stay grounded).
    let art_y = surface.z * b - surface.y * n;
    let art_z = surface.y * b + surface.z * n;
    pbr.N = normalize(t * sample.x + art_y * sample.y + art_z * sample.z);
    let color = pbr.material.base_color.rgb;
    let moss = smoothstep(0.24, 0.65, (color.g - color.b) / max(0.001, color.g));
    pbr.material.perceptual_roughness = mix(surface.x, pbr.material.perceptual_roughness, moss);
    var out = deferred_output(in, pbr);
#ifdef NORMAL_PREPASS
    out.normal = vec4(pbr.N * 0.5 + 0.5, 1.);
#endif
    return out;
}
