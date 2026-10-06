"""
Optional detail for zone art (docs/adr/0003), used by build_zone_art.py when a building's entry in
data/environment/zone_<rid>.json asks for it ("detail": [...], "displacement": "baked"):

  timber        timber frames extruded as real beams: the beam mask make_placeholders.py writes for
                timber textures (T_<grd>_M.png) is traced into polygons (with holes for the plaster
                panels) and extruded a few cm, keeping the painted texture on the front
  tiles         roof slabs covered tile by tile (overlapping rows, ridge caps) in the roof texture
  window_boxes  planters with greenery and flowers under windows that have a sill
  (always)      cut-out (alpha) originals - fences, gates, signs - as solids traced from their alpha
                mask (cutout_solid; thickness from facades.json "cutouts")
  bake          (displacement "baked") the runtime Nanite displacement applied to real geometry:
                subdivide, then move each vertex by the same height maps, strengths and edge mask

Runs inside Blender (mathutils, bmesh, numpy).
"""
import math
import os

import bmesh
import bpy
import numpy as np
from mathutils import Vector, geometry

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLACEHOLDERS = os.path.join(REPO, "build", "textures_placeholder")

BEAM_RELIEF_M = 0.035
TILE = {"width": 0.27, "gap": 0.02, "length": 0.40, "pitch": 0.30, "thick": 0.018, "lift": 0.03}


# ------------------------------------------------------------------------------- image helpers

_images = {}


def image_array(path):
    """(h, w) float array of the image's first channel, row 0 at the bottom (Blender order)."""
    if path in _images:
        return _images[path]
    arr = None
    if os.path.exists(path):
        img = bpy.data.images.load(path, check_existing=True)
        w, h = img.size
        px = np.empty(w * h * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        arr = px.reshape(h, w, 4)[:, :, 0].copy()
    _images[path] = arr
    return arr


def beam_mask(grd):
    """bool (h, w) beam mask, row 0 at the TOP (texture pixel order), or None."""
    arr = image_array(os.path.join(PLACEHOLDERS, "T_%s_M.png" % grd))
    return None if arr is None else (arr[::-1] > 0.5)


# ----------------------------------------------------------------------------- mask -> polygons

def trace_loops(mask):
    """Pixel-boundary loops of a bool mask (x right, y down, pixel corners as vertices).
    Returns (loops, signed areas): area < 0 for outer boundaries, > 0 for holes."""
    h, w = mask.shape
    m = np.zeros((h + 2, w + 2), dtype=bool)
    m[1:-1, 1:-1] = mask
    edges = {}

    def add(a, b):
        edges.setdefault(a, []).append(b)

    ys, xs = np.nonzero(m)
    for y, x in zip(ys.tolist(), xs.tolist()):
        px, py = x - 1, y - 1  # back to mask coordinates
        if not m[y - 1, x]:
            add((px + 1, py), (px, py))          # top, walking left
        if not m[y + 1, x]:
            add((px, py + 1), (px + 1, py + 1))  # bottom, walking right
        if not m[y, x - 1]:
            add((px, py), (px, py + 1))          # left, walking down
        if not m[y, x + 1]:
            add((px + 1, py + 1), (px + 1, py))  # right, walking up
    loops = []
    while edges:
        start = next(iter(edges))
        loop, cur = [start], start
        while True:
            nxt = edges[cur].pop()
            if not edges[cur]:
                del edges[cur]
            if nxt == start:
                break
            loop.append(nxt)
            cur = nxt
            if cur not in edges:
                break
        if len(loop) >= 4:
            loops.append(loop)
    areas = [0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(l, l[1:] + l[:1])) for l in loops]
    return loops, areas


def simplify(loop, tol):
    """Douglas-Peucker on a closed loop."""
    if len(loop) < 5:
        return loop
    pts = [Vector((p[0], p[1])) for p in loop]
    # split at the two farthest-apart points
    i0 = 0
    i1 = max(range(len(pts)), key=lambda i: (pts[i] - pts[0]).length)

    def dp(seq):
        if len(seq) < 3:
            return seq
        a, b = seq[0], seq[-1]
        ab = b - a
        best, idx = -1.0, 0
        for i in range(1, len(seq) - 1):
            p = seq[i]
            if ab.length < 1e-9:
                d = (p - a).length
            else:
                d = abs(ab.x * (p.y - a.y) - ab.y * (p.x - a.x)) / ab.length
            if d > best:
                best, idx = d, i
        if best <= tol:
            return [a, b]
        left = dp(seq[: idx + 1])
        return left[:-1] + dp(seq[idx:])

    first = dp(pts[i0: i1 + 1])
    second = dp(pts[i1:] + pts[:1])
    out = first[:-1] + second[:-1]
    return [(p.x, p.y) for p in out]


def point_in_loop(p, loop):
    x, y = p
    inside = False
    n = len(loop)
    for i in range(n):
        x0, y0 = loop[i]
        x1, y1 = loop[(i + 1) % n]
        if (y0 > y) != (y1 > y):
            xi = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if xi > x:
                inside = not inside
    return inside


def mask_polygons(mask, tol=1.3, min_area=6.0):
    """bool mask -> [(outer loop, [hole loops])] in texture pixels."""
    loops, areas = trace_loops(mask)
    outers = [(simplify(l, tol), a) for l, a in zip(loops, areas) if a < -min_area]
    holes = [simplify(l, tol) for l, a in zip(loops, areas) if a > min_area]
    out = [(o, []) for o, _ in outers]
    for hole in holes:
        for o, hs in out:
            if point_in_loop(hole[0], o):
                hs.append(hole)
                break
    return [(o, hs) for o, hs in out if len(o) >= 3]


def clip_convex(loop, clip):
    """Sutherland-Hodgman: polygon `loop` clipped to the convex polygon `clip` (both lists of
    2D Vectors, clip counter-clockwise)."""
    out = list(loop)
    for i in range(len(clip)):
        a, b = clip[i], clip[(i + 1) % len(clip)]
        edge = b - a
        inp, out = out, []
        if not inp:
            break

        def inside(p):
            return edge.x * (p.y - a.y) - edge.y * (p.x - a.x) >= -1e-9

        def cross(p, q):
            d1 = edge.x * (p.y - a.y) - edge.y * (p.x - a.x)
            d2 = edge.x * (q.y - a.y) - edge.y * (q.x - a.x)
            t = d1 / (d1 - d2)
            return p + (q - p) * t

        prev = inp[-1]
        for cur in inp:
            if inside(cur):
                if not inside(prev):
                    out.append(cross(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(cross(prev, cur))
            prev = cur
    return out


def _signed_area(loop):
    return 0.5 * sum(a.x * b.y - b.x * a.y for a, b in zip(loop, loop[1:] + loop[:1]))


# ------------------------------------------------------------------------------ timber relief

def timber_relief(out, wall, mat, openings_px, add_face_st, ups):
    """Extrude the texture's beam mask as real beams on one facade wall."""
    mask = beam_mask(mat)
    if mask is None:
        return 0
    mask = mask.copy()
    h, w = mask.shape
    # no beams over openings (their frames and recesses are geometry already)
    for outline in openings_px:
        xs = [p[0] for p in outline]
        ys = [p[1] for p in outline]
        for y in range(max(0, int(min(ys)) - 1), min(h, int(max(ys)) + 2)):
            for x in range(max(0, int(min(xs)) - 1), min(w, int(max(xs)) + 2)):
                if point_in_loop((x + 0.5, y + 0.5), outline):
                    mask[y, x] = False
    polys = mask_polygons(mask)
    clip = [Vector(p) for p in wall.st]
    if _signed_area(clip) < 0:
        clip = clip[::-1]
    count = 0
    for ku, kv in wall.repeats():
        for outer_px, holes_px in polys:
            outer = clip_convex([wall.st_of_px(p, ku, kv) for p in outer_px], clip)
            if len(outer) < 3 or abs(_signed_area(outer)) < 1e-4:
                continue
            holes = []
            for hp in holes_px:
                hl = clip_convex([wall.st_of_px(p, ku, kv) for p in hp], clip)
                if len(hl) >= 3 and abs(_signed_area(hl)) > 1e-4:
                    holes.append(hl)
            region_loops = [outer] + holes
            add_face_st(out, wall, region_loops, BEAM_RELIEF_M, mat, wall.n)

            def in_region(p):
                if not point_in_loop((p.x, p.y), [(q.x, q.y) for q in outer]):
                    return False
                return not any(point_in_loop((p.x, p.y), [(q.x, q.y) for q in hl]) for hl in holes)

            for loop in region_loops:
                n = len(loop)
                for i in range(n):
                    a, b = loop[i], loop[(i + 1) % n]
                    seg = b - a
                    if seg.length < 1e-4:
                        continue
                    perp = Vector((-seg.y, seg.x)) / seg.length
                    mid = (a + b) / 2
                    if in_region(mid + perp * 0.004):
                        perp = -perp  # face away from the beam
                    want = wall.r * perp.x + ups * perp.y
                    pts = [wall.p3(a.x, a.y, 0.0), wall.p3(b.x, b.y, 0.0),
                           wall.p3(b.x, b.y, BEAM_RELIEF_M), wall.p3(a.x, a.y, BEAM_RELIEF_M)]
                    uvs = [wall.uv(a), wall.uv(b), wall.uv(b), wall.uv(a)]
                    out.poly(pts, uvs, mat, want)
            count += 1
    return count


# --------------------------------------------------------------------------- cut-out solids

TEXTURES = os.path.join(REPO, "build", "textures")


def alpha_mask(grd):
    """bool (h, w) mask of the original's opaque texels, row 0 at the TOP, or None."""
    path = os.path.join(TEXTURES, grd + ".png")
    key = path + "#alpha"
    if key not in _images:
        arr = None
        if os.path.exists(path):
            img = bpy.data.images.load(path, check_existing=True)
            w, h = img.size
            px = np.empty(w * h * 4, dtype=np.float32)
            img.pixels.foreach_get(px)
            arr = px.reshape(h, w, 4)[::-1, :, 3] > 0.5
        _images[key] = arr
    return _images[key]


def cutout_solid(out, wall, grd, thickness, add_face_st, ups, tol=0.75):
    """A cut-out (alpha) wall as a solid: the original's alpha mask traced into outlines (pixel
    steps smoothed by `tol` texels), clipped to the wall, extruded `thickness` metres centred on the
    wall plane. Front and back keep the wall's texture mapping; the outline sides stretch it.
    Faces use the opaque "<grd>__solid" slot. Returns the number of outlines built."""
    mask = alpha_mask(grd)
    if mask is None:
        return 0
    polys = mask_polygons(mask, tol=tol, min_area=1.0)
    clip = [Vector(p) for p in wall.st]
    if _signed_area(clip) < 0:
        clip = clip[::-1]
    mat, half, count = grd + "__solid", thickness / 2.0, 0
    # Triangulate each whole outline first, then clip the triangles to the wall. Clipping the
    # outline and its holes first (as this used to) leaves holes touching the outline wherever the
    # wall ends mid-texture, and tessellate_polygon then drops most of the face: half a gate showed
    # only its edges.
    tris_px = []
    for outer_px, holes_px in polys:
        loops = [outer_px] + holes_px
        flat = [Vector((x, y, 0.0)) for loop in loops for x, y in loop]
        tris = geometry.tessellate_polygon([[Vector((x, y, 0.0)) for x, y in loop] for loop in loops])
        want = abs(_signed_area([Vector(p) for p in outer_px])) - sum(abs(_signed_area([Vector(p) for p in h]))
                                                                      for h in holes_px)
        got = sum(geometry.area_tri(flat[a], flat[b], flat[c]) for a, b, c in tris)
        if abs(got - want) > 0.02 * max(want, 1.0):
            print("[zone_detail] WARNING: %s outline of %.0f px2 triangulated to %.0f px2" % (grd, want, got))
        tris_px.append([[(flat[i].x, flat[i].y) for i in t] for t in tris])

    def side(a, b, solid_left):
        """An outline side between st points a and b, facing out of the solid."""
        seg = b - a
        if seg.length < 1e-5:
            return
        perp = Vector((-seg.y, seg.x)) / seg.length  # left of a->b
        if solid_left:
            perp = -perp
        want = wall.r * perp.x + ups * perp.y
        pts = [wall.p3(a.x, a.y, -half), wall.p3(b.x, b.y, -half), wall.p3(b.x, b.y, half), wall.p3(a.x, a.y, half)]
        at = (a.x + b.x) / 2
        out.poly(pts, [wall.uv(a, at), wall.uv(b, at), wall.uv(b, at), wall.uv(a, at)], mat, want)

    for _ in wall.frames():
        for ku, kv in wall.repeats():
            for (outer_px, holes_px), tris in zip(polys, tris_px):
                outer = [wall.st_of_px(p, ku, kv) for p in outer_px]
                holes = [[wall.st_of_px(p, ku, kv) for p in hp] for hp in holes_px]
                built = False
                for tri in tris:
                    piece = clip_convex([wall.st_of_px(p, ku, kv) for p in tri], clip)
                    # a repeat that only touches the wall's edge (a wall exactly one texture tall:
                    # the next repeat starts on its top edge) clips to pieces with no height; built,
                    # they showed as a thin line of side faces along the top of a fence
                    if (len(piece) >= 3 and abs(_signed_area(piece)) > 1e-7
                            and max(p.y for p in piece) - min(p.y for p in piece) > 0.002
                            and max(p.x for p in piece) - min(p.x for p in piece) > 0.002):
                        add_face_st(out, wall, [piece], half, mat, wall.n)
                        add_face_st(out, wall, [piece], -half, mat, -wall.n)
                        built = True
                if not built:
                    continue
                outer2 = [(q.x, q.y) for q in outer]
                holes2 = [[(q.x, q.y) for q in hl] for hl in holes]

                def solid(p):
                    return point_in_loop((p.x, p.y), outer2) and not any(point_in_loop((p.x, p.y), h) for h in holes2)

                for loop in [outer] + holes:
                    n = len(loop)
                    for i in range(n):
                        a, b = loop[i], loop[(i + 1) % n]
                        part = _clip_segment(a, b, clip)
                        if part:
                            seg = b - a
                            probe = (a + b) / 2 + Vector((-seg.y, seg.x)).normalized() * 0.002
                            side(*part, solid(probe))
                # caps where the wall's own edge cuts through the solid
                for i in range(len(clip)):
                    p, q = clip[i], clip[(i + 1) % len(clip)]
                    ts = [0.0, 1.0]
                    for loop in [outer] + holes:
                        for j in range(len(loop)):
                            x = geometry.intersect_line_line_2d(p, q, loop[j], loop[(j + 1) % len(loop)])
                            if x is not None:
                                ts.append((x - p).length / max((q - p).length, 1e-9))
                    ts.sort()
                    for t0, t1 in zip(ts, ts[1:]):
                        if t1 - t0 > 1e-6 and solid(p.lerp(q, (t0 + t1) / 2)):
                            # clip is counter-clockwise: the solid lies left of p->q
                            side(p.lerp(q, t0), p.lerp(q, t1), True)
                count += 1
    return count


def _clip_segment(a, b, clip):
    """The part of segment a-b inside the convex counter-clockwise polygon `clip` (Cyrus-Beck)."""
    t0, t1, d = 0.0, 1.0, b - a
    for i in range(len(clip)):
        p, q = clip[i], clip[(i + 1) % len(clip)]
        e = q - p
        num = e.x * (a.y - p.y) - e.y * (a.x - p.x)  # >= 0 inside
        den = e.x * d.y - e.y * d.x
        if abs(den) < 1e-12:
            if num < -1e-9:
                return None
            continue
        t = -num / den
        if den > 0:
            t0 = max(t0, t)
        else:
            t1 = min(t1, t)
        if t1 - t0 < 1e-9:
            return None
    return a + d * t0, a + d * t1


# --------------------------------------------------------------------------------- roof tiles

def roof_tiles(out, grown, n, r, d, c, uv, top_offset, mat):
    """Tile rows over a roof slab. grown: slab corner points (glTF space) on the underside;
    n up-ish normal, r along the ridge, d downslope, c centre, uv(p) -> roof UV."""
    ab = [((p - c).dot(r), (p - c).dot(d)) for p in grown]
    poly2 = [Vector(q) for q in ab]
    amin, amax = min(q[0] for q in ab), max(q[0] for q in ab)
    bmin, bmax = min(q[1] for q in ab), max(q[1] for q in ab)
    T = TILE
    base = top_offset + 0.003
    rows = 0
    b = bmin
    row = 0
    while b < bmax - 0.05:
        offset = (T["width"] + T["gap"]) * 0.5 * (row % 2)
        a = amin - offset
        while a < amax:
            ca, cb = a + T["width"] / 2, b + T["length"] / 2
            if point_in_loop((ca, cb), [(q.x, q.y) for q in poly2]):
                a0, a1 = max(a, amin), min(a + T["width"], amax)
                b0, b1 = b, min(b + T["length"], bmax)
                if a1 - a0 > 0.05:
                    _tile(out, c, r, d, n, a0, a1, b0, b1, base, T, uv, mat)
            a += T["width"] + T["gap"]
        b += T["pitch"]
        row += 1
        rows += 1
    # ridge cap along the high edge
    cap0, cap1 = bmin - 0.02, bmin + 0.12
    corners = [(amin, cap0), (amax, cap0), (amax, cap1), (amin, cap1)]
    lift = base + T["lift"] + 0.04
    pts = [c + r * x + d * y + n * lift for x, y in corners]
    out.poly(pts, [uv(p) for p in pts], mat, n)
    for (x0, y0), (x1, y1) in zip(corners, corners[1:] + corners[:1]):
        p0, p1 = c + r * x0 + d * y0, c + r * x1 + d * y1
        quad = [p0 + n * base, p1 + n * base, p1 + n * lift, p0 + n * lift]
        side = (p1 - p0).cross(n).normalized()
        if side.dot((p0 + p1) / 2 - (c + r * (amin + amax) / 2 + d * (cap0 + cap1) / 2)) < 0:
            side = -side
        out.poly(quad, [uv(p) for p in quad], mat, side)
    return rows


def _tile(out, c, r, d, n, a0, a1, b0, b1, base, T, uv, mat):
    """One tile: a thin slab whose lower (downslope) end is lifted, like a lapped tile."""
    def at(a, b, h):
        return c + r * a + d * b + n * h
    lift_hi, lift_lo = base, base + T["lift"]
    top = [at(a0, b0, lift_hi + T["thick"]), at(a1, b0, lift_hi + T["thick"]),
           at(a1, b1, lift_lo + T["thick"]), at(a0, b1, lift_lo + T["thick"])]
    bot = [at(a0, b0, lift_hi), at(a1, b0, lift_hi), at(a1, b1, lift_lo), at(a0, b1, lift_lo)]
    out.poly(top, [uv(p) for p in top], mat, n)
    # front lip (downslope end) and the two sides; the back hides under the row above
    for i0, i1, want in ((3, 2, d), (0, 3, -r), (2, 1, r)):
        q = [bot[i0], bot[i1], top[i1], top[i0]]
        out.poly(q, [uv(p) for p in q], mat, want)


# ------------------------------------------------------------------------------- window boxes

def _blob(out, centre, rx, rz, mat, up):
    """Squashed octahedron (a low-poly leafy clump or flower head)."""
    side_a = Vector((1, 0, 0)) if abs(up.x) < 0.9 else Vector((0, 0, 1))
    e1 = up.cross(side_a).normalized()
    e2 = up.cross(e1).normalized()
    top, bottom = centre + up * rz, centre - up * rz * 0.6
    ring = [centre + e1 * rx, centre + e2 * rx, centre - e1 * rx, centre - e2 * rx]
    for i in range(4):
        a, b = ring[i], ring[(i + 1) % 4]
        for tip in (top, bottom):
            want = ((a + b + tip) / 3 - centre)
            out.tri([a, b, tip], [Vector((0.5, 0.5))] * 3, mat, want)


def window_box(out, wall, s0, s1, sill_t, rng_seed, ups):
    """Planter under a window sill with greenery and a few flowers."""
    top = sill_t - 0.10
    box_h, depth = 0.17, 0.2
    pts = lambda s, t, dd: wall.p3(s, t, dd)  # noqa: E731
    # the planter: front, two ends, top rim (soil hidden by plants)
    faces = [
        ([(s0, top - box_h, depth), (s1, top - box_h, depth), (s1, top, depth), (s0, top, depth)], wall.n),
        ([(s0, top - box_h, 0.0), (s0, top - box_h, depth), (s0, top, depth), (s0, top, 0.0)], -wall.r),
        ([(s1, top - box_h, depth), (s1, top - box_h, 0.0), (s1, top, 0.0), (s1, top, depth)], wall.r),
        ([(s0, top - box_h, 0.0), (s1, top - box_h, 0.0), (s1, top - box_h, depth), (s0, top - box_h, depth)], -ups),
        ([(s0, top - 0.02, 0.0), (s1, top - 0.02, 0.0), (s1, top - 0.02, depth), (s0, top - 0.02, depth)], ups),
    ]
    for corners, want in faces:
        p = [pts(*q) for q in corners]
        uvs = [Vector((q[0] / 0.6, q[1] / 0.6 + q[2])) for q in corners]
        out.poly(p, uvs, "prop_planter", want)
    # plants: a row of leafy clumps, flowers on top (deterministic)
    import random
    rng = random.Random(rng_seed)
    s = s0 + 0.06
    while s < s1 - 0.05:
        centre = pts(s, top + 0.03, depth * 0.5 + rng.uniform(-0.04, 0.04))
        _blob(out, centre, rng.uniform(0.07, 0.1), rng.uniform(0.07, 0.12), "prop_foliage", ups)
        if rng.random() < 0.7:
            fc = centre + ups * rng.uniform(0.07, 0.12) + wall.n * rng.uniform(0.0, 0.06)
            _blob(out, fc, 0.035, 0.03, rng.choice(("prop_flower_red", "prop_flower_yellow")), ups)
        s += rng.uniform(0.09, 0.13)


# --------------------------------------------------------------------------- baked displacement

def bake_displacement(bm, slots, strengths, magnitude_m, mask_layer, cuts=4):
    """Subdivide every edge `cuts` times, then move each vertex along its face normal by
    (height - 0.5) * strength * magnitude * mask, sampling T_<grd>_H.png through the face UVs:
    the same result Nanite tessellation computes at runtime, as real geometry."""
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=cuts, use_grid_fill=True)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.normal_update()
    uv_layer = bm.loops.layers.uv.active
    moves = {}
    for f in bm.faces:
        slot = slots[f.material_index] if f.material_index < len(slots) else ""
        base, _, variant = slot.partition("__")
        strength = 0.0 if variant in ("glass", "panel") else float(strengths.get(base, 0.0))
        if strength <= 0:
            continue
        heights = image_array(os.path.join(PLACEHOLDERS, "T_%s_H.png" % base))
        if heights is None:
            continue
        hh, ww = heights.shape
        for loop in f.loops:
            v = loop.vert
            m = v[mask_layer]
            if m <= 0 or v.index in moves:
                continue
            u, t = loop[uv_layer].uv
            h = heights[int((t % 1.0) * hh) % hh, int((u % 1.0) * ww) % ww]
            moves[v.index] = (f.normal.copy(), (h - 0.5) * strength * magnitude_m * m)
    bm.verts.ensure_lookup_table()
    for i, (nrm, amount) in moves.items():
        bm.verts[i].co += nrm * amount
    return len(moves)
