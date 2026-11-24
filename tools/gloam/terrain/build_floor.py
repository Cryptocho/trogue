"""Blender 4.5 art source for the Gloam forest floor.

Run from the repository root:
  blender-4.5 --background --factory-startup --python-exit-code 1 --python tools/gloam/terrain/build_floor.py
Inputs use Bevy X/Z metres; the only coordinate conversion is B(x,-z,y).
无相机、灯具或动画被导出；Scene 0 是可直接加载的完整地面。
"""
import json
import math
import random
import sys
import shutil
import struct
import tempfile
from collections import defaultdict
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from terrain_field import PARAMS, ground_height, path_distance, path_x, vegetation_clearance, channel_value, wetness

PROJECT = ROOT.parents[2]
OUT = PROJECT / "assets/gloam/terrain"
RNG = random.Random(PARAMS["seed"])
X0, X1 = PARAMS["bounds_game_xyz"]["x"]
Z0, Z1 = PARAMS["bounds_game_xyz"]["z"]

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for datablocks in (bpy.data.meshes, bpy.data.materials, bpy.data.images):
    for datablock in list(datablocks):
        if datablock.users == 0:
            datablocks.remove(datablock)


def B(x, y, z):
    return (x, -z, y)


def material(name, color, roughness=0.86, double_sided=False):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.diffuse_color = (*color, 1)
    mat.use_backface_culling = not double_sided
    mat.node_tree.nodes.clear()
    shader = mat.node_tree.nodes.new("ShaderNodeBsdfPrincipled")
    shader.name = "Principled BSDF"
    output = mat.node_tree.nodes.new("ShaderNodeOutputMaterial")
    mat.node_tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Metallic"].default_value = 0
    return mat


def textured_ground():
    mat = material("Forest floor • scanned humus / moss / trail", (0.18, 0.16, 0.10))
    nt = mat.node_tree
    shader = nt.nodes.get("Principled BSDF")
    for filename, colorspace, input_name in (
        ("forest_albedo.png", "sRGB", "Base Color"),
        ("forest_orm.png", "Non-Color", "Roughness"),
        ("forest_normal.png", "Non-Color", "Normal"),
    ):
        image = bpy.data.images.load(str(OUT / filename), check_existing=True)
        image.colorspace_settings.name = colorspace
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.name = filename
        tex.image = image
        tex.extension = "EXTEND"
        if input_name == "Normal":
            normal = nt.nodes.new("ShaderNodeNormalMap")
            normal.inputs["Strength"].default_value = 1.0
            nt.links.new(tex.outputs["Color"], normal.inputs["Color"])
            nt.links.new(normal.outputs["Normal"], shader.inputs[input_name])
        elif input_name == "Roughness":
            channels = nt.nodes.new("ShaderNodeSeparateColor")
            nt.links.new(tex.outputs["Color"], channels.inputs["Color"])
            nt.links.new(channels.outputs["Green"], shader.inputs["Roughness"])
        else:
            nt.links.new(tex.outputs["Color"], shader.inputs[input_name])
    return mat


ground_mat = textured_ground()
grass_mats = [material(f"Grass palette • {i}", color, 0.84, True) for i, color in enumerate([
    (0.18, 0.27, 0.065), (0.23, 0.31, 0.09), (0.29, 0.34, 0.12),
    (0.13, 0.23, 0.095), (0.32, 0.32, 0.12), (0.25, 0.23, 0.09),
])]
fern_mats = [material(f"Fern • {i}", color, 0.83, True) for i, color in enumerate([
    (0.075, 0.16, 0.067), (0.12, 0.205, 0.08), (0.105, 0.175, 0.055),
])]
leaf_mats = [material(f"Decomposing leaf • {i}", color, 0.88, True) for i, color in enumerate([
    (0.21, 0.12, 0.046), (0.16, 0.088, 0.035), (0.27, 0.19, 0.08),
    (0.11, 0.095, 0.055), (0.23, 0.15, 0.061), (0.13, 0.12, 0.052),
])]
root_mats = [material("Wet exposed root", (0.105, 0.075, 0.042), 0.79),
             material("Old root bark", (0.16, 0.12, 0.068), 0.88)]
chip_mats = [material("Slate chips", (0.15, 0.16, 0.145), 0.80),
             material("Weathered warm chips", (0.20, 0.185, 0.14), 0.89)]

# One shared vertex-colour PBR material avoids six draw calls per grass sector.
undergrowth_mat = material("Forest undergrowth • vertex colour", (1,1,1), 0.76, True)
vertex_color = undergrowth_mat.node_tree.nodes.new("ShaderNodeVertexColor")
vertex_color.layer_name = "Color"
undergrowth_mat.node_tree.links.new(vertex_color.outputs["Color"],
                                  undergrowth_mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"])


class Batch:
    def __init__(self, name, materials):
        self.name = name
        self.materials = materials
        self.vertices = []
        self.faces = []
        self.colors = []

    def add(self, vertices, faces, index=0):
        offset = len(self.vertices)
        self.vertices.extend(B(*v) for v in vertices)
        for face in faces:
            if self.name.startswith("Arcing grass"):
                a, p, q = (vertices[face[i]] for i in range(3))
                u, v = tuple(p[i]-a[i] for i in range(3)), tuple(q[i]-a[i] for i in range(3))
                normal = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
                view = (-a[0], 11.6-a[1], 16.5-a[2])
                if sum(normal[i]*view[i] for i in range(3)) < 0:
                    face = tuple(reversed(face))
            self.faces.append(tuple(offset+i for i in face))
        self.colors.extend([tuple(self.materials[index].diffuse_color)]*len(vertices))

    def finish(self):
        if not self.faces:
            return None
        mesh = bpy.data.meshes.new(self.name)
        mesh.from_pydata(self.vertices, [], self.faces)
        mesh.materials.append(undergrowth_mat)
        colors = mesh.color_attributes.new(name="Color", type="FLOAT_COLOR", domain="POINT")
        colors.data.foreach_set("color", [c for rgba in self.colors for c in rgba])
        if self.name.startswith("Arcing grass"):
            for polygon in mesh.polygons:
                polygon.use_smooth = True
        mesh.update()
        if self.name.startswith("Arcing grass"):
            normals = []
            for vertex in mesh.vertices:
                nx, ny, nz = vertex.normal
                normal = (nx*0.35, ny*0.35, 0.83+abs(nz)*0.35)
                length = math.sqrt(sum(c*c for c in normal))
                normals.append(tuple(c/length for c in normal))
            mesh.normals_split_custom_set_from_vertices(normals)
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.collection.objects.link(obj)
        return obj


# Coarse sectors preserve frustum culling without disconnected height seams.
NX = math.ceil((X1-X0)/PARAMS["ground_spacing_m"])
NZ = math.ceil((Z1-Z0)/PARAMS["ground_spacing_m"])
STEP_X, STEP_Z = (X1-X0)/NX, (Z1-Z0)/NZ
CELLS = 23
height_min, height_max = 1e9, -1e9
for cz in range(0, NZ, CELLS):
    for cx in range(0, NX, CELLS):
        ex, ez = min(NX, cx+CELLS), min(NZ, cz+CELLS)
        vertices, faces, uvs = [], [], []
        width = ex-cx+1
        for iz in range(cz, ez+1):
            z = Z0 + iz*STEP_Z
            for ix in range(cx, ex+1):
                x = X0 + ix*STEP_X
                y = ground_height(x, z)
                height_min, height_max = min(height_min, y), max(height_max, y)
                vertices.append(B(x, y, z))
                uvs.append(((x-X0)/(X1-X0), (Z1-z)/(Z1-Z0)))
        for iz in range(ez-cz):
            for ix in range(ex-cx):
                a = iz*width+ix
                # Up-facing in Blender XY after (x,-z,y) conversion.
                faces.append((a, a+width, a+width+1, a+1))
        mesh = bpy.data.meshes.new(f"Floor sector {cx//CELLS:02d} {cz//CELLS:02d}")
        mesh.from_pydata(vertices, [], faces)
        mesh.materials.append(ground_mat)
        uv = mesh.uv_layers.new(name="UVMap")
        for polygon in mesh.polygons:
            polygon.use_smooth = True
            for loop_index in polygon.loop_indices:
                uv.data[loop_index].uv = uvs[mesh.loops[loop_index].vertex_index]
        mesh.update()
        obj = bpy.data.objects.new(mesh.name, mesh)
        bpy.context.collection.objects.link(obj)


batches = {}


def batch(kind, x, z, materials):
    # Eight-metre vegetation sectors keep far-off geometry cullable.
    sector = (int((x-X0)//8), int((z-Z0)//8))
    key = (kind, *sector)
    if key not in batches:
        batches[key] = Batch(f"{kind} sector {sector[0]:02d} {sector[1]:02d}", materials)
    return batches[key]


def grass_clump(x, z, scale=1, blades=7, far=False):
    b = batch("Arcing grass", x, z, grass_mats)
    y = ground_height(x, z)-0.003
    fan = RNG.uniform(0.3, 1.5)
    for _ in range(blades):
        angle = fan+RNG.uniform(-1.05,1.05)
        dx, dz = math.cos(angle), math.sin(angle)
        sx, sz = -dz, dx
        px, pz = x+RNG.uniform(-0.085,0.085), z+RNG.uniform(-0.085,0.085)
        height = RNG.uniform(0.23,0.53)*scale
        half_width = RNG.uniform(0.013,0.023)*scale
        lean = height*RNG.uniform(0.70,1.15)
        vertices, faces = [], []
        levels = (0,0.48,0.88) if far else (0,0.35,0.67,0.9)
        for t in levels:
            cx, cz = px+dx*lean*t*t, pz+dz*lean*t*t
            cy = y+height*math.sin(t*1.82)
            width = half_width*max(0.16,math.sin(math.pi*t)**0.65)
            fold = half_width*0.25
            vertices.extend([(cx-sx*width,cy,cz-sz*width),
                             (cx,cy+fold,cz),
                             (cx+sx*width,cy,cz+sz*width)])
        for k in range(len(levels)-1):
            a=k*3
            faces.extend([(a,a+3,a+4,a+1),(a+1,a+4,a+5,a+2)])
        tip=len(vertices)
        vertices.append((px+dx*lean,y+height*math.sin(1.82),pz+dz*lean))
        a=(len(levels)-1)*3
        faces.extend([(a,tip,a+1),(a+1,tip,a+2)])
        b.add(vertices,faces,RNG.randrange(len(grass_mats)))


counts = defaultdict(int)


def grass_colony(cx, cz, radius, number, scale=1, hero=False):
    for _ in range(number):
        angle=RNG.uniform(0,math.tau)
        r=radius*math.sqrt(RNG.random())
        x,z=cx+math.cos(angle)*r,cz+math.sin(angle)*r*0.62
        if path_distance(x,z)<0.87 or not vegetation_clearance(x,z):
            continue
        if math.hypot(x-0.55,z-0.55)<1.23:
            continue
        grass_clump(x,z,scale,RNG.randint(8,11) if hero else RNG.randint(5,8))
        counts["grass_clumps"]+=1


# Broken, irregular shoulder bands instead of evenly spaced pin-like sprouts.
for side in (-1,1):
    for i in range(43):
        z=-19+i*0.66+RNG.uniform(-0.2,0.2)
        if RNG.random()<0.18:
            continue
        x=path_x(z)+side*RNG.uniform(1.40,2.10)
        grass_colony(x,z,RNG.uniform(0.35,0.62),RNG.randint(5,9),RNG.uniform(0.65,0.9))
for _ in range(100):
    x,z=RNG.uniform(-12,12),RNG.uniform(-20,9)
    if path_distance(x,z)<2.3:
        continue
    grass_colony(x,z,RNG.uniform(0.45,0.90),RNG.randint(7,13),RNG.uniform(0.7,1.0))
for x,z,radius in ((2.75,1.20,0.43),(3.65,1.55,0.48),(1.63,1.87,0.34)):
    grass_colony(x,z,radius,23,1.0,hero=True)
for _ in range(1100):
    x,z=RNG.uniform(X0+0.5,X1-0.5),RNG.uniform(Z0+0.5,Z1-0.5)
    if abs(x)<13 and -21<z<10:
        continue
    if RNG.random()<0.5 and path_distance(x,z)>1 and vegetation_clearance(x,z):
        grass_clump(x,z,RNG.uniform(0.45,0.7),4,far=True)
        counts["distant_grass_clumps"]+=1


def fern(x, z, scale):
    b = batch("Woodland ferns", x, z, fern_mats)
    base = ground_height(x,z)
    n = RNG.randint(6, 8)
    for frond in range(n):
        angle = frond*math.tau/n+RNG.uniform(-0.25,0.25)
        dx, dz = math.cos(angle), math.sin(angle)
        side_x, side_z = -dz, dx
        length = RNG.uniform(0.32,0.52)*scale
        lift = RNG.uniform(0.24,0.40)*scale
        points = []
        for k in range(11):
            t=k/10
            points.append((x+dx*length*t, base+lift*math.sin(t*math.pi*0.72)+0.015, z+dz*length*t))
        # Thin central rib is opaque geometry, not an alpha billboard.
        for k in range(10):
            a, c = points[k], points[k+1]
            width=0.0027*scale*(1-k/13)
            b.add([(a[0]-side_x*width,a[1],a[2]-side_z*width),
                   (a[0]+side_x*width,a[1],a[2]+side_z*width),
                   (c[0]+side_x*width,c[1],c[2]+side_z*width),
                   (c[0]-side_x*width,c[1],c[2]-side_z*width)], [(0,1,2,3)], 0)
        for k in range(1,10):
            t=k/10
            center=points[k]
            leaflet_length=(0.03+0.14*math.sin(math.pi*t)**0.8)*(1-0.35*t)*scale
            for sign in (-1,1):
                sx,sz=side_x*sign,side_z*sign
                # Tapered raised centre ridge gives each leaflet real volume.
                tip=(center[0]+sx*leaflet_length+dx*0.045*scale,
                     center[1]+0.008*scale,
                     center[2]+sz*leaflet_length+dz*0.045*scale)
                mid=((center[0]+tip[0])*0.5, (center[1]+tip[1])*0.5+0.008*scale,
                     (center[2]+tip[2])*0.5)
                width=(0.015+0.010*(1-t))*scale
                verts=[center,(mid[0]-dx*width,mid[1]-0.004,mid[2]-dz*width),
                       (mid[0],mid[1]+0.004,mid[2]),tip,
                       (mid[0]+dx*width,mid[1]-0.004,mid[2]+dz*width)]
                b.add(verts,[(0,1,2),(1,3,2),(0,2,4),(2,3,4)], RNG.randrange(len(fern_mats)))


for _ in range(112):
    x,z=RNG.uniform(-15,15),RNG.uniform(-28,10)
    if path_distance(x,z) < 1.55 or not vegetation_clearance(x,z):
        continue
    # No tall ferns immediately in front of the traveller or lamppost.
    if -1.4 < x < 2.5 and -0.8 < z < 3.2:
        continue
    fern(x,z,RNG.uniform(0.95,1.45))
    counts["ferns"] += 1


# Small curved litter leaves read in grazing lamplight without becoming confetti.
for i in range(2850):
    if i % 7 == 0:
        cx,cz=RNG.uniform(-18,18),RNG.uniform(-30,12)
    x,z=cx+RNG.uniform(-0.14,0.14),cz+RNG.uniform(-0.12,0.12)
    if not vegetation_clearance(x,z):
        continue
    if RNG.random() > 0.72+0.20*math.sin(x*0.7-z*0.8):
        continue
    b=batch("Curled fallen leaves",x,z,leaf_mats)
    y=ground_height(x,z)+RNG.uniform(0.003,0.009)
    angle=RNG.uniform(0,math.tau)
    dx,dz=math.cos(angle),math.sin(angle)
    sx,sz=-dz,dx
    length,width=RNG.uniform(0.055,0.115),RNG.uniform(0.020,0.046)
    curl=RNG.uniform(0.007,0.020)
    verts=[(x-dx*length*0.5,y,z-dz*length*0.5),
           (x-sx*width,y+curl*0.5,z-sz*width),
           (x,y+curl,z),
           (x+dx*length*0.6,y+curl*0.2,z+dz*length*0.6),
           (x+sx*width,y+curl*0.3,z+sz*width)]
    b.add(verts,[(0,1,2),(1,3,2),(0,2,4),(2,3,4)],RNG.randrange(len(leaf_mats)))
    counts["curled_leaves"]+=1


def root_branch(x,z,angle,length,radius):
    b=batch("Low exposed roots",x,z,root_mats)
    rings=[]
    segments=9
    for k in range(segments+1):
        t=k/segments
        px=x+math.cos(angle)*length*t+math.sin(angle)*math.sin(t*math.pi*1.6)*length*0.06
        pz=z+math.sin(angle)*length*t-math.cos(angle)*math.sin(t*math.pi*1.6)*length*0.06
        py=ground_height(px,pz)-radius*0.10+math.sin(math.pi*t)*radius*0.38
        rad=radius*(1-t*0.85)
        ring=[]
        for j in range(6):
            a=j*math.tau/6
            ring.append((px-math.sin(angle)*math.cos(a)*rad,py+math.sin(a)*rad,
                         pz+math.cos(angle)*math.cos(a)*rad))
        rings.append(ring)
    verts=[v for ring in rings for v in ring]
    faces=[]
    for k in range(segments):
        for j in range(6):
            faces.append((k*6+j,(k+1)*6+j,(k+1)*6+(j+1)%6,k*6+(j+1)%6))
    faces.append(tuple(range(6)))
    faces.append(tuple(segments*6+j for j in range(5,-1,-1)))
    b.add(verts,faces,RNG.randrange(len(root_mats)))


for _ in range(83):
    x,z=RNG.uniform(-17,17),RNG.uniform(-29,10)
    length=RNG.uniform(0.45,1.5)
    angle=RNG.uniform(0,math.tau)
    if path_distance(x,z)<1.0 or not vegetation_clearance(x,z):
        continue
    if not vegetation_clearance(x+math.cos(angle)*length,z+math.sin(angle)*length):
        continue
    root_branch(x,z,angle,length,RNG.uniform(0.018,0.043))
    counts["exposed_roots"]+=1


# A few actual mineral chips catch highlights; source-photo grit handles the rest.
for _ in range(260):
    x,z=RNG.uniform(-18,18),RNG.uniform(-30,12)
    if not vegetation_clearance(x,z):
        continue
    b=batch("Embedded slate fragments",x,z,chip_mats)
    y=ground_height(x,z)
    r=RNG.uniform(0.045,0.14)
    angle=RNG.uniform(0,math.tau)
    vertices=[]
    for level in range(2):
        for j in range(5):
            a=angle+j*math.tau/5
            rr=r*RNG.uniform(0.7,1.1)*(0.75 if level else 1)
            vertices.append((x+math.cos(a)*rr,y+(r*0.38 if level else -0.006),z+math.sin(a)*rr*0.65))
    faces=[(9,8,7,6,5)]
    faces.extend((j,j+5,(j+1)%5+5,(j+1)%5) for j in range(5))
    b.add(vertices,faces,RNG.randrange(len(chip_mats)))
    counts["slate_chips"]+=1

for b in batches.values():
    b.finish()

# Water follows depressed, irregular drainage ribbons; this is not a disc laid
# above intact terrain. The wet sediment edge is baked into albedo/ORM.
water_mat=material("Shallow drainage water • low ripple", (0.014,0.025,0.028), 0.075)
shader=water_mat.node_tree.nodes["Principled BSDF"]
water_image=bpy.data.images.load(str(OUT/"water_normal.png"), check_existing=True)
water_image.colorspace_settings.name="Non-Color"
tex=water_mat.node_tree.nodes.new("ShaderNodeTexImage")
tex.image=water_image
normal=water_mat.node_tree.nodes.new("ShaderNodeNormalMap")
water_mat.node_tree.links.new(tex.outputs["Color"],normal.inputs["Color"])
water_mat.node_tree.links.new(normal.outputs["Normal"],shader.inputs["Normal"])
# Only a few centimetres of water: the diffuse term is the *same* pebbly
# bed visible on the banks, not the solid blue of a deep pond. Keep the
# ripple UVs independent, so the highlights can move across the sediment.
bed=water_mat.node_tree.nodes.new("ShaderNodeTexImage")
bed.image=bpy.data.images.get("forest_albedo.png")
assert bed.image is not None
bed.extension="EXTEND"
bed_uv=water_mat.node_tree.nodes.new("ShaderNodeUVMap")
bed_uv.uv_map="Streambed"
water_mat.node_tree.links.new(bed_uv.outputs["UV"],bed.inputs["Vector"])
tint=water_mat.node_tree.nodes.new("ShaderNodeMixRGB")
tint.blend_type='MULTIPLY'
tint.inputs[0].default_value=1.
tint.inputs[2].default_value=(0.52,0.65,0.70,1.)
water_mat.node_tree.links.new(bed.outputs["Color"],tint.inputs[1])
water_mat.node_tree.links.new(tint.outputs["Color"],shader.inputs["Base Color"])


def clip_shoreline(polygon, threshold=0.31):
    result=[]
    for a,b in zip(polygon,polygon[1:]+polygon[:1]):
        inside_a,inside_b=a[2]>=threshold,b[2]>=threshold
        if inside_a:
            result.append(a)
        if inside_a != inside_b:
            t=(threshold-a[2])/(b[2]-a[2])
            result.append((a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,threshold))
    return result


for channel in PARAMS["wet_channels"]:
    width=channel["half_width"]*1.35
    xmin=min(p[0] for p in channel["points"])-width
    xmax=max(p[0] for p in channel["points"])+width
    zmin=min(p[1] for p in channel["points"])-width
    zmax=max(p[1] for p in channel["points"])+width
    nx,nz=math.ceil((xmax-xmin)/0.09),math.ceil((zmax-zmin)/0.09)
    vertices,faces,uvs=[],[],[]
    for iz in range(nz):
        for ix in range(nx):
            x=xmin+ix*(xmax-xmin)/nx
            z=zmin+iz*(zmax-zmin)/nz
            corners=[(x,z),(x,z+(zmax-zmin)/nz),
                     (x+(xmax-xmin)/nx,z+(zmax-zmin)/nz),(x+(xmax-xmin)/nx,z)]
            values=[(px,pz,channel_value(px,pz,channel)) for px,pz in corners]
            for ids in ((0,1,2),(0,2,3)):
                clipped=clip_shoreline([values[i] for i in ids])
                if len(clipped)<3:
                    continue
                offset=len(vertices)
                for px,pz,value in clipped:
                    vertices.append(B(px,channel["level"],pz))
                    uvs.append((px*0.5,-pz*0.5))
                faces.append(tuple(offset+i for i in range(len(clipped))))
    mesh=bpy.data.meshes.new("Water ribbon • "+channel["name"])
    mesh.from_pydata(vertices,[],faces)
    mesh.materials.append(water_mat)
    uv=mesh.uv_layers.new(name="UVMap")
    for polygon in mesh.polygons:
        for loop in polygon.loop_indices:
            uv.data[loop].uv=uvs[mesh.loops[loop].vertex_index]
    bed_layer=mesh.uv_layers.new(name="Streambed")
    for loop in mesh.loops:
        x,back,_=mesh.vertices[loop.vertex_index].co
        bed_layer.data[loop.index].uv=((x-X0)/(X1-X0),(Z1+back)/(Z1-Z0))
    mesh.uv_layers.active=uv
    mesh.update()
    obj=bpy.data.objects.new(mesh.name,mesh)
    bpy.context.collection.objects.link(obj)
counts["shallow_water_ribbons"]=len(PARAMS["wet_channels"])

# Keep the authoring scene purely asset data: no light/camera/world-dependent nodes.
scene=bpy.context.scene
scene.name="Gloam_Forest_Floor"
scene.unit_settings.system="METRIC"
scene.unit_settings.scale_length=1.0
scene.world.color=(0.1,0.1,0.1)
scene["中文说明"]="原创夜林地表：湿润苔藓、腐殖土、曲折小径、短草、蕨叶、落叶及低矮树根。"
scene["coordinate_contract"]="Bevy Y-up; traveller and lantern contact at y=0. No baked lighting."
bpy.ops.object.select_all(action="SELECT")
for mat in list(bpy.data.materials):
    if mat.users == 0:
        bpy.data.materials.remove(mat)

for image in bpy.data.images:
    if image.filepath:
        image.filepath = bpy.path.relpath(image.filepath, start=str(ROOT))
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/"floor.blend"))
# Blender cannot directly write GLB with external images. Export standard
# Separate glTF to a temporary directory, then retain only its geometry BIN in
# the GLB container while keeping image URI references beside floor.glb.
with tempfile.TemporaryDirectory(prefix="gloam-terrain-") as directory:
    temporary=Path(directory)
    bpy.ops.export_scene.gltf(
        filepath=str(temporary/"floor.gltf"), export_format="GLTF_SEPARATE", use_selection=True,
        export_yup=True, export_apply=True, export_normals=True, export_tangents=True,
        export_materials="EXPORT", export_texcoords=True, export_animations=False,
        export_cameras=False, export_lights=False, export_extras=True,
        export_image_format="AUTO", export_unused_images=False,
    )
    gltf=json.loads((temporary/"floor.gltf").read_text())
    binary=(temporary/gltf["buffers"][0].pop("uri")).read_bytes()
    for image in gltf["images"]:
        filename=Path(image["uri"]).name
        shutil.copyfile(temporary/image["uri"],OUT/filename)
        image["uri"]=filename
    encoded=json.dumps(gltf,separators=(",",":"),ensure_ascii=False).encode("utf-8")
    encoded+=b" "*((-len(encoded))%4)
    binary+=bytes((-len(binary))%4)
    size=12+8+len(encoded)+8+len(binary)
    assert size<100*1024*1024, "External-image GLB must stay below hosting limits"
    content=(struct.pack("<III",0x46546c67,2,size)
             +struct.pack("<II",len(encoded),0x4e4f534a)+encoded
             +struct.pack("<II",len(binary),0x004e4942)+binary)
    (OUT/"floor.glb").write_bytes(content)
(OUT/"forest_roughness.png").unlink(missing_ok=True)

triangles=sum(len(p.vertices)-2 for obj in scene.objects if obj.type=="MESH" for p in obj.data.polygons)
report={
    "asset":"assets/gloam/terrain/floor.glb", "default_scene":0,
    "bounds_game_xz":[[X0,X1],[Z0,Z1]],
    "ground_y_range_m":[height_min,height_max],
    "contact_heights_m":{p["name"]:ground_height(p["x"],p["z"]) for p in PARAMS["contact_patches"]},
    "triangles":triangles, "mesh_objects":len([o for o in scene.objects if o.type=="MESH"]),
    "vegetation_counts":dict(counts),
    "textures":{"base_color":"sRGB","normal":"linear tangent-space OpenGL +Y","roughness":"linear glTF G channel"},
    "coordinate_conversion":"Blender (x,-z,y) then glTF +Y export, yielding exact game (x,y,z)",
    "geometry_notes":"Opaque double-sided grass/fern/leaf polygons; no alpha sorting, no animation, no tall trees or props.",
}
(ROOT/"geometry_report.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
print(json.dumps(report,indent=2,ensure_ascii=False))
