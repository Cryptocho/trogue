#import bevy_sprite::mesh2d_vertex_output::VertexOutput

@group(2) @binding(0) var frame: texture_2d<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let size = textureDimensions(frame);
    let cell = min(vec2<u32>(in.uv * vec2<f32>(size)), size - 1u);
    // Ignore camera alpha: it is not artwork coverage. Do not re-quantize
    // the river's smooth optical falloff after shading the low-colour inks.
    return vec4(textureLoad(frame, cell, 0).rgb, 1.);
}
