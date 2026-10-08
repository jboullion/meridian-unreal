"""
Review sheets for the equipment on sprite players (docs/adr/0012 M2b): every torso, weapon, shield and
helmet in data/sprites/equipment.json, drawn the way the runtime places them (player.kod SendOverlays:
a weapon bends the right arm to group 17 and rests on group 4, a shield or bow bends the left arm to
group 7 and rests on group 2, a helmet sits on the hair's hotspot).

    build/texai/.venv/Scripts/python tools/sprites/equipment_sheet.py [--methods nearest scale4x gtav_dither]

Output (build/sprites/equipment/):
    outfits.png    rows = every torso (shirts, robes, leather, scale, chain, plate, nerudite), male and
                   female, each with a weapon, a shield and a helmet in turn; columns = four views as
                   the original drew them (nearest), then the same with the upscale data/sprites/
                   upscale.json picks per part (what the game shows)
    items.png      every weapon, shield, bow and helmet on its own, front view, under each method:
                   the "other" role's choice (weapons, shields, hats) is made from this one
    methods.png    whole outfits (shirt; plate, sword, shield and helm; leather and shield) under the
                   original pixels, scale4x and gtav_dither, at the world's render density
                   (--methods-only writes just this one)
    faces.png      faces at their own pixels (finer than the body's) against snapped to the torso's
                   pixel grid, sharp or averaged (--faces)
"""
from __future__ import annotations

import argparse
import dataclasses
import json

from PIL import Image, ImageDraw

import composite as cp
import face_sheet as fs
import m59sprites as ms

OUT = ms.ROOT / "build" / "sprites" / "equipment"
SCALE = 3
OUTFIT_SCALE = 2
VIEWS = [0, ms.NUMDEGREES // 8, ms.NUMDEGREES // 4, ms.NUMDEGREES // 2]   # front, three-quarter, side, back
BG = (48, 52, 60, 255)
# helmets that take the hair off (Send(poOwner,@RemoveHair) in their Kod)
HAIR_OFF = {"helm", "dhelma", "khelm", "legionhelm", "indianhat", "pilgrimhat", "mummymsk", "pumpkin", "shrnkmsk"}


def label(im: Image.Image, text: str) -> Image.Image:
    out = Image.new("RGBA", (im.width, im.height + 14), BG)
    out.alpha_composite(im, (0, 14))
    ImageDraw.Draw(out).text((3, 1), text, fill=(230, 225, 210, 255))
    return out


def dress(look: ms.Look, kind: str, bgf: str) -> tuple[ms.Look, dict]:
    """The look wearing one piece, and the groups its parts rest on."""
    groups = {}
    if kind in ("body", "left_arm", "right_arm", "legs"):
        return dataclasses.replace(look, **{kind: bgf}), groups
    if kind == "weapon":
        groups.update(right_arm=17, weapon=4)
        return dataclasses.replace(look, weapon=bgf), groups
    if kind in ("shield", "bow"):
        groups.update(left_arm=7)
        return dataclasses.replace(look, equipment=[{"bgf": bgf, "hotspot": 32 if kind == "shield" else 33, "group": 2}]), groups
    hair = None if any(bgf.startswith(h) for h in HAIR_OFF) else look.hair
    return dataclasses.replace(look, hair=hair, equipment=[{"bgf": bgf, "hotspot": 13, "group": 1}]), groups


def draw(look: ms.Look, groups: dict, angle: int, source, canvas) -> Image.Image:
    """The look in a fixed cell (canvas: half width, height above the feet, below, in base pixels),
    its feet at the same place in every cell."""
    half, above, below = canvas
    cell = Image.new("RGBA", (2 * half * OUTFIT_SCALE, (above + below) * OUTFIT_SCALE), (0, 0, 0, 0))
    c = ms.composite(*look.layers(groups), angle, scale=OUTFIT_SCALE, source=source)
    if c:
        cell.alpha_composite(c.image, (round(half * OUTFIT_SCALE - c.center_x), round(above * OUTFIT_SCALE - c.feet_y)))
    return cell


def outfits(parts: dict, looks: dict) -> Image.Image:
    torsos = sorted(b for b, d in parts.items() if d["kind"] == "body")
    extras = {k: sorted(b for b, d in parts.items() if d["kind"] == k) for k in ("weapon", "shield", "helmet")}
    canvas = (90, 250, 6)   # half width, above the feet, below (base pixels)
    rows = []
    for i, torso in enumerate(torsos):
        gender = parts[torso].get("gender", "male")
        base = ms.Look.from_json(looks[f"player_{gender}"])
        look, groups = dress(base, "body", torso)
        # one more piece per row, in turn: a weapon, a shield, a helmet
        kind = ("weapon", "shield", "helmet")[i % 3]
        pool = extras[kind]
        if pool:
            piece = pool[(i // 3) % len(pool)]
            look, more = dress(look, kind, piece)
            groups.update(more)
            name = f"{torso} + {piece}"
        else:
            name = torso
        cells = [draw(look, groups, a, None, canvas) for a in VIEWS] + [draw(look, groups, a, fs.source(None), canvas) for a in VIEWS]
        row = Image.new("RGBA", (sum(c.width for c in cells) + 12, cells[0].height), BG)
        x = 0
        for j, c in enumerate(cells):
            row.alpha_composite(c, (x + (12 if j >= len(VIEWS) else 0), 0))
            x += c.width
        rows.append(label(row, f"{name}   (left: original pixels, right: as the game upscales them)"))
    return stack(rows)


def items(parts: dict, methods: list[str]) -> Image.Image:
    rows = []
    for kind in ("weapon", "shield", "bow", "helmet"):
        for bgf in sorted(b for b, d in parts.items() if d["kind"] == kind):
            b = ms.load_bgf(bgf)
            group = 4 if kind == "weapon" else 2 if kind in ("shield", "bow") else 1
            bi = ms.bitmap_index(bgf, min(group, len(b.groups)) - 1, 0)
            if bi is None:
                continue
            cells = []
            for m in methods:
                im = fs.source(m)(bgf, bi, 0) if m != "nearest" else None
                if im is None:
                    im = ms.bitmap_rgba(bgf, bi, 0)
                bm = b.bitmaps[bi]
                # the size it has on a body: the torso's pixels (shrink 4) x SCALE
                k = SCALE * 4.0 / b.shrink
                k = min(k, 360.0 / max(bm.w, bm.h))   # (a few, like mmrednose at shrink 1, would fill the sheet)
                size = (max(1, round(bm.w * k)), max(1, round(bm.h * k)))
                cells.append(im.resize(size, Image.LANCZOS if m != "nearest" or k < 1 else Image.NEAREST))
            h = max(c.height for c in cells)
            w = max(c.width for c in cells)
            row = Image.new("RGBA", ((w + 8) * len(cells), h), BG)
            for j, c in enumerate(cells):
                row.alpha_composite(c, (j * (w + 8), h - c.height))
            rows.append(label(row, f"{kind} {bgf}: " + " | ".join(methods)))
    # several columns of rows, so the sheet stays viewable
    cols = 4
    per = -(-len(rows) // cols)
    columns = [stack(rows[i * per:(i + 1) * per]) for i in range(cols)]
    out = Image.new("RGBA", (sum(c.width + 16 for c in columns), max(c.height for c in columns)), BG)
    x = 0
    for c in columns:
        out.alpha_composite(c, (x, 0))
        x += c.width + 16
    return out


def stack(rows: list[Image.Image]) -> Image.Image:
    w = max(r.width for r in rows) if rows else 1
    h = sum(r.height + 4 for r in rows) if rows else 1
    out = Image.new("RGBA", (w, h), BG)
    y = 0
    for r in rows:
        out.alpha_composite(r, (0, y))
        y += r.height + 4
    return out


def methods(looks: dict) -> Image.Image:
    """Whole outfits under each upscale: rows = the original pixels, scale4x, gtav_dither; columns =
    outfits seen front and three-quarter, at the render target's density in the world (4 texels per
    torso pixel, as close up). Every part uses the row's method."""
    male, female = (ms.Look.from_json(looks[f"player_{g}"]) for g in ("male", "female"))
    outfits = [("shirt", male, [])]
    look, groups = dress(male, "body", "bte")
    look, more = dress(look, "weapon", "swordov"); groups.update(more)
    look, more = dress(look, "shield", "metlshld"); groups.update(more)
    look, more = dress(look, "helmet", "helm"); groups.update(more)
    outfits.append(("plate, sword, shield, helm", look, groups))
    look, groups = dress(female, "body", "bth")
    look, more = dress(look, "shield", "gshe"); groups.update(more)
    outfits.append(("leather, shield", look, groups))
    rows = []
    global OUTFIT_SCALE
    keep, OUTFIT_SCALE = OUTFIT_SCALE, 4
    for label_, method in (("original pixels", None), ("scale4x", "scale4x"), ("gtav_dither", "gtav_dither")):
        cells = []
        for name, look, groups in outfits:
            for a in (0, ms.NUMDEGREES // 8):
                cells.append(draw(look, groups if isinstance(groups, dict) else {}, a, fs.source(method), (75, 240, 4)))
        row = Image.new("RGBA", (sum(c.width for c in cells), cells[0].height), BG)
        x = 0
        for c in cells:
            row.alpha_composite(c, (x, 0))
            x += c.width
        rows.append(label(row, label_))
    OUTFIT_SCALE = keep
    return stack(rows)


def faces(looks: dict) -> Image.Image:
    """Faces against the body's pixels: the face parts (head shrink 7, eyes, mouth, hair 14) are
    finer than the torso (4). Rows: as stored (each part at its own pixels), every part snapped to
    the torso's pixel grid point-sampled (what the original's screen showed at one screen pixel per
    torso pixel), and snapped averaged. Columns: looks front and three-quarter, then the heads x2."""
    male, female = (ms.Look.from_json(looks[f"player_{g}"]) for g in ("male", "female"))
    helmed, _ = dress(male, "helmet", "helm")
    looks_ = [male, female, dataclasses.replace(female, hair="ptbc"), helmed]
    def own(look, a):
        return draw(look, {}, a, None, (75, 240, 4))
    def grid(look, a, average):
        c = ms.composite(*look.layers({}), a, scale=1, source=(lambda n, i, x: ms.bitmap_rgba(n, i, x)) if average else None)
        cell = Image.new("RGBA", (150, 244), (0, 0, 0, 0))
        if c:
            cell.alpha_composite(c.image, (round(75 - c.center_x), round(240 - c.feet_y)))
        return cell.resize((cell.width * OUTFIT_SCALE, cell.height * OUTFIT_SCALE), Image.NEAREST)
    rows = []
    global OUTFIT_SCALE
    keep, OUTFIT_SCALE = OUTFIT_SCALE, 4
    for label_, fn in (("as stored: each part at its own pixels (the face finer than the body)", own),
                       ("snapped to the torso's pixels, point-sampled (sharp)", lambda l, a: grid(l, a, False)),
                       ("snapped to the torso's pixels, averaged (soft)", lambda l, a: grid(l, a, True))):
        cells = [fn(l, a) for l in looks_ for a in (0, ms.NUMDEGREES // 8)]
        heads = [c.crop((c.width // 2 - 110, 20 * 4, c.width // 2 + 110, 80 * 4)).resize((440, 480), Image.NEAREST) for c in cells[::2]]
        row = Image.new("RGBA", (sum(c.width for c in cells) + sum(h.width for h in heads) + 20, max(cells[0].height, 480)), BG)
        x = 0
        for c in cells + heads:
            row.alpha_composite(c, (x, 0))
            x += c.width + (20 if c is cells[-1] else 0)
        rows.append(label(row, label_))
    OUTFIT_SCALE = keep
    return stack(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--methods", nargs="*", default=["nearest", "scale4x", "gtav_dither"])
    ap.add_argument("--faces", action="store_true", help="only faces.png: faces at their own pixels or on the torso's")
    ap.add_argument("--methods-only", action="store_true", help="only methods.png: whole outfits under each upscale")
    a = ap.parse_args()
    parts = json.loads((ms.DATA / "equipment.json").read_text(encoding="utf-8"))["parts"]
    looks = ms.load_json("looks.json")
    OUT.mkdir(parents=True, exist_ok=True)
    if a.faces:
        faces(looks).save(OUT / "faces.png")
        print("wrote", OUT / "faces.png")
        return
    if a.methods_only:
        methods(looks).save(OUT / "methods.png")
        print("wrote", OUT / "methods.png")
        return
    outfits(parts, looks).save(OUT / "outfits.png")
    print("wrote", OUT / "outfits.png")
    items(parts, a.methods).save(OUT / "items.png")
    print("wrote", OUT / "items.png")


if __name__ == "__main__":
    main()
