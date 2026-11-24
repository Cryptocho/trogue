//! Art, reflections, HDR lamplight and fog all finish on the native pixel grid.
//! The window copies whole texels: light can grade smoothly without softening an outline.
use bevy::{
    core_pipeline::tonemapping::{DebandDither, Tonemapping},
    image::ImageSampler,
    prelude::*,
    render::{
        camera::{RenderTarget, ScalingMode},
        render_asset::RenderAssetUsages,
        render_resource::{AsBindGroup, Extent3d, ShaderRef, TextureDimension, TextureFormat, TextureUsages},
        view::RenderLayers,
    },
    sprite::{Material2d, Material2dPlugin},
    window::PrimaryWindow,
};

use super::{VIEW_HEIGHT, WORLD};
use crate::{Comparison, SceneCamera, SceneMode, lantern_bloom, pixel_scale};

pub(super) const SIZE: UVec2 = UVec2::new(512, 288);
const PRESENT: RenderLayers = RenderLayers::layer(1);

#[derive(Component)]
pub(crate) struct PixelOutput;

pub(super) struct PixelCanvas;

impl Plugin for PixelCanvas {
    fn build(&self, app: &mut App) {
        app.add_plugins(Material2dPlugin::<PixelFrame>::default())
            .add_systems(Startup, open)
            .add_systems(Update, fit);
    }
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct PixelFrame {
    #[texture(0)]
    frame: Handle<Image>,
}

impl Material2d for PixelFrame {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/present.wgsl".into()
    }
}

fn open(
    mut commands: Commands,
    comparison: Res<Comparison>,
    mut images: ResMut<Assets<Image>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<PixelFrame>>,
) {
    let mut target = Image::new_uninit(
        Extent3d { width: SIZE.x, height: SIZE.y, depth_or_array_layers: 1 },
        TextureDimension::D2,
        TextureFormat::Bgra8UnormSrgb,
        RenderAssetUsages::default(),
    );
    target.texture_descriptor.usage = TextureUsages::TEXTURE_BINDING | TextureUsages::RENDER_ATTACHMENT;
    target.sampler = ImageSampler::nearest();
    let frame = images.add(target);
    let mut world_view = commands.spawn((
        Name::new("青灯河湾 · 原生像素画布"),
        SceneCamera(SceneMode::Pixel),
        Camera2d,
        Camera {
            order: -1,
            is_active: comparison.mode == SceneMode::Pixel,
            target: RenderTarget::Image(frame.clone().into()),
            hdr: true,
            clear_color: ClearColorConfig::Custom(Color::BLACK),
            ..default()
        },
        Projection::Orthographic(OrthographicProjection {
            scaling_mode: ScalingMode::FixedVertical { viewport_height: VIEW_HEIGHT },
            scale: pixel_scale(comparison.zoom),
            ..OrthographicProjection::default_2d()
        }),
        Tonemapping::ReinhardLuminance,
        DebandDither::Disabled,
        Msaa::Off,
        WORLD,
    ));
    if comparison.bloom {
        world_view.insert(lantern_bloom());
    }
    commands.spawn((
        Name::new("整数像素呈现"),
        Mesh2d(meshes.add(Rectangle::new(SIZE.x as f32, SIZE.y as f32))),
        MeshMaterial2d(materials.add(PixelFrame { frame })),
        PRESENT,
    ));
    commands.spawn((
        Name::new("主窗口 · 不二次调色"),
        Camera2d,
        PixelOutput,
        Camera {
            is_active: comparison.mode == SceneMode::Pixel,
            clear_color: ClearColorConfig::Custom(Color::BLACK),
            ..default()
        },
        Tonemapping::None,
        DebandDither::Disabled,
        Msaa::Off,
        PRESENT,
    ));
}

fn fit(
    window: Single<&Window, With<PrimaryWindow>>,
    mut canvas: Single<&mut Transform, With<MeshMaterial2d<PixelFrame>>>,
    mut last: Local<Option<(UVec2, f32)>>,
) {
    let size = window.physical_size();
    let dpi = window.scale_factor();
    if *last == Some((size, dpi)) || size.min_element() == 0 { return; }
    *last = Some((size, dpi));
    // Center in physical pixels even at fractional desktop DPI and odd window
    // sizes. A sub-native window crops at 1:1 instead of inventing half pixels.
    let scale = (size / SIZE).min_element().max(1);
    let offset = (size.as_vec2() - (SIZE * scale).as_vec2()) * 0.5;
    let snap = (offset.floor() - offset) / dpi;
    canvas.scale = Vec3::new(scale as f32 / dpi, scale as f32 / dpi, 1.);
    canvas.translation = Vec3::new(snap.x, -snap.y, 0.);
}
