//! The painting is assembled from these feet, sprite pivots and a water mask.
//! Pixel placements are in the normal 512×288 view, not window coordinates.
use bevy::{
    image::{ImageLoaderSettings, ImageSampler},
    prelude::*,
};
use serde::Deserialize;

use super::{GROUND_SCALE, PaperAssets, PaperSettings, canvas};

pub(super) const PPM: f32 = 25.;
pub(super) const WATER_DROP: f32 = 4. / PPM;

#[derive(Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "lowercase")]
#[repr(usize)]
pub(super) enum Kind {
    Round,
    Spruce,
    Cypress,
    Lamp,
    Traveler,
    Stone,
}

impl Kind {
    const NAMES: [&'static str; 6] = ["round", "spruce", "cypress", "lamp", "traveler", "stone"];
    pub fn label(self) -> &'static str {
        Self::NAMES[self as usize]
    }
}

#[derive(Deserialize)]
pub(super) struct Placed {
    pub kind: Kind,
    pub foot: Vec2,
    pub height: f32,
    #[serde(default)]
    pub flip: bool,
}

impl Placed {
    pub fn canvas_foot(&self) -> Vec2 {
        let p = (self.foot - canvas::SIZE.as_vec2() * 0.5) / PPM;
        Vec2::new(p.x, -p.y)
    }
    pub fn root(&self) -> Vec2 {
        let p = self.canvas_foot();
        Vec2::new(p.x, -p.y / GROUND_SCALE - 0.3)
    }
}

#[derive(Deserialize)]
pub(super) struct Layout {
    pub map_size_px: Vec2,
    pub actors: Vec<Placed>,
}

#[derive(Deserialize)]
struct Sheet<'a> {
    #[serde(borrow)]
    sprites: [Entry<'a>; 3],
}

#[derive(Deserialize)]
struct Entry<'a> {
    name: &'a str,
    size_px: Vec2,
    anchor_px: Vec2,
    image: &'a str,
    normal: &'a str,
    emission: Option<&'a str>,
    emitter_px: Option<Vec2>,
}

pub(super) struct Art {
    pub color: Handle<Image>,
    pub normal: Handle<Image>,
    pub emission: Option<Handle<Image>>,
    pub anchor: Vec2,
    pub aspect: f32,
}

#[derive(Resource)]
pub(super) struct Stage {
    pub layout: Layout,
    pub art: [Art; 6],
    pub bank: Handle<Image>,
    pub field: Handle<Image>,
    pub lamp: Vec3,
    pub stone_center: Vec3,
}

impl Stage {
    pub fn lights(&self, settings: &PaperSettings) -> [Vec4; 2] {
        [
            self.lamp.extend(if settings.fog { 1. } else { 0. }),
            self.stone_center.extend(settings.key),
        ]
    }
}

pub(super) fn load(
    mut commands: Commands,
    server: Res<AssetServer>,
    mut pending: ResMut<PaperAssets>,
) {
    let layout: Layout =
        serde_json::from_str(include_str!("../../assets/gloam/riverside/layout.json"))
            .expect("invalid river layout");
    let sheets: [Sheet<'static>; 2] = [
        include_str!("../../assets/gloam/riverside/trees/manifest.json"),
        include_str!("../../assets/gloam/riverside/props/manifest.json"),
    ]
    .map(|json| serde_json::from_str(json).expect("invalid river sprite sheet"));
    let entry = |name: &str| {
        sheets
            .iter()
            .flat_map(|s| &s.sprites)
            .find(|e| e.name == name)
            .expect("missing river sprite")
    };
    let mut image = |path: &'static str, srgb| {
        let h = server.load_with_settings(path, move |s: &mut ImageLoaderSettings| {
            s.sampler = ImageSampler::nearest();
            s.is_srgb = srgb;
        });
        pending.0.push(h.id().untyped());
        h
    };
    let art = Kind::NAMES.map(|name| {
        let a = entry(name);
        Art {
            color: image(a.image, true),
            normal: image(a.normal, false),
            emission: a.emission.map(|path| image(path, false)),
            anchor: a.anchor_px / a.size_px,
            aspect: a.size_px.x / a.size_px.y,
        }
    });
    let placed = |kind| {
        layout
            .actors
            .iter()
            .find(|p| p.kind == kind)
            .expect("missing river landmark")
    };
    let source = placed(Kind::Lamp);
    let glass_art = entry("lamp");
    let glass = (glass_art.emitter_px.expect("lamp needs glass centre") - glass_art.anchor_px)
        * (source.height / (glass_art.size_px.y * PPM));
    let foot = source.root();
    let lamp = Vec3::new(
        foot.x + if source.flip { -glass.x } else { glass.x },
        -glass.y,
        foot.y,
    );
    let stone = placed(Kind::Stone);
    let foot = stone.root();
    let stone_center = Vec3::new(foot.x, stone.height / PPM * 0.49, foot.y);
    commands.insert_resource(Stage {
        layout,
        art,
        lamp,
        stone_center,
        bank: image("gloam/riverside/bank-color.png", true),
        field: image("gloam/riverside/bank-field.png", false),
    });
}
