"""
Render original-style player sprites offline (Phase 0 of docs/sprites.md): every action of a look
at all 8 view angles, at the original timing, to check the port of the client's compositing.

    python tools/sprites/composite.py --look test_male                 # all actions
    python tools/sprites/composite.py --look test_female --action walk dance --scale 4
    python tools/sprites/composite.py --catalog bta bra bla bfa         # every group of a part

Output (build/sprites/preview/<look>/):
    <action>.gif         the 8 angles side by side, animated at the original timing
    <action>_sheet.png   rows = angles, columns = the distinct poses in time order
    look.json            sizes: the composite in base pixels and metres
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

import m59sprites as ms

OUT = ms.ROOT / "build" / "sprites"
ANGLES = [i * ms.NUMDEGREES // 8 for i in range(8)]
PARTS = ["body", "left_arm", "right_arm", "legs", "head", "eyes", "mouth", "nose", "hair", "weapon"]
BG = (48, 52, 60, 255)


def tracks_for(look: ms.Look, action: dict) -> dict[str, ms.Track]:
    tracks = {k: ms.Track.from_json(v) for k, v in action.items() if not k.startswith("_")}
    if look.weapon:
        tracks.setdefault("right_arm", ms.Track("none", group=17))
        tracks.setdefault("weapon", ms.Track("none", group=4))
    else:
        tracks.pop("weapon", None)
        ra = tracks.get("right_arm")
        if ra and ra.final == 17:
            ra.final = 1
    return tracks


def timeline(look: ms.Look, action: dict, hold_ms: int = 600) -> list[tuple[dict[str, int], int]]:
    """Simulate AnimateSingle for every track: list of (groups per part, duration ms)."""
    tracks = tracks_for(look, action)
    moving = [t for t in tracks.values() if t.mode != "none"]
    if not moving:
        return [({k: t.group for k, t in tracks.items()}, hold_ms)]
    dt = math.gcd(*[t.period for t in moving])
    cycle = [t for t in moving if t.mode == "cycle"]
    if cycle:
        total = math.lcm(*[t.length_ms() for t in cycle])
        total = max(total, max(t.length_ms() for t in moving))
    else:
        total = max(t.length_ms() for t in moving) + hold_ms
    frames, now = [], 0
    while now < total:
        groups = {k: t.group for k, t in tracks.items()}
        if frames and frames[-1][0] == groups:
            frames[-1] = (groups, frames[-1][1] + dt)
        else:
            frames.append((groups, dt))
        for t in tracks.values():
            t.step(dt)
        now += dt
    return frames


def render_action(look: ms.Look, name: str, action: dict, scale: int, out: Path) -> dict:
    frames = timeline(look, action)
    # one canvas for every frame and angle, so nothing jumps
    boxes = []
    for groups, _ in frames:
        for a in ANGLES:
            c = ms.composite(*look.layers(groups), a)
            if c:
                l, t = -c.origin[0], -c.origin[1]
                boxes.append((l, t, l + c.image.width, t + c.image.height))
    canvas = (int(min(b[0] for b in boxes)), int(min(b[1] for b in boxes)),
              int(max(b[2] for b in boxes)), int(max(b[3] for b in boxes)))
    cw, ch = (canvas[2] - canvas[0]) * scale, (canvas[3] - canvas[1]) * scale
    gif, durations, sheet_cols = [], [], []
    feet = None
    for groups, ms_ in frames:
        strip = Image.new("RGBA", (cw * 8, ch + 14 * scale // 2), BG)
        col = []
        for i, a in enumerate(ANGLES):
            c = ms.composite(*look.layers(groups), a, scale=scale, canvas=canvas)
            if c:
                strip.alpha_composite(c.image, (i * cw, 0))
                col.append(c.image)
                feet = c.feet_y
            else:
                col.append(Image.new("RGBA", (cw, ch)))
        d = ImageDraw.Draw(strip)
        for i in range(8):
            d.text((i * cw + 3, ch + 1), f"{i}", fill=(200, 200, 210, 255))
            if feet is not None:
                d.line([(i * cw, feet), ((i + 1) * cw - 2, feet)], fill=(90, 160, 90, 255))
        gif.append(strip)
        durations.append(ms_)
        sheet_cols.append(col)
    out.mkdir(parents=True, exist_ok=True)
    gif[0].save(out / f"{name}.gif", save_all=True, append_images=gif[1:], duration=durations, loop=0,
                disposal=2)
    sheet = Image.new("RGBA", (cw * len(sheet_cols), ch * 8), BG)
    for x, col in enumerate(sheet_cols):
        for y, im in enumerate(col):
            sheet.alpha_composite(im, (x * cw, y * ch))
    sheet.save(out / f"{name}_sheet.png")
    return {"frames": [{"groups": g, "ms": d} for g, d in frames], "canvas_base_px": canvas}


def catalog(names: list[str], scale: int):
    """Every group of a part at all 8 angles, labelled with the Kod (1-based) group number."""
    pal_out = OUT / "catalog"
    pal_out.mkdir(parents=True, exist_ok=True)
    for n in names:
        bgf = ms.load_bgf(n)
        ims = [[ms.bitmap_rgba(n, i) if (i := ms.bitmap_index(n, g, a)) is not None else None for a in ANGLES]
               for g in range(len(bgf.groups))]
        cw = max(im.width for row in ims for im in row if im) * scale + 6
        ch = max(im.height for row in ims for im in row if im) * scale + 6
        sheet = Image.new("RGBA", (40 + cw * 8, ch * len(ims)), BG)
        d = ImageDraw.Draw(sheet)
        for g, row in enumerate(ims):
            d.text((4, g * ch + ch // 2 - 5), f"{g + 1}", fill=(230, 230, 120, 255))
            for a, im in enumerate(row):
                if im:
                    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
                    sheet.alpha_composite(im, (40 + a * cw, g * ch))
        sheet.save(pal_out / f"{n}.png")
        print("catalog", pal_out / f"{n}.png")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--look", nargs="*", default=[])
    ap.add_argument("--action", nargs="*")
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--catalog", nargs="*")
    a = ap.parse_args()
    if a.catalog:
        catalog(a.catalog, a.scale)
    looks = ms.load_json("looks.json")
    actions = ms.load_json("player_actions.json")
    for ln in a.look:
        look = ms.Look.from_json(looks[ln])
        out = OUT / "preview" / ln
        info = {"look": looks[ln], "actions": {}}
        for an in a.action or [k for k in actions if not k.startswith("_")]:
            info["actions"][an] = render_action(look, an, actions[an], a.scale, out)
        # size of the standing pose, front view
        c = ms.composite(*look.layers({}), 0)
        bx = c.image.getbbox()
        h_px = (bx[3] - bx[1]) / c.scale
        info["stand_front"] = {"height_base_px": h_px, "height_m": ms.base_px_to_m(h_px, c.shrink),
                               "feet_y_px": c.feet_y, "bbox": bx}
        (out / "look.json").write_text(json.dumps(info, indent=1))
        print(f"{ln}: {len(info['actions'])} actions -> {out}  standing height "
              f"{info['stand_front']['height_m']:.2f} m ({h_px:.0f} base px, shrink {c.shrink})")


if __name__ == "__main__":
    main()
