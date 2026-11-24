//! 世界空间倾斜焦面：主透明通道之后、Bloom 之前的单次 HDR 景深。
//!
//! Add `LensPlugin` after `DefaultPlugins` and attach `TiltFocus` to the perspective
//! camera. Removing that component disables the pass; do not also use Bevy DOF.
//! The opaque/alpha-mask depth prepass deliberately excludes transparent mist.

use bevy::{
    core_pipeline::{
        core_3d::graph::{Core3d, Node3d},
        fullscreen_vertex_shader::fullscreen_shader_vertex_state,
        prepass::{DepthPrepass, ViewPrepassTextures},
    },
    ecs::query::QueryItem,
    prelude::*,
    render::{
        Render, RenderApp, RenderSet,
        extract_component::{ExtractComponent, ExtractComponentPlugin},
        render_graph::{
            NodeRunError, RenderGraphApp, RenderGraphContext, RenderLabel, ViewNode, ViewNodeRunner,
        },
        render_resource::{
            binding_types::{sampler, texture_2d, texture_depth_2d, uniform_buffer},
            *,
        },
        renderer::{RenderContext, RenderDevice, RenderQueue},
        view::ViewTarget,
    },
};

const SHADER_PATH: &str = "gloam/light/tilt_focus.wgsl";
const REFERENCE_HEIGHT: f32 = 1440.0;
const REFERENCE_FOV: f32 = 32.0;
// Keep the fixed 20-tap kernel dense enough at the detail lens as well.
const MAX_REFERENCE_RADIUS: f32 = 12.0;

pub struct LensPlugin;

/// 相机上的景深开关与集中调节参数。Requires a perspective camera and `Msaa::Off`.
///
/// Radii and blur strength are specified at 1440 physical pixels / 32° vertical
/// FOV. The pass scales with physical viewport height and optical magnification,
/// capped at 12 reference pixels to bound the fixed kernel's sampling footprint.
/// It does not depend on window DPI, elapsed time, or screen Y; no jitter is used.
#[derive(Component, Clone, Copy)]
#[require(DepthPrepass)]
pub struct TiltFocus {
    /// A world-space point on the focal plane, in metres.
    pub focus_point: Vec3,
    /// World-space plane normal. Its sign is oriented away from the camera.
    pub plane_normal: Vec3,
    /// Half-width of the fully sharp slab, measured perpendicular to the plane.
    pub focus_half_width: f32,
    /// Smooth shoulder outside the sharp slab, in metres; not another hard band.
    pub focus_transition: f32,
    /// Reference pixel radius per metre outside the focal slab, at focal depth.
    pub blur_per_meter: f32,
    /// Maximum foreground blur radius, in reference pixels (not diameter).
    pub near_radius: f32,
    /// Maximum background blur radius, in reference pixels (not diameter).
    pub far_radius: f32,
    /// Geometry-depth tolerance, in metres, before rejecting another surface.
    pub edge_tolerance: f32,
    /// Finite depth used for clear/sky pixels and extremely distant geometry.
    pub max_depth: f32,
}

impl Default for TiltFocus {
    fn default() -> Self {
        Self {
            focus_point: Vec3::new(0.0, 0.65, 0.05),
            // About 9° off the fixed camera's axial depth plane, with a slight
            // sideways tilt. Traveler, lamp, waystone, and their footing share
            // the sharp slab; a tall foreground bough does not become sharp
            // merely because it reaches the same screen row as their faces.
            plane_normal: Vec3::new(0.035, -0.43, -0.902),
            focus_half_width: 1.2,
            focus_transition: 0.8,
            blur_per_meter: 1.85,
            near_radius: 6.0,
            far_radius: 9.5,
            edge_tolerance: 0.35,
            max_depth: 80.0,
        }
    }
}

// Six vec4s keep the CPU/WGSL layout explicit. Transforming the world plane
// once on the CPU avoids a matrix-vector multiply for every blur tap.
#[derive(Component)]
pub struct TiltFocusUniform([Vec4; 6]);

impl ExtractComponent for TiltFocus {
    type QueryData = (&'static Self, &'static Camera, &'static GlobalTransform);
    type QueryFilter = With<Camera3d>;
    type Out = TiltFocusUniform;

    fn extract_component(
        (settings, camera, transform): QueryItem<'_, Self::QueryData>,
    ) -> Option<Self::Out> {
        let rect = camera.physical_viewport_rect()?;
        let size = rect.size().as_vec2();
        let clip = camera.clip_from_view();
        // This scene zooms by changing perspective FOV. Orthographic/custom
        // non-perspective projections must not be decoded as reverse-Z depth.
        if !camera.is_active || size.min_element() <= 0.0 || clip.w_axis.w != 0.0 {
            return None;
        }
        let world_from_view = transform.compute_matrix();
        let forward = -world_from_view.z_axis.truncate().normalize();
        let mut normal = settings.plane_normal.try_normalize()?;
        if normal.dot(forward) < 0.0 {
            normal = -normal;
        }
        let plane_world = normal.extend(-normal.dot(settings.focus_point));
        let plane_view = world_from_view.transpose() * plane_world;
        let focal_depth = (settings.focus_point - transform.translation())
            .dot(forward)
            .max(0.01);
        let magnification = clip.y_axis.y.abs() * (REFERENCE_FOV.to_radians() * 0.5).tan();
        let resolution_scale = size.y / REFERENCE_HEIGHT;
        let pixel_scale = resolution_scale * magnification;
        let radius_budget = MAX_REFERENCE_RADIUS * resolution_scale;

        Some(TiltFocusUniform([
            plane_view,
            // Also retain the perspective off-axis terms, rather than assuming
            // that the principal ray is exactly in the middle of the viewport.
            Vec4::new(
                clip.x_axis.x.recip(),
                clip.y_axis.y.recip(),
                clip.z_axis.x,
                clip.z_axis.y,
            ),
            // Perspective axial depth = projection[3][2] / (raw + [2][2]).
            Vec4::new(
                clip.w_axis.z,
                clip.z_axis.z,
                settings.max_depth.max(0.01),
                focal_depth,
            ),
            rect.min.as_vec2().extend(size.x).extend(size.y),
            Vec4::new(
                settings.focus_half_width.max(0.0),
                settings.focus_transition.max(0.001),
                settings.blur_per_meter.max(0.0) * pixel_scale,
                (settings.near_radius.max(0.0) * pixel_scale).min(radius_budget),
            ),
            Vec4::new(
                (settings.far_radius.max(0.0) * pixel_scale).min(radius_budget),
                settings.edge_tolerance.max(0.001),
                0.0,
                0.0,
            ),
        ]))
    }
}

impl Plugin for LensPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins(ExtractComponentPlugin::<TiltFocus>::default());
        let Some(render_app) = app.get_sub_app_mut(RenderApp) else {
            return;
        };
        render_app
            .init_resource::<SpecializedRenderPipelines<LensPipeline>>()
            .add_systems(Render, prepare_lens.in_set(RenderSet::PrepareBindGroups))
            .add_render_graph_node::<ViewNodeRunner<LensNode>>(Core3d, TiltFocusPass)
            .add_render_graph_edges(
                Core3d,
                (
                    Node3d::EndMainPass,
                    Node3d::MotionBlur,
                    TiltFocusPass,
                    Node3d::Bloom,
                ),
            );
    }

    fn finish(&self, app: &mut App) {
        if let Some(render_app) = app.get_sub_app_mut(RenderApp) {
            render_app.init_resource::<LensPipeline>();
        }
    }
}

#[derive(Debug, Hash, PartialEq, Eq, Clone, RenderLabel)]
struct TiltFocusPass;

#[derive(Resource)]
struct LensPipeline {
    layout: BindGroupLayout,
    sampler: Sampler,
    shader: Handle<Shader>,
}

impl FromWorld for LensPipeline {
    fn from_world(world: &mut World) -> Self {
        let device = world.resource::<RenderDevice>();
        let layout = device.create_bind_group_layout(
            "tilt-focus layout",
            &BindGroupLayoutEntries::sequential(
                ShaderStages::FRAGMENT,
                (
                    texture_2d(TextureSampleType::Float { filterable: true }),
                    sampler(SamplerBindingType::Filtering),
                    texture_depth_2d(),
                    uniform_buffer::<[Vec4; 6]>(false),
                ),
            ),
        );
        let sampler = device.create_sampler(&SamplerDescriptor {
            label: Some("tilt-focus bilinear clamp"),
            min_filter: FilterMode::Linear,
            mag_filter: FilterMode::Linear,
            ..default()
        });
        let shader = world.load_asset(SHADER_PATH);
        Self {
            layout,
            sampler,
            shader,
        }
    }
}

impl SpecializedRenderPipeline for LensPipeline {
    type Key = TextureFormat;

    fn specialize(&self, format: Self::Key) -> RenderPipelineDescriptor {
        RenderPipelineDescriptor {
            label: Some("world-plane tilt focus".into()),
            layout: vec![self.layout.clone()],
            vertex: fullscreen_shader_vertex_state(),
            fragment: Some(FragmentState {
                shader: self.shader.clone(),
                shader_defs: vec![],
                entry_point: "fragment".into(),
                targets: vec![Some(ColorTargetState {
                    format,
                    blend: None,
                    write_mask: ColorWrites::ALL,
                })],
            }),
            primitive: default(),
            depth_stencil: None,
            multisample: default(),
            push_constant_ranges: vec![],
            zero_initialize_workgroup_memory: false,
        }
    }
}

// Cache both ping-pong sources. Zoom changes write only the existing per-view
// uniform; static frames neither upload it again nor rebuild bind groups.
#[derive(Component)]
struct PreparedLens {
    sources: [TextureViewId; 2],
    depth: TextureViewId,
    uniform: UniformBuffer<[Vec4; 6]>,
    bind_groups: [BindGroup; 2],
    pipeline: CachedRenderPipelineId,
}

fn prepare_lens(
    mut commands: Commands,
    device: Res<RenderDevice>,
    pipeline_cache: Res<PipelineCache>,
    pipeline: Res<LensPipeline>,
    mut pipelines: ResMut<SpecializedRenderPipelines<LensPipeline>>,
    queue: Res<RenderQueue>,
    mut views: Query<(
        Entity,
        &ViewTarget,
        &ViewPrepassTextures,
        &Msaa,
        &TiltFocusUniform,
        Option<&mut PreparedLens>,
    )>,
) {
    for (entity, target, prepass, msaa, settings, mut cached) in &mut views {
        if *msaa != Msaa::Off {
            continue;
        }
        let Some(depth) = prepass.depth_view() else {
            continue;
        };
        let sources = [target.main_texture_view(), target.main_texture_other_view()];
        let source_ids = sources.map(TextureView::id);
        if let Some(old) = cached.as_mut()
            && (old.sources == source_ids || old.sources == [source_ids[1], source_ids[0]])
            && old.depth == depth.id()
        {
            if old.uniform.get() != &settings.0 {
                old.uniform.set(settings.0);
                old.uniform.write_buffer(&device, &queue);
            }
            continue;
        }
        let mut uniform = UniformBuffer::from(settings.0);
        uniform.write_buffer(&device, &queue);
        let bind_groups = sources.map(|source| {
            device.create_bind_group(
                "tilt-focus cached view bindings",
                &pipeline.layout,
                &BindGroupEntries::sequential((
                    source,
                    &pipeline.sampler,
                    depth,
                    uniform.binding().unwrap(),
                )),
            )
        });
        let prepared = PreparedLens {
            sources: source_ids,
            depth: depth.id(),
            uniform,
            bind_groups,
            pipeline: pipelines.specialize(
                &pipeline_cache,
                &pipeline,
                target.main_texture_format(),
            ),
        };
        if let Some(mut old) = cached {
            *old = prepared;
        } else {
            commands.entity(entity).insert(prepared);
        }
    }
}

#[derive(Default)]
struct LensNode;

impl ViewNode for LensNode {
    type ViewQuery = (
        &'static ViewTarget,
        &'static ViewPrepassTextures,
        &'static PreparedLens,
        &'static Msaa,
        &'static TiltFocusUniform,
    );

    fn run<'w>(
        &self,
        _: &mut RenderGraphContext,
        render_context: &mut RenderContext<'w>,
        (target, prepass, prepared, msaa, _): QueryItem<'w, Self::ViewQuery>,
        world: &'w World,
    ) -> Result<(), NodeRunError> {
        if *msaa != Msaa::Off || prepass.depth_view().map(TextureView::id) != Some(prepared.depth) {
            return Ok(());
        }
        let Some(pipeline) = world
            .resource::<PipelineCache>()
            .get_render_pipeline(prepared.pipeline)
        else {
            return Ok(());
        };
        let source_id = target.main_texture_view().id();
        let group = if source_id == prepared.sources[0] {
            &prepared.bind_groups[0]
        } else if source_id == prepared.sources[1] {
            &prepared.bind_groups[1]
        } else {
            return Ok(());
        };

        // Do not flip the target until every dependency is ready: a skipped
        // async shader compilation must leave the original HDR image intact.
        let output = target.post_process_write();
        let attachments = [Some(RenderPassColorAttachment {
            view: output.destination,
            resolve_target: None,
            ops: Operations::default(),
        })];
        let mut pass = render_context
            .command_encoder()
            .begin_render_pass(&RenderPassDescriptor {
                label: Some("tilt-focus HDR gather"),
                color_attachments: &attachments,
                ..default()
            });
        pass.set_pipeline(pipeline);
        pass.set_bind_group(0, group, &[]);
        pass.draw(0..3, 0..1);
        Ok(())
    }
}
