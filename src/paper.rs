//! A bank, six relightable cutouts and their water reflections on one pixel grid.
mod actors;
mod canvas;
mod ground;
mod stage;

use bevy::{asset::UntypedAssetId, prelude::*};
use bevy::render::view::RenderLayers;

use crate::{Comparison, SceneMode, desktop::{KeySide, ViewOptions}, forest};

pub(super) use canvas::PixelOutput;
const WORLD: RenderLayers = RenderLayers::layer(2);

#[derive(Resource)]
pub(super) struct PixelReady;

const VIEW_HEIGHT: f32 = canvas::SIZE.y as f32 / stage::PPM;
pub(super) const DETAIL_SCALE: f32 = 25. / 42.;
pub(super) const WIDE_SCALE: f32 = 25. / 22.;
const GROUND_SCALE: f32 = 0.568;

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
    reflections: bool,
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
            reflections: !options.no_reflections,
            key: match options.stone_key {
                Some(KeySide::Left) => -1.0,
                Some(KeySide::Right) => 1.0,
                None => 0.0,
            },
        };
        app.insert_resource(settings)
            .init_resource::<PaperAssets>()
            .add_plugins((canvas::PixelCanvas, ground::GroundPlugin, actors::ActorPlugin))
            .add_systems(PreStartup, stage::load)
            .add_systems(Update, (controls, finish_loading.run_if(resource_exists::<PaperAssets>)));
    }
}

fn finish_loading(
    mut commands: Commands,
    assets: Res<PaperAssets>,
    server: Res<AssetServer>,
    flat: Query<(), With<Mesh2d>>,
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
    info!("青灯河湾素材就绪：画布 {}×{}，二维绘制对象 {}。", canvas::SIZE.x, canvas::SIZE.y, flat.iter().len());
    commands.insert_resource(PixelReady);
    commands.remove_resource::<PaperAssets>();
}

fn controls(
    keys: Res<ButtonInput<KeyCode>>,
    comparison: Res<Comparison>,
    mut settings: ResMut<PaperSettings>,
) {
    if keys.just_pressed(KeyCode::KeyF) {
        settings.fog = !settings.fog;
    }
    if comparison.mode != SceneMode::Pixel {
        return;
    }
    if keys.just_pressed(KeyCode::KeyS) {
        settings.shadows = !settings.shadows;
    }
    if keys.just_pressed(KeyCode::KeyR) {
        settings.reflections = !settings.reflections;
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
