"""
Zone art pass (docs/adr/0003 pass 3): rebuild the buildings listed in data/environment/zone_<rid>.json
as real geometry, from the roo2gltf blockout and the painted-feature descriptions in
data/environment/facades.json. Runs in headless Blender:

    blender -b --factory-startup -P tools/blender/build_zone_art.py -- --rid 300 [--preview] [--strict] [--plain]

--plain skips all the rebuilding: every building is the original blockout geometry as it is (still
cut into the displacement grid), for judging what height maps alone do (tools/lookdev/ai_maps_test.ps1).

zone_<rid>.json "rebuild" picks what is rebuilt (default all): "facades" (walls described in
facades.json: openings, trims, plinths, window panes, timber relief), "plain_facades" (only the
facades.json walls without painted windows or doors: their bands, piers and plinth), "roofs" (thickness and
overhangs), "parapets" (crenellation strips as solid merlons; without it they are cut-out solids),
"cutouts" (fences, gates, signs as solids). Anything not rebuilt is the original geometry.
materials.json "relief" "displacement": false leaves out the displacement grid.

For each building, every blockout face in its region is either rebuilt or copied:
  - facade walls (texture described in facades.json): the outward face keeps the original texture
    and UVs, but painted openings are cut and recessed (stained glass / door panel at the back of a
    stone reveal, showing the painted image), painted surrounds and bands stand proud of the wall,
    and a plinth runs along the base;
  - crenellation strips (alpha-cut merlons) become a solid parapet with real merlons;
  - sloped roofs (facades.json "roofs") get thickness and eave/verge overhangs;
  - water bodies (kind "water") become a sunken bed with a separate water surface mesh;
  - cut-out (alpha) originals - fences, gates, signs - become solids traced from their alpha mask
    and extruded a few cm (facades.json "cutouts"), in the "<grd>__solid" slot; foliage cut-outs
    (tree lines, field tops) stay flat cut-outs. A "kind": "cutouts" entry picks up the ones
    outside buildings;
  - everything else (flat roofs, the clock tower) is copied as is; rebuilt crenels use
    "<grd>__solid".
The wall's outer surface stays exactly on the blockout plane, so the hidden blockout collision
still matches; only trims (a few cm) stand proud.

Walls the original map split into pieces are joined first (merge_wall_runs) when the painting runs
on across the seam or is mirrored there (WF_BACKWARDS), so a window or door painted across the seam
is rebuilt rather than left flat. Every painted opening on a rebuilt building is checked off in
openings.json; any that could not be built is logged as a WARNING with the reason, and --strict
makes that an error.

Per building in zone_<rid>.json:
  "displacement": "runtime" (default; Nanite tessellation in UE) | "baked" (applied to real geometry
                  here, no tessellation in UE) | "none"
  "detail": ["timber", "tiles", "window_boxes"]  (tools/blender/zone_detail.py)

Overrides (the hybrid workflow): art_src/environment/zones/<rid>/<Building>.blend, when present, is
used for that building instead of generating it. Its object SM_Z<rid>_<Building> is exported as is
(custom property "displacement" on the object, default "none"). Start one from the generated mesh:

    blender -b --factory-startup -P tools/blender/build_zone_art.py -- --rid 300 --seed-override Inn

then edit it in Blender; re-running the generator never touches it (--force re-seeds).

Output, build/environment/zone_<rid>/ (git-ignored, regenerate any time):
  SM_Z<rid>_<Building>.glb   art mesh in zone space (glTF metres), material slots = texture ids,
                             "<grd>__glass" for stained glass, "<grd>__panel" for door leaves;
                             cut into a GRID_M grid, vertex colour R = displacement mask
  manifest.json              what build_world.py imports
  openings.json              every painted window/door on the buildings: built or not, where, why
  zone_<rid>_art.blend       the art over the remaining blockout, for inspection
  preview_<Building>.png     with --preview: quick textured render
"""
import json
import math
import os
import re
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector, geometry

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "environment"))
import blockout  # noqa: E402
import facades  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zone_detail  # noqa: E402
import zone_grime  # noqa: E402

M_PER_SQUARE = 2.2
GRID_M = 0.25  # art meshes are cut into this grid so displacement has interior vertices to move
# full range of the displacement, as the UE materials use it (materials.json "displacement_range_cm")
DISPLACEMENT_M = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "environment",
                                             "materials.json"), encoding="utf-8")).get("displacement_range_cm", 5.0) / 100.0
OVERRIDES = os.path.join(REPO, "art_src", "environment", "zones")
DETAIL = set()  # optional detail for the building being built (zone_<rid>.json "detail")
UP = Vector((0.0, 1.0, 0.0))  # glTF space: x east, y up, z south
EPS = 1e-4


def log(msg):
    print("[build_zone_art] " + msg, flush=True)


# ------------------------------------------------------------------------------------- inputs

def texture_catalog():
    path = os.path.join(REPO, "build", "textures", "catalog.json")
    return json.load(open(path, encoding="utf-8"))["textures"]


class Poly:
    """One roo2gltf polygon (a wall quad, a floor polygon...), glTF space."""

    def __init__(self, material, pts, uvs, normal):
        self.material = material
        self.pts = [Vector(p) for p in pts]
        self.uvs = [Vector(uv) for uv in uvs]
        self.normal = Vector(normal)

    @property
    def vertical(self):
        return abs(self.normal.y) < 0.01


def polys_of(prim, tri_ids):
    """roo2gltf writes each polygon as a fan (base, base+i, base+i+1); regroup the selected triangles."""
    fans = {}
    for t in sorted(tri_ids):
        i0, i1, i2 = prim.triangles[t]
        fans.setdefault(i0, set()).update((i0, i1, i2))
    out = []
    for base, idx in fans.items():
        order = sorted(idx)
        out.append(Poly(prim.material, [prim.positions[i] for i in order], [prim.uvs[i] for i in order],
                        prim.normals[base]))
    return out


class Floors:
    """Floor height lookup from the blockout's up-facing horizontal triangles around a region."""

    def __init__(self, prims, region, margin=3.0):
        x0, z0, x1, z1 = region[0] - margin, region[1] - margin, region[2] + margin, region[3] + margin
        self.tris = []
        for prim in prims.values():
            for t, (a, b, c) in enumerate(prim.triangles):
                pa, pb, pc = prim.positions[a], prim.positions[b], prim.positions[c]
                if prim.normals[a][1] < 0.5 or abs(pa[1] - pb[1]) > 1e-3 or abs(pa[1] - pc[1]) > 1e-3:
                    continue
                if max(pa[0], pb[0], pc[0]) < x0 or min(pa[0], pb[0], pc[0]) > x1:
                    continue
                if max(pa[2], pb[2], pc[2]) < z0 or min(pa[2], pb[2], pc[2]) > z1:
                    continue
                self.tris.append(((pa[0], pa[2]), (pb[0], pb[2]), (pc[0], pc[2]), pa[1]))

    def at(self, x, z, cap):
        best = None
        p = Vector((x, z))
        for a, b, c, y in self.tris:
            if y <= cap + 0.01 and geometry.intersect_point_tri_2d(p, Vector(a), Vector(b), Vector(c)):
                best = y if best is None else max(best, y)
        return best


def timber_only(poly):
    """A timber texture without a facades.json entry, on a building with timber detail."""
    return "timber" in DETAIL and zone_detail.beam_mask(poly.material) is not None


def poly_key(poly):
    """Same key for a polygon and its reversed (back-side) copy."""
    return frozenset((round(p.x, 2), round(p.y, 2), round(p.z, 2)) for p in poly.pts)


def _wall_piece(poly, r):
    """A wall quad with vertical ends as (s0, s1, (t_lo, t_hi) at s0, (t_lo, t_hi) at s1) in the plane's
    (s, t) coordinates (s = r . (x, 0, z), t = height), or None for any other shape."""
    if len(poly.pts) != 4:
        return None
    st = [(r.dot(Vector((p.x, 0.0, p.z))), p.y) for p in poly.pts]
    s0, s1 = min(s for s, _ in st), max(s for s, _ in st)
    left = sorted(t for s, t in st if abs(s - s0) < 0.01)
    right = sorted(t for s, t in st if abs(s - s1) < 0.01)
    if len(left) != 2 or len(right) != 2 or s1 - s0 < 0.01:
        return None
    return s0, s1, tuple(left), tuple(right)


def _uv_map(poly, r):
    """Affine (s, t) -> uv of a wall polygon, from the three corners spanning the largest triangle."""
    st = [Vector((r.dot(Vector((p.x, 0.0, p.z))), p.y)) for p in poly.pts]
    best, tri = -1.0, (0, 1, 2)
    k = len(st)
    for i in range(k):
        for j in range(i + 1, k):
            for m in range(j + 1, k):
                area = abs((st[j] - st[i]).cross(st[m] - st[i]))
                if area > best:
                    best, tri = area, (i, j, m)
    Mi = Matrix([[st[i].x, st[i].y, 1.0] for i in tri]).inverted()
    cu = Mi @ Vector([poly.uvs[i].x for i in tri])
    cv = Mi @ Vector([poly.uvs[i].y for i in tri])
    return lambda s, t: Vector((cu[0] * s + cu[1] * t + cu[2], cv[0] * s + cv[1] * t + cv[2]))


def _same_picture(uv, poly, r, mirror_at=None):
    """True when `poly`'s UVs are uv(s, t), or uv mirrored about s = mirror_at, up to whole repeats."""
    d = []
    for p, q in zip(poly.pts, poly.uvs):
        s = r.dot(Vector((p.x, 0.0, p.z)))
        d.append(uv(2 * mirror_at - s if mirror_at is not None else s, p.y) - q)
    return all(abs(v.x - round(v.x)) < 1e-3 and abs(v.y - round(v.y)) < 1e-3 for v in d) \
        and len({(round(v.x), round(v.y)) for v in d}) == 1


def merge_wall_runs(polys, mergeable, mirrorable):
    """Join neighbouring pieces of the same wall into one polygon. The original maps often split a
    wall into segments while its painting runs on across the seam, so a painted window or door can
    straddle two pieces and fit in neither (build_facade_wall then leaves it flat). Pieces join when
    they share material and plane, have vertical ends, and the end of one is the start of the next.
    The texture must either continue across the seam (up to whole repeats), or, for walls that
    `mirrorable` allows, be mirrored there (the original client's WF_BACKWARDS: symmetric facades
    whose seam runs through the middle of a window). A joined wall keeps one texture mapping per
    stretch ("maps", see Wall) and its original pieces ("parts", to copy it flat).
    -> (polys, continued seams, mirrored seams)"""
    groups, out = {}, []
    for poly in polys:
        if not (poly.vertical and mergeable(poly)) or len(poly.pts) != 4:
            out.append(poly)
            continue
        n = Vector((poly.normal.x, 0.0, poly.normal.z)).normalized()
        key = (poly.material, round(n.x, 3), round(n.z, 3), round(n.dot(Vector((poly.pts[0].x, 0.0, poly.pts[0].z))), 2))
        groups.setdefault(key, []).append(poly)
    joins = mirrors = 0
    for (material, nx, nz, _), pieces in groups.items():
        r = (-Vector((nx, 0.0, nz)).normalized()).cross(UP).normalized()
        shaped = [(p, _wall_piece(p, r)) for p in pieces]
        out += [p for p, sh in shaped if sh is None]
        shaped = sorted([(p, sh) for p, sh in shaped if sh], key=lambda x: x[1][0])
        run = None  # {"parts", "bottom", "top" ([(s, t)] left to right), "maps" [[s_lo, s_hi, uv]], "right"}
        for poly, (s0, s1, left, right) in shaped:
            if run and abs(s0 - run["maps"][-1][1]) < 0.01 and all(abs(a - b) < 0.01 for a, b in zip(left, run["right"])):
                last = run["maps"][-1]
                if _same_picture(last[2], poly, r):
                    kind = "continued"
                elif mirrorable(poly) and _same_picture(last[2], poly, r, mirror_at=s0):
                    kind = "mirrored"
                else:
                    kind = None
                if kind:
                    run["parts"].append(poly)
                    run["bottom"].append((s1, right[0]))
                    run["top"].append((s1, right[1]))
                    run["right"] = right
                    if kind == "continued":
                        last[1] = s1
                        joins += 1
                    else:
                        run["maps"].append([s0, s1, _uv_map(poly, r)])
                        mirrors += 1
                    continue
            if run:
                out.append(_joined_wall(material, run, r))
            run = {"parts": [poly], "bottom": [(s0, left[0]), (s1, right[0])], "top": [(s0, left[1]), (s1, right[1])],
                   "maps": [[s0, s1, _uv_map(poly, r)]], "right": right}
        if run:
            out.append(_joined_wall(material, run, r))
    return out, joins, mirrors


def _collinear(a, b, c):
    return abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) < 1e-4


def _joined_wall(material, run, r):
    """The polygon of a run of wall pieces (the piece itself when it is alone), wound like its first piece."""
    like = run["parts"][0]
    if len(run["parts"]) == 1:
        return like

    def simplify(chain):
        keep = [chain[0]]
        for i in range(1, len(chain) - 1):
            if not _collinear(keep[-1], chain[i], chain[i + 1]):
                keep.append(chain[i])
        return keep + [chain[-1]]

    outline = simplify(run["bottom"]) + simplify(run["top"])[::-1]
    base = Vector((like.pts[0].x, 0.0, like.pts[0].z))
    foot = base - r * r.dot(base)  # the plane's point at s = 0, height 0
    pts = [foot + r * s + UP * t for s, t in outline]
    n = Vector((like.normal.x, 0.0, like.normal.z))
    if (pts[1] - pts[0]).cross(pts[2] - pts[0]).dot(n) * (like.pts[1] - like.pts[0]).cross(like.pts[2] - like.pts[0]).dot(n) < 0:
        outline, pts = outline[::-1], pts[::-1]
    maps = [(lo, hi, f) for lo, hi, f in run["maps"]]

    def uv(s, t):
        return next((f for lo, hi, f in maps if s <= hi + 1e-6), maps[-1][2])(s, t)
    poly = Poly(material, pts, [uv(s, t) for s, t in outline], like.normal)
    poly.parts = run["parts"]
    poly.maps = maps if len(maps) > 1 else None
    return poly


def outward(poly, floors):
    """True when the polygon faces the lower floor (the open side of a building wall)."""
    mid = sum(poly.pts, Vector()) / len(poly.pts)
    top = max(p.y for p in poly.pts)
    n = Vector((poly.normal.x, 0.0, poly.normal.z)).normalized()
    out_p, in_p = mid + n * 0.3, mid - n * 0.3
    f_out = floors.at(out_p.x, out_p.z, top)
    f_in = floors.at(in_p.x, in_p.z, top)
    if f_out is None:
        return False
    return f_in is None or f_out < f_in - 0.05


# ------------------------------------------------------------------------------- mesh building

class MeshOut:
    """Triangles with per-corner UVs and a material slot each; faces are flipped to a wanted normal."""

    def __init__(self):
        self.verts, self.uvs, self.mats = [], [], []
        self.slots = []

    def slot(self, name):
        if name not in self.slots:
            self.slots.append(name)
        return self.slots.index(name)

    def tri(self, pts, uvs, mat, want=None):
        a, b, c = pts
        n = (b - a).cross(c - a)
        if n.length < 1e-10:
            return
        if want is not None and n.dot(want) < 0:
            pts, uvs = [a, c, b], [uvs[0], uvs[2], uvs[1]]
        self.verts.append(list(pts))
        self.uvs.append(list(uvs))
        self.mats.append(self.slot(mat))

    def poly(self, pts, uvs, mat, want=None):
        for i in range(1, len(pts) - 1):
            self.tri([pts[0], pts[i], pts[i + 1]], [uvs[0], uvs[i], uvs[i + 1]], mat, want)


class TexMap:
    """Affine (s, t) -> uv over the stretch s_lo..s_hi of a wall."""

    def __init__(self, A, c, s_lo, s_hi):
        self.A, self.c, self.Ai = A, c, A.inverted()
        self.s_lo, self.s_hi = s_lo, s_hi


class Wall:
    """Local frame of a vertical facade polygon: s along the wall (right, seen from outside),
    t = height, d = distance along the outward normal. The blockout UVs are an affine function of
    (s, t), which maps texture pixels to wall positions and back. A wall joined from mirrored pieces
    (merge_wall_runs) has one such map per piece; `use()` picks the one st_of_px() and repeats()
    work in, uv() picks by position, and faces are split at the seams between them."""

    def __init__(self, poly, tex):
        self.poly = poly
        self.n = Vector((poly.normal.x, 0.0, poly.normal.z)).normalized()
        self.r = (-self.n).cross(UP).normalized()
        self.o = Vector((poly.pts[0].x, 0.0, poly.pts[0].z))
        self.st = [Vector((self.r.dot(p - self.o), p.y)) for p in poly.pts]
        self.W, self.H = tex["w"], tex["h"]
        self.s0, self.s1 = min(p.x for p in self.st), max(p.x for p in self.st)
        self.t0, self.t1 = min(p.y for p in self.st), max(p.y for p in self.st)
        maps = getattr(poly, "maps", None)
        if maps:
            ro = self.r.dot(self.o)  # the joined wall's maps are in plane coordinates (s = r . (x, 0, z))
            self.maps = [self._fit(lambda s, t, f=f: f(s + ro, t), lo - ro, hi - ro) for lo, hi, f in maps]
        else:
            self.maps = [self._fit_corners()]
        self.seams = [m.s_hi for m in self.maps[:-1]]
        self.use(self.maps[0])

    def _fit_corners(self):
        # affine (s, t) -> (u, v) from the three corners spanning the largest triangle
        best, tri = -1.0, (0, 1, 2)
        k = len(self.st)
        for i in range(k):
            for j in range(i + 1, k):
                for m in range(j + 1, k):
                    area = abs((self.st[j] - self.st[i]).cross(self.st[m] - self.st[i]))
                    if area > best:
                        best, tri = area, (i, j, m)
        Mi = Matrix([[self.st[i].x, self.st[i].y, 1.0] for i in tri]).inverted()
        cu = Mi @ Vector([self.poly.uvs[i].x for i in tri])
        cv = Mi @ Vector([self.poly.uvs[i].y for i in tri])
        return TexMap(Matrix(((cu[0], cu[1]), (cv[0], cv[1]))), Vector((cu[2], cv[2])), self.s0, self.s1)

    @staticmethod
    def _fit(f, s_lo, s_hi):
        o, a, b = f(0.0, 0.0), f(1.0, 0.0), f(0.0, 1.0)
        return TexMap(Matrix(((a.x - o.x, b.x - o.x), (a.y - o.y, b.y - o.y))), Vector((o.x, o.y)), s_lo, s_hi)

    def use(self, m):
        """Work in map m: st_of_px() and repeats() then refer to its stretch of the wall."""
        self.map = m
        us = [m.A @ Vector((s, t)) + m.c for s in (m.s_lo, m.s_hi) for t in (self.t0, self.t1)]
        self.u_range = (min(u.x for u in us), max(u.x for u in us))
        self.v_range = (min(u.y for u in us), max(u.y for u in us))

    def frames(self):
        """Each texture map in turn (one for most walls)."""
        for m in self.maps:
            self.use(m)
            yield m
        self.use(self.maps[0])

    def map_at(self, s):
        return next((m for m in self.maps if s <= m.s_hi + 1e-6), self.maps[-1])

    def uv(self, st, at=None):
        """UV of a wall point; `at` (an s) picks the map for points on a seam."""
        m = self.map_at(st[0] if at is None else at)
        return m.A @ Vector(st) + m.c

    def st_of_uv(self, uv):
        return self.map.Ai @ (Vector(uv) - self.map.c)

    def st_of_px(self, px, ku, kv):
        return self.st_of_uv((ku + px[0] / self.W, kv + px[1] / self.H))

    def p3(self, s, t, d=0.0):
        return self.o + self.r * s + UP * t + self.n * d

    def repeats(self):
        """(ku, kv) texture repeats overlapping the current map's stretch of the wall."""
        for ku in range(math.floor(self.u_range[0]) - 1, math.ceil(self.u_range[1]) + 1):
            for kv in range(math.floor(self.v_range[0]) - 1, math.ceil(self.v_range[1]) + 1):
                yield ku, kv

    def inside(self, st_pts, clamp_bottom=False, tol=0.01):
        ok_s = all(self.s0 - tol <= p.x <= self.s1 + tol for p in st_pts)
        top_ok = all(p.y <= self.t1 + tol for p in st_pts)
        bottom_ok = all(p.y >= self.t0 - tol for p in st_pts)
        if clamp_bottom:
            return ok_s and top_ok and any(p.y > self.t0 + 0.5 for p in st_pts)
        return ok_s and top_ok and bottom_ok

    def mirrored(self, seam):
        return any(abs(seam - s) < 1e-4 for s in self.seams)

    def painted(self, st_pts, tol=0.02):
        """Is an outline found in the current map really painted there? It is when it stays within
        the map's stretch, or runs over a seam into a mirrored neighbour while centred on the seam
        (the mirror shows the other half of the same opening). Anything else found from this map
        beyond its stretch is the wrong half of a mirrored wall."""
        lo, hi = min(p.x for p in st_pts), max(p.x for p in st_pts)
        mid, m = (lo + hi) / 2, self.map
        if lo < m.s_lo - tol and not (self.mirrored(m.s_lo) and abs(mid - m.s_lo) < 0.05):
            return False
        if hi > m.s_hi + tol and not (self.mirrored(m.s_hi) and abs(mid - m.s_hi) < 0.05):
            return False
        return True

    def split(self, tri):
        """A triangle (three (s, t) Vectors) cut at the seams between maps, as triangles."""
        tris = [tri]
        for seam in self.seams:
            tris = [piece for t in tris for piece in _split_tri(t, seam)]
        return tris


def _split_tri(tri, c):
    """Cut a triangle by the line s = c into triangles on either side (same winding)."""
    if all(p.x <= c + 1e-7 for p in tri) or all(p.x >= c - 1e-7 for p in tri):
        return [tri]
    out = []
    for keep in (lambda x: x <= c, lambda x: x >= c):
        poly = []
        for i in range(3):
            a, b = tri[i], tri[(i + 1) % 3]
            if keep(a.x):
                poly.append(a)
            if (a.x - c) * (b.x - c) < 0:
                f = (c - a.x) / (b.x - a.x)
                poly.append(a + (b - a) * f)
        out += [[poly[0], poly[i], poly[i + 1]] for i in range(1, len(poly) - 1)]
    return out


def tessellate(loops):
    """Triangles (as index triples into the concatenated loops) filling loop 0 minus the others."""
    flat = [Vector((p.x, p.y, 0.0)) for loop in loops for p in loop]
    return flat, geometry.tessellate_polygon([[Vector((p.x, p.y, 0.0)) for p in loop] for loop in loops])


def add_face_st(out, wall, pts_st, d, mat, want, uv_fn=None):
    """Fill a planar polygon (with optional holes: pts_st = [outer, hole, ...]) at offset d."""
    flat, tris = tessellate(pts_st)
    for a, b, c in tris:
        for piece in wall.split([Vector((flat[i].x, flat[i].y)) for i in (a, b, c)]):
            at = sum(p.x for p in piece) / 3  # the map of this side of any seam
            fn = uv_fn or (lambda p: wall.uv(p, at))
            out.tri([wall.p3(p.x, p.y, d) for p in piece], [fn(p) for p in piece], mat, want)


def add_strip(out, wall, chain, d0, d1, mat, uv_trim, centre, facing="in", closed=False):
    """Extrude a chain of (s, t) points between offsets d0 and d1 (reveals, trim sides). Faces look
    towards `centre` (an (s, t) point) with facing="in" (reveals), away from it with "out"."""
    run = 0.0
    n = len(chain)
    for i in range(n if closed else n - 1):
        a, b = chain[i], chain[(i + 1) % n]
        seg = (b - a).length
        if seg < 1e-5:
            continue
        edge2 = (b - a) / seg
        perp = Vector((-edge2.y, edge2.x))
        if (perp.dot(centre - (a + b) / 2) > 0) != (facing == "in"):
            perp = -perp
        want = wall.r * perp.x + UP * perp.y
        pts = [wall.p3(a.x, a.y, d0), wall.p3(b.x, b.y, d0), wall.p3(b.x, b.y, d1), wall.p3(a.x, a.y, d1)]
        uvs = [uv_trim(run, d0), uv_trim(run + seg, d0), uv_trim(run + seg, d1), uv_trim(run, d1)]
        out.poly(pts, uvs, mat, want)
        run += seg


def open_chain(outline):
    """An opening outline without its bottom edge, as a path from the bottom point with the smaller
    s, up and over, to the other bottom point (works whether or not the texture is mirrored)."""
    k = len(outline)
    lowest = sorted(range(k), key=lambda i: outline[i].y)[:2]
    start = min(lowest, key=lambda i: outline[i].x)
    end = max(lowest, key=lambda i: outline[i].x)
    step = 1 if outline[(start + 1) % k].y > outline[start].y + 1e-6 else -1
    path, i = [outline[start]], start
    while i != end:
        i = (i + step) % k
        path.append(outline[i])
    return path


def add_box(out, wall, s0, s1, t0, t1, d0, d1, mat, front_uv=True, shift=0.0, ends=(True, True), mitre=(0.0, 0.0)):
    """Axis-aligned box in wall space. Front (+d) shows the facade texture through the wall's UVs
    (sampled `shift` metres along the wall); the other faces use the same mapping with depth folded
    into s or t. Cut at the seams between texture maps (joined mirrored walls); only the outer ends
    get end faces, and only where `ends` (s0 end, s1 end) asks for them. mitre: how much shorter the
    box is at d0 than at d1 at the s0 / s1 end (a mitred corner; that end gets no end face)."""
    cuts = [s0] + [c for c in wall.seams if s0 + 1e-4 < c < s1 - 1e-4] + [s1]
    for a, b in zip(cuts, cuts[1:]):
        ml, mr = (mitre[0] if a == s0 else 0.0), (mitre[1] if b == s1 else 0.0)
        _box(out, wall, a, b, t0, t1, d0, d1, mat, left=a == s0 and ends[0] and not ml,
             right=b == s1 and ends[1] and not mr, shift=shift, mitre=(ml, mr))


def _box(out, wall, s0, s1, t0, t1, d0, d1, mat, left=True, right=True, shift=0.0, mitre=(0.0, 0.0)):
    at = (s0 + s1) / 2 + shift
    b0, b1 = s0 + mitre[0], s1 - mitre[1]  # the ends at d0

    def uv(st):
        return wall.uv((st[0] + shift, st[1]), at)
    faces = [
        # (corner (s, t, d) list, wanted normal, uv function of (s, t, d))
        ([(s0, t0, d1), (s1, t0, d1), (s1, t1, d1), (s0, t1, d1)], wall.n, lambda s, t, d: uv((s, t))),
        ([(b0, t0, d0), (b0, t1, d0), (b1, t1, d0), (b1, t0, d0)], -wall.n, lambda s, t, d: uv((s, t))),
        ([(b0, t1, d0), (s0, t1, d1), (s1, t1, d1), (b1, t1, d0)], UP, lambda s, t, d: uv((s, t + d - d1))),
        ([(b0, t0, d0), (b1, t0, d0), (s1, t0, d1), (s0, t0, d1)], -UP, lambda s, t, d: uv((s, t - d + d1))),
    ]
    if right:
        faces.append(([(b1, t0, d0), (b1, t1, d0), (s1, t1, d1), (s1, t0, d1)], wall.r, lambda s, t, d: uv((s + d - d1, t))))
    if left:
        faces.append(([(b0, t0, d0), (s0, t0, d1), (s0, t1, d1), (b0, t1, d0)], -wall.r, lambda s, t, d: uv((s - d + d1, t))))
    for corners, want, uvf in faces:
        out.poly([wall.p3(s, t, d) for s, t, d in corners], [uvf(s, t, d) for s, t, d in corners], mat, want)


def trim_uv_fn(trim_tex):
    """UVs for reveal/trim strips on the trim texture: u across the depth, v along the run."""
    tw = trim_tex["squares_w"] * M_PER_SQUARE
    th = trim_tex["squares_h"] * M_PER_SQUARE
    return lambda run, d: Vector((-d / tw, -run / th))


# --------------------------------------------------------------------------------- the features

def opening_st(wall, op, ku, kv, grow_px=0.0):
    return [wall.st_of_px(p, ku, kv) for p in facades.opening_outline(op, grow_px)]


# every painted opening that lands on a rebuilt building's wall, and what became of it (openings.json)
OPENINGS = []


def overlaps(wall, pts, margin=0.02):
    """True if the outline's (s, t) box overlaps the wall's by more than `margin` metres both ways."""
    s0, s1 = min(p.x for p in pts), max(p.x for p in pts)
    t0, t1 = min(p.y for p in pts), max(p.y for p in pts)
    return min(s1, wall.s1) - max(s0, wall.s0) > margin and min(t1, wall.t1) - max(t0, wall.t0) > margin


CUT_OPENINGS = True  # facades.json defaults "cut_openings"; when off, nothing is reported


def note_opening(building, wall, op, i, ku, kv, pts, result, reason=""):
    if not CUT_OPENINGS:
        return
    s0, s1 = min(p.x for p in pts), max(p.x for p in pts)
    t0, t1 = min(p.y for p in pts), max(p.y for p in pts)
    centre = wall.p3((s0 + s1) / 2, (t0 + t1) / 2)
    OPENINGS.append({"building": building, "texture": wall.poly.material, "opening": i, "kind": op.get("kind", "window"),
                     "repeat": [ku, kv], "result": result, "reason": reason,
                     "at_m": [round(centre.x, 2), round(centre.y, 2), round(centre.z, 2)],
                     "facing": [round(wall.n.x, 3), round(wall.n.z, 3)],
                     "opening_st": [round(s0, 2), round(s1, 2), round(t0, 2), round(t1, 2)],
                     "wall_st": [round(wall.s0, 2), round(wall.s1, 2), round(wall.t0, 2), round(wall.t1, 2)]})


def why_outside(wall, pts, door, tol=0.01):
    s0, s1 = min(p.x for p in pts), max(p.x for p in pts)
    t0, t1 = min(p.y for p in pts), max(p.y for p in pts)
    why = []
    if s0 < wall.s0 - tol or s1 > wall.s1 + tol:
        why.append("crosses the wall's side edge (%.2f..%.2f m vs wall %.2f..%.2f m)" % (s0, s1, wall.s0, wall.s1))
    if t1 > wall.t1 + tol:
        why.append("pokes above the wall top (%.2f vs %.2f m)" % (t1, wall.t1))
    if not door and t0 < wall.t0 - tol:
        why.append("dips below the wall bottom (%.2f vs %.2f m)" % (t0, wall.t0))
    if door and t1 <= wall.t0 + 0.5:
        why.append("door less than 0.5 m above the wall bottom")
    return "; ".join(why) or "outside the wall"


def wall_continues(wall, polys, rebuilt=lambda q: False, tol=0.02):
    """(s0 end, s1 end): should bands and the plinth stop at that end instead of wrapping round it?
    They stop where another wall carries on in the same plane, and at an outside corner with another
    rebuilt facade wall (`rebuilt(poly)`) whose bands wrap round it instead (the wall with the
    smaller normal wraps), so two never overlap there."""
    flat_n = Vector((wall.n.x, wall.n.z))
    out = []
    for s in (wall.s0, wall.s1):
        found = False
        for q in polys:
            if q is wall.poly or not q.vertical:
                continue
            qn = Vector((q.normal.x, 0.0, q.normal.z)).normalized()
            ss = [wall.r.dot(Vector((v.x, 0.0, v.z)) - wall.o) for v in q.pts]
            ds = [wall.n.dot(Vector((v.x, 0.0, v.z)) - wall.o) for v in q.pts]
            touches = any(abs(x - s) < tol and abs(y) < tol for x, y in zip(ss, ds))
            if not touches:
                continue
            if qn.dot(wall.n) > 0.999 and max(abs(x) for x in ds) <= tol:
                beyond = min(ss) < wall.s0 - 0.05 if s == wall.s0 else max(ss) > wall.s1 + 0.05
                if beyond:  # the wall carries on in this plane
                    found = True
                    break
            elif abs(qn.dot(wall.n)) < 0.3 and max(ds) <= tol and min(ds) < -0.05 and rebuilt(q):
                # outside corner with another rebuilt facade: one of the two wraps round it
                if (round(qn.x, 3), round(qn.z, 3)) < (round(flat_n.x, 3), round(flat_n.y, 3)):
                    found = True
                    break
        out.append(found)
    return tuple(out)


def build_facade_wall(out, wall, desc, defaults, catalog, continues=(False, False)):
    """Facade wall with recessed openings, proud surrounds, bands and a plinth. With facades.json
    defaults "cut_openings": false nothing is recessed (depth comes from the height map): windows
    become flush panes in a glossy "<grd>__pane" slot, and doors only interrupt the plinth.
    continues (wall_continues): ends where the wall carries on in the same plane, so bands and the
    plinth stop there rather than wrapping round a corner."""
    mat = wall.poly.material
    trim = desc.get("trim", defaults["trim"])
    trim_uv = trim_uv_fn(catalog[trim])
    holes, notches = [], []  # notches: openings reaching the wall bottom (doors)
    cut = defaults.get("cut_openings", True)
    gaps = []  # doors the plinth stops at

    built = set()  # an opening across a mirrored seam is found from both sides
    for op_i, op in enumerate(desc.get("openings", [])):
        door = op.get("kind") == "door"
        depth = op.get("reveal_depth_m", defaults["reveal_depth_m"])
        proud = op.get("frame_proud_m", defaults["frame_proud_m"])
        fw = op.get("frame_px", 0)
        for m in wall.frames():
            for ku, kv in wall.repeats():
                inner = opening_st(wall, op, ku, kv)
                outer = opening_st(wall, op, ku, kv, fw) if fw else None
                mid = (min(p.x for p in inner) + max(p.x for p in inner)) / 2
                ours = m.s_lo - 0.05 <= mid <= m.s_hi + 0.05  # centred on this map's stretch
                if not wall.inside(outer or inner, clamp_bottom=door):
                    if ours and overlaps(wall, inner):
                        note_opening(BUILDING, wall, op, op_i, ku, kv, inner, "skipped", why_outside(wall, outer or inner, door))
                    continue
                if not wall.painted(outer or inner):
                    if ours:
                        note_opening(BUILDING, wall, op, op_i, ku, kv, inner, "skipped",
                                     "straddles a seam where the texture does not continue or mirror")
                    continue
                key = (op_i, round(min(p.x for p in inner), 2), round(min(p.y for p in inner), 2))
                if key in built:
                    continue
                built.add(key)
                if not cut:
                    if door:
                        gaps.append((inner, outer))
                    else:
                        # flush glazing: the painted window in its own glossy slot, displaced like the wall
                        holes.append(inner)
                        add_face_st(out, wall, [inner], 0.0, mat + "__pane", wall.n)
                    continue
                note_opening(BUILDING, wall, op, op_i, ku, kv, inner, "built")
                if door:
                    for p in inner + (outer or []):
                        p.y = max(p.y, wall.t0)
                    notches.append((inner, outer))
                    # threshold: floor of the recess, so the ground doesn't show a gap under the door
                    left, right = min(p.x for p in inner), max(p.x for p in inner)
                    add_box(out, wall, left, right, wall.t0 - 0.06, wall.t0 + 0.03, -depth, 0.0, trim)
                else:
                    holes.append(inner)
                centre = sum(inner, Vector((0.0, 0.0))) / len(inner)
                # reveal: from the front of the surround back to the panel
                front = proud if fw else 0.0
                if door:
                    add_strip(out, wall, open_chain(inner), front, -depth, trim, trim_uv, centre)
                else:
                    add_strip(out, wall, inner, front, -depth, trim, trim_uv, centre, closed=True)
                # panel at the back: painted glass or door leaf
                add_face_st(out, wall, [inner], -depth, mat + ("__panel" if door else "__glass"), wall.n)
                if fw and op.get("shape") == "circle":
                    # closed ring (rose window)
                    add_face_st(out, wall, [outer, inner], proud, mat, wall.n)
                    add_strip(out, wall, outer, 0.0, proud, trim, trim_uv, centre, facing="out", closed=True)
                elif fw:
                    # U-shaped surround open at the bottom (sill or ground below it)
                    oc, ic = open_chain(outer), open_chain(inner)
                    add_face_st(out, wall, [oc + ic[::-1]], proud, mat, wall.n)
                    add_strip(out, wall, oc, 0.0, proud, trim, trim_uv, centre, facing="out")
                if op.get("sill"):
                    edge = outer or inner
                    left, right = min(p.x for p in edge), max(p.x for p in edge)
                    foot = min(p.y for p in edge)
                    over = 0.06
                    add_box(out, wall, left - over, right + over, foot - 0.09, foot + 0.015, 0.0, 0.08, trim)
                    if "window_boxes" in DETAIL and not door:
                        zone_detail.window_box(out, wall, left - 0.02, right + 0.02, foot - 0.09,
                                               hash((round(left, 2), round(foot, 2), mat)) & 0xFFFF, UP)

    # timber frame as real beams (zone_detail.timber_relief), clear of the openings
    if "timber" in DETAIL:
        outlines = [facades.opening_outline(op, op.get("frame_px", 0) + 1) for op in desc.get("openings", [])]
        zone_detail.timber_relief(out, wall, mat, outlines, add_face_st, UP)

    # the wall face itself, with holes and door notches
    corners = sorted(wall.st, key=lambda p: (p.y, p.x))
    bottom = sorted([p for p in wall.st if p.y < wall.t0 + 0.01], key=lambda p: p.x)
    top = sorted([p for p in wall.st if p.y >= wall.t0 + 0.01], key=lambda p: -p.x)
    if len(bottom) == 2 and len(top) >= 2:
        loop = [bottom[0]]
        for inner, outer in sorted(notches, key=lambda n: min(p.x for p in n[0])):
            loop += open_chain(inner)  # left foot, up, over, down to the right foot
        loop += [bottom[1]] + top
        loop = [p for i, p in enumerate(loop) if i == 0 or (p - loop[i - 1]).length > 1e-5]
    else:
        loop = corners  # unusual (sloped) wall: no notches
    add_face_st(out, wall, [loop] + holes, 0.0, mat, wall.n)

    # piers (pilasters): full-height proud strips
    for pier in desc.get("piers", []):
        proud = pier.get("proud_m", defaults["pier_proud_m"])
        for m in wall.frames():
            for ku, kv in wall.repeats():
                a = wall.st_of_px((pier["x"][0], 0), ku, kv).x
                b = wall.st_of_px((pier["x"][1], 0), ku, kv).x
                a, b = max(min(a, b), m.s_lo), min(max(a, b), m.s_hi)
                if b - a > 0.05 and kv == math.floor(wall.v_range[0]):
                    add_box(out, wall, a, b, wall.t0, wall.t1, 0.0, proud, mat)

    # bands (string courses) across the whole wall, extended past the ends to close corners
    for band in desc.get("bands", []):
        band_proud = band.get("proud_m", desc.get("band_proud_m", defaults["band_proud_m"]))
        done = set()
        for ku, kv in (rep_ for _ in wall.frames() for rep_ in list(wall.repeats())):
            if kv in done:
                continue
            ta = wall.st_of_px((0, band["y"][0]), ku, kv).y
            tb = wall.st_of_px((0, band["y"][1]), ku, kv).y
            lo, hi = min(ta, tb), max(ta, tb)
            if lo < wall.t0 - 0.01 or hi > wall.t1 + 0.01:
                continue
            done.add(kv)
            add_box(out, wall, wall.s0 - (0.0 if continues[0] else band_proud),
                    wall.s1 + (0.0 if continues[1] else band_proud), lo, hi, 0.0, band_proud, mat)

    # plinth, interrupted by doors
    if desc.get("plinth"):
        pl = defaults["plinth"]
        spans = [(wall.s0 - (0.0 if continues[0] else pl["proud_m"]), wall.s1 + (0.0 if continues[1] else pl["proud_m"]))]
        for inner, outer in notches + gaps:
            edge = outer or inner
            a, b = min(p.x for p in edge), max(p.x for p in edge)
            spans = [piece for s0, s1 in spans for piece in ((s0, min(s1, a)), (max(s0, b), s1)) if piece[1] - piece[0] > 0.05]
        for s0, s1 in spans:
            add_box(out, wall, s0, s1, wall.t0, wall.t0 + pl["height_m"], 0.0, pl["proud_m"], mat)


PARAPET_CORNER_M = 0.25  # solid merlon width kept at each end of a parapet (besides its thickness)


def parapet_thickness(desc, defaults):
    return desc.get("parapet_thickness_m", defaults["parapet_thickness_m"])


def parapet_corner_trims(parapets, others=(), tol=0.03):
    """At an outside corner two parapets overlap in a T x T column, and the end face of one lies on
    the outer face of the other: they z-fight. -> {index: [trim at s0, trim at s1]}: metres to take
    off a parapet's end so it stops at the other's inner face (that end then gets no end face; the
    other parapet's end face, which carries its texture round the corner, closes it).

    parapets: [(Wall, T)]. At a corner where both parapets end, the one whose height range lies
    inside the other's is trimmed (the lower index when they match); where one ends against the
    middle of the other, that one is trimmed if the other covers its height.

    others: the building's other vertical polygons. A parapet end whose end face would lie on one of
    them facing the same way (a parapet running into a tower) gets no end face either (trim 0 and
    the end listed in the second result).

    Oblique outside corners (walls meeting at well under 90 degrees of turn) are mitred instead:
    -> mitres {index: [inner-face shortening at s0, at s1]}. Returns (trims, capless, mitres)."""
    def flat(v):
        return Vector((v.x, v.z))

    ends = []  # (index, end 0/1, corner point, unit direction into the parapet)
    for i, (w, _) in enumerate(parapets):
        a, b = flat(w.p3(w.s0, 0.0)), flat(w.p3(w.s1, 0.0))
        if (b - a).length > 1e-3:
            ends += [(i, 0, a, (b - a).normalized()), (i, 1, b, (a - b).normalized())]
    trims, mitres = {}, {}
    for i, e, p, u in ends:
        wi, ti = parapets[i]
        for j, (wj, tj) in enumerate(parapets):
            if j == i:
                continue
            a, b = flat(wj.p3(wj.s0, 0.0)), flat(wj.p3(wj.s1, 0.0))
            ab = b - a
            k = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
            if (a + ab * k - p).length > tol:
                continue
            if 0.05 < u.dot(-flat(wj.n)) < 0.7:
                # oblique outside corner: where both end here at the same height, mitre both (each
                # parapet's inner face stops where the two inner faces cross)
                uj = next((u2 for j2, _, p2, u2 in ends if j2 == j and (p2 - p).length <= tol), None)
                if uj is not None and abs(wi.t0 - wj.t0) < 0.02 and abs(wi.t1 - wj.t1) < 0.02:
                    a1, a2 = p - flat(wi.n) * ti, p - flat(wj.n) * tj
                    den = u.x * uj.y - u.y * uj.x
                    x = ((a2 - a1).x * uj.y - (a2 - a1).y * uj.x) / den if abs(den) > 1e-6 else 0.0
                    if 0.0 < x < ti + tj:
                        mitres.setdefault(i, [0.0, 0.0])[e] = x
                continue
            if u.dot(-flat(wj.n)) < 0.7:
                continue  # i doesn't end on j's outer face, running into j
            inside_j = wj.t0 - 0.02 <= wi.t0 and wi.t1 <= wj.t1 + 0.02
            inside_i = wi.t0 - 0.02 <= wj.t0 and wj.t1 <= wi.t1 + 0.02
            both_end = any(j2 == j and (p2 - p).length <= tol and u2.dot(-flat(wi.n)) >= 0.7
                           for j2, _, p2, u2 in ends)
            trim_i = (i < j if inside_i else True) if both_end and inside_j else inside_j
            if trim_i:  # else j is trimmed instead (or the heights don't let either be)
                trims.setdefault(i, [0.0, 0.0])[e] = tj
    capless = {}
    for i, e, p, u in ends:
        wi, ti = parapets[i]
        c = p - flat(wi.n) * (ti / 2)  # middle of the end face, which faces -u
        for poly in others:
            n2 = flat(poly.normal)
            if n2.length < 0.5 or n2.normalized().dot(-u) < 0.99:
                continue
            n2 = n2.normalized()
            if abs(n2.dot(c - flat(poly.pts[0]))) > 0.02:
                continue
            h = Vector((-n2.y, n2.x))
            hs = [h.dot(flat(q)) for q in poly.pts]
            ys = [q.y for q in poly.pts]
            if min(hs) - tol <= h.dot(c) <= max(hs) + tol and min(ys) < wi.t1 - 0.02 and max(ys) > wi.t0 + 0.02:
                capless.setdefault(i, set()).add(e)
                break
    return trims, capless, mitres


def build_parapet(out, wall, desc, defaults, trim=(0.0, 0.0), no_cap=(), mitre=(0.0, 0.0)):
    """Alpha-cut crenellation strip -> solid parapet with merlons, built inward from the wall plane.
    trim: metres taken off the s0 / s1 end where another parapet's corner covers it; no_cap: ends
    (0 = s0, 1 = s1) without an end face; mitre: inner-face shortening at an oblique corner
    (parapet_corner_trims)."""
    cren = desc["crenels"]
    T = parapet_thickness(desc, defaults)
    s_lo, s_hi = wall.s0 + trim[0], wall.s1 - trim[1]
    ends = (trim[0] == 0.0 and 0 not in no_cap, trim[1] == 0.0 and 1 not in no_cap)
    mat = wall.poly.material + "__solid"  # the cut-out original rebuilt as solid stone
    cut_t = None
    gaps = []
    for ku, kv in wall.repeats():
        top = wall.st_of_px((0, 0), ku, kv).y
        bot = wall.st_of_px((0, cren["depth_px"]), ku, kv).y
        if min(top, bot) < wall.t0 - 0.02 or max(top, bot) > wall.t1 + 0.02:
            continue
        cut_t = min(top, bot)
        for x0, x1 in cren["x"]:
            a = wall.st_of_px((x0, 0), ku, kv).x
            b = wall.st_of_px((x1 + 1, 0), ku, kv).x
            gaps.append((min(a, b), max(a, b)))
    if cut_t is None:
        add_box(out, wall, s_lo, s_hi, wall.t0, wall.t1, -T, 0.0, mat, ends=ends, mitre=mitre)
        return
    add_box(out, wall, s_lo, s_hi, wall.t0, cut_t, -T, 0.0, mat, ends=ends, mitre=mitre)
    # solid stone for the parapet's thickness (plus a merlon's worth) at both ends, so two parapets
    # meeting at a corner close into one corner merlon instead of two half-merlons with gaps; where
    # the painting has a crenel there, the end block shows the nearest painted merlon instead
    gaps = sorted((max(a, wall.s0), min(b, wall.s1)) for a, b in gaps if min(b, wall.s1) > max(a, wall.s0))
    end_m = T + PARAPET_CORNER_M
    lo, hi = wall.s0 + end_m, wall.s1 - end_m
    if hi - lo < 0.1:
        lo = hi = (wall.s0 + wall.s1) / 2  # short wall: solid
    for (a, b), (a2, b2), cap, mit in (((wall.s0, lo), (s_lo, lo), (ends[0], True), (mitre[0], 0.0)),
                                       ((hi, wall.s1), (hi, s_hi), (True, ends[1]), (0.0, mitre[1]))):
        if b2 - a2 > 0.01:  # the end block, less any corner trim
            add_box(out, wall, a2, b2, cut_t, wall.t1, -T, 0.0, mat, shift=_merlon_shift(a, b, gaps, wall.s0, wall.s1),
                    ends=cap, mitre=mit)
    s = lo
    for a, b in [(max(a, lo), min(b, hi)) for a, b in gaps if min(b, hi) - max(a, lo) > 0.05] + [(hi, hi)]:
        if a - s > 0.03:
            add_box(out, wall, s, a, cut_t, wall.t1, -T, 0.0, mat)
        s = max(s, b)


def _merlon_shift(a, b, gaps, s0, s1):
    """Texture shift (metres along the wall) for a solid block a..b: 0 when the painting has a merlon
    there, else the offset to the nearest painted merlon wide enough (the widest one otherwise)."""
    if not any(min(b, g1) - max(a, g0) > 0.01 for g0, g1 in gaps):
        return 0.0
    merlons, s = [], s0
    for g0, g1 in gaps:
        if g0 - s > 0.01:
            merlons.append((s, g0))
        s = max(s, g1)
    if s1 - s > 0.01:
        merlons.append((s, s1))
    if not merlons:
        return 0.0
    wide = [m for m in merlons if m[1] - m[0] >= (b - a) - 1e-3] or [max(merlons, key=lambda m: m[1] - m[0])]
    m0, m1 = min(wide, key=lambda m: min(abs(m[0] - a), abs(m[1] - b)))
    return m0 - a if abs(m0 - a) <= abs(m1 - b) else m1 - b


def roof_neighbour(a, b, poly, roofs_here, tol=0.05):
    """The other roof polygon of the building that shares the edge a-b of `poly` (None: an outer
    edge). Roofs are often split into several pieces, coplanar or meeting at hips and valleys."""
    e = b - a
    if e.length < 1e-6:
        return None
    e = e.normalized()
    mid = (a + b) / 2
    for q in roofs_here:
        if q is poly:
            continue
        for i in range(len(q.pts)):
            q1, q2 = q.pts[i], q.pts[(i + 1) % len(q.pts)]
            f = q2 - q1
            if f.length < 1e-6 or abs(e.dot(f.normalized())) < 0.99:
                continue
            k = max(0.0, min(1.0, (mid - q1).dot(f) / f.length_squared))
            if (q1 + f * k - mid).length < tol:
                return q
    return None


def build_roof(out, poly, cfg, roofs_here=()):
    """Sloped roof polygon -> slab with thickness, eave overhang on its low edge and verge
    overhangs along the slope; UVs continue the blockout's mapping. Edges shared with another roof
    piece (roofs_here: the building's roof polygons) get no overhang and, between coplanar pieces,
    no side face, so the pieces join into one slab instead of overlapping (z-fighting)."""
    n = poly.normal.normalized()
    down = (-UP) - n * (-UP).dot(n)
    if down.length < 1e-4:
        copy_poly(out, poly)
        return
    d = down.normalized()
    r = n.cross(d).normalized()
    c = sum(poly.pts, Vector()) / len(poly.pts)
    ab = [Vector(((p - c).dot(r), (p - c).dot(d))) for p in poly.pts]
    k = len(ab)
    # per edge: the neighbouring roof piece, the outward direction (in the roof plane) and how far
    # that edge moves out: eave on edges facing down the slope, none at the ridge, verge otherwise
    shared, outs, dist = [], [], []
    for i in range(k):
        a, b = ab[i], ab[(i + 1) % k]
        o = Vector((b.y - a.y, -(b.x - a.x)))
        o = o.normalized() if o.length > 1e-9 else Vector((0.0, 0.0))
        if o.dot((a + b) / 2) < 0:
            o = -o
        q = roof_neighbour(poly.pts[i], poly.pts[(i + 1) % k], poly, roofs_here)
        shared.append(q)
        outs.append(o)
        dist.append(0.0 if q is not None or o.y < -0.7 else cfg["eave_m"] if o.y > 0.7 else cfg["verge_m"])
    grown = []
    for i in range(k):
        j = (i - 1) % k  # the edges before and after vertex i
        o1, o2, d1, d2 = outs[j], outs[i], dist[j], dist[i]
        den = o1.x * o2.y - o1.y * o2.x
        if abs(den) < 1e-4:  # collinear edges
            x = o2 * max(d1, d2)
        else:  # the point at distance d1 from edge j's line and d2 from edge i's
            x = Vector(((d1 * o2.y - d2 * o1.y) / den, (o1.x * d2 - o2.x * d1) / den))
        grown.append(poly.pts[i] + r * x.x + d * x.y)
    ab = [(v.x, v.y) for v in ab]
    # affine (a, b) -> uv from the original corners
    best, tri = -1.0, (0, 1, 2)
    k = len(ab)
    for i in range(k):
        for j in range(i + 1, k):
            for m in range(j + 1, k):
                area = abs((ab[j][0] - ab[i][0]) * (ab[m][1] - ab[i][1]) - (ab[m][0] - ab[i][0]) * (ab[j][1] - ab[i][1]))
                if area > best:
                    best, tri = area, (i, j, m)
    M = Matrix([[ab[i][0], ab[i][1], 1.0] for i in tri]).inverted()
    cu = M @ Vector([poly.uvs[i].x for i in tri])
    cv = M @ Vector([poly.uvs[i].y for i in tri])

    def uv(p):
        a, b = (p - c).dot(r), (p - c).dot(d)
        return Vector((cu[0] * a + cu[1] * b + cu[2], cv[0] * a + cv[1] * b + cv[2]))

    T = cfg["thickness_m"]
    top = [q + n * T for q in grown]
    out.poly(top, [uv(q) for q in grown], poly.material, n)
    if "tiles" in DETAIL:
        zone_detail.roof_tiles(out, grown, n, r, d, c, uv, T, poly.material)
    out.poly(grown[::-1], [uv(q) for q in grown[::-1]], poly.material, -n)
    for i in range(len(grown)):
        q = shared[i]
        if q is not None and q.normal.normalized().dot(n) > 0.999:
            continue  # inside one slab
        a, b = grown[i], grown[(i + 1) % len(grown)]
        edge = b - a
        side = edge.cross(n).normalized()
        if side.dot(((a + b) / 2) - c) < 0:
            side = -side
        out.poly([a, b, b + n * T, a + n * T], [uv(a), uv(b), uv(b) + Vector((0, 0.04)), uv(a) + Vector((0, 0.04))],
                 poly.material, side)


def build_water(out, water_out, polys, entry):
    """Water floor polygons -> a sunken bed (the entry's bed_texture), skirts down to it from the
    original floor height, and a flat water surface (slot "water") a little below the rim."""
    bed_tex = entry.get("bed_texture", "grd09629")
    depth = entry.get("bed_depth_m", 0.9)
    level = entry.get("surface_m", 0.12)
    # boundary edges: used by one polygon only
    count = {}
    for poly in polys:
        k = len(poly.pts)
        for i in range(k):
            a, b = poly.pts[i], poly.pts[(i + 1) % k]
            key = tuple(sorted(((round(a.x, 3), round(a.z, 3)), (round(b.x, 3), round(b.z, 3)))))
            count[key] = count.get(key, 0) + 1
    edges = [key for key, n in count.items() if n == 1]

    def edge_dist(x, z):
        best = 1e9
        for (ax, az), (bx, bz) in edges:
            dx, dz = bx - ax, bz - az
            l2 = dx * dx + dz * dz
            t = 0.0 if l2 < 1e-9 else max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / l2))
            best = min(best, math.hypot(ax + dx * t - x, az + dz * t - z))
        return best

    def bed_y(p):
        # shelving bed: shallow at the rim, full depth ~2.5 m in
        return p.y - min(depth, 0.2 + depth * edge_dist(p.x, p.z) / 2.5)

    for poly in polys:
        # subdivide each floor polygon into a fan of small triangles so the bed can shelve
        c = sum(poly.pts, Vector()) / len(poly.pts)
        k = len(poly.pts)
        for i in range(k):
            a, b = poly.pts[i], poly.pts[(i + 1) % k]
            steps = max(1, int(max((a - c).length, (b - c).length) / 0.75))
            for si in range(steps):
                for sj in range(steps - si):
                    def at(u, v):
                        return c + (a - c) * (u / steps) + (b - c) * (v / steps)
                    tris = [(at(si, sj), at(si + 1, sj), at(si, sj + 1))]
                    if sj + si + 1 < steps:
                        tris.append((at(si + 1, sj), at(si + 1, sj + 1), at(si, sj + 1)))
                    for t in tris:
                        pts = [Vector((p.x, bed_y(p), p.z)) for p in t]
                        out.tri(pts, [Vector((p.x / M_PER_SQUARE, p.z / M_PER_SQUARE)) for p in pts], bed_tex, UP)
        water_out.poly([Vector((p.x, p.y + level, p.z)) for p in poly.pts],
                       [Vector((p.x / M_PER_SQUARE, p.z / M_PER_SQUARE)) for p in poly.pts], "water", UP)
    y0 = polys[0].pts[0].y
    for (ax, az), (bx, bz) in edges:
        a_top, b_top = Vector((ax, y0, az)), Vector((bx, y0, bz))
        a_bot, b_bot = Vector((ax, bed_y(a_top), az)), Vector((bx, bed_y(b_top), bz))
        mid = (a_top + b_top) / 2
        inward = Vector((-(bz - az), 0.0, bx - ax)).normalized()
        # face the skirt towards the water: probe which side has bed below the rim
        probe = mid + inward * 0.3
        if not any(geometry.intersect_point_tri_2d(
                Vector((probe.x, probe.z)), Vector((q.pts[0].x, q.pts[0].z)), Vector((q.pts[i].x, q.pts[i].z)),
                Vector((q.pts[i + 1].x, q.pts[i + 1].z))) for q in polys for i in range(1, len(q.pts) - 1)):
            inward = -inward
        length = (b_top - a_top).length
        out.poly([a_top, b_top, b_bot, a_bot],
                 [Vector((0, 0)), Vector((length / M_PER_SQUARE, 0)), Vector((length / M_PER_SQUARE, 0.4)), Vector((0, 0.4))],
                 bed_tex, inward)


def note_unbuilt_wall(poly, desc, catalog, result, reason):
    """A described wall that is copied flat: its painted openings stay painted."""
    if not desc or not desc.get("openings"):
        return
    wall = Wall(poly, catalog[poly.material])
    for i, op in enumerate(desc["openings"]):
        for m in wall.frames():
            for ku, kv in wall.repeats():
                inner = opening_st(wall, op, ku, kv)
                if overlaps(wall, inner) and wall.painted(inner):
                    note_opening(BUILDING, wall, op, i, ku, kv, inner, result, reason)


def build_cutout(out, poly, thickness, catalog, done):
    """A vertical cut-out (fence, gate, sign) as a solid; its back-side copy in the blockout is the
    same solid, so each wall is built once. Returns the outlines built (0 for a repeat)."""
    key = poly_key(poly)
    if key in done:
        return 0
    done.add(key)
    return zone_detail.cutout_solid(out, Wall(poly, catalog[poly.material]), poly.material, thickness, add_face_st, UP)


def copy_poly(out, poly):
    for part in getattr(poly, "parts", None) or [poly]:
        out.poly(list(part.pts), list(part.uvs), part.material)


# ---------------------------------------------------------------------------------- blender io

def to_blender(v):
    """glTF space (x east, y up, z south) -> Blender (x east, y north, z up)."""
    return Vector((v.x, -v.z, v.y))


def make_object(name, out, collection, grid=True, bake=None):
    mesh = bpy.data.meshes.new(name)
    verts, faces = [], []
    for tri in out.verts:
        faces.append(tuple(range(len(verts), len(verts) + 3)))
        verts += [to_blender(p) for p in tri]
    mesh.from_pydata(verts, [], faces)
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for poly in mesh.polygons:
        k = poly.index
        poly.material_index = out.mats[k]
        for corner, li in enumerate(poly.loop_indices):
            u, v = out.uvs[k][corner]
            uv_layer.data[li].uv = (u, 1.0 - v)  # glTF v is down, Blender v is up
    for slot in out.slots:
        mesh.materials.append(preview_material(slot))
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0005)
    if grid:
        grid_cut(bm, GRID_M if grid is True else grid)
    mask = displacement_mask(bm)
    if bake:
        moved = zone_detail.bake_displacement(bm, out.slots, bake, DISPLACEMENT_M, mask)
        log("%s: baked displacement into %d vertices" % (name, moved))
    weights = [v[mask] for v in bm.verts]
    bm.to_mesh(mesh)
    bm.free()
    if bake:
        for poly in mesh.polygons:
            poly.use_smooth = True
        mesh.set_sharp_from_angle(angle=math.radians(35))
    else:
        for poly in mesh.polygons:
            poly.use_smooth = False
    attr = mesh.color_attributes.new(name="Mask", type="BYTE_COLOR", domain="POINT")
    for i, w in enumerate(weights):
        attr.data[i].color = (w, w, w, 1.0)
    mesh.color_attributes.active_color = attr
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def grid_cut(bm, size=GRID_M):
    """Cut everything along axis planes every GRID_M, so large flat faces get interior vertices
    (Nanite displacement moves vertices along their normals; the mask below pins the edges)."""
    lo = Vector((min(v.co.x for v in bm.verts), min(v.co.y for v in bm.verts), min(v.co.z for v in bm.verts)))
    hi = Vector((max(v.co.x for v in bm.verts), max(v.co.y for v in bm.verts), max(v.co.z for v in bm.verts)))
    for axis in range(3):
        normal = Vector((0, 0, 0))
        normal[axis] = 1.0
        k = math.floor(lo[axis] / size) + 1
        while k * size < hi[axis]:
            co = Vector((0, 0, 0))
            co[axis] = k * size
            geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
            bmesh.ops.bisect_plane(bm, geom=geom, dist=0.0001, plane_co=co, plane_no=normal)
            k += 1
    bmesh.ops.triangulate(bm, faces=bm.faces[:])


def displacement_mask(bm):
    """Per-vertex float layer: 1 where a vertex lies inside one flat face (free to displace), 0 on
    corners, creases, openings and open edges, so displaced faces stay joined. Written to the mesh
    as vertex colour R (runtime displacement); subdivision interpolates it (baked displacement)."""
    bm.normal_update()
    layer = bm.verts.layers.float.new("dmask")
    for v in bm.verts:
        normals = [f.normal for f in v.link_faces]
        flat = bool(normals) and all(n.dot(normals[0]) > 0.999 for n in normals)
        closed = all(not e.is_boundary for e in v.link_edges)
        v[layer] = 1.0 if (flat and closed) else 0.0
    return layer


def preview_material(slot):
    """Material named after the slot (that name is what UE matches); textured for viewing in Blender."""
    mat = bpy.data.materials.get(slot)
    if mat:
        return mat
    mat = bpy.data.materials.new(slot)
    mat.use_nodes = True
    base = slot.split("__")[0]
    tex = os.path.join(REPO, "build", "textures_placeholder", "T_%s_D.png" % base)
    if not os.path.exists(tex):
        tex = os.path.join(REPO, "build", "textures", base + ".png")
    if os.path.exists(tex):
        nodes = mat.node_tree.nodes
        bsdf = nodes.get("Principled BSDF")
        img = nodes.new("ShaderNodeTexImage")
        img.image = bpy.data.images.load(tex, check_existing=True)
        mat.node_tree.links.new(img.outputs["Color"], bsdf.inputs["Base Color"])
        if slot.endswith("__glass"):
            bsdf.inputs["Roughness"].default_value = 0.15
    return mat


def export_glb(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                              export_materials="EXPORT", export_image_format="NONE", export_yup=True, export_apply=False,
                              export_vertex_color="ACTIVE")


def render_preview(path, obj):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 900
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    centre = sum(corners, Vector()) / 8
    size = max((c - centre).length for c in corners)
    cam_data = bpy.data.cameras.new("preview")
    cam_data.lens = 30
    cam = bpy.data.objects.new("preview", cam_data)
    scene.collection.objects.link(cam)
    # from the north-west, slightly above: the door, the tower and one long side
    eye = centre + Vector((-0.55, 0.9, 0.28)).normalized() * size * 1.7
    cam.location = eye
    cam.rotation_euler = (centre - eye).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


# ------------------------------------------------------------------------------------------ main

# what zone_<rid>.json "rebuild" can list (default: all of them)
REBUILD_FEATURES = ("facades", "plain_facades", "roofs", "parapets", "cutouts")


def plain_facade(desc):
    """A facades.json wall with trims (bands, piers, plinth) but no painted windows or doors: its
    relief is geometry that can't miss the painting (rebuild "plain_facades")."""
    return bool(desc) and not desc.get("openings") and any(k in desc for k in ("bands", "piers", "plinth"))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    rid = int(argv[argv.index("--rid") + 1]) if "--rid" in argv else 300
    preview = "--preview" in argv
    plain = "--plain" in argv
    i = argv.index("--preview") if preview else -1
    preview_only = set(argv[i + 1].split(",")) if preview and i + 1 < len(argv) and not argv[i + 1].startswith("--") else None
    seed = set(argv[argv.index("--seed-override") + 1].split(",")) if "--seed-override" in argv else set()
    force = "--force" in argv
    strengths = {k: v for k, v in json.load(open(os.path.join(REPO, "data", "environment", "materials.json"),
                                                 encoding="utf-8")).get("displacement", {}).items() if not k.startswith("_")}
    override_dir = os.path.join(OVERRIDES, str(rid))

    layout = json.load(open(os.path.join(REPO, "data", "zone_layout.json"), encoding="utf-8"))
    zone = next(z for z in layout["zones"] if z["rid"] == rid)
    config = blockout.zone_art_config(rid)
    if not config:
        raise SystemExit("no data/environment/zone_%d.json" % rid)
    fac = facades.load()
    defaults, described = fac["defaults"], fac["textures"]
    global CUT_OPENINGS
    CUT_OPENINGS = defaults.get("cut_openings", True)
    if not CUT_OPENINGS:
        log("openings are not cut (facades.json defaults.cut_openings is false): their depth comes from the height maps")
    roofs = {k: dict(defaults["roof"], **v) for k, v in fac.get("roofs", {}).items() if not k.startswith("_")}
    catalog = texture_catalog()

    glb = os.path.join(REPO, zone["mesh"])
    prims = blockout.read_glb(glb)
    out_dir = os.path.join(REPO, "build", "environment", "zone_%d" % rid)
    os.makedirs(out_dir, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    art_col = bpy.data.collections.new("Art")
    ref_col = bpy.data.collections.new("Blockout (reference)")
    bpy.context.scene.collection.children.link(art_col)
    bpy.context.scene.collection.children.link(ref_col)

    manifest = {"rid": rid, "meshes": []}
    # zone_<rid>.json "rebuild": which parts of the buildings are rebuilt (REBUILD_FEATURES); the
    # rest is copied as the original geometry
    rebuild = set(config.get("rebuild", REBUILD_FEATURES))
    if rebuild - set(REBUILD_FEATURES):
        raise SystemExit('zone_%d.json "rebuild": unknown %s (known: %s)'
                         % (rid, sorted(rebuild - set(REBUILD_FEATURES)), ", ".join(REBUILD_FEATURES)))
    log("rebuilding: %s; the rest stays the original geometry" % (", ".join(sorted(rebuild)) or "nothing"))
    # no parapets: crenellation strips are cut-outs like the fences
    cutouts = {grd: facades.cutout_thickness(grd, fac, catalog, crenels="parapets" not in rebuild) for grd in catalog}
    if "cutouts" not in rebuild:
        cutouts = {}
    relief = json.load(open(os.path.join(REPO, "data", "environment", "materials.json"), encoding="utf-8")).get("relief", {})
    displace = relief.get("displacement", True) if isinstance(relief, dict) else bool(relief)
    cutouts = {grd: t for grd, t in cutouts.items() if t}
    cut_done = set()
    global DETAIL, BUILDING
    for b, sel in blockout.assign_buildings(prims, config):
        name = "SM_Z%d_%s" % (rid, b["name"])
        BUILDING = b["name"]
        override = os.path.join(override_dir, b["name"] + ".blend")
        if os.path.exists(override) and not (b["name"] in seed and force):
            with bpy.data.libraries.load(override, link=False) as (src, dst):
                dst.objects = [n for n in src.objects if n == name]
            if not dst.objects:
                raise SystemExit("%s has no object %s" % (override, name))
            obj = dst.objects[0]
            art_col.objects.link(obj)
            path = os.path.join(out_dir, name + ".glb")
            export_glb(obj, path)
            mode = obj.get("displacement", "none")
            manifest["meshes"].append({"name": name, "file": os.path.basename(path), "building": b["name"],
                                       "displacement": mode, "override": True})
            log("%s: from override %s (%d triangles, displacement %s)" % (name, override, len(obj.data.polygons), mode))
            continue
        DETAIL = set(b.get("detail", []))
        if not sel:
            log("%s: no blockout faces in its region" % b["name"])
            continue
        if b.get("kind") == "water":
            out, water_out = MeshOut(), MeshOut()
            polys = [poly for name, tris in sorted(sel.items()) for poly in polys_of(prims[name], tris)]
            build_water(out, water_out, polys, b)
            for suffix, mesh_out, grid in (("", out, False), ("_Water", water_out, False)):
                name = "SM_Z%d_%s%s" % (rid, b["name"], suffix)  # noqa: F841
                obj = make_object(name, mesh_out, art_col, grid=grid)
                path = os.path.join(out_dir, name + ".glb")
                export_glb(obj, path)
                manifest["meshes"].append({"name": name, "file": os.path.basename(path), "building": b["name"] + suffix,
                                           "water": suffix == "_Water"})
                log("%s: %d triangles, slots %s" % (name, len(obj.data.polygons), ", ".join(mesh_out.slots)))
            continue
        if b.get("kind") == "cutouts":
            out, n_walls, n_outlines = MeshOut(), 0, 0
            for poly in (poly for mat, tris in sorted(sel.items()) for poly in polys_of(prims[mat], tris)):
                if poly.vertical and poly.material in cutouts and not plain:
                    built = build_cutout(out, poly, cutouts[poly.material], catalog, cut_done)
                    n_walls += 1 if built else 0
                    n_outlines += built
                else:
                    copy_poly(out, poly)
            obj = make_object(name, out, art_col, grid=False)
            obj["displacement"] = "none"
            path = os.path.join(out_dir, name + ".glb")
            export_glb(obj, path)
            manifest["meshes"].append({"name": name, "file": os.path.basename(path), "building": b["name"],
                                       "displacement": "none"})
            log("%s: %d cut-out walls as solids (%d outlines) -> %d triangles, slots %s"
                % (name, n_walls, n_outlines, len(obj.data.polygons), ", ".join(out.slots)))
            continue
        floors = Floors(prims, b["region_m"])
        out = MeshOut()
        counts = {"rebuilt": 0, "parapet": 0, "roof": 0, "copied": 0, "dropped": 0, "cut-outs": 0}
        all_polys = [poly for name, tris in sorted(sel.items()) for poly in polys_of(prims[name], tris)]
        building_polys = list(all_polys)  # for neighbours, whatever happens to each
        rebuilt_facades = {k for k, v in described.items() if "crenels" not in v and "clock" not in v
                           and ("facades" in rebuild or ("plain_facades" in rebuild and plain_facade(v)))}
        if plain:
            for poly in all_polys:
                copy_poly(out, poly)
            all_polys = []  # nothing left to rebuild
        if "facades" not in rebuild:
            # walls stay as they are: only the parts listed in "rebuild" (roofs, parapets, cut-outs,
            # and with "plain_facades" the trims of walls without painted openings)
            facade_like = [p for p in all_polys if p.vertical and (described.get(p.material) or timber_only(p))
                           and "crenels" not in described.get(p.material, {})
                           and not ("plain_facades" in rebuild and plain_facade(described.get(p.material)))]
            for poly in facade_like:
                copy_poly(out, poly)
                counts["copied"] += 1
            all_polys = [p for p in all_polys if p not in facade_like]
        all_polys, joins, mirrors = merge_wall_runs(
            all_polys, lambda p: described.get(p.material) or timber_only(p),
            lambda p: bool(described.get(p.material, {}).get("openings")) and "crenels" not in described.get(p.material, {}))
        if joins or mirrors:
            log("%s: joined %d wall seams where the texture continues, %d where it is mirrored"
                % (name, joins, mirrors))
        # walls whose two copies both fail the outward test (free-standing, or level floors on
        # both sides) are copied as they are rather than lost
        sides = {}
        for poly in all_polys:
            if poly.vertical and (described.get(poly.material) or timber_only(poly)):
                sides.setdefault(poly_key(poly), []).append(outward(poly, floors))
        keep_as_is = {k for k, flags in sides.items() if not any(flags)}
        # parapets meeting at corners: one of each pair stops at the other's inner face
        parapets = [p for p in all_polys if p.vertical and p.material not in cutouts and "parapets" in rebuild
                    and "crenels" in described.get(p.material, {}) and poly_key(p) not in keep_as_is and outward(p, floors)]
        walls_of = {id(p): Wall(p, catalog[p.material]) for p in parapets}
        others = [p for p in all_polys if p.vertical and id(p) not in walls_of]
        roofs_here = [p for p in all_polys if p.material in roofs and 0.1 < p.normal.y < 0.97]
        trims, capless, mitres = parapet_corner_trims(
            [(walls_of[id(p)], parapet_thickness(described[p.material], defaults)) for p in parapets], others)
        trim_of = {id(p): (tuple(trims.get(i, (0.0, 0.0))), capless.get(i, set()), tuple(mitres.get(i, (0.0, 0.0))))
                   for i, p in enumerate(parapets)}
        if trims or capless or mitres:
            log("%s: parapet corners: %d ends trimmed, %d mitred, %d end faces left out against walls"
                % (name, sum(1 for t in trims.values() for x in t if x), sum(1 for t in mitres.values() for x in t if x),
                   sum(len(c) for c in capless.values())))
        for poly in all_polys:
            if poly.vertical and poly.material in cutouts:
                counts["cut-outs"] += 1 if build_cutout(out, poly, cutouts[poly.material], catalog, cut_done) else 0
                continue
            if poly.vertical and (described.get(poly.material) or timber_only(poly)) and poly_key(poly) in keep_as_is:
                note_unbuilt_wall(poly, described.get(poly.material), catalog, "flat",
                                  "wall copied as is: neither side faces open ground (no outward side found)")
                copy_poly(out, poly)
                counts["copied"] += 1
                continue
            if poly.material in roofs and 0.1 < poly.normal.y < 0.97 and "roofs" in rebuild:
                build_roof(out, poly, roofs[poly.material], roofs_here)
                counts["roof"] += 1
                continue
            desc = described.get(poly.material)
            if not desc and poly.vertical and timber_only(poly):
                desc = {"name": poly.material}  # plain timber wall: relief only
            if desc and poly.vertical and ("crenels" not in desc or "parapets" in rebuild):
                if outward(poly, floors):
                    wall = walls_of.get(id(poly)) or Wall(poly, catalog[poly.material])
                    if "crenels" in desc:
                        trim, no_cap, mitre = trim_of.get(id(poly), ((0.0, 0.0), set(), (0.0, 0.0)))
                        build_parapet(out, wall, desc, defaults, trim, no_cap, mitre)
                        counts["parapet"] += 1
                    else:
                        stops = wall_continues(wall, building_polys, lambda q: q.material in rebuilt_facades)
                        build_facade_wall(out, wall, desc, defaults, catalog, stops)
                        counts["rebuilt"] += 1
                else:
                    counts["dropped"] += 1  # the inward copy of a rebuilt wall
                continue
            copy_poly(out, poly)
            counts["copied"] += 1
        mode = b.get("displacement", "runtime") if displace else "none"
        # the grid only serves displacement (interior vertices + the edge mask)
        obj = make_object(name, out, art_col, grid=b.get("grid_m", GRID_M) if mode != "none" else False,
                          bake=strengths if mode == "baked" else None)
        obj["displacement"] = mode
        path = os.path.join(out_dir, name + ".glb")
        export_glb(obj, path)
        manifest["meshes"].append({"name": name, "file": os.path.basename(path), "building": b["name"],
                                   "displacement": mode})
        if b["name"] in seed:
            os.makedirs(override_dir, exist_ok=True)
            obj["displacement"] = "none" if mode == "runtime" else mode
            bpy.data.libraries.write(override, {obj}, fake_user=True)
            log("%s: seeded override %s (edit it in Blender; the generator now uses it)" % (name, override))
        log("%s: %d walls rebuilt, %d parapets, %d roofs, %d cut-outs as solids, %d polygons copied, %d inner faces dropped -> %d triangles"
            % (name, counts["rebuilt"], counts["parapet"], counts["roof"], counts["cut-outs"], counts["copied"], counts["dropped"],
               len(obj.data.polygons)))
        mine = [o for o in OPENINGS if o["building"] == b["name"]]
        missed = [o for o in mine if o["result"] != "built"]
        if mine:
            log("%s: openings %d built, %d NOT built%s" % (name, len(mine) - len(missed), len(missed),
                                                          "".join("\n    %s #%d (%s) at %s: %s" % (o["texture"], o["opening"], o["kind"], o["at_m"], o["reason"])
                                                                  for o in missed)))

    # grime strips over the whole zone (materials.json "grime"; zone_grime.py): mesh decals in UE
    grime_cfg = json.load(open(os.path.join(REPO, "data", "environment", "materials.json"),
                               encoding="utf-8")).get("grime", {})
    if grime_cfg.get("enabled", True):
        pattern = re.compile(grime_cfg.get("exclude") or "$^", re.I)
        no_grime = {grd for grd, info in catalog.items()
                    if info.get("has_transparency") or pattern.search(info.get("name", ""))}
        out = MeshOut()
        n_base, n_eave = zone_grime.build_grime(out, prims, polys_of, grime_cfg, no_grime)
        name = "SM_Z%d_Grime" % rid
        obj = make_object(name, out, art_col, grid=False)
        path = os.path.join(out_dir, name + ".glb")
        export_glb(obj, path)
        manifest["meshes"].append({"name": name, "file": os.path.basename(path), "building": "Grime",
                                   "displacement": "none", "decal": True})
        log("%s: %d wall-foot strips, %d eave strips -> %d triangles" % (name, n_base, n_eave, len(obj.data.polygons)))

    # the rest of the blockout, for context in the .blend and the preview
    render_glb = os.path.join(out_dir, "blockout_render.glb")
    blockout.write_render_blockout(glb, render_glb, blockout.all_building_triangles(prims, config))
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=render_glb)
    for o in set(bpy.data.objects) - before:
        for c in o.users_collection:
            c.objects.unlink(o)
        ref_col.objects.link(o)

    json.dump(manifest, open(os.path.join(out_dir, "manifest.json"), "w"), indent=2)
    missed = [o for o in OPENINGS if o["result"] != "built"]
    json.dump({"built": len(OPENINGS) - len(missed), "not_built": len(missed), "openings": OPENINGS},
              open(os.path.join(out_dir, "openings.json"), "w"), indent=1)
    log("openings: %d built, %d not built (%s)" % (len(OPENINGS) - len(missed), len(missed),
                                                  os.path.join(out_dir, "openings.json")))
    for o in missed:
        log("WARNING: %s %s opening #%d (%s) at %s left flat: %s" % (o["building"], o["texture"], o["opening"],
                                                                     o["kind"], o["at_m"], o["reason"]))
    if missed and "--strict" in argv:
        raise SystemExit("%d painted openings were not built (--strict)" % len(missed))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, "zone_%d_art.blend" % rid))
    if preview:
        ref_col.hide_render = True
        for m in manifest["meshes"]:
            if preview_only and m["building"] not in preview_only:
                continue
            obj = bpy.data.objects[m["name"]]
            render_preview(os.path.join(out_dir, "preview_%s.png" % m["building"]), obj)
    log("wrote %s" % out_dir)


main()
