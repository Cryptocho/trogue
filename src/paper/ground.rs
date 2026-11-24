use bevy::{
    image::{ImageLoaderSettings, ImageSampler},
    prelude::*,
    render::render_resource::{AsBindGroup, ShaderRef},
    sprite::{AlphaMode2d, Material2d, Material2dPlugin},
};

use super::{GROUND_SCALE, PaperAssets, PaperSettings, on_map, sort_key};

pub struct GroundPlugin;

impl Plugin for GroundPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins((
            Material2dPlugin::<GroundMaterial>::default(),
            Material2dPlugin::<UnderstoryMaterial>::default(),
        ))
        .add_systems(Startup, plant_floor)
        .add_systems(Update, update_lighting.run_if(resource_changed::<PaperSettings>));
    }
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct GroundMaterial {
    /// Fog, diagnostic key side. Ground relief remains on in flat-sprite mode.
    #[uniform(0)]
    lighting: Vec4,
    #[texture(1)]
    #[sampler(2)]
    albedo: Handle<Image>,
    #[texture(3)]
    #[sampler(4)]
    normal: Handle<Image>,
    #[texture(5)]
    #[sampler(6)]
    orm: Handle<Image>,
    #[texture(7)]
    #[sampler(8)]
    water: Handle<Image>,
}

impl Material2d for GroundMaterial {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/ground.wgsl".into()
    }
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct UnderstoryMaterial {
    #[uniform(0)]
    lighting: Vec4,
    /// Atlas tile origin, tile width, and virtual maximum light height in metres.
    #[uniform(1)]
    atlas: Vec4,
    #[texture(2)]
    #[sampler(3)]
    albedo: Handle<Image>,
    #[texture(4)]
    #[sampler(5)]
    normal_height: Handle<Image>,
}

impl Material2d for UnderstoryMaterial {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/ground_understory.wgsl".into()
    }

    fn alpha_mode(&self) -> AlphaMode2d {
        AlphaMode2d::Blend
    }
}

#[derive(Resource)]
struct FloorMaterials {
    ground: Handle<GroundMaterial>,
    plants: Vec<Handle<UnderstoryMaterial>>,
}

#[derive(serde::Deserialize)]
struct PlantAtlas {
    atlas_columns: u32,
    variants: u32,
    card_width: f32,
    card_height: f32,
    root_above_bottom: f32,
    max_light_height: f32,
    plants: Vec<(f32, f32, f32, usize)>,
}

fn load_map(server: &AssetServer, pending: &mut PaperAssets, path: &'static str, srgb: bool) -> Handle<Image> {
    let image = server.load_with_settings(path, move |settings: &mut ImageLoaderSettings| {
        settings.is_srgb = srgb;
        settings.sampler = ImageSampler::linear();
    });
    pending.0.push(image.id().untyped());
    image
}

fn lighting(settings: &PaperSettings) -> Vec4 {
    Vec4::new(
        if settings.fog { 1. } else { 0. },
        settings.key,
        if settings.flat_sprites { 1. } else { 0. },
        0.,
    )
}

fn plant_floor(
    mut commands: Commands,
    server: Res<AssetServer>,
    settings: Res<PaperSettings>,
    mut pending: ResMut<PaperAssets>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut ground: ResMut<Assets<GroundMaterial>>,
    mut understory: ResMut<Assets<UnderstoryMaterial>>,
) {
    // This rectangle is the entire surface: no scene/GLB, height mesh, or backdrop.
    // Image row zero is map Z=-38, matching the original non-repeating floor bake.
    let material = ground.add(GroundMaterial {
        lighting: lighting(&settings),
        albedo: load_map(&server, &mut pending, "gloam/terrain/forest_albedo.png", true),
        normal: load_map(&server, &mut pending, "gloam/terrain/forest_normal.png", false),
        orm: load_map(&server, &mut pending, "gloam/terrain/forest_orm.png", false),
        water: load_map(&server, &mut pending, "gloam/paper/ground_water.png", false),
    });
    commands.spawn((
        Name::new("二维苔径 · 泥路与浅水"),
        Mesh2d(meshes.add(Rectangle::new(48., 52. * GROUND_SCALE))),
        MeshMaterial2d(material.clone()),
        Transform::from_translation(on_map(0., -12.).extend(0.)),
    ));

    let atlas: PlantAtlas = serde_json::from_str(include_str!("../../assets/gloam/paper/ground_plants.json"))
        .expect("invalid original undergrowth atlas manifest");
    let albedo = load_map(&server, &mut pending, "gloam/paper/ground_understory.png", true);
    let normal_height = load_map(&server, &mut pending, "gloam/paper/ground_understory_data.png", false);
    let columns = atlas.atlas_columns as f32;
    // A material is shared by every instance of a botanical drawing; no per-frame
    // placement, material creation, or CPU animation. All 1,306 cards share a mesh.
    let cards: Vec<_> = (0..atlas.variants).map(|index| {
        understory.add(UnderstoryMaterial {
            lighting: lighting(&settings),
            atlas: Vec4::new(
                (index % atlas.atlas_columns) as f32 / columns,
                (index / atlas.atlas_columns) as f32 / columns,
                1. / columns,
                atlas.max_light_height,
            ),
            albedo: albedo.clone(),
            normal_height: normal_height.clone(),
        })
    }).collect();
    let mesh = meshes.add(Rectangle::new(atlas.card_width, atlas.card_height));
    for (x, z, scale, variant) in atlas.plants {
        let center = on_map(x, z) + Vec2::Y * (atlas.card_height * 0.5 - atlas.root_above_bottom) * scale;
        commands.spawn((
            Name::new(if variant < 24 { "丝叶草丛" } else if variant < 40 { "林下羽蕨" } else { "卷叶与松针" }),
            Mesh2d(mesh.clone()),
            MeshMaterial2d(cards[variant].clone()),
            Transform::from_translation(center.extend(sort_key(z))).with_scale(Vec3::new(scale, scale, 1.)),
        ));
    }
    commands.insert_resource(FloorMaterials { ground: material, plants: cards });
}

fn update_lighting(
    settings: Res<PaperSettings>,
    handles: Res<FloorMaterials>,
    mut ground: ResMut<Assets<GroundMaterial>>,
    mut understory: ResMut<Assets<UnderstoryMaterial>>,
) {
    let value = lighting(&settings);
    // Only obtain a mutable asset when its actual shader inputs changed.
    if ground.get(&handles.ground).is_some_and(|material| {
        material.lighting.x != value.x || material.lighting.y != value.y
    }) {
        ground.get_mut(&handles.ground).unwrap().lighting = value;
    }
    for handle in &handles.plants {
        if understory.get(handle).is_some_and(|material| material.lighting != value) {
            understory.get_mut(handle).unwrap().lighting = value;
        }
    }
}
