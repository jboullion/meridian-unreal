"""
Zone art configs for newly built zones, the world remaster's batch recipe (docs/adr/0003 "Pass 3: Jasper";
tools/world/overnight.py runs it): what the Marion and Jasper passes wrote by hand.

    python tools/world/zone_configs.py 350 351 352            # write what's missing for these zones
    python tools/world/zone_configs.py --dry-run --built      # compare the rules with every built zone's profile

Per zone (each written only when it isn't there yet, so a hand-made config always wins):
- data/environment/zone_<rid>.json: "rebuild": ["cutouts"] with one cut-outs entry over the whole
  blockout; outdoors grass tufts on its grass floors; inside and underground the indoor grime.
- data/environment/moods.json "zones" profile: outdoor raza_outdoor (the room's weather zone and
  mask), interior raza_interior (daylight from the outside factor), underground raza_crypt below base
  light 164, else raza_interior, lamps always on.
- data/environment/materials.json "ground": its stone, earth and grass floors, world-aligned.
- data/environment/lookdev_cameras.json: two open views (tools/lookdev/suggest_cameras.py --open),
  out_<map> / int_<map> and _2.

The kind of zone: underground when the outside factor is 0; outdoor when less than half its floor
(water left out) has a ceiling, whatever the outside factor (the Arena of Kraanan and the Black
Desert have 5; the Raza Inn has 10 under a roof); interior otherwise.

Reads data/zones.json, data/zone_layout.json and build/environment/zone_<rid>/textures.json
(tools/environment/zone_textures.py, run first).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "lookdev"))
import suggest_cameras  # noqa: E402
sys.path.insert(0, os.path.join(REPO, "tools", "environment"))
import blockout  # noqa: E402

ENV = os.path.join(REPO, "data", "environment")
DATE = "2026-10-10"

GRASS = re.compile(r"grass", re.I)
NOT_GRASS = re.compile(r"cobble|stone|path|brick|tile|rock|roof|wall", re.I)
GROUND = re.compile(r"rock|cobble|dirt|grass|sand|path|stone|flagston|tile floor|earth|mud|gravel", re.I)
GROUND_STONES = re.compile(r"cobble|stone|path|flagston", re.I)


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def textures(rid):
    path = os.path.join(REPO, "build", "environment", "zone_%d" % rid, "textures.json")
    if not os.path.exists(path):
        raise SystemExit("no %s (run tools/environment/zone_textures.py %d)" % (path, rid))
    return load(path)


def covered_share(rid):
    """The share of the blockout's floor with a ceiling over it: ceiling area over floor area, water floors
    left out (a hall whose map holds a sea, Barloque's Adventurer's Hall, would read as open). From the
    geometry, not textures.json, since a texture can be both floor and ceiling."""
    import numpy as np
    catalog = load(os.path.join(REPO, "build", "textures", "catalog.json"))["textures"]
    floor = ceiling = 0.0
    for key, prim in blockout.read_glb(suggest_cameras.zone_glb(rid)).items():
        if WATER.search((catalog.get(key.split("__")[0]) or {}).get("name", "")):
            continue
        P = np.array(prim.positions, float)
        tri = np.array(prim.triangles)
        if not len(tri):
            continue
        n = np.cross(P[tri[:, 1]] - P[tri[:, 0]], P[tri[:, 2]] - P[tri[:, 0]])
        ln = np.linalg.norm(n, axis=1) + 1e-9
        up, area = n[:, 1] / ln, ln / 2
        floor += area[up > 0.7].sum()
        ceiling += area[up < -0.7].sum()
    return float(ceiling / floor) if floor else 1.0


def kind_of(zone, tex):
    outside = zone.get("outside_factor") or 0
    if not isinstance(outside, int):
        outside = 0
    if outside == 0:
        return "underground"
    if covered_share(zone["rid"]) < 0.5:
        return "outdoor"  # open to the sky: an arena, a desert, a canyon (outside factor 5, the Black Desert)
    return "interior"


def profile(zone, kind, tex):
    base = zone.get("base_light")
    base = base if isinstance(base, int) else 164
    outside = zone.get("outside_factor") or 0
    if kind == "outdoor":
        amb = {"pollen": 1, "fireflies": 1}
        if any(t["kind"] == "foliage cut-out" for t in tex):
            amb["leaves"] = 1
        return {"cycle": "raza_outdoor", "weather_zone": zone.get("weather_zone") or 0,
                "weather_mask": zone.get("weather_mask") or "DEFAULT", "ambient": amb}
    if kind == "interior":
        return {"cycle": "raza_interior", "kind": "interior", "sun": False, "daylight": round(outside / 10, 2),
                "base_light": base, "outside": outside, "ambient": {"motes": 1}}
    return {"cycle": "raza_interior" if base >= 164 else "raza_crypt", "kind": "underground", "sun": False,
            "daylight": 0.0, "lamps": "always", "base_light": base, "outside": 0, "weather_zone": 0,
            "ambient": {"motes": 1}}


def grass_floors(tex):
    return sorted(t["grd"] for t in tex if t["where"] == "floor" and t["kind"] == "surface"
                  and GRASS.search(t["name"]) and not NOT_GRASS.search(t["name"]))


WATER = re.compile(r"water|ocean", re.I)  # by name: "Water ... Scroll", "Water Pool", Ko'catan's "Island: Ocean 2 Closer"
SEA_M2 = 30000.0  # more water than this: the sea (Barloque's ports), a flat bed; less: a river or a lake


def water_entry(tex, region):
    """Outdoors, the original's water floors (Water ... Scroll, Water Pool, Island: Ocean...) as a water surface over a
    sunken bed (Raza's pond, Cor Noth's river; Barloque's sea with "shelf": false), or None."""
    # by name, or by hand (data/world/tags.json "ground_overrides": the Black Desert's sea is
    # "Set MStore Floor 5", grd08968)
    forced = {k for k, v in load(os.path.join(REPO, "data", "world", "tags.json")).get("ground_overrides", {}).items() if v == "water"}
    mats = sorted({t["grd"] for t in tex if t["where"] == "floor" and (WATER.search(t["name"]) or t["grd"] in forced)})
    area = sum(t["area_m2"] for t in tex if t["grd"] in mats)
    if not mats or area < 50:
        return None
    entry = {"name": "Water", "kind": "water", "region_m": region, "materials": mats, "bed_texture": "grd09629"}
    if area > SEA_M2:
        entry.update({"bed_depth_m": 3.0, "shelf": False, "_doc": "the sea or a great lake: a water surface over a flat bed 3 m down"})
    else:
        entry.update({"bed_depth_m": 0.9, "_doc": "rivers and lakes: a water surface over a shelving bed"})
    return entry


def zone_config(zone, layout, kind, tex):
    name = (zone.get("name") or zone["class"]).strip()
    b = layout["bounds_m"]
    region = [round(b["min"][0] - 1, 1), round(b["min"][2] - 1, 1), round(b["max"][0] + 1, 1), round(b["max"][2] + 1, 1)]
    cut = {"name": "Cutouts", "kind": "cutouts", "region_m": region, "displacement": "none"}
    if kind == "outdoor":
        grass = grass_floors(tex)
        cut["_doc"] = "every non-foliage cut-out, rebuilt as a solid"
        cfg = {"_doc": "Zone art for %s (rid %d), the world remaster's batch recipe (tools/world/zone_configs.py, %s): "
                       "outdoors, no buildings rebuilt yet. Cut-outs become solids; grime at the walls' feet%s."
                       % (name, zone["rid"], DATE, "; grass tufts on the grass" if grass else ""),
               "rid": zone["rid"], "rebuild": ["cutouts"], "buildings": [cut]}
        water = water_entry(tex, region)
        if water:
            cfg["buildings"].insert(0, water)
        if grass:
            cfg["scatter"] = [{"name": "grass", "_doc": "Grass tufts on the grass floors, as in the forest ring (zone_536.json).",
                               "meshes": ["SM_GrassClump_A", "SM_GrassClump_B", "SM_GrassClump_C"],
                               "per_m2": {g: 6.0 for g in grass}, "scale": [0.7, 1.4], "cull_m": [28, 42],
                               "shadows": True, "seed": 1, "keep_out_m": 0.12}]
        return cfg
    cut["_doc"] = "every cut-out, rebuilt as a solid"
    return {"_doc": "Zone art for %s (rid %d), the world remaster's batch recipe (tools/world/zone_configs.py, %s): "
                    "the interiors' recipe. Cut-outs become solids; indoor grime along the wall feet and under the ceiling."
                    % (name, zone["rid"], DATE),
            "rid": zone["rid"], "rebuild": ["cutouts"], "buildings": [cut],
            "grime": {"base": "indoor_base", "ceiling": "ceiling"}}


def ground_entries(tex, have):
    out = {}
    for t in tex:
        if t["where"] == "floor" and t["kind"] == "surface" and t["grd"] not in have and t["grd"] not in out \
                and t["grd"].startswith("grd") and GROUND.search(t["name"]):
            out[t["grd"]] = {"tile_m": 2.2, "normal_strength": 1.0} if GROUND_STONES.search(t["name"]) else {"tile_m": 2.2}
    return out


# ------------------------------------------------------------------------------ writing, keeping each file's format

def insert_moods(lines):
    path = os.path.join(ENV, "moods.json")
    s = open(path, encoding="utf-8").read()
    end = s.index('\n  },\n  "atmosphere"')
    s = s[:end] + ",\n" + ",\n".join(lines) + s[end:]
    json.loads(s)
    open(path, "w", encoding="utf-8").write(s)


def insert_ground(entries):
    path = os.path.join(ENV, "materials.json")
    s = open(path, encoding="utf-8").read()
    g = s.index('"ground": {')
    at = s.index('\n  "grd', g) + 1
    block = "".join('  "%s": {\n' % k + ",\n".join('   "%s": %s' % (a, json.dumps(b)) for a, b in v.items()) + "\n  },\n"
                    for k, v in entries.items())
    s = s[:at] + block + s[at:]
    json.loads(s)
    open(path, "w", encoding="utf-8").write(s)


def insert_cameras(cams):
    path = os.path.join(ENV, "lookdev_cameras.json")
    s = open(path, encoding="utf-8").read()
    end = s.rindex("\n  ]")
    s = s[:end] + ",\n" + ",\n".join("    " + json.dumps(c) for c in cams) + s[end:]
    json.loads(s)
    open(path, "w", encoding="utf-8").write(s)


def add_water(rids, zones, layouts):
    """Give existing outdoor configs the water entry the recipe now writes (kept when there is one)."""
    moods = load(os.path.join(ENV, "moods.json"))["zones"]
    for rid in rids:
        if moods.get(str(rid), {}).get("kind", "outdoor") != "outdoor":
            continue
        cfg_path = os.path.join(ENV, "zone_%d.json" % rid)
        if not os.path.exists(cfg_path):
            continue
        cfg = load(cfg_path)
        if any(b.get("kind") == "water" for b in cfg["buildings"]):
            continue
        water = water_entry(textures(rid), cfg["buildings"][-1]["region_m"])
        if not water:
            continue
        cfg["buildings"].insert(0, water)
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write(json.dumps(cfg, indent=2) + "\n")
        print("%5d water: %s%s" % (rid, ", ".join(water["materials"]), " (sea)" if water.get("shelf") is False else ""))


def reclassify(rids, zones, layouts):
    """Rooms whose kind the rules now read differently (2026-10-12: open-sky rooms with an outside factor
    under 8, the Arena of Kraanan, the Black Desert): rewrite their mood profile, and in their art config
    the parts that follow the kind (indoor grime, grass tufts), keeping hand-made entries (water)."""
    path = os.path.join(ENV, "moods.json")
    s = open(path, encoding="utf-8").read()
    changed = []
    for rid in rids:
        zone, layout = zones[rid], layouts[rid]
        tex = textures(rid)
        kind = kind_of(zone, tex)
        line = '    "%d": %s' % (rid, json.dumps(profile(zone, kind, tex)))
        s, n = re.subn(r'^    "%d": \{[^\n]*\}' % rid, lambda m: line, s, flags=re.M)
        if not n:
            continue
        cfg_path = os.path.join(ENV, "zone_%d.json" % rid)
        cfg = load(cfg_path) if os.path.exists(cfg_path) else zone_config(zone, layout, kind, tex)
        fresh = zone_config(zone, layout, kind, tex)
        cfg.pop("grime", None)
        if "grime" in fresh:
            cfg["grime"] = fresh["grime"]
        if "scatter" in fresh and "scatter" not in cfg:
            cfg["scatter"] = fresh["scatter"]
        cfg["_doc"] = fresh["_doc"]
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write(json.dumps(cfg, indent=2) + "\n")
        changed.append((rid, kind))
    json.loads(s)
    open(path, "w", encoding="utf-8").write(s)
    for rid, kind in changed:
        print("%5d %-22s now %s" % (rid, zones[rid]["class"][:22], kind))
    print("reclassified %d rooms (rebuild their art: build_zone_art.py, then build_world.ps1)" % len(changed))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rids", nargs="*", type=int)
    ap.add_argument("--built", action="store_true", help="every zone in data/zone_layout.json")
    ap.add_argument("--dry-run", action="store_true", help="write nothing; compare with the existing profiles")
    ap.add_argument("--no-cameras", action="store_true")
    ap.add_argument("--water", action="store_true", help="add the water entry to these rooms' existing outdoor configs")
    ap.add_argument("--reclassify", action="store_true", help="rewrite the profile and the kind's parts of the config for these rooms")
    args = ap.parse_args()
    zones = {z["rid"]: z for z in load(os.path.join(REPO, "data", "zones.json"))}
    layouts = {z["rid"]: z for z in load(os.path.join(REPO, "data", "zone_layout.json"))["zones"]}
    moods = load(os.path.join(ENV, "moods.json"))["zones"]
    ground = load(os.path.join(ENV, "materials.json"))["ground"]
    cam_names = {c["name"] for c in load(os.path.join(ENV, "lookdev_cameras.json"))["cameras"]}
    rids = sorted(layouts) if args.built else args.rids
    if args.reclassify:
        reclassify(rids, zones, layouts)
        return
    if args.water:
        add_water(rids, zones, layouts)
        return

    mood_lines, new_ground, new_cams, mismatches = [], {}, [], 0
    for rid in rids:
        zone, layout = zones.get(rid), layouts.get(rid)
        if not zone or not layout or "shares_geometry_with" in layout:
            continue
        tex = textures(rid)
        kind = kind_of(zone, tex)
        prof = profile(zone, kind, tex)
        if args.dry_run:
            have = moods.get(str(rid))
            if have:
                got = (have.get("kind", "outdoor"), have.get("cycle"))
                want = (prof.get("kind", "outdoor"), prof.get("cycle"))
                weather = (have.get("weather_zone"), prof.get("weather_zone")) if kind == "outdoor" else None
                flag = "" if got == want else "  <-- kind/cycle differ"
                if weather and weather[0] is not None and weather[0] != weather[1]:
                    flag += "  <-- weather %s here, Kod %s" % weather
                mismatches += bool(flag)
                print("%5d %-22s %-28s rules %-28s%s" % (rid, zone["class"][:22], "%s/%s" % got, "%s/%s" % want, flag))
            continue
        cfg_path = os.path.join(ENV, "zone_%d.json" % rid)
        if not os.path.exists(cfg_path):
            with open(cfg_path, "w", encoding="utf-8") as f:
                f.write(json.dumps(zone_config(zone, layout, kind, tex), indent=2) + "\n")
        if str(rid) not in moods:
            mood_lines.append('    "%d": %s' % (rid, json.dumps(prof)))
        new_ground.update(ground_entries(tex, set(ground) | set(new_ground)))
        if not args.no_cameras:
            stem = re.sub(r"\.roo$", "", (zone.get("roo") or str(rid)).lower())
            name = ("out_" if kind == "outdoor" else "int_") + re.sub(r"[^a-z0-9]+", "_", stem)
            if name not in cam_names:
                try:
                    cams = suggest_cameras.open_views(rid, layout, name, outdoor=kind == "outdoor")
                    new_cams += cams
                    cam_names |= {c["name"] for c in cams}
                except SystemExit as e:
                    print("cameras: %s" % e, file=sys.stderr)
        print("%5d %-22s %s" % (rid, zone["class"][:22], kind))
    if args.dry_run:
        print("%d of the existing profiles differ from the rules" % mismatches)
        return
    if mood_lines:
        insert_moods(mood_lines)
    if new_ground:
        insert_ground(new_ground)
    if new_cams:
        insert_cameras(new_cams)
    print("wrote: %d mood profiles, %d ground floors, %d cameras" % (len(mood_lines), len(new_ground), len(new_cams)))


if __name__ == "__main__":
    main()
