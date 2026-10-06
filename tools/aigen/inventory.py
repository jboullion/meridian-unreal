"""
Every Kod-placed object in the demo zones -> its sprite (.bgf), name and count per zone: the "pick
the object" step of the sprite-to-3d pipeline (docs/adr/0007).

    python tools/aigen/inventory.py            # -> data/aigen/inventory.json, and a table on stdout

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
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KOD = ROOT / "Server-104" / "kod"
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


def main():
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
