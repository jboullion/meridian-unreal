"""
In-between frames for sprite parts (docs/sprites.md Phase 4, smoothing B/C). The original animates
in a few poses (the walk: 4 leg poses at 100 ms, 2 arm poses at 200 ms), so between two consecutive
bitmaps of a part (same view) we add frames made without any AI model:

- "flow": the two bitmaps overlap well (silhouette IoU >= FLOW_IOU, e.g. the legs while walking):
  optical flow (OpenCV DIS) from A to B; frame t warps A forward and B back along it and blends.
- "rigid": they don't, but one is close to the other turned about a pivot (an arm swinging at the
  shoulder, IoU >= RIGID_IOU after the best rotation + shift): frame t turns A part of the way and
  B the rest of the way back, then the flow step above cleans up what is left.
- neither (a hand opening, a body twisting): no in-betweens; the runtime keeps the original snap
  or crossfades (mr.Sprite.Smooth.Crossfade). RIFE or ToonCrafter could fill these later.

    build/texai/.venv/Scripts/python tools/sprites/tweens.py --preview bla 6 12 --xlat 0x88

build_player_sprites.py adds each in-between as an extra bitmap of its bgf (after the originals,
with "tween": [A, B, k, n]) with a cell in every atlas of that bgf, and gives the bgf
"tweens": {"A>B": [indices]}; the runtime picks one from the track's phase.

Geometry: base pixels are torso pixels (shrink 4 for every player torso). A part bitmap sits at
hotspot + offset (offset in base pixels) and spans w*sc x h*sc base pixels, sc = 4 / shrink. An
in-between is drawn on a canvas around both (padded for rigid turns), then cropped to what it drew.
"""
from __future__ import annotations

import argparse
import json
import math
from functools import lru_cache

import numpy as np
from PIL import Image

import m59sprites as ms
import upscale_parts as up

BASE_SHRINK = 4
SCALE = 4            # atlas texels per original pixel (upscale_parts)
CACHE = ms.ROOT / "build" / "sprites" / "tweens"
METHOD = "rigidflow3"  # bump when the method changes, to refresh the cache
TWEEN_PARTS = ("body", "legs", "left_arm", "right_arm", "weapon")
FLOW_IOU = 0.6
RIGID_IOU = 0.6


def transitions(actions: dict, part: str) -> set[tuple[int, int]]:
    """Kod group pairs (1-based) a part steps between in these actions."""
    pairs = set()
    for act in actions.values():
        t = act.get(part)
        if not isinstance(t, dict) or t.get("mode") not in ("cycle", "once"):
            continue
        lo, hi = t["low"], t.get("high", t["low"])
        pairs |= {(g, g + 1) for g in range(lo, hi)}
        if t["mode"] == "cycle" and hi > lo:
            pairs.add((hi, lo))
        if t["mode"] == "once":
            pairs.add((hi, t.get("final", 1)))
    return {p for p in pairs if p[0] != p[1]}


def bitmap_pairs(bgf: str, group_pairs: set[tuple[int, int]]) -> list[tuple[int, int]]:
    """Distinct (bitmap A, bitmap B) over every view of each group transition."""
    b = ms.load_bgf(bgf)
    out = set()
    for ga, gb in group_pairs:
        if ga - 1 >= len(b.groups) or gb - 1 >= len(b.groups):
            continue
        for slot in range(len(b.groups[ga - 1])):
            ia = b.groups[ga - 1][slot]
            ib = b.groups[gb - 1][slot] if slot < len(b.groups[gb - 1]) else -1
            if ia >= 0 and ib >= 0 and ia != ib:
                out.add((ia, ib))
    return sorted(out)


def geometry(bgf: str, a: int, b: int, margin: int = 0) -> dict:
    """The canvas around A and B: its top-left in base pixels, its size in part pixels, and where
    A and B sit on it (part px)."""
    g = ms.load_bgf(bgf)
    sc = BASE_SHRINK / g.shrink
    A, B = g.bitmaps[a], g.bitmaps[b]
    x0 = min(A.xoff, B.xoff) - margin * sc
    y0 = min(A.yoff, B.yoff) - margin * sc
    x1 = max(A.xoff + A.w * sc, B.xoff + B.w * sc) + margin * sc
    y1 = max(A.yoff + A.h * sc, B.yoff + B.h * sc) + margin * sc
    return {"xoff": x0, "yoff": y0, "w": math.ceil((x1 - x0) / sc), "h": math.ceil((y1 - y0) / sc), "sc": sc,
            "a_at": ((A.xoff - x0) / sc, (A.yoff - y0) / sc), "b_at": ((B.xoff - x0) / sc, (B.yoff - y0) / sc)}


def _mask(bgf: str, i: int, at, shape) -> np.ndarray:
    bm = ms.load_bgf(bgf).bitmaps[i]
    m = np.zeros(shape, np.float32)
    px = (np.frombuffer(bm.pixels, np.uint8).reshape(bm.h, bm.w) != ms.TRANSPARENT).astype(np.float32)
    x, y = round(at[0]), round(at[1])
    m[y:y + bm.h, x:x + bm.w] = px[:shape[0] - y, :shape[1] - x]
    return m


def _iou(ma, mb) -> float:
    a, b = ma > 0.5, mb > 0.5
    u = (a | b).sum()
    return float((a & b).sum() / u) if u else 0.0


def _rigid(M, theta, pivot, d):
    """Turn M by theta degrees (image coordinates, + = counter-clockwise) about pivot, then shift by d."""
    import cv2
    R = cv2.getRotationMatrix2D((float(pivot[0]), float(pivot[1])), float(theta), 1.0)
    R[:, 2] += d
    return cv2.warpAffine(M, R, (M.shape[1], M.shape[0]), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)


@lru_cache(maxsize=None)
def plan(bgf: str, a: int, b: int) -> dict | None:
    """How to in-between A -> B (see the module docstring), or None."""
    g = ms.load_bgf(bgf)
    geo = geometry(bgf, a, b)
    shape = (geo["h"] + 1, geo["w"] + 1)
    ma, mb = _mask(bgf, a, geo["a_at"], shape), _mask(bgf, b, geo["b_at"], shape)
    direct = _iou(ma, mb)
    if direct >= FLOW_IOU:
        return {"mode": "flow", "iou": round(direct, 3), "margin": 2}
    # rigid: pivot at the top middle of A (a shoulder, a hip), every turn, then match the centroids
    margin = math.ceil(max(g.bitmaps[a].w, g.bitmaps[a].h, g.bitmaps[b].w, g.bitmaps[b].h) * 0.6)
    geo = geometry(bgf, a, b, margin)
    shape = (geo["h"] + 1, geo["w"] + 1)
    ma, mb = _mask(bgf, a, geo["a_at"], shape), _mask(bgf, b, geo["b_at"], shape)
    ys, xs = np.nonzero(ma > 0.5)
    if not len(ys):
        return None
    top = ys.min()
    pivot = (float(xs[ys <= top + 3].mean()), float(top + 2))
    cb = np.array(np.nonzero(mb > 0.5)[::-1]).mean(axis=1)
    best = (0.0, 0.0, (0.0, 0.0))
    for theta in range(-150, 151, 3):
        r = _rigid(ma, theta, pivot, (0.0, 0.0))
        pts = np.nonzero(r > 0.5)
        if not len(pts[0]):
            continue
        d = cb - np.array(pts[::-1]).mean(axis=1)
        iou = _iou(_rigid(ma, theta, pivot, d), mb)
        if iou > best[0]:
            best = (iou, theta, (float(d[0]), float(d[1])))
    if best[0] >= RIGID_IOU and best[0] > direct:
        return {"mode": "rigid", "iou": round(best[0], 3), "direct_iou": round(direct, 3), "theta": best[1],
                "pivot": pivot, "d": best[2], "margin": margin}
    return None


def _canvas(img: Image.Image, at, size) -> np.ndarray:
    c = Image.new("RGBA", size, (0, 0, 0, 0))
    c.alpha_composite(img, (round(at[0] * SCALE), round(at[1] * SCALE)))
    x = np.asarray(c, dtype=np.float32) / 255.0
    x[..., :3] *= x[..., 3:4]   # premultiplied
    return x


def _flow_morph(a: np.ndarray, b: np.ndarray, t: float) -> np.ndarray:
    import cv2

    def key(x):
        lum = x[..., 0] * 0.3 + x[..., 1] * 0.59 + x[..., 2] * 0.11
        return np.clip((lum * 0.6 + x[..., 3] * 0.4) * 255, 0, 255).astype(np.uint8)

    def warp(img, flow, s):
        h, w = flow.shape[:2]
        gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
        return cv2.remap(img, gx + flow[..., 0] * s, gy + flow[..., 1] * s, cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)

    dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    f_ab = dis.calc(key(a), key(b), None)   # a(x) ~ b(x + f_ab(x))
    f_ba = dis.calc(key(b), key(a), None)
    return warp(a, f_ba, t) * (1.0 - t) + warp(b, f_ab, 1.0 - t) * t


def make_tween(bgf: str, a: int, b: int, xid: int, t: float) -> np.ndarray:
    """Premultiplied RGBA on the plan's canvas (part px * SCALE)."""
    p = plan(bgf, a, b)
    geo = geometry(bgf, a, b, p["margin"])
    size = (geo["w"] * SCALE, geo["h"] * SCALE)
    A, B = _canvas(up.get(bgf, a, xid), geo["a_at"], size), _canvas(up.get(bgf, b, xid), geo["b_at"], size)
    if p["mode"] == "rigid":
        pv = (p["pivot"][0] * SCALE, p["pivot"][1] * SCALE)
        d = (p["d"][0] * SCALE, p["d"][1] * SCALE)
        A = _rigid(A, p["theta"] * t, pv, (d[0] * t, d[1] * t))
        # B turned back: about the pivot moved with it
        B = _rigid(B, -p["theta"] * (1 - t), (pv[0] + d[0], pv[1] + d[1]), (-d[0] * (1 - t), -d[1] * (1 - t)))
    return _flow_morph(A, B, t)


def _finish(x: np.ndarray) -> Image.Image:
    alpha = x[..., 3:4]
    rgb = np.where(alpha > 1e-3, x[..., :3] / np.maximum(alpha, 1e-3), 0)
    alpha = np.clip((alpha - 0.5) * 3.0 + 0.5, 0, 1)   # crisp cut-out: the runtime masks at 0.5
    return Image.fromarray(np.clip(np.concatenate([rgb, alpha], -1) * 255 + 0.5, 0, 255).astype(np.uint8), "RGBA")


def crop_box(bgf: str, a: int, b: int, t: float) -> tuple[int, int, int, int]:
    """Where in-between t can draw, from the 1-bit masks moved the same way (so every palette
    translation of a bgf gets the same box): canvas texels, on part-pixel boundaries."""
    p = plan(bgf, a, b)
    geo = geometry(bgf, a, b, p["margin"])
    shape = (geo["h"] + 1, geo["w"] + 1)
    ma, mb = _mask(bgf, a, geo["a_at"], shape), _mask(bgf, b, geo["b_at"], shape)
    if p["mode"] == "rigid":
        ma = _rigid(ma, p["theta"] * t, p["pivot"], (p["d"][0] * t, p["d"][1] * t))
        pv = (p["pivot"][0] + p["d"][0], p["pivot"][1] + p["d"][1])
        mb = _rigid(mb, -p["theta"] * (1 - t), pv, (-p["d"][0] * (1 - t), -p["d"][1] * (1 - t)))
    ys, xs = np.nonzero((ma > 0.05) | (mb > 0.05))
    x0, y0 = max(0, xs.min() - 2), max(0, ys.min() - 2)
    x1, y1 = min(geo["w"], xs.max() + 3), min(geo["h"], ys.max() + 3)
    return int(x0) * SCALE, int(y0) * SCALE, int(x1) * SCALE, int(y1) * SCALE


def get(bgf: str, a: int, b: int, k: int, n: int, xid: int) -> tuple[Image.Image, dict]:
    """In-between k of n (cached): the image, cropped to what it drew, and its bitmap entry
    (w, h in part px; offset in base px; hotspots interpolated)."""
    f = CACHE / up.MODEL / METHOD / f"{bgf}_x{xid:02x}" / f"{a:03d}_{b:03d}_{k}of{n}.png"
    side = f.with_suffix(".json")
    if f.exists() and side.exists():
        return Image.open(f).convert("RGBA"), json.loads(side.read_text())
    f.parent.mkdir(parents=True, exist_ok=True)
    t = k / (n + 1)
    p = plan(bgf, a, b)
    geo = geometry(bgf, a, b, p["margin"])
    im = _finish(make_tween(bgf, a, b, xid, t))
    x0, y0, x1, y1 = crop_box(bgf, a, b, t)
    im = im.crop((x0, y0, x1, y1))
    cx, cy = x0 / SCALE, y0 / SCALE   # crop origin, part px
    g = ms.load_bgf(bgf)
    A, B = g.bitmaps[a], g.bitmaps[b]
    hs_b = {h["num"]: h for h in B.hotspots}
    hotspots = []
    for h in A.hotspots:
        ax, ay = h["x"] + geo["a_at"][0], h["y"] + geo["a_at"][1]
        hb = hs_b.get(h["num"])
        bx, by = (hb["x"] + geo["b_at"][0], hb["y"] + geo["b_at"][1]) if hb else (ax, ay)
        hotspots.append([h["num"], round(ax + (bx - ax) * t - cx), round(ay + (by - ay) * t - cy)])
    meta = {"w": (x1 - x0) // SCALE, "h": (y1 - y0) // SCALE,
            "xoff": round(geo["xoff"] + cx * geo["sc"]), "yoff": round(geo["yoff"] + cy * geo["sc"]),
            "hotspots": hotspots, "tween": [a, b, k, n], "mode": p["mode"]}
    im.save(f)
    side.write_text(json.dumps(meta))
    return im, meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", nargs=3, metavar=("BGF", "A", "B"), help="bitmap indices")
    ap.add_argument("--xlat", default="0")
    ap.add_argument("-n", type=int, default=3)
    a = ap.parse_args()
    if a.preview:
        bgf, ia, ib = a.preview[0], int(a.preview[1]), int(a.preview[2])
        xid = int(a.xlat, 0)
        p = plan(bgf, ia, ib)
        print("plan", p)
        if not p:
            return
        sc = ms.load_bgf(bgf).shrink
        frames = [(up.get(bgf, ia, xid), ms.load_bgf(bgf).bitmaps[ia].xoff, ms.load_bgf(bgf).bitmaps[ia].yoff)]
        for k in range(1, a.n + 1):
            im, meta = get(bgf, ia, ib, k, a.n, xid)
            frames.append((im, meta["xoff"], meta["yoff"]))
        frames.append((up.get(bgf, ib, xid), ms.load_bgf(bgf).bitmaps[ib].xoff, ms.load_bgf(bgf).bitmaps[ib].yoff))
        # place each frame where the game would (offsets in base px -> preview px)
        k = SCALE * sc / BASE_SHRINK
        x0 = min(f[1] for f in frames) * k
        y0 = min(f[2] for f in frames) * k
        cw = int(max(f[1] * k - x0 + f[0].width for f in frames)) + 4
        ch = int(max(f[2] * k - y0 + f[0].height for f in frames)) + 4
        strip = Image.new("RGBA", (cw * len(frames), ch), (48, 52, 60, 255))
        for i, (im, xo, yo) in enumerate(frames):
            strip.alpha_composite(im, (i * cw + int(xo * k - x0), int(yo * k - y0)))
        out = ms.ROOT / "build" / "sprites" / "tween_preview"
        out.mkdir(parents=True, exist_ok=True)
        strip.save(out / f"{bgf}_{ia}_{ib}.png")
        print(out / f"{bgf}_{ia}_{ib}.png")


if __name__ == "__main__":
    main()
