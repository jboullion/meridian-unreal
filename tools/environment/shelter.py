"""
Shelter maps for rain and snow (docs/adr/0005 phase 4): for each zone, the height of the highest
surface over every 25 cm of ground, seen from above. M_Precip hides a streak or flake below that
height, so nothing falls under roofs, eaves, arcades or bridges, and nothing falls through floors.

Baked offline from the zone's blockout and its rebuilt art (roofs with their overhangs), so it is
deterministic and costs nothing at runtime. Upward- and downward-facing triangles count (|normal y|
>= 0.25); walls don't, and neither does grime. Each triangle counts at its highest point, drawn in
height order, so a cell keeps the highest surface over it (a little conservative on slopes).

    python tools/environment/shelter.py            # every zone in data/zone_layout.json
    python tools/environment/shelter.py 300 331

Writes build/environment/shelter/shelter_<rid>.png (8-bit: 0 = z0, 255 = z0 + z_scale) and
shelter.json ({rid: origin_cm, size_cm, z0_cm, z_scale_cm, file}, UE world cm; the map's x is UE X,
its y is UE Y). Zones that share another's geometry (the Outskirts) use that zone's map.
"""
import glob
import json
import math
import os
import sys

from PIL import Image, ImageDraw

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
import blockout  # noqa: E402

OUT = os.path.join(REPO, "build", "environment", "shelter")
CELL_M = 0.25
SKIP_ART = ("blockout_render", "Grime")


def zone_triangles(zone):
    """[(footprint [(x, z)], top y)] in glTF metres for the zone's blockout and art meshes."""
    files = [os.path.join(REPO, zone["mesh"])]
    art = os.path.join(REPO, "build", "environment", "zone_%d" % zone["rid"])
    files += [f for f in sorted(glob.glob(os.path.join(art, "*.glb"))) if not any(k in os.path.basename(f) for k in SKIP_ART)]
    tris = []
    for f in files:
        for prim in blockout.read_glb(f).values():
            P = prim.positions
            for t in prim.triangles:
                a, b, c = (P[i] for i in t)
                u = [b[k] - a[k] for k in range(3)]
                v = [c[k] - a[k] for k in range(3)]
                n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                ln = math.sqrt(sum(x * x for x in n))
                if ln < 1e-9 or abs(n[1]) / ln < 0.25:
                    continue
                tris.append(([(a[0], a[2]), (b[0], b[2]), (c[0], c[2])], max(a[1], b[1], c[1])))
    return tris, len(files)


def bake(zone):
    tris, nfiles = zone_triangles(zone)
    if not tris:
        return None
    xs = [p[0] for f, _ in tris for p in f]
    zs = [p[1] for f, _ in tris for p in f]
    x0, z0m = math.floor(min(xs)), math.floor(min(zs))
    w = int(math.ceil((max(xs) - x0) / CELL_M)) + 1
    h = int(math.ceil((max(zs) - z0m) / CELL_M)) + 1
    ys = [t for _, t in tris]
    y_lo = math.floor(min(ys)) - 1.0
    step = max(0.1, (max(ys) - y_lo) / 254.0)  # metres per level; 10 cm unless the zone is tall
    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    for foot, top in sorted(tris, key=lambda t: t[1]):
        value = min(255, int(math.ceil((top - y_lo) / step)))
        draw.polygon([((x - x0) / CELL_M, (z - z0m) / CELL_M) for x, z in foot], fill=value)
    os.makedirs(OUT, exist_ok=True)
    name = "shelter_%d.png" % zone["rid"]
    img.save(os.path.join(OUT, name))
    ox, oy, oz = zone["world_origin_cm"]
    print("zone %d: %d triangles from %d meshes, %dx%d cells, %.0f cm steps" % (zone["rid"], len(tris), nfiles, w, h, step * 100))
    return {"origin_cm": [round(ox + x0 * 100.0, 1), round(oy + z0m * 100.0, 1)],
            "size_cm": [round(w * CELL_M * 100.0, 1), round(h * CELL_M * 100.0, 1)],
            "z0_cm": round(oz + y_lo * 100.0, 1), "z_scale_cm": round(step * 255 * 100.0, 1), "file": name}


def main():
    layout = json.load(open(os.path.join(REPO, "data", "zone_layout.json"), encoding="utf-8"))
    zones = {z["rid"]: z for z in layout["zones"]}
    wanted = [int(a) for a in sys.argv[1:]] or sorted(zones)
    meta_path = os.path.join(OUT, "shelter.json")
    meta = json.load(open(meta_path, encoding="utf-8")) if os.path.exists(meta_path) else {}
    for rid in wanted:
        z = zones[rid]
        if "shares_geometry_with" in z:
            continue
        entry = bake(z)
        if entry:
            meta[str(rid)] = entry
    for rid, z in zones.items():
        owner = z.get("shares_geometry_with", {}).get("rid")
        if owner and str(owner) in meta:
            meta[str(rid)] = dict(meta[str(owner)], shares=owner)
    os.makedirs(OUT, exist_ok=True)
    json.dump(meta, open(meta_path, "w", encoding="utf-8"), indent=1, sort_keys=True)
    print("wrote %s" % meta_path)


if __name__ == "__main__":
    main()
