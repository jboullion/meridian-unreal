"""
Effect meshes for the environment (docs/adr/0005). Headless Blender:

    blender -b --factory-startup -P tools/blender/build_prop_kit.py

Writes build/environment/kit/SM_Precip.glb (rain, snow, ambient particles) and SM_Puffs.glb (chimney
smoke, moths): quads the materials move on the GPU. Origin at the base, +Z up (Blender), metres.
The Kod-placed props (lamps, braziers, candles, furniture, ...) are AI-generated from the original
sprites by tools/aigen (docs/adr/0007); the procedural lamp post, brazier and candle that lived here
were retired on 2026-10-06.
"""
import os

import bpy

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "build", "environment", "kit")


def material(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


PRECIP_QUADS = 10000
PRECIP_BOX_M = (32.0, 32.0, 16.0)  # AMRPrecipitationActor::BoxCm / BoxHeightCm


def quads(name, count, box, slot, stratified=False):
    """Tiny quads (1 cm) scattered through a box around the origin, for materials that move them on
    the GPU (world position offset): the vertex position is the quad's seed in the box, UV0 its
    corner, UV1 two random numbers per quad (which quads show at a given amount, their size and
    phase). stratified: UV1.x is (i + 0.5) / count instead, so the quads' phases spread evenly."""
    import random
    rng = random.Random(59)
    bx, by, bz = box
    verts, faces, uv0, uv1 = [], [], [], []
    h = 0.005
    for i in range(count):
        x, y, z = rng.uniform(-bx / 2, bx / 2), rng.uniform(-by / 2, by / 2), rng.uniform(-bz / 2, bz / 2)
        r = ((i + 0.5) / count if stratified else rng.random(), rng.random())
        n = len(verts)
        verts += [(x - h, y, z - h), (x + h, y, z - h), (x + h, y, z + h), (x - h, y, z + h)]
        faces.append((n, n + 1, n + 2, n + 3))
        uv0 += [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
        uv1 += [r] * 4
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    for layer_name, uvs in (("UVMap", uv0), ("Seed", uv1)):
        layer = mesh.uv_layers.new(name=layer_name)
        for loop in mesh.loops:
            layer.data[loop.index].uv = uvs[loop.vertex_index]
    mesh.materials.append(material(slot))
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def precip():
    """SM_Precip (docs/adr/0005 phase 4): PRECIP_QUADS quads through one box. M_Precip turns each
    into a falling streak or flake, M_Splash into a splash, M_Ambient (phase 5) into a dust mote,
    a speck of pollen, a firefly or a leaf."""
    return quads("SM_Precip", PRECIP_QUADS, PRECIP_BOX_M, "precip")


PUFF_QUADS = 48


def puffs():
    """SM_Puffs (docs/adr/0005 phase 5): PUFF_QUADS quads in a 1 m box standing on the origin, phases
    spread evenly. M_Smoke turns them into a chimney's plume of puffs, M_Moth into moths around a
    lamp; build_world.py scales the actor to the effect's extent (it sets the bounds)."""
    obj = quads("SM_Puffs", PUFF_QUADS, (1.0, 1.0, 1.0), "puffs", stratified=True)
    for v in obj.data.vertices:
        v.co.z += 0.5
    return obj


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT, exist_ok=True)
    for make in (precip, puffs):
        obj = make()
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        path = os.path.join(OUT, obj.name + ".glb")
        bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                                  export_materials="EXPORT", export_image_format="NONE", export_yup=True)
        print("[build_prop_kit] %s: %d faces -> %s" % (obj.name, len(obj.data.polygons), path), flush=True)


main()
