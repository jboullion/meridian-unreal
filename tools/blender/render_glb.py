"""
Headless preview render of a .glb blockout.

    blender -b --factory-startup -P tools/blender/render_glb.py -- in.glb out.png [--elev 35] [--azim 225]

Imports the glb into an empty scene, frames it with a perspective camera
(elevation/azimuth in degrees, looking at the bounds centre) and renders with
Workbench so material base colours show.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = os.path.abspath(argv[0]), os.path.abspath(argv[1])
elev = float(argv[argv.index("--elev") + 1]) if "--elev" in argv else 35.0
azim = float(argv[argv.index("--azim") + 1]) if "--azim" in argv else 225.0

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
corners = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
lo = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
hi = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
centre = (lo + hi) / 2
radius = (hi - lo).length / 2

cam_data = bpy.data.cameras.new("cam")
cam_data.lens = 35
cam_data.clip_end = radius * 20
cam = bpy.data.objects.new("cam", cam_data)
bpy.context.scene.collection.objects.link(cam)
dist = radius * 1.9
e, a = math.radians(elev), math.radians(azim)
cam.location = centre + Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e))) * dist
cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
bpy.context.scene.camera = cam

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_cavity = True
scene.display.shading.show_shadows = True
scene.render.resolution_x = 1600
scene.render.resolution_y = 1000
scene.render.filepath = dst
world = bpy.data.worlds.new("w")
world.color = (0.05, 0.06, 0.08)
scene.world = world
bpy.ops.render.render(write_still=True)
print("rendered", dst, "objects", len(objs), "size", tuple(round(v, 1) for v in (hi - lo)))
