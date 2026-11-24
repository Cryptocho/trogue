"""Inspect the exported GLB itself, including contact triangles and texture roles.

This is asset-pipeline inspection, not the Bevy/game acceptance test.
"""
import json
import struct

import numpy as np
from PIL import Image

from terrain_field import PARAMS, ROOT

path = ROOT.parents[2] / "assets/gloam/terrain/floor.glb"
data = path.read_bytes()
magic, version, size = struct.unpack_from("<III", data)
assert magic == 0x46546C67 and version == 2 and size == len(data)
length, kind = struct.unpack_from("<II", data, 12)
assert kind == 0x4E4F534A
asset = json.loads(data[20:20+length])
blen, bkind = struct.unpack_from("<II", data, 20+length)
assert bkind == 0x004E4942
binary = memoryview(data)[28+length:28+length+blen]


def accessor(index):
    item = asset["accessors"][index]
    view = asset["bufferViews"][item["bufferView"]]
    dtype = {5126:"<f4", 5125:"<u4", 5123:"<u2", 5121:"u1"}[item["componentType"]]
    width = {"SCALAR":1, "VEC2":2, "VEC3":3, "VEC4":4}[item["type"]]
    offset = view.get("byteOffset",0) + item.get("byteOffset",0)
    item_size = np.dtype(dtype).itemsize * width
    assert view.get("byteStride",item_size) == item_size
    return np.frombuffer(binary, dtype=dtype, count=item["count"]*width, offset=offset).reshape((-1,width))


floor_primitives=[]
for mesh in asset["meshes"]:
    if mesh["name"].startswith("Floor sector"):
        floor_primitives.extend(mesh["primitives"])
assert floor_primitives, "No named floor mesh in actual GLB"
mins=np.array([1e9,1e9,1e9])
maxs=-mins
normals=[]
contacts={}
for primitive in floor_primitives:
    positions=accessor(primitive["attributes"]["POSITION"])
    indices=accessor(primitive["indices"]).reshape((-1,3))
    mins=np.minimum(mins, positions.min(axis=0))
    maxs=np.maximum(maxs, positions.max(axis=0))
    normals.append(accessor(primitive["attributes"]["NORMAL"])[:,1])
    assert "TANGENT" in primitive["attributes"] and "TEXCOORD_0" in primitive["attributes"]
    triangles=positions[indices]
    for patch in PARAMS["contact_patches"]:
        point=np.array([patch["x"], patch["z"]])
        # Exact barycentric height of the exported triangles in Bevy X/Z.
        a=triangles[:,0][:,[0,2]]
        e1=triangles[:,1][:,[0,2]]-a
        e2=triangles[:,2][:,[0,2]]-a
        d=point-a
        det=e1[:,0]*e2[:,1]-e1[:,1]*e2[:,0]
        u=(d[:,0]*e2[:,1]-d[:,1]*e2[:,0])/det
        v=(e1[:,0]*d[:,1]-e1[:,1]*d[:,0])/det
        hit=np.flatnonzero((u>=-1e-6)&(v>=-1e-6)&(u+v<=1+1e-6))
        if len(hit):
            i=int(hit[0])
            y=float(triangles[i,0,1]*(1-u[i]-v[i])+triangles[i,1,1]*u[i]+triangles[i,2,1]*v[i])
            contacts[patch["name"]]=y
assert len(contacts)==len(PARAMS["contact_patches"])
assert all(abs(y)<0.025 for y in contacts.values())
assert mins[0] <= -24 and maxs[0] >= 24 and mins[2] <= -38 and maxs[2] >= 14
assert min(float(n.min()) for n in normals)>0.9, "Floor normals do not point upward"
assert asset.get("scene")==0
assert not asset.get("animations") and not asset.get("cameras")
assert not any("KHR_lights_punctual" in node.get("extensions",{}) for node in asset["nodes"])

floor_material=asset["materials"][floor_primitives[0]["material"]]
pbr=floor_material["pbrMetallicRoughness"]
assert pbr.get("metallicFactor",1)==0
assert "baseColorTexture" in pbr and "metallicRoughnessTexture" in pbr and "normalTexture" in floor_material
roles={"sRGB_base_color":pbr["baseColorTexture"]["index"],
       "linear_normal":floor_material["normalTexture"]["index"],
       "linear_roughness_G_metallic_B":pbr["metallicRoughnessTexture"]["index"]}
images={}
for role,index in roles.items():
    image=asset["images"][asset["textures"][index]["source"]]
    assert "uri" in image and "bufferView" not in image, "Runtime images must be external, not duplicated in GLB"
    image_path=path.parent/image["uri"]
    with Image.open(image_path) as pixels:
        assert pixels.format=="PNG", "Bevy project enables PNG, not JPEG"
        images[role]={"name":image.get("name"),"uri":image["uri"],"pixels":list(pixels.size),"bytes":image_path.stat().st_size}
        if role=="linear_roughness_G_metallic_B":
            packed=np.asarray(pixels.convert("RGB"))
            assert packed[:,:,2].max()/255*pbr.get("metallicFactor",1)==0
            assert packed[:,:,1].min()<100 and packed[:,:,1].max()>200
assert len(data)<100*1024*1024
grass_primitives=[p for mesh in asset["meshes"] if mesh["name"].startswith("Arcing grass") for p in mesh["primitives"]]
assert grass_primitives and all("COLOR_0" in p["attributes"] for p in grass_primitives)
water_meshes=[mesh for mesh in asset["meshes"] if mesh["name"].startswith("Water ribbon")]
assert len(water_meshes)==3
for mesh in water_meshes:
    material=asset["materials"][mesh["primitives"][0]["material"]]
    assert material["pbrMetallicRoughness"]["roughnessFactor"]<0.15

report={"file_bytes":len(data),"default_scene":asset["scene"],
        "scene_count":len(asset["scenes"]),"mesh_count":len(asset["meshes"]),
        "material_count":len(asset["materials"]),"image_count":len(asset["images"]),
        "actual_ground_bounds_xyz":[mins.tolist(),maxs.tolist()],
        "exported_contact_heights_m":contacts,
        "minimum_upward_normal_y":min(float(n.min()) for n in normals),
        "external_png_roles":images,
        "all_contact_errors_under_025m":True,
        "no_lights_cameras_or_animation":True,
        "grass_vertex_colour_exported":True,
        "shallow_water_meshes":len(water_meshes),
        "note":"GLB metadata, buffers and contact triangles inspected; final Bevy lighting/appearance remains main-agent acceptance."}
(ROOT/"glb_inspection.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
