//! The same clearing on a 2D canvas. Map depth affects sorting, never size.
mod actors;
mod ground;

use bevy::{asset::UntypedAssetId, core_pipeline::tonemapping::Tonemapping, prelude::*, render::camera::ScalingMode};

use crate::{SceneCamera, desktop::{KeySide, SceneReady, ViewOptions}, forest, lantern_bloom};

const VIEW_HEIGHT: f32 = 11.70;
pub(super) const DETAIL_SCALE: f32 = 6.84 / VIEW_HEIGHT;
pub(super) const WIDE_SCALE: f32 = 13.25 / VIEW_HEIGHT;
const GROUND_SCALE: f32 = 0.568;

fn on_map(x: f32, z: f32) -> Vec2 {
    Vec2::new(x, -(z + 0.3) * GROUND_SCALE)
}

fn sort_key(z: f32) -> f32 {
    10.0 + z * 0.1
}

#[derive(Resource, Default)]
struct PaperAssets(Vec<UntypedAssetId>);

#[derive(Resource)]
struct PaperSettings {
    fog: bool,
    shadows: bool,
    flat_sprites: bool,
    key: f32,
}

pub(super) struct PaperPlugin;

impl Plugin for PaperPlugin {
    fn build(&self, app: &mut App) {
        let options = app.world().resource::<ViewOptions>();
        let settings = PaperSettings {
            fog: !options.clear_air,
            shadows: !options.no_shadows,
            flat_sprites: options.flat_sprites,
            key: match options.stone_key {
                Some(KeySide::Left) => -1.0,
                Some(KeySide::Right) => 1.0,
                None => 0.0,
            },
        };
        app.insert_resource(settings)
            .init_resource::<PaperAssets>()
            .add_plugins((ground::GroundPlugin, actors::ActorPlugin))
            .add_systems(Startup, camera)
            .add_systems(Update, (controls, finish_loading.run_if(resource_exists::<PaperAssets>)));
    }
}

fn camera(mut commands: Commands, opts: Res<ViewOptions>) {
    let mut entity = commands.spawn((
        Name::new("苔径留灯 · 正交画布"),
        SceneCamera,
        Camera2d,
        Camera { hdr: true, clear_color: ClearColorConfig::Custom(Color::BLACK), ..default() },
        Projection::Orthographic(OrthographicProjection {
            scaling_mode: ScalingMode::FixedVertical { viewport_height: VIEW_HEIGHT },
            scale: if opts.detail { DETAIL_SCALE } else { 1.0 },
            ..OrthographicProjection::default_2d()
        }),
        Tonemapping::AcesFitted,
        Msaa::Off,
    ));
    if !opts.no_bloom {
        entity.insert(lantern_bloom());
    }
}

fn finish_loading(
    mut commands: Commands,
    assets: Res<PaperAssets>,
    server: Res<AssetServer>,
    flat: Query<(), With<Mesh2d>>,
    solid: Query<(), With<Mesh3d>>,
    mut exit: EventWriter<AppExit>,
) {
    for &id in &assets.0 {
        match forest::ready(&server, id) {
            Ok(true) => (),
            Ok(false) => return,
            Err(error) => {
                error!("纯 2D 素材加载失败：{error}");
                exit.write(AppExit::error());
                return;
            }
        }
    }
    info!("苔径留灯 · 纯 2D 已就绪：Mesh2d={}，Mesh3d={}。", flat.iter().len(), solid.iter().len());
    commands.insert_resource(SceneReady);
    commands.remove_resource::<PaperAssets>();
}

fn controls(keys: Res<ButtonInput<KeyCode>>, mut settings: ResMut<PaperSettings>) {
    if keys.just_pressed(KeyCode::KeyF) {
        settings.fog = !settings.fog;
    }
    if keys.just_pressed(KeyCode::KeyS) {
        settings.shadows = !settings.shadows;
    }
    if keys.just_pressed(KeyCode::KeyN) {
        settings.flat_sprites = !settings.flat_sprites;
    }
    if keys.just_pressed(KeyCode::ArrowLeft) {
        settings.key = -1.0;
    } else if keys.just_pressed(KeyCode::ArrowRight) {
        settings.key = 1.0;
    } else if keys.just_pressed(KeyCode::Digit0) {
        settings.key = 0.0;
    }
}
