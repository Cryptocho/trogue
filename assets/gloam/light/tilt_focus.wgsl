// 世界焦面景深 / deterministic, single-pass, depth-aware HDR gather.
#import bevy_core_pipeline::fullscreen_vertex_shader::FullscreenVertexOutput

struct TiltFocusUniform {
    // World-space focal plane transformed into view space on the CPU.
    plane: vec4<f32>,
    // inverse projection X/Y scale, off-axis projection X/Y terms
    projection: vec4<f32>,
    // projection near numerator, Z denominator offset, max depth, focal depth
    depth: vec4<f32>,
    // physical viewport origin XY, extent XY
    viewport: vec4<f32>,
    // sharp half-width, shoulder width, pixel radius / metre, max near radius
    coc: vec4<f32>,
    // max far radius, depth rejection tolerance in metres, unused, unused
    limits: vec4<f32>,
}

@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var source_sampler: sampler;
// The opaque/alpha-mask PREPASS copy, never the transparent fog proxy's depth.
@group(0) @binding(2) var geometry_depth: texture_depth_2d;
@group(0) @binding(3) var<uniform> lens: TiltFocusUniform;

// Ten diametrically paired Vogel samples. XY = disk offset, Z = radius,
// W = exp(-2 * radius^2). Symmetry avoids bias; constants avoid trigonometry,
// exponentials, random noise, frame-dependent rotations, and sample allocation.
const DISK: array<vec4<f32>, 20> = array<vec4<f32>, 20>(
    vec4<f32>(0.223607, 0.000000, 0.223607, 0.904837),
    vec4<f32>(-0.223607, 0.000000, 0.223607, 0.904837),
    vec4<f32>(-0.285582, 0.261616, 0.387298, 0.740818),
    vec4<f32>(0.285582, -0.261616, 0.387298, 0.740818),
    vec4<f32>(0.043713, -0.498086, 0.500000, 0.606531),
    vec4<f32>(-0.043713, 0.498086, 0.500000, 0.606531),
    vec4<f32>(0.359957, 0.469501, 0.591608, 0.496585),
    vec4<f32>(-0.359957, -0.469501, 0.591608, 0.496585),
    vec4<f32>(-0.660566, -0.116845, 0.670820, 0.406570),
    vec4<f32>(0.660566, 0.116845, 0.670820, 0.406570),
    vec4<f32>(0.625746, -0.398048, 0.741620, 0.332871),
    vec4<f32>(-0.625746, 0.398048, 0.741620, 0.332871),
    vec4<f32>(-0.209300, 0.778584, 0.806226, 0.272532),
    vec4<f32>(0.209300, -0.778584, 0.806226, 0.272532),
    vec4<f32>(-0.399157, -0.768553, 0.866025, 0.223130),
    vec4<f32>(0.399157, 0.768553, 0.866025, 0.223130),
    vec4<f32>(0.866011, 0.316266, 0.921954, 0.182684),
    vec4<f32>(-0.866011, -0.316266, 0.921954, 0.182684),
    vec4<f32>(-0.900941, 0.371895, 0.974679, 0.149569),
    vec4<f32>(0.900941, -0.371895, 0.974679, 0.149569),
);

fn axial_depth(raw: f32) -> f32 {
    // Bevy's perspective projection uses reverse Z (zero is the clear sky).
    return min(lens.depth.z, lens.depth.x / max(raw + lens.depth.y, 0.000001));
}

fn signed_coc(pixel: vec2<f32>, depth: f32) -> f32 {
    let uv = (pixel - lens.viewport.xy) / lens.viewport.zw;
    let ndc = uv * vec2<f32>(2.0, -2.0) + vec2<f32>(-1.0, 1.0);
    let ray_xy = (ndc + lens.projection.zw) * lens.projection.xy;
    let view_position = vec3<f32>(ray_xy * depth, -depth);
    let plane_distance = dot(lens.plane, vec4<f32>(view_position, 1.0));

    // A genuinely world-space slab, not a screen-Y focus mask. The shoulder is
    // C1 continuous at the slab boundary; there are no quantized focus bands.
    let outside = max(abs(plane_distance) - lens.coc.x, 0.0);
    let softened = outside * outside / (outside + lens.coc.y);
    let radius = softened * lens.coc.z * lens.depth.w / max(depth, 0.01);
    let cap = select(lens.coc.w, lens.limits.x, plane_distance >= 0.0);
    return sign(plane_distance) * min(radius, cap);
}

@fragment
fn fragment(in: FullscreenVertexOutput) -> @location(0) vec4<f32> {
    let pixel = in.position.xy;
    let center = textureLoad(source, vec2<i32>(pixel), 0);
    let viewport_end = lens.viewport.xy + lens.viewport.zw;
    // Preserve other camera viewports when ViewTarget uses a larger texture.
    if any(pixel < lens.viewport.xy) || any(pixel >= viewport_end) {
        return center;
    }
    let raw_center = textureLoad(geometry_depth, vec2<i32>(pixel), 0);
    let center_depth = axial_depth(raw_center);
    let center_coc = signed_coc(pixel, center_depth);
    let radius = abs(center_coc);
    if radius <= 0.35 {
        return center;
    }

    let inverse_size = 1.0 / vec2<f32>(textureDimensions(source));
    // A projected pixel footprint allows gentle surface slopes while metre-
    // scale depth discontinuities still block light/face/waystone contamination.
    let pixel_world_size = 2.0 * center_depth * abs(lens.projection.y) / lens.viewport.w;
    let tolerance = lens.limits.y + 3.0 * pixel_world_size * radius;
    var sum = center.rgb;
    var weight_sum = 1.0;
    for (var i = 0u; i < 20u; i += 1u) {
        let tap = DISK[i];
        let sample_pixel = clamp(
            pixel + tap.xy * radius,
            lens.viewport.xy + vec2<f32>(0.5),
            viewport_end - vec2<f32>(0.5),
        );
        let uv = sample_pixel * inverse_size;
        // The gather covers ALL FOUR texels used by the bilinear color sample.
        // Testing only its nearest depth would still leak a bright focused face
        // or lamp through the other three texels at a silhouette.
        let depths = textureGather(geometry_depth, source_sampler, uv);
        let near_raw = max(max(depths.x, depths.y), max(depths.z, depths.w));
        let far_raw = min(min(depths.x, depths.y), min(depths.z, depths.w));
        let near_depth = axial_depth(near_raw);
        let far_depth = axial_depth(far_raw);
        let separation = max(abs(near_depth - center_depth), abs(far_depth - center_depth));
        let geometry_weight = 1.0 - smoothstep(tolerance, tolerance * 2.0, separation);
        if geometry_weight <= 0.0 {
            continue;
        }

        // A source contributes only if its own blur footprint reaches this
        // destination. A zero-CoC lamp/face/stone is never borrowed by a blurry
        // background pixel, even when their axial depths happen to be close.
        let near_coc = signed_coc(sample_pixel, near_depth);
        let far_coc = signed_coc(sample_pixel, far_depth);
        let same_side = near_coc * far_coc > 0.0 && near_coc * center_coc > 0.0;
        let sample_radius = select(0.0, min(abs(near_coc), abs(far_coc)), same_side);
        let distance = tap.z * radius;
        let coverage = smoothstep(distance - 0.65, distance + 0.65, sample_radius)
            * smoothstep(0.25, 0.85, sample_radius);
        let weight = tap.w * geometry_weight * coverage;
        if weight > 0.0 {
            sum += textureSampleLevel(source, source_sampler, uv, 0.0).rgb * weight;
            weight_sum += weight;
        }
    }
    // Continuous subpixel fade prevents a visible step where blur starts. The
    // original alpha is retained; this is a lens operation, not new geometry.
    let blend = smoothstep(0.35, 1.15, radius);
    return vec4<f32>(mix(center.rgb, sum / weight_sum, blend), center.a);
}
