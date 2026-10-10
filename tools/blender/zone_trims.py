"""
Trims for interiors (docs/adr/0003, "Raza's interiors"): a skirting board along the foot of every
room wall that stands on a floor, and a stepped cornice along every wall top that meets a ceiling,
as solid boxes in front of the wall. Like the grime, they come from the blockout geometry alone,
so they can't misplace anything; the original never had them, and they give the rooms' corners
real depth.

zone_<rid>.json "trims" (missing: none):
  {"skirting": {"height_m": 0.14, "depth_m": 0.025, "texture": "grd09652"},
   "cornice":  {"height_m": 0.12, "depth_m": 0.08,  "texture": "grd09652"},
   "min_wall_m": 2.0,        # shorter walls are furniture (counters, tables), not room walls
   "exclude": "regex"}       # wall texture names that get none (painted doors, cabinets, shelves)

Used by build_zone_art.py (SM_Z<rid>_Trims, art: Nanite, no collision). Runs inside Blender.
"""
import re

from mathutils import Vector

import zone_grime

M_PER_SQUARE = 2.2
UP = Vector((0.0, 1.0, 0.0))
# painted doors, gates and furniture: a board across them looks wrong
DEFAULT_EXCLUDE = r"door|w/ ?dr|w/do|gate|cab|shelf|count|table|barrel|rack|weapons|torch"


def build_trims(out, prims, polys_of, cfg, catalog, no_trim=()):
    """Add the boards to `out` (a MeshOut, glTF space; slots = the texture names). Returns
    (skirting runs, cornice runs)."""
    exclude = re.compile(cfg.get("exclude", DEFAULT_EXCLUDE), re.I)
    min_wall = float(cfg.get("min_wall_m", 2.0))
    floors, ceilings = zone_grime._Lookup(), zone_grime._Lookup()
    walls = []
    for prim in prims.values():
        for a, b, c in prim.triangles:
            pa, pb, pc = (Vector(prim.positions[i]) for i in (a, b, c))
            ny = prim.normals[a][1]
            if ny > 0.97:
                floors.add(pa, pb, pc)
            elif ny < -0.97:
                ceilings.add(pa, pb, pc)
        info = catalog.get(prim.material, {})
        if prim.material in no_trim or info.get("has_transparency") or exclude.search(info.get("name", "")):
            continue
        walls += [p for p in polys_of(prim, range(len(prim.triangles))) if p.vertical]

    skirting, cornice = cfg.get("skirting"), cfg.get("cornice")
    n_skirt = n_corn = 0
    for i, poly in enumerate(walls):
        if len(poly.pts) < 3:
            continue
        n = Vector((poly.normal.x, 0.0, poly.normal.z))
        if n.length < 0.5:
            continue
        n.normalize()
        pts = sorted(poly.pts, key=lambda p: p.y)
        if pts[-1].y - pts[0].y < min_wall:
            continue
        # a hair's difference per wall, so two boards meeting in a corner never share a plane
        eps = 0.0015 * (i % 3)
        a, b = pts[0], pts[1]
        if skirting and (b - a).length > 0.2 and abs(a.y - b.y) < 0.02:
            probe = (a + b) / 2 + n * 0.3
            if any(abs(h - a.y) < 0.15 for h in floors.heights(probe.x, probe.z)):
                h = float(skirting.get("height_m", 0.14)) + eps
                d = float(skirting.get("depth_m", 0.025)) + eps
                _board(out, a, b, n, 0.0, h, d, skirting["texture"], catalog)
                n_skirt += 1
        c, e = pts[-1], pts[-2]
        if cornice and (e - c).length > 0.2 and abs(c.y - e.y) < 0.02:
            probe = (c + e) / 2 + n * 0.3
            if any(abs(h - c.y) < 0.15 for h in ceilings.heights(probe.x, probe.z)):
                h = float(cornice.get("height_m", 0.12))
                d = float(cornice.get("depth_m", 0.08))
                # two steps: the deep upper band against the ceiling, a slimmer one under it
                _board(out, c, e, n, -h * 0.55, 0.0, d + eps, cornice["texture"], catalog)
                _board(out, c, e, n, -h, -h * 0.55, d * 0.45 + eps, cornice["texture"], catalog)
                n_corn += 1
    return n_skirt, n_corn


def _board(out, a, b, n, y0, y1, depth, tex, catalog):
    """A box along the wall edge a-b (both at the edge's height), from y0 to y1 above it, `depth`
    out from the wall along n. UVs at the original's scale (one texture per its squares x 2.2 m)."""
    info = catalog.get(tex, {})
    tw = info.get("squares_w", 1.0) * M_PER_SQUARE
    th = info.get("squares_h", 1.0) * M_PER_SQUARE
    along = b - a
    length = along.length
    a0, b0 = a + UP * y0, b + UP * y0
    a1, b1 = a + UP * y1, b + UP * y1
    off = n * depth
    h = y1 - y0

    def uv(s, t):
        # from the middle of the texture: its edges are often a dark border or a painted trim
        return Vector((s / tw, -0.45 - t / th))

    # front, top, bottom, the two ends
    out.poly([a0 + off, b0 + off, b1 + off, a1 + off], [uv(0, 0), uv(length, 0), uv(length, h), uv(0, h)], tex, n)
    out.poly([a1, b1, b1 + off, a1 + off], [uv(0, 0), uv(length, 0), uv(length, depth), uv(0, depth)], tex, UP)
    out.poly([a0, b0, b0 + off, a0 + off], [uv(0, 0), uv(length, 0), uv(length, depth), uv(0, depth)], tex, -UP)
    side = along.normalized()
    for p0, p1, want in ((a0, a1, -side), (b0, b1, side)):
        out.poly([p0, p0 + off, p1 + off, p1], [uv(0, 0), uv(depth, 0), uv(depth, h), uv(0, h)], tex, want)
