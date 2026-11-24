#import bevy_sprite::mesh2d_vertex_output::VertexOutput
@group(2) @binding(0) var<uniform> scatter: vec4<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let v = 2.*in.uv-1.;
    let r2 = dot(v,v);
    if r2 >= 1. { discard; }
    let halo = exp(-3.8*r2) * (1.-smoothstep(0.62,1.,r2));
    return vec4(scatter.rgb,scatter.a*halo);
}
