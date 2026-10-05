"""
Suggest look-dev cameras for zones (data/environment/lookdev_cameras.json entries, printed as JSON):
one per zone, standing on the zone's main floor (the floor height with the most area) near a
corner, at eye height, looking diagonally across to the far corner. Good enough to start from for
interiors; move or add cameras by hand (or MRBookmark in game) where a room needs it.

    python tools/lookdev/suggest_cameras.py 301 302 303      # -> [{"name": "int_301", ...}, ...]
    python tools/lookdev/suggest_cameras.py 301 --prefix int_inn
"""
import argparse
import collections
import glob
import json
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "environment"))
import blockout  # noqa: E402


def floor_triangles(glb):
    """[(a, b, c, height, area)] of the upward-facing triangles, (x, z) corners in glTF metres."""
    out = []
    for prim in blockout.read_glb(glb).values():
        P = prim.positions
        for t in prim.triangles:
            a, b, c = (P[i] for i in t)
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            ln = math.sqrt(sum(x * x for x in n))
            if ln > 1e-6 and n[1] / ln > 0.97:
                out.append(((a[0], a[2]), (b[0], b[2]), (c[0], c[2]), (a[1] + b[1] + c[1]) / 3, ln / 2))
    return out


def inside(p, a, b, c):
    def s(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
    d1, d2, d3 = s(p, a, b), s(p, b, c), s(p, c, a)
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def suggest(rid, origin, name):
    glb = next(iter(glob.glob(os.path.join(REPO, "build", "zones", "%d_*.glb" % rid))), None)
    if not glb:
        raise SystemExit("no build/zones/%d_*.glb (run tools/roo2gltf/roo2gltf.py)" % rid)
    floors = floor_triangles(glb)
    by_height = collections.Counter()
    for *_, h, area in floors:
        by_height[round(h, 1)] += area
    main_h = by_height.most_common(1)[0][0]
    main = [f for f in floors if abs(f[3] - main_h) < 0.15]
    xs = [p[0] for f in main for p in f[:3]]
    zs = [p[1] for f in main for p in f[:3]]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    # from 15% in from a corner towards the middle, until the point is on the main floor
    px, pz = (x0 + x1) / 2, (z0 + z1) / 2
    for k in range(40):
        t = k / 40 * 0.7
        qx, qz = x0 + (x1 - x0) * (0.15 + 0.35 * t), z0 + (z1 - z0) * (0.15 + 0.35 * t)
        if any(inside((qx, qz), *f[:3]) for f in main):
            px, pz = qx, qz
            break
    tx, tz = x0 + (x1 - x0) * 0.85, z0 + (z1 - z0) * 0.85
    yaw = math.degrees(math.atan2(tz - pz, tx - px))  # UE yaw: 0 east (glTF x), 90 south (glTF z)
    ox, oy, oz = origin
    return {"name": name, "location_cm": [round(ox + px * 100), round(oy + pz * 100), round(oz + (main_h + 1.7) * 100)],
            "rotation": [-6.0, round(yaw, 1)], "fov": 90}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rids", nargs="+", type=int)
    ap.add_argument("--prefix", default="int_", help="camera name prefix (name = prefix + rid, or prefix alone for one zone)")
    args = ap.parse_args()
    layout = json.load(open(os.path.join(REPO, "data", "zone_layout.json"), encoding="utf-8"))
    origins = {z["rid"]: z["world_origin_cm"] for z in layout["zones"]}
    cams = []
    for rid in args.rids:
        name = args.prefix if len(args.rids) == 1 and not args.prefix.endswith("_") else "%s%d" % (args.prefix, rid)
        cams.append(suggest(rid, origins[rid], name))
    print(json.dumps(cams, indent=1))


main()
