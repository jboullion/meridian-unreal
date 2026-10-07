"""
The in-game UI's art (docs/adr/0009-user-interface.md), from the original client's interface
bitmaps in the Server-104 checkout (reference only; the output is git-ignored):

    module/merintr/bitmap/*.bmp   frames ("treatments"), stat bars, tab buttons, slot cursor
    clientd3d/bitmap/*.bmp        the stone window background, map parchment, view corners

    python tools/ui/build_ui_art.py                 # every variant in data/ui/ui_style.json "art_variants"
    python tools/ui/build_ui_art.py --variant nearest

Writes build/ui/art/<variant>/T_UI_<piece>.png (4x the original pixels) and
build/ui/art/<variant>/manifest.json: each piece's original size and, for frames, which pieces
make it up. Variants:

    nearest   4x nearest neighbour: the original pixels, crisp
    ai        4x with an upscale model (tools/textures/upscale.py in build/texai/.venv)

The original keys transparency with pure cyan (0, 255, 255). Before an AI upscale the
transparent pixels are filled with their nearest opaque colour (so cyan doesn't bleed into the
edges) and tiles are wrap-padded (so they still tile); the alpha mask is upscaled separately and
re-thresholded, which smooths the staircase a little without blurring the cut-out.

A frame (the original's "treatment", module/merintr/drawint.c) is eight corner strips, two at
each corner (one along the top or bottom edge, one down the side), plus four repeaters tiled
between them. The game draws it that way (SMRFrame), so the original pieces are kept as they are.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from server104 import SERVER104  # noqa: E402

MERINTR = SERVER104 / "module" / "merintr" / "bitmap"
CLIENT = SERVER104 / "clientd3d" / "bitmap"
STYLE = ROOT / "data" / "ui" / "ui_style.json"
OUT = ROOT / "build" / "ui" / "art"
VENV_PY = ROOT / "build" / "texai" / ".venv" / "Scripts" / "python.exe"
MODELS = ROOT / "build" / "texai" / "models"
CYAN = (0, 255, 255)
SCALE = 4

# frame sets: name -> (folder, {slot: file}). Slots: the eight corner strips and four repeaters.
FRAMES = {
    # the iron-and-vine "edge treatment" (around the chat and text boxes)
    "edge": (MERINTR, {
        "ul_h": "edgetreat_ultop", "ul_v": "edgetreat_ulleft", "ur_h": "edgetreat_urtop", "ur_v": "edgetreat_urright",
        "ll_h": "edgetreat_llbottom", "ll_v": "edgetreat_llleft", "lr_h": "edgetreat_lrbottom", "lr_v": "edgetreat_lrright",
        "top": "edgetreat_urepeat", "bottom": "edgetreat_brepeat", "left": "edgetreat_lrepeat", "right": "edgetreat_rrepeat"}),
    # the 3D view's frame (merintr.rc IDB_ULTOP... are the viewtreat_* strips; ultop.bmp and its set
    # are unused): an 8 px band outside the view, the gold-ball corners (view_ul...) inside it
    "view": (MERINTR, {
        "ul_h": "viewtreat_ul_top", "ul_v": "viewtreat_ul_left", "ur_h": "viewtreat_ur_top", "ur_v": "viewtreat_ur_right",
        "ll_h": "viewtreat_ll_bottom", "ll_v": "viewtreat_ll_left", "lr_h": "viewtreat_lr_bottom", "lr_v": "viewtreat_lr_right",
        "top": "top", "bottom": "bottom", "left": "left", "right": "right"}),
    # the inventory pane's thin bevel
    "inv": (MERINTR, {
        "ul_h": "iultop", "ul_v": "iulleft", "ur_h": "iurtop", "ur_v": "iurright",
        "ll_h": "illbottom", "ll_v": "illleft", "lr_h": "ilrbottom", "lr_v": "ilrright",
        "top": "itop", "bottom": "ibottom", "left": "ileft", "right": "iright"}),
    # the stats pane's bevel
    "stat": (MERINTR, {
        "ul_h": "statultop", "ul_v": "statulleft", "ur_h": "staturtop", "ur_v": "staturright",
        "ll_h": "statllbottom", "ll_v": "statllleft", "lr_h": "statlrbottom", "lr_v": "statlrright",
        "top": "stattop", "bottom": "statbottom", "left": "statleft", "right": "statright"}),
    # the minimap's metal rim: its corners run down the sides (drawint.c MULLEFT)
    "map": (MERINTR, {
        "ul_v": "maptreat_ul", "ur_v": "maptreat_ur", "ll_v": "maptreat_ll", "lr_v": "maptreat_lr",
        "top": "maptreat_urepeat", "bottom": "maptreat_brepeat", "left": "maptreat_lrepeat", "right": "maptreat_rrepeat"}),
}

# single pieces: name -> (folder, file, tile, crop box or None)
PIECES = {
    "bkgnd": (CLIENT, "bkgnd", True, None),          # the stone around the original client
    "invbkgnd": (MERINTR, "invbkgnd", True, None),   # darker stone of the inventory pane
    "statbgnd": (MERINTR, "statbgnd", True, None),   # tan stats pane
    "mapbkgnd": (CLIENT, "mapbkgnd", True, None),    # map parchment
    "slot_raised": (MERINTR, "icursor", False, None),  # the inventory cursor: a raised stone square
    "inuse": (MERINTR, "inuse", False, None),          # glow behind an item in use
    "bar_left": (MERINTR, "barleft", False, None),
    "bar_right": (MERINTR, "barright", False, None),
    "bar_top": (MERINTR, "bartop", True, None),
    "bar_bottom": (MERINTR, "barbottom", True, None),
    "view_ul": (CLIENT, "viewtreat_ul", False, None),  # the gargoyle corners of the 3D view
    "view_ur": (CLIENT, "viewtreat_ur", False, None),
    "view_ll": (CLIENT, "viewtreat_ll", False, None),
    "view_lr": (CLIENT, "viewtreat_lr", False, None),
    "view_ul_hi": (CLIENT, "viewtreat_ul_hilight", False, None),
    "view_ur_hi": (CLIENT, "viewtreat_ur_hilight", False, None),
    "view_ll_hi": (CLIENT, "viewtreat_ll_hilight", False, None),
    "view_lr_hi": (CLIENT, "viewtreat_lr_hilight", False, None),
}
# two-state buttons (up | down side by side in one bitmap)
for _tab, _file in (("invent", "statbtn_left_invent"), ("spell", "statbtn_left_spell"), ("skills", "statbtn_left_skills"),
                    ("stats", "statbtn_left_stats"), ("quest", "statbtn_left_quest")):
    PIECES["tab_%s_up" % _tab] = (MERINTR, _file, False, (0, 0, 26, 20))
    PIECES["tab_%s_down" % _tab] = (MERINTR, _file, False, (26, 0, 52, 20))
    # wide tabs: the button's left edge, then the icon without it (its top and bottom bevel match
    # the middle filler's, so it sits anywhere along a stretched button)
    PIECES["tab_%s_icon_up" % _tab] = (MERINTR, _file, False, (4, 0, 26, 20))
    PIECES["tab_%s_icon_down" % _tab] = (MERINTR, _file, False, (30, 0, 52, 20))
PIECES["tab_left_up"] = (MERINTR, "statbtn_left_invent", False, (0, 0, 4, 20))
PIECES["tab_left_down"] = (MERINTR, "statbtn_left_invent", False, (26, 0, 30, 20))
PIECES["tab_mid_up"] = (MERINTR, "statbtn_mid", True, (0, 0, 2, 20))
PIECES["tab_mid_down"] = (MERINTR, "statbtn_mid", True, (2, 0, 4, 20))
PIECES["tab_right_up"] = (MERINTR, "statbtn_right", False, (0, 0, 4, 20))
PIECES["tab_right_down"] = (MERINTR, "statbtn_right", False, (4, 0, 8, 20))
for _btn in ("cast", "map", "stand", "rest"):          # 72x36: two 36x36
    PIECES["btn_%s_up" % _btn] = (MERINTR, _btn, False, (0, 0, 36, 36))
    PIECES["btn_%s_down" % _btn] = (MERINTR, _btn, False, (36, 0, 72, 36))
for _btn in ("drop", "get", "help"):                  # 48x20: two 24x20
    PIECES["btn_%s_up" % _btn] = (MERINTR, _btn, False, (0, 0, 24, 20))
    PIECES["btn_%s_down" % _btn] = (MERINTR, _btn, False, (24, 0, 48, 20))


def find_bmp(folder: Path, stem: str) -> Path:
    for p in folder.iterdir():
        if p.stem.lower() == stem.lower() and p.suffix.lower() == ".bmp":
            return p
    raise FileNotFoundError("%s/%s.bmp" % (folder, stem))


def load_rgba(folder: Path, stem: str, crop=None) -> Image.Image:
    im = Image.open(find_bmp(folder, stem)).convert("RGBA")
    if crop:
        im = im.crop(crop)
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y][:3] == CYAN:
                px[x, y] = (0, 0, 0, 0)
    return im


def derived(pieces: dict[str, Image.Image]) -> None:
    """Pieces made from the original pixels: the recessed slot, the selection frame."""
    raised = pieces["slot_raised"]
    # a Minecraft slot is sunk into the panel: the raised cursor turned half round lights it from
    # the bottom right, then darkened toward the stone of the inventory pane
    sunk = raised.rotate(180)
    dark = Image.new("RGBA", sunk.size, (0, 0, 0, 0))
    dpx, spx = dark.load(), sunk.load()
    for y in range(sunk.height):
        for x in range(sunk.width):
            r, g, b, a = spx[x, y]
            dpx[x, y] = (int(r * 0.62), int(g * 0.62), int(b * 0.62), a)
    pieces["slot"] = dark
    # the selected-slot frame: the stat bar's gold (barleft: dark rim, amber, pale gold)
    w = raised.width
    sel = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    spx = sel.load()
    rings = [(7, 14, 3, 255), (238, 148, 0, 255), (255, 246, 159, 255), (238, 148, 0, 255), (7, 14, 3, 200)]
    for i, c in enumerate(rings):
        for t in range(i, w - i):
            for x, y in ((t, i), (t, w - 1 - i), (i, t), (w - 1 - i, t)):
                spx[x, y] = c
    pieces["slot_selected"] = sel


def map_arrow(size: int) -> Image.Image:
    """An arrowhead pointing up (north), the original map's player blue, outlined in black."""
    from PIL import ImageDraw
    s = size * SCALE
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pts = [(s * 0.5, s * 0.04), (s * 0.92, s * 0.94), (s * 0.5, s * 0.72), (s * 0.08, s * 0.94)]
    d.polygon(pts, fill=(0, 0, 0, 255))
    inner = [(s * 0.5, s * 0.17), (s * 0.80, s * 0.84), (s * 0.5, s * 0.64), (s * 0.20, s * 0.84)]
    d.polygon(inner, fill=(40, 90, 255, 255))
    d.polygon([(s * 0.5, s * 0.17), (s * 0.5, s * 0.64), (s * 0.20, s * 0.84)], fill=(110, 160, 255, 255))
    return im


def fill_transparent(im: Image.Image) -> Image.Image:
    """RGB with every transparent pixel set to its nearest opaque colour (grown outward)."""
    rgb = im.convert("RGB")
    alpha = im.getchannel("A")
    if alpha.getextrema()[0] == 255:
        return rgb
    filled = Image.new("RGB", im.size, (0, 0, 0))
    filled.paste(rgb, mask=alpha)
    known = alpha.point(lambda v: 255 if v else 0)
    for _ in range(max(im.size)):
        if known.getextrema()[0] == 255:
            break
        grown = filled.filter(ImageFilter.MaxFilter(3))
        grown_mask = known.filter(ImageFilter.MaxFilter(3))
        # only take grown colours where we had none
        new = Image.composite(filled, grown, known)
        filled, known = new, grown_mask
    return filled


def upscale_alpha(alpha: Image.Image) -> Image.Image:
    big = alpha.resize((alpha.width * SCALE, alpha.height * SCALE), Image.Resampling.LANCZOS)
    big = big.filter(ImageFilter.GaussianBlur(SCALE * 0.35))
    return big.point(lambda v: 0 if v < 96 else (255 if v > 160 else int((v - 96) * 255 / 64)))


def ai_upscale(images: dict[str, tuple[Image.Image, bool]], model: str) -> dict[str, Image.Image]:
    """Run the model over every piece in one process. Tiles are wrap-padded by 8 px."""
    weights = MODELS / ("%s.safetensors" % model)
    if not VENV_PY.exists() or not weights.exists():
        raise SystemExit("AI upscale needs build/texai (tools/textures/setup_ai.ps1) and %s" % weights)
    pad = 8
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = Path(tmp) / "in", Path(tmp) / "out"
        src.mkdir()
        for name, (im, tile) in images.items():
            rgb = fill_transparent(im)
            if tile:
                padded = Image.new("RGB", (rgb.width + 2 * pad, rgb.height + 2 * pad))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        padded.paste(rgb, (pad + dx * rgb.width, pad + dy * rgb.height))
                rgb = padded
            else:
                # edge-extend so the model sees no hard border
                rgb = rgb.crop((-pad, -pad, rgb.width + pad, rgb.height + pad))
                rgb = fill_edge(rgb, pad)
            rgb.save(src / ("%s.png" % name))
        subprocess.run([str(VENV_PY), str(ROOT / "tools" / "textures" / "upscale.py"), "--model", str(weights), str(src), str(dst)],
                       check=True)
        out = {}
        for name, (im, tile) in images.items():
            big = Image.open(dst / ("%s.png" % name)).convert("RGB")
            p = pad * SCALE
            big = big.crop((p, p, big.width - p, big.height - p))
            out[name] = big
        return out


def fill_edge(im: Image.Image, pad: int) -> Image.Image:
    """Replicate the inner border into the pad (crop() leaves it black)."""
    w, h = im.size
    inner = im.crop((pad, pad, w - pad, h - pad))
    out = inner.resize((w, h), Image.Resampling.NEAREST)  # rough fill
    out.paste(inner, (pad, pad))
    return out


def build(variant: str, cfg: dict) -> None:
    pieces: dict[str, tuple[Image.Image, bool]] = {}
    manifest = {"scale": SCALE, "pieces": {}, "frames": {}}
    for name, (folder, stem, tile, crop) in PIECES.items():
        pieces[name] = (load_rgba(folder, stem, crop), tile)
    for frame, (folder, slots) in FRAMES.items():
        manifest["frames"][frame] = {}
        for slot, stem in slots.items():
            name = "%s_%s" % (frame, slot)
            pieces[name] = (load_rgba(folder, stem), slot in ("top", "bottom", "left", "right"))
            manifest["frames"][frame][slot] = name
    # "inset": the inventory pane's bevel in the stone's grey instead of its brown (child windows)
    manifest["frames"]["inset"] = {}
    for slot, name in list(manifest["frames"]["inv"].items()):
        im, tile = pieces[name]
        grey = ImageOps.grayscale(im).point(lambda v: min(255, int(v * 1.15))).convert("RGBA")
        grey.putalpha(im.getchannel("A"))
        pieces["inset_" + slot] = (grey, tile)
        manifest["frames"]["inset"][slot] = "inset_" + slot
    plain = {n: im for n, (im, _) in pieces.items()}
    derived(plain)
    for n in ("slot", "slot_selected"):
        pieces[n] = (plain[n], False)

    out = OUT / variant
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    if variant == "nearest":
        big = {n: im.resize((im.width * SCALE, im.height * SCALE), Image.Resampling.NEAREST) for n, (im, _) in pieces.items()}
    else:
        rgb = ai_upscale(pieces, cfg["model"])
        big = {}
        for n, (im, _) in pieces.items():
            a = upscale_alpha(im.getchannel("A"))
            b = rgb[n].convert("RGBA")
            b.putalpha(a)
            big[n] = b
    for n, im in big.items():
        im.save(out / ("T_UI_%s.png" % n))
        src = pieces[n][0]
        manifest["pieces"][n] = {"w": src.width, "h": src.height, "tile": pieces[n][1]}
    # the minimap's player arrow, drawn at full resolution (no original bitmap: the original
    # client draws the player as a blue dot with a line, map.c)
    arrow = map_arrow(12)
    arrow.save(out / "T_UI_map_arrow.png")
    manifest["pieces"]["map_arrow"] = {"w": 12, "h": 12, "tile": False}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))
    print("%s: %d pieces -> %s" % (variant, len(big), out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", action="append", help="build only these variants (default: all in ui_style.json)")
    a = ap.parse_args()
    style = json.loads(STYLE.read_text(encoding="utf-8"))
    variants = style["art_variants"]
    for v in a.variant or list(variants):
        build(v, variants[v])


if __name__ == "__main__":
    main()
