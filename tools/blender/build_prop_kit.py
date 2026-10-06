"""
Prop meshes for Kod-placed light sources (docs/adr/0003 pass 3, data/environment/props.json).
Headless Blender:

    blender -b --factory-startup -P tools/blender/build_prop_kit.py

Writes build/environment/kit/SM_LampPost.glb, SM_Brazier.glb, SM_Candle.glb, SM_Precip.glb (rain, snow,
ambient particles) and SM_Puffs.glb (chimney smoke, moths): mid-poly, ironwork in the
spirit of the original sprites. Material slots (props.json "materials"): iron, lamp_glass,
embers, bronze, wax. Origin at the base, +Z up (Blender), metres.
"""
import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "build", "environment", "kit")


def material(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


class Builder:
    def __init__(self, name):
        self.bm = bmesh.new()
        self.name = name
        self.slots = []

    def slot(self, name):
        if name not in self.slots:
            self.slots.append(name)
        return self.slots.index(name)

    def _tag(self, geom, slot):
        for f in [g for g in geom if isinstance(g, bmesh.types.BMFace)]:
            f.material_index = self.slot(slot)

    def cylinder(self, slot, r0, r1, z0, z1, sides=8, x=0.0, y=0.0, caps=True):
        res = bmesh.ops.create_cone(self.bm, cap_ends=caps, segments=sides, radius1=r0, radius2=r1,
                                    depth=z1 - z0, matrix=Matrix.Translation((x, y, (z0 + z1) / 2)))
        self._tag(self.bm.faces[-(sides + (2 if caps else 0)):], slot)
        return res

    def box(self, slot, sx, sy, sz, cx=0.0, cy=0.0, cz=0.0):
        bmesh.ops.create_cube(self.bm, size=1.0, matrix=Matrix.Translation((cx, cy, cz)) @ Matrix.Diagonal((sx, sy, sz, 1)))
        self._tag(self.bm.faces[-6:], slot)

    def strut(self, slot, a, b, r, sides=6):
        """Thin cylinder from a to b."""
        a, b = Vector(a), Vector(b)
        d = b - a
        rot = d.to_track_quat("Z", "Y").to_matrix().to_4x4()
        bmesh.ops.create_cone(self.bm, cap_ends=True, segments=sides, radius1=r, radius2=r, depth=d.length,
                              matrix=Matrix.Translation((a + b) / 2) @ rot)
        self._tag(self.bm.faces[-(sides + 2):], slot)

    def finish(self):
        mesh = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(mesh)
        self.bm.free()
        for s in self.slots:
            mesh.materials.append(material(s))
        for p in mesh.polygons:
            p.use_smooth = False
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def lamp_post():
    b = Builder("SM_LampPost")
    b.cylinder("iron", 0.17, 0.14, 0.0, 0.12, sides=8)            # foot
    b.cylinder("iron", 0.11, 0.09, 0.12, 0.3, sides=8)            # base moulding
    b.cylinder("iron", 0.05, 0.04, 0.3, 2.3, sides=8)             # post
    b.cylinder("iron", 0.07, 0.07, 2.3, 2.36, sides=8)            # collar
    # lantern: frame posts, glass panes, floor and pyramid roof
    h0, h1, w = 2.36, 2.78, 0.15
    b.box("iron", 2 * w + 0.04, 2 * w + 0.04, 0.04, cz=h0 + 0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box("iron", 0.03, 0.03, h1 - h0, cx=sx * w, cy=sy * w, cz=(h0 + h1) / 2)
    b.box("lamp_glass", 2 * w - 0.01, 2 * w - 0.01, h1 - h0 - 0.06, cz=(h0 + h1) / 2)
    b.cylinder("iron", 0.27, 0.02, h1, h1 + 0.2, sides=4)         # roof (rotated square)
    b.cylinder("iron", 0.025, 0.0, h1 + 0.2, h1 + 0.32, sides=6)   # finial
    obj = b.finish()
    obj.rotation_euler.z = math.radians(45)  # roof square aligned with the panes
    return obj


def brazier():
    b = Builder("SM_Brazier")
    top = 0.95
    for i in range(3):
        a = 2 * math.pi * i / 3
        b.strut("iron", (math.cos(a) * 0.32, math.sin(a) * 0.32, 0.0), (math.cos(a) * 0.18, math.sin(a) * 0.18, top - 0.1), 0.025)
    b.cylinder("iron", 0.2, 0.36, top - 0.18, top + 0.04, sides=12)    # bowl
    b.cylinder("iron", 0.37, 0.37, top + 0.04, top + 0.07, sides=12)   # rim
    b.cylinder("embers", 0.33, 0.25, top + 0.02, top + 0.1, sides=12)  # glowing coals, slightly domed
    return b.finish()


def candle():
    """The original's candlestick (candle.bgf: a turned stand about 0.76 m tall); the flame is a
    sprite (props.json "fires" candle) on the wick at 0.715 m."""
    b = Builder("SM_Candle")
    b.cylinder("bronze", 0.11, 0.09, 0.0, 0.03, sides=10)     # foot
    b.cylinder("bronze", 0.07, 0.03, 0.03, 0.09, sides=10)    # foot moulding
    b.cylinder("bronze", 0.018, 0.016, 0.09, 0.53, sides=8)   # stem
    for z in (0.2, 0.36):
        b.cylinder("bronze", 0.03, 0.03, z, z + 0.025, sides=8)  # turned rings
    b.cylinder("bronze", 0.03, 0.07, 0.53, 0.56, sides=10)    # drip tray
    b.cylinder("wax", 0.022, 0.021, 0.56, 0.70, sides=10)     # candle
    b.cylinder("wax", 0.021, 0.012, 0.70, 0.708, sides=10)
    b.cylinder("iron", 0.002, 0.002, 0.708, 0.72, sides=4)    # wick
    return b.finish()


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
    for make in (lamp_post, brazier, candle, precip, puffs):
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
