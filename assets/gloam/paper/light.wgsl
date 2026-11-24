// All positions are tied to the map (metres), never to the window or zoom.
const EYE: vec3<f32> = vec3(0.0, 0.568, 0.823);
const MOON: vec3<f32> = vec3(-0.56, 0.70, -0.441);

fn map_coord(xy: vec2<f32>, dimensions: vec2<u32>) -> vec2<i32> {
    let size = vec2<i32>(dimensions);
    let cell = floor(vec2<f32>(dimensions) * 0.5 + vec2(xy.x, -xy.y) * 25.0);
    return clamp(vec2<i32>(cell), vec2<i32>(0), size - 1);
}

fn land_position(xy: vec2<f32>) -> vec3<f32> {
    return vec3(xy.x, 0., -xy.y / 0.568 - 0.3);
}

fn coat(color: vec3<f32>, n: vec3<f32>, p: vec3<f32>, lamp: vec4<f32>, key: vec4<f32>, sheen: f32) -> vec3<f32> {
    // A dim sky leaves the unlit bank and the upper canopy in one value family.
    // The ONLY strong local source on land is the glass on the pole.
    let sky = vec3(0.115, 0.173, 0.225) * (0.78 + 0.22*n.y);
    let moon = vec3(0.095,0.135,0.23)*max(dot(n, MOON),0.);
    let delta = lamp.xyz - p;
    let d2 = dot(delta,delta);
    let l = delta * inverseSqrt(max(d2,0.006));
    let flame = vec3(1.0,0.53,0.16) * (7.8 / (0.62+d2))
                * (1.-smoothstep(14.,64.,d2));
    let clearing = 1.0 - 0.83*smoothstep(14.,145.,dot(delta.xz,delta.xz));
    var result = color*((sky + moon)*clearing + flame*max(dot(n,l),0.));
    result += flame * sheen * pow(max(dot(n,normalize(l+EYE)),0.),24.);
    if abs(key.w)>0.5 {
        let bulb = key.xyz + vec3(key.w*1.92,1.16,1.25);
        let ray = bulb-p;
        let r2 = dot(ray,ray);
        let k = ray * inverseSqrt(max(r2,0.005));
        let cone = smoothstep(0.76,0.92,dot(k,normalize(bulb-key.xyz)));
        result += 20.*cone/(r2+0.55)*(color*max(dot(n,k),0.) +
                  sheen*pow(max(dot(n,normalize(k+EYE)),0.),18.));
    }
    // Subtle distant atmospheric ink, not a white screen-wide overlay.
    let far = smoothstep(5.,15.,-p.z) * lamp.w * 0.11;
    return mix(result,vec3(0.005,0.010,0.023),far);
}
