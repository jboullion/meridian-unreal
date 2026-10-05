"""
Prop meshes for Kod-placed light sources (docs/adr/0003 pass 3, data/environment/props.json).
Headless Blender:

    blender -b --factory-startup -P tools/blender/build_prop_kit.py

Writes build/environment/kit/SM_LampPost.glb and SM_Brazier.glb: mid-poly, ironwork in the
spirit of the original sprites. Material slots (props.json "materials"): iron, lamp_glass,
embers. Origin at the base, +Z up (Blender), metres.
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


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT, exist_ok=True)
    for make in (lamp_post, brazier):
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
