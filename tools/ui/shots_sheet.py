"""
Contact sheet of the UI screenshots (tools/ue/run_ui_shots.ps1): every NN_<name>.png in a folder,
labelled, three across, into <folder>/sheet.png.

    python tools/ui/shots_sheet.py build/ui/shots/<label>
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main():
    folder = Path(sys.argv[1])
    shots = sorted(p for p in folder.glob("*.png") if p.name != "sheet.png")
    if not shots:
        sys.exit("no screenshots in %s" % folder)
    cols, w = 3, 960
    first = Image.open(shots[0])
    h = round(first.height * w / first.width)
    label = 28
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows * (h + label)), (16, 16, 18))
    d = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=20)
    for i, p in enumerate(shots):
        x, y = (i % cols) * w, (i // cols) * (h + label)
        sheet.paste(Image.open(p).convert("RGB").resize((w, h), Image.Resampling.LANCZOS), (x, y + label))
        d.text((x + 8, y + 3), p.stem, fill=(255, 236, 170), font=font)
    out = folder / "sheet.png"
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
