"""
Minimap styles (docs/adr/0009-user-interface.md) from the in-game top-down captures
(tools/ue/run_map_capture.ps1 -> build/minimap/raw/T_Map_<rid>.png + .json):

    photo      the capture as it is
    walls      the capture, a little darker, with the zone's solid walls drawn over it (the
               original map's lines, from tools/roo2gltf/roo2gltf.py --walls-only)
    parchment  the original map's look: the parchment (mapbkgnd) with a faint wash of the
               capture and the walls in black

    python tools/ui/minimap.py

Writes build/minimap/<style>/T_Map_<rid>.png for every style, copies the one data/ui/minimap.json
"style" names to build/minimap/final/ (what tools/ue/import_ui.py imports) and writes a review
sheet, build/minimap/review.png (a row per zone, a column per style).
"""
from __future__ import annotations

import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "build" / "minimap" / "raw"
OUT = ROOT / "build" / "minimap"
STYLES = ("photo", "walls", "parchment")


def zone_walls(rid: int, layout: dict) -> list[tuple[float, float, float, float]]:
    """World (UE cm) wall segments of the zone whose geometry this is."""
    zone = next((z for z in layout["zones"] if z["rid"] == rid), None)
    if not zone:
        return []
    path = ROOT / "build" / "zones" / ("%d_%s_walls.json" % (rid, zone["class"]))
    if not path.exists():
        print("  no walls for %d (python tools/roo2gltf/roo2gltf.py --walls-only)" % rid)
        return []
    ox, oy, _ = zone["world_origin_cm"]
    return [(ox + 100 * x0, oy + 100 * z0, ox + 100 * x1, oy + 100 * z1) for x0, z0, x1, z1 in json.loads(path.read_text())["walls"]]


def draw_walls(im: Image.Image, walls, meta: dict, color, width: int, halo=None) -> None:
    (mx, my), (Mx, My) = meta["rect_min_cm"], meta["rect_max_cm"]
    sx, sy = im.width / (Mx - mx), im.height / (My - my)
    d = ImageDraw.Draw(im)
    segs = [((x0 - mx) * sx, (y0 - my) * sy, (x1 - mx) * sx, (y1 - my) * sy) for x0, y0, x1, y1 in walls]
    if halo:
        for s in segs:
            d.line(s, fill=halo, width=width + 4)
    for s in segs:
        d.line(s, fill=color, width=width)


def parchment_tile(size: int) -> Image.Image:
    for variant in ("ai_ultrasharp", "nearest"):
        p = ROOT / "build" / "ui" / "art" / variant / "T_UI_mapbkgnd.png"
        if p.exists():
            tile = Image.open(p).convert("RGB")
            out = Image.new("RGB", (size, size))
            for y in range(0, size, tile.height):
                for x in range(0, size, tile.width):
                    out.paste(tile, (x, y))
            return out
    return Image.new("RGB", (size, size), (168, 168, 168))


def outside_mask(photo: Image.Image) -> Image.Image:
    """Alpha: 0 where the capture saw nothing (black, outside the zone), so the minimap's
    parchment shows there; a soft edge."""
    lum = ImageOps.grayscale(photo)
    # empty = black and connected to the picture's edge (dark water inside the zone stays)
    dark = lum.point(lambda v: 255 if v <= 8 else 0).filter(ImageFilter.MinFilter(3))
    w, h = dark.size
    px = dark.load()
    for x, y in [(x, 0) for x in range(0, w, 16)] + [(x, h - 1) for x in range(0, w, 16)] +             [(0, y) for y in range(0, h, 16)] + [(w - 1, y) for y in range(0, h, 16)]:
        if px[x, y] == 255:
            ImageDraw.floodfill(dark, (x, y), 128)
    solid = dark.point(lambda v: 0 if v == 128 else 255).filter(ImageFilter.MaxFilter(3))
    return solid.filter(ImageFilter.GaussianBlur(1.5))


def style(name: str, photo: Image.Image, walls, meta: dict) -> Image.Image:
    w = max(2, photo.width // 512)
    if name == "photo":
        im = photo.convert("RGBA")
        im.putalpha(outside_mask(photo))
        return im
    if name == "walls":
        im = ImageEnhance.Brightness(photo).enhance(0.85).convert("RGBA")
        im.putalpha(outside_mask(photo))
        draw_walls(im, walls, meta, (250, 240, 215, 255), w, halo=(20, 16, 12, 255))
        return im
    # parchment: the capture as a faint ink wash on the original map's paper
    paper = parchment_tile(photo.width)
    wash = ImageOps.grayscale(photo).convert("RGB")
    im = Image.blend(paper, Image.composite(wash, paper, Image.new("L", photo.size, 255)), 0.35)
    im = Image.blend(im, ImageEnhance.Color(photo).enhance(0.4), 0.15)
    draw_walls(im, walls, meta, (10, 8, 6), w)
    return im


def main():
    layout = json.loads((ROOT / "data" / "zone_layout.json").read_text(encoding="utf-8"))
    chosen = json.loads((ROOT / "data" / "ui" / "minimap.json").read_text(encoding="utf-8")).get("style", "photo")
    raws = sorted(RAW.glob("T_Map_*.png"))
    if not raws:
        raise SystemExit("no captures in %s: run tools/ue/run_map_capture.ps1" % RAW)
    for s in STYLES + ("final",):
        (OUT / s).mkdir(parents=True, exist_ok=True)
    thumbs = []
    for raw in raws:
        rid = int(raw.stem.split("_")[-1])
        meta = json.loads(raw.with_suffix(".json").read_text())
        photo = Image.open(raw).convert("RGB")
        walls = zone_walls(rid, layout)
        row = []
        for s in STYLES:
            im = style(s, photo, walls, meta)
            im.save(OUT / s / raw.name)
            thumb = parchment_tile(im.width)
            if im.mode == "RGBA":
                thumb.paste(im, (0, 0), im)
            else:
                thumb = im
            row.append(thumb.resize((420, 420), Image.Resampling.LANCZOS))
        shutil.copy(OUT / chosen / raw.name, OUT / "final" / raw.name)
        thumbs.append((rid, row))
        print("%d: %d walls" % (rid, len(walls)))
    # review sheet
    pad, label = 10, 24
    sheet = Image.new("RGB", (pad + len(STYLES) * (420 + pad), label + len(thumbs) * (420 + pad + label)), (18, 18, 20))
    d = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=18)
    for c, s in enumerate(STYLES):
        d.text((pad + c * (420 + pad), 2), s + (" (in use)" if s == chosen else ""), fill=(255, 236, 170), font=font)
    for r, (rid, row) in enumerate(thumbs):
        y = label + r * (420 + pad + label)
        d.text((pad, y + 2), str(rid), fill=(220, 220, 220), font=font)
        for c, im in enumerate(row):
            sheet.paste(im, (pad + c * (420 + pad), y + label))
    sheet.save(OUT / "review.png")
    print(OUT / "review.png")


if __name__ == "__main__":
    main()
