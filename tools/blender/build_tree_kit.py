"""
Procedural trees for the Kod-placed tree objects (docs/adr/0007 "Trees"). Headless Blender:

    python tools/textures/make_tree_textures.py      # leaf atlas + bark first
    blender -b --factory-startup -P tools/blender/build_tree_kit.py [-- Apple Pear ...]

Writes build/environment/kit/SM_Tree_<Name>_<Variant>.glb: a seeded trunk-and-branch skeleton as
tapered tubes ("tree_bark" slot, T_TreeBark_<Name>) and a crown of leaf-cluster cards ("tree_leaves"
slot, T_TreeLeaves_<Name>, a 2x2 atlas) filling the original sprite's silhouette, measured off the
sprite: clear trunk to crown_base_m, an ellipsoid crown of radii crown_r_m.

What makes it read as a tree rather than a pile of cards:
- Leaf normals come from the crown ellipsoid, not the cards, so the crown shades as one soft
  volume like the painted original. Each card is two-sided in geometry (a back copy with the same
  normals), because a two-sided material would flip those normals on the back faces.
- Cards face outwards with scatter and are folded down the middle, so they don't vanish edge-on.
- Vertex colour: R wind weight (0 at the trunk base, 1 at the crown's rim), G a random value per
  card or branch (colour variation), B ambient occlusion (dark deep in the crown), A 1 on leaves.
Deterministic: the same seed gives the same tree.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "build", "environment", "kit")

# Measured off the original sprites (data/aigen/props/<bgf>.json height_m/width_m; the crown's
# bottom edge from build/bgf/<bgf>/frame_00.png)
TREES = {
    "Mid": {  # midtree2 (OrnamentalObject type 192): a round, dense crown on a clear straight trunk
        "height_m": 4.92, "crown_base_m": 1.41, "crown_r_m": (1.72, 1.78), "trunk_d_m": 0.19,
        "limbs": (4, 6), "twigs": (3, 5), "cards": 460, "card_m": (0.68, 0.98),
        "variants": {"A": 11, "B": 23, "C": 37},
    },
    "Shrub": {  # shrubee1 (198): a dense dome of small leaves from the ground up, a hidden stem. The
        # crown's ellipsoid dips below the ground, so the dome meets the ground wide, as the sprite's
        "height_m": 2.26, "crown_base_m": -0.32, "crown_r_m": (1.12, 1.28), "trunk_d_m": 0.08,
        "limbs": (5, 7), "twigs": (2, 3), "cards": 340, "card_m": (0.48, 0.72), "min_card_z_m": 0.06,
        "variants": {"A": 41, "B": 53, "C": 67},
    },
    "Dead": {  # nectree1 (91): leafless and gnarled: a flared trunk splitting into crooked limbs and twigs
        "bare": True, "height_m": 3.76, "width_m": 2.64, "split_m": 1.15, "trunk_d_m": 0.4,
        "limbs": (4, 6), "depth": 4, "children": (2, 4), "gnarl": 0.3,
        "variants": {"A": 71, "B": 83},
    },
    # The fruit trees (FoodDispenser; build/bgf/appletree, peartree, orangetree): one shape, a round
    # crown on a stout trunk, its leaf atlas carrying the fruit (make_tree_textures.py "fruit").
    # Apple and orange share the drawing; the pear's crown is narrower and taller.
    "Apple": {
        "height_m": 4.37, "crown_base_m": 1.41, "crown_r_m": (1.54, 1.48), "trunk_d_m": 0.3,
        "limbs": (4, 6), "twigs": (3, 5), "cards": 400, "card_m": (0.62, 0.9),
        "variants": {"A": 101, "B": 113},
    },
    "Pear": {
        "height_m": 4.4, "crown_base_m": 1.32, "crown_r_m": (1.39, 1.54), "trunk_d_m": 0.26,
        "limbs": (4, 5), "twigs": (3, 5), "cards": 380, "card_m": (0.6, 0.88),
        "variants": {"A": 127, "B": 139},
    },
    "Orange": {
        "height_m": 4.37, "crown_base_m": 1.41, "crown_r_m": (1.54, 1.48), "trunk_d_m": 0.3,
        "limbs": (4, 6), "twigs": (3, 5), "cards": 400, "card_m": (0.62, 0.9),
        "variants": {"A": 151, "B": 163},
    },
    "Raza": {  # raztree1 (116): a broad yellow-green crown on a stout trunk with heavy limbs
        "height_m": 4.37, "crown_base_m": 1.38, "crown_r_m": (1.59, 1.5), "trunk_d_m": 0.32,
        "limbs": (5, 7), "twigs": (3, 5), "cards": 440, "card_m": (0.66, 0.96),
        "variants": {"A": 173, "B": 181, "C": 191},
    },
    # The world batch (docs/adr/0007 "World batch"): the rest of the world's trees, measured off their
    # sprites the same way (height and width from the visible pixels, crown base from the lowest wide
    # row). "taper" narrows a crown towards its top (0 an ellipsoid, 1 a cone): poplars, cypresses.
    "Mid1": {  # midtree1: a broad round crown on a slender trunk
        "height_m": 4.77, "crown_base_m": 1.61, "crown_r_m": (1.66, 1.58), "trunk_d_m": 0.17,
        "limbs": (4, 6), "twigs": (3, 5), "cards": 420, "card_m": (0.66, 0.96),
        "variants": {"A": 211, "B": 223, "C": 227},
    },
    "Mid3": {  # midtree3: a small, open crown
        "height_m": 4.27, "crown_base_m": 1.54, "crown_r_m": (1.0, 1.37), "trunk_d_m": 0.15, "taper": 0.2,
        "limbs": (3, 5), "twigs": (3, 4), "cards": 220, "card_m": (0.5, 0.76),
        "variants": {"A": 229, "B": 233},
    },
    "Mid4": {  # midtree4: a tall oval crown
        "height_m": 7.0, "crown_base_m": 1.48, "crown_r_m": (1.45, 2.76), "trunk_d_m": 0.17, "taper": 0.3,
        "limbs": (5, 7), "twigs": (3, 5), "cards": 640, "card_m": (0.7, 1.0),
        "variants": {"A": 239, "B": 241, "C": 251},
    },
    "Tall1": {  # talltree1: the biggest broadleaf, a high oval crown on a stout trunk
        "height_m": 8.76, "crown_base_m": 2.2, "crown_r_m": (1.7, 3.28), "trunk_d_m": 0.28, "taper": 0.25,
        "limbs": (6, 8), "twigs": (3, 5), "cards": 900, "card_m": (0.8, 1.15),
        "variants": {"A": 257, "B": 263, "C": 269},
    },
    "Tall2": {  # talltree2: narrow, pointed, poplar-like
        "height_m": 6.04, "crown_base_m": 1.59, "crown_r_m": (1.05, 2.23), "trunk_d_m": 0.15, "taper": 0.55,
        "limbs": (5, 7), "twigs": (2, 4), "cards": 320, "card_m": (0.55, 0.82),
        "variants": {"A": 271, "B": 277},
    },
    "Tall3": {  # talltree3: tall and narrow, pointed
        "height_m": 8.1, "crown_base_m": 1.88, "crown_r_m": (1.15, 3.11), "trunk_d_m": 0.18, "taper": 0.6,
        "limbs": (6, 8), "twigs": (2, 4), "cards": 520, "card_m": (0.65, 0.95),
        "variants": {"A": 281, "B": 283},
    },
    # Jungle trunks (tree1-3): a thick buttressed trunk wrapped in vines; the sprite shows only the
    # canopy's underside at its top edge. "flare" widens the foot, "roots" adds buttress roots,
    # "vines" hangs that many vines from under the crown.
    "Jungle1": {
        "height_m": 8.8, "crown_base_m": 7.75, "crown_r_m": (2.2, 0.52), "trunk_d_m": 0.95,
        "flare": 2.2, "roots": 5, "vines": 5, "limbs": (5, 7), "twigs": (2, 3), "cards": 380, "card_m": (0.75, 1.05),
        "variants": {"A": 293, "B": 307},
    },
    "Jungle2": {
        "height_m": 8.8, "crown_base_m": 7.75, "crown_r_m": (2.25, 0.52), "trunk_d_m": 1.0,
        "flare": 2.6, "roots": 6, "vines": 3, "limbs": (5, 7), "twigs": (2, 3), "cards": 380, "card_m": (0.75, 1.05),
        "variants": {"A": 311, "B": 313},
    },
    "Jungle3": {
        "height_m": 8.8, "crown_base_m": 7.75, "crown_r_m": (2.25, 0.52), "trunk_d_m": 0.95,
        "flare": 2.4, "roots": 5, "vines": 2, "limbs": (5, 7), "twigs": (2, 3), "cards": 380, "card_m": (0.75, 1.05),
        "variants": {"A": 317, "B": 331},
    },
    "Yrxl": {  # yrxltree: a twisted, vine-wrapped trunk with clumps of hanging moss
        "height_m": 4.4, "crown_base_m": 2.5, "crown_r_m": (0.9, 0.95), "trunk_d_m": 0.42,
        "flare": 1.8, "roots": 4, "vines": 4, "limbs": (3, 4), "twigs": (2, 3), "cards": 150, "card_m": (0.45, 0.7),
        "variants": {"A": 337, "B": 347},
    },
    # Shrubs, from the ground up like Shrub
    "RazaShrub": {  # razshrub: a yellow-green dome
        "height_m": 2.18, "crown_base_m": -0.3, "crown_r_m": (1.05, 1.24), "trunk_d_m": 0.08,
        "limbs": (5, 7), "twigs": (2, 3), "cards": 320, "card_m": (0.46, 0.7), "min_card_z_m": 0.06,
        "variants": {"A": 349, "B": 353, "C": 359},
    },
    "Bush": {  # shrub: a dark green dome
        "height_m": 2.18, "crown_base_m": -0.3, "crown_r_m": (1.05, 1.24), "trunk_d_m": 0.08,
        "limbs": (5, 7), "twigs": (2, 3), "cards": 320, "card_m": (0.46, 0.7), "min_card_z_m": 0.06,
        "variants": {"A": 367, "B": 373, "C": 379},
    },
    "Topiary": {  # roundshrub: a clipped cone
        "height_m": 4.25, "crown_base_m": -0.2, "crown_r_m": (1.1, 2.22), "trunk_d_m": 0.1, "taper": 0.55,
        "limbs": (6, 8), "twigs": (2, 3), "cards": 500, "card_m": (0.42, 0.62), "min_card_z_m": 0.08,
        "variants": {"A": 383, "B": 389},
    },
    "Cypress": {  # tallbush: a tall dark column on a short red trunk
        "height_m": 8.8, "crown_base_m": 0.35, "crown_r_m": (1.12, 4.22), "trunk_d_m": 0.25, "taper": 0.75,
        "limbs": (8, 10), "twigs": (2, 3), "cards": 900, "card_m": (0.5, 0.75),
        "variants": {"A": 397, "B": 401},
    },
    "Fern": {  # palm (OO_PALM): a low fern, knee-high
        "height_m": 0.55, "crown_base_m": -0.05, "crown_r_m": (0.46, 0.3), "trunk_d_m": 0.03,
        "limbs": (5, 7), "twigs": (1, 2), "cards": 60, "card_m": (0.3, 0.45), "min_card_z_m": 0.02,
        "variants": {"A": 409, "B": 419},
    },
}

TRUNK_SIDES = 9
BRANCH_SIDES = 5
BARK_TILE_M = 1.2  # bark texture repeat along the trunk (the texture is 1:2, wrapped once around)


class Builder:
    def __init__(self):
        self.bm = bmesh.new()
        # linear float colour, exported as the mesh's only COLOR_0 (a byte colour layer made through
        # bmesh isn't the active one, and the exporter then wrote a white COLOR_0 ahead of it:
        # wind weight 1 everywhere, so the whole tree slid at its base, 2026-10-06)
        self.col = self.bm.loops.layers.float_color.new("Col")
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.normals = []  # per face (creation order): [loop normal]

    def face(self, verts, uvs, normals, colors, mat):
        f = self.bm.faces.new([self.bm.verts.new(v) for v in verts])
        f.material_index = mat
        for loop, uv, c in zip(f.loops, uvs, colors):
            loop[self.uv].uv = uv
            loop[self.col] = c
        self.normals.append(normals)
        return f

    def to_object(self, name, materials):
        mesh = bpy.data.meshes.new(name)
        self.bm.to_mesh(mesh)
        self.bm.free()
        loops = [n for face in self.normals for n in face]
        mesh.normals_split_custom_set([tuple(n) for n in loops])
        for m in materials:
            mesh.materials.append(bpy.data.materials.get(m) or bpy.data.materials.new(m))
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def tube(b, points, radii, sides, wind, tint, ao, v0=0.0):
    """Tapered tube along points (Vectors) with radius per point; bark UVs: u once around, v along the
    length in BARK_TILE_M. wind/ao: per point. -> v at the end (for chaining)."""
    frames = []
    tangent_prev = None
    normal = None
    for i, p in enumerate(points):
        t = (points[min(i + 1, len(points) - 1)] - points[max(i - 1, 0)]).normalized()
        if normal is None:
            normal = t.orthogonal().normalized()
        else:  # parallel transport, so the tube doesn't twist
            axis = tangent_prev.cross(t)
            if axis.length > 1e-6:
                normal = Matrix.Rotation(tangent_prev.angle(t), 3, axis.normalized()) @ normal
        tangent_prev = t
        frames.append((p, t, normal.copy(), t.cross(normal)))
    vs = [v0]
    for i in range(1, len(points)):
        vs.append(vs[-1] + (points[i] - points[i - 1]).length / BARK_TILE_M)
    rings = []
    for (p, t, n, bn), r in zip(frames, radii):
        ring = []
        for j in range(sides + 1):
            a = 2 * math.pi * j / sides
            d = n * math.cos(a) + bn * math.sin(a)
            ring.append((p + d * r, d))
        rings.append(ring)
    for i in range(len(points) - 1):
        for j in range(sides):
            quad = [(i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)]
            b.face([rings[a][c][0] for a, c in quad],
                   [(c / sides, vs[a]) for a, c in quad],
                   [rings[a][c][1] for a, c in quad],
                   [(wind[a], tint, ao[a], 0.0) for a, c in quad], 0)
    return vs[-1]


def crown_point(cfg, centre, direction, frac):
    """The point at `frac` of the crown ellipsoid's radius from its centre along `direction`."""
    rh, rv = cfg["crown_r_m"]
    d = direction.normalized()
    # distance to the ellipsoid surface along d
    k = 1.0 / math.sqrt((d.x / rh) ** 2 + (d.y / rh) ** 2 + (d.z / rv) ** 2)
    p = d * k * frac
    s = taper_at(cfg, p.z)
    return centre + Vector((p.x * s, p.y * s, p.z))


def taper_at(cfg, z):
    """Horizontal scale of the crown at height z above its centre: 1 for an ellipsoid; with "taper",
    wider at the bottom and narrowing towards the top (a cone at taper 1), the middle unchanged."""
    t = cfg.get("taper", 0.0)
    if not t:
        return 1.0
    u = min(1.0, max(0.0, (z / cfg["crown_r_m"][1] + 1) / 2))  # 0 at the crown's bottom, 1 at its top
    return (1 - t * u) / (1 - t * 0.5)


def ellipsoid_normal(cfg, centre, p):
    rh, rv = cfg["crown_r_m"]
    q = p - centre
    t = cfg.get("taper", 0.0)
    if not t:
        return Vector((q.x / rh ** 2, q.y / rh ** 2, q.z / rv ** 2)).normalized()
    # a tapered crown: the ellipsoid's normal at the tapered radius, tipped up by the side's slope
    rs = rh * max(0.2, taper_at(cfg, q.z))
    n = Vector((q.x / rs ** 2, q.y / rs ** 2, q.z / rv ** 2)).normalized()
    slope = t * rh / (2 * rv * (1 - t * 0.5))
    return (n + Vector((0, 0, slope * math.hypot(n.x, n.y)))).normalized()


def wind_at(cfg, centre, p):
    """0 at the trunk base, rising through the crown, 1 at its rim."""
    rh, rv = cfg["crown_r_m"]
    up = max(0.0, (p.z - cfg["crown_base_m"] * 0.6) / (cfg["height_m"] - cfg["crown_base_m"] * 0.6))
    out = math.hypot(p.x, p.y) / rh
    return min(1.0, up * 0.55 + out * 0.6) ** 1.3


def ao_at(cfg, centre, p):
    rh, rv = cfg["crown_r_m"]
    q = p - centre
    rho = math.sqrt((q.x / rh) ** 2 + (q.y / rh) ** 2 + (q.z / rv) ** 2)
    below = 1.0 if p.z < cfg["crown_base_m"] else 0.0
    return max(below, min(1.0, 0.3 + 0.7 * rho ** 1.5))


def curve(start, direction, length, segments, rng, lift=0.0, wobble=0.0):
    """A gently bending branch: segments steps along direction, lifted towards +Z by `lift`."""
    pts = [start.copy()]
    d = direction.normalized()
    step = length / segments
    for _ in range(segments):
        d = (d + Vector((0, 0, lift)) + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * wobble).normalized()
        pts.append(pts[-1] + d * step)
    return pts


def skeleton(b, cfg, rng, centre):
    """Trunk, limbs and twigs as bark tubes."""
    r0 = cfg["trunk_d_m"] / 2
    top = cfg["crown_base_m"] + cfg["crown_r_m"][1] * 0.55
    lean = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * 0.03
    trunk = [Vector((0, 0, -0.15))] + [Vector((0, 0, top * t)) + lean * t * t * top +
                                       Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * 0.012 for t in (0.2, 0.45, 0.7, 0.85, 1.0)]
    flare = cfg.get("flare")  # the foot's widening; the first trees keep their fixed 1.35, 1.08
    foot = (r0 * flare, r0 * (1 + (flare - 1) * 0.23)) if flare else (r0 * 1.35, r0 * 1.08)
    radii = [*foot, r0, r0 * 0.9, r0 * 0.78, r0 * 0.4]
    tint = rng.random()
    tube(b, trunk, radii, TRUNK_SIDES, [wind_at(cfg, centre, p) for p in trunk], tint,
         [ao_at(cfg, centre, p) for p in trunk])
    roots(b, cfg, rng)
    vines(b, cfg, rng)
    n_limbs = rng.randint(*cfg["limbs"])
    phase = rng.uniform(0, 2 * math.pi)
    for i in range(n_limbs):
        az = phase + 2 * math.pi * (i + rng.uniform(-0.25, 0.25)) / n_limbs
        el = math.radians(rng.uniform(32, 62))
        start_t = rng.uniform(0.72, 0.97)
        start = trunk[0].lerp(trunk[-1], start_t) + Vector((0, 0, 0.15))
        start.z = cfg["crown_base_m"] + (top - cfg["crown_base_m"]) * rng.uniform(0.05, 0.95)
        d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
        end = crown_point(cfg, centre, crown_point(cfg, centre, d, 1.0) - centre, rng.uniform(0.62, 0.78))
        pts = curve(start, end - start, (end - start).length, 5, rng, lift=0.04, wobble=0.08)
        lr = r0 * rng.uniform(0.45, 0.6)
        tube(b, pts, [lr * (1 - 0.8 * k / 5) for k in range(6)], BRANCH_SIDES,
             [wind_at(cfg, centre, p) for p in pts], rng.random(), [ao_at(cfg, centre, p) for p in pts])
        for _ in range(rng.randint(*cfg["twigs"])):
            k = rng.uniform(0.3, 0.9)
            idx = min(4, int(k * 5))
            s = pts[idx].lerp(pts[idx + 1], k * 5 - idx)
            side = (end - start).normalized()
            td = (side + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.2, 0.9))) * 0.9).normalized()
            tend = crown_point(cfg, centre, (s + td) - centre, rng.uniform(0.8, 0.92))
            tpts = curve(s, tend - s, (tend - s).length, 3, rng, lift=0.05, wobble=0.12)
            tr = lr * 0.42
            tube(b, tpts, [tr * (1 - 0.85 * k / 3) for k in range(4)], 4,
                 [wind_at(cfg, centre, p) for p in tpts], rng.random(), [ao_at(cfg, centre, p) for p in tpts])


def roots(b, cfg, rng):
    """Buttress roots ("roots"): tubes from low on the trunk arching out and down into the ground."""
    r0 = cfg["trunk_d_m"] / 2
    n = cfg.get("roots", 0)
    if not n:  # no random draws, so trees without roots keep their seeds' shapes
        return
    phase = rng.uniform(0, 2 * math.pi)
    for i in range(n):
        az = phase + 2 * math.pi * (i + rng.uniform(-0.2, 0.2)) / n
        out = Vector((math.cos(az), math.sin(az), 0))
        reach = r0 * rng.uniform(2.2, 3.2)
        start = Vector((0, 0, r0 * rng.uniform(1.4, 2.2))) + out * r0 * 0.4
        pts = [start + out * reach * t + Vector((0, 0, -start.z * t ** 0.7 - 0.12 * t)) for t in (0.0, 0.3, 0.6, 1.0)]
        rr = r0 * rng.uniform(0.45, 0.6)
        tube(b, pts, [rr, rr * 0.75, rr * 0.5, rr * 0.25], BRANCH_SIDES, [0.0] * 4, rng.random(), [1.0] * 4)


def vines(b, cfg, rng):
    """Hanging vines ("vines"): thin tubes from under the crown, swaying down towards the ground."""
    rh = cfg["crown_r_m"][0]
    for _ in range(cfg.get("vines", 0)):
        az = rng.uniform(0, 2 * math.pi)
        out = Vector((math.cos(az), math.sin(az), 0))
        top = Vector((0, 0, cfg["crown_base_m"] + 0.1)) + out * rh * rng.uniform(0.35, 0.85)
        length = (top.z - 0.6) * rng.uniform(0.45, 0.85)
        ts = (0.0, 0.2, 0.4, 0.6, 0.8, 1.0)
        pts = [top + Vector((math.sin(t * 9 + az) * 0.08, math.cos(t * 7 + az) * 0.08, -length * t)) for t in ts]
        r = cfg["trunk_d_m"] * 0.035 + 0.012
        tube(b, pts, [r * f for f in (1.0, 1.0, 0.9, 0.85, 0.75, 0.5)], 4, [0.6 + 0.4 * t for t in ts],
             rng.random(), [0.6] * 6)


def lobes(rng, count=7):
    """Bulges on the crown (directions, mostly sideways and up), so it isn't a perfect ball."""
    out = []
    for _ in range(count):
        a = rng.uniform(0, 2 * math.pi)
        z = rng.uniform(-0.3, 0.9)
        out.append(Vector((math.cos(a) * math.sqrt(1 - z * z), math.sin(a) * math.sqrt(1 - z * z), z)))
    return out


def lumpy(bulges, d, amount=0.13):
    """Crown radius factor along direction d: up to 1 + amount over a bulge, 1 - amount between."""
    d = d.normalized()
    near = max(max(0.0, d.dot(v)) ** 6 for v in bulges)
    return 1.0 - amount + 2 * amount * near


def leaf_cards(b, cfg, rng, centre):
    rh, rv = cfg["crown_r_m"]
    bulges = lobes(rng)
    placed = 0
    while placed < cfg["cards"]:
        d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1)))
        if d.length < 1e-6:
            continue
        rho = 1.0 - 0.5 * rng.random() ** 2  # mostly near the shell, some deeper
        p = crown_point(cfg, centre, d, rho * lumpy(bulges, d))
        if p.z < cfg["crown_base_m"] + cfg.get("min_card_z_m", 0.1):
            continue
        out = ellipsoid_normal(cfg, centre, p)
        facing = (out + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * 0.55).normalized()
        # the card's "up" (twig base -> tip in the atlas): world up in the card's plane, rolled
        up = Vector((0, 0, 1)) - facing * facing.z
        if up.length < 0.2:
            up = out - facing * facing.dot(out)
        up = Matrix.Rotation(rng.uniform(-1.0, 1.0), 3, facing) @ up.normalized()
        right = up.cross(facing).normalized()
        size = rng.uniform(*cfg["card_m"])
        cell = rng.randrange(4)
        u0, v0 = (cell % 2) * 0.5, 0.5 - (cell // 2) * 0.5  # atlas row 0 is the image's top
        fold = size * 0.12
        base = p - up * size * 0.45  # the twig's base sits a little inside the crown
        # folded down the middle: left, centre (pushed out), right; bottom and top rows
        cols = [(-0.5, 0.0), (0.0, fold), (0.5, 0.0)]
        grid = [[base + right * (cx * size) + facing * f + up * (row * size) for cx, f in cols] for row in (0.0, 1.0)]
        uvs = [[(u0 + (cx + 0.5) * 0.5, v0 + row * 0.5) for cx, _ in cols] for row in (0.0, 1.0)]
        tint = rng.random()
        for c in range(2):
            quad = [(0, c), (0, c + 1), (1, c + 1), (1, c)]
            verts = [grid[r][k] for r, k in quad]
            normals = [ellipsoid_normal(cfg, centre, v) if (v - centre).length > 1e-3 else out for v in verts]
            colors = [(wind_at(cfg, centre, v), tint, ao_at(cfg, centre, v), 1.0) for v in verts]
            uv = [uvs[r][k] for r, k in quad]
            b.face(verts, uv, normals, colors, 1)
            b.face(verts[::-1], uv[::-1], normals[::-1], colors[::-1], 1)  # the back, same normals
        placed += 1


SIDES_BY_DEPTH = (7, 5, 4, 3, 3)


def bare_branch(b, cfg, rng, start, direction, length, radius, depth):
    """One crooked branch of a leafless tree and, recursively, its children. Vertex colour R (wind
    weight) rises with depth; the material's height bend keeps the trunk planted anyway."""
    segs = max(2, 5 - depth)
    pts = curve(start, direction, length, segs, rng, lift=0.06, wobble=cfg["gnarl"] * (1.0 + 0.25 * depth))
    radii = [radius * (1 - 0.85 * k / segs) for k in range(segs + 1)]  # nearly to a point: no cut ends
    wind = [min(1.0, 0.25 + 0.2 * depth + 0.1 * k / segs) for k in range(segs + 1)]
    tube(b, pts, radii, SIDES_BY_DEPTH[depth], wind, rng.random(), [1.0] * len(pts))
    if depth >= cfg["depth"]:
        return
    for _ in range(rng.randint(*cfg["children"])):
        t = rng.uniform(0.45, 1.0)
        i = min(segs - 1, int(t * segs))
        s = pts[i].lerp(pts[i + 1], t * segs - i)
        axis = direction.orthogonal().normalized()
        axis = Matrix.Rotation(rng.uniform(0, 2 * math.pi), 3, direction.normalized()) @ axis
        d = Matrix.Rotation(math.radians(rng.uniform(22, 55)), 3, axis) @ direction.normalized()
        d = (d + Vector((0, 0, 0.15))).normalized()
        bare_branch(b, cfg, rng, s, d, length * rng.uniform(0.58, 0.74), radii[i] * rng.uniform(0.58, 0.72), depth + 1)


def bare_tree(b, cfg, rng):
    """A leafless tree: a gnarled trunk with a flared foot up to split_m, then limbs that branch
    `depth` times. Built at a nominal size, then scaled to height_m by the caller."""
    r0 = cfg["trunk_d_m"] / 2
    split = cfg["split_m"]
    lean = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * 0.08
    ts = [0.0, 0.06, 0.18, 0.4, 0.65, 0.85, 1.0]
    trunk = [Vector((0, 0, -0.15))] + [Vector((0, 0, split * t)) + lean * t * split +
                                       Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * 0.09 * t for t in ts[1:]]
    # the foot flares out, then the bole swells and narrows into the fork
    radii = [r0 * f for f in (1.9, 1.45, 1.1, 0.95, 1.0, 0.85, 0.4)]  # tapers into the fork, no stub
    tube(b, trunk, radii, 10, [0.0, 0.0, 0.02, 0.05, 0.1, 0.15, 0.2], rng.random(), [1.0] * len(trunk))
    n = rng.randint(*cfg["limbs"])
    phase = rng.uniform(0, 2 * math.pi)
    for i in range(n):
        az = phase + 2 * math.pi * (i + rng.uniform(-0.3, 0.3)) / n
        el = math.radians(rng.uniform(25, 62))
        d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
        start = trunk[-2].lerp(trunk[-3], rng.uniform(0.0, 0.5))
        bare_branch(b, cfg, rng, start, d, rng.uniform(1.0, 1.4), r0 * rng.uniform(0.62, 0.8), 0)


def fit(obj, cfg):
    """Scale a mesh uniformly (about its foot) so its top is at height_m."""
    top = max(v.co.z for v in obj.data.vertices)
    k = cfg["height_m"] / top
    for v in obj.data.vertices:
        v.co *= k


def build(name, variant, seed, cfg):
    rng = random.Random(seed)
    b = Builder()
    if cfg.get("bare"):
        bare_tree(b, cfg, rng)
        obj = b.to_object("SM_Tree_%s_%s" % (name, variant), ["tree_bark"])
        fit(obj, cfg)
        return obj
    centre = Vector((0, 0, cfg["crown_base_m"] + cfg["crown_r_m"][1]))
    skeleton(b, cfg, rng, centre)
    leaf_cards(b, cfg, rng, centre)
    return b.to_object("SM_Tree_%s_%s" % (name, variant), ["tree_bark", "tree_leaves"])


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT, exist_ok=True)
    only = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    for name, cfg in TREES.items():
        if only and name not in only:
            continue
        for variant, seed in cfg["variants"].items():
            obj = build(name, variant, seed, cfg)
            bpy.ops.object.select_all(action="DESELECT")
            obj.select_set(True)
            bpy.context.view_layer.objects.active = obj
            path = os.path.join(OUT, obj.name + ".glb")
            bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                                      export_materials="EXPORT", export_image_format="NONE",
                                      export_yup=True, export_vertex_color="NAME", export_vertex_color_name="Col",
                                      export_all_vertex_colors=False, export_normals=True)
            tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
            print("[build_tree_kit] %s: %d triangles -> %s" % (obj.name, tris, path), flush=True)


main()
