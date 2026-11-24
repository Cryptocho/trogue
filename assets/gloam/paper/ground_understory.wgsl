#import bevy_sprite::mesh2d_vertex_output::VertexOutput
#import "gloam/paper/light.wgsl"::{illuminate, air}

@group(2) @binding(0) var<uniform> lighting: vec4<f32>;
@group(2) @binding(1) var<uniform> atlas: vec4<f32>;
@group(2) @binding(2) var albedo_map: texture_2d<f32>;
@group(2) @binding(3) var albedo_sampler: sampler;
@group(2) @binding(4) var data_map: texture_2d<f32>;
@group(2) @binding(5) var data_sampler: sampler;

@fragment
fn fragment(mesh: VertexOutput) -> @location(0) vec4<f32> {
    let uv = atlas.xy + mesh.uv*atlas.z;
    let leaf = textureSample(albedo_map, albedo_sampler, uv);
    let data = textureSample(data_map, data_sampler, uv);
    if leaf.a < 0.5 {
        discard;
    }
    // Native 32px plants are placed at scale 1; data A is authored light height.
    let height = data.a*atlas.w;
    let p = vec3<f32>(mesh.world_position.x, height,
        -(mesh.world_position.y-height*0.823)/0.568-0.3);
    let painted_normal = normalize(data.rgb*2.0-1.0);
    let n = normalize(mix(painted_normal, vec3<f32>(0.0, 1.0, 0.0), lighting.z));
    // The single painted root fold is intrinsic material occlusion. The clear
    // silhouette and 1px leaf steps remain intact in the 512px camera canvas.
    let root_ao = select(1.0, 0.86, height < 0.08);
    let color = illuminate(leaf.rgb*root_ao, n, p, 0.80, 0.035, lighting.y);
    return vec4<f32>(air(color, p, lighting.x), leaf.a);
}
