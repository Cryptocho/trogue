#import bevy_sprite::mesh2d_vertex_output::VertexOutput
#import "gloam/paper/light.wgsl"::map_coord

@group(2) @binding(0) var<uniform> support: vec4<f32>;
@group(2) @binding(1) var field: texture_2d<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let d = in.uv*2.-1.;
    let d2 = dot(d,d);
    if d2 >= 1. { discard; }
    if textureLoad(field,map_coord(in.world_position.xy,textureDimensions(field)),0).r > 0.5 { discard; }
    let at_foot = (in.uv-support.xy)*2./support.z;
    let core = 1.-smoothstep(0.15,1.,dot(at_foot,at_foot));
    // A short hard ellipse grounds each trunk as in the style reference.
    return vec4(0.002,0.003,0.004,(0.56+0.25*core)*(1.-smoothstep(0.90,1.,d2)));
}
