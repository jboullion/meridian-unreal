"""
4x upscales of player sprite bitmaps, cached per bitmap, method and palette translation:

    build/texai/.venv/Scripts/python tools/sprites/upscale_parts.py --look test_male test_female
    build/texai/.venv/Scripts/python tools/sprites/upscale_parts.py --compare test_male

The method is a data switch (data/sprites/upscale.json): a default and one per part role (head,
eyes, nose, mouth, hair, body, arms, legs, weapon). Methods, all local (no generated images):

    nearest      the original pixels, 4x each way
    scale4x      Scale2x (EPX) twice on the palette indices: corners rounded, no new colours
    gtav_dither  4xTextures_GTAV_rgt-s_dither (the environment's texture model, ADR 0003): until
                 2026-10-07 the only method; its dither shows as grain on faces
    gtav         4xTextures_GTAV_rgt-s, the same model without the dither
    gtav_pinned  gtav, with each original pixel's 4x4 block moved back to that pixel's colour on
                 average: the model's shading inside a pixel, the original's colours
    animesharp   4x-AnimeSharp (flat-shaded art)

A model's colour comes from the model (transparent pixels are first filled from their neighbours so
the key colour can't bleed in); its alpha is the original's, 4x nearest, so the part covers exactly
the original's pixels (the runtime's palette ramps are per original pixel: docs/sprites.md).
--compare writes build/sprites/upscale_test/<look>_<action>.png, one row per view: original x4
nearest | parts upscaled then composited (what the runtime does) | composited then upscaled.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import m59sprites as ms

sys.path.insert(0, str(ms.ROOT / "tools" / "textures"))
SCALE = 4
PAD = 4
MODELS = {"gtav_dither": "4xTextures_GTAV_rgt-s_dither", "gtav": "4xTextures_GTAV_rgt-s",
          "gtav_pinned": "4xTextures_GTAV_rgt-s", "animesharp": "4x-AnimeSharp"}
METHODS = ["nearest", "scale4x", "gtav_dither", "gtav", "gtav_pinned", "animesharp"]
CONFIG = ms.DATA / "upscale.json"
_models: dict[str, object] = {}

# a part's role from its bgf name: face parts and hair are four letters (system.kod charinfo_*),
# the torso, arms and legs three (player.kod: bta / btb, armour btc..btr, bla, bra, bfa...)
ROLE_PREFIX = [("ph", "head", 4), ("pe", "eyes", 4), ("pn", "nose", 4), ("pm", "mouth", 4), ("pt", "hair", 4),
               ("bt", "body", 3), ("bl", "arms", 3), ("br", "arms", 3), ("bf", "legs", 3)]


def role(name: str) -> str:
    n = name.lower()
    if ms.is_custom(n):
        return "custom"
    for p, r, length in ROLE_PREFIX:
        if n.startswith(p) and len(n) == length:
            return r
    return "other"


def config() -> dict:
    return json.loads(CONFIG.read_text(encoding="utf-8")) if CONFIG.exists() else {"default": "gtav_dither"}


def method_for(name: str) -> str:
    c = config()
    m = c.get("roles", {}).get(role(name), c.get("default", "gtav_dither"))
    if m not in METHODS:
        raise SystemExit(f"{CONFIG.name}: unknown method {m} (one of {METHODS})")
    return m


def models_dir() -> Path:
    for d in (ms.ROOT, *ms.ROOT.parents):     # build/ is git-ignored: worktrees use the main checkout's
        if (d / "build" / "texai" / "models").is_dir():
            return d / "build" / "texai" / "models"
    raise SystemExit("build/texai/models missing: run tools/textures/setup_ai.ps1")


def cache_dir(method: str) -> Path:
    # model methods keep the model's name (the cache from before the switch is gtav_dither's)
    folder = MODELS[method] if method in ("gtav_dither", "gtav", "animesharp") else method
    return ms.ROOT / "build" / "sprites" / "up" / folder


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


def nearest_alpha(img: Image.Image) -> np.ndarray:
    """The original's 1-bit alpha at SCALE x, nearest: exactly the original pixels."""
    a = np.asarray(img.convert("RGBA"))[..., 3]
    return np.where(np.kron(a, np.ones((SCALE, SCALE), np.uint8)) >= 128, 255, 0).astype(np.uint8)


def model_rgb(img: Image.Image, method: str) -> np.ndarray:
    """The model's 4x colour of an RGBA bitmap (transparent pixels filled first)."""
    import spandrel
    import torch
    import upscale as up
    model_name = MODELS[method]
    if model_name not in _models:
        m = spandrel.ModelLoader().load_from_file(str(models_dir() / f"{model_name}.safetensors")).cuda().eval()
        if m.supports_half:
            m.model.half()
        _models[model_name] = m
    model = _models[model_name]
    a = np.zeros((img.height + 2 * PAD, img.width + 2 * PAD, 4), np.uint8)
    a[PAD:PAD + img.height, PAD:PAD + img.width] = np.asarray(img.convert("RGBA"))
    rgb = np.asarray(up.upscale(model, Image.fromarray(fill_transparent(a)), model.supports_half))
    torch.cuda.empty_cache()
    s = SCALE
    return rgb[PAD * s:(PAD + img.height) * s, PAD * s:(PAD + img.width) * s]


def pinned(rgb: np.ndarray, img: Image.Image) -> np.ndarray:
    """Move each SCALE x SCALE block's mean to its original pixel's colour (opaque pixels only)."""
    src = np.asarray(img.convert("RGBA")).astype(np.float32)
    h, w = src.shape[:2]
    blocks = rgb.astype(np.float32).reshape(h, SCALE, w, SCALE, 3)
    mean = blocks.mean(axis=(1, 3))
    shift = np.where((src[..., 3] >= 128)[..., None], src[..., :3] - mean, 0)
    out = blocks + shift[:, None, :, None, :]
    return np.clip(out.reshape(h * SCALE, w * SCALE, 3) + 0.5, 0, 255).astype(np.uint8)


def scale2x(idx: np.ndarray) -> np.ndarray:
    """Scale2x / EPX (Andrea Mazzoleni's rules) on palette indices: no new colours."""
    p = np.pad(idx, 1, mode="edge")
    a, b, c, d = p[:-2, 1:-1], p[1:-1, 2:], p[1:-1, :-2], p[2:, 1:-1]   # up, right, left, down
    out = np.empty((idx.shape[0] * 2, idx.shape[1] * 2), idx.dtype)
    out[0::2, 0::2] = np.where((c == a) & (c != d) & (a != b), a, idx)
    out[0::2, 1::2] = np.where((a == b) & (a != c) & (b != d), b, idx)
    out[1::2, 0::2] = np.where((d == c) & (d != b) & (c != a), c, idx)
    out[1::2, 1::2] = np.where((b == d) & (b != a) & (d != c), d, idx)
    return out


def scaled_indices(name: str, index: int, method: str) -> np.ndarray:
    """The palette indices at SCALE x for the index-based methods (nearest, scale4x)."""
    b = ms.load_bgf(name).bitmaps[index]
    idx = np.frombuffer(b.pixels, np.uint8).reshape(b.h, b.w)
    if method == "scale4x":
        return scale2x(scale2x(idx))
    return np.kron(idx, np.ones((SCALE, SCALE), np.uint8))


def upscale(name: str, index: int, xid: int, method: str) -> Image.Image:
    if method in ("nearest", "scale4x"):
        idx = scaled_indices(name, index, method)
        rgb = ms.palette()[np.array(ms.xlat(xid), dtype=np.uint8)[idx]]
        a = np.where(idx == ms.TRANSPARENT, 0, 255).astype(np.uint8)
        return Image.fromarray(np.dstack([rgb, a]).astype(np.uint8), "RGBA")
    img = ms.bitmap_rgba(name, index, xid)
    rgb = model_rgb(img, method)
    if method == "gtav_pinned":
        rgb = pinned(rgb, img)
    return Image.fromarray(np.dstack([rgb, nearest_alpha(img)]), "RGBA")


def get(name: str, index: int, xid: int, create: bool = True, method: str | None = None) -> Image.Image | None:
    if ms.is_custom(name):   # drawn at the atlas scale already
        return ms.recolor_gray(ms.load_bgf(name).image(index), xid)
    method = method or method_for(name)
    if method in ("nearest", "scale4x"):
        return upscale(name, index, xid, method)   # cheap: never cached
    f = cache_dir(method) / f"{name}_{index:03d}_x{xid:02x}.png"
    if f.exists():
        im = Image.open(f).convert("RGBA")
        # (caches from before 2026-10-07 hold a blurred alpha: the original's mask replaces it)
        im.putalpha(Image.fromarray(nearest_alpha(ms.bitmap_rgba(name, index, xid))))
        return im
    if not create:
        return None
    f.parent.mkdir(parents=True, exist_ok=True)
    im = upscale(name, index, xid, method)
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
        whole = Image.fromarray(model_rgb(ms.composite(*look.layers(groups), a, scale=1).image, "gtav_dither"))
        rows.append([orig.image, per_part.image, whole.convert("RGBA")])
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
