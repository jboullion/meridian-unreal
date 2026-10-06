"""Contact sheets for the aigen review steps: labelled columns, each image scaled to one height."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

BG = (34, 36, 42)
CHECKER = ((70, 72, 78), (90, 92, 98))
LABEL_H = 40
GAP = 16


def _font(size: int):
    try:
        return ImageFont.truetype("arial.ttf", size)
    except OSError:
        return ImageFont.load_default()


def _flatten(img: Image.Image, cell: int = 16) -> Image.Image:
    """RGBA over a checkerboard, so the cut-out's edges are visible."""
    if img.mode != "RGBA":
        return img.convert("RGB")
    board = Image.new("RGB", img.size, CHECKER[0])
    d = ImageDraw.Draw(board)
    for y in range(0, img.height, cell):
        for x in range((y // cell) % 2 * cell, img.width, 2 * cell):
            d.rectangle((x, y, x + cell - 1, y + cell - 1), fill=CHECKER[1])
    board.paste(img, (0, 0), img)
    return board


def sheet(columns: list, out: Path, height: int = 640, title: str = "", nearest_below: int = 256) -> Path:
    """columns: [(label, PIL image)]. Images smaller than `nearest_below` px are scaled with nearest
    neighbour so the original pixels stay readable."""
    cells = []
    for label, img in columns:
        s = height / img.height
        method = Image.Resampling.NEAREST if img.height < nearest_below else Image.Resampling.LANCZOS
        im = _flatten(img).resize((max(1, round(img.width * s)), height), method)
        cells.append((label, im))
    title_h = LABEL_H if title else 0
    w = sum(im.width for _, im in cells) + GAP * (len(cells) + 1)
    canvas = Image.new("RGB", (w, height + LABEL_H + title_h + 2 * GAP), BG)
    d = ImageDraw.Draw(canvas)
    if title:
        d.text((GAP, GAP // 2), title, fill=(230, 230, 230), font=_font(26))
    x = GAP
    for label, im in cells:
        canvas.paste(im, (x, title_h + GAP))
        d.text((x, title_h + GAP + height + 6), label, fill=(220, 220, 220), font=_font(22))
        x += im.width + GAP
    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out)
    return out


def grid(rows: list, out: Path, height: int = 400, title: str = "") -> Path:
    """rows: [(row label, [(label, image)])] -> one sheet per row stacked vertically."""
    parts = [Image.open(sheet(cols, out.with_suffix(".row%d.png" % i), height, "%s" % lbl)) for i, (lbl, cols) in enumerate(rows)]
    w = max(p.width for p in parts)
    title_h = LABEL_H if title else 0
    canvas = Image.new("RGB", (w, sum(p.height for p in parts) + title_h), BG)
    if title:
        ImageDraw.Draw(canvas).text((GAP, 6), title, fill=(240, 240, 240), font=_font(28))
    y = title_h
    for i, p in enumerate(parts):
        canvas.paste(p, (0, y))
        y += p.height
        out.with_suffix(".row%d.png" % i).unlink()
    canvas.save(out)
    return out
