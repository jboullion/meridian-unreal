"""
A review sheet of the procedural trees beside their original sprites, all at one scale (100 px per
metre): build/environment/tree_review/sheet.png. Renders first: tools/blender/render_tree_kit.py.

    python tools/blender/tree_sheet.py [Name ...]
"""
import glob
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REVIEW = os.path.join(REPO, "build", "environment", "tree_review")
sys.path.insert(0, os.path.join(REPO, "tools", "textures"))
from make_tree_textures import TREES  # noqa: E402

PX_PER_M = 100
M_PER_TEXEL = 0.034358  # metres per sprite texel at shrink 1 (midtree2: 4.92 m from 716 px at shrink 5)
BG = (64, 72, 82)


def sprite(bgf):
    im = Image.open(os.path.join(REPO, "build", "bgf", bgf, "frame_00.png")).convert("RGBA")
    im = im.crop(im.getchannel("A").getbbox())
    shrink = json.load(open(os.path.join(REPO, "build", "bgf", bgf, "meta.json")))["shrink"]
    k = M_PER_TEXEL / shrink * PX_PER_M
    return im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.Resampling.LANCZOS)


def main():
    only = sys.argv[1:]
    try:
        font = ImageFont.truetype("arial.ttf", 22)
    except OSError:
        font = None
    rows = []
    for name, cfg in TREES.items():
        if only and name not in only:
            continue
        renders = [Image.open(p) for p in sorted(glob.glob(os.path.join(REVIEW, "SM_Tree_%s_*.png" % name)))
                   if re.fullmatch(r"SM_Tree_%s_[A-Z]\.png" % name, os.path.basename(p))]
        if renders:
            rows.append((name, cfg["bgf"], [sprite(cfg["bgf"])] + renders))
    # pack rows of tiles left to right, wrapping at a fixed width
    width, pad, label = 3600, 16, 30
    placed, x, y, row_h = [], pad, pad, 0
    for name, bgf, tiles in rows:
        group_w = sum(t.width for t in tiles) + pad * (len(tiles) - 1)
        group_h = max(t.height for t in tiles) + label
        if x + group_w > width - pad and x > pad:
            x, y, row_h = pad, y + row_h + pad * 2, 0
        placed.append((x, y, name, bgf, tiles, group_h))
        x += group_w + pad * 4
        row_h = max(row_h, group_h)
    sheet = Image.new("RGB", (width, y + row_h + pad), BG)
    d = ImageDraw.Draw(sheet)
    for x, y, name, bgf, tiles, group_h in placed:
        tx = x
        for t in tiles:
            sheet.paste(t, (tx, y + group_h - label - t.height), t)
            tx += t.width + pad
        d.text((x, y + group_h - label + 4), "%s (%s): sprite, %d variants" % (name, bgf, len(tiles) - 1),
               fill=(235, 235, 235), font=font)
    out = os.path.join(REVIEW, "sheet.png")
    sheet.save(out)
    print("tree_sheet: %s (%d trees)" % (out, len(placed)))


if __name__ == "__main__":
    main()
