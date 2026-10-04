"""
Render a character kit from tools/blender/mpfb_character.py with given or random head sliders.

    blender -b --factory-startup -P tools/blender/preview_character.py -- art_src/characters/MPFB_Male out.png [--hair short02] [--seed 3] [--strength 0.8] [--face]

--seed N     random slider values (0 = neutral face)
--face       frame the head instead of the whole body
"""
import json
import math
import os
import random
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
kit, out = os.path.abspath(argv[0]), os.path.abspath(argv[1])


def opt(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


manifest = json.load(open(os.path.join(kit, "manifest.json"), encoding="utf-8"))
hair = opt("--hair", manifest["files"]["hair"][0]["style"] if manifest["files"]["hair"] else None)
seed = int(opt("--seed", "0"))
strength = float(opt("--strength", "0.8"))

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=os.path.join(kit, manifest["files"]["body"]))
if hair:
    bpy.ops.import_scene.fbx(filepath=os.path.join(kit, "hair_%s.fbx" % hair))

# slider values -> shape key values on every mesh that has them (body, hair)
rng = random.Random(seed)
values = {}
if seed:
    for s in manifest["sliders"]:
        v = rng.uniform(-strength, strength)
        if s["decr"] and v < 0:
            values[s["decr"]] = -v
        if s["incr"] and v > 0:
            values[s["incr"]] = v
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in meshes:
    keys = o.data.shape_keys
    if keys:
        for k in keys.key_blocks[1:]:
            k.value = values.get(k.name, 0.0)

colors = {"MR_Skin": (0.78, 0.6, 0.5, 1), "MR_Eyes": (0.95, 0.95, 0.95, 1), "MR_Brows": (0.2, 0.13, 0.08, 1),
          "MR_Lashes": (0.1, 0.07, 0.05, 1), "MR_Hair": (0.25, 0.16, 0.09, 1)}
for o in meshes:
    for slot in o.material_slots:
        if slot.material:
            base = slot.material.name.split(".")[0]
            slot.material.diffuse_color = colors.get(base, (0.7, 0.7, 0.7, 1))

# frame
corners = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
hi = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
if "--face" in argv:
    centre, dist = Vector((0, 0, hi.z - 0.12)), 0.75
else:
    centre, dist = (lo + hi) / 2, (hi - lo).length * 1.4
cam_data = bpy.data.cameras.new("cam")
cam_data.lens = 50
cam = bpy.data.objects.new("cam", cam_data)
bpy.context.scene.collection.objects.link(cam)
a, e = math.radians(-65), math.radians(5)  # front-left, characters face -Y in Blender
cam.location = centre + Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e))) * dist
cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
scene = bpy.context.scene
scene.camera = cam
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_cavity = True
scene.render.resolution_x, scene.render.resolution_y = 900, 900
scene.render.filepath = out
w = bpy.data.worlds.new("w")
w.color = (0.12, 0.13, 0.16)
scene.world = w
bpy.ops.render.render(write_still=True)
print("[preview_character] %s seed=%d hair=%s morphs=%d -> %s" % (manifest["name"], seed, hair, len(values), out))
