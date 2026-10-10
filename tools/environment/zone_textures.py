"""
A zone's textures, sorted the way the art pass needs them (the zone-environment skill's step 2):
which are plain surfaces, painted art kept flat, cut-outs rebuilt as solids, foliage cut-outs,
wall torches (the fire pipeline's), animated, and which get grime. Writes a contact sheet and the
list for each zone.

    python tools/environment/zone_textures.py 301 302 303
      -> build/environment/zone_<rid>/textures.png and textures.json

Reads the roo2gltf blockout (build/zones/<rid>_*.glb), build/textures/ (catalog.json and the
originals), data/environment/materials.json, facades.json and props.json.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
import blockout  # noqa: E402
import facades  # noqa: E402

TEXTURES = os.path.join(REPO, "build", "textures")
ENV = os.path.join(REPO, "data", "environment")

ORDER = ["wall torch", "cut-out solid", "foliage cut-out", "painted, flat", "animated", "surface"]
COLOURS = {"wall torch": (255, 140, 40), "cut-out solid": (90, 200, 255), "foliage cut-out": (110, 210, 110),
           "painted, flat": (230, 120, 230), "animated": (255, 230, 90), "surface": (200, 200, 200)}


def load(name):
    with open(os.path.join(ENV, name), encoding="utf-8") as f:
        return json.load(f)


def slots(rid):
    """{grd: {"area_m2", "wall", "floor", "ceiling"}} for the zone's blockout (areas in m2)."""
    glb = next((g for g in glob.glob(os.path.join(REPO, "build", "zones", "%d_*.glb" % rid))
                if not g.endswith(("_collision.glb", "_render.glb"))), None)
    if not glb:
        raise SystemExit("no build/zones/%d_*.glb (run tools/roo2gltf/roo2gltf.py)" % rid)
    out = {}
    for name, prim in blockout.read_glb(glb).items():
        acc = {"area_m2": 0.0, "wall": 0.0, "floor": 0.0, "ceiling": 0.0}
        for t in prim.triangles:
            a, b, c = (prim.positions[i] for i in t)
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            area = math.sqrt(sum(x * x for x in n)) / 2
            if area <= 0:
                continue
            ny = n[1] / (2 * area)
            acc["area_m2"] += area
            acc["floor" if ny > 0.7 else "ceiling" if ny < -0.7 else "wall"] += area
        out[name.split("__")[0]] = acc
    return out


def classify(rid):
    catalog = json.load(open(os.path.join(TEXTURES, "catalog.json"), encoding="utf-8"))["textures"]
    materials = load("materials.json")
    fac = facades.load()
    fires = load("props.json").get("fires", {})
    torch_walls = {grd for p in fires.values() if isinstance(p, dict) for grd in p.get("walls", {})}
    flat = re.compile(materials["relief"].get("flat") or "$^", re.I)
    grime_ex = re.compile(materials["grime"].get("exclude") or "$^", re.I)
    ground = materials.get("ground", {})
    rows = []
    for grd, use in sorted(slots(rid).items(), key=lambda kv: -kv[1]["area_m2"]):
        info = catalog.get(grd, {})
        name = info.get("name", "?")
        openings = bool(fac["textures"].get(grd, {}).get("openings"))
        thickness = facades.cutout_thickness(grd, fac, catalog)
        if grd in torch_walls:
            kind = "wall torch"
        elif thickness:
            kind = "cut-out solid"
        elif info.get("has_transparency"):
            kind = "foliage cut-out"
        elif openings or flat.search(name):
            kind = "painted, flat"
        elif info.get("frames", 1) > 1:
            kind = "animated"
        else:
            kind = "surface"
        where = max(("wall", "floor", "ceiling"), key=lambda k: use[k])
        rows.append({
            "grd": grd, "name": name, "kind": kind, "where": where, "area_m2": round(use["area_m2"], 1),
            "frames": info.get("frames", 1), "cutout_m": thickness,
            "grime": grd != "untextured" and not (info.get("has_transparency") or grime_ex.search(name)) and where == "wall",
            "ground": grd in ground if isinstance(ground, dict) else False,
            "openings": openings,
        })
    rows.sort(key=lambda r: (ORDER.index(r["kind"]), -r["area_m2"]))
    return rows


def sheet(rid, rows, path):
    from PIL import Image, ImageDraw
    tile, pad, text_h, cols = 160, 8, 58, 6
    rows_n = max(1, math.ceil(len(rows) / cols))
    img = Image.new("RGB", (cols * (tile + pad) + pad, 30 + rows_n * (tile + text_h + pad)), (24, 24, 28))
    d = ImageDraw.Draw(img)
    d.text((pad, 8), "zone %d: %d textures (colour = kind)" % (rid, len(rows)), fill=(230, 230, 230))
    for i, r in enumerate(rows):
        x = pad + (i % cols) * (tile + pad)
        y = 30 + (i // cols) * (tile + text_h + pad)
        src = os.path.join(TEXTURES, r["grd"] + ".png")
        if os.path.exists(src):
            t = Image.open(src).convert("RGBA")
            t.thumbnail((tile, tile))
            bg = Image.new("RGBA", (tile, tile), (60, 0, 60, 255))  # magenta behind what's see-through
            bg.paste(t, ((tile - t.width) // 2, (tile - t.height) // 2), t)
            img.paste(bg.convert("RGB"), (x, y))
        c = COLOURS[r["kind"]]
        d.rectangle([x - 2, y - 2, x + tile + 1, y + tile + 1], outline=c, width=2)
        flags = [r["where"], "%.0f m2" % r["area_m2"]]
        if r["frames"] > 1:
            flags.append("%d fr" % r["frames"])
        if r["grime"]:
            flags.append("grime")
        if r["cutout_m"]:
            flags.append("%.1f cm" % (r["cutout_m"] * 100))
        d.text((x, y + tile + 3), r["grd"], fill=(235, 235, 235))
        d.text((x, y + tile + 16), r["name"][:26], fill=(200, 200, 200))
        d.text((x, y + tile + 29), r["kind"], fill=c)
        d.text((x, y + tile + 42), ", ".join(flags)[:30], fill=(160, 160, 160))
    img.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rids", nargs="+", type=int)
    args = ap.parse_args()
    for rid in args.rids:
        rows = classify(rid)
        out = os.path.join(REPO, "build", "environment", "zone_%d" % rid)
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "textures.json"), "w", encoding="utf-8") as f:
            json.dump(rows, f, indent=1)
        sheet(rid, rows, os.path.join(out, "textures.png"))
        counts = {}
        for r in rows:
            counts[r["kind"]] = counts.get(r["kind"], 0) + 1
        print("zone %d: %d textures (%s) -> %s" % (rid, len(rows), ", ".join("%d %s" % (n, k) for k, n in counts.items()),
                                                  os.path.relpath(os.path.join(out, "textures.png"), REPO)))


if __name__ == "__main__":
    main()
