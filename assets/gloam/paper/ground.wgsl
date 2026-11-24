#import bevy_sprite::mesh2d_vertex_output::VertexOutput
#import "gloam/paper/light.wgsl"::{illuminate, air, LAMP}

// A 1200×738 native-pixel painting, not the HD-2D terrain bake. All colour,
// planar normals, material data and water shards use nearest sampling.
@group(2) @binding(0) var<uniform> lighting: vec4<f32>;
@group(2) @binding(1) var albedo_map: texture_2d<f32>;
@group(2) @binding(2) var albedo_sampler: sampler;
@group(2) @binding(3) var normal_map: texture_2d<f32>;
@group(2) @binding(4) var normal_sampler: sampler;
@group(2) @binding(5) var orm_map: texture_2d<f32>;
@group(2) @binding(6) var orm_sampler: sampler;
@group(2) @binding(7) var water_map: texture_2d<f32>;
@group(2) @binding(8) var water_sampler: sampler;

// The wetland data contains only discrete painted pixels: R is water coverage,
// G the broken sky reflection, B the few lamp-reflection shards. There is no
// continuous ripple field, elapsed-time input, or procedural texture noise.

@fragment
fn fragment(mesh: VertexOutput) -> @location(0) vec4<f32> {
    let xz = vec2<f32>(mesh.world_position.x, -mesh.world_position.y/0.568 - 0.3);
    let uv = (xz - vec2<f32>(-24.0, -38.0)) / vec2<f32>(48.0, 52.0);
    let albedo = textureSample(albedo_map, albedo_sampler, uv).rgb;
    let painted_normal = textureSample(normal_map, normal_sampler, uv).rgb*2.0-1.0;
    let orm = textureSample(orm_map, orm_sampler, uv).rgb;
    let water = textureSample(water_map, water_sampler, uv).rgb;
    // The authoring brush stores world XYZ normals directly, in linear RGB.
    let n = normalize(painted_normal);
    let p = vec3<f32>(xz.x, 0.0, xz.y);
    let rough = clamp(orm.g, 0.55, 0.97);
    let specular = mix(0.025, 0.075, water.r);
    var color = illuminate(albedo, n, p, rough, specular, lighting.y);
    if water.r > 0.5 {
        color += water.g * vec3<f32>(0.012, 0.022, 0.027);
        let to_lamp = LAMP-p;
        // Warm glints remain separated horizontal brushstrokes, never a white
        // ellipse. Their strength still falls away from the actual world lamp.
        color += water.b * vec3<f32>(1.4, 0.69, 0.19)
            / (1.0 + dot(to_lamp, to_lamp)*0.20);
    }

    color = air(color, p, lighting.x);
    return vec4<f32>(color, 1.0);
}
