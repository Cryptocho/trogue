//! World lighting and Bloom finish on the art grid. Only palette lookup and
//! integer enlargement touch the native window; no high-res fog or overlays.
use bevy::{
    core_pipeline::tonemapping::{DebandDither, Tonemapping},
    image::{ImageLoaderSettings, ImageSampler},
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

use super::{PaperAssets, DETAIL_SCALE, VIEW_HEIGHT};
use crate::{SceneCamera, desktop::ViewOptions, lantern_bloom};

pub(super) const SIZE: UVec2 = UVec2::new(512, 288);
const PRESENT: RenderLayers = RenderLayers::layer(1);

pub(super) struct PixelCanvas;

impl Plugin for PixelCanvas {
    fn build(&self, app: &mut App) {
        app.add_plugins(Material2dPlugin::<IndexedFrame>::default())
            .add_systems(Startup, open)
            .add_systems(Update, fit);
    }
}

#[derive(Asset, TypePath, AsBindGroup, Clone)]
struct IndexedFrame {
    #[texture(0)]
    frame: Handle<Image>,
    #[texture(1)]
    colors: Handle<Image>,
}

impl Material2d for IndexedFrame {
    fn fragment_shader() -> ShaderRef {
        "gloam/paper/present.wgsl".into()
    }
}

fn open(
    mut commands: Commands,
    opts: Res<ViewOptions>,
    server: Res<AssetServer>,
    mut pending: ResMut<PaperAssets>,
    mut images: ResMut<Assets<Image>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<IndexedFrame>>,
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
        Name::new("苔径留灯 · 原生像素画布"),
        SceneCamera,
        Camera2d,
        Camera {
            order: -1,
            target: RenderTarget::Image(frame.clone().into()),
            hdr: true,
            clear_color: ClearColorConfig::Custom(Color::BLACK),
            ..default()
        },
        Projection::Orthographic(OrthographicProjection {
            scaling_mode: ScalingMode::FixedVertical { viewport_height: VIEW_HEIGHT },
            scale: if opts.detail { DETAIL_SCALE } else { 1. },
            ..OrthographicProjection::default_2d()
        }),
        Tonemapping::ReinhardLuminance,
        DebandDither::Disabled,
        Msaa::Off,
    ));
    if !opts.no_bloom {
        world_view.insert(lantern_bloom());
    }
    let colors: Handle<Image> = server.load_with_settings("gloam/paper/palette.png", |s: &mut ImageLoaderSettings| {
        s.is_srgb = true;
        s.sampler = ImageSampler::nearest();
    });
    pending.0.push(colors.id().untyped());
    commands.spawn((
        Name::new("整数像素呈现"),
        Mesh2d(meshes.add(Rectangle::new(SIZE.x as f32, SIZE.y as f32))),
        MeshMaterial2d(materials.add(IndexedFrame { frame, colors })),
        PRESENT,
    ));
    commands.spawn((
        Name::new("主窗口 · 不二次调色"),
        Camera2d,
        Camera { clear_color: ClearColorConfig::Custom(Color::BLACK), ..default() },
        Tonemapping::None,
        DebandDither::Disabled,
        Msaa::Off,
        PRESENT,
    ));
}

fn fit(
    window: Single<&Window, With<PrimaryWindow>>,
    mut canvas: Single<&mut Transform, With<MeshMaterial2d<IndexedFrame>>>,
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
