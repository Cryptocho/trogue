use bevy::{
    image::{ImageLoaderSettings, ImageSampler},
    prelude::*,
    render::render_resource::{AsBindGroup, ShaderRef},
    sprite::{AlphaMode2d, Material2d, Material2dPlugin},
};

use super::{GROUND_SCALE, PaperAssets, PaperSettings, WORLD, on_map, sort_key};
use crate::forest::{CLEARING, NAMES, Species, SpriteMaps};

// Native pixel cards are upright. Only their face-normal basis uses the
// 35-degree picture view; no HD-2D sprite or photograph is loaded here.
const LANTERN_ROOT: Vec2 = Vec2::new(crate::LAMP.x, crate::LAMP.z);
const LANTERN_HEIGHT: f32 = 2.10;
const LANTERN_WIDTH: f32 = 1.575;
const LANTERN_ANCHOR: Vec2 = Vec2::new(12. / 42., 55. / 56.);
const LANTERN_FOOT_U: f32 = 31. / 42.;
const MOON_SLOPE: Vec2 = Vec2::new(10. / 12., 5. / 12.);
const LAMP_HEIGHT: f32 = 1.48;

pub(super) struct ActorPlugin;

impl Plugin for ActorPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins((
            Material2dPlugin::<ActorMaterial>::default(),
            Material2dPlugin::<ShadowMaterial>::default(),
        ))
        .add_systems(Startup, plant)
        .add_systems(
            Update,
            update_settings.run_if(resource_changed::<PaperSettings>),
        );
    }
}

type ActorBlock = [Vec4; 4];

#[derive(Clone, Copy)]
struct ActorUniform {
    // Root map x/z, full image width/height in metres.
    root: Vec4,
    // Original top-left UV anchor, horizontal handedness, ground embedding.
    anchor: Vec4,
    // Bare roughness, dielectric specular, glass emission enabled, card pitch.
    surface: Vec4,
    // Fog, flat-normal inspection, white key side, unused.
    options: Vec4,
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
#[uniform(0, ActorBlock)]
struct ActorMaterial {
    actor: ActorUniform,
    #[texture(1)]
    #[sampler(2)]
    color: Handle<Image>,
    #[texture(3)]
    #[sampler(4)]
    normal: Handle<Image>,
    #[texture(5)]
    #[sampler(6)]
    emission: Option<Handle<Image>>,
}

impl From<&ActorMaterial> for ActorBlock {
    fn from(material: &ActorMaterial) -> Self {
        let a = &material.actor;
        [a.root, a.anchor, a.surface, a.options]
    }
}

impl Material2d for ActorMaterial {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/actor.wgsl".into()
    }

    fn alpha_mode(&self) -> AlphaMode2d {
        AlphaMode2d::Mask(0.5)
    }
}

type ShadowBlock = [Vec4; 5];

#[derive(Clone, Copy)]
struct ShadowUniform {
    root: Vec4,
    // UV anchor, handedness, 0 = contact, 1 = moon, 2 = lantern.
    anchor: Vec4,
    // Projection direction x/z, horizontal emitter distance, finite length.
    projection: Vec4,
    // Actual foot x/z and support ellipse radii in map metres.
    contact: Vec4,
    // Shadow switch, fog switch, source strength, unused.
    options: Vec4,
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
#[uniform(0, ShadowBlock)]
struct ShadowMaterial {
    shadow: ShadowUniform,
    #[texture(1)]
    #[sampler(2)]
    silhouette: Handle<Image>,
}

impl From<&ShadowMaterial> for ShadowBlock {
    fn from(material: &ShadowMaterial) -> Self {
        let s = &material.shadow;
        [s.root, s.anchor, s.projection, s.contact, s.options]
    }
}

impl Material2d for ShadowMaterial {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/shadow.wgsl".into()
    }

    fn alpha_mode(&self) -> AlphaMode2d {
        AlphaMode2d::Blend
    }
}

#[derive(Resource, Default)]
struct ActorHandles {
    surfaces: Vec<Handle<ActorMaterial>>,
    shadows: Vec<Handle<ShadowMaterial>>,
}

fn surface_options(settings: &PaperSettings) -> Vec4 {
    Vec4::new(
        if settings.fog { 1. } else { 0. },
        if settings.flat_sprites { 1. } else { 0. },
        settings.key,
        0.,
    )
}

#[derive(serde::Deserialize)]
struct CastManifest<'a> {
    #[serde(borrow)]
    sprites: [CastEntry<'a>; NAMES.len()],
    #[serde(borrow)]
    near_trees: [CastEntry<'a>; 2],
}

#[derive(serde::Deserialize)]
struct CastEntry<'a> {
    name: &'a str,
    image: &'a str,
    normal: &'a str,
    size_px: [u32; 2],
    ground_anchor_uv: [f32; 2],
}

fn pixel_image(server: &AssetServer, path: &'static str, srgb: bool) -> Handle<Image> {
    server.load_with_settings(path, move |s: &mut ImageLoaderSettings| {
        s.sampler = ImageSampler::nearest();
        s.is_srgb = srgb;
    })
}

fn load_cast(server: &AssetServer) -> ([SpriteMaps; NAMES.len()], [SpriteMaps; 2]) {
    let manifest: CastManifest<'static> =
        serde_json::from_str(include_str!("../../assets/gloam/paper/cast/manifest.json"))
            .expect("invalid native pixel cast manifest");
    let load = |entry: &CastEntry<'static>| SpriteMaps {
        color: pixel_image(server, entry.image, true),
        normal: pixel_image(server, entry.normal, false),
        anchor: entry.ground_anchor_uv.into(),
        aspect: entry.size_px[0] as f32 / entry.size_px[1] as f32,
    };
    let cast = NAMES.map(|name| {
        let entry = manifest.sprites.iter().find(|entry| entry.name == name)
            .expect("missing native pixel actor");
        load(entry)
    });
    (cast, manifest.near_trees.each_ref().map(load))
}

fn plant(
    mut commands: Commands,
    server: Res<AssetServer>,
    settings: Res<PaperSettings>,
    mut pending: ResMut<PaperAssets>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut surfaces: ResMut<Assets<ActorMaterial>>,
    mut shadows: ResMut<Assets<ShadowMaterial>>,
) {
    let (maps, near_trees) = load_cast(&server);
    for sprite in maps.iter().chain(&near_trees) {
        pending.0.extend([
            sprite.color.id().untyped(),
            sprite.normal.id().untyped(),
        ]);
    }
    let lantern = SpriteMaps {
        color: pixel_image(&server, "gloam/paper/lantern.png", true),
        normal: pixel_image(&server, "gloam/paper/lantern_normal.png", false),
        anchor: LANTERN_ANCHOR,
        aspect: LANTERN_WIDTH / LANTERN_HEIGHT,
    };
    let emission = pixel_image(&server, "gloam/paper/lantern_emission.png", false);
    let deadfall = SpriteMaps {
        color: pixel_image(&server, "gloam/paper/deadfall.png", true),
        normal: pixel_image(&server, "gloam/paper/deadfall_normal.png", false),
        anchor: Vec2::new(31. / 61., 26. / 29.),
        aspect: 61. / 29.,
    };
    pending.0.extend([
        lantern.color.id().untyped(),
        lantern.normal.id().untyped(),
        emission.id().untyped(),
        deadfall.color.id().untyped(),
        deadfall.normal.id().untyped(),
    ]);

    let quad = meshes.add(Rectangle::new(1., 1.));
    let mut handles = ActorHandles::default();
    for &(species, x, z, height, width_multiplier) in CLEARING {
        let maps = match species {
            Species::Oak if z >= -6. => &near_trees[0],
            Species::Fir if z >= -6. => &near_trees[1],
            _ => &maps[species as usize],
        };
        let width = height * maps.aspect * width_multiplier.abs();
        let mirror = width_multiplier.signum();
        let root = Vec2::new(x, z);
        let embedded = match species {
            Species::Rock => 0.025 * height,
            Species::Waystone => 0.015 * height,
            _ => 0.,
        };
        let stone = matches!(species, Species::Rock | Species::Waystone);
        handles.surfaces.push(place(
            &mut commands, &quad, &mut surfaces, NAMES[species as usize], maps, None,
            ActorUniform {
                root: Vec4::new(x, z, width, height),
                anchor: Vec4::new(maps.anchor.x, maps.anchor.y, mirror, embedded),
                surface: Vec4::new(
                    if stone { 0.64 } else { 0.94 },
                    if stone { 0.28 } else { 0.16 },
                    0.,
                    0.,
                ),
                options: surface_options(&settings),
            },
        ));

        // These are support widths, not crown widths: a tree's contact shadow
        // belongs to its trunk/root plate, and the traveler's to the two boots.
        let support = match species {
            Species::Traveler => Vec2::new(0.18, 0.15),
            Species::Oak => Vec2::new(height * 0.105, height * 0.055),
            Species::Fir => Vec2::new(height * 0.075, height * 0.045),
            Species::Snag => Vec2::new(height * 0.090, height * 0.055),
            Species::Birch => Vec2::new(height * 0.065, height * 0.045),
            Species::Rock => Vec2::new(width * 0.39, height * 0.40),
            Species::Waystone => Vec2::new(width * 0.40, height * 0.16),
        };
        plant_shadows(
            &mut commands,
            &quad,
            &mut shadows,
            &mut handles,
            &settings,
            maps,
            root,
            width,
            height,
            mirror,
            support,
        );
    }

    handles.surfaces.push(place(
        &mut commands, &quad, &mut surfaces, "旧道提灯", &lantern, Some(emission),
        ActorUniform {
            root: Vec4::new(
                LANTERN_ROOT.x,
                LANTERN_ROOT.y,
                LANTERN_WIDTH,
                LANTERN_HEIGHT,
            ),
            anchor: Vec4::new(LANTERN_ANCHOR.x, LANTERN_ANCHOR.y, 1., 0.),
            // Only the separate glass mask emits. Pole, stone, copper hood and
            // bracket all retain normal-mapped illumination.
            surface: Vec4::new(0.72, 0.32, 1., 0.),
            options: surface_options(&settings),
        },
    ));

    // The map light position is below the hanging glass, NOT below the pole.
    // Moving the shadow caster's UV anchor preserves the complete silhouette
    // while putting its contact and outgoing projection under the stone base.
    let pole_root = LANTERN_ROOT
        + Vec2::X * ((LANTERN_FOOT_U - LANTERN_ANCHOR.x) * LANTERN_WIDTH);
    let pole_maps = SpriteMaps {
        anchor: Vec2::new(LANTERN_FOOT_U, LANTERN_ANCHOR.y),
        ..lantern
    };
    plant_shadows(
        &mut commands,
        &quad,
        &mut shadows,
        &mut handles,
        &settings,
        &pole_maps,
        pole_root,
        LANTERN_WIDTH,
        LANTERN_HEIGHT,
        1.,
        Vec2::new(0.24, 0.19),
    );
    // The fallen trunk occupies ground depth: its twigs are not a standing
    // 1.16 m wall merely because its picture is 29 pixels tall.
    let root = Vec2::new(-4.4, 4.9);
    let width = 61. / 25.;
    let height = 29. / 25.;
    let pitch = 1.12_f32;
    handles.surfaces.push(place(
        &mut commands, &quad, &mut surfaces, "层菌朽木", &deadfall, None,
        ActorUniform {
            root: Vec4::new(root.x, root.y, width, height),
            anchor: Vec4::new(deadfall.anchor.x, deadfall.anchor.y, 1., 0.),
            surface: Vec4::new(0.91, 0.16, 0., pitch),
            options: surface_options(&settings),
        },
    ));
    plant_shadows(
        &mut commands, &quad, &mut shadows, &mut handles, &settings, &deadfall,
        root, width, height * pitch.cos(), 1., Vec2::new(0.94, 0.25),
    );
    commands.insert_resource(handles);
}

fn place(
    commands: &mut Commands,
    quad: &Handle<Mesh>,
    surfaces: &mut Assets<ActorMaterial>,
    name: &'static str,
    maps: &SpriteMaps,
    emission: Option<Handle<Image>>,
    actor: ActorUniform,
) -> Handle<ActorMaterial> {
    let material = surfaces.add(ActorMaterial {
        actor, color: maps.color.clone(), normal: maps.normal.clone(), emission,
    });
    commands.spawn((
        Name::new(name),
        Mesh2d(quad.clone()),
        WORLD,
        MeshMaterial2d(material.clone()),
        actor_transform(
            Vec2::new(actor.root.x, actor.root.y), maps.anchor, actor.root.z, actor.root.w,
            actor.anchor.z, actor.anchor.w,
        ),
    ));
    material
}

fn actor_transform(
    root: Vec2,
    anchor: Vec2,
    width: f32,
    screen_height: f32,
    mirror: f32,
    embedded: f32,
) -> Transform {
    let center = on_map(root.x, root.y)
        + Vec2::new(
            (0.5 - anchor.x) * width * mirror,
            (anchor.y - 0.5) * screen_height - embedded * 0.819_152,
        );
    Transform::from_xyz(center.x, center.y, sort_key(root.y)).with_scale(Vec3::new(
        width,
        screen_height,
        1.,
    ))
}

#[allow(clippy::too_many_arguments)]
fn plant_shadows(
    commands: &mut Commands,
    quad: &Handle<Mesh>,
    materials: &mut Assets<ShadowMaterial>,
    handles: &mut ActorHandles,
    settings: &PaperSettings,
    maps: &SpriteMaps,
    root: Vec2,
    width: f32,
    height: f32,
    mirror: f32,
    support: Vec2,
) {
    let mut shadow = ShadowUniform {
        root: Vec4::new(root.x, root.y, width, height),
        anchor: Vec4::new(maps.anchor.x, maps.anchor.y, mirror, 0.),
        projection: Vec4::ZERO,
        contact: Vec4::new(root.x, root.y, support.x, support.y),
        options: Vec4::new(
            if settings.shadows { 1. } else { 0. },
            if settings.fog { 1. } else { 0. },
            0.64,
            0.,
        ),
    };
    spawn_shadow(
        commands,
        quad,
        materials,
        handles,
        maps,
        shadow,
        root - support,
        root + support,
    );

    // A light-facing alpha impostor is projected onto the map plane. Parallel
    // moon rays keep their length; the low point lamp widens the silhouette by
    // H/(H-h). Above-emitter pixels cannot cast a downward ground shadow.
    let moon_direction = MOON_SLOPE.normalize();
    let moon_length = (height * maps.anchor.y * MOON_SLOPE.length()).min(8.);
    shadow.anchor.w = 1.;
    shadow.projection = Vec4::new(moon_direction.x, moon_direction.y, 0., moon_length);
    shadow.options.z = 0.32;
    let (min, max) = projection_bounds(
        root,
        maps.anchor.x,
        width,
        mirror,
        moon_direction,
        moon_length,
        1.,
    );
    spawn_shadow(commands, quad, materials, handles, maps, shadow, min, max);

    let delta = root - LANTERN_ROOT;
    let distance = delta.length();
    if distance > 0.12 && distance < 8.5 {
        let direction = delta / distance;
        let caster_height = (height * maps.anchor.y).min(LAMP_HEIGHT * 0.88);
        let length = (distance * caster_height / (LAMP_HEIGHT - caster_height)).min(5.5);
        let strength = 0.64 / (1. + 0.12 * distance * distance)
            * (1. - ((distance - 5.5) / 3.).clamp(0., 1.));
        shadow.anchor.w = 2.;
        shadow.projection = Vec4::new(direction.x, direction.y, distance, length);
        shadow.options.z = strength;
        let (min, max) = projection_bounds(
            root,
            maps.anchor.x,
            width,
            mirror,
            direction,
            length,
            1. + length / distance,
        );
        spawn_shadow(commands, quad, materials, handles, maps, shadow, min, max);
    }
}

fn projection_bounds(
    root: Vec2,
    anchor_x: f32,
    width: f32,
    mirror: f32,
    direction: Vec2,
    length: f32,
    end_scale: f32,
) -> (Vec2, Vec2) {
    let across = Vec2::new(direction.y, -direction.x);
    let left = -anchor_x * width * mirror;
    let right = (1. - anchor_x) * width * mirror;
    let points = [
        root + across * left,
        root + across * right,
        root + direction * length + across * left * end_scale,
        root + direction * length + across * right * end_scale,
    ];
    let mut min = points[0];
    let mut max = points[0];
    for point in points.into_iter().skip(1) {
        min = min.min(point);
        max = max.max(point);
    }
    (min - Vec2::splat(0.18), max + Vec2::splat(0.18))
}

#[allow(clippy::too_many_arguments)]
fn spawn_shadow(
    commands: &mut Commands,
    quad: &Handle<Mesh>,
    materials: &mut Assets<ShadowMaterial>,
    handles: &mut ActorHandles,
    maps: &SpriteMaps,
    shadow: ShadowUniform,
    min: Vec2,
    max: Vec2,
) {
    let material = materials.add(ShadowMaterial {
        shadow,
        silhouette: maps.color.clone(),
    });
    let center = (min + max) * 0.5;
    let size = max - min;
    let screen = on_map(center.x, center.y);
    commands.spawn((
        Name::new(match shadow.anchor.w as u8 {
            0 => "二维·根部接触影",
            1 => "二维·月光剪影投影",
            _ => "二维·灯光剪影投影",
        }),
        Mesh2d(quad.clone()),
        WORLD,
        MeshMaterial2d(material.clone()),
        Transform::from_xyz(screen.x, screen.y, 1. + shadow.anchor.w * 0.01)
            .with_scale(Vec3::new(size.x, size.y * GROUND_SCALE, 1.)),
    ));
    handles.shadows.push(material);
}

fn update_settings(
    settings: Res<PaperSettings>,
    handles: Res<ActorHandles>,
    mut surfaces: ResMut<Assets<ActorMaterial>>,
    mut shadows: ResMut<Assets<ShadowMaterial>>,
) {
    let options = surface_options(&settings);
    for handle in &handles.surfaces {
        if surfaces.get(handle).is_some_and(|m| m.actor.options != options) {
            surfaces.get_mut(handle).unwrap().actor.options = options;
        }
    }
    let shadow_switch = if settings.shadows { 1. } else { 0. };
    let fog_switch = if settings.fog { 1. } else { 0. };
    for handle in &handles.shadows {
        if shadows.get(handle).is_some_and(|m| {
            m.shadow.options.x != shadow_switch || m.shadow.options.y != fog_switch
        }) {
            let material = shadows.get_mut(handle).unwrap();
            material.shadow.options.x = shadow_switch;
            material.shadow.options.y = fog_switch;
        }
    }
}
