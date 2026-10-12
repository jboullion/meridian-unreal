"""
Look-dev cameras aimed at placed props, one per (zone, class): data/environment/prop_cameras.json, for
tools/ue/run_lookdev.ps1 -Cameras prop_cameras.json -StartZone <rid>. Pure Python:

    python tools/lookdev/prop_cameras.py 206:FoodDispenser/Carrot 205:Candelabra 717:Loom ...

A target is <rid>:<Class>[/<classtype>][#<n>] (the n-th such object, default the first). The camera
stands `--dist` metres away on open floor (the blockout's floor within 0.4 m of the prop's at the
camera and on the way there; of eight directions, the one nearest the zone's centre), 1.6 m up,
looking at the prop's middle. Existing cameras of the same name are replaced; others kept.
"""
import argparse
import json
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "environment"))
import blockout  # noqa: E402

OUT = os.path.join(ROOT, "data", "environment", "prop_cameras.json")
DOC = ("Look-dev cameras aimed at placed props (tools/lookdev/prop_cameras.py; docs/adr/0007). Same format "
       "as lookdev_cameras.json. Render: tools/ue/run_lookdev.ps1 -Cameras prop_cameras.json -StartZone <rid>.")


def floors(prims, pts):
    """{(x, z): [floor heights]} of the blockout's upward faces over each point."""
    out = {p: [] for p in pts}
    for pr in prims.values():
        for t in pr.triangles:
            a, b, c = (pr.positions[i] for i in t)
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            if u[2] * v[0] - u[0] * v[2] <= 1e-6:
                continue
            xs, zs = (a[0], b[0], c[0]), (a[2], b[2], c[2])
            for p in pts:
                if not (min(xs) <= p[0] <= max(xs) and min(zs) <= p[1] <= max(zs)):
                    continue
                d = [(q[0] - p[0], q[2] - p[1]) for q in (a, b, c)]
                cr = [d[i][0] * d[(i + 1) % 3][1] - d[i][1] * d[(i + 1) % 3][0] for i in range(3)]
                if all(x >= 0 for x in cr) or all(x <= 0 for x in cr):
                    out[p].append((a[1] + b[1] + c[1]) / 3)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("targets", nargs="+")
    ap.add_argument("--dist", type=float, default=3.2)
    ap.add_argument("--height", type=float, default=1.6)
    a = ap.parse_args()
    zones = {z["rid"]: z for z in json.load(open(os.path.join(ROOT, "data", "zone_layout.json")))["zones"]}
    cams = json.load(open(OUT))["cameras"] if os.path.exists(OUT) else []
    for target in a.targets:
        rid, rest = target.split(":", 1)
        rest, _, nth = rest.partition("#")
        cls, _, ctype = rest.partition("/")
        z = zones[int(rid)]
        objs = [o for o in z["objects"] if o["class"] == cls and (not ctype or ((o.get("params") or {}).get("classtype") or {}).get("class") == ctype)]
        if not objs:
            print("no %s in %s" % (rest, rid))
            continue
        o = objs[int(nth or 0)]
        x, y, zz = o["pos"]
        b = z["bounds_m"]
        cx, cz = (b["min"][0] + b["max"][0]) / 2, (b["min"][2] + b["max"][2]) / 2
        prims = blockout.read_glb(os.path.join(ROOT, z["mesh"]))
        best = None
        for k in range(8):
            ang = k * math.pi / 4
            dx, dz = math.cos(ang), math.sin(ang)
            pts = [(x + dx * a.dist * f, zz + dz * a.dist * f) for f in (0.33, 0.66, 1.0)]
            fl = floors(prims, pts)
            if not all(any(abs(h - y) < 0.4 for h in fl[p]) for p in pts):
                continue
            score = math.hypot(pts[-1][0] - cx, pts[-1][1] - cz)
            if best is None or score < best[0]:
                best = (score, pts[-1])
        if not best:
            print("no open floor around %s" % target)
            continue
        px, pz = best[1]
        org = z["world_origin_cm"]
        loc = [org[0] + px * 100, org[1] + pz * 100, org[2] + (y + a.height) * 100]
        aim = [org[0] + x * 100, org[1] + zz * 100, org[2] + (y + 0.7) * 100]
        d = [aim[i] - loc[i] for i in range(3)]
        yaw = math.degrees(math.atan2(d[1], d[0]))
        pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
        name = "prop_%s_%s%s" % (rid, rest.replace("/", "_").lower(), "_%s" % nth if nth else "")
        cams = [c for c in cams if c["name"] != name]
        cams.append({"name": name, "location_cm": [round(v) for v in loc], "rotation": [round(pitch, 1), round(yaw, 1)], "fov": 70})
        print(name, cams[-1]["location_cm"])
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n  "_doc": %s,\n  "cameras": [\n%s\n  ]\n}\n' % (json.dumps(DOC), ",\n".join("    " + json.dumps(c) for c in cams)))


if __name__ == "__main__":
    main()
