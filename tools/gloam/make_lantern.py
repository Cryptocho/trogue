"""blender-4.5 --background --python tools/gloam/make_lantern.py

Original wayside oil lamp. In Blender the flame is at (0, 0, 1.48);
+Z becomes Bevy +Y on export. Geometry and patina are baked, not shader nodes.
"""
from pathlib import Path
from collections import defaultdict
from math import cos, sin, pi, tau
from random import Random
from tempfile import TemporaryDirectory
import subprocess

import bpy
import numpy as np
from mathutils import Vector

bpy.ops.wm.read_factory_settings(use_empty=True)
rng = Random(93)
root = Path(__file__).resolve().parents[2]
output = root / "assets/gloam/waylight.glb"
output.parent.mkdir(parents=True, exist_ok=True)
temp = TemporaryDirectory(prefix="gloam-lantern-")

# Build in linear RGB. Vertex colors diversify the end grain, rust and oil
# streaks without allocating a separate material (or texture) per piece.
surfaces = {}
geometry = defaultdict(lambda: ([], [], []))

def material(name, color, metal=0., rough=.7, emission=None):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Metallic'].default_value = metal
    bsdf.inputs['Roughness'].default_value = rough
    if emission:
        bsdf.inputs['Emission Color'].default_value = (*emission, 1)
        bsdf.inputs['Emission Strength'].default_value = 5.5
    vc = mat.node_tree.nodes.new('ShaderNodeVertexColor')
    vc.layer_name = 'Tint'
    mat.node_tree.links.new(vc.outputs['Color'], bsdf.inputs['Base Color'])
    surfaces[name] = (mat, color)
    return name

wood = material('Split chestnut', (.16, .080, .039), rough=.86)
end = material('Silvered heartwood', (.24, .18, .119), rough=.9)
iron = material('Blackened iron', (.074, .061, .049), .58, .54)
bronze = material('Worn brass edges', (.39, .213, .077), .57, .38)
roof = material('Verdigris bronze', (.112, .182, .137), .48, .57)
cord = material('Waxed cord', (.305, .219, .123), rough=.98)
cloth = material('Oxblood linen', (.30, .049, .028), rough=.96)
glass = material('Amber glass', (.95, .60, .25), rough=.24, emission=(1., .41, .075))
stone = material('Foundation slate', (.126, .146, .141), rough=.8)


def face(surf, verts, value=1.):
    points, polys, colors = geometry[surf]
    start = len(points)
    points.extend(verts)
    polys.append(tuple(range(start, start + len(verts))))
    colors.append(tuple(min(1., value*c) for c in surfaces[surf][1]) + (1.,))


def ring(x, y, z, r, n, angle, ry=1.):
    return [(x + r*cos(angle + tau*i/n), y + r*ry*sin(angle + tau*i/n), z) for i in range(n)]


def turned(surf, profile, n=8, xy=(0, 0), angle=pi/8, variation=.045):
    # A single continuous profile carries the rolled rims, chamfers and ribs.
    rows = [ring(*xy, z, r, n, angle) for z, r in profile]
    face(surf, list(reversed(rows[0])))
    for lo, hi in zip(rows, rows[1:]):
        for i in range(n):
            j = (i + 1) % n
            face(surf, (lo[i], lo[j], hi[j], hi[i]), 1 + rng.uniform(-variation, variation))
    face(surf, rows[-1])


def rod(surf, a, b, radius, n=6, end_radius=None):
    a, b = Vector(a), Vector(b)
    axis = (b - a).normalized()
    u = axis.orthogonal().normalized()
    v = axis.cross(u)
    lo = [a + radius*(u*cos(tau*i/n) + v*sin(tau*i/n)) for i in range(n)]
    hi = [b + (end_radius or radius)*(u*cos(tau*i/n) + v*sin(tau*i/n)) for i in range(n)]
    face(surf, list(reversed(lo)))
    for i in range(n):
        j = (i + 1) % n
        face(surf, (lo[i], lo[j], hi[j], hi[i]))
    face(surf, hi)


def wrought(surf, points, radius):
    for a, b in zip(points, points[1:]):
        rod(surf, a, b, radius)


# Crooked, deeply split post. Keep each longitudinal ridge for the whole pole,
# then disturb its edges so the silhouette is cut wood rather than a cylinder.
count = 29
ridge = [rng.uniform(.83, 1.06) for _ in range(count)]
rows = []
for z, x, y, r in [(-.07,.57,.06,.14), (.21,.57,.06,.135), (.75,.60,.06,.114),
                    (1.35,.60,.055,.108), (1.92,.57,.06,.091), (2.18,.55,.06,.083)]:
    rows.append([(x + r*ridge[i]*cos(tau*i/count+.12*z), y + r*ridge[i]*sin(tau*i/count+.12*z),
                  z + (rng.uniform(-.045,.047) if z>2 else 0)) for i in range(count)])
for lo, hi in zip(rows, rows[1:]):
    for i in range(count):
        j = (i + 1) % count
        face(wood, (lo[i],lo[j],hi[j],hi[i]))
face(end, rows[-1]); face(end, list(reversed(rows[0])))
# The post is wedged between split pieces of slate, not set in a machined base.
for x,y,rx,ry,h,a in [(.40,.01,.17,.13,.12,.5), (.60,-.088,.19,.122,.087,.2),
                       (.73,.109,.16,.20,.155,.4), (.48,.227,.15,.123,.097,0)]:
    n = 9
    rim = [(rx*cos(a+tau*i/n)*rng.uniform(.78,1.13),ry*sin(a+tau*i/n)*rng.uniform(.76,1.1)) for i in range(n)]
    lo = [(x+u,y+v,-.035) for u,v in rim]
    mid = [(x+u,y+v,h*rng.uniform(.28,.78)) for u,v in rim]
    hi = [(x+u*.69-.011,y+v*.72,h*rng.uniform(.78,1.06)) for u,v in rim]
    for i in range(n):
        j = (i+1)%n
        face(stone,(lo[i],lo[j],mid[j],mid[i]),rng.uniform(.43,.74))
        face(stone,(mid[i],mid[j],hi[j],hi[i]),rng.uniform(.56,.98))
        face(stone,(hi[i],hi[j],(x-.035,y+.013,h*1.12)),rng.uniform(.79,1.23))
turned(iron, [(.16,.139),(.18,.144),(.235,.137)], 12, (.57,.06))
for z in [1.76,1.79,1.82,1.85]:
    turned(cord, [(z,.104),(z+.012,.109),(z+.023,.101)], 12, (.58,.06), variation=.13)

# Two prongs and a swept blacksmith's hook carry the lamp off the path side.
wrought(iron, [(.59,.06,1.81),(.44,.05,1.83),(.29,.04,1.96),(.15,.027,2.045),
                (.045,.014,2.06),(-.016,.009,2.018),(0,0,1.967),(.032,0,1.989)], .021)
wrought(iron, [(.58,.07,1.54),(.48,.065,1.65),(.41,.05,1.79),(.29,.04,1.96)], .016)
for i in range(3):
    z = 1.952 - .045*i
    a = .65 + i*pi/2
    points = [(.012 + .027*cos(t)*cos(a), .027*cos(t)*sin(a), z+.041*sin(t))
              for t in (tau*j/12 for j in range(13))]
    wrought(iron, points, .0068)

# Hexagonal copper lantern: spun oil reservoir, crimped sill, frosted amber
# panes, a projecting rain hood with scalloped vents and a finial.
a = pi/6
turned(iron, [(1.197,.086),(1.21,.126),(1.236,.121),(1.247,.152),(1.267,.166),(1.28,.155)], 12)
turned(bronze, [(1.27,.157),(1.279,.165),(1.292,.164),(1.301,.154)], 6, angle=a)
turned(glass, [(1.293,.144),(1.34,.166),(1.603,.180),(1.635,.167)], 6, angle=a)
for i in range(6):
    ang = a+tau*i/6
    p = [(r*cos(ang),r*sin(ang),z) for z,r in [(1.287,.153),(1.34,.173),(1.622,.185),(1.647,.176)]]
    wrought(bronze, p, .010)
    # Shaped window guard; two wires converge at a tiny diamond boss.
    a2 = a+tau*(i+1)/6
    center = (.159*cos((ang+a2)/2),.159*sin((ang+a2)/2),1.412)
    wrought(iron, [p[1],center,(.175*cos(a2),.175*sin(a2),1.595)], .0048)
turned(bronze, [(1.625,.179),(1.635,.193),(1.649,.193),(1.657,.179)], 6, angle=a)
turned(roof, [(1.652,.172),(1.667,.223),(1.681,.230),(1.701,.214),(1.760,.130),
              (1.779,.092),(1.791,.088)], 6, angle=a, variation=.12)
turned(iron, [(1.788,.065),(1.824,.063)], 12)
turned(roof, [(1.819,.084),(1.835,.087),(1.849,.057),(1.862,.034)], 8)
turned(bronze, [(1.85,.031),(1.88,.024)], 10)
for i in range(6):
    t = a+tau*i/6
    rod(bronze,(.224*cos(t),.224*sin(t),1.685),(.084*cos(t),.084*sin(t),1.785),.0055)

# Small linen pilgrim's knot: ragged hanging tails are double-sided solids.
for x, length in [(.548,.37),(.62,.24)]:
    y, z = -.063, 1.8
    for i in range(5):
        xx = x + .022*sin(i*.9)
        nx = x + .022*sin((i+1)*.9)
        zz = z-length*i/5
        lo = z-length*(i+1)/5
        w = .054*(1-i*.09)
        face(cloth, [(xx-w/2,y,zz),(nx-w/2,y-.011,lo+.012),(nx+w/2,y-.01,lo),
                     (xx+w/2,y,zz)], .95-.072*i)
        y -= .011

def wrap_timber(mesh):
    # Resample to the physical scale of the post before deriving its relief.
    source = Path(__file__).parent / 'waylight' / 'timber-original.webp'
    w,h = 384,576
    prepared = Path(temp.name)/'Chestnut.png'
    # Some Blender builds omit WebP; use ImageMagick for color resampling.
    subprocess.run(['magick', str(source), '-resize', f'{w}x{h}!', '-strip',
                    'PNG24:'+str(prepared)], check=True)
    img = bpy.data.images.load(str(prepared))
    pixels = np.empty(w*h*4, np.float32)
    img.pixels.foreach_get(pixels)
    luminance = pixels.reshape(h,w,4)[...,:3] @ np.array((.213,.715,.072),np.float32)
    dy,dx = np.gradient(np.log1p(16*luminance))
    normals = np.stack((-2.4*dx,-2.4*dy,np.ones_like(dx)),axis=-1)
    normals /= np.linalg.norm(normals,axis=-1,keepdims=True)
    nimg = bpy.data.images.new('Chestnut fibres',w,h,alpha=False)
    nimg.colorspace_settings.name = 'Non-Color'
    rgba = np.ones((h,w,4),np.float32); rgba[...,:3] = normals*.5+.5
    nimg.pixels.foreach_set(rgba.ravel())
    nimg.filepath_raw = str(Path(temp.name)/'Chestnut fibres.png')
    nimg.file_format = 'PNG'
    nimg.save()
    tree = surfaces[wood][0].node_tree
    bsdf = next(n for n in tree.nodes if n.type == 'BSDF_PRINCIPLED')
    color = tree.nodes.new('ShaderNodeTexImage'); color.image = img
    normal = tree.nodes.new('ShaderNodeTexImage'); normal.image = nimg
    bump = tree.nodes.new('ShaderNodeNormalMap')
    tree.links.new(color.outputs['Color'], bsdf.inputs['Base Color'])
    tree.links.new(normal.outputs['Color'], bump.inputs['Color'])
    tree.links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    uv = mesh.uv_layers.new(name='Bark wrap')
    for p in mesh.polygons:
        i = p.index % count
        for j, u in zip(p.loop_indices,(i,i+1,i+1,i)):
            z = mesh.vertices[mesh.loops[j].vertex_index].co.z
            uv.data[j].uv = (u/count,(z+.07)/2.32)

for name, (verts, polys, colors) in geometry.items():
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], polys)
    mesh.materials.append(surfaces[name][0])
    if name == wood:
        wrap_timber(mesh)
    else:
        attr = mesh.color_attributes.new(name='Tint', type='BYTE_COLOR', domain='CORNER')
        for poly, color in zip(mesh.polygons, colors):
            for j in poly.loop_indices:
                attr.data[j].color = color
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
bpy.ops.export_scene.gltf(filepath=str(output), export_format='GLB', export_yup=True,
                          export_materials='EXPORT', export_all_vertex_colors=False, export_tangents=True,
                          export_vertex_color='MATERIAL', export_animations=False)
print(f'Lantern written: {output}')
temp.cleanup()
