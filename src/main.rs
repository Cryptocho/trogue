mod desktop;
mod forest;
mod haze;
mod lens;
mod paper;

use bevy::{
    core_pipeline::{
        bloom::{Bloom, BloomCompositeMode, BloomPrefilter},
        tonemapping::Tonemapping,
    },
    input::mouse::{MouseScrollUnit, MouseWheel},
    pbr::{
        CascadeShadowConfigBuilder, DirectionalLightShadowMap, PointLightShadowMap,
        ScreenSpaceAmbientOcclusion, ScreenSpaceAmbientOcclusionQualityLevel,
    },
    prelude::*,
    render::{RenderPlugin, camera::Exposure},
    window::{MonitorSelection, PresentMode, PrimaryWindow, WindowMode, WindowResolution},
};
use desktop::{SceneReady, ViewOptions};

const LAMP: Vec3 = Vec3::new(1.5, 0.0, 0.83);
const CANOPY_FROM: Vec3 = Vec3::new(-9.5, 12.0, -6.0);
const CANOPY_TO: Vec3 = Vec3::new(-2.6, 0.0, -2.2);
const NORMAL_FOV: f32 = 32.0;
const DETAIL_FOV: f32 = 19.0;

#[derive(Clone, Copy, PartialEq, Eq)]
enum SceneMode {
    Hd,
    Pixel,
}

impl SceneMode {
    fn title(self) -> &'static str {
        match self {
            Self::Hd => "苔径留灯 · HD-2D",
            Self::Pixel => "青灯河湾 · 纯 2D 像素",
        }
    }
}

#[derive(Component)]
struct SceneCamera(SceneMode);

#[derive(Resource)]
struct Comparison {
    mode: SceneMode,
    // Share half-frustum height rather than field of view (perspective) or
    // scale (orthographic). Match the three authored zoom anchors.
    zoom: f32,
    bloom: bool,
}

fn frustum(fov: f32) -> f32 {
    (0.5 * fov.to_radians()).tan()
}

fn pixel_scale(zoom: f32) -> f32 {
    let normal = frustum(NORMAL_FOV);
    if zoom <= normal {
        let t = (zoom - frustum(DETAIL_FOV)) / (normal - frustum(DETAIL_FOV));
        paper::DETAIL_SCALE + t * (1. - paper::DETAIL_SCALE)
    } else {
        let t = (zoom - normal) / (frustum(36.) - normal);
        1. + t * (paper::WIDE_SCALE - 1.)
    }
}

fn main() -> AppExit {
    let options = match ViewOptions::parse() {
        Ok(options) => options,
        Err(error) => {
            eprintln!("{error}");
            return AppExit::error();
        }
    };
    let mode = if options.pure_2d { SceneMode::Pixel } else { SceneMode::Hd };
    let comparison = Comparison {
        mode,
        zoom: frustum(if options.detail { DETAIL_FOV } else { NORMAL_FOV }),
        bloom: !options.no_bloom,
    };
    let mut app = App::new();
    app.insert_resource(options)
        .insert_resource(comparison)
        .add_plugins(
            DefaultPlugins
                .set(WindowPlugin {
                    primary_window: Some(Window {
                        title: mode.title().into(),
                        name: Some("wanderstead.gloam".into()),
                        resolution: WindowResolution::new(2560., 1440.),
                        mode: WindowMode::BorderlessFullscreen(MonitorSelection::Primary),
                        present_mode: PresentMode::AutoVsync, // Selected against the native surface at startup.
                        ..default()
                    }),
                    ..default()
                })
                .set(RenderPlugin {
                    synchronous_pipeline_compilation: true,
                    ..default()
                }),
        )
        .add_plugins((desktop::DesktopView, paper::PaperPlugin))
        .add_systems(
            Update,
            (
                comparison_ready.run_if(not(resource_exists::<SceneReady>)),
                framing_controls.run_if(resource_exists::<SceneReady>),
                close_on_escape,
            ),
        );
    install_hd_scene(&mut app);
    app.run()
}

fn install_hd_scene(app: &mut App) {
    app.add_plugins(lens::LensPlugin)
        .insert_resource(bevy::pbr::DefaultOpaqueRendererMethod::deferred())
        .add_plugins(MaterialPlugin::<forest::LitSprite>::default())
        .add_plugins(MaterialPlugin::<haze::MistMaterial> {
            prepass_enabled: false,
            shadows_enabled: false,
            ..default()
        })
        .insert_resource(ClearColor(Color::srgb(0.16, 0.24, 0.32)))
        .insert_resource(AmbientLight {
            color: Color::srgb(0.61, 0.74, 0.96),
            brightness: 11.,
            ..default()
        })
        .insert_resource(DirectionalLightShadowMap { size: 2048 })
        .insert_resource(PointLightShadowMap { size: 512 })
        .add_systems(Startup, (stage_lights, forest::load, haze::spawn))
        .add_systems(
            Update,
            (
                forest::plant_when_loaded.run_if(resource_exists::<forest::Loading>),
                forest_atmosphere_controls,
            ),
        );
}

fn stage_lights(mut commands: Commands, options: Res<ViewOptions>, comparison: Res<Comparison>) {
    let cast = !options.no_shadows;
    let mut camera = commands.spawn((
        Name::new("林间镜头"),
        SceneCamera(SceneMode::Hd),
        Camera3d::default(),
        Camera {
            hdr: true,
            is_active: comparison.mode == SceneMode::Hd,
            ..default()
        },
        Projection::Perspective(PerspectiveProjection {
            fov: 2. * comparison.zoom.atan(),
            near: 0.2,
            far: 110.,
            ..default()
        }),
        Transform::from_xyz(0., 11.6, 16.5).looking_at(Vec3::new(0., 0., -0.3), Vec3::Y),
        Exposure { ev100: 7.25 },
        Tonemapping::AcesFitted,
        Msaa::Off,
        ScreenSpaceAmbientOcclusion {
            quality_level: ScreenSpaceAmbientOcclusionQualityLevel::Medium,
            constant_object_thickness: 0.18,
        },
        bevy::pbr::ScreenSpaceReflections {
            perceptual_roughness_threshold: 0.18,
            thickness: 0.13,
            linear_steps: 20,
            bisection_steps: 4,
            ..default()
        },
        DistanceFog {
            color: Color::srgb(0.30, 0.42, 0.55),
            directional_light_color: Color::NONE,
            falloff: FogFalloff::Linear {
                start: 23.5,
                end: 53.0,
            },
            ..default()
        },
    ));
    if !options.deep_focus {
        camera.insert(lens::TiltFocus::default());
    }
    if comparison.bloom {
        camera.insert(lantern_bloom());
    }

    commands.spawn((
        Name::new("穿林月光"),
        DirectionalLight {
            color: Color::srgb(0.59, 0.76, 1.0),
            illuminance: 710.,
            shadows_enabled: cast,
            shadow_depth_bias: 0.009,
            shadow_normal_bias: 0.68,
            ..default()
        },
        Transform::from_xyz(-10., 12., -5.).looking_at(Vec3::ZERO, Vec3::Y),
        CascadeShadowConfigBuilder {
            num_cascades: 2,
            minimum_distance: 5.,
            first_cascade_far_bound: 23.5,
            maximum_distance: 48.,
            ..default()
        }
        .build(),
    ));
    // Silver skylight entering through a break in the high canopy. It hits
    // the granite first, and the low mist reveals its direction.
    commands.spawn((
        Name::new("林冠缺口"),
        Transform::from_translation(CANOPY_FROM).looking_at(CANOPY_TO, Vec3::Y),
        SpotLight {
            color: Color::srgb(0.66, 0.83, 1.),
            intensity: 2_400_000.,
            range: 27.,
            inner_angle: 0.065,
            outer_angle: 0.255,
            shadows_enabled: cast,
            ..default()
        },
    ));
    commands.spawn((
        Name::new("玻璃罩内的灯芯"),
        Transform::from_translation(LAMP + Vec3::Y * 1.48),
        PointLight {
            color: Color::srgb(1., 0.78, 0.49),
            intensity: 37_000.,
            range: 9.,
            radius: 0.14,
            // The finite lantern glass softens edges as caster and receiver
            // separate. The distant moon keeps stable cascaded shadows.
            soft_shadows_enabled: true,
            // The emitting glass must not shadow its own filament. Exterior
            // objects (including the bracket and traveler) still cast shadows.
            shadow_map_near_z: 0.30,
            shadow_depth_bias: 0.008,
            shadow_normal_bias: 0.21,
            shadows_enabled: cast,
            ..default()
        },
    ));
    // The copper hood returns light down onto the wet verge. This emitter
    // shares the glass position rather than painting a glow onto the terrain.
    commands.spawn((
        Name::new("铜罩的暖光"),
        Transform::from_translation(LAMP + Vec3::Y * 1.47)
            .looking_at(LAMP + Vec3::new(0.54, 0., 0.70), Vec3::Y),
        SpotLight {
            color: Color::srgb(1., 0.82, 0.55),
            intensity: 73_000.,
            range: 7.4,
            inner_angle: 0.34,
            outer_angle: 1.10,
            radius: 0.14,
            soft_shadows_enabled: true,
            shadows_enabled: cast,
            shadow_depth_bias: 0.009,
            shadow_normal_bias: 0.30,
            shadow_map_near_z: 0.31,
            ..default()
        },
    ));
    // Controlled relighting: same color, distance, height and flux on either
    // side. This inspection light is never spawned in the normal forest.
    if let Some(side) = options.stone_key {
        let dx = match side {
            desktop::KeySide::Left => -2.4,
            desktop::KeySide::Right => 2.4,
        };
        let sample = forest::WAYSTONE_ROOT + Vec3::new(0., 0.90, -0.32);
        commands.spawn((
            Name::new("界石验光（非场景灯）"),
            Transform::from_translation(sample + Vec3::new(dx, 1.40, 1.45))
                .looking_at(sample, Vec3::Y),
            SpotLight {
                color: Color::WHITE,
                intensity: 53_000.,
                range: 6.2,
                radius: 0.12,
                inner_angle: 0.38,
                outer_angle: 0.67,
                shadows_enabled: cast,
                ..default()
            },
        ));
    }
}

fn lantern_bloom() -> Bloom {
    // Scatter the exposed HDR glass and its reflection, not the whole forest.
    Bloom {
        prefilter: BloomPrefilter {
            threshold: 0.82,
            threshold_softness: 0.31,
        },
        composite_mode: BloomCompositeMode::Additive,
        intensity: 0.12,
        low_frequency_boost: 0.23,
        high_pass_frequency: 0.78,
        ..Bloom::NATURAL
    }
}

fn forest_atmosphere_controls(
    mut commands: Commands,
    keys: Res<ButtonInput<KeyCode>>,
    camera: Single<(Entity, Has<lens::TiltFocus>), With<Camera3d>>,
    mut mist: Single<&mut Visibility, With<haze::GroundMist>>,
) {
    let (entity, focused) = *camera;
    if keys.just_pressed(KeyCode::KeyD) {
        if focused {
            commands.entity(entity).remove::<lens::TiltFocus>();
        } else {
            commands.entity(entity).insert(lens::TiltFocus::default());
        }
    }
    if keys.just_pressed(KeyCode::KeyF) {
        **mist = if **mist == Visibility::Hidden {
            Visibility::Visible
        } else {
            Visibility::Hidden
        };
    }
}

fn comparison_ready(
    mut commands: Commands,
    hd: Option<Res<forest::HdReady>>,
    pixel: Option<Res<paper::PixelReady>>,
) {
    if hd.is_some() && pixel.is_some() {
        commands.insert_resource(SceneReady);
        info!("两种画法已就绪；空格切换");
    }
}

fn close_on_escape(keys: Res<ButtonInput<KeyCode>>, mut exit: EventWriter<AppExit>) {
    if keys.just_pressed(KeyCode::Escape) {
        exit.write(AppExit::Success);
    }
}

fn framing_controls(
    mut commands: Commands,
    keys: Res<ButtonInput<KeyCode>>,
    mut wheel: EventReader<MouseWheel>,
    mut cameras: Query<(Entity, &SceneCamera, &mut Camera, &mut Projection)>,
    mut pixel_output: Single<&mut Camera, (With<paper::PixelOutput>, Without<SceneCamera>)>,
    mut window: Single<&mut Window, With<PrimaryWindow>>,
    mut comparison: ResMut<Comparison>,
) {
    let switch = keys.just_pressed(KeyCode::Space);
    let glow = keys.just_pressed(KeyCode::KeyB);
    if switch {
        comparison.mode = match comparison.mode {
            SceneMode::Hd => SceneMode::Pixel,
            SceneMode::Pixel => SceneMode::Hd,
        };
        pixel_output.is_active = comparison.mode == SceneMode::Pixel;
        window.title = comparison.mode.title().into();
        info!("切换画法：{}", comparison.mode.title());
    }
    if glow {
        comparison.bloom = !comparison.bloom;
    }
    let steps: f32 = wheel
        .read()
        .map(|e| match e.unit {
            MouseScrollUnit::Line => e.y,
            MouseScrollUnit::Pixel => e.y / 48.,
        })
        .sum();
    if steps != 0. {
        comparison.zoom = (comparison.zoom * (-0.11 * steps).exp())
            .clamp(frustum(DETAIL_FOV), frustum(36.));
    }
    if !switch && !glow && steps == 0. {
        return;
    }
    for (entity, mode, mut camera, mut projection) in &mut cameras {
        if switch {
            camera.is_active = mode.0 == comparison.mode;
        }
        if glow {
            let mut camera = commands.entity(entity);
            if comparison.bloom {
                camera.insert(lantern_bloom());
            } else {
                camera.remove::<Bloom>();
            }
        }
        if steps != 0. {
            match &mut *projection {
                Projection::Perspective(p) => p.fov = 2. * comparison.zoom.atan(),
                Projection::Orthographic(p) => p.scale = pixel_scale(comparison.zoom),
                Projection::Custom(_) => (),
            }
        }
    }
}
