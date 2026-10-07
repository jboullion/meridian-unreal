"""
The original's sound, from the Server-104 Kod (docs/adr/0006 phase 1a): what each demo zone plays
and every sound the game names, so the remaster plays the same files by the same rules.

    python tools/audio/extract_audio.py            # -> data/audio/rooms.json, data/audio/sounds.json

rooms.json, per room id (data/zone_layout.json zones):
  music        the room's track (kod prMusic); the same track carries on across rooms
  loops        looping 3D sounds: the room's own plLooping_sounds plus its terrain's ambience
               (kod room.kod Constructed); each [sound, row, col] -> "pos_m" [x east, z south] in
               zone metres (glTF, the square's centre), "floor_m" (the lowest floor there, else the nearest,
               from the blockout), "volume" (the Kod max volume / 100; the 104 client ignored it)
  periodic     the terrain's random sounds (kod room.kod CreatePeriodicSounds): "sounds",
               "interval_ms" (20 s halved per terrain that adds sounds, kept to 2-20 s; +-20% each
               time), played at a random square of the room
  wading       the sound of walking in water (kod vrWading_Sound)
  door         the room's door sound (kod vrDoor_sound)
  terrain      the Kod TERRAIN_* flags, for reference
sounds.json: {file name: {"category", "loop", "used_by": [...]}} for every sound the demo can
play (marked "missing" when the original client doesn't ship the file): the rooms', the demo monsters' (data/monsters.json), the spells' (data/spells.json), the
player's and the system's (kod user.kod / player.kod resources), the crypt's levers and the
weather (data/environment/moods.json). Categories: music, loop, periodic, combat, spell, ui,
world, weather.
"""
from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "kod_extract"))
sys.path.insert(0, str(ROOT / "tools" / "environment"))
sys.path.insert(0, str(ROOT / "tools"))
from kodparse import KodIndex  # noqa: E402
from extract import parse_list_literal, matching  # noqa: E402
from server104 import SERVER104  # noqa: E402

KOD = SERVER104 / "kod"
# the original client's resources (as tools/ue/build_audio.py looks for them)
CLIENT_RES = [Path(os.environ.get("LOCALAPPDATA", "")) / "Meridian-104" / "resource",
              Path("H:/Steam/steamapps/common/Meridian 59/resource")]
OUT = ROOT / "data" / "audio"
M_PER_SQUARE = 2.2  # tools/roo2gltf/roo2gltf.py

# kod room.kod Constructed: the terrain's ambience loop, first match wins:
# (terrain flags, resource, row, col, radius, max volume, condition)
TERRAIN_LOOPS = [
    (("TERRAIN_BADLANDS", "TERRAIN_MOUNTAIN"), "room_badmount_sound", 1, 1, 300, 100),
    (("TERRAIN_BEACH",), "room_beach_sound", 1, 1, 300, 100),
    (("TERRAIN_JUNGLE",), "room_jungle_sound", 1, 1, 300, 100),
    (("TERRAIN_CAVES",), "room_cave_sound", 1, 1, 300, 70),
    (("TERRAIN_FOREST",), "room_forest_sound", 1, 1, 300, 100),   # not in guild halls
    (("TERRAIN_SEWERS",), "room_sewer_sound", 1, 1, 300, 100),
    (("TERRAIN_LAVA",), "room_lava_sound", 18, 14, 300, 100),
    (("TERRAIN_NECROPOLIS",), "room_necro_sound", 1, 1, 300, 100),  # only without the room's own loops
]


def terrain_periodic(has, res):
    """kod room.kod CreatePeriodicSounds -> (sounds, interval ms)."""
    interval, sounds = 20000, []
    if has("TERRAIN_BADLANDS") or has("TERRAIN_MOUNTAIN"):
        interval //= 2
        sounds += [res("room_badmount_sound%d" % i) for i in range(1, 5)]
        if not has("TERRAIN_JUNGLE"):
            sounds.append(res("room_badmount_sound5"))
    if has("TERRAIN_JUNGLE"):
        interval //= 4
        sounds += [res("room_jungle_sound%d" % i) for i in range(1, 26)]
    if has("TERRAIN_BEACH") or has("TERRAIN_LAKE"):
        interval //= 2
        sounds += [res("room_beach_sound%d" % i) for i in range(1, 8)]
        if has("TERRAIN_CITY"):
            sounds += [res("room_waterfront_sound1"), res("room_waterfront_sound2")]
    if has("TERRAIN_NECROPOLIS"):
        interval //= 2
        sounds += [res("room_necropolis_sound%d" % i) for i in range(1, 10)]
    if has("TERRAIN_CAVES") and not has("TERRAIN_LAVA"):
        sounds.append(res("room_cave_sound1"))
    if has("TERRAIN_FOREST") and not has("TERRAIN_GUILDHALL"):
        interval //= 2
        sounds += [res("room_forest_sound%d" % i) for i in range(1, 8)]
    if has("TERRAIN_SEWERS"):
        interval //= 2
        sounds += [res("room_sewer_sound%d" % i) for i in range(1, 5)]
    return [s for s in sounds if s], max(2000, min(20000, interval))


def own_looping_sounds(idx, cls):
    """The room class's own plLooping_sounds (set in its messages or properties; not Room's)."""
    for c in idx.chain(cls):
        if c.name.lower() == "room":
            break
        texts = list(c.messages.values()) + [c.properties.get("plLooping_sounds", "")]
        for text in texts:
            m = re.search(r"plLooping_sounds\s*=\s*\[", text)
            if not m:
                continue
            start = m.end() - 1
            literal = text[start: matching(text, start) + 1]
            return [x for x in parse_list_literal(literal) if isinstance(x, list)]
    return []


def square_m(row, col):
    """Kod row/col (1-based big squares) -> the square's centre in zone metres [x east, z south]."""
    return [round((col - 0.5) * M_PER_SQUARE, 3), round((row - 0.5) * M_PER_SQUARE, 3)]


class Floors:
    """The lowest upward-facing blockout surface under a point (zone metres): where a sound stands."""

    def __init__(self, glb):
        import blockout
        self.tris = []
        for prim in blockout.read_glb(glb).values():
            for t in prim.triangles:
                P = [prim.positions[i] for i in t]
                (ax, ay, az), (bx, by, bz), (cx, cy, cz) = P
                ny = (bz - az) * (cx - ax) - (bx - ax) * (cz - az)
                if ny > 1e-9:  # faces up (glTF y up, z south)
                    self.tris.append(P)

    def at(self, x, z):
        best = None
        for (ax, ay, az), (bx, by, bz), (cx, cy, cz) in self.tris:
            d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
            if abs(d) < 1e-12:
                continue
            u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d
            v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d
            w = 1 - u - v
            if min(u, v, w) < -1e-6:
                continue
            y = u * ay + v * by + w * cy
            best = y if best is None else min(best, y)
        if best is None and self.tris:
            # no floor right there (the room's corner square): the nearest one
            near = min(self.tris, key=lambda P: (sum(p[0] for p in P) / 3 - x) ** 2 + (sum(p[2] for p in P) / 3 - z) ** 2)
            best = sum(p[1] for p in near) / 3
        return best


def main():
    idx = KodIndex(KOD)
    layout = json.loads((ROOT / "data" / "zone_layout.json").read_text(encoding="utf-8"))
    terrain_flags = idx.const_names("TERRAIN_")
    sounds = {}

    def add(name, category, used_by, loop=False):
        if not name or not isinstance(name, str) or not name.lower().endswith((".ogg", ".wav")):
            return
        e = sounds.setdefault(name.lower(), {"file": name, "category": category, "loop": False, "used_by": []})
        e["loop"] = e["loop"] or loop
        if used_by not in e["used_by"]:
            e["used_by"].append(used_by)

    rooms = {}
    for z in layout["zones"]:
        cls = z["class"]
        if cls not in idx.classes:
            print("WARNING: no Kod class %s for room %s" % (cls, z["rid"]))
            continue
        terrain = idx.resolve_var(cls, "viTerrain_type") or 0
        names = [k for k, v in terrain_flags.items() if v and (v & (v - 1)) == 0 and terrain & v]

        def has(flag):
            return bool(terrain & terrain_flags.get(flag, 0))

        def res(name):
            r = idx.resource(cls, name)
            return r if isinstance(r, str) and r.lower().endswith((".ogg", ".wav")) else None

        loops = []
        own = own_looping_sounds(idx, cls)
        for item in own:
            snd = res(item[0])
            if not snd:
                continue
            nums = [idx.eval_in(cls, x) for x in item[1:]]
            loops.append({"sound": snd, "row": nums[0], "col": nums[1],
                          "volume": (nums[3] / 100.0) if len(nums) > 3 and isinstance(nums[3], int) else 1.0, "source": "room"})
        for flags, rname, row, col, radius, vol in TERRAIN_LOOPS:
            if any(has(f) for f in flags):
                if rname == "room_forest_sound" and has("TERRAIN_GUILDHALL"):
                    continue
                if rname == "room_necro_sound" and own:
                    break
                snd = res(rname)
                if snd:
                    loops.append({"sound": snd, "row": row, "col": col, "volume": vol / 100.0, "source": "terrain"})
                break
        floors = Floors(ROOT / z["mesh"]) if loops else None
        for lp in loops:
            lp["pos_m"] = square_m(lp["row"], lp["col"])
            f = floors.at(*lp["pos_m"])
            lp["floor_m"] = round(f, 3) if f is not None else 0.0
        periodic, interval = terrain_periodic(has, res)
        music = idx.resolve_var(cls, "prMusic")
        wading = idx.resolve_var(cls, "vrWading_Sound")
        door = idx.resolve_var(cls, "vrDoor_sound")
        rooms[str(z["rid"])] = {
            "class": cls, "terrain": names, "music": music if isinstance(music, str) else None,
            "loops": loops, "periodic": {"sounds": periodic, "interval_ms": interval} if periodic else None,
            "wading": wading if isinstance(wading, str) else None, "door": door if isinstance(door, str) else None,
            "grid_size_m": z.get("grid_size_m")}
        r = rooms[str(z["rid"])]
        add(r["music"], "music", "room %s" % z["rid"], loop=True)
        for lp in loops:
            add(lp["sound"], "loop", "room %s" % z["rid"], loop=True)
        for s in periodic:
            add(s, "periodic", "room %s" % z["rid"])
        add(r["wading"], "world", "room %s wading" % z["rid"])
        add(r["door"], "world", "room %s door" % z["rid"])
        # anything else the room's own code plays (the crypt's doors and levers)
        for c in idx.chain(cls):
            if c.name.lower() == "room":
                break
            for text in c.messages.values():
                for m in re.finditer(r"wave_rsc\s*=\s*(\w+)", text):
                    add(res(m.group(1)), "world", "room %s" % z["rid"])

    # the demo's monsters, all spells
    for m in json.loads((ROOT / "data" / "monsters.json").read_text(encoding="utf-8")):
        for k in ("vrSound_aware", "vrSound_hit", "vrSound_miss", "vrSound_death"):
            add(m.get(k), "combat", "%s %s" % (m["class"], k[len("vrSound_"):]))
    for s in json.loads((ROOT / "data" / "spells.json").read_text(encoding="utf-8")):
        add(s.get("vrSucceed_wav"), "spell", "spell %s" % s["class"])
    # the player's and the system's: every sound resource of the player classes
    ui = re.compile(r"save|safety|welcome|login|logged|cannot|cant_|improved|tougher|level|learn", re.I)
    for cls in ("User", "Player"):
        for c in idx.chain(cls):
            for k, v in c.resources.items():
                if isinstance(v, str) and v.lower().endswith((".ogg", ".wav")):
                    add(v, "ui" if ui.search(k) else "combat", "%s %s" % (c.name, k))
    # weather (docs/adr/0005 phase 4)
    moods = json.loads((ROOT / "data" / "environment" / "moods.json").read_text(encoding="utf-8"))
    for k, v in moods.get("weather", {}).get("sounds", {}).items():
        if not k.startswith("_") and isinstance(v, str):
            add(v + ".ogg", "weather", "weather %s" % k, loop=k in ("rain", "wind"))

    # sounds the Kod names that the client doesn't ship (the importer skips them, the test knows)
    shipped = {f.name.lower() for d in CLIENT_RES if d.is_dir() for f in d.iterdir()}
    for e in sounds.values():
        if shipped and e["file"].lower() not in shipped:
            e["missing"] = True
    OUT.mkdir(parents=True, exist_ok=True)
    doc = {"_doc": __doc__.strip().split("\n\n")[0] + " See tools/audio/extract_audio.py and docs/adr/0006-audio.md."}
    (OUT / "rooms.json").write_text(json.dumps(dict(doc, rooms=rooms), indent=1), encoding="utf-8")
    (OUT / "sounds.json").write_text(json.dumps(dict(doc, sounds=dict(sorted(sounds.items()))), indent=1), encoding="utf-8")
    by_cat = {}
    for e in sounds.values():
        by_cat[e["category"]] = by_cat.get(e["category"], 0) + 1
    print("rooms: %d; sounds: %d %s" % (len(rooms), len(sounds), by_cat))
    for rid, r in rooms.items():
        print("  %s %-12s music %-12s loops %s periodic %s" % (
            rid, r["class"], r["music"], [(lp["sound"], lp["pos_m"], lp["floor_m"]) for lp in r["loops"]],
            r["periodic"] and (len(r["periodic"]["sounds"]), r["periodic"]["interval_ms"])))


if __name__ == "__main__":
    main()
