"""
Runtime colour for sprite players (docs/sprites.md "Colour at runtime"): the atlases hold every part
once, in its original untranslated colours (upscaled), and M_SpriteBody recolours each pixel with
the part's palette translation (xlat), the way the original client translated palette indices.
Two lookup textures make that possible on the GPU:

  T_SprXlat   256 x 256, row = xlat id (xlat.c InitStandardXlats via m59sprites.xlat), column =
              source palette index -> the translated colour (sRGB). Row 0 is the palette itself.
  T_SprClass  where an (upscaled, so not exactly palette) colour sits on each translatable ramp:
              64 x 64 x 64 sRGB cells stored as 8 x 8 tiles of 64 x 64 (blue picks the tile);
              R, G, B = position (0 = the ramp's first entry, 1 = its 16th) on the red (0x10),
              dark blue (0x90) and grey (0xD0) ramps; A = the ramp a colour most likely belongs to
              (0 none, 1/3 red, 2/3 blue, 1 grey), only for in-between frames: the original
              bitmaps carry their ramp exactly, from their palette indices (ramp_alpha).

Which ramp each atlas texel came from is stored in the atlas's alpha (ramp_alpha): 0 transparent,
then 0.6 / 0.733 / 0.867 / 1.0 for no ramp / red / dark blue / grey. The colour pass draws parts
alpha-tested at 0.5 (crisp, like the original), so the alpha survives into the render target.

    python tools/sprites/luts.py      -> build/sprites/lut/T_SprXlat.png, T_SprClass.png
"""
from __future__ import annotations

import numpy as np
from PIL import Image

import m59sprites as ms

OUT = ms.ROOT / "build" / "sprites" / "lut"
RAMPS = {1: 0x10, 2: 0x90, 3: 0xD0}   # the ramps player translations change: red, dark blue, grey
N = 64                                 # classifier cells per channel
MAX_DIST = 40.0                        # sRGB units: farther than this from every ramp -> not translated


def xlat_lut() -> Image.Image:
    pal = ms.palette()
    rows = [pal[np.array(ms.xlat(x), dtype=np.uint8)] for x in range(256)]
    return Image.fromarray(np.stack(rows).astype(np.uint8), "RGB")


def _segment_project(p: np.ndarray, a: np.ndarray, b: np.ndarray):
    """Closest point on segment a-b for every colour in p (n x 3): (distance, t)."""
    ab = b - a
    denom = max(float(ab @ ab), 1e-6)
    t = np.clip(((p - a) @ ab) / denom, 0.0, 1.0)
    d = np.linalg.norm(p - (a + t[:, None] * ab), axis=1)
    return d, t


def class_lut() -> Image.Image:
    pal = ms.palette().astype(np.float32)
    g = (np.arange(N, dtype=np.float32) + 0.5) * 255.0 / N
    r, gg, b = np.meshgrid(g, g, g, indexing="ij")
    p = np.stack([r.ravel(), gg.ravel(), b.ravel()], axis=1)          # (N^3, 3), index = (r, g, b)
    # nearest non-ramp palette colour (special entries 0 and 254/255 aside)
    ramp_idx = {base + i for base in RAMPS.values() for i in range(16)}
    others = np.array([pal[i] for i in range(1, 254) if i not in ramp_idx])
    d_other = np.full(len(p), np.inf, np.float32)
    for c in others:
        d_other = np.minimum(d_other, np.linalg.norm(p - c, axis=1))
    best_d = np.full(len(p), np.inf, np.float32)
    best_ramp = np.zeros(len(p), np.uint8)
    best_pos = np.zeros(len(p), np.float32)
    for rid, base in RAMPS.items():
        curve = pal[base:base + 16]
        for i in range(15):
            d, t = _segment_project(p, curve[i], curve[i + 1])
            better = d < best_d
            best_d[better] = d[better]
            best_ramp[better] = rid
            best_pos[better] = (i + t[better]) / 15.0
    keep = (best_d <= np.maximum(d_other, 6.0)) & (best_d < MAX_DIST)
    ramp = np.where(keep, best_ramp, 0)
    # position on each ramp regardless of distance (a texel's own ramp is known from its alpha)
    pos_on = []
    for rid, base in RAMPS.items():
        curve = pal[base:base + 16]
        bd = np.full(len(p), np.inf, np.float32)
        bp = np.zeros(len(p), np.float32)
        for i in range(15):
            d, t = _segment_project(p, curve[i], curve[i + 1])
            better = d < bd
            bd[better] = d[better]
            bp[better] = (i + t[better]) / 15.0
        pos_on.append(bp)
    chans = [(x * 255 + 0.5).astype(np.uint8).reshape(N, N, N) for x in pos_on]
    chans.append((ramp.astype(np.float32) / 3.0 * 255 + 0.5).astype(np.uint8).reshape(N, N, N))
    # (r, g, b) -> 8 x 8 tiles of N x N: tile = b, x = r, y = g
    img = np.zeros((8 * N, 8 * N, 4), np.uint8)
    for bi in range(N):
        ty, tx = divmod(bi, 8)
        for c in range(4):
            img[ty * N:(ty + 1) * N, tx * N:(tx + 1) * N, c] = chans[c][:, :, bi].T   # rows = g, cols = r
    return Image.fromarray(img, "RGBA")


RAMP_ALPHA = np.array([153, 187, 221, 255], np.uint8)   # alpha per ramp id: none, red, blue, grey
_class_cache = None


def ramp_ids_from_indices(pixels: bytes, w: int, h: int) -> np.ndarray:
    """Ramp id per pixel of an original (indexed) bitmap (0 none, 1 red, 2 dark blue, 3 grey)."""
    idx = np.frombuffer(pixels, np.uint8).reshape(h, w)
    out = np.zeros((h, w), np.int8)
    for rid, base in RAMPS.items():
        out[(idx >= base) & (idx < base + 16)] = rid
    return out


def ramp_alpha(img: Image.Image, ids: np.ndarray | None) -> Image.Image:
    """Put the ramp ids into the alpha of an upscaled cell (ids at any resolution, nearest-scaled;
    None = classify the colours, for in-between frames)."""
    global _class_cache
    a = np.array(img.convert("RGBA"))
    if ids is None:
        if _class_cache is None:
            _class_cache = np.asarray(class_lut(), np.uint8)
        q = np.clip((a[..., :3].astype(np.float32) / 255.0 * N).astype(int), 0, N - 1)
        ty, tx = np.divmod(q[..., 2], 8)
        ids = np.round(_class_cache[ty * N + q[..., 1], tx * N + q[..., 0], 3] / 255.0 * 3).astype(np.int16)
    else:
        ids = np.asarray(Image.fromarray(ids.astype(np.uint8)).resize((a.shape[1], a.shape[0]), Image.NEAREST), np.int16)
    a[..., 3] = np.where(a[..., 3] > 127, RAMP_ALPHA[np.clip(ids, 0, 3)], 0)
    return Image.fromarray(a, "RGBA")


def translate(img: Image.Image, xid: int) -> Image.Image:
    """CPU twin of M_SpriteBody's recolouring, for previews: img carries its ramp ids in alpha
    (ramp_alpha); each texel's position on its ramp, then that entry under the translation."""
    if xid == 0:
        return img
    lut = np.asarray(xlat_lut(), np.float32)
    cls = np.asarray(class_lut(), np.float32)
    a = np.asarray(img.convert("RGBA"), np.float32)
    q = np.clip((a[..., :3] / 255.0 * N).astype(int), 0, N - 1)
    ty, tx = np.divmod(q[..., 2], 8)
    c = cls[ty * N + q[..., 1], tx * N + q[..., 0]]
    rid = np.where(a[..., 3] > 127, np.round((a[..., 3] - 153) / 34.0), 0).astype(int)
    pos = np.choose(np.clip(rid - 1, 0, 2), [c[..., 0], c[..., 1], c[..., 2]]) / 255.0
    base = np.choose(np.clip(rid, 0, 3), [0, 0x10, 0x90, 0xD0])
    x = base + pos * 15
    lo = np.floor(x).astype(int)
    hi = np.minimum(lo + 1, 255)
    f = (x - lo)[..., None]
    row = lut[xid]
    out = row[lo] * (1 - f) + row[hi] * f
    rgb = np.where((rid > 0)[..., None], out, a[..., :3])
    alpha = np.where(a[..., 3] > 127, 255, 0)
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), alpha]).astype(np.uint8), "RGBA")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    xlat_lut().save(OUT / "T_SprXlat.png")
    class_lut().save(OUT / "T_SprClass.png")
    print("wrote", OUT / "T_SprXlat.png", OUT / "T_SprClass.png")


if __name__ == "__main__":
    main()
