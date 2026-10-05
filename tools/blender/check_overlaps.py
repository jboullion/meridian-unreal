"""
Find z-fighting in zone art: pairs of triangles in the same plane, facing the same way, that overlap.
Runs in headless Blender on the inspection file build_zone_art.py writes:

    blender -b build/environment/zone_300/zone_300_art.blend --factory-startup -P tools/blender/check_overlaps.py -- SM_Z300_Hall SM_Z300_Shops
    (MR_OVERLAP_DETAIL=20 also prints the first 20 pairs: materials, normal, overlap area, corners)

Not every pair is visible. The back faces of proud trims (bands, piers, plinths) lie on the wall
plane facing into the wall, and the bottoms of boxes stand on the ground, so a rebuilt facade
always reports some. Pairs on outer faces (parapet corners, roofs, band fronts) are real.
"""
import itertools
import os
import sys

import bpy
from mathutils import Vector


def triangles(obj):
    me = obj.data
    me.calc_loop_triangles()
    M = obj.matrix_world
    out = []
    for t in me.loop_triangles:
        p = [M @ me.vertices[i].co for i in t.vertices]
        n = (p[1] - p[0]).cross(p[2] - p[0])
        if n.length < 1e-8:
            continue
        n.normalize()
        mat = obj.material_slots[t.material_index].name if obj.material_slots else ""
        out.append((p, n, n.dot(p[0]), mat))
    return out


def overlap_area(a, b, n):
    """Area shared by triangles a and b, both in the plane with normal n (convex clipping)."""
    u = n.orthogonal().normalized()
    v = n.cross(u)
    A = [Vector((u.dot(p), v.dot(p))) for p in a]
    B = [Vector((u.dot(p), v.dot(p))) for p in b]

    def signed(P):
        return sum(P[i - 1].x * P[i].y - P[i].x * P[i - 1].y for i in range(len(P))) / 2

    def ccw(P):
        return P if signed(P) > 0 else P[::-1]
    poly, B = ccw(A), ccw(B)
    for i in range(3):
        c, e = B[i], B[(i + 1) % 3] - B[i]

        def side(p):
            return e.x * (p.y - c.y) - e.y * (p.x - c.x)
        new = []
        for j in range(len(poly)):
            p, q = poly[j], poly[(j + 1) % len(poly)]
            fp, fq = side(p), side(q)
            if fp >= -1e-9:
                new.append(p)
            if (fp >= -1e-9) != (fq >= -1e-9):
                new.append(p + (q - p) * (fp / (fp - fq)))
        poly = new
        if len(poly) < 3:
            return 0.0
    return abs(signed(poly))


def main():
    names = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    detail = int(os.environ.get("MR_OVERLAP_DETAIL", "0"))
    for name in names or [o.name for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("SM_")]:
        obj = bpy.data.objects.get(name)
        if obj is None:
            print("[check_overlaps] %s: no such object" % name)
            continue
        tris = triangles(obj)
        buckets = {}
        for i, (p, n, d, _) in enumerate(tris):
            buckets.setdefault((round(n.x, 2), round(n.y, 2), round(n.z, 2), round(d, 2)), []).append(i)
        pairs = 0
        for idx in buckets.values():
            for i, j in itertools.combinations(idx, 2):
                a, b = tris[i], tris[j]
                if a[1].dot(b[1]) < 0.999 or abs(a[2] - b[2]) > 0.003:
                    continue
                area = overlap_area(a[0], b[0], a[1])
                if area > 1e-4:
                    pairs += 1
                    if pairs <= detail:
                        print("  %s / %s normal %s: %.1f cm2 at %s" % (
                            a[3], b[3], tuple(round(x, 2) for x in a[1]), area * 1e4,
                            tuple(round(x, 2) for x in sum(a[0], Vector()) / 3)))
        print("[check_overlaps] %s: %d triangles, %d overlapping coplanar pairs" % (name, len(tris), pairs))


main()
