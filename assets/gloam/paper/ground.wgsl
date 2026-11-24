#import bevy_sprite::mesh2d_vertex_output::VertexOutput
#import "gloam/paper/light.wgsl"::{map_coord, land_position, coat}

struct Lights { lamp: vec4<f32>, key: vec4<f32> }
@group(2) @binding(0) var<uniform> lights: Lights;
@group(2) @binding(1) var paint: texture_2d<f32>;
@group(2) @binding(2) var field: texture_2d<f32>;

@fragment
fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
    let xy = in.world_position.xy;
    let px = map_coord(xy,textureDimensions(field));
    let ink = textureLoad(paint,px,0).rgb;
    let site = textureLoad(field,px,0);
    if site.r > 0.5 {
        // Blue skylight above water, two jade shoals below it. R also clips
        // the separately drawn virtual images. No RGB tree or lamp is baked here.
        let hue = smoothstep(1.5,5.1,xy.x);
        let jade = mix(vec3(0.0008,0.206,0.19),vec3(0.0035,0.363,0.18),hue);
        let glide = abs(site.b - 128./255.);
        let edge = site.a * (0.025 + 0.72*site.g);
        let color = ink + jade * (site.g*site.g*(1.05+glide*0.18))
                    + vec3(0.018,0.071,0.084)*glide*(0.22+site.g)
                    + vec3(0.17,0.63,0.57)*edge;
        return vec4(color,1.);
    }
    let n = normalize(mix(vec3(0.,1.,0.),vec3(-0.18,0.45,0.87),site.a));
    return vec4(coat(ink,n,land_position(xy),lights.lamp,lights.key,0.001),1.);
}
