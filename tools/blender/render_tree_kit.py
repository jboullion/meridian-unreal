"""
Review renders of the procedural trees (tools/blender/build_tree_kit.py, docs/adr/0007 "Trees"):
each SM_Tree_<Name>_<Variant>.glb textured with its bark and leaf atlas (leaves alpha-clipped), seen
from the side with an orthographic camera at a fixed metres-per-pixel scale, so trees compare by size.

    blender -b --factory-startup -P tools/blender/render_tree_kit.py -- [Name ...]   # default: all
    python tools/blender/tree_sheet.py [Name ...]                                    # the sheet

Writes build/environment/tree_review/<mesh>.png (RGBA, 100 px per metre, the ground at the bottom).
"""
import glob
import math
import os
import sys

import bpy
from mathutils import Vector

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
KIT = os.path.join(REPO, "build", "environment", "kit")
TEX = os.path.join(REPO, "build", "textures_placeholder")
OUT = os.path.join(REPO, "build", "environment", "tree_review")
PX_PER_M = 100


def material(name, image, leaves):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(image)
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.9
    if leaves:
        nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        if hasattr(m, "alpha_threshold"):
            m.alpha_threshold = 0.4
        if hasattr(m, "surface_render_method"):
            m.surface_render_method = "DITHERED"
    return m


def render(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    mesh = os.path.splitext(os.path.basename(path))[0]
    kind = mesh.split("_")[2]
    mats = {"tree_bark": material("bark", os.path.join(TEX, "T_TreeBark_%s.png" % kind), False)}
    leaves = os.path.join(TEX, "T_TreeLeaves_%s.png" % kind)
    if os.path.exists(leaves):
        mats["tree_leaves"] = material("leaves", leaves, True)
    objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    for o in objs:
        for slot in o.material_slots:
            base = slot.material.name.split(".")[0] if slot.material else ""
            if base in mats:
                slot.material = mats[base]
    corners = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    lo = Vector((min(c.x for c in corners), min(c.y for c in corners), 0.0))
    hi = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
    w, h = max(hi.x - lo.x, hi.y - lo.y) + 0.4, hi.z + 0.2
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in [e.identifier for e in
                                                                         bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE"
    scene.render.film_transparent = True
    scene.render.resolution_x, scene.render.resolution_y = max(16, int(w * PX_PER_M)), max(16, int(h * PX_PER_M))
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(w, h)
    scene.collection.objects.link(cam)
    scene.camera = cam
    cx, cy = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2
    cam.location = (cx, cy - 50, h / 2 - 0.1)  # from the front (-Y), level
    cam.rotation_euler = (math.radians(90), 0, 0)
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
    scene.collection.objects.link(sun)
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.6, 0.7, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8
    scene.world = world
    os.makedirs(OUT, exist_ok=True)
    scene.render.filepath = os.path.join(OUT, mesh + ".png")
    bpy.ops.render.render(write_still=True)
    print("[render_tree_kit] %s" % scene.render.filepath, flush=True)


def main():
    only = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    for path in sorted(glob.glob(os.path.join(KIT, "SM_Tree_*.glb"))):
        kind = os.path.basename(path).split("_")[2]
        if not only or kind in only:
            render(path)


main()
