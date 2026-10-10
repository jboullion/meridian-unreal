"""
PBR textures from the extracted originals (ADR 0003). Needs Pillow; uses Real-ESRGAN and the AI
relief environment when they are installed (README, "Environment art"). Run after
tools/bgf2png --textures-for-zones:

    python tools/textures/make_placeholders.py [--scale 4] [--max 2048] [--esrgan PATH | --no-esrgan]
                                               [--esrgan-model NAME] [--relief auto|rules] [--force] [--jobs N]

  base colour   AI 4x upscale of the original (4xTextures_GTAV_rgt-s_dither, see ESRGAN_MODEL),
                wrap-padded so tiling textures stay seamless, when
                build/texai/realesrgan/realesrgan-ncnn-vulkan.exe exists (or --esrgan PATH); otherwise
                Lanczos + sharpening (--no-esrgan forces that). The model is picked per texture name
                (ESRGAN_RULES; --esrgan-model NAME uses one model for everything, to compare): a
                Real-ESRGAN model, or any 4x model saved as build/texai/models/<NAME>.safetensors (run
                through spandrel by tools/textures/upscale.py in build/texai/.venv). Each model runs
                once over all the originals it has to do; the upscales are cached in
                build/texai/upscaled/<model>/, so switching models back and forth is cheap.
  relief        --relief auto (default): after this script, tools/textures/ai_maps.py --apply (in
                build/texai/.venv, when it exists) replaces every height and normal map with Marigold's,
                made from the base colour, incrementally. --relief rules keeps the rule-based maps below.

Incremental: a texture set is remade only when its inputs change (the original's pixels, its catalog
entry, the rules that apply to it, the options, or the code that makes it); the rest are kept from
the last run (build/textures_placeholder/cache.json). Changed sets are made in parallel. --force
remakes everything.

For every texture in build/textures/catalog.json it writes, to build/textures_placeholder/:
  T_<grd>_D.png   base colour: upscaled (see above) and resized to power-of-two sides so UE gets mips.
                  The UVs from roo2gltf span one texture repeat, so the stretch doesn't change the mapping.
  T_<grd>_H.png   height (0 = deepest), for normals and Nanite displacement (rule-based here; Marigold's
                  with --relief auto). Modes:
                  "luma": blurred luminance (dark cracks = low);
                  "stones" (masonry: cobble, ashlar, paths): the mortar is found as the minority side of
                  an Otsu threshold (it is light on some originals and dark on others, so luminance
                  alone would raise the mortar), and each stone is domed by its distance to the mortar.
                  "timber" (timber-framed facades): the dark side of the Otsu split (beams, posts,
                  braces) stands proud of the light plaster, with rounded edges.
  T_<grd>_E.png   textures with painted windows (facades.json) only: the window mask, lit at night.
  T_<grd>_M.png   timber textures only: the beam mask at the original's size (255 = beam), from
                  which build_zone_art.py can extrude real beams.
  T_<grd>_N.png   tangent-space normal map, DirectX convention (green = down), from the height. Filters
                  wrap around the tile edges so tiling textures stay seamless. Blue is a constant: UE
                  stores normal maps as BC5 and rebuilds Z from X/Y.
  T_WaterNormal.png   tileable ripple normal map for the water material.
  T_Stars.png         equirectangular star map for the night sky (M_NightSky, docs/adr/0005).
  T_Bolts.png         four lightning bolts side by side, white on black, for distant strokes (M_Bolt).
  T_Fire_<name>.png   flame flipbooks (data/environment/props.json "fires", docs/adr/0005 phase 3): the
                      original torch, brazier and candle frames, cropped to the flame, upscaled with the
                      base-colour model, one power-of-two cell per frame (4 per row), soft alpha.
                      Wall textures in a preset's "walls" lose their painted flame (alpha 0 where the
                      pixels are flame-coloured inside the "erase" rect): the flipbook replaces it.
  T_MacroNoise.png    tileable low-frequency noise (R large, G medium, B small blobs) for breaking up
                      tiling and tinting ground and grass across the world (world-aligned in UE).
  placeholders.json   per texture: file names, masked (has transparency), roughness, normal strength;
                      "extras": shared textures (macro noise); "fires": per flame preset its atlas,
                      cells, frame rate, brightness and size in metres.

These are throwaway: tools/ue/build_world.py turns them into material instances under
/Game/Generated, and data/environment/materials.json overrides any slot with a real material.
"""
import argparse
import hashlib
import inspect
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "build" / "textures"
FACADES = ROOT / "data" / "environment" / "facades.json"
PROPS = ROOT / "data" / "environment" / "props.json"
BGF = ROOT / "build" / "bgf"  # tools/bgf2png <name>: build/bgf/<name>/frame_NN.png + meta.json
MATERIALS = ROOT / "data" / "environment" / "materials.json"
OUT = ROOT / "build" / "textures_placeholder"

# (name pattern, roughness, normal strength); first match wins
SURFACE_RULES = [
    (r"water", 0.08, 0.15),
    (r"marble", 0.35, 0.6),
    (r"sign|tapestry|carpet|cloth|clock|picture", 0.8, 0.35),
    (r"cage|fence|gate|torch", 0.55, 0.8),
    (r"roof", 0.75, 2.2),  # rule-based relief (materials.json relief rules_for): a stronger lift for the strokes
    (r"grass|field|forest|tree", 0.95, 0.7),
    (r"rock|stone|path|brick|flagston|mausoleum|wall", 0.9, 1.0),
    (r"wood|roof|door|floor|cab|shelf|table|bar|chair|barrel", 0.75, 0.8),
]
DEFAULT_SURFACE = (0.85, 0.8)
# height mode by name; first match wins, default "luma"
HEIGHT_RULES = [
    (r"sign|tapestry|carpet|cloth|clock|picture|water|fence|cage|gate|torch|tree|forest", "luma"),
    (r"\bint|floor|ceil|cieling|cab\b|table|chair|barrel|tos-", "luma"),  # interiors and furniture
    (r"roof", "luma"),  # follows the painted shingle strokes; "stones" made blobs on slate
    (r"chimney", "stones"),  # chimney bricks dome; their dark lines are gaps
    (r"bldg-?[abgj]\b", "timber"),
    (r"stone|rock|cobble|brick|flagston|mausoleum|bldg-?[hilm]\b|bldgl|def|new building|raz-wall|path|repeating", "stones"),
]
STONE_RADIUS = 3.5  # dome radius in texels of the original art (typical stone half-width)
# Upscale model by texture name; first match wins, default ESRGAN_MODEL. 4xTextures_GTAV_rgt-s_dither
# (Phips, CC-BY-4.0; trained on game textures, removes dithering) cleans the originals up but keeps
# their rough, painted feel and colours; Real-ESRGAN x4plus repaints them smoother and drifts (blue
# turns black on the signs). Chosen 2026-10-05 from in-engine tests (docs/adr/0003; comparison
# sheets in build/texai/upscalers/, look-dev labels up_<model>). When its weights are missing
# (build/texai/models/, see setup_ai.ps1), ESRGAN_FALLBACK is used.
ESRGAN_RULES: list = []
ESRGAN_MODEL = "4xTextures_GTAV_rgt-s_dither"
ESRGAN_FALLBACK = "realesrgan-x4plus"
DEDITHER = r"grass|field"  # originals de-dithered (median) before upscaling
BUMP = 6.0  # height of a black-to-white luminance step, in texels of the original art


def surface(name: str) -> tuple[float, float]:
    for pat, rough, strength in SURFACE_RULES:
        if re.search(pat, name, re.I):
            return rough, strength
    return DEFAULT_SURFACE


def esrgan_model(name: str) -> str:
    for pat, model in ESRGAN_RULES:
        if re.search(pat, name, re.I):
            return model
    return ESRGAN_MODEL


def available_model(model: str, exe: Path) -> str:
    """`model` if its weights are installed (build/texai/models/<model>.safetensors with the AI
    environment, or a Real-ESRGAN model next to the exe), else ESRGAN_FALLBACK."""
    if (MODELS_DIR / (model + ".safetensors")).exists() and RELIEF_PYTHON.exists():
        return model
    if (exe.parent / "models" / (model + ".param")).exists():
        return model
    return ESRGAN_FALLBACK


def height_mode(name: str) -> str:
    for pat, mode in HEIGHT_RULES:
        if re.search(pat, name, re.I):
            return mode
    return "luma"


def otsu(img: Image.Image) -> int:
    hist = img.histogram()
    total = sum(hist)
    sum_all = sum(i * h for i, h in enumerate(hist))
    best, best_t, w0, sum0 = -1.0, 128, 0, 0.0
    for t in range(256):
        w0 += hist[t]
        if w0 == 0 or w0 == total:
            continue
        sum0 += t * hist[t]
        m0, m1 = sum0 / w0, (sum_all - sum0) / (total - w0)
        between = w0 * (total - w0) * (m0 - m1) ** 2
        if between > best:
            best, best_t = between, t
    return best_t


def _beam_mask(luma: Image.Image, pad: int, w: int, h: int, texel_scale: float) -> Image.Image:
    """Timber facades: the dark side of an Otsu split (beams, posts, braces), specks removed.
    `luma` is padded by `pad` on every side; so is the result."""
    smooth = luma.filter(ImageFilter.GaussianBlur(texel_scale * 0.5))
    t = otsu(smooth.crop((pad, pad, pad + w, pad + h)))
    beams = smooth.point(lambda v: 255 if v <= t else 0)
    k = 2 * max(1, round(texel_scale * 0.5)) + 1
    beams = beams.filter(ImageFilter.MaxFilter(k)).filter(ImageFilter.MinFilter(k))
    return beams.filter(ImageFilter.MinFilter(k)).filter(ImageFilter.MaxFilter(k))


def beam_mask(color: Image.Image, texel_scale: float, size: tuple[int, int]) -> Image.Image:
    """The timber beam mask at the original texture's size (255 = beam), for geometry built from
    it (tools/blender/build_zone_art.py timber relief)."""
    w, h = color.size
    pad = int(max(1.0, texel_scale * 0.6) * 3) + 2
    luma = wrap_crop(color.convert("L"), pad).filter(ImageFilter.GaussianBlur(max(1.0, texel_scale * 0.6)))
    mask = _beam_mask(luma, pad, w, h, texel_scale).crop((pad, pad, pad + w, pad + h))
    return mask.resize(size, Image.Resampling.BILINEAR).point(lambda v: 255 if v >= 128 else 0)


def height_map(color: Image.Image, mode: str, texel_scale: float) -> Image.Image:
    """0..255 height, same size as color, seamless across tile edges."""
    w, h = color.size
    blur = max(1.0, texel_scale * 0.6)  # hide the original's dither, keep its shapes
    radius = STONE_RADIUS * texel_scale
    pad = int(blur * 3) + 2 + (int(radius * 3) if mode == "stones" else 0)
    luma = wrap_crop(color.convert("L"), pad).filter(ImageFilter.GaussianBlur(blur))
    if mode == "timber":
        beams = _beam_mask(luma, pad, w, h, texel_scale)
        profile = beams.filter(ImageFilter.GaussianBlur(texel_scale * 0.8))  # rounded beam edges
        plaster = luma.point(lambda v: 40 + v // 6)  # slightly uneven plaster
        out = ImageChops.lighter(profile.point(lambda v: 60 + v * 195 // 255), plaster)
        return Image.blend(out, luma, 0.08).crop((pad, pad, pad + w, pad + h))
    if mode != "stones":
        return luma.crop((pad, pad, pad + w, pad + h))
    # stone mask: Otsu split of a smoother copy, mortar = the minority side, specks removed
    smooth = luma.filter(ImageFilter.GaussianBlur(texel_scale * 0.6))
    t = otsu(smooth.crop((pad, pad, pad + w, pad + h)))
    stones = smooth.point(lambda v: 255 if v <= t else 0)
    if sum(stones.histogram()[255:]) < (stones.width * stones.height) / 2:
        stones = smooth.point(lambda v: 255 if v > t else 0)
    k = 2 * max(1, round(texel_scale * 0.5)) + 1
    stones = stones.filter(ImageFilter.MaxFilter(k)).filter(ImageFilter.MinFilter(k))  # close specks in stones
    stones = stones.filter(ImageFilter.MinFilter(k)).filter(ImageFilter.MaxFilter(k))  # open specks in mortar
    # dome: the blurred mask is ~0.5 at a stone's edge and rises towards its middle
    dome = ImageChops.multiply(stones.filter(ImageFilter.GaussianBlur(radius)), stones)
    dome = dome.filter(ImageFilter.GaussianBlur(texel_scale * 0.5))  # round the mortar edges
    lo, hi = dome.getextrema()
    dome = dome.point(lambda v: round(255 * (v - lo) / max(1, hi - lo)))
    # a little of the painted surface detail on top
    return Image.blend(dome, luma, 0.12).crop((pad, pad, pad + w, pad + h))


def macro_noise(size: int = 512, seed: int = 59) -> Image.Image:
    """Tileable value noise: random cells upscaled with wrap-around bicubic, one octave per channel."""
    import random
    rng = random.Random(seed)
    channels = []
    for cells in (4, 9, 22):
        small = Image.new("L", (cells, cells))
        small.putdata([rng.randrange(256) for _ in range(cells * cells)])
        big = wrap_crop(small, 2).resize(((cells + 4) * size // cells, (cells + 4) * size // cells), Image.Resampling.BICUBIC)
        off = 2 * size // cells
        channels.append(big.crop((off, off, off + size, off + size)).filter(ImageFilter.GaussianBlur(size / cells / 6)))
    return Image.merge("RGB", channels)


def bolts(cell=(256, 1024), count: int = 4, seed: int = 13) -> Image.Image:
    """Lightning bolts for the distant strokes in rainstorms (M_Bolt, docs/adr/0005 phase 4): `count`
    cells side by side, each a jagged channel from the top (the cloud base) to the bottom (the
    ground) with a few forks that die out, a white core inside a soft blue-white glow, on black (the
    material adds it to the sky)."""
    import math
    import random
    rng = random.Random(seed)
    w, h = cell
    img = Image.new("RGB", (w * count, h))
    for i in range(count):
        core = Image.new("L", (w, h))
        draw = ImageDraw.Draw(core)

        def channel(x, y, angle, length, width, depth):
            steps = int(length / 14)
            for _ in range(steps):
                # jagged, but the main channel is steered back towards the middle of its cell
                pull = -(x - w / 2) / (w / 2) * (0.5 if depth == 0 else 0.15)
                angle = max(-0.7, min(0.7, angle + rng.gauss(0, 0.45) + pull))
                nx, ny = x + math.sin(angle) * 14, y + math.cos(angle) * 14
                draw.line((x, y, nx, ny), fill=255, width=max(1, round(width)))
                x, y = nx, ny
                width = max(1.0, width * 0.995)
                if depth < 2 and rng.random() < 0.06:
                    channel(x, y, angle + rng.choice((-1, 1)) * rng.uniform(0.4, 0.9), length * rng.uniform(0.15, 0.35), width * 0.6, depth + 1)
                if y >= h - 4 or (depth > 0 and not 4 <= x <= w - 4):
                    break
                x = max(8.0, min(w - 8.0, x))

        channel(w / 2 + rng.uniform(-30, 30), 0, rng.uniform(-0.3, 0.3), h * 1.3, 4.0, 0)
        glow = core.filter(ImageFilter.GaussianBlur(10)).point(lambda v: min(255, v * 3))
        halo = core.filter(ImageFilter.GaussianBlur(3)).point(lambda v: min(255, v * 2))
        r = ImageChops.add(ImageChops.add(core, halo.point(lambda v: v * 0.8)), glow.point(lambda v: v * 0.55))
        b = ImageChops.add(ImageChops.add(core, halo), glow)
        img.paste(Image.merge("RGB", (r, r, b)), (i * w, 0))
    return img


def stars(width: int = 4096, seed: int = 59) -> Image.Image:
    """Night-sky star map, equirectangular (u = longitude, v = 0 at the zenith, 0.5 at the horizon),
    for M_NightSky (docs/adr/0005). Mostly faint stars, a few bright and tinted ones, and a band of
    extra faint ones along a tilted great circle for a hint of a milky way. Stars are widened towards
    the poles so they stay round on the sky."""
    import math
    import random
    rng = random.Random(seed)
    height = width // 2
    img = Image.new("RGB", (width, height))
    draw = ImageDraw.Draw(img)
    tints = [(255, 255, 255)] * 6 + [(205, 218, 255), (255, 232, 205), (255, 214, 186)]

    def star(x, y, z, b):
        z = max(-1.0, min(1.0, z))
        lat = math.asin(z)
        u = (math.atan2(y, x) / (2 * math.pi)) % 1.0 * width
        v = math.acos(z) / math.pi * height
        r = 0.35 + 0.8 * b  # about a texel: the map is seen ~1.5x magnified, plus bloom
        rx = min(r / max(math.cos(lat), 0.05), width / 16)
        c = tuple(round(t * b) for t in rng.choice(tints))
        for dx in (-width, 0, width):  # wrap round the seam
            draw.ellipse((u + dx - rx, v - r, u + dx + rx, v + r), fill=c)

    def point():
        z = rng.uniform(-1.0, 1.0)
        a = rng.uniform(0.0, 2 * math.pi)
        s = math.sqrt(1.0 - z * z)
        return s * math.cos(a), s * math.sin(a), z

    for _ in range(9000):
        star(*point(), 0.18 + 0.82 * rng.random() ** 7)
    # the band: points near a great circle (normal n), spread a little either side
    n = (0.35, -0.6, 0.72)
    ln = math.sqrt(sum(c * c for c in n))
    n = tuple(c / ln for c in n)
    a = (n[1], -n[0], 0.0)
    la = math.sqrt(sum(c * c for c in a))
    a = tuple(c / la for c in a)
    b = (n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0])
    for _ in range(16000):
        t = rng.uniform(0.0, 2 * math.pi)
        off = rng.gauss(0.0, 0.11)
        p = [a[i] * math.cos(t) + b[i] * math.sin(t) + n[i] * off for i in range(3)]
        lp = math.sqrt(sum(c * c for c in p))
        star(p[0] / lp, p[1] / lp, p[2] / lp, 0.08 + 0.45 * rng.random() ** 9)
    img = img.filter(ImageFilter.GaussianBlur(0.5))
    return img.point(lambda v: min(255, round(v * 2.0)))


def _normalise(premul: Image.Image, weight: Image.Image) -> Image.Image:
    """premultiplied colour / weight, per channel (0 where the weight is 0)."""
    w = weight.tobytes()
    out = []
    for band in premul.split():
        out.append([min(255, round(c * 255 / a)) if a else 0 for c, a in zip(band.tobytes(), w)])
    img = Image.new("RGB", premul.size)
    img.putdata(list(zip(*out)))
    return img


def water_normal(size: int = 512, seed: int = 7) -> Image.Image:
    """Tileable ripple normal map (DirectX): a sum of integer-frequency waves in random
    directions plus fine noise, so it tiles and pans without seams."""
    import math
    import random
    rng = random.Random(seed)
    waves = []
    for _ in range(14):
        fx, fy = rng.randint(-9, 9), rng.randint(-9, 9)
        if fx == 0 and fy == 0:
            fx = 1
        amp = 1.0 / math.hypot(fx, fy) ** 0.8
        waves.append((fx, fy, amp, rng.uniform(0, 2 * math.pi)))
    data = []
    for y in range(size):
        for x in range(size):
            h = 0.0
            for fx, fy, amp, ph in waves:
                h += amp * math.sin(2 * math.pi * (fx * x + fy * y) / size + ph)
            data.append(h)
    lo, hi = min(data), max(data)
    img = Image.new("L", (size, size))
    img.putdata([round(255 * (v - lo) / (hi - lo)) for v in data])
    return normal_map(img, strength=0.35, texel_scale=1.0)


def pow2(n: int) -> int:
    p = 1
    while p < n:
        p *= 2
    return p


def target_size(w: int, h: int, scale: int, cap: int) -> tuple[int, int]:
    return min(pow2(w * scale), cap), min(pow2(h * scale), cap)


def fill_cutout(img: Image.Image) -> Image.Image:
    """RGB of an RGBA original with every transparent pixel set to the average of its nearest opaque
    neighbours, grown ring by ring (wrapping around, as the textures tile). The originals key their
    transparent pixels as cyan (0, 255, 255); left in, any resampling filter blends it into the edge of
    the cut-out, which then renders as a cyan outline."""
    w, h = img.size
    px = img.load()
    rgb = [[px[x, y][:3] for x in range(w)] for y in range(h)]
    known = [[px[x, y][3] >= 128 for x in range(w)] for y in range(h)]
    todo = [(x, y) for y in range(h) for x in range(w) if not known[y][x]]
    while todo and len(todo) < w * h:
        ring, rest = [], []
        for x, y in todo:
            near = [rgb[(y + dy) % h][(x + dx) % w] for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                    if (dx or dy) and known[(y + dy) % h][(x + dx) % w]]
            if near:
                ring.append((x, y, tuple(round(sum(c[i] for c in near) / len(near)) for i in range(3))))
            else:
                rest.append((x, y))
        for x, y, c in ring:
            rgb[y][x] = c
            known[y][x] = True
        todo = rest
    out = Image.new("RGB", (w, h))
    out.putdata([c for row in rgb for c in row])
    return out


CLOCK_COLS = 4  # clock atlas: frames in rows of 4


def _facade(key: str) -> dict:
    return json.loads(FACADES.read_text(encoding="utf-8"))["textures"].get(key, {})


def clock_layout(key: str):
    """(columns, rows, [bitmap per cell]) of the clock atlas for a texture facades.json marks as a
    "clock" (cell i = the frame the original server shows at game hour i mod 12), else None."""
    if not _facade(key).get("clock"):
        return None
    info = json.loads((SRC / "catalog.json").read_text())["textures"][key]
    groups = info.get("groups") or [[i] for i in range(info["frames"])]
    order = [g[0] for g in groups[:12]]
    return CLOCK_COLS, -(-len(order) // CLOCK_COLS), order


def source_image(key: str) -> Image.Image:
    """The original as RGBA, or for a clock its frames packed into the atlas (clock_layout)."""
    layout = clock_layout(key)
    if not layout:
        return Image.open(SRC / (key + ".png")).convert("RGBA")
    cols, rows, order = layout
    frames = [Image.open(SRC / ("%s_f%02d.png" % (key, i))).convert("RGBA") for i in order]
    w, h = frames[0].size
    atlas = Image.new("RGBA", (cols * w, rows * h))
    for n, im in enumerate(frames):
        atlas.paste(im, ((n % cols) * w, (n // cols) * h))
    return atlas


def source_size(key: str) -> tuple:
    return source_image(key).size


def window_outlines(key: str) -> list:
    """Outlines (source_image pixels) of what glows at night on `key` (facades.json): the painted
    windows, and a clock's dial in every atlas cell."""
    sys.path.insert(0, str(ROOT / "tools" / "environment"))
    from facades import opening_outline
    desc = _facade(key)
    out = [[list(p) for p in opening_outline(op)] for op in desc.get("openings", []) if op.get("kind") == "window"]
    layout = clock_layout(key)
    if layout:
        info = json.loads((SRC / "catalog.json").read_text())["textures"][key]
        cols, rows, order = layout
        dial = opening_outline(dict(desc["clock"], shape="circle"))
        for n in range(len(order)):
            dx, dy = (n % cols) * info["w"], (n // cols) * info["h"]
            out.append([[x + dx, y + dy] for x, y in dial])
    return out


def flatten_windows(height: Image.Image, nrm: Image.Image, windows: list, orig_size: tuple) -> tuple:
    """Glass is flat: inside each painted window (`windows` from window_outlines) the height is
    filled in smoothly from the wall around it and the normals are flat. The pane and the wall
    sample the same texels where they meet, so the displaced pane still joins the wall, but the
    painted glazing bars no longer bend it (they read as wobbly glass otherwise)."""
    if not windows:
        return height, nrm
    sx, sy = height.width / orig_size[0], height.height / orig_size[1]
    glass = Image.new("L", height.size, 0)
    draw = ImageDraw.Draw(glass)
    for outline in windows:
        draw.polygon([(x * sx, y * sy) for x, y in outline], fill=255)
    wall = ImageChops.invert(glass)
    src = height.convert("RGB")
    mean = round(ImageStat.Stat(height.convert("L"), wall).mean[0])
    fill = Image.new("RGB", height.size, (mean,) * 3)  # deep inside wide windows, out of reach of the blurs
    for radius in (128, 32, 8, 2):
        premul = ImageChops.multiply(src, Image.merge("RGB", (wall,) * 3)).filter(ImageFilter.GaussianBlur(radius))
        weight = wall.filter(ImageFilter.GaussianBlur(radius))
        fill = Image.composite(_normalise(premul, weight), fill, weight.point(lambda a: 255 if a > 8 else 0))
    height = Image.composite(fill.getchannel(0), height.convert("L"), glass)
    nrm = Image.composite(Image.new("RGB", nrm.size, (128, 128, 255)), nrm.convert("RGB"), glass)
    return height, nrm


ESRGAN_DEFAULT = ROOT / "build" / "texai" / "realesrgan" / "realesrgan-ncnn-vulkan.exe"
MODELS_DIR = ROOT / "build" / "texai" / "models"  # spandrel models: <name>.safetensors
UPSCALED = ROOT / "build" / "texai" / "upscaled"
ESRGAN_PAD = 8  # texels of wrap-around context on each side, so tile edges upscale seamlessly
RELIEF_PYTHON = ROOT / "build" / "texai" / ".venv" / "Scripts" / "python.exe"


def _upscale_source(job):
    """The image a model upscales: the original (cut-out key colour filled), wrap-padded."""
    key, masked, path = job
    orig = source_image(key)
    wrap_crop(fill_cutout(orig) if masked else orig.convert("RGB"), ESRGAN_PAD).save(path)


def upscaled_path(key: str, model: str, code: str) -> Path:
    """Cache file of `key`'s 4x upscale by `model` (named by the original's pixels and the code)."""
    tag = hashlib.sha1((file_digest(SRC / (key + ".png")) + code).encode()).hexdigest()[:12]
    return UPSCALED / model / ("%s_%s.png" % (key, tag))


def run_upscales(todo: dict, exe: Path, jobs: int) -> None:
    """todo {model: [(key, masked, cache path)]}: each model runs once over its textures (alpha is
    handled by make_set; the models drop it)."""
    for model, items in todo.items():
        started = time.time()
        with tempfile.TemporaryDirectory() as tmp:
            src_dir, dst_dir = Path(tmp) / "in", Path(tmp) / "out"
            src_dir.mkdir()
            dst_dir.mkdir()
            with ProcessPoolExecutor(max_workers=max(1, min(jobs, len(items)))) as pool:
                list(pool.map(_upscale_source, [(key, masked, src_dir / (key + ".png")) for key, masked, _ in items]))
            weights = MODELS_DIR / (model + ".safetensors")
            if weights.exists():
                cmd = [RELIEF_PYTHON, ROOT / "tools" / "textures" / "upscale.py", "--model", weights, src_dir, dst_dir]
            else:
                cmd = [exe, "-i", src_dir, "-o", dst_dir, "-n", model, "-s", "4", "-f", "png"]
            subprocess.run([str(c) for c in cmd], check=True, capture_output=True)
            p = ESRGAN_PAD * 4
            for key, _, path in items:
                up = Image.open(dst_dir / (key + ".png")).convert("RGB")
                path.parent.mkdir(parents=True, exist_ok=True)
                up.crop((p, p, up.width - p, up.height - p)).save(path)
        print("upscaled %d originals with %s (%.0f s)" % (len(items), model, time.time() - started))


def wrap_crop(img: Image.Image, pad: int) -> Image.Image:
    """img tiled 3x3, cropped to img plus a `pad` border on every side (for seamless filtering)."""
    w, h = img.size
    big = Image.new(img.mode, (w * 3, h * 3))
    for i in range(3):
        for j in range(3):
            big.paste(img, (i * w, j * h))
    return big.crop((w - pad, h - pad, 2 * w + pad, 2 * h + pad))


def normal_map(height_img: Image.Image, strength: float, texel_scale: float) -> Image.Image:
    """Slopes of the height map from Sobel; X/Y squashed into [-1, 1] by g/sqrt(1+g^2)."""
    w, h = height_img.size
    pad = 2
    height = wrap_crop(height_img, pad)
    # Sobel on 0..255 heights spans -1020..1020; scale 8 + offset 128 keeps it in a byte.
    # Pillow flips kernels vertically before applying them, so ky is written upside down:
    # both give +slope where height rises to the right / down the image.
    kx = ImageFilter.Kernel((3, 3), [-1, 0, 1, -2, 0, 2, -1, 0, 1], scale=8, offset=128)
    ky = ImageFilter.Kernel((3, 3), [1, 2, 1, 0, 0, 0, -1, -2, -1], scale=8, offset=128)
    dx = height.filter(kx).crop((pad, pad, pad + w, pad + h))
    dy = height.filter(ky).crop((pad, pad, pad + w, pad + h))
    # slope per texel of the *original* art, so the bump strength doesn't depend on --scale
    gain = strength * texel_scale * BUMP / 255.0

    def channel(sign: float):
        lut = []
        for v in range(256):
            g = sign * (v - 128) * gain
            lut.append(round(127.5 + 127.5 * g / (1 + g * g) ** 0.5))
        return lut

    # tangent X: -dh/dx.  DirectX tangent Y points down the image, so -dh/dy as well.
    r = dx.point(channel(-1.0))
    g = dy.point(channel(-1.0))
    b = Image.new("L", (w, h), 255)
    return Image.merge("RGB", (r, g, b))


def make_set(key: str, info: dict, rules: dict, scale: int, max_side: int, upscaled: str | None) -> tuple[dict, list[str]]:
    """One texture set (D, N, H and, for timber, M) -> (manifest entry, files written). `rules` holds
    what the name-based rules resolved to, so editing a rule only remakes the textures it changes."""
    orig = erase_flame(source_image(key), rules.get("flame"))
    size = target_size(*orig.size, scale, max_side)
    masked = bool(info.get("has_transparency"))
    # cut-outs: replace the key colour before anything filters the image (see fill_cutout)
    src_rgb = fill_cutout(orig) if masked else orig.convert("RGB")
    if upscaled:
        rgb = Image.open(upscaled).convert("RGB").resize(size, Image.Resampling.LANCZOS)
    else:
        if rules["dedither"]:
            # 64 px grass/field art is mostly dither; upscaled, it reads as coloured noise
            src_rgb = src_rgb.filter(ImageFilter.MedianFilter(3))
            rgb = src_rgb.resize(size, Image.Resampling.BICUBIC)
        else:
            rgb = src_rgb.resize(size, Image.Resampling.LANCZOS)
            rgb = rgb.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
    # alpha stays hard-edged (the originals are 1-bit cut-outs)
    alpha = orig.getchannel("A").resize(size, Image.Resampling.BILINEAR).point(lambda a: 255 if a >= 128 else 0)
    if masked:
        # fill cut-out texels with the surrounding opaque colour (alpha-weighted blur), so mips,
        # filtering and solid rebuilds (zone art merlons) never show the palette's key colour
        fill = Image.new("RGB", size)
        for radius in (32, 8, 2):
            premul = ImageChops.multiply(rgb, Image.merge("RGB", (alpha,) * 3)).filter(ImageFilter.GaussianBlur(radius))
            weight = alpha.filter(ImageFilter.GaussianBlur(radius))
            layer = _normalise(premul, weight)
            fill = Image.composite(layer, fill, weight.point(lambda a: 255 if a > 8 else 0))
        rgb = Image.composite(rgb, fill, alpha)
    rough, strength, mode = rules["roughness"], rules["normal_strength"], rules["height_mode"]
    texel_scale = size[0] / orig.size[0]
    height = height_map(rgb, mode, texel_scale)
    nrm = normal_map(height, strength, texel_scale)
    if masked:
        # flat normals in the holes so the cut edge doesn't shade as a cliff
        nrm = Image.composite(nrm, Image.new("RGB", size, (128, 128, 255)), alpha)
    height, nrm = flatten_windows(height, nrm, rules["windows"], orig.size)

    d_name, n_name, h_name = "T_%s_D.png" % key, "T_%s_N.png" % key, "T_%s_H.png" % key
    (Image.merge("RGBA", (*rgb.split(), alpha)) if masked else rgb).save(OUT / d_name)
    nrm.save(OUT / n_name)
    height.save(OUT / h_name)
    files = [d_name, n_name, h_name]
    if mode == "timber":
        files.append("T_%s_M.png" % key)
        beam_mask(rgb, texel_scale, orig.size).save(OUT / files[-1])
    entry = {"name": info["name"], "d": d_name, "n": n_name, "height": h_name, "w": size[0], "h": size[1],
             "height_mode": mode, "masked": masked, "roughness": rough, "normal_strength": strength}
    if rules["clock"]:
        entry["atlas"] = list(rules["clock"][:2])  # the material picks the cell from the game hour
    if rules["windows"]:
        entry["emissive"] = "T_%s_E.png" % key
        files.append(entry["emissive"])
        window_mask(size, rules["windows"], orig.size).save(OUT / entry["emissive"])
    return entry, files


def wall_flame(key: str) -> dict | None:
    """props.json "fires" -> the preset "walls" entry for a wall texture with a painted flame (the
    wall torches): {"at": flame centre px, "out": texture direction away from the wall, "erase": rect}."""
    if not PROPS.exists():
        return None
    for name, preset in json.loads(PROPS.read_text(encoding="utf-8")).get("fires", {}).items():
        if isinstance(preset, dict) and key in preset.get("walls", {}):
            out = dict(preset["walls"][key], preset=name)
            # a 3D torch replaces the painted one (props.json fires "mesh"), once its kit mesh is made
            mesh = preset.get("mesh")
            if mesh and (ROOT / "build" / "environment" / "kit" / (mesh + ".glb")).exists():
                out["replaced_by"] = mesh
            return out
    return None


def is_flame(p) -> bool:
    """An opaque pixel painted as fire: bright red to yellow (the originals' flames), not the brown
    torch heads, grey brackets or copper bowls next to them."""
    r, g, b, a = p
    return a >= 128 and r >= 150 and r - b >= 70


def erase_flame(img: Image.Image, flame: dict | None) -> Image.Image:
    """The original without its painted flame (props.json fires "walls" "erase": [x0, y0, x1, y1] in the
    original's pixels): flame-coloured pixels inside the rect become transparent, so a flipbook flame
    can take their place (docs/adr/0005 phase 3). A clock atlas is never a flame texture."""
    if flame and flame.get("replaced_by"):
        # the whole torch is a 3D prop now: the texture draws nothing (its faces stay in the blockout)
        return Image.new("RGBA", img.size, (0, 255, 255, 0))
    if not flame or not flame.get("erase"):
        return img
    img = img.copy()
    px = img.load()
    x0, y0, x1, y1 = flame["erase"]
    for y in range(max(0, y0), min(img.height, y1)):
        for x in range(max(0, x0), min(img.width, x1)):
            if is_flame(px[x, y]):
                px[x, y] = (0, 255, 255, 0)
    return img


def fire_frames(preset: dict, catalog: dict) -> tuple:
    """-> ([RGBA frame], metres per pixel) of a props.json "fires" preset: "texture": a wall texture's
    animation frames (build/textures/<grd>_fNN.png), or "bgf" + "frames": an object's
    (build/bgf/<name>/frame_NN.png, from tools/bgf2png/bgf2png.py <name>; none if not extracted)."""
    if "texture" in preset:
        key = preset["texture"]
        info = catalog[key]
        groups = info.get("groups") or [[i] for i in range(info["frames"])]
        frames = [Image.open(SRC / ("%s_f%02d.png" % (key, g[0]))).convert("RGBA") for g in groups]
        return frames, info["squares_w"] * 2.2 / info["w"]
    folder = BGF / preset["bgf"]
    if not (folder / "meta.json").exists():
        return [], 0.0
    shrink = json.loads((folder / "meta.json").read_text())["shrink"]
    frames = [Image.open(folder / ("frame_%02d.png" % i)).convert("RGBA") for i in preset["frames"]]
    return frames, 2.2 / 64 / shrink  # 64 fine units to a 2.2 m square, shrunk by the bitmap's shrink


FIRE_COLS = 4  # flipbook cells per row
FIRE_SCALE = 4


def make_fires(catalog: dict, model: str | None, exe: Path | None) -> dict:
    """T_Fire_<name>.png for each props.json "fires" preset -> {name: manifest entry}. The crop
    ("crop": [x0, y0, x1, y1] in the original's pixels) keeps flame-coloured pixels only ("keep":
    "flame", for flames drawn over a torch head or a bowl) or every opaque pixel ("keep": "all")."""
    presets = {k: v for k, v in json.loads(PROPS.read_text(encoding="utf-8")).get("fires", {}).items()
               if not k.startswith("_")}
    pad = 8
    crops = {}
    for name, preset in list(presets.items()):
        frames, mpp = fire_frames(preset, catalog)
        if not frames:
            print("fire %s: no frames (run python tools/bgf2png/bgf2png.py %s); skipped" % (name, preset.get("bgf")))
            del presets[name]
            continue
        x0, y0, x1, y1 = preset["crop"]
        out = []
        for im in frames:
            c = im.crop((x0, y0, x1, y1))
            if preset.get("keep", "flame") == "flame":
                px = c.load()
                for y in range(c.height):
                    for x in range(c.width):
                        if not is_flame(px[x, y]):
                            px[x, y] = (0, 255, 255, 0)
            framed = Image.new("RGBA", (c.width + 2 * pad, c.height + 2 * pad), (0, 255, 255, 0))
            framed.paste(c, (pad, pad))
            out.append(framed)
        crops[name] = (out, mpp)
    # one upscale run over every frame (the base-colour model, as the textures)
    ups = {}
    with tempfile.TemporaryDirectory() as tmp:
        src_dir, dst_dir = Path(tmp) / "in", Path(tmp) / "out"
        src_dir.mkdir()
        dst_dir.mkdir()
        for name, (frames, _) in crops.items():
            for i, f in enumerate(frames):
                fill_cutout(f).save(src_dir / ("%s_%02d.png" % (name, i)))
        if model and exe:
            weights = MODELS_DIR / (model + ".safetensors")
            if weights.exists() and RELIEF_PYTHON.exists():
                cmd = [RELIEF_PYTHON, ROOT / "tools" / "textures" / "upscale.py", "--model", weights, src_dir, dst_dir]
            else:
                cmd = [exe, "-i", src_dir, "-o", dst_dir, "-n", model, "-s", "4", "-f", "png"]
            subprocess.run([str(c) for c in cmd], check=True, capture_output=True)
        for name, (frames, _) in crops.items():
            for i, f in enumerate(frames):
                p = dst_dir / ("%s_%02d.png" % (name, i))
                big = (f.width * FIRE_SCALE, f.height * FIRE_SCALE)
                rgb = (Image.open(p).convert("RGB").resize(big, Image.Resampling.LANCZOS) if p.exists()
                       else fill_cutout(f).resize(big, Image.Resampling.LANCZOS))
                # soft edges: the originals' 1-bit flame outline, smoothed at the upscaled size
                alpha = f.getchannel("A").point(lambda a: 255 if a >= 128 else 0).resize(big, Image.Resampling.BILINEAR)
                alpha = alpha.filter(ImageFilter.GaussianBlur(FIRE_SCALE * 0.4))
                ups[(name, i)] = Image.merge("RGBA", (*rgb.split(), alpha))
    entries = {}
    for name, preset in presets.items():
        frames, mpp = crops[name]
        x0, y0, x1, y1 = preset["crop"]
        cw, ch = (x1 - x0) * FIRE_SCALE, (y1 - y0) * FIRE_SCALE
        cell = (pow2(cw), pow2(ch))
        cols = min(FIRE_COLS, len(frames))
        rows = -(-len(frames) // cols)
        atlas = Image.new("RGBA", (pow2(cols * cell[0]), pow2(rows * cell[1])), (0, 0, 0, 0))
        cols = atlas.width // cell[0]
        rows = -(-len(frames) // cols)
        p = pad * FIRE_SCALE
        for i in range(len(frames)):
            im = ups[(name, i)].crop((p, p, p + cw, p + ch))
            cx, cy = (i % cols) * cell[0], (i // cols) * cell[1]
            atlas.paste(im, (cx + (cell[0] - cw) // 2, cy + (cell[1] - ch) // 2))
        # transparent texels take the flame's colour nearby, so filtering never darkens the edge
        rgb, solid = atlas.convert("RGB"), atlas.getchannel("A").point(lambda a: 255 if a >= 8 else 0)
        fill = Image.new("RGB", atlas.size)
        for radius in (32, 8, 2):
            premul = ImageChops.multiply(rgb, Image.merge("RGB", (solid,) * 3)).filter(ImageFilter.GaussianBlur(radius))
            weight = solid.filter(ImageFilter.GaussianBlur(radius))
            fill = Image.composite(_normalise(premul, weight), fill, weight.point(lambda a: 255 if a > 8 else 0))
        atlas = Image.merge("RGBA", (*Image.composite(rgb, fill, solid).split(), atlas.getchannel("A")))
        f = "T_Fire_%s.png" % name
        atlas.save(OUT / f)
        scale = mpp / FIRE_SCALE
        entries[name] = {"file": f, "cols": cols, "rows": atlas.height // cell[1], "frames": len(frames),
                         "fps": preset.get("fps", 8), "brightness": preset.get("brightness", 4.0),
                         "size_m": [round(cell[0] * scale, 4), round(cell[1] * scale, 4)],
                         "flame_m": [round(cw * scale, 4), round(ch * scale, 4)]}
        print("fire %-8s %d frames, %dx%d atlas, flame %.2f x %.2f m" % (name, len(frames), atlas.width, atlas.height,
                                                                         cw * scale, ch * scale))
    return entries


def window_mask(size: tuple, windows: list, orig_size: tuple) -> Image.Image:
    """T_<grd>_E.png: where the painted windows are (255 = glass), at a quarter of the base colour's
    size, edges softened by half a texel of the original art. The materials light it up at night
    (WindowGlow in the mood's Material Parameter Collection)."""
    w, h = max(4, size[0] // 4), max(4, size[1] // 4)
    sx, sy = w / orig_size[0], h / orig_size[1]
    mask = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask)
    for outline in windows:
        draw.polygon([(x * sx, y * sy) for x, y in outline], fill=255)
    return mask.filter(ImageFilter.GaussianBlur(max(0.5, sx * 0.5)))


# a painted window glows in its own glass colour x the window light (environment_materials
# _window_glow); glass painted much darker than the usual pale pane barely glowed (the Inn's dark
# teal, 2026-10-06), so dark glass is brought up towards the typical pane. Pale glass is never
# dimmed: the shops' bright panes and the stained glass look right in their own colours.
GLOW_TARGET_LUM = 0.04   # linear luminance of the typical pane (Raza's: 0.013-0.18)
GLOW_GAIN_RANGE = (1.0, 4.0)


def glow_gain(d_path: Path, e_path: Path) -> float:
    """How much to brighten a texture's window glow so dark painted glass (mean linear luminance under
    the window mask) glows like a typical pane: 1 for pale glass, up to GLOW_GAIN_RANGE[1]."""
    d = Image.open(d_path).convert("RGB")
    mask = Image.open(e_path).convert("L").resize(d.size)
    lum = d.convert("L").point(lambda v: round(255 * ((v / 255) / 12.92 if v / 255 <= 0.04045 else ((v / 255 + 0.055) / 1.055) ** 2.4)))
    sel = mask.point(lambda v: 255 if v > 127 else 0)
    stat = ImageStat.Stat(lum, sel)
    mean = stat.mean[0] / 255 if stat.count[0] else 0.0
    return round(min(GLOW_GAIN_RANGE[1], max(GLOW_GAIN_RANGE[0], GLOW_TARGET_LUM / max(mean, 1e-4))), 3)


# the code a texture set depends on (rule tables are resolved per texture instead, see set_rules)
SET_CODE = (source_image, clock_layout, make_set, fill_cutout, height_map, normal_map, beam_mask, _beam_mask, otsu, _normalise, target_size, pow2,
            wrap_crop, flatten_windows, window_mask, erase_flame, is_flame)
FIRE_CODE = (make_fires, fire_frames, is_flame, fill_cutout, pow2, _normalise)
UPSCALE_CODE = (_upscale_source, run_upscales, wrap_crop, fill_cutout, source_image, clock_layout)


def relief_rules_for() -> str:
    """materials.json "relief" "rules_for": names whose rule-based maps tools/textures/ai_maps.py
    --apply leaves in place instead of Marigold's."""
    relief = json.loads(MATERIALS.read_text(encoding="utf-8")).get("relief", {}) if MATERIALS.exists() else {}
    return (relief.get("rules_for") or "") if isinstance(relief, dict) else ""


def set_rules(key: str, info: dict, rules_for: str = "") -> dict:
    rough, strength = surface(info["name"])
    return {"roughness": rough, "normal_strength": strength, "height_mode": height_mode(info["name"]),
            "dedither": bool(re.search(DEDITHER, info["name"], re.I)), "windows": window_outlines(key), "clock": clock_layout(key),
            "esrgan_model": esrgan_model(info["name"]), "flame": wall_flame(key),
            # a texture moving to (or from) the rule-based maps must be remade: ai_maps.py has
            # overwritten its maps with Marigold's
            "rule_relief": bool(rules_for and re.search(rules_for, info["name"], re.I))}


def file_digest(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def set_key(key: str, info: dict, rules: dict, options: list, code: str) -> str:
    blob = json.dumps([file_digest(SRC / (key + ".png")), info.get("name"), bool(info.get("has_transparency")),
                       rules, options, code], sort_keys=True)
    return hashlib.sha1(blob.encode("utf-8")).hexdigest()


def _make_job(job):
    return job[0], make_set(*job)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scale", type=int, default=4, help="upscale factor before power-of-two rounding")
    ap.add_argument("--max", type=int, default=2048, help="largest side in pixels")
    ap.add_argument("--esrgan", type=Path, help="realesrgan-ncnn-vulkan.exe (default: %s, if present)" % ESRGAN_DEFAULT)
    ap.add_argument("--no-esrgan", action="store_true", help="Lanczos upscaling even when Real-ESRGAN is installed")
    ap.add_argument("--esrgan-model", help="one upscale model for every texture instead of ESRGAN_RULES (comparisons)")
    ap.add_argument("--relief", choices=("auto", "rules"), default="auto",
                    help="auto: Marigold height/normal maps (tools/textures/ai_maps.py --apply) when installed")
    ap.add_argument("--force", action="store_true", help="remake every texture set")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4, help="parallel processes")
    args = ap.parse_args()

    catalog_path = SRC / "catalog.json"
    if not catalog_path.exists():
        sys.exit("missing %s: run tools/bgf2png/bgf2png.py --textures-for-zones first" % catalog_path)
    catalog = json.loads(catalog_path.read_text())["textures"]
    if args.esrgan and not args.esrgan.exists():
        sys.exit("--esrgan: %s not found" % args.esrgan)
    if not args.esrgan and not args.no_esrgan and ESRGAN_DEFAULT.exists():
        args.esrgan = ESRGAN_DEFAULT

    started = time.time()
    OUT.mkdir(parents=True, exist_ok=True)
    cache_path = OUT / "cache.json"
    cache = {} if args.force or not cache_path.exists() else json.loads(cache_path.read_text())
    code = hashlib.sha1(("".join(inspect.getsource(f) for f in SET_CODE) + repr((STONE_RADIUS, BUMP))).encode()).hexdigest()
    up_code = hashlib.sha1("".join(inspect.getsource(f) for f in UPSCALE_CODE).encode()).hexdigest()
    options = [args.scale, args.max, bool(args.esrgan)]

    manifest, new_cache, jobs, todo, missing = {}, {}, [], {}, set()
    rules_for = relief_rules_for()
    for key, info in sorted(catalog.items()):
        rules = set_rules(key, info, rules_for)
        if args.esrgan_model:
            rules["esrgan_model"] = args.esrgan_model
        if args.esrgan:
            wanted = rules["esrgan_model"]
            rules["esrgan_model"] = available_model(wanted, args.esrgan)
            if rules["esrgan_model"] != wanted:
                missing.add(wanted)
        up = upscaled_path(key, rules["esrgan_model"], up_code) if args.esrgan else None
        k = set_key(key, info, rules, options + [up.name if up else None], code)
        old = cache.get(key)
        if old and old["key"] == k and all((OUT / f).exists() for f in old["files"]):
            manifest[key], new_cache[key] = old["entry"], old
        else:
            jobs.append((key, info, rules, args.scale, args.max, str(up) if up else None))
            new_cache[key] = {"key": k}
            if up and not up.exists():
                todo.setdefault(rules["esrgan_model"], []).append((key, bool(info.get("has_transparency")), up))
    for model in sorted(missing):
        print("upscale model %s isn't installed (tools/textures/setup_ai.ps1); using %s" % (model, ESRGAN_FALLBACK))
    if todo:
        run_upscales(todo, args.esrgan, args.jobs)

    if jobs:
        with ProcessPoolExecutor(max_workers=max(1, min(args.jobs, len(jobs)))) as pool:
            for key, (entry, files) in pool.map(_make_job, jobs):
                manifest[key] = entry
                new_cache[key].update(entry=entry, files=files)
                print("%s %-34s %4dx%-4d %s rough %.2f %s" % (key, entry["name"][:34], entry["w"], entry["h"],
                                                              "masked" if entry["masked"] else "      ",
                                                              entry["roughness"], entry["height_mode"]))

    extras = {"macro": "T_MacroNoise.png", "water_normal": "T_WaterNormal.png", "no_emissive": "T_NoEmissive.png",
              "stars": "T_Stars.png", "bolts": "T_Bolts.png"}
    extras_code = hashlib.sha1("".join(inspect.getsource(f) for f in (macro_noise, water_normal, stars, bolts)).encode()).hexdigest()
    if cache.get("_extras", {}).get("key") != extras_code or not all((OUT / f).exists() for f in extras.values()):
        macro_noise().save(OUT / extras["macro"])
        water_normal().save(OUT / extras["water_normal"])
        stars().save(OUT / extras["stars"])
        bolts().save(OUT / extras["bolts"])
        Image.new("L", (4, 4), 0).save(OUT / extras["no_emissive"])  # the materials' default window mask
    new_cache["_extras"] = {"key": extras_code, "files": list(extras.values())}

    # flame flipbooks (props.json "fires")
    fire_model = available_model(esrgan_model("fire"), args.esrgan) if args.esrgan else None
    fire_inputs = [hashlib.sha1("".join(inspect.getsource(f) for f in FIRE_CODE).encode()).hexdigest(), fire_model,
                   json.loads(PROPS.read_text(encoding="utf-8")).get("fires", {}) if PROPS.exists() else {}]
    for d in sorted(BGF.glob("*/frame_*.png")) + sorted(SRC.glob("grd*_f*.png")):
        if any(d.name.startswith(p.get("texture", "-")) or d.parent.name == p.get("bgf") for p in fire_inputs[2].values()
               if isinstance(p, dict)):
            fire_inputs.append(file_digest(d))
    fire_key = hashlib.sha1(json.dumps(fire_inputs, sort_keys=True).encode()).hexdigest()
    old_fires = cache.get("_fires", {})
    if old_fires.get("key") == fire_key and all((OUT / f).exists() for f in old_fires.get("files", [])):
        fires = old_fires["entries"]
    else:
        fires = make_fires(catalog, fire_model, args.esrgan) if PROPS.exists() else {}
    new_cache["_fires"] = {"key": fire_key, "entries": fires, "files": [e["file"] for e in fires.values()]}

    # anything else in the folder is from a texture no longer in the catalog
    keep = {f for entry in new_cache.values() for f in entry.get("files", [])} | {"placeholders.json", "cache.json", "relief.json"}
    for f in OUT.iterdir():
        # (the prop gallery's sprite pictures live here too: tools/environment/prop_gallery.py writes them)
        if f.is_file() and f.name not in keep and not f.name.startswith("T_GallerySprite_"):
            f.unlink()

    manifest = dict(sorted(manifest.items()))
    facade_cfg = json.loads(FACADES.read_text(encoding="utf-8")).get("textures", {}) if FACADES.exists() else {}
    for key, entry in manifest.items():
        if entry.get("emissive"):
            # facades.json "glow_gain": a multiplier on top (1 = as bright as every other window);
            # "glow_tint": how much of the glass's own colour to keep (1, the default: all of it;
            # 0: the shared WindowGlowColor only)
            cfg = facade_cfg.get(key, {})
            entry["glow_gain"] = round(glow_gain(OUT / entry["d"], OUT / entry["emissive"]) * float(cfg.get("glow_gain", 1.0)), 3)
            entry["glow_tint"] = float(cfg.get("glow_tint", 1.0))
    (OUT / "placeholders.json").write_text(json.dumps({"textures": manifest, "extras": extras, "fires": fires}, indent=1, sort_keys=True))
    cache_path.write_text(json.dumps(new_cache, indent=0, sort_keys=True))
    print("%d placeholder texture sets in %s: %d remade, %d unchanged, base colour %s (%.0f s)"
          % (len(manifest), OUT.relative_to(ROOT), len(jobs), len(manifest) - len(jobs),
             (args.esrgan_model or "AI upscale (ESRGAN_RULES)") if args.esrgan else "Lanczos", time.time() - started))

    if args.relief == "auto":
        if RELIEF_PYTHON.exists():
            cmd = [str(RELIEF_PYTHON), str(ROOT / "tools" / "textures" / "ai_maps.py"), "--apply"]
            subprocess.run(cmd + (["--force"] if args.force else []), check=True)
        else:
            print("relief: rule-based height/normal maps (no %s; see README to install the AI relief step)"
                  % RELIEF_PYTHON.relative_to(ROOT))


if __name__ == "__main__":
    main()
