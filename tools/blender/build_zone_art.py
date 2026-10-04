"""
Zone art pass (docs/adr/0003 pass 3): rebuild the buildings listed in data/environment/zone_<rid>.json
as real geometry, from the roo2gltf blockout and the painted-feature descriptions in
data/environment/facades.json. Runs in headless Blender:

    blender -b --factory-startup -P tools/blender/build_zone_art.py -- --rid 300 [--preview]

For each building, every blockout face in its region is either rebuilt or copied:
  - facade walls (texture described in facades.json): the outward face keeps the original texture
    and UVs, but painted openings are cut and recessed (stained glass / door panel at the back of a
    stone reveal, showing the painted image), painted surrounds and bands stand proud of the wall,
    and a plinth runs along the base;
  - crenellation strips (alpha-cut merlons) become a solid parapet with real merlons;
  - everything else (roofs, the clock tower, pilasters) is copied as is.
The wall's outer surface stays exactly on the blockout plane, so the hidden blockout collision
still matches; only trims (a few cm) stand proud.

Output, build/environment/zone_<rid>/ (git-ignored, regenerate any time):
  SM_Z<rid>_<Building>.glb   art mesh in zone space (glTF metres), material slots = texture ids,
                             "<grd>__glass" for stained glass, "<grd>__panel" for door leaves;
                             cut into a GRID_M grid, vertex colour R = displacement mask
  manifest.json              what build_world.py imports
  zone_<rid>_art.blend       the art over the remaining blockout, for inspection
  preview_<Building>.png     with --preview: quick textured render
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector, geometry

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "environment"))
import blockout  # noqa: E402
import facades  # noqa: E402

M_PER_SQUARE = 2.2
GRID_M = 0.25  # art meshes are cut into this grid so displacement has interior vertices to move
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


class Wall:
    """Local frame of a vertical facade polygon: s along the wall (right, seen from outside),
    t = height, d = distance along the outward normal. The blockout UVs are an affine function of
    (s, t), which maps texture pixels to wall positions and back."""

    def __init__(self, poly, tex):
        self.poly = poly
        self.n = Vector((poly.normal.x, 0.0, poly.normal.z)).normalized()
        self.r = (-self.n).cross(UP).normalized()
        self.o = Vector((poly.pts[0].x, 0.0, poly.pts[0].z))
        self.st = [Vector((self.r.dot(p - self.o), p.y)) for p in poly.pts]
        self.W, self.H = tex["w"], tex["h"]
        # affine (s, t) -> (u, v) from the three corners spanning the largest triangle
        best, tri = -1.0, (0, 1, 2)
        k = len(self.st)
        for i in range(k):
            for j in range(i + 1, k):
                for m in range(j + 1, k):
                    area = abs((self.st[j] - self.st[i]).cross(self.st[m] - self.st[i]))
                    if area > best:
                        best, tri = area, (i, j, m)
        M = Matrix([[self.st[i].x, self.st[i].y, 1.0] for i in tri])
        Mi = M.inverted()
        cu = Mi @ Vector([poly.uvs[i].x for i in tri])
        cv = Mi @ Vector([poly.uvs[i].y for i in tri])
        self.A = Matrix(((cu[0], cu[1]), (cv[0], cv[1])))
        self.c = Vector((cu[2], cv[2]))
        self.Ai = self.A.inverted()
        self.s0, self.s1 = min(p.x for p in self.st), max(p.x for p in self.st)
        self.t0, self.t1 = min(p.y for p in self.st), max(p.y for p in self.st)
        us = [self.uv(p) for p in self.st]
        self.u_range = (min(u.x for u in us), max(u.x for u in us))
        self.v_range = (min(u.y for u in us), max(u.y for u in us))

    def uv(self, st):
        return self.A @ Vector(st) + self.c

    def st_of_uv(self, uv):
        return self.Ai @ (Vector(uv) - self.c)

    def st_of_px(self, px, ku, kv):
        return self.st_of_uv((ku + px[0] / self.W, kv + px[1] / self.H))

    def p3(self, s, t, d=0.0):
        return self.o + self.r * s + UP * t + self.n * d

    def repeats(self):
        """(ku, kv) texture repeats overlapping the wall."""
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


def tessellate(loops):
    """Triangles (as index triples into the concatenated loops) filling loop 0 minus the others."""
    flat = [Vector((p.x, p.y, 0.0)) for loop in loops for p in loop]
    return flat, geometry.tessellate_polygon([[Vector((p.x, p.y, 0.0)) for p in loop] for loop in loops])


def add_face_st(out, wall, pts_st, d, mat, want, uv_fn=None):
    """Fill a planar polygon (with optional holes: pts_st = [outer, hole, ...]) at offset d."""
    flat, tris = tessellate(pts_st)
    uv_fn = uv_fn or (lambda p: wall.uv(p))
    for a, b, c in tris:
        pa, pb, pc = (Vector((flat[i].x, flat[i].y)) for i in (a, b, c))
        out.tri([wall.p3(p.x, p.y, d) for p in (pa, pb, pc)], [uv_fn(p) for p in (pa, pb, pc)], mat, want)


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


def add_box(out, wall, s0, s1, t0, t1, d0, d1, mat, front_uv=True):
    """Axis-aligned box in wall space. Front (+d) shows the facade texture through the wall's UVs;
    the other faces use the same mapping with depth folded into s or t."""
    uv = wall.uv
    faces = [
        # (corner (s, t, d) list, wanted normal, uv function of (s, t, d))
        ([(s0, t0, d1), (s1, t0, d1), (s1, t1, d1), (s0, t1, d1)], wall.n, lambda s, t, d: uv((s, t))),
        ([(s0, t0, d0), (s0, t1, d0), (s1, t1, d0), (s1, t0, d0)], -wall.n, lambda s, t, d: uv((s, t))),
        ([(s0, t1, d0), (s0, t1, d1), (s1, t1, d1), (s1, t1, d0)], UP, lambda s, t, d: uv((s, t + d - d1))),
        ([(s0, t0, d0), (s1, t0, d0), (s1, t0, d1), (s0, t0, d1)], -UP, lambda s, t, d: uv((s, t - d + d1))),
        ([(s1, t0, d0), (s1, t1, d0), (s1, t1, d1), (s1, t0, d1)], wall.r, lambda s, t, d: uv((s + d - d1, t))),
        ([(s0, t0, d0), (s0, t0, d1), (s0, t1, d1), (s0, t1, d0)], -wall.r, lambda s, t, d: uv((s - d + d1, t))),
    ]
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


def build_facade_wall(out, wall, desc, defaults, catalog):
    """Facade wall with recessed openings, proud surrounds, bands and a plinth."""
    mat = wall.poly.material
    trim = desc.get("trim", defaults["trim"])
    trim_uv = trim_uv_fn(catalog[trim])
    holes, notches = [], []  # notches: openings reaching the wall bottom (doors)

    for op in desc.get("openings", []):
        door = op.get("kind") == "door"
        depth = op.get("reveal_depth_m", defaults["reveal_depth_m"])
        proud = op.get("frame_proud_m", defaults["frame_proud_m"])
        fw = op.get("frame_px", 0)
        for ku, kv in wall.repeats():
            inner = opening_st(wall, op, ku, kv)
            outer = opening_st(wall, op, ku, kv, fw) if fw else None
            if not wall.inside(outer or inner, clamp_bottom=door):
                continue
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
            if fw:
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

    # the wall face itself, with holes and door notches
    corners = sorted(wall.st, key=lambda p: (p.y, p.x))
    bottom = sorted([p for p in wall.st if p.y < wall.t0 + 0.01], key=lambda p: p.x)
    top = sorted([p for p in wall.st if p.y >= wall.t0 + 0.01], key=lambda p: -p.x)
    if len(bottom) == 2 and len(top) == 2:
        loop = [bottom[0]]
        for inner, outer in sorted(notches, key=lambda n: min(p.x for p in n[0])):
            loop += open_chain(inner)  # left foot, up, over, down to the right foot
        loop += [bottom[1]] + top
        loop = [p for i, p in enumerate(loop) if i == 0 or (p - loop[i - 1]).length > 1e-5]
    else:
        loop = corners  # unusual (sloped) wall: no notches
    add_face_st(out, wall, [loop] + holes, 0.0, mat, wall.n)

    # bands (string courses) across the whole wall, extended past the ends to close corners
    band_proud = desc.get("band_proud_m", defaults["band_proud_m"])
    for band in desc.get("bands", []):
        done = set()
        for ku, kv in wall.repeats():
            if kv in done:
                continue
            ta = wall.st_of_px((0, band["y"][0]), ku, kv).y
            tb = wall.st_of_px((0, band["y"][1]), ku, kv).y
            lo, hi = min(ta, tb), max(ta, tb)
            if lo < wall.t0 - 0.01 or hi > wall.t1 + 0.01:
                continue
            done.add(kv)
            add_box(out, wall, wall.s0 - band_proud, wall.s1 + band_proud, lo, hi, 0.0, band_proud, mat)

    # plinth, interrupted by doors
    if desc.get("plinth"):
        pl = defaults["plinth"]
        spans = [(wall.s0 - pl["proud_m"], wall.s1 + pl["proud_m"])]
        for inner, outer in notches:
            edge = outer or inner
            a, b = min(p.x for p in edge), max(p.x for p in edge)
            spans = [piece for s0, s1 in spans for piece in ((s0, min(s1, a)), (max(s0, b), s1)) if piece[1] - piece[0] > 0.05]
        for s0, s1 in spans:
            add_box(out, wall, s0, s1, wall.t0, wall.t0 + pl["height_m"], 0.0, pl["proud_m"], mat)


def build_parapet(out, wall, desc, defaults):
    """Alpha-cut crenellation strip -> solid parapet with merlons, built inward from the wall plane."""
    cren = desc["crenels"]
    T = desc.get("parapet_thickness_m", defaults["parapet_thickness_m"])
    mat = wall.poly.material
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
        add_box(out, wall, wall.s0, wall.s1, wall.t0, wall.t1, -T, 0.0, mat)
        return
    add_box(out, wall, wall.s0, wall.s1, wall.t0, cut_t, -T, 0.0, mat)
    s = wall.s0
    for a, b in sorted(gaps) + [(wall.s1, wall.s1)]:
        a = min(max(a, wall.s0), wall.s1)
        b = min(max(b, wall.s0), wall.s1)
        if a - s > 0.03:
            add_box(out, wall, s, a, cut_t, wall.t1, -T, 0.0, mat)
        s = max(s, b)


def copy_poly(out, poly):
    out.poly(list(poly.pts), list(poly.uvs), poly.material)


# ---------------------------------------------------------------------------------- blender io

def to_blender(v):
    """glTF space (x east, y up, z south) -> Blender (x east, y north, z up)."""
    return Vector((v.x, -v.z, v.y))


def make_object(name, out, collection):
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
    grid_cut(bm)
    bm.to_mesh(mesh)
    bm.free()
    for poly in mesh.polygons:
        poly.use_smooth = False
    displacement_mask(mesh)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def grid_cut(bm):
    """Cut everything along axis planes every GRID_M, so large flat faces get interior vertices
    (Nanite displacement moves vertices along their normals; the mask below pins the edges)."""
    lo = Vector((min(v.co.x for v in bm.verts), min(v.co.y for v in bm.verts), min(v.co.z for v in bm.verts)))
    hi = Vector((max(v.co.x for v in bm.verts), max(v.co.y for v in bm.verts), max(v.co.z for v in bm.verts)))
    for axis in range(3):
        normal = Vector((0, 0, 0))
        normal[axis] = 1.0
        k = math.floor(lo[axis] / GRID_M) + 1
        while k * GRID_M < hi[axis]:
            co = Vector((0, 0, 0))
            co[axis] = k * GRID_M
            geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
            bmesh.ops.bisect_plane(bm, geom=geom, dist=0.0001, plane_co=co, plane_no=normal)
            k += 1
    bmesh.ops.triangulate(bm, faces=bm.faces[:])


def displacement_mask(mesh):
    """Vertex colour R = 1 where a vertex lies inside one flat face (free to displace), 0 on
    corners, creases, openings and open edges, so displaced faces stay joined."""
    bm = bmesh.new()
    bm.from_mesh(mesh)
    free = {}
    for v in bm.verts:
        normals = [f.normal for f in v.link_faces]
        flat = all(n.dot(normals[0]) > 0.999 for n in normals)
        closed = all(not e.is_boundary for e in v.link_edges)
        free[v.index] = 1.0 if (flat and closed and normals) else 0.0
    bm.free()
    attr = mesh.color_attributes.new(name="Mask", type="BYTE_COLOR", domain="POINT")
    for i, w in free.items():
        attr.data[i].color = (w, w, w, 1.0)
    mesh.color_attributes.active_color = attr


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

def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    rid = int(argv[argv.index("--rid") + 1]) if "--rid" in argv else 300
    preview = "--preview" in argv

    layout = json.load(open(os.path.join(REPO, "data", "zone_layout.json"), encoding="utf-8"))
    zone = next(z for z in layout["zones"] if z["rid"] == rid)
    config = blockout.zone_art_config(rid)
    if not config:
        raise SystemExit("no data/environment/zone_%d.json" % rid)
    fac = facades.load()
    defaults, described = fac["defaults"], fac["textures"]
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
    for b in config["buildings"]:
        sel = blockout.building_triangles(prims, b)
        floors = Floors(prims, b["region_m"])
        out = MeshOut()
        counts = {"rebuilt": 0, "parapet": 0, "copied": 0, "dropped": 0}
        for name, tris in sorted(sel.items()):
            for poly in polys_of(prims[name], tris):
                desc = described.get(poly.material)
                if desc and poly.vertical:
                    if outward(poly, floors):
                        wall = Wall(poly, catalog[poly.material])
                        if "crenels" in desc:
                            build_parapet(out, wall, desc, defaults)
                            counts["parapet"] += 1
                        else:
                            build_facade_wall(out, wall, desc, defaults, catalog)
                            counts["rebuilt"] += 1
                    else:
                        counts["dropped"] += 1  # the inward copy of a rebuilt wall
                    continue
                copy_poly(out, poly)
                counts["copied"] += 1
        name = "SM_Z%d_%s" % (rid, b["name"])
        obj = make_object(name, out, art_col)
        path = os.path.join(out_dir, name + ".glb")
        export_glb(obj, path)
        manifest["meshes"].append({"name": name, "file": os.path.basename(path), "building": b["name"]})
        log("%s: %d walls rebuilt, %d parapets, %d polygons copied, %d inner faces dropped -> %d triangles, slots %s"
            % (name, counts["rebuilt"], counts["parapet"], counts["copied"], counts["dropped"],
               len(obj.data.polygons), ", ".join(out.slots)))

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
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, "zone_%d_art.blend" % rid))
    if preview:
        ref_col.hide_render = True
        for m in manifest["meshes"]:
            obj = bpy.data.objects[m["name"]]
            render_preview(os.path.join(out_dir, "preview_%s.png" % m["building"]), obj)
    log("wrote %s" % out_dir)


main()
