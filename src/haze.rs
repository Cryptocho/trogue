use bevy::{
    pbr::NotShadowCaster,
    prelude::*,
    reflect::TypePath,
    render::render_resource::{AsBindGroup, ShaderRef},
};

use crate::{CANOPY_FROM, CANOPY_TO, LAMP, desktop::ViewOptions};

#[derive(Component)]
pub struct GroundMist(pub Handle<Image>);

#[derive(Asset, TypePath, AsBindGroup, Clone)]
pub struct MistMaterial {
    #[uniform(0)]
    scatter: [Vec4; 5], // Packed in the same order as WGSL Scatter.
    #[texture(1, dimension = "3d")]
    #[sampler(2)]
    density: Handle<Image>,
}

impl Material for MistMaterial {
    fn fragment_shader() -> ShaderRef {
        "gloam/light/ground_mist.wgsl".into()
    }
    fn alpha_mode(&self) -> AlphaMode {
        AlphaMode::Premultiplied
    }
}

pub fn spawn(
    mut commands: Commands,
    options: Res<ViewOptions>,
    server: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<MistMaterial>>,
) {
    let size = Vec3::new(54., 4.6, 39.);
    let center = Vec3::new(0., 1.65, -8.);
    let density = server.load("gloam/light/ground-mist.dds");
    let material = materials.add(MistMaterial {
        scatter: [
            (LAMP + 1.48 * Vec3::Y).extend(0.82),
            CANOPY_FROM.extend(0.62),
            CANOPY_TO.extend(0.78),
            (center - 0.5 * size).extend(0.28),
            size.recip().extend(0.),
        ],
        density: density.clone(),
    });
    // The fixed camera stays above this entry plane, including at both zooms.
    // The shader clips each ray to the volume AND opaque/alpha-mask prepass,
    // then integrates world-space density; this is not a translucent fog card.
    commands.spawn((
        Name::new("贴地薄雾"),
        GroundMist(density),
        MeshMaterial3d(material),
        Mesh3d(meshes.add(Plane3d::default().mesh().size(size.x, size.z))),
        Transform::from_translation(center + Vec3::Y * 0.5 * size.y),
        NotShadowCaster,
        if options.clear_air {
            Visibility::Hidden
        } else {
            Visibility::Visible
        },
    ));
}
