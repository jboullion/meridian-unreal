"""
Grass clump meshes for the ground scatter (docs/adr/0003 ground pass). Headless Blender:

    blender -b --factory-startup -P tools/blender/build_grass_kit.py

Writes build/environment/kit/SM_GrassClump_<A|B|C>.glb: a tuft of tapered, gently bent blades,
mid-poly (about 100 triangles) so it holds up next to the rebuilt buildings. Deterministic (seeded).

Vertex colour R = height along the blade (0 root, 1 tip: colour gradient and wind weight),
G = a random value per blade (colour variation). Normals all point up, so the tuft shades like
the ground it stands on instead of like a pile of thin cards.
"""
import math
import os
import random

import bmesh
import bpy
from mathutils import Vector

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "build", "environment", "kit")

VARIANTS = {
    # name: (seed, blades, height range m, clump radius m)
    "A": (11, 22, (0.10, 0.24), 0.11),
    "B": (23, 16, (0.08, 0.18), 0.09),
    "C": (37, 26, (0.14, 0.30), 0.14),
}
SEGMENTS = 4
ROOT_WIDTH = (0.03, 0.045)


def blade(bm, rng, height, radius, color_layer):
    angle = rng.uniform(0, 2 * math.pi)
    dist = radius * math.sqrt(rng.random())
    root = Vector((math.cos(angle) * dist, math.sin(angle) * dist, 0.0))
    facing = rng.uniform(0, 2 * math.pi)  # blade plane orientation
    side = Vector((math.cos(facing), math.sin(facing), 0.0))
    lean_dir = Vector((math.cos(angle), math.sin(angle), 0.0)) if dist > 1e-4 else side.cross(Vector((0, 0, 1)))
    lean = rng.uniform(0.15, 0.45) * height  # how far the tip ends up from above the root
    width = rng.uniform(*ROOT_WIDTH)
    tint = rng.random()
    rows = []
    for i in range(SEGMENTS + 1):
        f = i / SEGMENTS
        centre = root + Vector((0, 0, height * f)) + lean_dir * lean * f * f
        w = width * (1 - f) ** 0.8
        if i == SEGMENTS:
            rows.append([bm.verts.new(centre)])
        else:
            rows.append([bm.verts.new(centre - side * w / 2), bm.verts.new(centre + side * w / 2)])
    faces = []
    for i in range(SEGMENTS):
        a, b = rows[i], rows[i + 1]
        if len(b) == 1:
            faces.append(bm.faces.new((a[0], a[1], b[0])))
        else:
            faces.append(bm.faces.new((a[0], a[1], b[1], b[0])))
    bm.verts.ensure_lookup_table()
    for f in faces:
        for loop in f.loops:
            h = loop.vert.co.z / height
            loop[color_layer] = (h, tint, 0.0, 1.0)


def build(name, seed, count, heights, radius):
    rng = random.Random(seed)
    mesh = bpy.data.meshes.new("SM_GrassClump_" + name)
    bm = bmesh.new()
    # linear float colour, exported by name as the only COLOR_0: a byte colour layer made through
    # bmesh isn't the active one, and the exporter wrote a white COLOR_0 ahead of it, so the grass
    # had no gradient, no per-blade variation and full wind at the roots (2026-10-06, docs/adr/0007 "Trees")
    color_layer = bm.loops.layers.float_color.new("Col")
    for _ in range(count):
        blade(bm, rng, rng.uniform(*heights), radius, color_layer)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(mesh)
    bm.free()
    # all normals up: the tuft lights like the ground
    mesh.normals_split_custom_set_from_vertices([(0.0, 0.0, 1.0)] * len(mesh.vertices))
    mat = bpy.data.materials.get("grass_clump") or bpy.data.materials.new("grass_clump")
    mesh.materials.append(mat)
    obj = bpy.data.objects.new(mesh.name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT, exist_ok=True)
    for name, (seed, count, heights, radius) in VARIANTS.items():
        obj = build(name, seed, count, heights, radius)
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        path = os.path.join(OUT, obj.name + ".glb")
        bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                                  export_materials="EXPORT", export_image_format="NONE",
                                  export_yup=True, export_vertex_color="NAME", export_vertex_color_name="Col",
                                  export_all_vertex_colors=False)
        print("[build_grass_kit] %s: %d triangles -> %s" % (obj.name, len(obj.data.polygons), path), flush=True)


main()
