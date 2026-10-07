"""
Leaf-cluster atlas and bark for the procedural trees (tools/blender/build_tree_kit.py, docs/adr/0007
"Trees"). Pure Python + Pillow:

    python tools/textures/make_tree_textures.py [--force]

For each tree in TREES, reads the original sprite (build/bgf/<bgf>/frame_00.png, from
python tools/bgf2png/bgf2png.py <bgf>) and writes into build/textures_placeholder/:

- T_TreeLeaves_<Name>.png: a 2x2 atlas of leaf clusters (RGBA, hard-ish alpha). Every leaf is a
  pointed oval filled with a patch of the sprite's own painted canopy, so the colours and the
  painted texture are the original's; inner leaves are drawn first and darker, which gives each
  cluster depth. A thin twig runs up each cell.
  A fruit tree (`fruit`) first paints its fruit out of the canopy (filled with the leaf colour
  around it), then draws fruit into the clusters among the leaves: round (apple, orange) or a
  teardrop (pear), shaded from the sprite's own fruit colours.
- T_TreeBark_<Name>.png: a tileable bark strip in the colours of the sprite's trunk (vertical
  streaks from stretched, wrapped noise); `trunk_bgf` takes them from another sprite (the shrub
  shows no trunk). A tree with no `canopy` (leafless) gets bark only.

Deterministic (seeded); skipped when the output is newer than the sprite and this script.
"""
import argparse
import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "build", "textures_placeholder")

# name: sprite, canopy box (x0, y0, x1, y1 in sprite pixels; the region leaf patches come from),
# trunk box, seed. The canopy box keeps clear of the darkest bottom band, which is shadow.
TREES = {
    "Mid": {"bgf": "midtree2", "canopy": (60, 40, 450, 430), "trunk": (246, 520, 268, 715), "seed": 7},
    # shrubee1 (198): small boxwood-like leaves, densely packed; no trunk shows, so midtree2's bark
    "Shrub": {"bgf": "shrubee1", "canopy": (60, 110, 450, 470), "trunk_bgf": "midtree2", "trunk": (246, 520, 268, 715),
              "seed": 13, "leaves": 230, "leaf_len": (24, 38)},
    # nectree1 (91): leafless, so bark only, from its gnarled trunk
    "Dead": {"bgf": "nectree1", "canopy": None, "trunk": (140, 350, 190, 425), "seed": 19},
    # the fruit trees (FoodDispenser: the Apple, Pear and Orange classes' dispenser icons): one
    # dark-leaved tree, its fruit drawn on; the sprites are small (200 px), so fewer, larger leaves
    "Apple": {"bgf": "appletree", "canopy": (8, 6, 178, 160), "trunk": (88, 185, 104, 250), "seed": 29,
              "leaves": 110, "fruit": {"shape": "round", "count": (0, 2), "size": (40, 52)}},
    "Pear": {"bgf": "peartree", "canopy": (6, 6, 160, 160), "trunk": (75, 185, 87, 250), "seed": 31,
             "leaves": 110, "fruit": {"shape": "pear", "count": (0, 2), "size": (40, 52)}},
    "Orange": {"bgf": "orangetree", "canopy": (8, 6, 178, 160), "trunk": (88, 185, 104, 250), "seed": 37,
               "leaves": 110, "fruit": {"shape": "round", "count": (0, 2), "size": (46, 58)}},
    # raztree1 (OrnamentalObject 116, OO_RAZA_TREE1): a broad yellow-green crown on a stout trunk
    "Raza": {"bgf": "raztree1", "canopy": (8, 6, 182, 160), "trunk": (88, 185, 104, 250), "seed": 43, "leaves": 120},
}

CELL = 512          # atlas cell, px (the atlas is 2x2 cells)
SS = 2              # supersampling for smooth leaf edges
LEAVES = 125        # leaves per cluster
LEAF_LEN = (46, 70)  # px in a cell
LEAF_W = (0.42, 0.55)  # width / length


def leaf_polygon(cx, cy, length, width, angle, steps=10):
    """A pointed oval from its base (cx, cy) along `angle`: rounded near the base, pointed tip."""
    pts_l, pts_r = [], []
    ca, sa = math.cos(angle), math.sin(angle)
    for i in range(steps + 1):
        t = i / steps
        half = width / 2 * math.sin(math.pi * t) ** 0.75 * (1.0 - 0.35 * t)
        along = length * t
        px, py = cx + ca * along, cy + sa * along
        pts_l.append((px - sa * half, py + ca * half))
        pts_r.append((px + sa * half, py - ca * half))
    return pts_l + pts_r[::-1]


def fruit_mask(rgb, opaque):
    """The fruit in a canopy: saturated, bright pixels (the leaves are dark and dull)."""
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    return opaque & (mx - mn > 60) & (mx > 110)


def canopy_patches(sprite, box, fruit=False):
    """The canopy region as an RGB array, plus a mask of fully opaque pixels. fruit: paint the
    fruit out first (each fruit pixel, grown by 2 px, takes the leaf colour around it), so leaf
    patches carry no half-fruit; the clusters draw whole fruit back in (draw_fruit)."""
    region = np.asarray(sprite.crop(box).convert("RGBA")).astype(np.float32)
    rgb, opaque = region[..., :3], region[..., 3] > 250
    if fruit:
        f = fruit_mask(rgb, opaque)
        f = box_blur(f[..., None].astype(np.float32), 2, 1)[..., 0] > 0.01
        keep = (opaque & ~f)[..., None].astype(np.float32)
        spread = box_blur(rgb * keep, 4, 3) / np.maximum(box_blur(keep, 4, 3), 1e-3)
        rgb = np.where(f[..., None], spread, rgb)
    return rgb, opaque


def fruit_colours(sprite, box):
    """(dark, mid, light) RGB of the sprite's fruit, from its fruit pixels."""
    region = np.asarray(sprite.crop(box).convert("RGBA")).astype(np.float32)
    rgb = region[..., :3]
    px = rgb[fruit_mask(rgb, region[..., 3] > 250)]
    return tuple(np.percentile(px, p, axis=0) for p in (10, 50, 92))


def draw_fruit(img, cx, cy, size, shape, colours, rng):
    """One fruit at (cx, cy), `size` px across: a round fruit or a pear's teardrop, shaded from
    `colours` (dark rim, mid body, a light highlight up and to the left), with a short stem."""
    dark, mid, light = colours
    # at least some shading, for sprites whose fruit is one flat colour (the orange's)
    dark, light = np.minimum(dark, mid * 0.6), np.maximum(light, mid + (255 - mid) * 0.35)
    h = int(size * (1.35 if shape == "pear" else 1.0))
    w, x0, y0 = int(size), int(cx - size / 2), int(cy - h / 2)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = (xx + 0.5) / w * 2 - 1, (yy + 0.5) / h * 2 - 1  # -1..1, v down
    if shape == "pear":
        # narrow neck at the top widening into a round bottom
        half = np.where(v < 0.1, 0.42 + 0.3 * (v + 1) / 1.1, np.sqrt(np.clip(1 - ((v - 0.1) / 0.9) ** 2, 0, 1)) * 0.75 + 0.0)
        inside = (np.abs(u) <= half) & (v > -0.97)
        r = np.clip(np.abs(u) / np.maximum(half, 1e-3), 0, 1) * 0.7 + np.clip(np.abs(v), 0, 1) * 0.3
    else:
        rr = np.sqrt(u * u + v * v)
        inside = rr <= 1.0
        r = rr
    hl = np.clip(1.0 - np.sqrt((u + 0.35) ** 2 + (v + 0.4) ** 2) / 0.45, 0, 1) ** 2
    col = mid[None, None] * (1 - r[..., None] ** 2) + dark[None, None] * (r[..., None] ** 2)
    col = col + (light - col) * hl[..., None] * 0.8
    col = col * rng.uniform(0.85, 1.1)
    alpha = inside.astype(np.float32) * 255
    fruit = Image.fromarray(np.dstack([np.clip(col, 0, 255), alpha]).astype(np.uint8), "RGBA")
    img.alpha_composite(fruit, (x0, y0))
    # the stem, a short dark stroke above the top
    d = ImageDraw.Draw(img)
    top = (cx + rng.uniform(-0.05, 0.05) * size, y0 + 2)
    d.line([top, (top[0] + rng.uniform(-0.15, 0.15) * size, top[1] - size * 0.22)], fill=(48, 32, 16, 255),
           width=max(2, int(size * 0.07)))


def draw_cluster(rng, canopy, opaque, size, cfg, fruit_cols=None):
    """One leaf cluster in a size x size RGBA image."""
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ch, cw = opaque.shape
    # the twig: from the bottom centre, curving up into the cluster
    twig = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(twig)
    bend = rng.uniform(-0.12, 0.12) * s
    pts = [(s * 0.5 + bend * (t * t), s * (0.98 - 0.62 * t)) for t in [i / 12 for i in range(13)]]
    for i in range(len(pts) - 1):
        w = int((1.0 - i / 12) * 9 * SS + 2 * SS)
        d.line([pts[i], pts[i + 1]], fill=(40, 26, 14, 255), width=w)
    img.alpha_composite(twig)
    centre = (s * 0.5, s * 0.46)
    radius = s * 0.40
    leaves = []
    for _ in range(cfg.get("leaves", LEAVES)):
        # base points fill a disc (denser inside); leaves point outwards with some scatter
        r = radius * rng.random() ** 0.75 * 0.84
        a = rng.uniform(0, 2 * math.pi)
        bx, by = centre[0] + math.cos(a) * r, centre[1] + math.sin(a) * r * 0.92
        out = math.atan2(by - centre[1], bx - centre[0]) if r > radius * 0.1 else rng.uniform(0, 2 * math.pi)
        angle = out + rng.uniform(-0.7, 0.7)
        depth = r / radius  # 0 centre .. ~0.82 rim: inner leaves sit deeper in the clump
        leaves.append((depth + rng.uniform(-0.25, 0.25), bx, by, angle))
    fruit = cfg.get("fruit")
    if fruit and fruit_cols is not None:
        # fruit hangs among the leaves: at mid depth, so outer leaves partly cover some of it
        for _ in range(rng.randint(*fruit["count"])):
            r = radius * rng.uniform(0.15, 0.7)
            a = rng.uniform(0, 2 * math.pi)
            leaves.append((rng.uniform(0.35, 0.75), centre[0] + math.cos(a) * r, centre[1] + math.sin(a) * r * 0.92, None))
    leaves.sort(key=lambda leaf: leaf[0])  # inner (deep) first, outer drawn on top
    for depth, bx, by, angle in leaves:
        if angle is None:
            draw_fruit(img, bx, by, rng.uniform(*fruit["size"]) * SS, fruit["shape"], fruit_cols, rng)
            continue
        length = rng.uniform(*cfg.get("leaf_len", LEAF_LEN)) * SS
        poly = leaf_polygon(bx, by, length, length * rng.uniform(*LEAF_W), angle)
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        x0, y0 = int(min(xs)) - 1, int(min(ys)) - 1
        x1, y1 = int(max(xs)) + 2, int(max(ys)) + 2
        w, h = x1 - x0, y1 - y0
        mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(mask).polygon([(x - x0, y - y0) for x, y in poly], fill=255)
        # fill: a patch of the painted canopy (at half scale, so the paint keeps its grain)
        pw, ph = max(2, w // (2 * SS) + 1), max(2, h // (2 * SS) + 1)
        for _ in range(20):
            px, py = rng.randrange(0, cw - pw), rng.randrange(0, ch - ph)
            if opaque[py:py + ph, px:px + pw].all():
                break
        patch = canopy[py:py + ph, px:px + pw]
        fill = np.asarray(Image.fromarray(patch.astype(np.uint8)).resize((w, h), Image.BILINEAR)).astype(np.float32)
        # light: brighter towards the tip and on the outside of the clump, darker deep inside
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        along = ((xx + x0 - bx) * math.cos(angle) + (yy + y0 - by) * math.sin(angle)) / length
        shade = (0.55 + 0.6 * np.clip(depth, 0, 1)) * (0.8 + 0.35 * np.clip(along, 0, 1))
        fill = np.clip(fill * shade[..., None] * 1.25 + 4.0, 0, 255)
        leaf = Image.fromarray(np.dstack([fill, np.full((h, w), 255, np.float32)]).astype(np.uint8), "RGBA")
        leaf.putalpha(mask)
        img.alpha_composite(leaf, (x0, y0))
    return img.resize((size, size), Image.LANCZOS)


def box_blur(arr, r, passes):
    """Repeated separable box blur (about a Gaussian) of an H x W x C float array, edges clamped."""
    for _ in range(passes):
        for axis in (0, 1):
            pad = [(0, 0)] * arr.ndim
            pad[axis] = (r + 1, r)
            c = np.cumsum(np.pad(arr, pad, mode="edge"), axis=axis)
            n = arr.shape[axis]
            arr = (np.take(c, range(2 * r + 1, 2 * r + 1 + n), axis=axis) - np.take(c, range(0, n), axis=axis)) / (2 * r + 1)
    return arr


def leaf_atlas(sprite, cfg):
    rng = random.Random(cfg["seed"])
    canopy, opaque = canopy_patches(sprite, cfg["canopy"], bool(cfg.get("fruit")))
    fruit_cols = fruit_colours(sprite, cfg["canopy"]) if cfg.get("fruit") else None
    atlas = Image.new("RGBA", (CELL * 2, CELL * 2), (0, 0, 0, 0))
    for i in range(4):
        atlas.alpha_composite(draw_cluster(rng, canopy, opaque, CELL, cfg, fruit_cols), ((i % 2) * CELL, (i // 2) * CELL))
    # fill the clear pixels with the nearby leaf colour (alpha stays), so filtering and mips don't
    # pull a dark or bright fringe into the leaf edges: a normalised blur of colour by alpha
    px = np.asarray(atlas).astype(np.float32)
    a = px[..., 3:4] / 255.0
    spread = box_blur(px[..., :3] * a, 12, 3) / np.maximum(box_blur(a, 12, 3), 1e-3)
    fill = px[..., :3] * a + np.clip(spread, 0, 255) * (1 - a)
    out = np.dstack([fill, px[..., 3]]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def wrapped_noise(rng, size, cells):
    """Tileable smooth noise in [0, 1]: small random grid, tiled 3x3, upscaled, centre cropped."""
    w, h = size
    cw, chh = cells
    small = np.array([[rng.random() for _ in range(cw)] for _ in range(chh)], np.float32)
    tiled = np.tile(small, (3, 3))
    big = Image.fromarray((tiled * 255).astype(np.uint8)).resize((w * 3, h * 3), Image.BICUBIC)
    return np.asarray(big).astype(np.float32)[h:2 * h, w:2 * w] / 255.0


def bark(sprite, cfg):
    rng = random.Random(cfg["seed"] + 1)
    trunk = np.asarray(sprite.crop(cfg["trunk"]).convert("RGB")).reshape(-1, 3).astype(np.float32)
    dark, mid, light = (np.percentile(trunk, p, axis=0) for p in (15, 50, 88))
    w, h = 256, 512
    n = (0.5 * wrapped_noise(rng, (w, h), (24, 6)) + 0.3 * wrapped_noise(rng, (w, h), (48, 16))
         + 0.2 * wrapped_noise(rng, (w, h), (96, 48)))
    n = (n - n.min()) / (n.max() - n.min())
    # furrows: dark vertical cracks where the streak noise is low
    t = n[..., None]
    col = np.where(t < 0.5, dark + (mid - dark) * (t / 0.5), mid + (light - mid) * ((t - 0.5) / 0.5))
    col = col * (0.85 + 0.3 * wrapped_noise(rng, (w, h), (16, 32))[..., None])
    return Image.fromarray(np.clip(col, 0, 255).astype(np.uint8), "RGB")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    me = os.path.getmtime(__file__)
    for name, cfg in TREES.items():
        src = os.path.join(REPO, "build", "bgf", cfg["bgf"], "frame_00.png")
        if not os.path.exists(src):
            print("[make_tree_textures] missing %s (run python tools/bgf2png/bgf2png.py %s)" % (src, cfg["bgf"]))
            sys.exit(1)
        trunk_src = os.path.join(REPO, "build", "bgf", cfg.get("trunk_bgf", cfg["bgf"]), "frame_00.png")
        newest = max(me, os.path.getmtime(src), os.path.getmtime(trunk_src))
        sprite = Image.open(src).convert("RGBA")
        jobs = [("Bark", bark, Image.open(trunk_src).convert("RGBA"))]
        if cfg["canopy"]:  # leafless trees have no leaf atlas
            jobs.insert(0, ("Leaves", leaf_atlas, sprite))
        for kind, make, image in jobs:
            path = os.path.join(OUT, "T_Tree%s_%s.png" % (kind, name))
            if not args.force and os.path.exists(path) and os.path.getmtime(path) > newest:
                continue
            make(image, cfg).save(path)
            print("[make_tree_textures] %s" % path)


if __name__ == "__main__":
    main()
