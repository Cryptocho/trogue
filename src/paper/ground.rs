use bevy::{
    prelude::*,
    render::render_resource::{AsBindGroup, ShaderRef},
    sprite::{Material2d, Material2dPlugin},
};

use super::{
    PaperSettings, WORLD,
    stage::{PPM, Stage},
};

pub(super) struct GroundPlugin;

impl Plugin for GroundPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins(Material2dPlugin::<BankMaterial>::default())
            .add_systems(Startup, place_bank)
            .add_systems(Update, relight.run_if(resource_changed::<PaperSettings>));
    }
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct BankMaterial {
    #[uniform(0)]
    lights: [Vec4; 2],
    #[texture(1)]
    color: Handle<Image>,
    #[texture(2)]
    field: Handle<Image>,
}

impl Material2d for BankMaterial {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/ground.wgsl".into()
    }
}

fn place_bank(
    mut commands: Commands,
    stage: Res<Stage>,
    settings: Res<PaperSettings>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<BankMaterial>>,
) {
    let size = stage.layout.map_size_px / PPM;
    commands.spawn((
        Name::new("青灯河湾 · 草岸与水底"),
        WORLD,
        Mesh2d(meshes.add(Rectangle::new(size.x, size.y))),
        MeshMaterial2d(materials.add(BankMaterial {
            lights: stage.lights(&settings),
            color: stage.bank.clone(),
            field: stage.field.clone(),
        })),
    ));
}

fn relight(
    stage: Res<Stage>,
    settings: Res<PaperSettings>,
    bank: Single<&MeshMaterial2d<BankMaterial>>,
    mut materials: ResMut<Assets<BankMaterial>>,
) {
    let lights = stage.lights(&settings);
    if materials.get(&bank.0).is_some_and(|m| m.lights != lights) {
        materials.get_mut(&bank.0).unwrap().lights = lights;
    }
}
