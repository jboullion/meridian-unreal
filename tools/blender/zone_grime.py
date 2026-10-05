"""
Grime strips for zone art (docs/adr/0003, "Grime"): dirt rising from the foot of every wall that
stands on a floor, and a shadowy band under every eave, as thin strips 1 cm in front of the wall
that tools/ue/build_world.py turns into mesh decals (materials.json "grime" sets their colour and
strength). They come from the blockout geometry alone - where a wall meets a floor, where a roof
sits on a wall - never from reading the textures, so they can't misplace anything.

Used by build_zone_art.py (SM_Z<rid>_Grime). Runs inside Blender (mathutils).
"""
import math

from mathutils import Vector

OFFSET_M = 0.012   # in front of the wall, so the decal never fights it
CELL_M = 4.0       # spatial hash for the floor / roof lookups
UP = Vector((0.0, 1.0, 0.0))  # glTF space: y up


class _Lookup:
    """Horizontal (floors) or sloped (roofs) blockout triangles, hashed on (x, z)."""

    def __init__(self):
        self.cells = {}

    def add(self, a, b, c):
        x0, x1 = min(a.x, b.x, c.x), max(a.x, b.x, c.x)
        z0, z1 = min(a.z, b.z, c.z), max(a.z, b.z, c.z)
        for i in range(math.floor(x0 / CELL_M), math.floor(x1 / CELL_M) + 1):
            for j in range(math.floor(z0 / CELL_M), math.floor(z1 / CELL_M) + 1):
                self.cells.setdefault((i, j), []).append((a, b, c))

    def heights(self, x, z):
        """Heights of every triangle above or below (x, z)."""
        out = []
        for a, b, c in self.cells.get((math.floor(x / CELL_M), math.floor(z / CELL_M)), ()):
            # barycentric in the xz plane
            d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z)
            if abs(d) < 1e-9:
                continue
            l1 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d
            l2 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d
            l3 = 1.0 - l1 - l2
            if min(l1, l2, l3) >= -1e-4:
                out.append(l1 * a.y + l2 * b.y + l3 * c.y)
        return out


def build_grime(out, prims, polys_of, cfg, no_grime):
    """Add the strips to `out` (a MeshOut, glTF space) in slots "grime_base" and "grime_eave".
    cfg: materials.json "grime"; no_grime: wall textures that get none (cut-outs, foliage, the
    names matching cfg "exclude"). Returns (base strips, eave strips)."""
    floors, roofs = _Lookup(), _Lookup()
    walls = []
    for prim in prims.values():
        for a, b, c in prim.triangles:
            pa, pb, pc = (Vector(prim.positions[i]) for i in (a, b, c))
            ny = prim.normals[a][1]
            if ny > 0.97:
                floors.add(pa, pb, pc)
            elif 0.1 < ny <= 0.97:
                roofs.add(pa, pb, pc)
        if prim.material in no_grime:
            continue
        walls += [p for p in polys_of(prim, range(len(prim.triangles))) if p.vertical]

    base_h = float(cfg.get("base", {}).get("height_m", 0.9))
    eave_h = float(cfg.get("eave", {}).get("height_m", 0.6))
    n_base = n_eave = 0
    for poly in walls:
        if len(poly.pts) < 3:
            continue
        n = Vector((poly.normal.x, 0.0, poly.normal.z))
        if n.length < 0.5:
            continue
        n.normalize()
        pts = sorted(poly.pts, key=lambda p: p.y)
        top_y, bottom_y = pts[-1].y, pts[0].y
        # foot: the two lowest corners, where the wall stands on a floor in front of it
        a, b = pts[0], pts[1]
        mid = (a + b) / 2
        if (b - a).length > 0.2 and abs(a.y - b.y) < 0.5 * (b - a).length:
            probe = mid + n * 0.3
            if any(abs(h - mid.y) < 0.15 for h in floors.heights(probe.x, probe.z)):
                h = min(base_h, 0.6 * (top_y - bottom_y))
                _strip(out, a, b, UP * h, n, "grime_base")
                n_base += 1
        # eave: a level top edge with a roof starting on it
        c, d = pts[-1], pts[-2]
        if (d - c).length > 0.2 and abs(c.y - d.y) < 0.02:
            mid = (c + d) / 2
            probe = mid - n * 0.25  # just behind the wall face, under the roof
            if any(-0.05 < h - mid.y < 1.2 for h in roofs.heights(probe.x, probe.z)):
                h = min(eave_h, 0.5 * (top_y - bottom_y))
                _strip(out, c, d, -UP * h, n, "grime_eave")
                n_eave += 1
    return n_base, n_eave


def _strip(out, a, b, rise, n, slot):
    """Quad from edge a-b along `rise`, OFFSET_M in front of the wall. UV: u = metres along the
    edge, v = 0 at the edge (the ground or the eave), 1 at the far side (glTF convention)."""
    off = n * OFFSET_M
    length = (b - a).length
    pts = [a + off, b + off, b + rise + off, a + rise + off]
    uvs = [(0.0, 0.0), (length, 0.0), (length, 1.0), (0.0, 1.0)]
    out.poly(pts, uvs, slot, n)
