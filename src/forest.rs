use bevy::{
    asset::{LoadState, RecursiveDependencyLoadState, UntypedAssetId},
    image::{ImageLoaderSettings, ImageSampler},
    pbr::{ExtendedMaterial, MaterialExtension, OpaqueRendererMethod},
    prelude::*,
    render::render_resource::{AsBindGroup, ShaderRef},
    scene::SceneInstance,
};

use super::LAMP;

const CARD_PITCH: f32 = 0.35;
pub const WAYSTONE_ROOT: Vec3 = Vec3::new(-3.15, 0., -0.98);

pub type LitSprite = ExtendedMaterial<StandardMaterial, SpriteRelief>;

#[derive(Asset, TypePath, AsBindGroup, Clone)]
pub struct SpriteRelief {
    #[texture(100)]
    #[sampler(101)]
    normal: Handle<Image>,
    #[uniform(102)]
    surface: Vec4,
}

impl MaterialExtension for SpriteRelief {
    fn deferred_fragment_shader() -> ShaderRef {
        "gloam/light/lit_sprite.wgsl".into()
    }
}

#[derive(Clone, Copy)]
#[repr(usize)]
enum Species {
    Traveler,
    Oak,
    Fir,
    Snag,
    Rock,
    Waystone,
    Birch,
}
use Species::*;
const NAMES: [&str; 7] = [
    "traveler", "oak", "fir", "snag", "rock", "waystone", "birch",
];

// (sprite, root x, root z, visible height, width multiplier). A negative
// width mirrors UVs, never the world transform or the shadow winding.
const CLEARING: &[(Species, f32, f32, f32, f32)] = &[
    (Traveler, 0.55, 0.55, 1.58, 1.),
    // An old boundary stone with a fallen fragment and a pocket of granite.
    (Waystone, WAYSTONE_ROOT.x, WAYSTONE_ROOT.z, 1.82, 1.05),
    (Rock, -3.83, -0.66, 0.58, -1.32),
    (Rock, 5.57, -2.42, 0.98, -1.10),
    (Rock, -5.92, 3.74, 0.57, -1.26),
    (Rock, -5.47, 4.14, 0.25, 0.94),
    (Rock, 8.24, 3.12, 0.64, 0.91),
    (Rock, 7.38, 3.51, 0.42, -0.83),
    (Rock, -1.87, -6.72, 0.47, -0.85),
    (Rock, 4.62, -9.20, 0.79, 1.25),
    // Near boughs crop the edges; the center stays open all the way to the ground.
    (Oak, -9.14, 3.38, 6.35, 1.12),
    (Fir, 9.44, 3.27, 6.63, -1.02),
    (Snag, 7.53, 5.78, 3.99, -0.85),
    (Fir, -6.78, 8.81, 3.90, -1.03),
    // Uneven groups, not a ring of equally spaced identical trunks.
    (Fir, -8.20, -2.85, 5.14, 0.87),
    (Snag, -6.24, -4.72, 3.91, 1.01),
    (Oak, 7.46, -3.67, 5.46, -0.94),
    (Fir, 9.49, -5.10, 4.64, 0.97),
    (Birch, 4.65, -4.77, 4.65, 1.04),
    (Oak, -9.78, -8.42, 5.84, -1.04),
    (Fir, -4.42, -8.74, 5.36, -0.86),
    (Fir, -2.56, -9.40, 3.46, 0.78),
    (Snag, 5.72, -9.94, 4.69, -1.14),
    (Fir, 7.65, -11.64, 6.12, 1.07),
    (Oak, 11.54, -10.21, 5.04, 0.82),
    (Snag, -12.30, -10.82, 4.24, 0.9),
    (Fir, -13.40, -7.58, 4.91, 1.08),
    // Beyond the clearing the path slips behind a second and third stand.
    (Fir, -14.88, -15.7, 6.72, -1.19),
    (Birch, -11.45, -14.2, 5.90, -0.99),
    (Fir, -7.45, -13.3, 5.02, -0.90),
    (Snag, -5.65, -15.1, 5.67, 1.09),
    (Oak, -1.42, -14.3, 6.49, -1.04),
    (Fir, 2.75, -15.6, 4.78, 1.07),
    (Fir, 4.11, -17.4, 6.34, -0.82),
    (Oak, 8.28, -17.2, 5.70, 0.96),
    (Oak, 13.40, -16.2, 6.55, -0.88),
    (Fir, 16.2, -15.3, 5.60, 0.95),
    (Fir, -12.4, -21.1, 7.5, 1.1),
    (Fir, -5.3, -20.4, 6.1, -1.1),
    (Oak, 1.1, -22.0, 6.8, -0.9),
    (Fir, 6.4, -21.1, 7.3, -1.0),
    (Fir, 13.1, -21.5, 7.1, 1.0),
];

#[derive(serde::Deserialize)]
struct Manifest<'a> {
    #[serde(borrow)]
    sprites: [SpriteEntry<'a>; NAMES.len()],
}

#[derive(serde::Deserialize)]
struct SpriteEntry<'a> {
    name: &'a str,
    image: &'a str,
    normal: &'a str,
    ground_anchor_uv_top_left: [f32; 2],
}

struct SpriteMaps {
    color: Handle<Image>,
    normal: Handle<Image>,
    anchor: Vec2,
}

#[derive(Resource)]
pub struct Loading {
    sprites: [SpriteMaps; NAMES.len()],
    scenes: [Handle<Scene>; 2],
    sky: EnvironmentMapLight,
}

pub fn load(mut commands: Commands, server: Res<AssetServer>) {
    let manifest: Manifest<'static> =
        serde_json::from_str(include_str!("../assets/gloam/sprites/manifest.json"))
            .expect("invalid sprite export manifest");
    let sprites = NAMES.map(|name| {
        let entry = manifest
            .sprites
            .iter()
            .find(|s| s.name == name)
            .expect("missing sprite in export manifest");
        let image = |path: &'static str, srgb| {
            server.load_with_settings(path, move |s: &mut ImageLoaderSettings| {
                s.sampler = ImageSampler::nearest();
                s.is_srgb = srgb;
            })
        };
        SpriteMaps {
            color: image(entry.image, true),
            normal: image(entry.normal, false),
            anchor: entry.ground_anchor_uv_top_left.into(),
        }
    });
    let scenes = ["gloam/terrain/floor.glb", "gloam/waylight.glb"]
        .map(|file| server.load(GltfAssetLabel::Scene(0).from_asset(file)));
    commands.spawn((Name::new("苔土与林下植被"), SceneRoot(scenes[0].clone())));
    commands.spawn((
        Name::new("旧道提灯"),
        SceneRoot(scenes[1].clone()),
        Transform::from_translation(LAMP),
    ));
    let sky = EnvironmentMapLight {
        diffuse_map: server.load("gloam/light/canopy-irradiance.dds"),
        specular_map: server.load("gloam/light/canopy-radiance.dds"),
        intensity: 104.,
        ..default()
    };
    commands.insert_resource(Loading {
        sprites,
        scenes,
        sky,
    });
}

// A pair of foot-anchored cutouts. The map's green axis always points up
// the picture, including in a reflection; mirrored highlights stay on the lit side.
fn cutout(mirrored: bool) -> Mesh {
    let mut mesh = Mesh::from(Rectangle::new(1., 1.)).translated_by(Vec3::Y * 0.5);
    let s = if mirrored { -1. } else { 1. };
    if mirrored {
        mesh.insert_attribute(
            Mesh::ATTRIBUTE_UV_0,
            vec![[0., 0.], [1., 0.], [1., 1.], [0., 1.]],
        );
    }
    mesh.insert_attribute(Mesh::ATTRIBUTE_TANGENT, vec![[s, 0., 0., s]; 4]);
    mesh
}

pub fn plant_when_loaded(
    mut commands: Commands,
    pending: Res<Loading>,
    server: Res<AssetServer>,
    scenes: Query<&SceneInstance, With<SceneRoot>>,
    spawner: Res<SceneSpawner>,
    camera: Single<Entity, With<Camera3d>>,
    images: Res<Assets<Image>>,
    fog: Single<&crate::haze::GroundMist>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<LitSprite>>,
    options: Res<crate::desktop::ViewOptions>,
    mut exit: EventWriter<AppExit>,
) {
    let ids = pending
        .scenes
        .iter()
        .map(|h| h.id().untyped())
        .chain(
            pending
                .sprites
                .iter()
                .flat_map(|s| [s.color.id().untyped(), s.normal.id().untyped()]),
        )
        .chain([
            pending.sky.diffuse_map.id().untyped(),
            pending.sky.specular_map.id().untyped(),
            fog.0.id().untyped(),
        ]);
    for id in ids {
        match ready(&server, id) {
            Ok(true) => (),
            Ok(false) => return,
            Err(e) => {
                error!("场景素材加载失败：{e}");
                exit.write(AppExit::error());
                return;
            }
        }
    }
    if scenes.iter().len() != pending.scenes.len()
        || !scenes.iter().all(|s| spawner.instance_is_ready(**s))
    {
        return;
    }
    let cards = [meshes.add(cutout(false)), meshes.add(cutout(true))];
    let (sin_pitch, cos_pitch) = (35_f32.to_radians() - CARD_PITCH).sin_cos();
    let prototypes: [_; NAMES.len()] = std::array::from_fn(|i| {
        let s = &pending.sprites[i];
        let stone = i == Rock as usize || i == Waystone as usize;
        let size = images.get(&s.color).unwrap().size();
        let mat = materials.add(LitSprite {
            base: StandardMaterial {
                base_color_texture: Some(s.color.clone()),
                alpha_mode: AlphaMode::Mask(0.5),
                cull_mode: None,
                double_sided: true,
                perceptual_roughness: 0.92,
                reflectance: if stone { 0.44 } else { 0.24 },
                opaque_render_method: OpaqueRendererMethod::Deferred,
                ..default()
            },
            extension: SpriteRelief {
                normal: s.normal.clone(),
                // Moss is matte, damp exposed rock is not polished metal.
                surface: Vec4::new(
                    if stone { 0.58 } else { 0.92 },
                    sin_pitch,
                    cos_pitch,
                    if options.flat_sprites { 0. } else { 1. },
                ),
            },
        });
        (size.x as f32 / size.y as f32, mat, s.anchor)
    });
    let tilt = Quat::from_rotation_x(-CARD_PITCH);
    for &(kind, x, z, h, width) in CLEARING {
        let (aspect, mat, anchor) = &prototypes[kind as usize];
        let embedded = match kind {
            Rock => 0.09 * h,
            Waystone => 0.035 * h,
            _ => 0.,
        };
        let foot = Vec3::new(x, -embedded, z);
        let offset = Vec3::new(
            (0.5 - anchor.x) * h * aspect * width,
            (anchor.y - 1.) * h,
            0.,
        );
        commands.spawn((
            Name::new(NAMES[kind as usize]),
            Mesh3d(cards[usize::from(width < 0.)].clone()),
            MeshMaterial3d(mat.clone()),
            Transform::from_translation(foot + tilt * offset)
                .with_rotation(tilt)
                .with_scale(Vec3::new(h * aspect * width.abs(), h, h)),
        ));
    }
    commands.entity(*camera).insert(pending.sky.clone());
    commands.remove_resource::<Loading>();
    info!("苔径留灯：地表、灯具、精灵及探针已就绪。");
}

fn ready(
    server: &AssetServer,
    id: UntypedAssetId,
) -> Result<bool, std::sync::Arc<bevy::asset::AssetLoadError>> {
    use RecursiveDependencyLoadState as Deps;
    match server.get_load_states(id) {
        Some((LoadState::Failed(e), _, _)) | Some((_, _, Deps::Failed(e))) => Err(e),
        Some((LoadState::Loaded, _, Deps::Loaded)) => Ok(true),
        _ => Ok(false),
    }
}
