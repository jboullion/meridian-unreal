"""
The original sprites of the objects whose 3D prop props.json "mesh_keep" leaves out (docs/adr/0007 "Fewer
props"), for offline play. Online the server draws them from their own bitmaps (UMRBgfSpriteComponent);
offline the world build stands this quad in their place, turned to the camera by its material
(M_ObjectSprite), and the game hides it when it plays online (AMROfflineSprite). Pure Python:

    python tools/environment/object_sprites.py

Reads data/zone_layout.json, data/environment/props.json, the aigen manifests and
data/aigen/inventory_world.json (which sprite an entry is, as tools/environment/prop_gallery.py finds it),
and the original bgfs (tools/bgf2png), and writes:

- build/environment/sprites/SM_ObjSprite_<bgf>.glb: the sprite's first view (its first group's first
  bitmap) as an upright quad facing south (+z), its feet at the origin where the original puts them
  (the bitmap's offsets, as UMRBgfSpriteComponent::Show places a bitmap), one pixel 16/shrink fine units;
- build/textures_placeholder/T_ObjSprite_<bgf>.png: that bitmap, the original's pixels;
- data/environment/object_sprites.json: each placed object's sprite by its world-build label
  (Prop_<rid>_<class or OO type>_<index>, build_world.zone_props), and each sprite's quad and picture.
"""
import json
import os
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(ROOT, "tools", "aigen"))
sys.path.insert(0, os.path.join(ROOT, "tools", "textures"))
sys.path.insert(0, os.path.join(ROOT, "tools", "bgf2png"))

import props_lookup  # noqa: E402
from prop_gallery import sprite_sources, write_glb  # noqa: E402

LAYOUT = os.path.join(ROOT, "data", "zone_layout.json")
OUT = os.path.join(ROOT, "data", "environment", "object_sprites.json")
QUADS = os.path.join(ROOT, "build", "environment", "sprites")
TEXTURES = os.path.join(ROOT, "build", "textures_placeholder")
BGF_DIR = os.path.join(ROOT, "build", "bgf")
CM_PER_FINE = 220.0 / 1024.0  # a square is 1024 fine units of 2.2 m


def bgf_frames(bgf):
    """-> (meta, dir) of a bgf exported by bgf2png (exported now if it isn't yet), or (None, None)."""
    out = os.path.join(BGF_DIR, bgf)
    meta = os.path.join(out, "meta.json")
    if not os.path.exists(meta):
        import bgf2png
        path = bgf2png.find_file(bgf2png.CLIENT_RES, bgf)
        if not path:
            return None, None
        from pathlib import Path
        bgf2png.export_sprite(bgf2png.BGF(path), Path(out), bgf2png.load_palette())
    return json.load(open(meta, encoding="utf-8")), out


def write_sprite(bgf):
    """The quad and picture of a bgf's first view -> {"mesh", "texture", "w_m", "h_m"}, or None."""
    meta, folder = bgf_frames(bgf)
    if not meta or not meta.get("bitmaps"):
        return None
    groups = [g for g in meta.get("groups") or [] if g]
    index = groups[0][0] if groups and 0 <= groups[0][0] < len(meta["bitmaps"]) else 0
    b = meta["bitmaps"][index]
    shrink = max(1, int(meta.get("shrink") or 1))
    w, h = int(b["w"]), int(b["h"])
    # the feet in the bitmap, as the original finds them (UMRBgfSpriteComponent::Show, FMRSpriteLibrary::Place)
    feet_x = w * 0.5 - b.get("xoff", 0) * shrink / 16.0
    feet_y = h - b.get("yoff", 0) * shrink / 4.0
    m = 16.0 / shrink * CM_PER_FINE / 100.0  # metres a pixel
    # glTF: x east (the picture's left to right as seen from the south), y up, facing +z (south: UE +Y)
    pos = [((px - feet_x) * m, (feet_y - py) * m, 0.0) for px, py in ((0, h), (w, h), (w, 0), (0, 0))]
    nrm = [(0.0, 0.0, 1.0)] * 4
    uv = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    mesh = "SM_ObjSprite_%s" % bgf
    write_glb(os.path.join(QUADS, mesh + ".glb"), [("sprite", pos, nrm, uv, None, [0, 1, 2, 0, 2, 3])])
    texture = "T_ObjSprite_%s.png" % bgf
    os.makedirs(TEXTURES, exist_ok=True)
    Image.open(os.path.join(folder, "frame_%02d.png" % index)).convert("RGBA").save(os.path.join(TEXTURES, texture))
    return {"mesh": mesh, "texture": texture, "w_m": round(w * m, 3), "h_m": round(h * m, 3)}


def object_bgf(obj, key, cfg, by_mesh, by_key, oo):
    """The bgf (lower case, no extension) the original draws an object with: its own icon when it was made
    with one, an OrnamentalObject's type's icon, else the sprite its props.json entry was modelled from."""
    params = obj.get("params") or {}
    icon = params.get("rIcon") if isinstance(params.get("rIcon"), str) else None
    if not icon and obj["class"] == "OrnamentalObject":
        icon = (oo.get(params.get("type")) or {}).get("icon")
    if icon:
        return os.path.splitext(icon.lower())[0]
    for mesh in props_lookup.entry_meshes(cfg):
        if mesh in by_mesh:
            return by_mesh[mesh]
    cls, _, sub = key.partition("/")
    return by_key.get(sub.split("=")[0] if sub and "=" not in sub else cls)


def main():
    import inventory
    props = props_lookup.load()
    layout = json.load(open(LAYOUT, encoding="utf-8"))
    by_mesh, by_key, _ = sprite_sources()
    oo = inventory.oo_table()
    objects, sprites, missing = {}, {}, {}
    for zone in layout["zones"]:
        for i, obj in enumerate(zone.get("objects", [])):
            key, cfg = props_lookup.prop_entry(props, obj)
            if not cfg or not props_lookup.entry_meshes(cfg) or props_lookup.mesh_kept(props, key, cfg):
                continue
            kind = obj["class"] if obj["class"] != "OrnamentalObject" else "OO%s" % (obj.get("params") or {}).get("type")
            label = "Prop_%d_%s_%d" % (zone["rid"], kind, i)  # as build_world.zone_props names it
            bgf = object_bgf(obj, key, cfg, by_mesh, by_key, oo)
            if bgf and bgf not in sprites:
                sprites[bgf] = write_sprite(bgf)
            if bgf and sprites[bgf]:
                objects[label] = bgf
            else:
                missing[key] = missing.get(key, 0) + 1
    data = {
        "_doc": "Generated by tools/environment/object_sprites.py: the original sprite (bgf) of every placed object "
                "whose prop props.json \"mesh_keep\" leaves out, by its world-build label, for offline play "
                "(docs/adr/0007 \"Fewer props\"), and each sprite's quad and picture.",
        "objects": dict(sorted(objects.items())),
        "sprites": {k: v for k, v in sorted(sprites.items()) if v},
    }
    open(OUT, "w", encoding="utf-8").write(json.dumps(data, indent=1) + "\n")
    print("object_sprites: %d objects, %d sprites -> %s" % (len(objects), len(data["sprites"]), os.path.relpath(OUT, ROOT)))
    if missing:
        print("  no sprite found for: %s" % ", ".join("%s (%d)" % kv for kv in sorted(missing.items())))


if __name__ == "__main__":
    main()
