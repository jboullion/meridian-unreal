"""
Where the wall torches' flames are (docs/adr/0005 phase 3). The original draws a wall torch as a
wall texture with the flame painted on (Torch Attaches to wall, Torch Cross); props.json "fires"
names those textures under a preset's "walls", with the flame's centre in the texture's pixels.
Every blockout face that uses one gets a flame there: the face's UV mapping is extended to the
flame's texel (the face may show only part of the texture), and faces that agree on a torch (the two
crossed planes, both sides of a double-sided quad) are merged into one.

    python tools/environment/fires.py 306        # print the flames found in a zone (glTF metres)
"""
import glob
import json
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROPS = os.path.join(REPO, "data", "environment", "props.json")
CATALOG = os.path.join(REPO, "build", "textures", "catalog.json")

MERGE_M = 0.4     # flames closer than this are one torch
OUT_M = 0.12      # how far a flame sits off its wall, so the sprite doesn't cut into it
MARGIN_UV = 0.2   # how far outside the face's own UVs the flame may be (a face showing half the torch)


def load_presets():
    if not os.path.exists(PROPS):
        return {}
    return {k: v for k, v in json.load(open(PROPS, encoding="utf-8")).get("fires", {}).items()
            if not k.startswith("_") and isinstance(v, dict)}


def _uv_frame(P, T):
    """Affine map of a triangle: point(u, v) = O + U*u + V*v (positions P, uvs T), or None if degenerate."""
    (u0, v0), (u1, v1), (u2, v2) = T
    du1, dv1, du2, dv2 = u1 - u0, v1 - v0, u2 - u0, v2 - v0
    det = du1 * dv2 - du2 * dv1
    if abs(det) < 1e-9:
        return None
    e1 = [P[1][k] - P[0][k] for k in range(3)]
    e2 = [P[2][k] - P[0][k] for k in range(3)]
    U = [(dv2 * e1[k] - dv1 * e2[k]) / det for k in range(3)]
    V = [(du1 * e2[k] - du2 * e1[k]) / det for k in range(3)]
    O = [P[0][k] - U[k] * u0 - V[k] * v0 for k in range(3)]
    return O, U, V


def wall_flames(prims, presets=None, catalog=None):
    """-> [{"preset", "pos": [x, y, z] glTF metres, "out": unit vector off the wall or None, "wall": the
    point on the wall behind the flame (the texture's edge in the "out" direction) or None, "texel":
    the flame's texel, before OUT_M} for the zone's blockout primitives (tools/environment/blockout.read_glb)."""
    presets = load_presets() if presets is None else presets
    if catalog is None:
        catalog = json.load(open(CATALOG, encoding="utf-8"))["textures"]
    found = []
    for name, preset in presets.items():
        for grd, wall in preset.get("walls", {}).items():
            prim = prims.get(grd)
            info = catalog.get(grd)
            if not prim or not info or not prim.uvs:
                continue
            fu, fv = wall["at"][0] / info["w"], wall["at"][1] / info["h"]
            for t in prim.triangles:
                P = [prim.positions[i] for i in t]
                T = [prim.uvs[i] for i in t]
                frame = _uv_frame(P, T)
                if not frame:
                    continue
                O, U, V = frame
                us, vs = [x[0] for x in T], [x[1] for x in T]
                # the flame's texel in this face's repeat of the texture
                u = fu + round((min(us) + max(us)) / 2 - fu)
                v = fv + round((min(vs) + max(vs)) / 2 - fv)
                if not (min(us) - MARGIN_UV <= u <= max(us) + MARGIN_UV and min(vs) - MARGIN_UV <= v <= max(vs) + MARGIN_UV):
                    continue
                pos = [O[k] + U[k] * u + V[k] * v for k in range(3)]
                out = at_wall = None
                if wall.get("out"):
                    d = [U[k] * wall["out"][0] + V[k] * wall["out"][1] for k in range(3)]
                    n = math.sqrt(sum(x * x for x in d))
                    out = [x / n for x in d] if n > 1e-9 else None
                    # the wall is the texture's edge the torch is mounted on: back along "out" from the flame
                    ou, ov = wall["out"]
                    du = (-fu if ou > 0 else 1 - fu) * abs(ou)
                    dv = (-fv if ov > 0 else 1 - fv) * abs(ov)
                    at_wall = [pos[k] + U[k] * du + V[k] * dv for k in range(3)]
                found.append({"preset": name, "pos": pos, "out": out, "wall": at_wall})
    # one flame per torch: merge the faces that agree, preferring those that know the wall's side
    flames = []
    for f in sorted(found, key=lambda f: f["out"] is None):
        near = next((g for g in flames if math.dist(g["pos"], f["pos"]) < MERGE_M), None)
        if near is None:
            flames.append(dict(f, n=1))
        else:
            near["n"] += 1
    for f in flames:
        f["texel"] = [round(x, 4) for x in f["pos"]]
        if f["wall"]:
            f["wall"] = [round(x, 4) for x in f["wall"]]
        if f["out"]:
            f["pos"] = [f["pos"][k] + f["out"][k] * OUT_M for k in range(3)]
        f["pos"] = [round(x, 4) for x in f["pos"]]
    return flames


def main():
    sys.path.insert(0, os.path.dirname(__file__))
    import blockout
    for rid in sys.argv[1:] or ["306"]:
        glb = next(iter(glob.glob(os.path.join(REPO, "build", "zones", "%s_*.glb" % rid))))
        flames = wall_flames(blockout.read_glb(glb))
        print("zone %s: %d wall flames" % (rid, len(flames)))
        for f in flames:
            print("  %-6s %s out %s wall %s (%d faces)" % (f["preset"], f["pos"], f["out"] and [round(x, 2) for x in f["out"]], f["wall"], f["n"]))


if __name__ == "__main__":
    main()
