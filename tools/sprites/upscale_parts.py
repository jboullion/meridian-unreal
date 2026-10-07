"""
4x upscales of player sprite bitmaps with the environment's texture model (docs/adr/0003:
4xTextures_GTAV_rgt-s_dither), cached per bitmap and palette translation:

    build/texai/.venv/Scripts/python tools/sprites/upscale_parts.py --look test_male test_female
    build/texai/.venv/Scripts/python tools/sprites/upscale_parts.py --compare test_male

Colour comes from the model (transparent pixels are first filled from their neighbours so the
key colour can't bleed in); alpha is the 1-bit original, smoothed at the new size.
--compare writes build/sprites/upscale_test/<look>_<action>.png, one row per view: original x4
nearest | parts upscaled then composited (what the runtime does) | composited then upscaled
(the seam-free reference).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

import m59sprites as ms

sys.path.insert(0, str(ms.ROOT / "tools" / "textures"))
MODEL = "4xTextures_GTAV_rgt-s_dither"
PAD = 4
_model = None


def models_dir() -> Path:
    for d in (ms.ROOT, *ms.ROOT.parents):     # build/ is git-ignored: worktrees use the main checkout's
        if (d / "build" / "texai" / "models").is_dir():
            return d / "build" / "texai" / "models"
    raise SystemExit("build/texai/models missing: run tools/textures/setup_ai.ps1")


def cache_dir() -> Path:
    return ms.ROOT / "build" / "sprites" / "up" / MODEL


def fill_transparent(a: np.ndarray) -> np.ndarray:
    """RGB with transparent pixels grown from their opaque neighbours, ring by ring."""
    rgb, known = a[..., :3].astype(np.float32), a[..., 3] >= 128
    rgb[~known] = 0
    h, w = known.shape
    while not known.all():
        k = np.pad(known.astype(np.float32), 1)
        c = np.pad(rgb, ((1, 1), (1, 1), (0, 0))) * k[..., None]
        acc, cnt = np.zeros_like(rgb), np.zeros(known.shape, np.float32)
        for dy in (0, 1, 2):
            for dx in (0, 1, 2):
                if dx != 1 or dy != 1:
                    acc += c[dy:dy + h, dx:dx + w]
                    cnt += k[dy:dy + h, dx:dx + w]
        ring = (~known) & (cnt > 0)
        if not ring.any():
            break
        rgb[ring] = acc[ring] / cnt[ring][:, None]
        known |= ring
    return np.clip(rgb + 0.5, 0, 255).astype(np.uint8)


def upscale_rgba(img: Image.Image) -> Image.Image:
    global _model
    import spandrel
    import torch
    import upscale as up
    if _model is None:
        _model = spandrel.ModelLoader().load_from_file(str(models_dir() / f"{MODEL}.safetensors")).cuda().eval()
        if _model.supports_half:
            _model.model.half()
    a = np.zeros((img.height + 2 * PAD, img.width + 2 * PAD, 4), np.uint8)
    a[PAD:PAD + img.height, PAD:PAD + img.width] = np.asarray(img.convert("RGBA"))
    rgb = up.upscale(_model, Image.fromarray(fill_transparent(a)), _model.supports_half)
    torch.cuda.empty_cache()
    s = 4
    alpha = Image.fromarray(a[..., 3]).resize((a.shape[1] * s, a.shape[0] * s), Image.NEAREST)
    alpha = alpha.filter(ImageFilter.GaussianBlur(s * 0.35))
    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    return out.crop((PAD * s, PAD * s, (PAD + img.width) * s, (PAD + img.height) * s))


def get(name: str, index: int, xid: int, create: bool = True) -> Image.Image | None:
    if ms.is_custom(name):   # drawn at the atlas scale already
        return ms.recolor_gray(ms.load_bgf(name).image(index), xid)
    f = cache_dir() / f"{name}_{index:03d}_x{xid:02x}.png"
    if f.exists():
        return Image.open(f).convert("RGBA")
    if not create:
        return None
    f.parent.mkdir(parents=True, exist_ok=True)
    im = upscale_rgba(ms.bitmap_rgba(name, index, xid))
    im.save(f)
    return im


def look_bitmaps(look: ms.Look, actions: dict) -> set[tuple[str, int, int]]:
    """Every (bgf, bitmap, xlat) a look can show in the given actions, at any angle."""
    import composite as cp
    need = set()
    for act in actions.values():
        for groups, _ in cp.timeline(look, act):
            body, ovs = look.layers(groups)
            for a in cp.ANGLES:
                res = ms.place(body, ovs, a)
                if res:
                    need.add((body.name, res[0], body.xid))
                    need |= {(p.name, p.index, p.xid) for p in res[1]}
    return need


def compare(look_name: str, action: str, scale: int = 4):
    import composite as cp
    look = ms.Look.from_json(ms.load_json("looks.json")[look_name])
    groups = cp.timeline(look, ms.load_json("player_actions.json")[action])[0][0]
    rows = []
    for a in cp.ANGLES[:3] + [cp.ANGLES[4], cp.ANGLES[6]]:
        orig = ms.composite(*look.layers(groups), a, scale=scale)
        per_part = ms.composite(*look.layers(groups), a, scale=scale, source=get)
        whole = upscale_rgba(ms.composite(*look.layers(groups), a, scale=1).image)
        rows.append([orig.image, per_part.image, whole])
    w = max(im.width for r in rows for im in r)
    h = max(im.height for r in rows for im in r)
    sheet = Image.new("RGBA", (w * 3, h * len(rows)), (48, 52, 60, 255))
    for y, r in enumerate(rows):
        for x, im in enumerate(r):
            sheet.alpha_composite(im, (x * w, y * h))
    out = ms.ROOT / "build" / "sprites" / "upscale_test"
    out.mkdir(parents=True, exist_ok=True)
    sheet.save(out / f"{look_name}_{action}.png")
    print("compare", out / f"{look_name}_{action}.png")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--look", nargs="*", default=[])
    ap.add_argument("--compare", nargs="*", default=[])
    ap.add_argument("--action", default="stand")
    a = ap.parse_args()
    actions = {k: v for k, v in ms.load_json("player_actions.json").items() if not k.startswith("_")}
    looks = ms.load_json("looks.json")
    for ln in a.look:
        need = sorted(look_bitmaps(ms.Look.from_json(looks[ln]), actions))
        todo = [n for n in need if get(*n, create=False) is None]
        print(f"{ln}: {len(need)} bitmaps, {len(todo)} to upscale")
        for i, n in enumerate(todo):
            get(*n)
            if i % 25 == 0:
                print(f"  {i}/{len(todo)} {n}")
    for ln in a.compare:
        compare(ln, a.action)


if __name__ == "__main__":
    main()
