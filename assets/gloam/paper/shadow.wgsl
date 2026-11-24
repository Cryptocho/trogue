#import bevy_sprite::mesh2d_vertex_output::VertexOutput

struct Shadow {
    root: vec4<f32>,
    anchor: vec4<f32>,
    projection: vec4<f32>,
    contact: vec4<f32>,
    options: vec4<f32>,
};

@group(2) @binding(0) var<uniform> shadow: Shadow;
@group(2) @binding(1) var silhouette: texture_2d<f32>;
@group(2) @binding(2) var silhouette_sampler: sampler;

fn coverage(uv: vec2<f32>) -> f32 {
    if any(uv < vec2(0.)) || any(uv > vec2(1.)) {
        return 0.;
    }
    return textureSampleLevel(silhouette, silhouette_sampler, uv, 0.).a;
}

fn soft_silhouette(uv: vec2<f32>, radius: vec2<f32>) -> f32 {
    // The color sampler stays nearest for the pixel art. Explicit alpha taps
    // soften only its cast shadow, with a penumbra that grows off the feet.
    var alpha = coverage(uv) * 0.20;
    alpha += coverage(uv + radius * vec2(1., 0.)) * 0.10;
    alpha += coverage(uv + radius * vec2(-1., 0.)) * 0.10;
    alpha += coverage(uv + radius * vec2(0., 1.)) * 0.10;
    alpha += coverage(uv + radius * vec2(0., -1.)) * 0.10;
    alpha += coverage(uv + radius * vec2(0.7071, 0.7071)) * 0.10;
    alpha += coverage(uv + radius * vec2(-0.7071, 0.7071)) * 0.10;
    alpha += coverage(uv + radius * vec2(0.7071, -0.7071)) * 0.10;
    alpha += coverage(uv + radius * vec2(-0.7071, -0.7071)) * 0.10;
    return alpha;
}

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    if shadow.options.x < 0.5 {
        discard;
    }
    // This is the inverse of paper::on_map, not a screen-space vignette.
    let ground = vec2(in.world_position.x, -in.world_position.y / 0.568 - 0.3);
    var occlusion = 0.;
    if shadow.anchor.w < 0.5 {
        let q = (ground - shadow.contact.xy) / shadow.contact.zw;
        occlusion = pow(max(0., 1. - dot(q, q)), 1.8);
    } else {
        let direction = shadow.projection.xy;
        let across = vec2(direction.y, -direction.x);
        let delta = ground - shadow.root.xy;
        let distance_along = dot(delta, direction);
        let distance_across = dot(delta, across);
        if distance_along < -0.05 || distance_along > shadow.projection.w {
            discard;
        }
        let t = max(0., distance_along);
        var height = t / 0.931695;
        var width_scale = 1.;
        if shadow.anchor.w > 1.5 {
            // Exact point-light projection on the receiving plane:
            // length = distance*h/(H-h), width scale = H/(H-h).
            let distance_to_light = shadow.projection.z;
            height = 1.48 * t / (distance_to_light + t);
            width_scale = 1. + t / distance_to_light;
        }
        let uv = vec2(
            shadow.anchor.x + distance_across * shadow.anchor.z / (shadow.root.z * width_scale),
            shadow.anchor.y - height / shadow.root.w,
        );
        let penumbra = select(0.025 + height * 0.018, 0.02 + t * 0.065, shadow.anchor.w > 1.5);
        let radius = vec2(
            penumbra / (shadow.root.z * width_scale),
            penumbra / shadow.root.w,
        );
        let finite_edge = 1. - smoothstep(shadow.projection.w * 0.85, shadow.projection.w, t);
        occlusion = soft_silhouette(uv, radius) * finite_edge;
    }
    // Fog attenuates visibility of an existing shadow; it never supplies the
    // shadow shape. The footprint and every projected edge have finite bounds.
    let far_fade = 1. - shadow.options.y * 0.64 * smoothstep(6., 27., -ground.y);
    let alpha = occlusion * shadow.options.z * far_fade;
    if alpha < 0.002 {
        discard;
    }
    return vec4(0., 0., 0., alpha);
}
