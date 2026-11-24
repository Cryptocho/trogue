use bevy::{
    prelude::*,
    render::render_resource::{AsBindGroup, ShaderRef},
    sprite::{AlphaMode2d, Material2d, Material2dPlugin},
};

use super::{
    GROUND_SCALE, PaperSettings, WORLD, sort_key,
    stage::{Kind, PPM, Stage, WATER_DROP},
};

pub(super) struct ActorPlugin;

impl Plugin for ActorPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins((
            Material2dPlugin::<Ink>::default(),
            Material2dPlugin::<Footprint>::default(),
            Material2dPlugin::<LampAir>::default(),
        ))
        .add_systems(Startup, place)
        .add_systems(
            Update,
            apply_settings.run_if(resource_changed::<PaperSettings>),
        );
    }
}

/// The upright and virtual image share exactly the same art, normals, emitter,
/// and light uniforms. Only their plane, vertical orientation and blend differ.
#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct Ink {
    #[uniform(0)]
    data: [Vec4; 5],
    #[texture(1)]
    color: Handle<Image>,
    #[texture(2)]
    normal: Handle<Image>,
    #[texture(3)]
    emission: Option<Handle<Image>>,
    #[texture(4)]
    water: Handle<Image>,
}

impl Material2d for Ink {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/actor.wgsl".into()
    }
    fn alpha_mode(&self) -> AlphaMode2d {
        AlphaMode2d::Blend
    }
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct Footprint {
    // xy: boot/root within the ellipse's unit bounds; z: dark core width.
    #[uniform(0)]
    support: Vec4,
    #[texture(1)]
    water: Handle<Image>,
}

impl Material2d for Footprint {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/shadow.wgsl".into()
    }
    fn alpha_mode(&self) -> AlphaMode2d {
        AlphaMode2d::Blend
    }
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct LampAir {
    #[uniform(0)]
    scatter: LinearRgba,
}

impl Material2d for LampAir {
    fn fragment_shader() -> ShaderRef {
        "gloam/riverside/lamplight.wgsl".into()
    }
    fn alpha_mode(&self) -> AlphaMode2d {
        AlphaMode2d::Blend
    }
}

#[derive(Component)]
enum Layer {
    Shadow,
    Reflection,
    Air,
}

impl Layer {
    fn visibility(&self, settings: &PaperSettings) -> Visibility {
        let on = match self {
            Self::Reflection => settings.reflections,
            Self::Shadow => settings.shadows,
            Self::Air => settings.fog,
        };
        if on {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        }
    }
}

#[derive(Resource)]
struct Inks(Vec<Handle<Ink>>);

fn place(
    mut commands: Commands,
    stage: Res<Stage>,
    settings: Res<PaperSettings>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut inks: ResMut<Assets<Ink>>,
    mut prints: ResMut<Assets<Footprint>>,
    mut halos: ResMut<Assets<LampAir>>,
) {
    let quad = meshes.add(Rectangle::new(1., 1.));
    let [lamp, key] = stage.lights(&settings);
    let mut handles = Vec::with_capacity(stage.layout.actors.len());
    for object in &stage.layout.actors {
        let art = &stage.art[object.kind as usize];
        let root = object.root();
        let foot = object.canvas_foot();
        let h = object.height / PPM;
        let size = Vec2::new(h * art.aspect, h);
        let flip = if object.flip { -1. } else { 1. };
        let anchor = art.anchor;
        let material = inks.add(Ink {
            data: [
                Vec4::new(root.x, root.y, size.x, size.y),
                Vec4::new(anchor.x, anchor.y, flip, f32::from(art.emission.is_some())),
                lamp,
                key,
                Vec4::new(
                    f32::from(settings.flat_sprites),
                    f32::from(object.kind == Kind::Stone),
                    f32::from(matches!(object.kind, Kind::Stone | Kind::Traveler)),
                    0.,
                ),
            ],
            color: art.color.clone(),
            normal: art.normal.clone(),
            emission: art.emission.clone(),
            water: stage.field.clone(),
        });
        let offset = Vec2::new((0.5 - anchor.x) * size.x * flip, (anchor.y - 0.5) * size.y);
        let order = sort_key(root.y);
        commands.spawn((
            Name::new(object.kind.label()),
            WORLD,
            Mesh2d(quad.clone()),
            MeshMaterial2d(material.clone()),
            Transform::from_translation((foot + offset).extend(order)).with_scale(size.extend(1.)),
        ));
        // Mirror about the horizontal water plane below the bank. The shader
        // discards every fragment outside the water, including the far shore.
        let mirrored = foot + Vec2::new(offset.x, -offset.y - 2. * WATER_DROP);
        commands.spawn((
            Layer::Reflection,
            Layer::Reflection.visibility(&settings),
            WORLD,
            Mesh2d(quad.clone()),
            MeshMaterial2d(material.clone()),
            Transform::from_translation(mirrored.extend(0.43 + root.y * 0.009))
                .with_scale(size.extend(1.)),
        ));
        handles.push(material);

        let (across, depth) = match object.kind {
            Kind::Round => (0.64, 0.39),
            Kind::Spruce => (0.70, 0.34),
            Kind::Cypress => (1.08, 0.60),
            Kind::Stone => (1.19, 0.45),
            Kind::Traveler => (1.18, 0.45),
            Kind::Lamp => (0.73, 0.39),
        };
        let footprint_size = Vec2::new(across, depth) * size.x;
        let away = Vec2::new(root.x - lamp.x, (lamp.z - root.y) * GROUND_SCALE).normalize_or_zero();
        let bias = away * footprint_size * 0.12;
        let uv = Vec2::splat(0.5) + Vec2::new(-bias.x, bias.y) / footprint_size;
        commands.spawn((
            Layer::Shadow,
            Layer::Shadow.visibility(&settings),
            WORLD,
            Mesh2d(quad.clone()),
            MeshMaterial2d(prints.add(Footprint {
                support: Vec4::new(uv.x, uv.y, 0.47, 0.),
                water: stage.field.clone(),
            })),
            Transform::from_translation((foot + bias).extend(1.2 + root.y * 0.001))
                .with_scale(footprint_size.extend(1.)),
        ));
        if object.kind == Kind::Lamp {
            // This small volume of warm air lives on the glass, not at the
            // pole foot or in screen space. Nearer opaque sprites can cover it.
            let glass = foot + Vec2::new(lamp.x - root.x, lamp.y);
            commands.spawn((
                Name::new("灯纱边的暖雾"),
                Layer::Air,
                Layer::Air.visibility(&settings),
                WORLD,
                Mesh2d(quad.clone()),
                MeshMaterial2d(halos.add(LampAir {
                    scatter: LinearRgba::new(0.78, 0.337, 0.056, 0.24),
                })),
                Transform::from_translation(glass.extend(order - 0.001))
                    .with_scale(Vec3::new(3.25, 3.05, 1.)),
            ));
        }
    }
    commands.insert_resource(Inks(handles));
}

fn apply_settings(
    settings: Res<PaperSettings>,
    stage: Res<Stage>,
    handles: Res<Inks>,
    mut materials: ResMut<Assets<Ink>>,
    mut layers: Query<(&Layer, &mut Visibility)>,
) {
    let [lamp, key] = stage.lights(&settings);
    let flat = f32::from(settings.flat_sprites);
    for h in &handles.0 {
        if materials
            .get(h)
            .is_some_and(|m| m.data[2] != lamp || m.data[3] != key || m.data[4].x != flat)
        {
            let m = materials.get_mut(h).unwrap();
            m.data[2] = lamp;
            m.data[3] = key;
            m.data[4].x = flat;
        }
    }
    for (layer, mut v) in &mut layers {
        v.set_if_neq(layer.visibility(&settings));
    }
}
