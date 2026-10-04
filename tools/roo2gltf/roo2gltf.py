"""
Convert Meridian 59 .roo rooms into glTF (.glb) blockouts + zone metadata.

    python tools/roo2gltf/roo2gltf.py                 # all zones in data/zones.json
    python tools/roo2gltf/roo2gltf.py --rid 300 301   # selected zones
    python tools/roo2gltf/roo2gltf.py --preview       # also write top-down PNG previews

Outputs (build/ is git-ignored, regenerate any time):
    build/zones/<rid>_<class>.glb        geometry, one primitive per original texture (material "grdNNNNN")
    build/zones/<rid>_<class>.png        top-down preview (with --preview)
    data/zone_layout.json                world-space positions of exits / arrivals / spawns per zone

Coordinate systems
------------------
ROO ("client") units: 1024 per grid square. X grows east, Y grows south (rows).
Heights in the file are Kod fineness (64 per square) -> *16 to ROO units.
Kod grid -> ROO:  X = (col-1)*1024 + fine_col*16,  Y = (row-1)*1024 + fine_row*16
                  (blakserv/roofile.h GRIDCOORDTOROO)
Scale: the original eye height is 0.75 squares (clientd3d/game.c); with a 1.65 m eye
that is 2.2 m per square.
glTF (right-handed, Y up, metres):  (x, y, z) = (roo_x, height, roo_y) * M_PER_ROO
  -> +X east, +Z south, -Z north.  UE's glTF importer converts this to UE space.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Server-104" / "roomedit" / "roogen"))
from roofile import Room, BSP_LEAF  # noqa: E402

ROO_PER_SQUARE = 1024.0
HEIGHT_TO_ROO = 16.0
M_PER_SQUARE = 2.2
M_PER_ROO = M_PER_SQUARE / ROO_PER_SQUARE

WF_TRANSPARENT = 0x2
WF_PASSABLE = 0x4
WF_NORMAL_TOPDOWN = 0x100
WF_NO_VTILE = 0x200

# Texture repeat sizes come from tools/bgf2png (--textures-for-zones) when available:
# one texel = 1/shrink Kod units = 16/shrink ROO units.  Unknown textures tile per square.
_TEX_CATALOG = ROOT / "build" / "textures" / "catalog.json"
TEX_SIZES: dict[str, tuple[float, float]] = {}
if _TEX_CATALOG.exists():
    for _k, _t in json.loads(_TEX_CATALOG.read_text())["textures"].items():
        TEX_SIZES[_k] = (_t["w"] / _t["shrink"] * HEIGHT_TO_ROO, _t["h"] / _t["shrink"] * HEIGHT_TO_ROO)


def tex_repeat(t: int) -> tuple[float, float]:
    """(width, height) in ROO units covered by one repeat of texture t."""
    return TEX_SIZES.get(tex_key(t), (ROO_PER_SQUARE, ROO_PER_SQUARE))


def grid_to_roo(row: int, col: int, fine_row: int = 0, fine_col: int = 0) -> tuple[float, float]:
    return ((col - 1) * 64 + fine_col) * HEIGHT_TO_ROO, ((row - 1) * 64 + fine_row) * HEIGHT_TO_ROO


def square_center_roo(row: int, col: int) -> tuple[float, float]:
    return grid_to_roo(row, col, 32, 32)


class SectorHeights:
    """Floor/ceiling height (ROO units) at a point, honouring slopes."""

    def __init__(self, sectors):
        self.sectors = sectors

    @staticmethod
    def _plane_z(slope, x, y):
        if not slope or abs(slope.c) < 1e-6:
            return None
        return -(slope.a * x + slope.b * y + slope.d) / slope.c

    def floor(self, si: int, x: float, y: float) -> float:
        s = self.sectors[si - 1]
        z = self._plane_z(s.floor_slope, x, y)
        return z if z is not None else s.floorh * HEIGHT_TO_ROO

    def ceil(self, si: int, x: float, y: float) -> float:
        s = self.sectors[si - 1]
        z = self._plane_z(s.ceiling_slope, x, y)
        return z if z is not None else s.ceilh * HEIGHT_TO_ROO


class MeshBuilder:
    """Accumulates triangles per material key."""

    def __init__(self):
        self.prims: dict[str, dict[str, list]] = {}

    def _prim(self, key):
        return self.prims.setdefault(key, {"pos": [], "nrm": [], "uv": [], "idx": []})

    def quad(self, key, p0, p1, p2, p3, uv0, uv1, uv2, uv3, double=False):
        """p* are (x, y, z) in ROO space with z = height.  Winding p0->p1->p2->p3."""
        self.poly(key, [p0, p1, p2, p3], [uv0, uv1, uv2, uv3])
        if double:
            self.poly(key, [p3, p2, p1, p0], [uv3, uv2, uv1, uv0])

    def poly(self, key, pts, uvs):
        if len(pts) < 3:
            return
        g = [to_gltf(p) for p in pts]
        n = newell_normal(g)
        if n is None:
            return
        pr = self._prim(key)
        base = len(pr["pos"])
        for p, uv in zip(g, uvs):
            pr["pos"].append(p)
            pr["nrm"].append(n)
            pr["uv"].append(uv)
        for i in range(1, len(pts) - 1):
            pr["idx"] += [base, base + i, base + i + 1]


def to_gltf(p):
    x, y, z = p
    return (x * M_PER_ROO, z * M_PER_ROO, y * M_PER_ROO)


def newell_normal(pts):
    nx = ny = nz = 0.0
    for i in range(len(pts)):
        x0, y0, z0 = pts[i]
        x1, y1, z1 = pts[(i + 1) % len(pts)]
        nx += (y0 - y1) * (z0 + z1)
        ny += (z0 - z1) * (x0 + x1)
        nz += (x0 - x1) * (y0 + y1)
    l = math.sqrt(nx * nx + ny * ny + nz * nz)
    if l < 1e-9:
        return None
    return (nx / l, ny / l, nz / l)


def tex_key(t: int) -> str:
    return "grd%05d" % t if t else "untextured"


def build_room_mesh(room: Room) -> MeshBuilder:
    mb = MeshBuilder()
    H = SectorHeights(room.sectors)

    # ---- floors & ceilings from BSP leaves (convex polygons)
    for node in room.bsp.walk():
        if node.type != BSP_LEAF or not node.sector or len(node.points) < 3:
            continue
        s = room.sectors[node.sector - 1]
        pts = node.points
        # glTF normal must point up for floors.  In ROO space (x east, y south), the
        # gltf mapping (x, h, y) keeps handedness; decide winding by the computed normal.
        floor = [(x, y, H.floor(node.sector, x, y)) for x, y in pts]
        fw, fh = tex_repeat(s.floor_type)
        uvs = [(x / fw, y / fh) for x, y in pts]
        g = [to_gltf(p) for p in floor]
        n = newell_normal(g)
        if n and n[1] < 0:
            floor, uvs = floor[::-1], uvs[::-1]
        mb.poly(tex_key(s.floor_type), floor, uvs)

        if s.ceiling_type:  # 0 = open sky
            ceil = [(x, y, H.ceil(node.sector, x, y)) for x, y in pts]
            cw, chh = tex_repeat(s.ceiling_type)
            cuv = [(x / cw, y / chh) for x, y in pts]
            n = newell_normal([to_gltf(p) for p in ceil])
            if n and n[1] > 0:
                ceil, cuv = ceil[::-1], cuv[::-1]
            mb.poly(tex_key(s.ceiling_type), ceil, cuv)

    # ---- walls from client walls (Doom-style upper / lower / middle sections)
    seen = set()
    for w in room.client_walls:
        key = (round(w.x0), round(w.y0), round(w.x1), round(w.y1), w.pos_sector, w.neg_sector)
        if key in seen:
            continue
        seen.add(key)
        length = math.hypot(w.x1 - w.x0, w.y1 - w.y0)
        if length < 1:
            continue
        sd_pos = room.sidedefs[w.pos_sidedef - 1] if w.pos_sidedef else None
        sd_neg = room.sidedefs[w.neg_sidedef - 1] if w.neg_sidedef else None
        P, N = w.pos_sector, w.neg_sector
        def emit(tex, b0, t0, b1, t1, double=True):
            if t0 - b0 < 1 and t1 - b1 < 1:
                return
            t0, t1 = max(t0, b0), max(t1, b1)
            rw, rh = tex_repeat(tex)
            u1 = length / rw
            mb.quad(tex_key(tex),
                    (w.x0, w.y0, b0), (w.x1, w.y1, b1), (w.x1, w.y1, t1), (w.x0, w.y0, t0),
                    (0, -b0 / rh), (u1, -b1 / rh), (u1, -t1 / rh), (0, -t0 / rh), double=double)

        if not P and not N:
            continue
        if not P or not N:  # one-sided: solid wall
            S, sd = (P, sd_pos) if P else (N, sd_neg)
            if sd is None:
                sd = sd_pos or sd_neg
            emit(sd.type_normal if sd else 0,
                 H.floor(S, w.x0, w.y0), H.ceil(S, w.x0, w.y0),
                 H.floor(S, w.x1, w.y1), H.ceil(S, w.x1, w.y1))
            continue

        fP0, fP1 = H.floor(P, w.x0, w.y0), H.floor(P, w.x1, w.y1)
        fN0, fN1 = H.floor(N, w.x0, w.y0), H.floor(N, w.x1, w.y1)
        cP0, cP1 = H.ceil(P, w.x0, w.y0), H.ceil(P, w.x1, w.y1)
        cN0, cN1 = H.ceil(N, w.x0, w.y0), H.ceil(N, w.x1, w.y1)
        sky = not room.sectors[P - 1].ceiling_type and not room.sectors[N - 1].ceiling_type

        # lower section: the side with the lower floor sees it -> that side's "below" texture
        lowP = (fP0 + fP1) <= (fN0 + fN1)
        sd_low = sd_pos if lowP else sd_neg
        emit(sd_low.type_below if sd_low else 0,
             min(fP0, fN0), max(fP0, fN0), min(fP1, fN1), max(fP1, fN1))

        # upper section (skipped between two open-sky sectors)
        if not sky:
            highP = (cP0 + cP1) >= (cN0 + cN1)
            sd_up = sd_pos if highP else sd_neg
            emit(sd_up.type_above if sd_up else 0,
                 min(cP0, cN0), max(cP0, cN0), min(cP1, cN1), max(cP1, cN1))

        # middle (fences, windows, railings): only where a normal texture is set
        for sd in (sd_pos, sd_neg):
            if sd and sd.type_normal:
                b0, t0 = max(fP0, fN0), min(cP0, cN0)
                b1, t1 = max(fP1, fN1), min(cP1, cN1)
                if sd.flags & WF_NO_VTILE:
                    # drawn once, one texture tall (fences, hedges, railings)
                    th = tex_repeat(sd.type_normal)[1]
                    if sd.flags & WF_NORMAL_TOPDOWN and not sky:
                        b0, b1 = max(b0, t0 - th), max(b1, t1 - th)
                    else:
                        t0, t1 = min(t0, b0 + th), min(t1, b1 + th)
                emit(sd.type_normal, b0, t0, b1, t1)
                break
    return mb


# ------------------------------------------------------------------ glb writer

def write_glb(mb: MeshBuilder, path: Path, name: str) -> dict:
    bin_chunks = bytearray()
    buffer_views, accessors, materials, primitives = [], [], [], []
    tri_count = 0

    def add_view(data: bytes, target: int) -> int:
        nonlocal bin_chunks
        while len(bin_chunks) % 4:
            bin_chunks += b"\0"
        buffer_views.append({"buffer": 0, "byteOffset": len(bin_chunks), "byteLength": len(data), "target": target})
        bin_chunks += data
        return len(buffer_views) - 1

    for key in sorted(mb.prims):
        pr = mb.prims[key]
        if not pr["idx"]:
            continue
        pos = pr["pos"]
        mins = [min(p[i] for p in pos) for i in range(3)]
        maxs = [max(p[i] for p in pos) for i in range(3)]
        pv = add_view(b"".join(struct.pack("<3f", *p) for p in pos), 34962)
        nv = add_view(b"".join(struct.pack("<3f", *n) for n in pr["nrm"]), 34962)
        uv = add_view(b"".join(struct.pack("<2f", *t) for t in pr["uv"]), 34962)
        iv = add_view(struct.pack("<%dI" % len(pr["idx"]), *pr["idx"]), 34963)
        a0 = len(accessors)
        accessors += [
            {"bufferView": pv, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mins, "max": maxs},
            {"bufferView": nv, "componentType": 5126, "count": len(pos), "type": "VEC3"},
            {"bufferView": uv, "componentType": 5126, "count": len(pos), "type": "VEC2"},
            {"bufferView": iv, "componentType": 5125, "count": len(pr["idx"]), "type": "SCALAR"},
        ]
        # deterministic pastel colour per texture id so the blockout is readable untextured
        h = (hash(key) & 0xFFFF) / 0xFFFF if key != "untextured" else 0
        r, g, b = (0.55 + 0.35 * math.sin(6.28 * (h + k / 3)) for k in range(3))
        materials.append({"name": key, "doubleSided": True,
                          "pbrMetallicRoughness": {"baseColorFactor": [r, g, b, 1.0],
                                                   "metallicFactor": 0.0, "roughnessFactor": 0.9}})
        primitives.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "TEXCOORD_0": a0 + 2},
                           "indices": a0 + 3, "material": len(materials) - 1})
        tri_count += len(pr["idx"]) // 3

    gltf = {
        "asset": {"version": "2.0", "generator": "meridian-remastered roo2gltf"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": primitives}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(bin_chunks)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(bin_chunks) % 4:
        bin_chunks += b"\0"
    total = 12 + 8 + len(js) + 8 + len(bin_chunks)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(bin_chunks), 0x004E4942) + bytes(bin_chunks))
    return {"triangles": tri_count, "materials": len(materials)}


# --------------------------------------------------------------- layout / meta

def world(room_xy, height_roo: float) -> list[float]:
    """ROO (x, y) + height -> glTF metres [x, y(up), z]."""
    g = to_gltf((room_xy[0], room_xy[1], height_roo))
    return [round(v, 3) for v in g]


def floor_at(room: Room, x: float, y: float) -> float:
    """Floor height at a ROO point by descending the BSP tree."""
    node = room.bsp
    H = SectorHeights(room.sectors)
    while node is not None:
        if node.type == BSP_LEAF:
            return H.floor(node.sector, x, y) if node.sector else 0.0
        side = node.a * x + node.b * y + node.c
        node = node.pos if side >= 0 else node.neg
        if node is None:
            break
    return 0.0


def zone_layout(zone: dict, room: Room) -> dict:
    def at(row, col, fr=32, fc=32):
        xy = grid_to_roo(row, col, fr, fc)
        return world(xy, floor_at(room, *xy))

    exits = []
    for e in zone["exits"]:
        item = {"pos": at(e["row"], e["col"]), "row": e["row"], "col": e["col"]}
        if e.get("locked"):
            item["locked"] = True
            item["message"] = e.get("message")
        else:
            item.update({"dest_rid": e["dest_rid"], "dest_row": e["dest_row"], "dest_col": e["dest_col"]})
        exits.append(item)
    t = zone["teleport"]
    objects = []
    for o in zone["objects"]:
        if "row" in o and "col" in o and isinstance(o["row"], int) and isinstance(o["col"], int):
            fr = o.get("fine_row", 32) if isinstance(o.get("fine_row"), int) else 32
            fc = o.get("fine_col", 32) if isinstance(o.get("fine_col"), int) else 32
            objects.append({"class": o.get("class"), "params": o.get("params"), "pos": at(o["row"], o["col"], fr, fc),
                            "yaw_kod": o.get("angle")})
    gens = []
    for g in (zone.get("spawning") or {}).get("generators", []):
        if len(g) >= 2 and all(isinstance(v, int) for v in g[:2]):
            gens.append({"row": g[0], "col": g[1], "pos": at(g[0], g[1])})
    xs = [p[0] for n in room.bsp.walk() if n.type == BSP_LEAF for p in n.points]
    ys = [p[1] for n in room.bsp.walk() if n.type == BSP_LEAF for p in n.points]
    return {
        "rid": zone["rid"],
        "class": zone["class"],
        "name": zone["name"],
        "bounds_m": {"min": world((min(xs), min(ys)), 0), "max": world((max(xs), max(ys)), 0)},
        # The Kod grid rectangle is [0, width] x [0, height] in this zone's ROO coords (room
        # header).  Geometry can extend beyond it (e.g. the forest drawn outside Raza's walls);
        # leaving the rectangle on a side with an edge exit is how the original changes zone.
        "grid_size_roo": [room.width, room.height],
        "grid_size_m": [round(room.width * M_PER_ROO, 3), round(room.height * M_PER_ROO, 3)],
        "teleport": {"pos": at(t["row"], t["col"]), "yaw_kod": t.get("angle")} if t.get("row") else None,
        "exits": exits,
        "edge_exits": zone["edge_exits"],
        "objects": objects,
        "generators": gens,
    }


# --------------------------------------------------------------------- preview

def write_preview(room: Room, layout: dict, path: Path, size: int = 1400):
    from PIL import Image, ImageDraw

    pts = [p for n in room.bsp.walk() if n.type == BSP_LEAF for p in n.points]
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    span = max(x1 - x0, y1 - y0) or 1
    sc = (size - 40) / span
    W, Hh = int((x1 - x0) * sc) + 40, int((y1 - y0) * sc) + 40
    img = Image.new("RGB", (W, Hh), (24, 24, 28))
    d = ImageDraw.Draw(img)
    T = lambda x, y: (20 + (x - x0) * sc, 20 + (y - y0) * sc)  # noqa: E731
    heights = [s.floorh for s in room.sectors] or [0]
    lo, hi = min(heights), max(heights)
    for n in room.bsp.walk():
        if n.type == BSP_LEAF and n.sector and len(n.points) >= 3:
            s = room.sectors[n.sector - 1]
            k = (s.floorh - lo) / ((hi - lo) or 1)
            col = (int(60 + 150 * k), int(90 + 120 * k), int(70 + 60 * (1 - k)))
            d.polygon([T(*p) for p in n.points], fill=col)
    for w in room.client_walls:
        if not w.pos_sector or not w.neg_sector:
            d.line([T(w.x0, w.y0), T(w.x1, w.y1)], fill=(235, 235, 235), width=2)
    # grid squares for exits, teleport and generators (convert back from metres)
    inv = lambda p: T(p[0] / M_PER_ROO, p[2] / M_PER_ROO)  # noqa: E731
    r = max(4, int(sc * 400))
    for e in layout["exits"]:
        cx, cy = inv(e["pos"])
        d.rectangle([cx - r, cy - r, cx + r, cy + r], outline=(255, 80, 80) if e.get("locked") else (80, 200, 255), width=2)
    for g in layout["generators"]:
        cx, cy = inv(g["pos"])
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 160, 0), width=2)
    if layout["teleport"]:
        cx, cy = inv(layout["teleport"]["pos"])
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 0))
    for o in layout["objects"]:
        cx, cy = inv(o["pos"])
        d.ellipse([cx - 2, cy - 2, cx + 2, cy + 2], fill=(255, 0, 255))
    d.text((8, 4), f'{layout["rid"]} {layout["name"]}  (N up, 1 sq = {M_PER_SQUARE} m)', fill=(255, 255, 255))
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rid", type=int, nargs="*")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--rooms", type=Path, default=ROOT / "Server-104" / "resource" / "rooms")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "zones")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)

    zones = json.loads((ROOT / "data" / "zones.json").read_text(encoding="utf-8"))
    layouts = []
    for z in zones:
        if a.rid and z["rid"] not in a.rid:
            continue
        roo = next((p for p in a.rooms.iterdir() if p.name.lower() == z["roo"].lower()), None)
        if roo is None:
            print(f"!! {z['rid']}: {z['roo']} not found", file=sys.stderr)
            continue
        room = Room.load(str(roo))
        stem = f"{z['rid']}_{z['class']}"
        stats = write_glb(build_room_mesh(room), a.out / f"{stem}.glb", stem)
        lay = zone_layout(z, room)
        lay["mesh"] = f"build/zones/{stem}.glb"
        lay["roo"] = z["roo"]
        layouts.append(lay)
        if a.preview:
            write_preview(room, lay, a.out / f"{stem}.png")
        b = lay["bounds_m"]
        print(f"{stem:28s} {stats['triangles']:6d} tris {stats['materials']:3d} mats  "
              f"{b['max'][0] - b['min'][0]:6.1f} x {b['max'][2] - b['min'][2]:6.1f} m")

    # Zones drawn from the same physical map with a shifted grid origin (e.g. Raza town and
    # the Outskirts) share geometry; record the offset so the level builder can overlay them
    # into one seamless outdoor space instead of two copies.
    rooms_by_rid = {}
    for lay in layouts:
        r = Room.load(str(next(p for p in a.rooms.iterdir() if p.name.lower() == lay["roo"].lower())))
        pts = [p for n in r.bsp.walk() if n.type == BSP_LEAF for p in n.points]
        rooms_by_rid[lay["rid"]] = (min(p[0] for p in pts), min(p[1] for p in pts),
                                    max(p[0] for p in pts), max(p[1] for p in pts))
    for lay in layouts:
        bx = rooms_by_rid[lay["rid"]]
        for other in layouts:
            ob = rooms_by_rid[other["rid"]]
            # identical extents on a large (outdoor-sized) map; small interiors of equal size
            # are common and unrelated (e.g. the Raza bar and vault)
            if other["rid"] < lay["rid"] and abs((bx[2] - bx[0]) - (ob[2] - ob[0])) < 1 \
                    and abs((bx[3] - bx[1]) - (ob[3] - ob[1])) < 1 \
                    and (bx[2] - bx[0]) > 40 * ROO_PER_SQUARE:
                off = (bx[0] - ob[0], bx[1] - ob[1])
                lay["shares_geometry_with"] = {
                    "rid": other["rid"],
                    "roo_offset": list(off),
                    "offset_m": [round(off[0] * M_PER_ROO, 3), 0.0, round(off[1] * M_PER_ROO, 3)],
                    "note": "this zone's coords = other zone's coords + offset; place this zone's "
                            "origin at -offset_m relative to the other to overlay them",
                }
                print(f"  {lay['rid']} shares geometry with {other['rid']} (offset {off})")

    # World placement (single source of truth for the UE level builder and the game's zone
    # subsystem).  Zones sit on a 2 km grid in UE space (X east, Y south, cm); a zone that shares
    # geometry with another is placed so the two overlay exactly.
    spacing_cm = 200000.0
    slot = 0
    by_rid = {lay["rid"]: lay for lay in layouts}
    for lay in sorted(layouts, key=lambda l: l["rid"]):
        if "shares_geometry_with" in lay:
            continue
        lay["world_origin_cm"] = [(slot % 4) * spacing_cm, (slot // 4) * spacing_cm, 0.0]
        slot += 1
    for lay in layouts:
        sg = lay.get("shares_geometry_with")
        if sg and sg["rid"] in by_rid:
            o = by_rid[sg["rid"]]["world_origin_cm"]
            off_cm = [sg["roo_offset"][0] * M_PER_ROO * 100, sg["roo_offset"][1] * M_PER_ROO * 100]
            # same physical point: roo_this = roo_other + offset  ->  origin_this = origin_other - offset
            lay["world_origin_cm"] = [round(o[0] - off_cm[0], 3), round(o[1] - off_cm[1], 3), o[2]]

    if not a.rid:
        out = {"m_per_square": M_PER_SQUARE, "m_per_roo_unit": M_PER_ROO,
               "axes": "positions: glTF metres [+X east, +Y up, +Z south] relative to the zone; world_origin_cm: UE cm [X east, Y south, Z up]", "zones": layouts}
        (ROOT / "data" / "zone_layout.json").write_text(json.dumps(out, indent=1, ensure_ascii=False), encoding="utf-8")
        print("wrote data/zone_layout.json")


if __name__ == "__main__":
    main()
