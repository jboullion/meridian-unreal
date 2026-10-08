"""
Convert Meridian 59 .roo rooms into glTF (.glb) blockouts + zone metadata.

    python tools/roo2gltf/roo2gltf.py                 # all zones in data/zones.json
    python tools/roo2gltf/roo2gltf.py --rid 300 301   # selected zones
    python tools/roo2gltf/roo2gltf.py --preview       # also write top-down PNG previews

Outputs (build/ is git-ignored, regenerate any time):
    build/zones/<rid>_<class>.glb        geometry, one primitive per original texture (material "grdNNNNN")
    build/zones/<rid>_<class>_collision.glb   the same without the walls the original lets you walk
                                         through (WF_PASSABLE: field tops, hanging signs, wall torches);
                                         the world build's collision (tools/ue/build_world.py)
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
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from server104 import SERVER104  # noqa: E402  (ReferenceServers/Server-104)
sys.path.insert(0, str(SERVER104 / "roomedit" / "roogen"))
from roofile import Room, BSP_LEAF  # noqa: E402

ROO_PER_SQUARE = 1024.0
HEIGHT_TO_ROO = 16.0
M_PER_SQUARE = 2.2
M_PER_ROO = M_PER_SQUARE / ROO_PER_SQUARE

WF_BACKWARDS = 0x1
WF_TRANSPARENT = 0x2
WF_PASSABLE = 0x4
WF_ABOVE_BOTTOMUP = 0x40
WF_BELOW_TOPDOWN = 0x80
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


def signed16(v: int) -> int:
    return v - 0x10000 if v >= 0x8000 else v


def wall_uvs(tex: int, sd, section: str, side: int, w, length: float, b0, t0, b1, t1):
    """Wall texture coordinates as the original client computes them (clientd3d/d3drender.c,
    D3DRenderWallExtract). Returns ((b0, t0, b1, t1), (uv_b0, uv_t0, uv_b1, uv_t1)) for the
    corners (x0, b0), (x0, t0), (x1, b1), (x1, t1); the heights change only when a
    WF_NO_VTILE texture is clipped to one repeat.

    - Horizontal: the drawing side's start vertex (x0 for the pos side, x1 for the neg side)
      plus that side's x offset; WF_BACKWARDS mirrors it. (Restarting at 0 on every wall piece
      broke textures across BSP splits.)
    - Vertical: anchored to the section's own bottom (texture bottom row on the wall's bottom
      edge) or top (texture top row on the top edge) plus the y offset, not to world height 0.
      Normal and below sections are bottom-up unless WF_NORMAL_TOPDOWN / WF_BELOW_TOPDOWN;
      above sections are top-down unless WF_ABOVE_BOTTOMUP. Sloped edges anchor to the
      enclosing whole grid square, like the client.
    - Offsets are in Kod fine units (16 ROO units), signed 16-bit."""
    rw, rh = tex_repeat(tex)
    flags = sd.flags if sd else 0
    xoff = signed16(w.pos_xoff if side > 0 else w.neg_xoff) * HEIGHT_TO_ROO
    yoff = signed16(w.pos_yoff if side > 0 else w.neg_yoff) * HEIGHT_TO_ROO
    start, end = xoff / rw, (xoff + length) / rw
    if flags & WF_BACKWARDS:
        start, end = end, start
    u_x0, u_x1 = (start, end) if side > 0 else (end, start)

    if section == "normal":
        topdown = bool(flags & WF_NORMAL_TOPDOWN)
    elif section == "below":
        topdown = bool(flags & WF_BELOW_TOPDOWN)
    else:
        topdown = not flags & WF_ABOVE_BOTTOMUP
    if topdown:
        top = t0 if t0 == t1 else math.ceil(max(t0, t1) / ROO_PER_SQUARE) * ROO_PER_SQUARE

        def v(z):
            return (top - z - yoff) / rh
    else:
        bottom = b0 if b0 == b1 else math.floor(min(b0, b1) / ROO_PER_SQUARE) * ROO_PER_SQUARE

        def v(z):
            return 1.0 - (yoff + z - bottom) / rh

    if section == "normal" and flags & WF_NO_VTILE:
        # drawn once: keep the part of the wall where 0 <= v <= 1 (fences, railings, hedges)
        def clip(b, t):
            vb, vt = v(b), v(t)  # v falls as z rises
            if vt < 0:
                t = b + (t - b) * (vb - 0.0) / (vb - vt) if vb != vt else b
            if vb > 1:
                b = t - (t - b) * (1.0 - v(t)) / (vb - v(t)) if vb != v(t) else t
            return b, t
        b0, t0 = clip(b0, t0)
        b1, t1 = clip(b1, t1)
    return (b0, t0, b1, t1), ((u_x0, v(b0)), (u_x0, v(t0)), (u_x1, v(b1)), (u_x1, v(t1)))


def grid_to_roo(row: int, col: int, fine_row: int = 0, fine_col: int = 0) -> tuple[float, float]:
    return ((col - 1) * 64 + fine_col) * HEIGHT_TO_ROO, ((row - 1) * 64 + fine_row) * HEIGHT_TO_ROO


def square_center_roo(row: int, col: int) -> tuple[float, float]:
    return grid_to_roo(row, col, 32, 32)


# How far you sink into a sector with a wading depth (SF_MASK_DEPTH), ROO units: 0, 1/5, 2/5 and 3/5
# of a square (blakserv roofile.c DEPTHMODIFY*, clientd3d draw3d.c sector_depths)
DEPTH_SINK_ROO = (0.0, ROO_PER_SQUARE / 5, 2 * ROO_PER_SQUARE / 5, 3 * ROO_PER_SQUARE / 5)


class SectorHeights:
    """Floor/ceiling height (ROO units) at a point, honouring slopes. wading: floors of sectors with
    a depth are lowered by it, to where the original stands players and objects (floor - depth):
    Raza's wheat fields are blocks 0.86 m above the grass with depth 2 (0.88 m), so you wade
    through them at ground level with the wheat at your waist."""

    def __init__(self, sectors, wading=False):
        self.sectors = sectors
        self.wading = wading

    @staticmethod
    def _plane_z(slope, x, y):
        if not slope or abs(slope.c) < 1e-6:
            return None
        return -(slope.a * x + slope.b * y + slope.d) / slope.c

    def floor(self, si: int, x: float, y: float) -> float:
        s = self.sectors[si - 1]
        z = self._plane_z(s.floor_slope, x, y)
        z = z if z is not None else s.floorh * HEIGHT_TO_ROO
        return z - DEPTH_SINK_ROO[s.blak_flags & 0x3] if self.wading else z

    def ceil(self, si: int, x: float, y: float) -> float:
        s = self.sectors[si - 1]
        z = self._plane_z(s.ceiling_slope, x, y)
        return z if z is not None else s.ceilh * HEIGHT_TO_ROO


class MeshBuilder:
    """Accumulates triangles per material key."""

    def __init__(self):
        self.prims: dict[str, dict[str, list]] = {}

    def _prim(self, key):
        return self.prims.setdefault(key, {"pos": [], "nrm": [], "uv": [], "col": [], "idx": []})

    def quad(self, key, p0, p1, p2, p3, uv0, uv1, uv2, uv3, double=False, light=None):
        """p* are (x, y, z) in ROO space with z = height.  Winding p0->p1->p2->p3. light(normal) ->
        the sector light (0..1) a face with that glTF normal sees (each side of a double wall its own)."""
        self.poly(key, [p0, p1, p2, p3], [uv0, uv1, uv2, uv3], light=light)
        if double:
            self.poly(key, [p3, p2, p1, p0], [uv3, uv2, uv1, uv0], light=light)

    def poly(self, key, pts, uvs, light=1.0):
        """light: the original sector light level (0..1) for the face, or a function of its glTF
        normal; written as the vertex colour (COLOR_0), the materials' ambient floor (docs/adr/0005)."""
        if len(pts) < 3:
            return
        g = [to_gltf(p) for p in pts]
        n = newell_normal(g)
        if n is None:
            return
        level = light(n) if callable(light) else (1.0 if light is None else light)
        pr = self._prim(key)
        base = len(pr["pos"])
        for p, uv in zip(g, uvs):
            pr["pos"].append(p)
            pr["nrm"].append(n)
            pr["uv"].append(uv)
            pr["col"].append((level, level, level, 1.0))
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


SF_FLICKER = 0x00000200  # clientd3d bsp.h: the sector's light flickers
SF_MASK_DEPTH = 0x00000003  # blakserv roofile.c: wading depth 0-3 (fields, pools); slows movement


def sector_at(room: Room):
    """-> at(x, y): the sector (1-based, as walls and BSP leaves number them) under a ROO point, from
    the BSP leaves (convex polygons), or None outside every sector."""
    leaves = [(node.points, node.sector) for node in room.bsp.walk()
              if node.type == BSP_LEAF and node.sector and len(node.points) >= 3]

    def inside(pts, x, y):
        sign = 0
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            c = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0)
            if abs(c) < 1e-6:
                continue
            if sign == 0:
                sign = 1 if c > 0 else -1
            elif (c > 0) != (sign > 0):
                return False
        return True

    def at(x, y):
        for pts, sector in leaves:
            if inside(pts, x, y):
                return sector
        return None
    return at


def sector_lights(room: Room):
    """-> light_at(x, y): the light level (0..1) of the sector at a ROO point, or None outside every
    sector. The original client lights each sector at its own level (0..255); the blockout carries it
    as vertex colour."""
    at = sector_at(room)

    def light_at(x, y):
        sector = at(x, y)
        return None if sector is None else room.sectors[sector - 1].light / 255.0
    return light_at


def build_room_mesh(room: Room, collision: bool = False) -> MeshBuilder:
    """The room's floors, ceilings and walls. collision: the surfaces you walk on and into, as the
    original moves you: floors of wading sectors at their wading height (SectorHeights wading, the
    step walls around them to match), and without the middle (normal-texture) sections the original
    lets you walk through: every sidedef of the wall has WF_PASSABLE (blakserv roofile.c
    BSPCanMoveInRoomTreeInternal, clientd3d move.c). Raza's wheat and crop field borders, its
    hanging signs and the interiors' wall torches are such walls."""
    mb = MeshBuilder()
    H = SectorHeights(room.sectors, wading=collision)
    light_at = sector_lights(room)

    # ---- floors & ceilings from BSP leaves (convex polygons)
    for node in room.bsp.walk():
        if node.type != BSP_LEAF or not node.sector or len(node.points) < 3:
            continue
        s = room.sectors[node.sector - 1]
        pts = node.points
        # glTF normal must point up for floors.  In ROO space (x east, y south), the
        # gltf mapping (x, h, y) keeps handedness; decide winding by the computed normal.
        floor = [(x, y, H.floor(node.sector, x, y)) for x, y in pts]
        # The original client tiles every floor and ceiling texture once per grid square,
        # whatever its size, shifted by the sector's texture offset (Kod fine units).
        tx, ty = signed16(s.xoffset) * HEIGHT_TO_ROO, signed16(s.yoffset) * HEIGHT_TO_ROO
        uvs = [((x - tx) / ROO_PER_SQUARE, (y - ty) / ROO_PER_SQUARE) for x, y in pts]
        g = [to_gltf(p) for p in floor]
        n = newell_normal(g)
        if n and n[1] < 0:
            floor, uvs = floor[::-1], uvs[::-1]
        mb.poly(tex_key(s.floor_type), floor, uvs, light=s.light / 255.0)

        if s.ceiling_type:  # 0 = open sky
            ceil = [(x, y, H.ceil(node.sector, x, y)) for x, y in pts]
            cuv = [((x - tx) / ROO_PER_SQUARE, (y - ty) / ROO_PER_SQUARE) for x, y in pts]
            n = newell_normal([to_gltf(p) for p in ceil])
            if n and n[1] > 0:
                ceil, cuv = ceil[::-1], cuv[::-1]
            mb.poly(tex_key(s.ceiling_type), ceil, cuv, light=s.light / 255.0)

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
        mid = ((w.x0 + w.x1) / 2.0, (w.y0 + w.y1) / 2.0)
        fallback = max(room.sectors[S - 1].light for S in (P, N) if S) / 255.0 if (P or N) else 1.0

        def wall_light(n, mid=mid, fallback=fallback):
            # the sector a wall face looks into: a little way off the wall along its normal
            # (glTF x, z = ROO x, y)
            step = 0.15 / M_PER_ROO
            level = light_at(mid[0] + n[0] * step, mid[1] + n[2] * step)
            return fallback if level is None else level

        def emit(tex, b0, t0, b1, t1, sd, section, side, double=True):
            if t0 - b0 < 1 and t1 - b1 < 1:
                return
            t0, t1 = max(t0, b0), max(t1, b1)
            (b0, t0, b1, t1), (ub0, ut0, ub1, ut1) = wall_uvs(tex, sd, section, side, w, length, b0, t0, b1, t1)
            if t0 - b0 < 1 and t1 - b1 < 1:
                return
            mb.quad(tex_key(tex),
                    (w.x0, w.y0, b0), (w.x1, w.y1, b1), (w.x1, w.y1, t1), (w.x0, w.y0, t0),
                    ub0, ub1, ut1, ut0, double=double, light=wall_light)

        if not P and not N:
            continue
        if not P or not N:  # one-sided: solid wall
            S, sd, side = (P, sd_pos, 1) if P else (N, sd_neg, -1)
            if sd is None:
                sd = sd_pos or sd_neg
            emit(sd.type_normal if sd else 0,
                 H.floor(S, w.x0, w.y0), H.ceil(S, w.x0, w.y0),
                 H.floor(S, w.x1, w.y1), H.ceil(S, w.x1, w.y1), sd, "normal", side)
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
             min(fP0, fN0), max(fP0, fN0), min(fP1, fN1), max(fP1, fN1), sd_low, "below", 1 if lowP else -1)

        # upper section (skipped between two open-sky sectors)
        if not sky:
            highP = (cP0 + cP1) >= (cN0 + cN1)
            sd_up = sd_pos if highP else sd_neg
            emit(sd_up.type_above if sd_up else 0,
                 min(cP0, cN0), max(cP0, cN0), min(cP1, cN1), max(cP1, cN1), sd_up, "above", 1 if highP else -1)

        # middle (fences, windows, railings): only where a normal texture is set
        if collision and all(sd.flags & WF_PASSABLE for sd in (sd_pos, sd_neg) if sd):
            continue
        for sd, side in ((sd_pos, 1), (sd_neg, -1)):
            if sd and sd.type_normal:
                b0, t0 = max(fP0, fN0), min(cP0, cN0)
                b1, t1 = max(fP1, fN1), min(cP1, cN1)
                # WF_NO_VTILE (fences, hedges, railings) is clipped to one repeat in wall_uvs
                emit(sd.type_normal, b0, t0, b1, t1, sd, "normal", side)
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
        cv = add_view(b"".join(struct.pack("<4f", *c) for c in pr["col"]), 34962)
        a0 = len(accessors)
        accessors += [
            {"bufferView": pv, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mins, "max": maxs},
            {"bufferView": nv, "componentType": 5126, "count": len(pos), "type": "VEC3"},
            {"bufferView": uv, "componentType": 5126, "count": len(pos), "type": "VEC2"},
            {"bufferView": iv, "componentType": 5125, "count": len(pr["idx"]), "type": "SCALAR"},
            {"bufferView": cv, "componentType": 5126, "count": len(pos), "type": "VEC4"},
        ]
        # deterministic pastel colour per texture id so the blockout is readable untextured
        h = (zlib.crc32(key.encode()) & 0xFFFF) / 0xFFFF if key != "untextured" else 0  # crc32: the same every run (str hash is salted)
        r, g, b = (0.55 + 0.35 * math.sin(6.28 * (h + k / 3)) for k in range(3))
        materials.append({"name": key, "doubleSided": True,
                          "pbrMetallicRoughness": {"baseColorFactor": [r, g, b, 1.0],
                                                   "metallicFactor": 0.0, "roughnessFactor": 0.9}})
        primitives.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "TEXCOORD_0": a0 + 2, "COLOR_0": a0 + 4},
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
    """Where something stands at a ROO point (by descending the BSP tree): the floor, lowered by a
    wading sector's depth, as the original places players and objects (clientd3d object.c)."""
    node = room.bsp
    H = SectorHeights(room.sectors, wading=True)
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
    sector = sector_at(room)
    for o in zone["objects"]:
        if "row" in o and "col" in o and isinstance(o["row"], int) and isinstance(o["col"], int):
            fr = o.get("fine_row", 32) if isinstance(o.get("fine_row"), int) else 32
            fc = o.get("fine_col", 32) if isinstance(o.get("fine_col"), int) else 32
            item = {"class": o.get("class"), "params": o.get("params"), "pos": at(o["row"], o["col"], fr, fc),
                    "yaw_kod": o.get("angle")}
            xy = grid_to_roo(o["row"], o["col"], fr, fc)
            s = sector(*xy)
            if s and room.sectors[s - 1].blak_flags & SF_FLICKER:
                item["flicker"] = True  # in a flickering sector: its lights flicker (docs/adr/0005)
            if s and room.sectors[s - 1].ceiling_type:
                # the ceiling above it (glTF metres, up): the original pins OF_HANGING objects (the
                # chandelier) to it, at ceiling - their sprite's height (clientd3d object.c RoomObjectSetHeight)
                item["ceiling_y"] = round(SectorHeights(room.sectors).ceil(s, *xy) * M_PER_ROO, 3)
            objects.append(item)
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
        "depth_areas": depth_areas(room),
    }


def roo_security(path: Path) -> int:
    """The room's security value: the u32 after the magic and version in the .roo header. The
    server sends it in BP_PLAYER and the client compares the low 28 bits (clientd3d bspload.c
    LoadRoomFile, game.c SetPlayerInfo): a mismatch means our zone isn't built from the server's room."""
    with open(path, "rb") as f:
        head = f.read(12)
    return struct.unpack_from("<I", head, 8)[0]


def depth_areas(room: Room) -> list[dict]:
    """The sectors with a wading depth (SF_MASK_DEPTH 1-3: fields, pools) as convex BSP-leaf
    polygons, [x, z] glTF metres in zone coordinates. The original slows you to 3/4, 1/2 and 1/4
    speed in them (clientd3d move.c UserMovePlayer); UMRCharacterMovementComponent does the same."""
    out = []
    for node in room.bsp.walk():
        if node.type != BSP_LEAF or not node.sector or len(node.points) < 3:
            continue
        depth = room.sectors[node.sector - 1].blak_flags & SF_MASK_DEPTH
        if depth:
            out.append({"depth": depth, "points": [[w[0], w[2]] for w in (world(p, 0) for p in node.points)]})
    return out


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


def write_walls(room: Room, path: Path) -> int:
    """The solid walls (one side open, the other none), as the original map draws them (map.c),
    in zone metres [x0, z0, x1, z1] like the blockout: the minimap's wall overlay (tools/ui/minimap.py)."""
    walls = [[round(w.x0 * M_PER_ROO, 3), round(w.y0 * M_PER_ROO, 3), round(w.x1 * M_PER_ROO, 3), round(w.y1 * M_PER_ROO, 3)]
             for w in room.client_walls if not w.pos_sector or not w.neg_sector]
    path.write_text(json.dumps({"walls": walls}))
    return len(walls)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rid", type=int, nargs="*")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--walls-only", action="store_true",
                    help="only write build/zones/<rid>_<class>_walls.json (the minimap's walls); no meshes, no layout")
    ap.add_argument("--rooms", type=Path, default=SERVER104 / "resource" / "rooms")
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
        n_walls = write_walls(room, a.out / f"{stem}_walls.json")
        if a.walls_only:
            print(f"{stem:28s} {n_walls:5d} walls")
            continue
        stats = write_glb(build_room_mesh(room), a.out / f"{stem}.glb", stem)
        write_glb(build_room_mesh(room, collision=True), a.out / f"{stem}_collision.glb", stem)
        lay = zone_layout(z, room)
        lay["mesh"] = f"build/zones/{stem}.glb"
        lay["roo"] = z["roo"]
        lay["roo_security"] = roo_security(roo)
        layouts.append(lay)
        if a.preview:
            write_preview(room, lay, a.out / f"{stem}.png")
        b = lay["bounds_m"]
        print(f"{stem:28s} {stats['triangles']:6d} tris {stats['materials']:3d} mats  "
              f"{b['max'][0] - b['min'][0]:6.1f} x {b['max'][2] - b['min'][2]:6.1f} m")

    if a.walls_only:
        return

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
