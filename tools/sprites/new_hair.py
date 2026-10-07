"""
Phase 5 of docs/sprites.md: a new hairstyle for sprite players from an image model, using the
original heads and an existing hairstyle as the reference.

    build/texai/.venv/Scripts/python tools/sprites/new_hair.py --name ai_hair_01 \
        --describe "shoulder-length wavy hair swept back from the forehead" [--ref ptbb] [--provider openai]

For each of the head's 6 views (slots 0, 1, 2, 3 (the back), 6, 7):
  1. render the original head + face + the reference hair, upscaled, on flat green;
  2. ask the model (tools/aigen/restyle.py, the repo-root .env keys) to replace the hair, drawn in
     neutral grey (the game colours grey hair by palette ramp); the front first, then every other
     view with the approved front as a second reference;
  3. matte the hair (not green, low saturation, not over the eyes / mouth / nose), even out its
     greys to the original ramp's range, and map it back to head pixels.
The result is a custom part in build/sprites/custom/<name>/ (meta.json + bitmap_NN.png) that
m59sprites.py treats like a bgf: shrink 14, placed on the head's hair hotspot (HS_TOUPEE 13),
coloured by recolor_gray. Model outputs are cached in build/sprites/ai_hair/<name>/ (delete one to
regenerate it). Paid calls: 6 images per style.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

import m59sprites as ms
import upscale_parts as up

AIGEN = next((d / "tools" / "aigen" for d in (ms.ROOT, *ms.ROOT.parents) if (d / "tools" / "aigen" / "restyle.py").exists()), None)
OUT = ms.ROOT / "build" / "sprites" / "ai_hair"
SLOTS = [0, 1, 2, 3, 6, 7]       # the head's 6 views (slots 4 and 5 show the back again)
CANVAS = 1024
GREEN = (0, 177, 64)
K = 8                             # texels per head pixel in the saved hair (hair px = head px / 2 at shrink 14)
HAIR_SHRINK = 14

PROMPT = ("The first image shows the head of a character from a 1990s pre-rendered fantasy role-playing game, "
          "with a placeholder hairstyle drawn in neutral grey. Replace the hair with: {describe}. "
          "Keep the head, face, skin, ears, viewing angle, size and position exactly as they are; change only the hair. "
          "Draw the hair in neutral grey shades only, light grey highlights to dark grey shadows, with no colour at all "
          "(the game recolours it). Match the original's painted, slightly grainy pre-rendered look and its soft light. "
          "The background must stay flat pure green (#00B140) everywhere outside the head and hair. No text, no border.")
PROMPT_VIEW = (" The second image is the approved new hairstyle seen from the front: draw exactly that hairstyle, "
               "the same length, volume and parting, on the head in the first image, seen from the first image's angle.")


def head_render(look: ms.Look, angle: int, with_hair: bool, hair: str | None = None):
    """Head, face and (optionally) hair at K*2 texels per head pixel on green, centred on a
    CANVAS square. -> (image, texels per head px, head top-left on the canvas, head bitmap index,
    mask of the face parts)."""
    # the hair in its raw grey ramp (no translation), as the game stores it
    lk = ms.Look(**{**look.__dict__, "hair": (hair or look.hair) if with_hair else None, "hair_color": "none"})
    body, ovs = lk.layers({})
    res = ms.place(body, ovs, angle)
    bi, placed = res
    keep = [p for p in placed if p.name in (lk.head, lk.eyes, lk.mouth, lk.nose, lk.hair)]
    head = next(p for p in keep if p.name == lk.head)
    s_head = ms.load_bgf(lk.head).shrink
    tpb = K * 2 * s_head / ms.load_bgf(body.name).shrink    # texels per base px (head px = 4/7 base px)
    x0 = min(p.x for p in keep)
    y0 = min(p.y for p in keep)
    x1 = max(p.x + ms.load_bgf(p.name).bitmaps[p.index].w * p.scale for p in keep)
    y1 = max(p.y + ms.load_bgf(p.name).bitmaps[p.index].h * p.scale for p in keep)
    # fit the head region into 60% of the canvas
    fit = min(CANVAS * 0.6 / ((x1 - x0) * tpb), CANVAS * 0.6 / ((y1 - y0) * tpb), 2.5)
    t = tpb * fit
    ox = CANVAS / 2 - (x0 + x1) / 2 * t
    oy = CANVAS / 2 - (y0 + y1) / 2 * t
    img = Image.new("RGBA", (CANVAS, CANVAS), GREEN + (255,))
    face = Image.new("L", (CANVAS, CANVAS), 0)
    for p in keep:
        bm = ms.load_bgf(p.name).bitmaps[p.index]
        src = up.get(p.name, p.index, p.xid)
        size = (max(1, round(bm.w * p.scale * t)), max(1, round(bm.h * p.scale * t)))
        im = src.resize(size, Image.LANCZOS)
        pos = (round(ox + p.x * t), round(oy + p.y * t))
        img.alpha_composite(im, pos)
        if p.name in (lk.eyes, lk.mouth, lk.nose):
            face.paste(255, pos, im.getchannel("A").point(lambda v: 255 if v > 128 else 0))
    texels_per_head_px = t * ms.load_bgf(body.name).shrink / s_head
    return img.convert("RGB"), texels_per_head_px, (ox + head.x * t, oy + head.y * t), head.index, face


def generate(name: str, describe: str, look: ms.Look, ref_hair: str, provider: str, model: str | None):
    sys.path.insert(0, str(AIGEN))
    import restyle
    model = model or restyle.DEFAULT_MODELS[provider]
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    (out / "request.json").write_text(json.dumps({"describe": describe, "ref": ref_hair, "provider": provider, "model": model}, indent=1))
    front = None
    for slot in SLOTS:
        f = out / f"slot_{slot}.png"
        ref, *_ = head_render(look, slot * 512, True, ref_hair)
        ref.save(out / f"ref_{slot}.png")
        if not f.exists():
            images = [ref] + ([front] if front is not None else [])
            text = PROMPT.format(describe=describe) + (PROMPT_VIEW if front is not None else "")
            print(f"  {name} slot {slot}: {provider}@{model}")
            img, info = restyle.run(provider, images, text, model)
            img.convert("RGB").resize((CANVAS, CANVAS), Image.LANCZOS).save(f)
        if slot == 0:
            front = Image.open(f).convert("RGB")


def matte(name: str, look: ms.Look) -> dict:
    """The model outputs -> custom part build/sprites/custom/<name>."""
    src = OUT / name
    dst = ms.CUSTOM / name
    dst.mkdir(parents=True, exist_ok=True)
    head_bgf = ms.load_bgf(look.head)
    meta = {"shrink": HAIR_SHRINK, "groups": [[0, 1, 2, 3, 3, 3, 4, 5]], "bitmaps": [], "source": "ai", "views": SLOTS}
    greys = []
    for i, slot in enumerate(SLOTS):
        gen = np.asarray(Image.open(src / f"slot_{slot}.png").convert("RGB"), np.float32)
        _, tph, head_at, head_idx, face = head_render(look, slot * 512, False)
        hsv = np.asarray(Image.fromarray(gen.astype(np.uint8)).convert("HSV"), np.float32)
        green = (np.abs(gen - np.array(GREEN, np.float32)).sum(-1) < 120) | ((hsv[..., 0] > 60) & (hsv[..., 0] < 120) & (hsv[..., 1] > 90))
        grey = hsv[..., 1] < 60          # hair is drawn without colour; skin is orange-brown
        facemask = np.asarray(face.filter(ImageFilter.MaxFilter(15)), bool)
        m = (~green) & grey & (~facemask)
        mask = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(5))
        mask = mask.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))
        box = mask.getbbox()
        if not box:
            raise SystemExit(f"{name} slot {slot}: no hair found in the output")
        lum = gen.mean(-1)
        greys.append(lum[np.asarray(mask, bool)])
        # back to head pixels: K texels per head px
        k = K / tph
        crop = Image.fromarray(np.dstack([gen, np.asarray(mask, np.float32)]).astype(np.uint8), "RGBA").crop(box)
        w4 = max(4, round(crop.width * k / 4) * 4)
        h4 = max(4, round(crop.height * k / 4) * 4)
        crop = crop.resize((w4, h4), Image.LANCZOS)
        crop.save(dst / f"bitmap_{i:02d}.png")
        hx, hy = next(((h["x"], h["y"]) for h in head_bgf.bitmaps[head_idx].hotspots if abs(h["num"]) == 13), (0, 0))
        # offset in head px from the head's hair hotspot (the original's overlay-on-overlay rule)
        xoff = round((box[0] - head_at[0]) / tph - hx)
        yoff = round((box[1] - head_at[1]) / tph - hy)
        meta["bitmaps"].append({"w": w4 // 4, "h": h4 // 4, "xoff": xoff, "yoff": yoff, "hotspots": []})
    # even out the greys over all views to the original ramp's range (36..231)
    allg = np.concatenate(greys)
    lo, hi = np.percentile(allg, 3), np.percentile(allg, 97)
    for i in range(len(SLOTS)):
        im = np.asarray(Image.open(dst / f"bitmap_{i:02d}.png").convert("RGBA"), np.float32)
        l = np.clip((im[..., :3].mean(-1) - lo) / max(1.0, hi - lo), 0, 1) * (220 - 50) + 50
        im[..., 0] = im[..., 1] = im[..., 2] = l
        Image.fromarray(im.astype(np.uint8), "RGBA").save(dst / f"bitmap_{i:02d}.png")
    (dst / "meta.json").write_text(json.dumps(meta, indent=1))
    ms.load_bgf.cache_clear()
    ms.bitmap_rgba.cache_clear()
    return meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--describe", required=True)
    ap.add_argument("--look", default="test_male")
    ap.add_argument("--ref", default="ptbb", help="reference hairstyle bgf (its shape and scale guide the model)")
    ap.add_argument("--provider", default="openai")
    ap.add_argument("--model")
    ap.add_argument("--matte-only", action="store_true")
    a = ap.parse_args()
    look = ms.Look.from_json(ms.load_json("looks.json")[a.look])
    if not a.matte_only:
        if not AIGEN:
            raise SystemExit("tools/aigen/restyle.py not found (merge main, which has the prop pipeline)")
        generate(a.name, a.describe, look, a.ref, a.provider, a.model)
    meta = matte(a.name, look)
    print(f"{a.name}: {len(meta['bitmaps'])} views -> {ms.CUSTOM / a.name}")


if __name__ == "__main__":
    main()
