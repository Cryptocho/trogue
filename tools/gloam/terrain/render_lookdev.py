"""Offline material/geometry art preview, never presented as a game screenshot.

Run: blender-4.5 --background --factory-startup --python tools/gloam/terrain/render_lookdev.py
No preview camera/light is written into floor.blend or floor.glb.
"""
import math
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parent
bpy.ops.wm.open_mainfile(filepath=str(ROOT / "floor.blend"))
scene = bpy.context.scene


def B(x,y,z):
    return Vector((x,-z,y))


camera = bpy.data.cameras.new("Temporary art camera")
obj = bpy.data.objects.new("Temporary art camera",camera)
scene.collection.objects.link(obj)
obj.location = B(0,11.6,16.5)
obj.rotation_euler = (B(0,0,-0.3)-obj.location).to_track_quat("-Z","Y").to_euler()
camera.sensor_fit = "VERTICAL"
camera.sensor_height = 24
camera.lens = 24/(2*math.tan(math.radians(32)/2))
scene.camera = obj

moon = bpy.data.lights.new("Temporary cool moon", "AREA")
moon.energy = 2400
moon.color = (0.34,0.53,1.0)
moon.shape = "DISK"
moon.size = 9
obj = bpy.data.objects.new(moon.name,moon)
scene.collection.objects.link(obj)
obj.location = B(-6,10,-3)
obj.rotation_euler = (B(0,0,-2)-obj.location).to_track_quat("-Z","Y").to_euler()

lamp = bpy.data.lights.new("Temporary warm contact light", "POINT")
lamp.energy = 150
lamp.color = (1.0,0.54,0.18)
lamp.shadow_soft_size = 0.15
obj = bpy.data.objects.new(lamp.name,lamp)
scene.collection.objects.link(obj)
obj.location = B(1.5,1.25,0.83)

world = bpy.data.worlds.new("Temporary neutral cool ambience")
world.use_nodes = True
world.node_tree.nodes.get("Background").inputs["Color"].default_value = (0.15,0.21,0.35,1)
world.node_tree.nodes.get("Background").inputs["Strength"].default_value = 0.15
scene.world = world
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 32
scene.cycles.use_denoising = True
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(ROOT / "terrain_lookdev.png")
scene.view_settings.view_transform = "AgX"
scene.view_settings.look = "AgX - Medium High Contrast"
bpy.ops.render.render(write_still=True)
