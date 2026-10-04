"""
Facade feature descriptions (data/environment/facades.json) and their outlines (docs/adr/0003 pass 3).

The original Raza buildings are flat walls with windows, doors, string courses and crenellations
painted into their textures. facades.json records where those features are, in the texture's own
pixels, so tools/blender/build_zone_art.py can rebuild them as geometry exactly where the
painting shows them (recessed stained glass, protruding bands, solid merlons) while the wall keeps
the original texture.

    python tools/environment/facades.py          # overlay every outline on its texture
                                                 # -> build/environment/facade_check/<grd>.png

Pure Python (the overlay check needs Pillow), shared with Blender.
"""
from __future__ import annotations

import json
import math
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FACADES = os.path.join(REPO, "data", "environment", "facades.json")
TEXTURES = os.path.join(REPO, "build", "textures")


def load():
    """-> {"defaults": {...}, "textures": {grd: description}}"""
    return json.load(open(FACADES, encoding="utf-8"))


def _arc(cx, cy, r, a0, a1, segments):
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / segments), cy + r * math.sin(a0 + (a1 - a0) * i / segments))
            for i in range(segments + 1)]


def opening_outline(op, grow=0.0, segments=8):
    """Outline of an opening in texture pixels (x right, y down), counter-clockwise on screen
    starting at the bottom-left corner. `grow` expands it (pixels) for frames/surrounds.

    op: {"x": [left, right], "y": [apex, bottom], "spring": y where the arch starts,
         "shape": "pointed" | "round" | "rect"}"""
    xl, xr = op["x"][0] - grow, op["x"][1] + grow
    ya, yb = op["y"][0] - grow, op["y"][1] + grow
    shape = op.get("shape", "rect")
    if shape == "rect":
        return [(xl, yb), (xr, yb), (xr, ya), (xl, ya)]
    ys = op["spring"]
    a = (xr - xl) / 2.0
    h = ys - ya  # rise of the arch (pixels, y down)
    xm = (xl + xr) / 2.0
    pts = [(xl, yb), (xr, yb), (xr, ys)]
    if shape == "round" or h <= 0 or abs(h - a) < 1e-6:
        # semicircle (or segmental when h != a): centre below the apex on the midline
        r = (a * a + h * h) / (2 * h) if h > 0 else a
        cy = ya + r
        start = math.atan2(ys - cy, xr - xm)
        end = math.atan2(ys - cy, xl - xm)
        if end > start:
            end -= 2 * math.pi
        # sweep from the right springer over the top (y decreasing) to the left springer
        arc = _arc(xm, cy, r, start, end, segments * 2)
        pts += arc[1:-1]
    else:
        # pointed (gothic): two arcs centred on the spring line, meeting at the apex
        r = (a * a + h * h) / (2 * a)
        # right arc: centre at (xr - r, ys), from the right springer (angle 0) up to the apex
        cxr = xr - r
        ang_apex_r = math.atan2(ya - ys, xm - cxr)
        pts += _arc(cxr, ys, r, 0.0, ang_apex_r, segments)[1:]
        # left arc: centre at (xl + r, ys), from the apex down to the left springer (angle pi)
        cxl = xl + r
        ang_apex_l = math.atan2(ya - ys, xm - cxl)
        pts += _arc(cxl, ys, r, ang_apex_l, -math.pi, segments)[1:-1]
    pts.append((xl, ys))
    return pts


def check_overlays():
    """Draw every described feature over its texture (4x) for a visual check."""
    from PIL import Image, ImageDraw

    data = load()
    out_dir = os.path.join(REPO, "build", "environment", "facade_check")
    os.makedirs(out_dir, exist_ok=True)
    s = 4
    for grd, desc in data["textures"].items():
        path = os.path.join(TEXTURES, grd + ".png")
        if not os.path.exists(path):
            print("missing", path)
            continue
        im = Image.open(path).convert("RGBA")
        im = im.resize((im.width * s, im.height * s), Image.NEAREST)
        d = ImageDraw.Draw(im)
        for op in desc.get("openings", []):
            d.line([(x * s, y * s) for x, y in opening_outline(op)] + [(op["x"][0] * s, op["y"][1] * s)], fill=(255, 0, 255, 255), width=2)
            fw = op.get("frame_px", 0)
            if fw:
                d.line([(x * s, y * s) for x, y in opening_outline(op, fw)] + [((op["x"][0] - fw) * s, (op["y"][1] + fw) * s)], fill=(0, 255, 255, 255), width=1)
        for band in desc.get("bands", []):
            d.rectangle([0, band["y"][0] * s, im.width - 1, band["y"][1] * s], outline=(255, 255, 0, 255), width=2)
        cren = desc.get("crenels")
        if cren:
            for x0, x1 in cren["x"]:
                d.rectangle([x0 * s, 0, x1 * s, cren["depth_px"] * s], outline=(255, 80, 0, 255), width=2)
            d.line([(0, cren["cap_px"] * s), (im.width, cren["cap_px"] * s)], fill=(0, 255, 0, 255), width=1)
        im.save(os.path.join(out_dir, grd + ".png"))
        print(os.path.join(out_dir, grd + ".png"))


if __name__ == "__main__":
    check_overlays()
