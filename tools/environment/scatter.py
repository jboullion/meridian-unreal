"""
Deterministic ground scatter (docs/adr/0003 ground pass): points on the blockout's floors, by the
floor's original texture, for grass tufts and similar decoration. Pure Python; tools/ue/build_world.py
turns the result into AMRScatterActor instances.

Configured per zone in data/environment/zone_<rid>.json:

    "scatter": [
      {"name": "grass", "meshes": ["SM_GrassClump_A", "SM_GrassClump_B"],
       "per_m2": {"grd02306": 6.0, "grd09603": 1.5},     # density on floors with that texture
       "scale": [0.75, 1.35], "cull_m": [35, 55], "shadows": true, "seed": 1,
       "keep_out_m": 0.15}                                # distance kept from walls
    ]

    python tools/environment/scatter.py 300     # counts per mesh, without Unreal
"""
from __future__ import annotations

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blockout  # noqa: E402


def _walls_by_cell(prims, cell=2.0):
    """Vertical blockout edges at floor level, bucketed on a grid, for keep-out tests."""
    grid = {}
    for prim in prims.values():
        for a, b, c in prim.triangles:
            pa, pb, pc = prim.positions[a], prim.positions[b], prim.positions[c]
            if abs(prim.normals[a][1]) > 0.1:
                continue  # not a wall
            pts = (pa, pb, pc)
            low = min(p[1] for p in pts)
            base = [p for p in pts if abs(p[1] - low) < 1e-3]
            if len(base) < 2:
                continue
            seg = ((base[0][0], base[0][2]), (base[1][0], base[1][2]))
            x0, x1 = sorted((seg[0][0], seg[1][0]))
            z0, z1 = sorted((seg[0][1], seg[1][1]))
            for i in range(int(math.floor(x0 / cell)) - 1, int(math.floor(x1 / cell)) + 2):
                for j in range(int(math.floor(z0 / cell)) - 1, int(math.floor(z1 / cell)) + 2):
                    grid.setdefault((i, j), []).append(seg)
    return grid, cell


def _near_wall(walls, x, z, dist):
    grid, cell = walls
    for (ax, az), (bx, bz) in grid.get((int(math.floor(x / cell)), int(math.floor(z / cell))), ()):
        dx, dz = bx - ax, bz - az
        length2 = dx * dx + dz * dz
        t = 0.0 if length2 < 1e-9 else max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / length2))
        px, pz = ax + dx * t - x, az + dz * t - z
        if px * px + pz * pz < dist * dist:
            return True
    return False


def scatter(prims, rule):
    """-> {mesh name: [(x, y, z, yaw_deg, scale)]} in blockout glTF metres."""
    rng = random.Random(rule.get("seed", 1))
    meshes = rule["meshes"]
    out = {m: [] for m in meshes}
    keep_out = rule.get("keep_out_m", 0.15)
    walls = _walls_by_cell(prims) if keep_out > 0 else None
    s0, s1 = rule.get("scale", [1.0, 1.0])
    for material in sorted(rule["per_m2"]):
        density = rule["per_m2"][material]
        prim = prims.get(material)
        if not prim:
            continue
        for a, b, c in prim.triangles:
            if prim.normals[a][1] < 0.7:
                continue  # floors only (slopes up to ~45 degrees)
            pa, pb, pc = prim.positions[a], prim.positions[b], prim.positions[c]
            ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
            vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
            cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
            area = 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
            expected = area * density
            count = int(expected) + (1 if rng.random() < expected - int(expected) else 0)
            for _ in range(count):
                r1, r2 = rng.random(), rng.random()
                if r1 + r2 > 1:
                    r1, r2 = 1 - r1, 1 - r2
                x = pa[0] + ux * r1 + vx * r2
                y = pa[1] + uy * r1 + vy * r2
                z = pa[2] + uz * r1 + vz * r2
                mesh = meshes[rng.randrange(len(meshes))]
                yaw = rng.uniform(0, 360)
                scale = rng.uniform(s0, s1)
                if walls and _near_wall(walls, x, z, keep_out):
                    continue
                out[mesh].append((x, y, z, yaw, scale))
    return out


def main():
    import json
    rid = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    layout = json.load(open(os.path.join(blockout.REPO, "data", "zone_layout.json"), encoding="utf-8"))
    zone = next(z for z in layout["zones"] if z["rid"] == rid)
    prims = blockout.read_glb(os.path.join(blockout.REPO, zone["mesh"]))
    for rule in (blockout.zone_art_config(rid) or {}).get("scatter", []):
        result = scatter(prims, rule)
        print(rule["name"], {m: len(p) for m, p in result.items()}, "total", sum(len(p) for p in result.values()))


if __name__ == "__main__":
    main()
