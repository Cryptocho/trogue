"""Bake a non-repeating, metre-scaled forest material from the saved AI albedo.

Run: uv run --project tools/gloam/terrain tools/gloam/terrain/prepare_textures.py
纹理加工可重现；生成图仅作为材质样本，不是场景背景。
"""
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates

from terrain_field import PARAMS, ROOT, path_x

OUT = ROOT.parents[2] / "assets/gloam/terrain"
OUT.mkdir(parents=True, exist_ok=True)
N = PARAMS["texture_resolution"]
x0, x1 = PARAMS["bounds_game_xyz"]["x"]
z0, z1 = PARAMS["bounds_game_xyz"]["z"]
source = np.asarray(Image.open(ROOT / PARAMS["source"]).convert("RGB"), dtype=np.float32) / 255.0
h, w = source.shape[:2]
swatches = []
for col, row in PARAMS["source_swatches"].values():
    # Do not retain the generated four-material boundaries in a sampled swatch.
    patch = source[int(row*h/2)+20:int((row+1)*h/2)-20, int(col*w/2)+20:int((col+1)*w/2)-20].copy()
    # Remove low-frequency illumination while retaining genuine colour variation.
    low = gaussian_filter(patch, sigma=(48, 48, 0))
    patch = np.clip(patch - 0.20*(low - patch.mean(axis=(0, 1))), 0, 1)
    swatches.append(patch)
rng = np.random.default_rng(PARAMS["seed"])
regions = gaussian_filter(rng.normal(size=(48, 48)).astype(np.float32), 1.25)
regions = (regions-regions.mean())/regions.std()
damp_regions = gaussian_filter(rng.normal(size=(80, 80)).astype(np.float32), 1.15)
damp_regions = (damp_regions-damp_regions.mean())/damp_regions.std()


def field(noise, x, z):
    uu = (x-x0)/(x1-x0)*(noise.shape[1]-1)
    vv = (z-z0)/(z1-z0)*(noise.shape[0]-1)
    return map_coordinates(noise, [vv, uu], order=3, mode="nearest")


def wet_field(x, z):
    value = np.zeros_like(x)
    for channel in PARAMS["wet_channels"]:
        distance = np.full_like(x, 1e9)
        for a, b in zip(channel["points"], channel["points"][1:]):
            dx, dz = b[0]-a[0], b[1]-a[1]
            t = np.clip(((x-a[0])*dx+(z-a[1])*dz)/(dx*dx+dz*dz), 0, 1)
            distance = np.minimum(distance, np.hypot(x-a[0]-t*dx, z-a[1]-t*dz))
        width = channel["half_width"]*(1+0.16*np.sin(8.2*x+2.1*z)+0.11*np.sin(5.8*z-3.3*x))
        value = np.maximum(value, np.clip(1-distance/width, 0, 1))
    return value


def sample(patch, x, z, index):
    angle = (0.24, -0.41, 0.79, -0.17)[index]
    ca, sa = np.cos(angle), np.sin(angle)
    # Smooth domain distortion plus two incommensurate source scales prevents
    # exact tile repetition; mirrored addressing avoids a discontinuous seam.
    a = ca*x - sa*z + 0.19*np.sin(1.13*z + 0.27*x + index)
    b = sa*x + ca*z + 0.16*np.sin(0.97*x - 0.31*z + index*2)
    size = patch.shape[0] - 1
    def at(uu, vv):
        uu = (1-np.abs(np.mod(uu, 2)-1))*size
        vv = (1-np.abs(np.mod(vv, 2)-1))*size
        return np.stack([map_coordinates(patch[:, :, c], [vv, uu], order=1, mode="reflect") for c in range(3)], axis=-1)
    metres = PARAMS["source_swatch_width_m"]
    first = at(a/metres + index*0.21, b/metres + index*0.43)
    second = at((0.73*a + 0.61*b)/(metres*1.71) + 0.37, (-0.61*a + 0.73*b)/(metres*1.71) + 0.63)
    return first*0.94 + second*0.06


albedo = np.empty((N, N, 3), dtype=np.uint8)
height = np.empty((N, N), dtype=np.float32)
rough = np.empty((N, N), dtype=np.uint8)
path_mask = np.empty((N, N), dtype=np.uint8)
xs = np.linspace(x0, x1, N, dtype=np.float32)
zs = np.linspace(z0, z1, N, dtype=np.float32)
centres = np.array([path_x(float(z)) for z in zs], dtype=np.float32)
slopes = np.gradient(centres, (z1-z0)/(N-1))

for start in range(0, N, 96):
    end = min(N, start+96)
    x, z = np.meshgrid(xs, zs[start:end])
    distance = np.abs(x-centres[start:end, None]) / np.sqrt(1+slopes[start:end, None]**2)
    edge = (0.12*np.sin(3.7*x + 1.8*z) + 0.08*np.sin(5.3*z - 2.7*x)
            + 0.075*np.sin(0.61*z + 0.7*x))
    width = 0.96 + 0.13*np.sin(z*0.31)
    t = np.clip((distance + edge - width + 0.24)/0.80, 0, 1)
    outside = t*t*(3-2*t)
    organic = field(regions, x, z)
    damp = field(damp_regions, x, z)
    mineral_weight = outside*0.38*np.clip((damp-0.65)*1.8, 0, 1) + (1-outside)*0.045
    moss_region = np.clip(0.48+0.92*organic, 0, 1)
    moss_weight = outside*moss_region*(1-mineral_weight)
    duff_weight = outside*(1-moss_region)*(1-mineral_weight)*0.96
    trail_weight = np.maximum(0, 1-moss_weight-duff_weight-mineral_weight)
    weights = (moss_weight, trail_weight, duff_weight, mineral_weight)
    total = sum(weights)
    color = np.zeros((*x.shape, 3), dtype=np.float32)
    # These are reflected colour values in sRGB; no baked night lighting.
    for i, (swatch, weight) in enumerate(zip(swatches, weights)):
        surface = sample(swatch, x, z, i)
        if i == 0:
            surface *= np.array([0.93, 1.16, 0.97], dtype=np.float32)
        elif i == 2:
            surface *= np.array([1.13, 1.02, 0.91], dtype=np.float32)
        color += surface*(weight/total)[:, :, None]
    variation = 0.97 + 0.025*np.clip(organic, -2, 2)
    color *= variation[:, :, None]
    # Moss should read as old olive vegetation, not acid-green lawn.
    color[:, :, 0] *= 0.99
    color[:, :, 2] *= 1.025
    wet = wet_field(x, z)
    color *= (1-0.33*np.clip(wet*1.4, 0, 1))[:, :, None]
    luminance = color @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    # Centimetre-scale leaf ridges, not high-frequency sand/grain normals.
    relief = gaussian_filter(luminance, 1.1)-gaussian_filter(luminance, 7.0)
    height[start:end] = (relief*0.026 + moss_weight*0.005 + mineral_weight*0.003)*(1-wet*0.9)
    moist = (1-outside)*np.clip(0.52+0.2*damp, 0.1, 1) + outside*np.clip(0.14*damp, 0, 0.4)
    roughness = 0.91 - 0.29*moist - 0.12*mineral_weight - 0.10*duff_weight - wet*0.51 + relief*0.30
    rough[start:end] = np.uint8(np.clip(roughness, 0.24, 0.94)*255)
    albedo[start:end] = np.uint8(np.clip(color, 0, 1)*255)
    path_mask[start:end] = np.uint8(outside*255)

# Gradient is in metres, not texels. Texture V points toward -game-Z, so +G is
# +dH/dgame-Z. This produces glTF/OpenGL tangent-space normals, not DirectX.
gz, gx = np.gradient(gaussian_filter(height, 0.45), (z1-z0)/(N-1), (x1-x0)/(N-1))
normal = np.stack([-gx, gz, np.ones_like(gx)], axis=-1)
normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
normal_image = np.uint8(np.clip(normal*0.5+0.5, 0, 1)*255)
Image.fromarray(albedo).save(OUT / "forest_albedo.png", compress_level=8)
Image.fromarray(normal_image).save(OUT / "forest_normal.png", compress_level=8)
orm = np.stack([np.full_like(rough, 255), rough, np.zeros_like(rough)], axis=-1)
Image.fromarray(orm).save(OUT / "forest_orm.png", compress_level=8)

# Sub-degree, seamless ripple normals keep the shallow drainage surfaces quiet.
u,v=np.meshgrid(np.linspace(0,np.pi*2,256,endpoint=False),np.linspace(0,np.pi*2,256,endpoint=False))
nx=0.014*np.sin(2*u+v)+0.006*np.sin(3*v-u)
ny=0.011*np.sin(2*v-u)+0.005*np.sin(3*u+v)
water_normal=np.stack([nx,ny,np.ones_like(nx)],axis=-1)
water_normal/=np.linalg.norm(water_normal,axis=-1,keepdims=True)
Image.fromarray(np.uint8((water_normal*0.5+0.5)*255)).save(OUT/"water_normal.png")
# Art-source views, not runtime assets.
Image.fromarray(path_mask).resize((1024, 1024)).save(ROOT / "path_mask.png")
Image.fromarray(albedo).resize((1536, 1536)).save(ROOT / "albedo_overview.jpg", quality=94)
Image.fromarray(normal_image).resize((1024, 1024)).save(ROOT / "normal_overview.png")
Image.fromarray(rough).resize((1024, 1024)).save(ROOT / "roughness_overview.png")
detail = (N//2-384, N//2-384, N//2+384, N//2+384)
Image.fromarray(albedo).crop(detail).save(ROOT / "albedo_detail.png")
Image.fromarray(normal_image).crop(detail).save(ROOT / "normal_detail.png")
Image.fromarray(rough).crop(detail).save(ROOT / "roughness_detail.png")
summary = {
    "source_pixels": [w, h], "texture_pixels": [N, N],
    "metres_per_texel_xz": [(x1-x0)/(N-1), (z1-z0)/(N-1)],
    "roughness_min_max": [float(rough.min()/255), float(rough.max()/255)],
    "normal_channels": "RGB encodes linear tangent-space XYZ, OpenGL +Y",
    "baked_lighting": False, "top_of_image_game_z": z0,
}
(ROOT / "texture_report.json").write_text(json.dumps(summary, indent=2)+"\n")
print(json.dumps(summary, indent=2))
