"""
Side-by-side look-dev comparison sheets (docs/adr/0003).

    python tools/lookdev/compare.py baseline mood1             # every camera both labels have
    python tools/lookdev/compare.py baseline mood1 hall        # three columns
    python tools/lookdev/compare.py baseline mood1 --only hall_south,hall_front

Reads build/lookdev/<label>/<camera>.png (written by tools/ue/run_lookdev.ps1) and writes
build/lookdev/compare_<a>_<b>[_<c>...].png: one row per camera, one column per label.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
LOOKDEV = ROOT / "build" / "lookdev"
CELL_W = 800  # each shot is scaled to this width


def font(size: int):
    try:
        return ImageFont.truetype("arial.ttf", size)
    except OSError:
        return ImageFont.load_default()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("labels", nargs="+")
    ap.add_argument("--only", default="", help="comma-separated camera names")
    args = ap.parse_args()

    dirs = [LOOKDEV / label for label in args.labels]
    for d in dirs:
        if not d.is_dir():
            raise SystemExit("missing %s (run tools/ue/run_lookdev.ps1 -Label %s)" % (d, d.name))
    cams = sorted(set.intersection(*({p.stem for p in d.glob("*.png")} for d in dirs)))
    if args.only:
        wanted = args.only.split(",")
        cams = [c for c in cams if c in wanted]
    if not cams:
        raise SystemExit("no camera has a shot in every label")

    first = Image.open(dirs[0] / (cams[0] + ".png"))
    cell_h = round(first.height * CELL_W / first.width)
    head, pad, side = 44, 6, 190
    sheet = Image.new("RGB", (side + len(dirs) * (CELL_W + pad), head + len(cams) * (cell_h + pad)), (24, 24, 28))
    draw = ImageDraw.Draw(sheet)
    for i, d in enumerate(dirs):
        draw.text((side + i * (CELL_W + pad) + 8, 10), d.name, fill=(235, 235, 235), font=font(24))
    for r, cam in enumerate(cams):
        y = head + r * (cell_h + pad)
        draw.text((10, y + 8), cam, fill=(200, 200, 200), font=font(18))
        for i, d in enumerate(dirs):
            img = Image.open(d / (cam + ".png")).convert("RGB").resize((CELL_W, cell_h), Image.LANCZOS)
            sheet.paste(img, (side + i * (CELL_W + pad), y))

    out = LOOKDEV / ("compare_%s.png" % "_".join(args.labels))
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
