"""
Blockout glTF helpers shared by the zone-art tools (docs/adr/0003 pass 3). Pure Python, no
dependencies, so it runs in plain Python, Blender and the Unreal Editor alike.

- read_glb():               the triangles of a roo2gltf blockout, grouped by material slot (grdNNNNN)
- zone_art_config():        data/environment/zone_<rid>.json (which buildings are rebuilt as art)
- building_triangles():     which blockout triangles belong to a rebuilt building
- write_render_blockout():  a copy of the blockout without those triangles, so the art mesh doesn't
                            z-fight with them (the full blockout stays as hidden collision)
- floor_light():            the original sector light of the floor under a point (props' ambient floor)

Blender (tools/blender/build_zone_art.py) and the Unreal side (tools/ue/build_world.py) both call
building_triangles(), so the faces rebuilt as art and the faces hidden always match.

Coordinates are the blockout's glTF metres: x east, y up, z south.
"""
from __future__ import annotations

import json
import os
import struct

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ENV_DATA = os.path.join(REPO, "data", "environment")

# A face is "ground" (left in the blockout) when it is horizontal and no higher than this above the
# lowest floor in the building's region. Everything else inside the region belongs to the building.
GROUND_TOLERANCE_M = 0.25


# ------------------------------------------------------------------------------------------ glb io

def _parse(path):
    data = open(path, "rb").read()
    magic, _version, _total = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError("%s is not a glb" % path)
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    blen = struct.unpack_from("<I", data, off)[0]
    return gltf, bytearray(data[off + 8:off + 8 + blen])


def _accessor(gltf, binary, index):
    a = gltf["accessors"][index]
    view = gltf["bufferViews"][a["bufferView"]]
    start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    fmt = {5126: "f", 5125: "I", 5123: "H", 5121: "B"}[a["componentType"]]
    flat = struct.unpack_from("<%d%s" % (a["count"] * comps, fmt), binary, start)
    if comps == 1:
        return list(flat)
    return [tuple(flat[i:i + comps]) for i in range(0, len(flat), comps)]


class Primitive:
    """One material slot of the blockout: vertex positions/normals/uvs and triangle index triples."""

    def __init__(self, material, positions, normals, uvs, triangles):
        self.material = material
        self.positions = positions
        self.normals = normals
        self.uvs = uvs
        self.triangles = triangles  # list of (i0, i1, i2)

    def tri(self, t):
        return [self.positions[i] for i in self.triangles[t]]


def read_glb(path):
    """-> {material name: Primitive} for the first mesh of a roo2gltf blockout."""
    gltf, binary = _parse(path)
    out = {}
    for p in gltf["meshes"][0]["primitives"]:
        attrs = p["attributes"]
        idx = _accessor(gltf, binary, p["indices"])
        name = gltf["materials"][p["material"]]["name"]
        out[name] = Primitive(
            name,
            _accessor(gltf, binary, attrs["POSITION"]),
            _accessor(gltf, binary, attrs["NORMAL"]) if "NORMAL" in attrs else None,
            _accessor(gltf, binary, attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else None,
            [tuple(idx[i:i + 3]) for i in range(0, len(idx), 3)],
        )
    return out


_FLOORS = {}


def floor_light(path, x, z, y, default=1.0):
    """-> the original sector light (0..1, roo2gltf's vertex colour R) of the floor under the
    glTF point (x east, z south, y up; metres): the highest horizontal face at or below y + 0.3 m
    that contains (x, z). `default` where there is none. The interiors' ambient floor scales by it,
    so a prop gets the same fill as the floor it stands on (docs/adr/0007)."""
    if path not in _FLOORS:
        gltf, binary = _parse(path)
        faces = []
        for p in gltf["meshes"][0]["primitives"]:
            attrs = p["attributes"]
            if "COLOR_0" not in attrs:
                continue
            pos = _accessor(gltf, binary, attrs["POSITION"])
            col = _accessor(gltf, binary, attrs["COLOR_0"])
            scale = {5121: 255.0, 5123: 65535.0}.get(gltf["accessors"][attrs["COLOR_0"]]["componentType"], 1.0)
            idx = _accessor(gltf, binary, p["indices"])
            for i in range(0, len(idx), 3):
                tri = [pos[j] for j in idx[i:i + 3]]
                if _horizontal(tri):
                    faces.append((tri, sum(col[j][0] for j in idx[i:i + 3]) / (3.0 * scale)))
        _FLOORS[path] = faces
    best = None
    for tri, light in _FLOORS[path]:
        h = tri[0][1]
        if h > y + 0.3 or (best and h <= best[0]):
            continue
        (ax, _, az), (bx, _, bz), (cx, _, cz) = tri
        d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(d) < 1e-12:
            continue
        l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d
        l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d
        if l1 >= -1e-6 and l2 >= -1e-6 and 1 - l1 - l2 >= -1e-6:
            best = (h, light)
    return best[1] if best else default


def write_render_blockout(src, dst, hidden):
    """Copy glb `src` to `dst` without the triangles in `hidden` ({material: set(triangle index)}).
    Vertex data is kept as is; only index buffers change. Returns the number of triangles removed."""
    gltf, binary = _parse(src)
    removed = 0
    keep_prims = []
    for p in gltf["meshes"][0]["primitives"]:
        name = gltf["materials"][p["material"]]["name"]
        drop = hidden.get(name)
        if not drop:
            keep_prims.append(p)
            continue
        idx = _accessor(gltf, binary, p["indices"])
        tris = [idx[i:i + 3] for i in range(0, len(idx), 3)]
        kept = [t for n, t in enumerate(tris) if n not in drop]
        removed += len(tris) - len(kept)
        if not kept:
            continue
        flat = [i for t in kept for i in t]
        while len(binary) % 4:
            binary += b"\0"
        gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": 4 * len(flat), "target": 34963})
        binary += struct.pack("<%dI" % len(flat), *flat)
        gltf["accessors"].append({"bufferView": len(gltf["bufferViews"]) - 1, "componentType": 5125,
                                  "count": len(flat), "type": "SCALAR"})
        p["indices"] = len(gltf["accessors"]) - 1
        keep_prims.append(p)
    gltf["meshes"][0]["primitives"] = keep_prims
    while len(binary) % 4:
        binary += b"\0"
    gltf["buffers"] = [{"byteLength": len(binary)}]
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    with open(dst, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + bytes(binary))
    return removed


# ------------------------------------------------------------------------------------ zone config

def zone_art_config(rid):
    """data/environment/zone_<rid>.json, or None when the zone has no art."""
    path = os.path.join(ENV_DATA, "zone_%d.json" % rid)
    if not os.path.exists(path):
        return None
    return json.load(open(path, encoding="utf-8"))


def _inside(region, tri):
    x0, z0, x1, z1 = region
    return all(x0 <= p[0] <= x1 and z0 <= p[2] <= z1 for p in tri)


def _horizontal(tri):
    return max(p[1] for p in tri) - min(p[1] for p in tri) < 1e-3


def building_triangles(prims, building, claimed=None):
    """-> {material: set(triangle index)}: the blockout triangles that make up one building
    (`building` is an entry of zone_<rid>.json "buildings", with region_m = [x0, z0, x1, z1]).

    Every triangle fully inside the region belongs to the building, except horizontal ones at
    ground level (floors around the building stay part of the blockout). An optional
    "materials" list limits the building to those textures (the town wall, the pond), and
    triangles already in `claimed` ({material: set}) belong to an earlier entry. A "cutouts" entry
    takes the cut-out originals rebuilt as solids (facades.cutout_materials()) left over by the
    buildings before it: fences, gates, signs."""
    region = building["region_m"]
    only = set(building.get("materials", [])) or None
    if building.get("kind") == "cutouts":
        import facades  # same folder; every cut-out original rebuilt as a solid
        only = facades.cutout_materials()
    water = building.get("kind") == "water"
    claimed = claimed or {}
    inside = {}
    ground = None
    for name, prim in prims.items():
        if only and name not in only:
            continue
        taken = claimed.get(name, ())
        for t in range(len(prim.triangles)):
            if t in taken:
                continue
            tri = prim.tri(t)
            if _inside(region, tri):
                inside.setdefault(name, []).append(t)
                y = min(p[1] for p in tri)
                ground = y if ground is None else min(ground, y)
    out = {}
    for name, tris in inside.items():
        prim = prims[name]
        for t in tris:
            tri = prim.tri(t)
            if not water and _horizontal(tri) and tri[0][1] <= ground + GROUND_TOLERANCE_M:
                continue
            out.setdefault(name, set()).add(t)
    return out


def assign_buildings(prims, config):
    """-> [(building, {material: set(triangle)})] in config order; a triangle belongs to the first
    building that takes it, so broad entries (the town wall by texture) go last."""
    claimed, out = {}, []
    for b in (config or {}).get("buildings", []):
        tris = building_triangles(prims, b, claimed)
        for name, ts in tris.items():
            claimed.setdefault(name, set()).update(ts)
        out.append((b, tris))
    return out


def all_building_triangles(prims, config):
    """Every blockout triangle some building (or water body) in a zone config replaces."""
    out = {}
    for _, tris in assign_buildings(prims, config):
        for name, ts in tris.items():
            out.setdefault(name, set()).update(ts)
    return out
