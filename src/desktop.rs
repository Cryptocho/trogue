use std::{
    path::PathBuf,
    time::{SystemTime, UNIX_EPOCH},
};

use bevy::{
    prelude::*,
    render::{
        RenderApp,
        renderer::{RenderAdapter, RenderAdapterInfo, RenderInstance},
        view::screenshot::{Screenshot, ScreenshotCaptured},
    },
    window::{PresentMode, PrimaryWindow, RawHandleWrapper},
    winit::WinitWindows,
};

/// Present after both renderers have all their scene assets.
#[derive(Resource)]
pub struct SceneReady;

#[derive(Resource, Default)]
pub struct ViewOptions {
    pub pure_2d: bool,
    pub detail: bool,
    pub clear_air: bool,
    pub deep_focus: bool,
    pub no_bloom: bool,
    pub no_shadows: bool,
    pub no_reflections: bool,
    pub flat_sprites: bool,
    pub stone_key: Option<KeySide>,
    capture: Option<PathBuf>,
}

#[derive(Copy, Clone)]
pub enum KeySide {
    Left,
    Right,
}

impl ViewOptions {
    pub fn parse() -> Result<Self, String> {
        let mut options = Self::default();
        let mut args = std::env::args_os().skip(1);
        while let Some(arg) = args.next() {
            if arg == "--2d" {
                options.pure_2d = true;
            } else if arg == "--detail" {
                options.detail = true;
            } else if arg == "--clear-air" {
                options.clear_air = true;
            } else if arg == "--deep-focus" {
                options.deep_focus = true;
            } else if arg == "--no-reflections" {
                options.no_reflections = true;
            } else if arg == "--no-bloom" {
                options.no_bloom = true;
            } else if arg == "--no-shadows" {
                options.no_shadows = true;
            } else if arg == "--flat-sprites" {
                options.flat_sprites = true;
            } else if arg == "--stone-key" {
                options.stone_key = Some(match args.next().as_deref().and_then(|s| s.to_str()) {
                    Some("left") => KeySide::Left,
                    Some("right") => KeySide::Right,
                    _ => return Err("--stone-key 需要 left 或 right".into()),
                });
            } else if arg == "--capture" {
                let path: PathBuf = args.next().ok_or("--capture 需要 PNG 文件名")?.into();
                if path
                    .extension()
                    .is_none_or(|ext| !ext.eq_ignore_ascii_case("png"))
                {
                    return Err("截图路径需要以 .png 结尾".into());
                }
                options.capture = Some(path);
            } else {
                return Err(format!(
                    "未知参数 {arg:?}；支持 --2d、--detail、--clear-air、--deep-focus、--no-bloom、--no-shadows、--no-reflections、--flat-sprites、--stone-key left|right、--capture 路径.png"
                ));
            }
        }
        Ok(options)
    }
}

pub struct DesktopView;

impl Plugin for DesktopView {
    fn build(&self, app: &mut App) {
        app.add_systems(
            Update,
            configure_surface.run_if(resource_exists::<RenderInstance>),
        );
        let capture = app.world().resource::<ViewOptions>().capture.clone();
        if let Some(path) = capture {
            app.insert_resource(CaptureRun {
                path,
                warm: 0.,
                warm_frames: 0,
                seconds: 0.,
                ms: Vec::with_capacity(768),
                always_focused: true,
                mode: None,
            });
            app.add_systems(
                Update,
                sample_and_capture
                    .run_if(resource_exists::<CaptureRun>.and(resource_exists::<SceneReady>)),
            );
        } else {
            app.add_systems(
                Update,
                manual_capture.run_if(resource_exists::<SceneReady>),
            );
        }
    }
    fn finish(&self, app: &mut App) {
        let instance = app
            .sub_app(RenderApp)
            .world()
            .resource::<RenderInstance>()
            .clone();
        app.insert_resource(instance);
    }
}

fn configure_surface(
    mut commands: Commands,
    mut windows: Query<(Entity, &mut Window, &RawHandleWrapper), With<PrimaryWindow>>,
    native: NonSend<WinitWindows>,
    instance: Res<RenderInstance>,
    adapter: Res<RenderAdapter>,
    info: Res<RenderAdapterInfo>,
    mut exit: EventWriter<AppExit>,
) {
    let Ok((id, mut window, handles)) = windows.single_mut() else {
        return;
    };
    let Some(winit_window) = native.get_window(id) else {
        return;
    };
    let backend = handles.get_window_handle();
    if cfg!(target_os = "linux")
        && !matches!(backend, raw_window_handle::RawWindowHandle::Wayland(_))
    {
        error!("需要 Wayland 窗口，实际为 {backend:?}");
        exit.write(AppExit::error());
        return;
    }
    if info.device_type == wgpu_types::DeviceType::Cpu {
        error!("需要硬件 GPU，实际为 {}", info.name);
        exit.write(AppExit::error());
        return;
    }
    // wgpu 24 rejects unsupported explicit present modes. Probe the same
    // window/adapter before requesting Mailbox; AutoVsync is already valid.
    match instance.create_surface(&**winit_window) {
        Ok(surface) => {
            if surface
                .get_capabilities(&adapter)
                .present_modes
                .contains(&wgpu_types::PresentMode::Mailbox)
            {
                window.present_mode = PresentMode::Mailbox;
            }
        }
        Err(e) => warn!("无法查询显示模式，保持 AutoVsync：{e}"),
    }
    info!(
        "原生窗口 {backend:?}；{:?}，DPI {}，{:?}；GPU {} / {} {} / {:?}",
        window.resolution.physical_size(),
        window.resolution.scale_factor(),
        window.present_mode,
        info.name,
        info.driver,
        info.driver_info,
        info.backend
    );
    commands.remove_resource::<RenderInstance>();
}

#[derive(Resource)]
struct CaptureRun {
    path: PathBuf,
    warm: f64,
    warm_frames: u32,
    seconds: f64,
    ms: Vec<f64>,
    always_focused: bool,
    mode: Option<crate::SceneMode>,
}

fn sample_and_capture(
    mut commands: Commands,
    time: Res<Time<Real>>,
    mut run: ResMut<CaptureRun>,
    window: Single<(Entity, &Window), With<PrimaryWindow>>,
    cameras: Query<(&Camera, &crate::SceneCamera)>,
    native: NonSend<WinitWindows>,
) {
    let Some((camera, mode)) = cameras.iter().find(|(camera, _)| camera.is_active) else {
        return;
    };
    if run.mode != Some(mode.0) {
        run.mode = Some(mode.0);
        run.warm = 0.;
        run.warm_frames = 0;
        run.seconds = 0.;
        run.ms.clear();
        run.always_focused = true;
    }
    let dt = time.delta_secs_f64();
    if run.warm < 2. || run.warm_frames < 32 {
        run.warm += dt;
        run.warm_frames += 1;
        return;
    }
    let (window_id, window) = *window;
    run.always_focused &= window.focused;
    run.seconds += dt;
    run.ms.push(dt * 1000.);
    if run.seconds < 5. {
        return;
    }
    run.ms.sort_unstable_by(f64::total_cmp);
    let n = run.ms.len();
    let p95 = (n * 95).div_ceil(100) - 1;
    let os_window = native.get_window(window_id);
    let refresh = os_window
        .and_then(|w| w.current_monitor())
        .and_then(|m| m.refresh_rate_millihertz());
    let minimized = os_window.and_then(|w| w.is_minimized());
    info!(
        "稳定采样（截图前） frames={n} seconds={:.3} fps={:.1} median_ms={:.2} p95_ms={:.2} focused_all={} minimized={minimized:?} refresh_mHz={refresh:?} scene_pixels={:?} window_pixels={:?} present={:?}",
        run.seconds,
        n as f64 / run.seconds,
        run.ms[n / 2],
        run.ms[p95],
        run.always_focused,
        camera.physical_viewport_size(),
        window.physical_size(),
        window.present_mode
    );
    request(&mut commands, std::mem::take(&mut run.path), true);
    commands.remove_resource::<CaptureRun>();
}

fn manual_capture(mut commands: Commands, keys: Res<ButtonInput<KeyCode>>) {
    if keys.just_pressed(KeyCode::F12) {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis();
        request(
            &mut commands,
            format!("captures/gloam-{now}.png").into(),
            false,
        );
    }
}

fn request(commands: &mut Commands, path: PathBuf, quit: bool) {
    commands.spawn(Screenshot::primary_window()).observe(
        move |event: Trigger<ScreenshotCaptured>, mut exit: EventWriter<AppExit>| {
            let save = || -> Result<(), Box<dyn std::error::Error>> {
                if let Some(dir) = path.parent().filter(|dir| !dir.as_os_str().is_empty()) {
                    std::fs::create_dir_all(dir)?;
                }
                // HDR window alpha is luminance, not coverage. Save opaque RGB.
                event
                    .event()
                    .0
                    .clone()
                    .try_into_dynamic()?
                    .into_rgb8()
                    .save(&path)?;
                Ok(())
            };
            match save() {
                Ok(()) => {
                    info!("窗口截图已保存：{}", path.display());
                    if quit {
                        exit.write(AppExit::Success);
                    }
                }
                Err(e) => {
                    error!("保存截图失败：{}：{e}", path.display());
                    if quit {
                        exit.write(AppExit::error());
                    }
                }
            }
        },
    );
}
