#import bevy_pbr::{
    forward_io::VertexOutput,
    mesh_view_bindings::{view, lights, clusterable_objects},
    mesh_view_types::{POINT_LIGHT_FLAGS_SHADOWS_ENABLED_BIT, POINT_LIGHT_FLAGS_SPOT_LIGHT_Y_NEGATIVE, DIRECTIONAL_LIGHT_FLAGS_SHADOWS_ENABLED_BIT},
    prepass_utils::prepass_depth,
    view_transformations::{frag_coord_to_ndc, position_ndc_to_world, position_world_to_view, position_world_to_clip, ndc_to_frag_coord},
    shadows::{get_cascade_index, world_to_directional_light_local},
    shadow_sampling::{sample_shadow_map_hardware, sample_shadow_cubemap_hardware},
    clustered_forward::{fragment_cluster_index, unpack_clusterable_object_index_ranges, get_clusterable_object_id},
    lighting::getDistanceAttenuation,
}

struct Scatter {
    lamp: vec4<f32>,         // world position, scattering gain
    shaft_origin: vec4<f32>, // real canopy spotlight position, scattering gain
    shaft_target: vec4<f32>, // visible point on its axis, moon scattering gain
    volume_min: vec4<f32>,   // world-space box minimum, extinction per metre
    inverse_size: vec4<f32>,
};
@group(2) @binding(0) var<uniform> air: Scatter;
@group(2) @binding(1) var density_field: texture_3d<f32>;
@group(2) @binding(2) var linear_sampler: sampler;

const NO_LIGHT: u32 = 0xffffffffu;

// A mildly forward-scattering medium, not an additive glow on surfaces.
fn phase(cos_angle: f32) -> f32 {
    let g = 0.24;
    let d = 1. + g * g - 2. * g * cos_angle;
    return 0.07957747 * (1. - g * g) / (d * sqrt(d));
}

// Resolve the two actual local lights once per ray. The fixed camera keeps
// these world anchors visible at both zooms. Never assume light-buffer order,
// nor use a surface's cluster for the whole volume ray.
fn find_light(anchor: vec3<f32>, origin: vec3<f32>, spot: bool) -> u32 {
    let clip = position_world_to_clip(anchor);
    if clip.w <= 0. { return NO_LIGHT; }
    let ndc = clip.xy / clip.w;
    if any(abs(ndc) >= vec2(1.)) { return NO_LIGHT; }
    let cluster = fragment_cluster_index(
        ndc_to_frag_coord(ndc), position_world_to_view(anchor).z, false);
    let ranges = unpack_clusterable_object_index_ranges(cluster);
    let begin = select(ranges.first_point_light_index_offset, ranges.first_spot_light_index_offset, spot);
    let end = select(ranges.first_spot_light_index_offset, ranges.first_reflection_probe_index_offset, spot);
    for (var i = begin; i < end; i++) {
        let id = get_clusterable_object_id(i);
        let delta = clusterable_objects.data[id].position_radius.xyz - origin;
        if dot(delta, delta) < 0.04 { return id; }
    }
    return NO_LIGHT;
}

fn moon_cascade_visibility(p: vec3<f32>, cascade: u32) -> f32 {
    let light = &lights.directional_lights[0];
    // Medium samples have no surface normal: apply depth bias only.
    let clip = world_to_directional_light_local(0u, cascade,
        vec4(p + (*light).direction_to_light * (*light).shadow_depth_bias, 1.));
    if clip.w == 0. { return 1.; }
    return sample_shadow_map_hardware(
        clip.xy, clip.z, i32((*light).depth_texture_base_index + cascade));
}

fn moon_visibility(p: vec3<f32>) -> f32 {
    if lights.n_directional_lights == 0u { return 1.; }
    let light = &lights.directional_lights[0];
    if ((*light).flags & DIRECTIONAL_LIGHT_FLAGS_SHADOWS_ENABLED_BIT) == 0u
        || (*light).num_cascades == 0u { return 1.; }
    let view_z = position_world_to_view(p).z;
    let cascade = get_cascade_index(0u, view_z);
    if cascade >= (*light).num_cascades { return 1.; }
    var visibility = moon_cascade_visibility(p, cascade);
    if cascade + 1u < (*light).num_cascades {
        let far = (*light).cascades[cascade].far_bound;
        let near = far * (1. - (*light).cascades_overlap_proportion);
        if -view_z > near {
            visibility = mix(visibility, moon_cascade_visibility(p, cascade + 1u),
                clamp((-view_z - near) / max(far - near, 0.0001), 0., 1.));
        }
    }
    return visibility;
}

// This is the same stable basis used by Bevy's spot shadow renderer.
// Construct it once, outside the march; no normal-bias offset belongs in air.
fn spot_basis(id: u32) -> mat3x3<f32> {
    if id == NO_LIGHT { return mat3x3<f32>(vec3(1., 0., 0.), vec3(0., 1., 0.), vec3(0., 0., 1.)); }
    let light = &clusterable_objects.data[id];
    var direction = vec3((*light).light_custom_data.x, 0., (*light).light_custom_data.y);
    direction.y = sqrt(max(0., 1. - dot(direction.xz, direction.xz)));
    if ((*light).flags & POINT_LIGHT_FLAGS_SPOT_LIGHT_Y_NEGATIVE) != 0u {
        direction.y = -direction.y;
    }
    let fwd = -direction;
    let sign = select(-1., 1., fwd.z >= 0.);
    let a = -1. / (fwd.z + sign);
    let b = fwd.x * fwd.y * a;
    let up = vec3(1. + sign * fwd.x * fwd.x * a, sign * b, -sign * fwd.x);
    let right = vec3(-b, -sign - fwd.y * fwd.y * a, fwd.y);
    return mat3x3<f32>(right, up, fwd);
}

fn point_visibility(p: vec3<f32>, to_light: vec3<f32>, id: u32) -> f32 {
    let light = &clusterable_objects.data[id];
    if ((*light).flags & POINT_LIGHT_FLAGS_SHADOWS_ENABLED_BIT) == 0u { return 1.; }
    let local = p + (*light).shadow_depth_bias * to_light - (*light).position_radius.xyz;
    let major = max(abs(local.x), max(abs(local.y), abs(local.z)));
    if major <= (*light).shadow_map_near_z { return 1.; }
    let zw = -major * (*light).light_custom_data.xy + (*light).light_custom_data.zw;
    return sample_shadow_cubemap_hardware(local * vec3(1., 1., -1.), zw.x / zw.y, id);
}

fn spot_visibility(p: vec3<f32>, to_light: vec3<f32>, id: u32, basis: mat3x3<f32>) -> f32 {
    let light = &clusterable_objects.data[id];
    if ((*light).flags & POINT_LIGHT_FLAGS_SHADOWS_ENABLED_BIT) == 0u { return 1.; }
    let local = (p - (*light).position_radius.xyz + (*light).shadow_depth_bias * to_light) * basis;
    if -local.z <= (*light).shadow_map_near_z { return 1.; }
    let uv = local.xy / ((*light).spot_light_tan_angle * -local.z) * vec2(0.5, -0.5) + vec2(0.5);
    if any(uv < vec2(0.)) || any(uv > vec2(1.)) { return 1.; }
    return sample_shadow_map_hardware(uv, (*light).shadow_map_near_z / -local.z,
        i32(id) + lights.spot_light_shadowmap_offset);
}

fn local_scatter(p: vec3<f32>, ray: vec3<f32>, id: u32, spot: bool, basis: mat3x3<f32>) -> vec3<f32> {
    if id == NO_LIGHT { return vec3(0.); }
    let light = &clusterable_objects.data[id];
    let color = (*light).color_inverse_square_range.rgb * view.exposure;
    if all(color <= vec3(0.)) { return vec3(0.); }
    let delta = (*light).position_radius.xyz - p;
    let distance_squared = max(dot(delta, delta), 0.0001);
    var attenuation = getDistanceAttenuation(
        max(distance_squared, (*light).position_radius.w * (*light).position_radius.w),
        (*light).color_inverse_square_range.w);
    if attenuation <= 0. { return vec3(0.); }
    let to_light = delta * inverseSqrt(distance_squared);
    if spot {
        let cone = clamp(dot(basis[2], to_light) * (*light).light_custom_data.z
            + (*light).light_custom_data.w, 0., 1.);
        attenuation *= cone * cone;
        // Do not pay for a shadow sample outside the real spotlight cone.
        if attenuation <= 0. { return vec3(0.); }
        attenuation *= spot_visibility(p, to_light, id, basis);
    } else {
        attenuation *= point_visibility(p, to_light, id);
    }
    return color * (attenuation * phase(dot(ray, to_light)));
}

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let entry = in.world_position.xyz;
    let ray = normalize(entry - view.world_position);
    let inverse_ray = 1. / select(vec3(0.00001), ray, abs(ray) > vec3(0.00001));
    let volume_max = air.volume_min.xyz + 1. / air.inverse_size.xyz;
    let far_faces = max((air.volume_min.xyz - entry) * inverse_ray, (volume_max - entry) * inverse_ray);
    var distance = min(far_faces.x, min(far_faces.y, far_faces.z));
    // Reverse-Z depth zero is sky, not a surface at an arbitrary far distance.
    let depth = prepass_depth(in.position, 0u);
    if depth > 0. {
        let solid = position_ndc_to_world(frag_coord_to_ndc(vec4(in.position.xy, depth, 1.)));
        distance = min(distance, dot(solid - entry, ray) - 0.025);
    }
    if distance <= 0. { return vec4(0.); }
    // Include the half-voxel trilinear border beyond the baked z=4 cutoff.
    if min(entry.z, (entry + ray * distance).z) >= 4.25 { return vec4(0.); }

    let lamp_id = find_light(air.lamp.xyz, air.lamp.xyz, false);
    let shaft_id = find_light(air.shaft_target.xyz, air.shaft_origin.xyz, true);
    let basis = spot_basis(shaft_id);
    var moon = vec3(0.);
    if lights.n_directional_lights > 0u {
        let light = &lights.directional_lights[0];
        moon = (*light).color.rgb * view.exposure
            * (air.shaft_target.w * phase(dot(ray, (*light).direction_to_light)));
    }
    // At most twenty trilinear density taps; short rays need fewer. Unlike
    // stock volumetrics, no low-resolution screen buffer is enlarged here.
    let steps = u32(clamp(ceil(distance / 0.48), 4., 20.));
    let ds = distance / f32(steps);
    // Stationary sub-step dither only; all density is in world/volume space.
    let offset = 0.5 + 0.35 * (fract(dot(in.position.xy, vec2(0.7548777, 0.5698403))) - 0.5);
    var transmission = 1.;
    var radiance = vec3(0.);
    for (var i = 0u; i < steps; i++) {
        let p = entry + ray * ((f32(i) + offset) * ds);
        let uvw = (p - air.volume_min.xyz) * air.inverse_size.xyz;
        let density = textureSampleLevel(density_field, linear_sampler, uvw, 0.).r;
        if density < 0.001 { continue; }
        let opacity = 1. - exp(-air.volume_min.w * density * ds);
        var incident = vec3(0.008, 0.014, 0.024);
        if any(moon > vec3(0.)) { incident += moon * moon_visibility(p); }
        incident += air.lamp.w * local_scatter(p, ray, lamp_id, false, basis)
            + air.shaft_origin.w * local_scatter(p, ray, shaft_id, true, basis);
        radiance += transmission * opacity * incident;
        transmission *= 1. - opacity;
    }
    return vec4(radiance, 1. - transmission);
}
