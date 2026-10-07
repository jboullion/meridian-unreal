"""
Review sheet for the UI art variants (docs/adr/0009-user-interface.md): the same mock panel
(stone, frame, slot grid with icons, hotbar, stat bars) drawn with each variant's pieces, the
way the game draws them (SMRFrame: corner strips plus tiled repeaters), at the in-game size for
1080p (ui_scale in data/ui/ui_style.json).

    python tools/ui/review_ui_art.py            # -> build/ui/review/ui_art_variants.png
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "build" / "ui" / "art"
ICONS = ROOT / "build" / "ui" / "icons"
STYLE = ROOT / "data" / "ui" / "ui_style.json"
OUT = ROOT / "build" / "ui" / "review"

SAMPLE_ICONS = ["sword", "metlshld", "helm", "chainamr", "ruby", "diamond", "apple", "mushroom", "ifirebal",
                "iblind", "isecwind", "bow", "wand", "gauntlet", "scimitar", "mace", "herb", "emerald"]


class Kit:
    def __init__(self, variant: str, scale: float):
        self.dir = ART / variant
        self.icons = ICONS / variant
        self.man = json.loads((self.dir / "manifest.json").read_text())
        self.k = scale / self.man["scale"]  # art texels -> output pixels
        self.scale = scale
        self.cache: dict[str, Image.Image] = {}

    def piece(self, name: str) -> Image.Image:
        if name not in self.cache:
            im = Image.open(self.dir / ("T_UI_%s.png" % name)).convert("RGBA")
            w, h = max(1, round(im.width * self.k)), max(1, round(im.height * self.k))
            self.cache[name] = im.resize((w, h), Image.Resampling.LANCZOS if self.k < 1 else Image.Resampling.NEAREST)
        return self.cache[name]

    def icon(self, name: str, size: int) -> Image.Image | None:
        p = self.icons / ("T_Icon_%s.png" % name)
        if not p.exists():
            return None
        return Image.open(p).convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)

    def tile(self, dst: Image.Image, name: str, box) -> None:
        t = self.piece(name)
        x0, y0, x1, y1 = box
        layer = Image.new("RGBA", (x1 - x0, y1 - y0))
        for y in range(0, y1 - y0, t.height):
            for x in range(0, x1 - x0, t.width):
                layer.alpha_composite(t, (x, y))
        dst.alpha_composite(layer, (x0, y0))

    def frame(self, dst: Image.Image, frame: str, box) -> None:
        """drawint.c's layout: repeaters between the corner strips, corners on top."""
        f = self.man["frames"][frame]
        x0, y0, x1, y1 = box

        def p(slot):
            return self.piece(f[slot]) if slot in f else Image.new("RGBA", (0, 0))
        ulh, ulv, urh, urv = p("ul_h"), p("ul_v"), p("ur_h"), p("ur_v")
        llh, llv, lrh, lrv = p("ll_h"), p("ll_v"), p("lr_h"), p("lr_v")
        top, bot, lef, rig = p("top"), p("bottom"), p("left"), p("right")
        # repeaters
        self.tile(dst, f["top"], (x0 + max(ulh.width, ulv.width), y0, x1 - max(urh.width, urv.width), y0 + top.height))
        self.tile(dst, f["bottom"], (x0 + max(llh.width, llv.width), y1 - bot.height, x1 - max(lrh.width, lrv.width), y1))
        self.tile(dst, f["left"], (x0, y0 + ulh.height + ulv.height, x0 + lef.width, y1 - llh.height - llv.height))
        self.tile(dst, f["right"], (x1 - rig.width, y0 + urh.height + urv.height, x1, y1 - lrh.height - lrv.height))
        # corner strips
        dst.alpha_composite(ulh, (x0, y0))
        dst.alpha_composite(ulv, (x0, y0 + ulh.height))
        dst.alpha_composite(urh, (x1 - urh.width, y0))
        dst.alpha_composite(urv, (x1 - urv.width, y0 + urh.height))
        dst.alpha_composite(llh, (x0, y1 - llh.height))
        dst.alpha_composite(llv, (x0, y1 - llh.height - llv.height))
        dst.alpha_composite(lrh, (x1 - lrh.width, y1 - lrh.height))
        dst.alpha_composite(lrv, (x1 - lrv.width, y1 - lrh.height - lrv.height))


def mock(kit: Kit, title: str) -> Image.Image:
    s = kit.scale
    W, H = int(330 * s), int(250 * s)
    im = Image.new("RGBA", (W, H), (30, 30, 34, 255))
    d = ImageDraw.Draw(im)
    font = ImageFont.load_default(size=int(9 * s))
    # dialog: stone, edge frame, gargoyle corners
    kit.tile(im, "bkgnd", (0, 0, W, H))
    kit.frame(im, "edge", (0, 0, W, H))
    d.text((int(16 * s), int(14 * s)), title, fill=(255, 236, 170, 255), font=font)
    # tabs
    x = int(16 * s)
    for i, t in enumerate(("invent", "spell", "skills", "stats", "quest")):
        im.alpha_composite(kit.piece("tab_%s_%s" % (t, "down" if i == 0 else "up")), (x, int(28 * s)))
        x += kit.piece("tab_invent_up").width + int(2 * s)
    # bag grid 9 x 3 in an "inv" frame on the darker stone
    slot = int(20 * s)
    gx, gy = int(16 * s), int(56 * s)
    kit.tile(im, "invbkgnd", (gx - 4, gy - 4, gx + 9 * slot + 4, gy + 3 * slot + 4))
    kit.frame(im, "inv", (gx - 4, gy - 4, gx + 9 * slot + 4, gy + 3 * slot + 4))
    n = 0
    for r in range(3):
        for c in range(9):
            sp = kit.piece("slot").resize((slot, slot), Image.Resampling.LANCZOS)
            im.alpha_composite(sp, (gx + c * slot, gy + r * slot))
            ic = kit.icon(SAMPLE_ICONS[n % len(SAMPLE_ICONS)], int(slot * 0.8)) if (r * 9 + c) % 3 != 2 else None
            if ic:
                im.alpha_composite(ic, (gx + c * slot + int(slot * 0.1), gy + r * slot + int(slot * 0.1)))
                n += 1
    # hotbar with a selected slot
    hy = gy + 3 * slot + int(14 * s)
    for c in range(9):
        im.alpha_composite(kit.piece("slot").resize((slot, slot), Image.Resampling.LANCZOS), (gx + c * slot, hy))
        ic = kit.icon(SAMPLE_ICONS[(c + 5) % len(SAMPLE_ICONS)], int(slot * 0.8))
        if ic:
            im.alpha_composite(ic, (gx + c * slot + int(slot * 0.1), hy + int(slot * 0.1)))
    sel = kit.piece("slot_selected").resize((slot + 4, slot + 4), Image.Resampling.LANCZOS)
    im.alpha_composite(sel, (gx + 2 * slot - 2, hy - 2))
    # stat bars: bar_left + fill + bar_right, top/bottom repeaters
    by = hy + slot + int(12 * s)
    for i, (col, frac) in enumerate((((200, 30, 30), 0.8), ((40, 90, 220), 0.55), ((230, 180, 30), 0.9))):
        bx0, bx1 = gx + int(6 * s), gx + int(6 * s) + int(120 * s)
        yy = by + i * int(14 * s)
        h = kit.piece("bar_left").height
        d.rectangle((bx0, yy, bx0 + int((bx1 - bx0) * frac), yy + h - 1), fill=col + (255,))
        d.rectangle((bx0 + int((bx1 - bx0) * frac), yy, bx1, yy + h - 1), fill=(20, 20, 20, 255))
        top = kit.piece("bar_top")
        kit.tile(im, "bar_top", (bx0, yy, bx1, yy + top.height))
        kit.tile(im, "bar_bottom", (bx0, yy + h - kit.piece("bar_bottom").height, bx1, yy + h))
        im.alpha_composite(kit.piece("bar_left"), (bx0 - kit.piece("bar_left").width, yy))
        im.alpha_composite(kit.piece("bar_right"), (bx1, yy))
    # minimap rim on parchment
    mx0, my0 = gx + int(190 * s), hy + slot + int(8 * s)
    mx1, my1 = mx0 + int(90 * s), my0 + int(52 * s)
    kit.tile(im, "mapbkgnd", (mx0, my0, mx1, my1))
    kit.frame(im, "map", (mx0, my0, mx1, my1))
    # view corner ornaments on the dialog
    im.alpha_composite(kit.piece("view_ul"), (0, 0))
    im.alpha_composite(kit.piece("view_lr"), (W - kit.piece("view_lr").width, H - kit.piece("view_lr").height))
    return im


def main():
    style = json.loads(STYLE.read_text(encoding="utf-8"))
    scale = float(style.get("ui_scale", 2.0))
    variants = [v for v in style["art_variants"] if (ART / v / "manifest.json").exists()]
    if not variants:
        sys.exit("no art: run tools/ui/build_ui_art.py")
    panels = [mock(Kit(v, scale), v) for v in variants]
    pad = 16
    W = sum(p.width for p in panels) + pad * (len(panels) + 1)
    H = max(p.height for p in panels) + 2 * pad
    sheet = Image.new("RGBA", (W, H), (18, 18, 20, 255))
    x = pad
    for p in panels:
        sheet.alpha_composite(p, (x, pad))
        x += p.width + pad
    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / "ui_art_variants.png"
    sheet.convert("RGB").save(out)
    # a close-up at 2x so the upscale differences show
    crops = [p.crop((0, 0, p.width // 2, p.height // 2)).resize((p.width, p.height), Image.Resampling.NEAREST) for p in panels]
    zoom = Image.new("RGBA", (W, H), (18, 18, 20, 255))
    x = pad
    for c in crops:
        zoom.alpha_composite(c, (x, pad))
        x += c.width + pad
    zoom.convert("RGB").save(OUT / "ui_art_variants_zoom.png")
    print(out)


if __name__ == "__main__":
    main()
