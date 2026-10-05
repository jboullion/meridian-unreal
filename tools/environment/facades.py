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
    """Outline of an opening in texture pixels (x right, y down), starting at the bottom-left
    corner. `grow` expands it (pixels) for frames/surrounds.

    op: {"x": [left, right], "y": [apex, bottom], "spring": y where the arch starts,
         "shape": "pointed" | "round" | "rect" | "circle"}"""
    xl, xr = op["x"][0] - grow, op["x"][1] + grow
    ya, yb = op["y"][0] - grow, op["y"][1] + grow
    shape = op.get("shape", "rect")
    if shape == "circle":
        # ellipse inscribed in the box, starting at the bottom (no straight bottom edge)
        cx, cy, rx, ry = (xl + xr) / 2.0, (ya + yb) / 2.0, (xr - xl) / 2.0, (yb - ya) / 2.0
        n = segments * 3
        return [(cx + rx * math.sin(2 * math.pi * i / n), cy + ry * math.cos(2 * math.pi * i / n)) for i in range(n)]
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


def cutout_thickness(grd, data=None, catalog=None, crenels=False):
    """Thickness (m) to rebuild a cut-out (alpha) original as a solid, or None when it stays a flat
    cut-out: not transparent, foliage ("cutouts" "exclude"), or crenellations (built as parapets,
    unless `crenels`: zone art "cutouts" mode, where there are no parapets)."""
    import re
    data = data or load()
    if catalog is None:
        catalog = json.load(open(os.path.join(TEXTURES, "catalog.json"), encoding="utf-8"))["textures"]
    info = catalog.get(grd.split("__")[0])
    rules = data.get("cutouts", {})
    if not info or not info.get("has_transparency") or ("crenels" in data["textures"].get(grd, {}) and not crenels):
        return None
    name = info["name"]
    if rules.get("exclude") and re.search(rules["exclude"], name, re.I):
        return None
    for pattern, metres in rules.get("thickness_m", []):
        if re.search(pattern, name, re.I):
            return metres
    return rules.get("default_m", 0.03)


def cutout_materials():
    """Every texture that cutout_thickness() rebuilds as a solid."""
    data = load()
    catalog = json.load(open(os.path.join(TEXTURES, "catalog.json"), encoding="utf-8"))["textures"]
    return {grd for grd in catalog if cutout_thickness(grd, data, catalog)}


def check_overlays():
    """Draw every described feature over its texture (4x) for a visual check."""
    from PIL import Image, ImageDraw

    data = load()
    out_dir = os.path.join(REPO, "build", "environment", "facade_check")
    os.makedirs(out_dir, exist_ok=True)
    s = 4
    for grd, desc in data["textures"].items():
        if grd.startswith("_"):
            continue
        path = os.path.join(TEXTURES, grd + ".png")
        if not os.path.exists(path):
            print("missing", path)
            continue
        im = Image.open(path).convert("RGBA")
        im = im.resize((im.width * s, im.height * s), Image.NEAREST)
        d = ImageDraw.Draw(im)
        for op in desc.get("openings", []):
            outline = opening_outline(op)
            d.line([(x * s, y * s) for x, y in outline + outline[:1]], fill=(255, 0, 255, 255), width=2)
            fw = op.get("frame_px", 0)
            if fw:
                outer = opening_outline(op, fw)
                d.line([(x * s, y * s) for x, y in outer + outer[:1]], fill=(0, 255, 255, 255), width=1)
        for band in desc.get("bands", []):
            d.rectangle([0, band["y"][0] * s, im.width - 1, band["y"][1] * s], outline=(255, 255, 0, 255), width=2)
        for pier in desc.get("piers", []):
            d.rectangle([pier["x"][0] * s, 0, pier["x"][1] * s, im.height - 1], outline=(255, 160, 0, 255), width=2)
        cren = desc.get("crenels")
        if cren:
            for x0, x1 in cren["x"]:
                d.rectangle([x0 * s, 0, x1 * s, cren["depth_px"] * s], outline=(255, 80, 0, 255), width=2)
            d.line([(0, cren["cap_px"] * s), (im.width, cren["cap_px"] * s)], fill=(0, 255, 0, 255), width=1)
        im.save(os.path.join(out_dir, grd + ".png"))
        print(os.path.join(out_dir, grd + ".png"))


def openings_sheet(cols=6, s=4, pad_px=10, fit=320):
    """Every described opening cropped from its texture at up to s x (fit px a side), with its outline (magenta) and frame
    (cyan) drawn on, labelled, in one image: build/environment/facade_check/openings_sheet.png.
    The quickest way to spot an outline that misses its painted window or door."""
    from PIL import Image, ImageDraw

    data = load()
    tiles = []
    for grd, desc in sorted(data["textures"].items()):
        if grd.startswith("_"):
            continue
        path = os.path.join(TEXTURES, grd + ".png")
        if not os.path.exists(path):
            continue
        tex = Image.open(path).convert("RGB")
        for i, op in enumerate(desc.get("openings", [])):
            fw = op.get("frame_px", 0)
            outer = opening_outline(op, fw)
            x0 = max(0, int(min(x for x, _ in outer)) - pad_px)
            y0 = max(0, int(min(y for _, y in outer)) - pad_px)
            x1 = min(tex.width, int(max(x for x, _ in outer)) + pad_px + 1)
            y1 = min(tex.height, int(max(y for _, y in outer)) + pad_px + 1)
            k = min(s, fit / max(x1 - x0, y1 - y0))  # big doors shrink to fit a cell
            im = tex.crop((x0, y0, x1, y1)).resize((round((x1 - x0) * k), round((y1 - y0) * k)), Image.NEAREST)
            d = ImageDraw.Draw(im)
            for pts, colour, width in ((outer, (0, 255, 255), 1), (opening_outline(op), (255, 0, 255), 2)):
                if pts is outer and not fw:
                    continue
                d.line([((x - x0) * k, (y - y0) * k) for x, y in pts + pts[:1]], fill=colour, width=width)
            tiles.append(("%s #%d %s %s" % (grd, i, op.get("kind", "window"), op.get("shape", "rect")), im))
    cell = max(max(im.width for _, im in tiles), max(im.height for _, im in tiles)) + 24
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * cell), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    for n, (label, im) in enumerate(tiles):
        cx, cy = (n % cols) * cell, (n // cols) * cell
        sheet.paste(im, (cx + (cell - im.width) // 2, cy + 20))
        d.text((cx + 4, cy + 4), label, fill=(255, 255, 255))
    out = os.path.join(REPO, "build", "environment", "facade_check", "openings_sheet.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print("%d openings -> %s" % (len(tiles), out))
    return out


if __name__ == "__main__":
    check_overlays()
    openings_sheet()
