#import bevy_sprite::mesh2d_vertex_output::VertexOutput
#import "gloam/paper/light.wgsl"::{map_coord, coat}

struct Card {
    root: vec4<f32>,    // map x/z and art width/height (metres)
    pivot: vec4<f32>,   // UV foot, horizontal handedness, glass flag
    lamp: vec4<f32>, key: vec4<f32>, opt: vec4<f32>,
}
@group(2) @binding(0) var<uniform> card: Card;
@group(2) @binding(1) var paint: texture_2d<f32>;
@group(2) @binding(2) var relief: texture_2d<f32>;
@group(2) @binding(3) var emission: texture_2d<f32>;
@group(2) @binding(4) var river: texture_2d<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let mirror = in.world_position.z < 1.;
    var uv = in.uv;
    var stream = vec4(0.);
    if mirror {
        stream = textureLoad(river,map_coord(in.world_position.xy,textureDimensions(river)),0);
        if stream.r < 0.5 { discard; }
        uv.y = 1.-uv.y;
        // Fractional representation of an INTEGER 0/±1/±2 native-pixel stroke;
        // frame, image orientation and the surface normal stay in lockstep.
        uv.x += round((stream.b*255.-128.)/58.) / (25.*card.root.z);
    }
    if card.pivot.z < 0. { uv.x = 1.-uv.x; }
    if any(uv < vec2(0.)) || any(uv >= vec2(1.)) { discard; }
    let px = vec2<i32>(floor(uv*vec2<f32>(textureDimensions(paint))));
    let ink = textureLoad(paint,px,0);
    if ink.a < 0.5 { discard; }
    let tangent = textureLoad(relief,px,0).xyz*2.-1.;
    let t = mix(tangent,vec3(0.,0.,1.),card.opt.x);
    let n = normalize(vec3(t.x*card.pivot.z, t.y*0.823+t.z*0.568, -t.y*0.568+t.z*0.823));
    let p = vec3(card.root.x+(uv.x-card.pivot.x)*card.root.z*card.pivot.z,
                 max(0.,(card.pivot.y-uv.y)*card.root.w), card.root.y);
    var lit = coat(ink.rgb,n,p,card.lamp,card.key,mix(0.0012,0.012,card.opt.y));
    if card.opt.z > 0.5 {
        // A broken warm line *inside* the lit edge of cloak or wet granite.
        // Probe towards the actual glass (including mirrored sprite UVs).
        let ray = card.lamp.xyz - p;
        let d = vec2(ray.x*card.pivot.z, -ray.y + ray.z*0.568);
        let step_px = vec2<i32>(round(d * inverseSqrt(max(dot(d,d),0.01))*1.8));
        let near_edge = px + step_px;
        var edge = true;
        if all(near_edge >= vec2<i32>(0)) && all(near_edge < vec2<i32>(textureDimensions(paint))) {
            edge = textureLoad(paint,near_edge,0).a < 0.5;
        }
        if edge && max(ink.r,max(ink.g,ink.b))>0.019 {
            lit += vec3(0.76,0.34,0.055) * (2.25/(1.+dot(ray,ray)));
        }
    }
    var glass = 0.;
    if card.pivot.w > 0.5 { glass = textureLoad(emission,px,0).r; }
    if !mirror {
        return vec4(lit + glass*vec3(3.65,2.8,1.15),1.);
    }
    // Reflections image the ORIGINAL, already lit face; flipping the normals
    // would incorrectly light the tree from underneath. Water absorbs fine
    // colour before the mirror is composited over the visible stream bed.
    let dark = min(lit,vec3(0.14)) * vec3(0.056,0.084,0.17) + vec3(0.0013,0.003,0.008);
    let waviness = abs(stream.b-128./255.);
    let opacity = (0.73 + 0.12*stream.g) * (1.-0.72*waviness) * (1.-0.65*stream.a);
    return vec4(dark + glass*vec3(0.29,0.18,0.043),opacity);
}
