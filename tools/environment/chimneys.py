"""
Where the chimneys smoke from (docs/adr/0005 phase 5). The original draws a chimney as walls with a
chimney texture (RAZ-BLDG-B CHIMNEY); props.json "smoke" "textures" names them. Every blockout face
that uses one counts: faces whose footprints are within MERGE_M of each other are one chimney, and
its smoke rises from the middle of its footprint at its highest point.

    python tools/environment/chimneys.py 300        # print a zone's chimney tops (glTF metres)
"""
import glob
import json
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROPS = os.path.join(REPO, "data", "environment", "props.json")

MERGE_M = 1.5  # faces closer than this (in plan) are one chimney


def load_config():
    if not os.path.exists(PROPS):
        return {}
    return json.load(open(PROPS, encoding="utf-8")).get("smoke", {})


def chimney_tops(prims, config=None):
    """-> [[x, y, z] glTF metres] the top centre of every chimney in the zone's blockout primitives
    (tools/environment/blockout.read_glb), x then z sorted."""
    config = load_config() if config is None else config
    groups = []
    for grd in config.get("textures", []):
        prim = prims.get(grd)
        if not prim:
            continue
        for t in prim.triangles:
            P = [prim.positions[i] for i in t]
            cx, cz = sum(p[0] for p in P) / 3, sum(p[2] for p in P) / 3
            g = next((g for g in groups if math.hypot(g["c"][0] - cx, g["c"][1] - cz) < MERGE_M), None)
            if g is None:
                g = {"c": (cx, cz), "pts": []}
                groups.append(g)
            g["pts"] += P
    tops = []
    for g in groups:
        xs, ys, zs = [p[0] for p in g["pts"]], [p[1] for p in g["pts"]], [p[2] for p in g["pts"]]
        tops.append([round((min(xs) + max(xs)) / 2, 3), round(max(ys), 3), round((min(zs) + max(zs)) / 2, 3)])
    return sorted(tops, key=lambda p: (p[0], p[2]))


def main():
    sys.path.insert(0, os.path.dirname(__file__))
    import blockout
    for rid in sys.argv[1:] or ["300"]:
        glb = next(iter(glob.glob(os.path.join(REPO, "build", "zones", "%s_*.glb" % rid))))
        tops = chimney_tops(blockout.read_glb(glb))
        print("zone %s: %d chimneys" % (rid, len(tops)))
        for p in tops:
            print("  ", p)


if __name__ == "__main__":
    main()
