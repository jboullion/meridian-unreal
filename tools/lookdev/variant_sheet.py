"""A sheet comparing look-dev labels side by side, each image captioned with its GPU time from the
label's -Profile run (profile.log, the "full" variant), and the mean per label in the header.

    python tools/lookdev/variant_sheet.py v_epic v_high v_medium --cams north_gate,int_inn --out build/lookdev/lighting.png
"""
import argparse
import re
import statistics
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
LOOKDEV = ROOT / "build" / "lookdev"
LINE = re.compile(r"camera=(\S+) variant=full game=[\d.]+ms render=[\d.]+ms gpu=([\d.]+)ms")


def gpu_times(label):
    log = LOOKDEV / label / "profile.log"
    if not log.exists():
        return {}
    return {m.group(1): float(m.group(2)) for m in LINE.finditer(log.read_text(encoding="utf-8-sig", errors="replace"))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("labels", nargs="+")
    ap.add_argument("--cams", required=True, help="comma-separated camera names")
    ap.add_argument("--names", default="", help="|-separated column titles (default: the labels)")
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    cams = args.cams.split(",")
    names = args.names.split("|") if args.names else args.labels
    w = args.width
    h = w * 9 // 16
    head, gap = 44, 6
    try:
        font = ImageFont.truetype("arial.ttf", 15)
        small = ImageFont.truetype("arial.ttf", 13)
    except OSError:
        font = small = ImageFont.load_default()
    sheet = Image.new("RGB", (len(args.labels) * (w + gap), head + len(cams) * (h + gap)), (20, 20, 22))
    draw = ImageDraw.Draw(sheet)
    for c, (label, name) in enumerate(zip(args.labels, names)):
        times = gpu_times(label)
        x = c * (w + gap)
        mean = statistics.fmean(times[k] for k in cams if k in times) if any(k in times for k in cams) else None
        draw.text((x + 4, 4), name, fill=(255, 255, 255), font=font)
        draw.text((x + 4, 24), "GPU mean %.1f ms" % mean if mean is not None else "(no profile)", fill=(255, 210, 90), font=small)
        for r, cam in enumerate(cams):
            path = LOOKDEV / label / (cam + ".png")
            y = head + r * (h + gap)
            if path.exists():
                sheet.paste(Image.open(path).convert("RGB").resize((w, h)), (x, y))
            caption = "%s  %.1f ms" % (cam, times[cam]) if cam in times else cam
            draw.rectangle((x, y, x + 8 + int(draw.textlength(caption, font=small)), y + 18), fill=(0, 0, 0))
            draw.text((x + 4, y + 2), caption, fill=(255, 255, 255), font=small)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
