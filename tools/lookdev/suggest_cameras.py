"""
Suggest look-dev cameras for zones (data/environment/lookdev_cameras.json entries, printed as JSON):
one per zone, standing on the zone's main floor (the floor height with the most area) near a
corner, at eye height, looking diagonally across to the far corner. Good enough to start from for
interiors; move or add cameras by hand (or MRBookmark in game) where a room needs it.

    python tools/lookdev/suggest_cameras.py 301 302 303      # -> [{"name": "int_301", ...}, ...]
    python tools/lookdev/suggest_cameras.py 301 --prefix int_inn
    python tools/lookdev/suggest_cameras.py 301 --prefix int_inn_2 --opposite   # from the far corner, looking back
    python tools/lookdev/suggest_cameras.py 357 --open --prefix int_jasab7   # two open views: int_jasab7, int_jasab7_2

--open (2026-10-10, Jasper): the corner walk put about a third of Jasper's small rooms' cameras against
a wall. It searches a grid on the main floor instead (with --outdoor, the first camera stands at the
arrival point), casts view rays through the blockout every 10 degrees and keeps the spot and
direction whose view is most open; a spot where any ray meets nothing is outside the room and
refused. The second camera stands away from the first and looks another way.
"""
import argparse
import collections
import glob
import json
import math
import os
import sys

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "environment"))
import blockout  # noqa: E402


def floor_triangles(glb):
    """[(a, b, c, height, area)] of the upward-facing triangles, (x, z) corners in glTF metres."""
    out = []
    for prim in blockout.read_glb(glb).values():
        P = prim.positions
        for t in prim.triangles:
            a, b, c = (P[i] for i in t)
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            ln = math.sqrt(sum(x * x for x in n))
            if ln > 1e-6 and n[1] / ln > 0.97:
                out.append(((a[0], a[2]), (b[0], b[2]), (c[0], c[2]), (a[1] + b[1] + c[1]) / 3, ln / 2))
    return out


def inside(p, a, b, c):
    def s(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
    d1, d2, d3 = s(p, a, b), s(p, b, c), s(p, c, a)
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def zone_glb(rid):
    return next((g for g in sorted(glob.glob(os.path.join(REPO, "build", "zones", "%d_*.glb" % rid)))
                 if not g.endswith("_collision.glb")), None)


def suggest(rid, origin, name, opposite=False):
    glb = zone_glb(rid)
    if not glb:
        raise SystemExit("no build/zones/%d_*.glb (run tools/roo2gltf/roo2gltf.py)" % rid)
    floors = floor_triangles(glb)
    by_height = collections.Counter()
    for *_, h, area in floors:
        by_height[round(h, 1)] += area
    main_h = by_height.most_common(1)[0][0]
    main = [f for f in floors if abs(f[3] - main_h) < 0.15]
    xs = [p[0] for f in main for p in f[:3]]
    zs = [p[1] for f in main for p in f[:3]]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    if opposite:
        x0, x1, z0, z1 = x1, x0, z1, z0  # the same walk from the far corner
    # from 15% in from a corner towards the middle, until the point is on the main floor
    px, pz = (x0 + x1) / 2, (z0 + z1) / 2
    for k in range(40):
        t = k / 40 * 0.7
        qx, qz = x0 + (x1 - x0) * (0.15 + 0.35 * t), z0 + (z1 - z0) * (0.15 + 0.35 * t)
        if any(inside((qx, qz), *f[:3]) for f in main):
            px, pz = qx, qz
            break
    tx, tz = x0 + (x1 - x0) * 0.85, z0 + (z1 - z0) * 0.85
    yaw = math.degrees(math.atan2(tz - pz, tx - px))  # UE yaw: 0 east (glTF x), 90 south (glTF z)
    ox, oy, oz = origin
    return {"name": name, "location_cm": [round(ox + px * 100), round(oy + pz * 100), round(oz + (main_h + 1.7) * 100)],
            "rotation": [-6.0, round(yaw, 1)], "fov": 90}


# ------------------------------------------------------------------------------ --open

EYE_M = 1.7
NO_HIT_M = 80.0


def _triangles(glb):
    tris = []
    for prim in blockout.read_glb(glb).values():
        P = prim.positions
        for t in prim.triangles:
            tris.append([P[i] for i in t])
    return np.array(tris, float)


def _rays(T, o, dirs):
    """Distance from o along each direction to the first triangle (Moller-Trumbore), NO_HIT_M if none."""
    e1 = T[:, 1] - T[:, 0]
    e2 = T[:, 2] - T[:, 0]
    s = o - T[:, 0]
    q = np.cross(s, e1)
    out = []
    for d in dirs:
        p = np.cross(d, e2)
        det = (e1 * p).sum(1)
        ok = np.abs(det) > 1e-9
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        u = (s * p).sum(1) * inv
        v = (q * d).sum(1) * inv
        t = (e2 * q).sum(1) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0.05)
        out.append(float(t[hit].min()) if hit.any() else NO_HIT_M)
    return out


def _floor_at(F, x, z):
    """The height of the highest upward floor triangle under (x, z), at that point, or None.
    F: (n, 3, 3) triangles. Interpolated across the triangle: its corners' mean put cameras metres
    under the Black Desert's dunes (big sloped triangles), where they saw only sky (2026-10-12)."""
    a, b, c = F[:, 0][:, [0, 2]], F[:, 1][:, [0, 2]], F[:, 2][:, [0, 2]]

    def s(p2, p3):
        return (x - p3[:, 0]) * (p2[:, 1] - p3[:, 1]) - (p2[:, 0] - p3[:, 0]) * (z - p3[:, 1])
    d1, d2, d3 = s(a, b), s(b, c), s(c, a)
    hit = ~(((d1 < 0) | (d2 < 0) | (d3 < 0)) & ((d1 > 0) | (d2 > 0) | (d3 > 0)))
    if not hit.any():
        return None
    total = (d1 + d2 + d3)[hit]
    total = np.where(np.abs(total) < 1e-12, 1e-12, total)
    # barycentric weights: d2 belongs to the corner opposite edge b-c (a), d3 to b, d1 to c
    h = (d2[hit] * F[hit][:, 0, 1] + d3[hit] * F[hit][:, 1, 1] + d1[hit] * F[hit][:, 2, 1]) / total
    return float(h.max())


def _views(T, spot, outdoor=False, drawn=None):
    """Ray distances every 10 degrees at eye height (UE yaw: 0 east, 90 south), or None when the
    spot is under something low (a counter, a bed) or outside the room (indoors more than a sixth of the
    rays meet nothing; outdoors, where a view may open onto the sea (Barloque's ports), all but 6)).
    T (the collision mesh, untextured walls kept) decides where the room is; with drawn (the drawn
    mesh), the distances are to what's drawn, 0 where a ray meets nothing drawn: a view onto an
    untextured wall shows only sky (the Black Desert's oasis, 2026-10-12)."""
    x, h, z = spot
    if _rays(T, np.array([x, h + 0.3, z]), [np.array([0.0, 1.0, 0.0])])[0] < EYE_M - 0.2:
        return None
    o = np.array([x, h + EYE_M, z])
    dirs = [np.array([math.cos(math.radians(a)), 0.0, math.sin(math.radians(a))]) for a in range(0, 360, 10)]
    d = _rays(T, o, dirs)
    misses = sum(1 for x in d if x >= NO_HIT_M)
    # inside, a few rays may leave through an opening (Berg's Jala: every spot misses 2 to 15); outside
    # the room they all do. Outdoors a view may open onto the sea: some walls in view are enough
    if misses > (len(d) - 6 if outdoor else len(d) // 6):
        return None
    if drawn is None:
        return d
    return [0.0 if x >= NO_HIT_M else x for x in _rays(drawn, o, dirs)]


def _best_yaw(d, avoid=None):
    """(score, yaw) of the most open 70-degree view: its mean distance, capped by twice the nearest
    (a wall at the edge of the frame counts). avoid: a yaw the view must turn 90 degrees away from."""
    best = (-1.0, 0)
    for i in range(36):
        if avoid is not None and abs(((i * 10 - avoid) + 180) % 360 - 180) < 90:
            continue
        span = [d[(i + k) % 36] for k in range(-3, 4)]
        best = max(best, (min(min(span) * 2, sum(span) / len(span)), i * 10))
    return best


def open_views(rid, zone, name, outdoor=False):
    """Two cameras with open views on the zone's main floor: [name, name + "_2"]."""
    glb = zone_glb(rid)
    if not glb:
        raise SystemExit("no build/zones/%d_*.glb (run tools/roo2gltf/roo2gltf.py)" % rid)
    D = _triangles(glb)
    n = np.cross(D[:, 1] - D[:, 0], D[:, 2] - D[:, 0])
    F = D[n[:, 1] / (np.linalg.norm(n, axis=1) + 1e-9) > 0.97]  # the drawn floors: cameras stand on what's drawn
    # view rays against the collision mesh: it keeps the untextured walls the drawn mesh leaves out
    collision = glb[:-4] + "_collision.glb"
    T = _triangles(collision) if os.path.exists(collision) else D
    by_height = collections.Counter()
    for *_, h, area in floor_triangles(glb):
        by_height[round(h, 1)] += area
    main_h = by_height.most_common(1)[0][0]
    tp = (zone.get("teleport") or {}).get("pos")
    tp_h = _floor_at(F, tp[0], tp[2]) if outdoor and tp else None
    if tp_h is not None:
        main_h = tp_h  # outdoors both views stand on the arrival point's level (not a lake bed)
    b = zone["bounds_m"]
    sx, sz = b["max"][0] - b["min"][0], b["max"][2] - b["min"][2]
    step = max(1.1, max(sx, sz) / 30)
    spots = []
    for x in np.arange(b["min"][0] + step / 2, b["max"][0], step):
        for z in np.arange(b["min"][2] + step / 2, b["max"][2], step):
            h = _floor_at(F, float(x), float(z))
            if h is not None and abs(h - main_h) < 0.3:
                spots.append((float(x), h, float(z)))
    views = {}

    def view(sp):
        if sp not in views:
            views[sp] = _views(T, sp, outdoor, drawn=D)
        return views[sp]
    first = None
    if tp_h is not None and view((tp[0], tp_h, tp[2])):  # outdoors the first view is where the player arrives
        first = (tp[0], tp_h, tp[2])
    if first is None:
        cands = [sp for sp in spots if view(sp)]
        if not cands:
            raise SystemExit("zone %d: no spot on its main floor (%.1f m) sees walls all round" % (rid, main_h))
        first = max(cands, key=lambda sp: _best_yaw(view(sp))[0])
    _, yaw1 = _best_yaw(view(first))
    far = max(4.0, 0.25 * min(sx, sz))
    cands = [(sp, _best_yaw(view(sp), avoid=yaw1)) for sp in spots
             if math.hypot(sp[0] - first[0], sp[2] - first[2]) >= far and view(sp)]
    second, (_, yaw2) = max(cands, key=lambda c: c[1][0]) if cands else (first, _best_yaw(view(first), avoid=yaw1))
    ox, oy, oz = zone["world_origin_cm"]

    def cam(nm, sp, yaw):
        return {"name": nm, "location_cm": [round(ox + sp[0] * 100), round(oy + sp[2] * 100), round(oz + (sp[1] + EYE_M) * 100)],
                "rotation": [-6.0, float(yaw if yaw <= 180 else yaw - 360)], "fov": 90}
    return [cam(name, first, yaw1), cam(name + "_2", second, yaw2)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rids", nargs="+", type=int)
    ap.add_argument("--prefix", default="int_", help="camera name prefix (name = prefix + rid, or prefix alone for one zone)")
    ap.add_argument("--opposite", action="store_true", help="stand in the far corner and look back")
    ap.add_argument("--open", action="store_true", help="two cameras per zone where the view is most open (see above)")
    ap.add_argument("--outdoor", action="store_true", help="with --open: the first camera stands at the arrival point")
    args = ap.parse_args()
    layout = json.load(open(os.path.join(REPO, "data", "zone_layout.json"), encoding="utf-8"))
    zones = {z["rid"]: z for z in layout["zones"]}
    cams = []
    for rid in args.rids:
        name = args.prefix if len(args.rids) == 1 and not args.prefix.endswith("_") else "%s%d" % (args.prefix, rid)
        if args.open:
            cams += open_views(rid, zones[rid], name, args.outdoor)
        else:
            cams.append(suggest(rid, zones[rid]["world_origin_cm"], name, args.opposite))
    print(json.dumps(cams, indent=1))


if __name__ == "__main__":
    main()
