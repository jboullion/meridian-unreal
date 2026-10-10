"""
Every map in the world and how far each is remastered (docs/world-maps.md).

The unit is the .roo file: the client picks a built zone by its room file and checksum
(UMRZoneSubsystem::RidForRoom), so one remastered .roo serves every Kod room that uses it (the
Death Realm reuses the overland maps, the Black Desert's twelve dunes share one).

Reads:
  data/net/rooms.json                  every Kod room: rid, class, roo, name, links (tools/kod_extract)
  the Kod room folders (Server-104)    which folder each room class is in (razarm/, monsroom/desert/...)
  the room files                       Server-104/resource/rooms and the installed client's resource folder
  data/zones.json, data/zone_layout.json, data/environment/zone_<rid>.json, moods.json, lookdev_cameras.json
                                       what each built zone has
  data/world/regions.json              hand-kept: each .roo's pass and region, a note, and the date its art
                                       pass was approved

Writes data/world/maps.json and docs/world-maps.md.

  python tools/world/map_inventory.py           # the list (fails if a used .roo has no region)
  python tools/world/map_inventory.py --seed    # add a drafted region for every .roo regions.json lacks
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server104 import SERVER104  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "data"
REGIONS = DATA / "world" / "regions.json"
MAPS = DATA / "world" / "maps.json"
DOC = ROOT / "docs" / "world-maps.md"
KOD_ROOMS = SERVER104 / "kod" / "object" / "active" / "holder" / "room"

PASSES = {
    1: "Raza",
    2: "Near Raza",
    3: "Cities and their surroundings",
    4: "Overland",
    5: "Caves and castles",
    6: "Brax",
    7: "The islands",
    8: "Special and unused",
}

# --------------------------------------------------------------------------------- the seed rules

# A city's Kod folder -> (pass, region)
FOLDERS = {
    "razarm": (1, "Raza"),
    "marnrm": (3, "Marion"),
    "jasperrm": (3, "Jasper"),
    "crnthrm": (3, "Cor Noth"),
    "barlqrm": (3, "Barloque"),
    "tosrm": (3, "Tos"),
    "bergrm": (3, "Berg"),
    "jixarm": (3, "Jixa"),
    "kocarm": (7, "Ko'catan"),
    "monsroom/kcforest": (7, "Ko'catan jungle"),
    "monsroom/feyforst": (4, "Fey Forest"),
    "monsroom/desert": (4, "Black Desert"),
    "monsroom/desert/desertdunes": (4, "Black Desert"),
    "monsroom/maze": (5, "Labyrinth"),
    "monsroom/deathrealm": (8, "Death Realm"),
    "monsroom/oldtown": (8, "Historic towns"),
}

# Everything the folders don't settle, by room file (lower case, without .roo)
EXPLICIT: dict[tuple[int, str], list[str]] = {
    (1, "Raza"): ["razacrypt"],
    (2, "Near Raza"): ["razaforest", "farolwest", "b6", "c6", "c7", "guildh7"],
    (3, "Marion"): ["marsmith", "marinn", "marelder", "greenplantation", "workerhome1", "mardun01", "mardun02", "mardun03",
                    "marcrypt4", "c5", "guildh17"],
    (3, "Jasper"): ["jassew1", "jassew2", "jassew3", "sewking", "guildh3", "guildh4", "guildh10", "f8", "e7"],
    (3, "Cor Noth"): ["weaponsm", "g4", "g5", "guildh1", "guildh6"],
    (3, "Barloque"): ["barlbar1", "barlsew", "barlsew2", "barlsew3", "h3", "i3", "guildh2", "guildh8", "guildh9", "guildh14", "guildh16"],
    (3, "Tos"): ["tosinn", "toscellar", "tossecret", "tosgrave", "toscrypt", "h6", "i6", "toseast_mg", "guildh5"],
    (3, "Berg"): ["bergmg", "berg1", "berg2", "berg4", "berg_jala", "berg_grave", "guildh20"],
    (3, "Jixa"): ["ji3", "ji4"],
    (4, "Ilerian Woods"): ["c4", "d4", "d5", "e2", "e4", "f2", "f3", "f4", "temple", "guildh11", "a5", "a6"],
    (4, "Forest of Farol"): ["d6", "d7", "d6e6", "e5", "e6"],
    (4, "The King's Way and the Cragged Mountains"): ["f6", "f7", "g6", "g8", "g9", "h4", "h5", "h7", "h9", "i7", "i8", "i9", "hermhut",
                                                      "tempkra"],
    (4, "The Queen's Way and the Badlands"): ["j3", "j3_1", "j3_2", "survival_ex", "badland1", "badland2", "k5", "waylayoasis"],
    (4, "Kardde's Pass and the Valley of Death"): ["berg3", "berg5", "berg6", "berg7", "berg8", "dvalley1", "dvalley2", "dvalley3",
                                                    "dvalley4", "dvalley5"],
    (4, "The Duke's Way and the marshes"): ["emgtoswamp", "swamptojixa", "swamp1", "swamp2", "ji1", "ji2", "guildh19"],
    (5, "Castle Victoria"): ["castle1", "castle1b", "castle1c", "castle1d", "throne1", "throne2", "greeny"],
    (5, "The Bourgeois Castle"): ["castle2a", "castle2b", "castle2c", "castle2d", "castle2e", "mockers", "bizmans"],
    (5, "Blackstone Keep"): ["duke1", "duke2", "duke3", "duke4", "duke5"],
    (5, "Caves"): ["cave2", "cave3", "nest1", "icecave1", "ratcave1", "ratcave2", "ratcave3", "d6e6ulake", "golem_dungeon",
                   "bergmine", "berg3_cave", "banditcamp", "banditcave"],
    (5, "Ugol's Warren (the orc caves)"): ["oc01", "oc02", "oc03", "oc04", "oc05", "oc06", "ocpa", "ocpb", "oc1a", "oc5b", "ocp1"],
    (5, "Labyrinth"): ["desertmazeentrance"],
    (5, "The Valley's temples"): ["dvalley5b", "dvalley_temple"],
    (6, "Brax"): ["necarea1", "necarea2", "necarea3", "necarea4", "necarea5", "necarea6", "nec3a", "nec3b", "lichmaze", "guildh15",
                  "necarena"],
    (7, "Ko'catan"): ["kochoh", "kocsew1", "kocsew2", "guildh12", "guildh13"],
    (7, "Ko'catan jungle"): ["bowmaker", "tpost1", "tpost2", "ke1"],
    (8, "Not linked from anywhere"): ["forest1", "forest2", "forest3", "forest4", "forest5", "field1", "canyons", "canyons2", "tempqor"],
    (8, "Gods, tests and the Underworld"): ["uworld", "outgrace", "zndramas", "univ"],
    (8, "Death Realm"): ["dcastle", "dbarlsew"],
}


def roo_key(name: str) -> str:
    return name.lower().removesuffix(".roo")


# --------------------------------------------------------------------------------- reading

def read_json(path: Path, default=None):
    if not path.exists():
        return default
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def kod_folders() -> dict[str, str]:
    """Room class (lower case) -> its folder under the Kod room tree ("razarm", "monsroom/desert")."""
    out = {}
    for dirpath, _, files in os.walk(KOD_ROOMS):
        for f in files:
            if not f.endswith(".kod"):
                continue
            text = open(os.path.join(dirpath, f), encoding="latin-1").read()
            m = re.search(r"^\s*(\w+)\s+is\s+\w+", text, re.M)
            if m:
                out[m.group(1).lower()] = os.path.relpath(dirpath, KOD_ROOMS).replace(os.sep, "/")
    return out


def room_files() -> set[str]:
    """Every .roo we can see: the reference checkout's and the installed client's."""
    dirs = [SERVER104 / "resource" / "rooms"]
    local = os.environ.get("LOCALAPPDATA")
    if local:
        dirs.append(Path(local) / "Meridian-104" / "resource")
    found = set()
    for d in dirs:
        if d.is_dir():
            found |= {roo_key(f) for f in os.listdir(d) if f.lower().endswith(".roo")}
    return found


def camera_zones(layout: list[dict], cameras: list[dict]) -> dict[int, list[str]]:
    """Look-dev cameras by the built zone they stand in (zone_layout bounds_m at world_origin_cm)."""
    out = defaultdict(list)
    for cam in cameras:
        x, y = cam["location_cm"][0], cam["location_cm"][1]
        for z in layout:
            ox, oy = z["world_origin_cm"][0], z["world_origin_cm"][1]
            lo, hi = z["bounds_m"]["min"], z["bounds_m"]["max"]
            if ox + 100 * lo[0] <= x <= ox + 100 * hi[0] and oy + 100 * lo[2] <= y <= oy + 100 * hi[2]:
                out[z["rid"]].append(cam["name"])
    return out


# --------------------------------------------------------------------------------- the list

def seed_region(roo: str, rooms: list[dict], folders: dict[str, str]) -> tuple[int, str] | None:
    for key, names in EXPLICIT.items():
        if roo in names:
            return key
    # the folder of the first room using it that isn't the Death Realm's copy
    ordered = sorted(rooms, key=lambda r: (folders.get(r["class"].lower(), "") == "monsroom/deathrealm", r["rid"]))
    for r in ordered:
        f = folders.get(r["class"].lower())
        if f in FOLDERS:
            return FOLDERS[f]
    return None


def write_regions(regions: dict) -> None:
    """regions.json with one line per map, so it reads (and diffs) as a table."""
    lines = ["{", f' "_doc": {json.dumps(regions["_doc"], ensure_ascii=False)},',
             f' "city_order": {json.dumps(regions["city_order"], ensure_ascii=False)},', ' "maps": {']
    items = list(regions["maps"].items())
    for i, (roo, reg) in enumerate(items):
        lines.append(f"  {json.dumps(roo)}: {json.dumps(reg, ensure_ascii=False)}" + ("," if i < len(items) - 1 else ""))
    lines += [" }", "}", ""]
    REGIONS.parent.mkdir(parents=True, exist_ok=True)
    with open(REGIONS, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def build(args) -> int:
    rooms = read_json(DATA / "net" / "rooms.json")["rooms"]
    folders = kod_folders()
    files = room_files()
    zones = read_json(DATA / "zones.json", [])
    layout = read_json(DATA / "zone_layout.json", {"zones": []})["zones"]
    moods = read_json(DATA / "environment" / "moods.json", {}).get("zones", {})
    cameras = read_json(DATA / "environment" / "lookdev_cameras.json", {"cameras": []})["cameras"]
    regions = read_json(REGIONS, {"_doc": "", "city_order": [], "maps": {}})

    by_roo: dict[str, list[dict]] = defaultdict(list)
    for r in rooms:
        by_roo[roo_key(r["roo"])].append(r)

    if args.seed:
        added = 0
        for roo, rs in sorted(by_roo.items()):
            if roo in regions["maps"]:
                continue
            got = seed_region(roo, rs, folders)
            if got:
                regions["maps"][roo] = {"pass": got[0], "region": got[1]}
                added += 1
        for roo in sorted(files - set(by_roo)):
            regions["maps"].setdefault(roo, {"pass": 8, "region": "Not used by any Kod room"})
        regions["maps"] = dict(sorted(regions["maps"].items()))
        write_regions(regions)
        print(f"seeded {added} regions into {REGIONS.relative_to(ROOT)}")

    # what each built zone has
    built: dict[str, dict] = {}
    cams = camera_zones(layout, cameras)
    shares = {z["rid"]: z["shares_geometry_with"]["rid"] for z in layout if "shares_geometry_with" in z}
    for z in zones:
        rid = z["rid"]
        art_rid = shares.get(rid, rid)
        built[roo_key(z["roo"])] = {
            "zone": rid,
            "art": (DATA / "environment" / f"zone_{art_rid}.json").exists(),
            "lighting": str(rid) in moods,
            "cameras": sorted(set(cams.get(rid, []))),
        }

    missing = []
    entries = []
    for roo in sorted(set(by_roo) | files):
        reg = regions["maps"].get(roo)
        if not reg:
            missing.append(roo)
            continue
        b = built.get(roo)
        if b and reg.get("approved") and b["art"] and b["cameras"]:
            status = "remastered"
        elif b and b["art"]:
            status = "art in progress"
        elif b:
            status = "built"
        else:
            status = "runtime"
        todo = []
        if status != "remastered":
            if not b:
                todo.append("build")
            else:
                todo += [n for n, ok in (("art config", b["art"]), ("lighting", b["lighting"]), ("cameras", bool(b["cameras"]))) if not ok]
            todo.append("approval")
        entries.append({
            "roo": roo,
            "pass": reg["pass"],
            "region": reg["region"],
            "status": status,
            "rooms": [{"rid": r["rid"], "class": r["class"], "name": r["name"].strip(),
                       "folder": folders.get(r["class"].lower(), "")} for r in sorted(by_roo.get(roo, []), key=lambda r: r["rid"])],
            "on_disk": roo in files,
            "built": b,
            "approved": reg.get("approved"),
            "to_do": todo,
            "note": reg.get("note", ""),
        })
    if missing:
        print("No region in data/world/regions.json for: " + ", ".join(missing), file=sys.stderr)
        print("Add them by hand, or run with --seed for a draft.", file=sys.stderr)
        return 1

    MAPS.parent.mkdir(parents=True, exist_ok=True)
    with open(MAPS, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"_doc": "Generated by tools/world/map_inventory.py: every .roo, its rooms, region, pass and how far it's "
                           "remastered. Edit data/world/regions.json, not this.", "maps": entries}, f, indent=1, ensure_ascii=False)
        f.write("\n")
    write_doc(entries, regions, len(rooms))
    counts = defaultdict(int)
    for e in entries:
        counts[e["status"]] += 1
    print(f"{len(entries)} maps ({len(rooms)} Kod rooms): " + ", ".join(f"{n} {s}" for s, n in sorted(counts.items())))
    print(f"wrote {MAPS.relative_to(ROOT)} and {DOC.relative_to(ROOT)}")
    return 0


# --------------------------------------------------------------------------------- the page

STATUS_ORDER = ["remastered", "art in progress", "built", "runtime"]


def write_doc(entries: list[dict], regions: dict, kod_rooms: int) -> None:
    used = [e for e in entries if e["rooms"]]
    out = ["# The world's maps", "",
           "Generated by `tools/world/map_inventory.py` (`npm run maps`); edit `data/world/regions.json`, not this page.",
           "",
           "Every map is a `.roo` file. The client picks a built zone by its room file and checksum, so one remastered `.roo` "
           "serves every Kod room that uses it (the Death Realm reuses the overland maps).",
           "",
           "**Status:** *remastered* = built, with an art config (`data/environment/zone_<rid>.json`), look-dev cameras and the "
           "maintainer's approval; *art in progress* = an art config, not approved yet; *built* = a UE level (upscaled textures, "
           "props) without an art pass; *runtime* = built from the server's file when entered, with the original look.",
           "",
           "## Summary",
           "",
           f"{len(entries)} maps: {len(used)} used by the {kod_rooms} Kod rooms, {len(entries) - len(used)} used by none.",
           "",
           "| Pass | " + " | ".join(s.capitalize() for s in STATUS_ORDER) + " | Maps |",
           "|---|" + "---|" * (len(STATUS_ORDER) + 1)]
    totals = defaultdict(int)
    for p, title in PASSES.items():
        es = [e for e in entries if e["pass"] == p]
        row = [sum(1 for e in es if e["status"] == s) for s in STATUS_ORDER]
        for s, n in zip(STATUS_ORDER, row):
            totals[s] += n
        out.append(f"| {p}. {title} | " + " | ".join(str(n) for n in row) + f" | {len(es)} |")
    out.append("| **All** | " + " | ".join(f"**{totals[s]}**" for s in STATUS_ORDER) + f" | **{len(entries)}** |")
    out += ["", "Within pass 3 the cities go in this order: " + ", ".join(regions.get("city_order", [])) + ".", ""]

    order = {c: i for i, c in enumerate(regions.get("city_order", []))}
    for p, title in PASSES.items():
        es = [e for e in entries if e["pass"] == p]
        if not es:
            continue
        out += [f"## {p}. {title}", ""]
        regs = sorted({e["region"] for e in es}, key=lambda r: (order.get(r, 999), r))
        for reg in regs:
            rs = sorted((e for e in es if e["region"] == reg), key=lambda e: (STATUS_ORDER.index(e["status"]), e["rooms"][0]["rid"] if e["rooms"] else 0, e["roo"]))
            done = sum(1 for e in rs if e["status"] == "remastered")
            out += [f"### {reg} ({done} of {len(rs)} remastered)", "",
                    "| Map | Rooms | Status | To do |", "|---|---|---|---|"]
            for e in rs:
                if e["rooms"]:
                    rooms = "; ".join(f"{r['rid']} {r['name']}" for r in e["rooms"][:4])
                    if len(e["rooms"]) > 4:
                        rooms += f"; and {len(e['rooms']) - 4} more"
                else:
                    rooms = "(no Kod room)"
                status = e["status"] + (f" ({e['approved']})" if e["approved"] else "")
                todo = ". ".join(t for t in (", ".join(e["to_do"]).capitalize(), e["note"]) if t)
                out.append(f"| `{e['roo']}.roo` | {rooms} | {status} | {todo} |")
            out.append("")
    with open(DOC, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", action="store_true", help="draft a region for every .roo data/world/regions.json lacks")
    return build(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())
