"""
Every Kod-placed object in the demo zones -> its sprite (.bgf), name and count per zone: the "pick
the object" step of the sprite-to-3d pipeline (docs/adr/0007).

    python tools/aigen/inventory.py            # -> data/aigen/inventory.json, and a table on stdout
    python tools/aigen/inventory.py --world    # every room's objects -> data/aigen/inventory_world.json
    python tools/aigen/inventory.py --world --manifests   # + a draft manifest per new static prop

--world reads the rooms' Kod (`Create(&Class ...)` in object/active/holder/room/) instead of the demo
zones' layout, counts each sprite's frames, and sorts by the full inheritance chain: "prop", "tree"
(the Blender tree kit), "npc", "monster", "item" (pick-ups), "effect", "logic". A prop is "static"
when its sprite has one bitmap, or one per direction (8 groups); "done" when a manifest has its bgf.

Reads data/zone_layout.json (placements) and the Kod sources in Server-104/kod (reference only):
- OrnamentalObject: params.type -> OO_* (include/blakston.khd) -> the `case OO_*:` block in
  object/passive/ornobj.kod -> its vrIcon/vrName resources.
- any other class: the first `*icon* = <file>.bgf` resource (and `*name*_rsc`) in its own .kod file,
  following `is <Parent>` up the inheritance until one is found.
Kinds: "prop" (a mesh to make), "npc" / "monster" (other pipelines), "logic" (no visible object).
"""
import collections
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from server104 import SERVER104  # noqa: E402

KOD = SERVER104 / "kod"
LAYOUT = ROOT / "data" / "zone_layout.json"
OUT = ROOT / "data" / "aigen" / "inventory.json"

LOGIC = {"DynamicLight", "NewsLink", "FoodDispenser"}  # invisible or handled elsewhere


def _read(p: Path) -> str:
    return p.read_text(encoding="latin-1")


def oo_table() -> dict:
    """OO type number -> {"const", "icon", "name"}."""
    consts = {int(n): c for c, n in re.findall(r"\b(OO_\w+)\s*=\s*(\d+)", _read(KOD / "include" / "blakston.khd"))}
    src = _read(KOD / "object" / "passive" / "ornobj.kod")
    res = {k.lower(): v.strip().strip('"') for k, v in re.findall(r"^\s*(oo_\w+)\s*=\s*(.+?)\s*$", src, re.M)}
    cases = {}
    for block in re.split(r"\bcase\s+", src)[1:]:
        const = block.split(":", 1)[0].strip()
        icon = re.search(r"vrIcon\s*=\s*(\w+)", block)
        name = re.search(r"vrName\s*=\s*(\w+)", block)
        cases[const] = (res.get(icon.group(1).lower()) if icon else None, res.get(name.group(1).lower()) if name else None)
    out = {}
    for n, c in consts.items():
        icon, name = cases.get(c, (None, None))
        out[n] = {"const": c, "icon": icon, "name": name}
    return out


_kod_index = None


def kod_class(cls: str):
    """(path, source) of the .kod file declaring `cls`."""
    global _kod_index
    if _kod_index is None:
        _kod_index = {}
        for p in KOD.rglob("*.kod"):
            m = re.search(r"^\s*(\w+)\s+is\s+(\w+)", _read(p), re.M)
            if m:
                _kod_index[m.group(1).lower()] = p
    p = _kod_index.get(cls.lower())
    return (p, _read(p)) if p else (None, "")


def class_icon(cls: str, depth: int = 0):
    """(icon, name, parent chain) from the class's own resources, else its parent's."""
    p, src = kod_class(cls)
    if not p or depth > 8:
        return None, None, []
    icon = re.search(r"^\s*\w*icon\w*\s*=\s*(\S+\.bgf)", src, re.M | re.I)
    name = re.search(r"^\s*\w*name\w*_rsc\s*=\s*\"([^\"]*)\"", src, re.M | re.I)
    parent = re.search(r"^\s*\w+\s+is\s+(\w+)", src, re.M).group(1)
    if icon:
        return icon.group(1), name.group(1) if name else None, [parent]
    picon, pname, chain = class_icon(parent, depth + 1)
    return picon, (name.group(1) if name else pname), [parent] + chain


def kind_of(cls: str, chain: list) -> str:
    if cls in LOGIC:
        return "logic"
    if any(c.endswith("Town") or c in ("Towns", "Shopkeeper") for c in chain):
        return "npc"
    if "Monster" in chain:
        return "monster"
    return "prop"


ROOMS = KOD / "object" / "active" / "holder" / "room"
OUT_WORLD = ROOT / "data" / "aigen" / "inventory_world.json"
PROPS = ROOT / "data" / "aigen" / "props"
# drawn by the tree kit (tools/blender/build_tree_kit.py), not Tripo (docs/adr/0007 "Trees"). Leafless
# trees (the necropolis, fey and spider trees) are Tripo props: one closed mesh suits bare limbs
TREE_CLASSES = {"Tree", "YrxlTree", "Shrub", "Tallbush"}
TREE_OO = re.compile(r"(?<!NEC)TREE|SHRUB|PALM|BUSH")
# no mesh: lights, moving floors, swirls, smoke and water jets (sprites or particles in game); the
# forge sprite is only its flame (the forge itself is part of the room)
EFFECT_CLASSES = {"Forge", "KocatanForge", "MovingSector", "SmokeColumn", "FountainJet", "LivingStatue", "Portal", "HellPortal",
                  "DeathRealmEntrance", "DeathRealmExit", "CorpseNode"}
EFFECT_OO = {"OO_MUSEUM_TELEPORT"}
NPC_BASES = {"Towns", "Temples", "Watcher", "Wanderer", "Pirates", "Shopkeeper", "Human"}
# simple shapes that hold up at 1000 triangles (docs/adr/0007 "World batch")
SIMPLE = re.compile(r"jug|vase|bot|urn|pot|bowl|bucket|bag|skul(?!l[12356])|dice|quill|bell|barrel|globe|necrock")


def full_chain(cls: str) -> list:
    """Every parent of `cls`, nearest first."""
    out = []
    while len(out) < 20:
        p, src = kod_class(cls)
        m = re.search(r"^\s*\w+\s+is\s+(\w+)", src, re.M) if p else None
        if not m:
            break
        cls = m.group(1)
        out.append(cls)
    return out


def world_kind(cls: str, const: str | None) -> str:
    if const:  # an OrnamentalObject type
        return "effect" if const in EFFECT_OO else "tree" if TREE_OO.search(const) else "prop"
    chain = full_chain(cls)
    if cls in LOGIC or cls.lower() in {c.lower() for c in LOGIC}:
        return "logic"
    if cls in TREE_CLASSES:
        return "tree"
    if cls in EFFECT_CLASSES or "Portal" in chain:
        return "effect"
    if "Item" in chain:
        return "item"
    if NPC_BASES & set(chain) or any(c.endswith("Town") for c in chain):
        return "npc"
    if "Monster" in chain:
        return "monster"
    return "prop"


def sprite_frames(bgf: str):
    """(bitmaps, groups) of the sprite, or None when no client has it."""
    sys.path.insert(0, str(ROOT / "tools" / "bgf2png"))
    import bgf2png
    p = bgf2png.find_file(bgf2png.CLIENT_RES, bgf)
    if not p:
        return None
    b = bgf2png.BGF(p)
    return len(b.bitmaps), len(b.groups)


def world():
    """Every room's placed objects, keyed by sprite (several classes can share one)."""
    oo = {v["const"]: v for v in oo_table().values() if v["const"]}
    done = {}
    for p in PROPS.glob("*.json"):
        m = json.loads(p.read_text(encoding="utf-8"))
        if m.get("bgf"):
            done[m["bgf"].lower()] = m["name"]
    sprites = {}
    for path in sorted(ROOMS.rglob("*.kod")):
        src = _read(path)
        room = re.search(r"^\s*(\w+)\s+is\s+\w+", src, re.M)
        room = room.group(1) if room else path.stem
        for m in re.finditer(r"Create\(\s*&(\w+)([^)]*)", src):
            cls, args = m.group(1), m.group(2)
            t = re.search(r"#type\s*=\s*(OO_\w+)", args)
            if cls.lower() == "ornamentalobject" and t:
                info = oo.get(t.group(1), {})
                icon, name, const = info.get("icon"), info.get("name"), t.group(1)
            else:
                icon, name, _ = class_icon(cls)
                const = None
            if not icon:
                continue
            bgf = icon.lower().removesuffix(".bgf")
            e = sprites.setdefault(bgf, {"bgf": bgf, "name": name, "kind": world_kind(cls, const), "classes": {},
                                         "rooms": {}})
            key = const or cls
            e["classes"][key] = e["classes"].get(key, 0) + 1
            e["rooms"][room] = e["rooms"].get(room, 0) + 1
    for bgf, e in sprites.items():
        e["count"] = sum(e["rooms"].values())
        fr = sprite_frames(bgf)
        e["bitmaps"], e["groups"] = fr if fr else (0, 0)
        e["static"] = bool(fr) and (fr[0] == 1 or (fr[1] >= 8 and fr[0] <= 16))
        e["done"] = done.get(bgf)
    return dict(sorted(sprites.items(), key=lambda kv: (kv[1]["kind"], -kv[1]["count"], kv[0])))


def mesh_name(bgf: str) -> str:
    return "SM_AI_" + "".join(w.capitalize() for w in re.split(r"[^a-z0-9]+", bgf) if w)


def write_manifests(sprites: dict):
    """A draft manifest per static prop not done yet: front image only (variant C), "describe" to write
    after looking at the upscale. Existing manifests are left alone."""
    written = []
    for bgf, e in sprites.items():
        p = PROPS / ("%s.json" % bgf)
        if e["kind"] != "prop" or not e["static"] or e["done"] or p.exists():
            continue
        key = max(e["classes"], key=lambda k: (k.startswith("OO_"), e["classes"][k]))  # an OO type over a bare class
        m = {"_doc": "aigen manifest (tools/aigen/aigen.py, docs/adr/0007); fields as in brazier.json",
             "name": bgf, "kind": "props"}
        if key.startswith("OO_"):
            n = next(n for n, v in oo_table().items() if v["const"] == key)
            m.update({"class": "OrnamentalObject", "oo_type": n, "oo_const": key})
        else:
            m["class"] = key
        m.update({"bgf": bgf, "frame": 0, "describe": "", "mesh": mesh_name(bgf), "size_by": "height",
                  "placed": e["count"], "rooms": len(e["rooms"]),
                  "restyle": {"providers": ["openai"], "views": ["front"]},
                  "tripo": {"variants": {"C": {"front": "openai"}}}})
        if SIMPLE.search(bgf):
            m["polycount"] = 1000
        p.write_text(json.dumps(m, indent=1) + "\n", encoding="utf-8")
        written.append(bgf)
    print("manifests: %d written" % len(written))


def main_world(manifests: bool):
    sprites = world()
    OUT_WORLD.write_text(json.dumps({"_doc": "Every room's placed objects by sprite (tools/aigen/inventory.py --world)",
                                     "sprites": sprites}, indent=1) + "\n", encoding="utf-8")
    counts = collections.Counter((e["kind"], "done" if e["done"] else "static" if e["static"] else "frames")
                                 for e in sprites.values())
    for e in sprites.values():
        print("%-7s %-6s %-14s %4d placed %3d rooms  %2d/%-2d  %s" % (
            e["kind"], "done" if e["done"] else "static" if e["static"] else "frames", e["bgf"], e["count"],
            len(e["rooms"]), e["bitmaps"], e["groups"], ",".join(e["classes"])[:60]))
    print(dict(sorted(counts.items())))
    print("-> %s" % OUT_WORLD.relative_to(ROOT))
    if manifests:
        write_manifests(sprites)


def main():
    if "--world" in sys.argv:
        main_world("--manifests" in sys.argv)
        return
    layout = json.loads(LAYOUT.read_text(encoding="utf-8"))
    oo = oo_table()
    items = {}
    for z in layout["zones"]:
        for o in z.get("objects", []):
            cls, params = o["class"], o.get("params") or {}
            if cls == "OrnamentalObject":
                t = int(params.get("type", 0))
                info = oo.get(t, {})
                key = "OO_%d" % t
                entry = items.setdefault(key, {"class": cls, "oo_type": t, "const": info.get("const"),
                                               "bgf": (info.get("icon") or "").removesuffix(".bgf").lower() or None,
                                               "name": info.get("name"), "kind": "prop", "zones": {}})
            else:
                key = cls
                if key not in items:
                    icon, name, chain = class_icon(cls)
                    items[key] = {"class": cls, "bgf": icon.removesuffix(".bgf").lower() if icon else None, "name": name,
                                  "kind": kind_of(cls, chain), "parents": chain[:4], "zones": {}}
                entry = items[key]
            entry["zones"][str(z["rid"])] = entry["zones"].get(str(z["rid"]), 0) + 1
    for e in items.values():
        e["count"] = sum(e["zones"].values())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({"_doc": __doc__.strip().splitlines()[0], "items": dict(sorted(items.items()))}, indent=1) + "\n",
                   encoding="utf-8")
    for k, e in sorted(items.items(), key=lambda kv: (kv[1]["kind"], kv[0])):
        print("%-8s %-18s %-12s %4d  %-30s %s" % (e["kind"], k, e["bgf"], e["count"], (e["name"] or "")[:30], e["zones"]))


if __name__ == "__main__":
    main()
