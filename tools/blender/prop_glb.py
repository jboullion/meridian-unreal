"""
AI-generated prop GLBs (tools/aigen, docs/adr/0007): preview renders for review, and normalising a
generated model into a kit mesh build_world.py can place.

    blender -b --factory-startup -P tools/blender/prop_glb.py -- preview IN.glb OUT_DIR [--views front,left,back,above] [--size 768]
    blender -b --factory-startup -P tools/blender/prop_glb.py -- normalize IN.glb OUT.glb --size 0.98 [--size-by height|length] [--lift 0] --name SM_AI_Brazier [--yaw 0] [--max-tris 0]

preview: orthographic Workbench renders with the model's own textures on a transparent background,
<view>.png per view. The front is the glTF +Z side (what Tripo faces to the camera), seen at eye
level like the original sprites.
normalize: joins the meshes, turns the model `--yaw` degrees about up, scales it uniformly so its
height (or with --size-by length its longest horizontal side: items lying down) is `--size` metres,
puts the origin at the centre of its base (+Z up, metres), raises it `--lift` metres above the origin
(a hanging chandelier) and exports a GLB with embedded textures; `--max-tris` decimates above that.
`--lay-flat` first turns a model generated from a side view (a sword standing on its edge) to lie
on the ground, its front face up. Prints one "[prop_glb] {json}" line of
stats either way.
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
mode, src = argv[0], os.path.abspath(argv[1])


def opt(name, default):
    return argv[argv.index(name) + 1] if name in argv else default


# camera direction from the model (azimuth about +Z from +X, elevation); glTF +Z (front) is Blender -Y
VIEWS = {"front": (-90, 0), "left": (0, 0), "back": (90, 0), "right": (180, 0), "above": (-60, 35), "top": (-90, 89)}


def load():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def bounds(objs):
    corners = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    lo = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
    hi = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
    return lo, hi


def tris(objs):
    return sum(len(p.vertices) - 2 for o in objs for p in o.data.polygons)


def stats(objs, **extra):
    lo, hi = bounds(objs)
    out = {"objects": len(objs), "tris": tris(objs), "size_m": [round(v, 4) for v in (hi - lo)],
           "min_m": [round(v, 4) for v in lo], "materials": sorted({s.material.name for o in objs for s in o.material_slots if s.material})}
    out.update(extra)
    print("[prop_glb] " + json.dumps(out), flush=True)


def preview():
    out_dir = os.path.abspath(argv[2])
    os.makedirs(out_dir, exist_ok=True)
    views = opt("--views", "front,left,back,above").split(",")
    size = int(opt("--size", "768"))
    objs = load()
    lo, hi = bounds(objs)
    centre, extent = (lo + hi) / 2, (hi - lo)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_cavity = True
    scene.render.film_transparent = True
    scene.render.resolution_x = scene.render.resolution_y = size
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = max(extent) * 1.1
    cam_data.clip_end = max(extent) * 20
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    for v in views:
        a, e = (math.radians(x) for x in VIEWS[v])
        cam.location = centre + Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e))) * max(extent) * 5
        cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(out_dir, v + ".png")
        bpy.ops.render.render(write_still=True)
    stats(objs, views=views)


def normalize():
    dst = os.path.abspath(argv[2])
    size = float(opt("--size", opt("--height", "1.0")))
    size_by = opt("--size-by", "height")
    lift = float(opt("--lift", "0"))
    lay_flat = "--lay-flat" in argv
    name = opt("--name", "SM_AI_Prop")
    yaw = float(opt("--yaw", "0"))
    max_tris = int(opt("--max-tris", "0"))
    objs = load()
    before = tris(objs)
    # unparent (keeping transforms), drop the empties glTF import makes, join
    for o in objs:
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    for o in list(bpy.context.scene.objects):
        if o.type != "MESH":
            bpy.data.objects.remove(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    if lay_flat:  # generated from a side view (a sword on its edge): its front (-Y) turned to face up
        obj.matrix_world = Matrix.Rotation(math.radians(-90), 4, "X") @ obj.matrix_world
    obj.matrix_world = Matrix.Rotation(math.radians(yaw), 4, "Z") @ obj.matrix_world
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    lo, hi = bounds([obj])
    extent = (hi.z - lo.z) if size_by == "height" else max(hi.x - lo.x, hi.y - lo.y)
    s = size / max(extent, 1e-6)
    base = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
    obj.data.transform(Matrix.Translation(Vector((0, 0, lift))) @ Matrix.Scale(s, 4) @ Matrix.Translation(-base))
    obj.data.update()
    if max_tris and tris([obj]) > max_tris:
        mod = obj.modifiers.new("decimate", "DECIMATE")
        mod.ratio = max_tris / tris([obj])
        bpy.ops.object.modifier_apply(modifier=mod.name)
    obj.name = obj.data.name = name
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=True, export_materials="EXPORT",
                              export_image_format="AUTO", export_yup=True)
    stats([obj], scale=round(s, 5), tris_before=before, out=dst)


{"preview": preview, "normalize": normalize}[mode]()
