#import bevy_sprite::mesh2d_vertex_output::VertexOutput

@group(2) @binding(0) var frame: texture_2d<f32>;
@group(2) @binding(1) var colors: texture_2d<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let size = textureDimensions(frame);
    let cell = min(vec2<u32>(in.uv * vec2<f32>(size)), size - 1u);
    let linear = textureLoad(frame, cell, 0).rgb;
    // palette.py encodes the same sqrt-linear 64³ grid, blue slices across X.
    let i = vec3<u32>(round(sqrt(saturate(linear)) * 63.));
    return vec4(textureLoad(colors, vec2(i.x + 64u * i.z, i.y), 0).rgb, 1.);
}
