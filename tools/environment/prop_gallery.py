"""
The prop gallery: a test zone that shows every prop and tree mesh the world build can place, each
beside its original sprite at true scale, under the game's real sky, moods and weather (docs/adr/0007
"Prop gallery"). Pure Python:

    python tools/environment/prop_gallery.py

Reads data/environment/props.json (every "classes" / "types" entry and each mesh it can show: the
custom model, the HD model, every "mesh_options" variant), the kit meshes in build/environment/kit/,
the aigen manifests and data/aigen/inventory_world.json (which sprite each entry is), and writes:

- data/environment/prop_gallery.json: the gallery as a zone (rid 9000, class PropGallery, its world
  origin, start point) with "objects" in zone_layout.json's format, so tools/ue/build_world.py places
  them with the same code as the real zones; plus each object's sprite billboard and labels.
- data/environment/gallery_cameras.json: look-dev cameras along the rows
  (tools/ue/run_lookdev.ps1 -Gallery).
- build/zones/9000_PropGallery.glb (+ _collision.glb): the paved floor (Raza's stone path, grd09604).
- build/environment/gallery/SM_GallerySprite_<bgf>.glb: an upright quad per sprite, and its picture
  as build/textures_placeholder/T_GallerySprite_<bgf>.png.

Rows run west to east, nearest the start point first: small props, then medium, large, and trees at
the back. Each entry is its sprite, then its meshes left to right; a label under each names it.
"""
import glob
import json
import math
import os
import struct
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "aigen"))
sys.path.insert(0, os.path.join(ROOT, "tools", "textures"))
sys.path.insert(0, os.path.join(ROOT, "tools", "bgf2png"))

PROPS = os.path.join(ROOT, "data", "environment", "props.json")
OUT = os.path.join(ROOT, "data", "environment", "prop_gallery.json")
CAMERAS = os.path.join(ROOT, "data", "environment", "gallery_cameras.json")
KIT = os.path.join(ROOT, "build", "environment", "kit")
SPRITES = os.path.join(ROOT, "build", "environment", "gallery")
TEXTURES = os.path.join(ROOT, "build", "textures_placeholder")
FLOOR = os.path.join(ROOT, "build", "zones", "9000_PropGallery.glb")

RID = 9000
ORIGIN_CM = [-400000.0, -400000.0, 0.0]  # 4 km north-west of Raza: clear of every zone's 2 km cell
M_PER_TEXEL = 0.034358  # metres per sprite texel at shrink 1 (midtree2: 4.92 m from 716 px at shrink 5)
FLOOR_TEXTURE, FLOOR_TILE_M = "grd09604", 3.52  # RAZ-STONEPATH, 1.6 squares of 2.2 m
SECTOR_LIGHT = 0.5  # outdoors in Raza (the blockout's vertex colour there)
ROW_MAX_M = 60.0
GAP_M, SLOT_GAP_M, ROW_GAP_M = 0.4, 1.6, 4.0
HANG_CEILING_M = 3.2  # hanging props hang from here (there is no ceiling)
ROWS = [("small", lambda h: h < 0.6), ("medium", lambda h: h < 1.5), ("large", lambda h: True)]
# per row: camera spacing along the row, distance in front, eye height (m), pitch (deg)
CAMERA = {"small": (5.0, 2.8, 1.1, -14.0), "medium": (8.0, 4.5, 1.5, -10.0), "large": (10.0, 7.0, 1.8, -6.0),
          "trees": (14.0, 15.0, 3.0, 2.0)}


def kit_bounds(name):
    """(width, depth, height) m of a kit GLB from its POSITION accessor's min/max (glTF: Y up)."""
    data = open(os.path.join(KIT, name + ".glb"), "rb").read()
    n = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + n])
    lo, hi = [1e9] * 3, [-1e9] * 3
    for mesh in gltf["meshes"]:
        for p in mesh["primitives"]:
            acc = gltf["accessors"][p["attributes"]["POSITION"]]
            lo = [min(a, b) for a, b in zip(lo, acc["min"])]
            hi = [max(a, b) for a, b in zip(hi, acc["max"])]
    return hi[0] - lo[0], hi[2] - lo[2], hi[1] - lo[1]


def available(name):
    return bool(name) and not name.startswith("/") and os.path.exists(os.path.join(KIT, name + ".glb"))


def entry_meshes(cfg):
    """[(mesh, used)] for one props.json entry: every mesh it can show, the one the game places first."""
    out = []
    custom = [c for c in (cfg.get("mesh_custom"), cfg.get("mesh") and cfg["mesh"] + "_Custom") if available(c)]
    options = {k: v for k, v in (cfg.get("mesh_options") or {}).items() if not k.startswith("_")}
    use = cfg.get("mesh_use")
    if custom:
        out.append((custom[0], True))
    elif options and use in options:
        out += [(m, True) for m in options[use] if available(m)]
    elif available(cfg.get("mesh")):
        out.append((cfg["mesh"], True))
    for m in [cfg.get("mesh")] + [m for k, v in options.items() for m in v]:
        if available(m) and m not in [o[0] for o in out]:
            out.append((m, False))
    return out


def sprite_sources():
    """-> ({mesh: bgf}, {class or OO const: bgf}, {bgf: (png, height m)}) from the aigen manifests,
    the tree kit and the world inventory."""
    by_mesh, by_key, pics = {}, {}, {}
    for f in glob.glob(os.path.join(ROOT, "data", "aigen", "*", "*.json")):
        m = json.load(open(f, encoding="utf-8"))
        if not m.get("bgf"):
            continue
        by_mesh[m["mesh"]] = m["bgf"]
        up = os.path.join(ROOT, "build", "aigen", m["kind"], m["name"], "01_upscale", "upscaled.png")
        if os.path.exists(up) and m.get("height_m"):
            pics[m["bgf"]] = (up, float(m["height_m"]))
    from make_tree_textures import TREES
    for kind, cfg in TREES.items():
        by_mesh["SM_Tree_%s" % kind] = cfg["bgf"]
    inv = os.path.join(ROOT, "data", "aigen", "inventory_world.json")
    if os.path.exists(inv):
        for bgf, e in json.load(open(inv, encoding="utf-8"))["sprites"].items():
            for key in e["classes"]:
                by_key.setdefault(key, bgf)
    return by_mesh, by_key, pics


def sprite_picture(bgf, pics):
    """(cropped RGBA image, height m) of a sprite: the aigen upscale, else the bgf's first frame."""
    if bgf in pics:
        im = Image.open(pics[bgf][0]).convert("RGBA")
        return im.crop(im.getchannel("A").getbbox()), pics[bgf][1]
    frame = os.path.join(ROOT, "build", "bgf", bgf, "frame_00.png")
    if not os.path.exists(frame):
        import bgf2png
        path = bgf2png.find_file(bgf2png.CLIENT_RES, bgf)
        if not path:
            return None, 0.0
        bgf2png.export_sprite(bgf2png.BGF(path), os.path.dirname(frame), bgf2png.load_palette())
    im = Image.open(frame).convert("RGBA")
    shrink = json.load(open(os.path.join(os.path.dirname(frame), "meta.json")))["shrink"]
    im = im.crop(im.getchannel("A").getbbox())
    return im, im.height * M_PER_TEXEL / shrink


def write_glb(path, prims):
    """prims: [(material, positions, normals, uvs, colors or None, indices)] -> a minimal glTF binary."""
    gltf = {"asset": {"version": "2.0", "generator": "tools/environment/prop_gallery.py"}, "scene": 0,
            "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": os.path.splitext(os.path.basename(path))[0]}],
            "meshes": [{"primitives": []}], "materials": [], "buffers": [], "bufferViews": [], "accessors": []}
    blob = b""

    def add(values, fmt, count, kind, target, minmax=False):
        nonlocal blob
        while len(blob) % 4:
            blob += b"\0"
        data = struct.pack("<%d%s" % (len(values) * count, fmt), *[c for v in values for c in (v if isinstance(v, tuple) else (v,))])
        gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        acc = {"bufferView": len(gltf["bufferViews"]) - 1, "componentType": 5125 if fmt == "I" else 5126,
               "count": len(values), "type": kind}
        if minmax:
            acc["min"] = [min(v[i] for v in values) for i in range(3)]
            acc["max"] = [max(v[i] for v in values) for i in range(3)]
        gltf["accessors"].append(acc)
        blob += data
        return len(gltf["accessors"]) - 1

    for mat, pos, nrm, uv, col, idx in prims:
        gltf["materials"].append({"name": mat, "doubleSided": True,
                                  "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.9}})
        attrs = {"POSITION": add(pos, "f", 3, "VEC3", 34962, True), "NORMAL": add(nrm, "f", 3, "VEC3", 34962),
                 "TEXCOORD_0": add(uv, "f", 2, "VEC2", 34962)}
        if col:
            attrs["COLOR_0"] = add(col, "f", 4, "VEC4", 34962)
        gltf["meshes"][0]["primitives"].append({"attributes": attrs, "indices": add(idx, "I", 1, "SCALAR", 34963),
                                                "material": len(gltf["materials"]) - 1})
    while len(blob) % 4:
        blob += b"\0"
    gltf["buffers"].append({"byteLength": len(blob)})
    js = json.dumps(gltf).encode()
    while len(js) % 4:
        js += b" "
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)


def write_floor(x0, z0, x1, z1):
    """The paved floor in layout metres (glTF: x east, y up, z south), one quad per 3.52 m tile so the
    stone path repeats as in Raza; the sector light as vertex colour, as roo2gltf writes it."""
    pos, nrm, uv, col, idx = [], [], [], [], []
    nx, nz = max(1, math.ceil((x1 - x0) / FLOOR_TILE_M)), max(1, math.ceil((z1 - z0) / FLOOR_TILE_M))
    for i in range(nx):
        for j in range(nz):
            a, b = x0 + i * FLOOR_TILE_M, z0 + j * FLOOR_TILE_M
            base = len(pos)
            for dx, dz in ((0, 0), (1, 0), (1, 1), (0, 1)):
                pos.append((a + dx * FLOOR_TILE_M, 0.0, b + dz * FLOOR_TILE_M))
                nrm.append((0.0, 1.0, 0.0))
                uv.append((float(dx), float(dz)))
                col.append((SECTOR_LIGHT, SECTOR_LIGHT, SECTOR_LIGHT, 1.0))
            idx += [base, base + 2, base + 1, base, base + 3, base + 2]  # counter-clockwise seen from above
    for path in (FLOOR, FLOOR[:-4] + "_collision.glb"):
        write_glb(path, [(FLOOR_TEXTURE, pos, nrm, uv, col, idx)])


def write_sprite_quad(bgf, w, h):
    """An upright quad w x h m standing on y=0 and facing south (+z in glTF, +Y in UE), material "sprite"."""
    pos = [(-w / 2, 0.0, 0.0), (w / 2, 0.0, 0.0), (w / 2, h, 0.0), (-w / 2, h, 0.0)]
    nrm = [(0.0, 0.0, 1.0)] * 4
    uv = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    name = "SM_GallerySprite_%s" % bgf
    write_glb(os.path.join(SPRITES, name + ".glb"), [("sprite", pos, nrm, uv, None, [0, 1, 2, 0, 2, 3])])
    return name


def main():
    props = json.load(open(PROPS, encoding="utf-8"))
    import inventory
    oo_const = {n: v["const"] for n, v in inventory.oo_table().items()}
    by_mesh, by_key, pics = sprite_sources()

    entries = []
    for section in ("classes", "types"):
        for key, cfg in props.get(section, {}).items():
            meshes = entry_meshes(cfg)
            if not meshes:
                continue
            if section == "types":
                obj = {"class": "OrnamentalObject", "params": {"type": int(key)}}
                inv_key = oo_const.get(int(key))
            else:
                cls, _, sub = key.partition("/")
                obj = {"class": cls, "params": {"classtype": {"class": sub}} if sub else {}}
                inv_key = sub or cls
            first = meshes[0][0]
            tree = first.startswith("SM_Tree_")
            bgf = by_mesh.get("_".join(first.split("_")[:3]) if tree else first) or by_key.get(inv_key)
            sizes = [kit_bounds(m) for m, _ in meshes]
            entries.append({"key": ("OO %s" % key) if section == "types" else key, "obj": obj, "cfg": cfg, "bgf": bgf,
                            "meshes": meshes, "sizes": sizes, "tree": tree,
                            "height": max(s[2] for s in sizes)})
    # one entry per sprite and mesh set (OrnamentalObject types that share a sprite and meshes show once)
    seen, unique = set(), []
    for e in entries:
        sig = (e["bgf"], tuple(m for m, _ in e["meshes"]))
        if sig not in seen:
            seen.add(sig)
            unique.append(e)

    rows = []
    for name, test in ROWS:
        rows.append((name, sorted([e for e in unique if not e["tree"] and test(e["height"])
                                   and not any(e in r[1] for r in rows)], key=lambda e: (e["height"], e["key"]))))
    rows.append(("trees", sorted([e for e in unique if e["tree"]], key=lambda e: e["key"])))

    objects, sprites, labels, cameras = [], [], [], []
    z = 0.0  # the front (south) row's front edge; rows go north (-z)
    width = 0.0
    used_sprites = {}
    for row_name, row in rows:
        x = 0.0
        line = []  # [(entry, x0)] of the current line
        lines = []
        for e in row:
            sprite_w = 0.0
            if e["bgf"]:
                if e["bgf"] not in used_sprites:
                    im, h = sprite_picture(e["bgf"], pics)
                    used_sprites[e["bgf"]] = (im, h)
                im, h = used_sprites[e["bgf"]]
                sprite_w = (im.width / im.height * h) if im is not None and h > 0 else 0.0
            slot = sprite_w + sum(max(s[0], 0.4) + GAP_M for s in e["sizes"])
            if line and x + slot > ROW_MAX_M:
                lines.append(line)
                line, x = [], 0.0
            line.append((e, x, sprite_w))
            x += slot + SLOT_GAP_M
        if line:
            lines.append(line)
        for line in lines:
            depth = max(max(s[1] for s in e["sizes"]) for e, _, _ in line)
            zc = z - depth / 2 - 0.5  # the line's centre
            for e, x0, sprite_w in line:
                xc = x0
                if sprite_w:
                    im, h = used_sprites[e["bgf"]]
                    quad = write_sprite_quad(e["bgf"], sprite_w, h)
                    os.makedirs(TEXTURES, exist_ok=True)
                    tex = "T_GallerySprite_%s.png" % e["bgf"]
                    im.save(os.path.join(TEXTURES, tex))
                    sprites.append({"label": "Gallery_Sprite_%s_%d" % (e["bgf"], len(sprites)), "mesh": quad, "texture": tex,
                                    "pos": [round(xc + sprite_w / 2, 3), 0.0, round(zc, 3)]})
                    labels.append({"text": "sprite %s" % e["bgf"], "pos": [round(xc + sprite_w / 2, 3), 0.02, round(zc + depth / 2 + 0.6, 3)]})
                    xc += sprite_w + GAP_M
                for (mesh, used), size in zip(e["meshes"], e["sizes"]):
                    w = max(size[0], 0.4)
                    obj = dict(e["obj"], pos=[round(xc + w / 2, 3), 0.0, round(zc, 3)],
                               gallery={"mesh": mesh, "random_yaw": False, "facing": "south"})
                    if e["cfg"].get("hanging"):
                        obj["ceiling_y"] = HANG_CEILING_M
                    objects.append(obj)
                    labels.append({"text": "%s%s\n%s" % (mesh.replace("SM_AI_", "").replace("SM_Tree_", "Tree "),
                                                         "" if used else " (alt)", e["key"]),
                                   "pos": [round(xc + w / 2, 3), 0.02, round(zc + depth / 2 + 0.6, 3)]})
                    xc += w + GAP_M
                width = max(width, xc)
            # cameras along the line, in front of it, looking north: close and low for small props,
            # further back and higher for trees, so each shot shows a few entries at a readable size
            step, far, eye, pitch = CAMERA[row_name]
            line_end = max(x0 + sw + sum(max(s[0], 0.4) + GAP_M for s in e["sizes"]) for e, x0, sw in line)
            for cx in [c + step / 2 for c in range(0, int(max(line_end, step)), int(step))]:
                cameras.append({"name": "gallery_%02d_%s" % (len(cameras), row_name),
                                "location_cm": [round(ORIGIN_CM[0] + cx * 100.0), round(ORIGIN_CM[1] + (zc + far) * 100.0),
                                                round(ORIGIN_CM[2] + eye * 100.0)],
                                "rotation": [pitch, -90.0], "fov": 70})
            z -= depth + ROW_GAP_M + 1.0

    start = [round(width / 2, 2), 0.0, 8.0]
    write_floor(-8.0, z - 8.0, width + 8.0, 14.0)
    gallery = {
        "_doc": "Generated by tools/environment/prop_gallery.py: the prop gallery, a test zone (docs/adr/0007 "
                "\"Prop gallery\"). Played offline with -MRGallery; built by tools/ue/build_world.py.",
        "rid": RID, "class": "PropGallery", "name": "Prop Gallery", "world_origin_cm": ORIGIN_CM,
        "mesh": os.path.relpath(FLOOR, ROOT).replace("\\", "/"), "sector_light": SECTOR_LIGHT,
        "bounds_m": {"min": [-8.0, 0.0, round(z - 8.0, 2)], "max": [round(width + 8.0, 2), 10.0, 14.0]},
        "start": {"pos": start, "yaw": -90.0},  # UE yaw: facing north, along the rows
        "objects": objects, "sprites": sprites, "labels": labels,
    }
    open(OUT, "w", encoding="utf-8").write(json.dumps(gallery, indent=1) + "\n")
    open(CAMERAS, "w", encoding="utf-8").write(json.dumps({
        "_doc": "Look-dev cameras for the prop gallery (tools/environment/prop_gallery.py; tools/ue/run_lookdev.ps1 -Gallery). "
                "Same format as lookdev_cameras.json.", "cameras": cameras}, indent=1) + "\n")
    print("prop_gallery: %d meshes, %d sprites, %d rows (%s), %.0f x %.0f m, %d cameras -> %s" % (
        len(objects), len(sprites), len(rows), ", ".join("%s %d" % (n, len(r)) for n, r in rows), width, -z,
        len(cameras), os.path.relpath(OUT, ROOT)))


if __name__ == "__main__":
    main()
