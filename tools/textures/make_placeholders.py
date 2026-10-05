"""
PBR textures from the extracted originals (ADR 0003). Needs Pillow; uses Real-ESRGAN and the AI
relief environment when they are installed (README, "Environment art"). Run after
tools/bgf2png --textures-for-zones:

    python tools/textures/make_placeholders.py [--scale 4] [--max 2048] [--esrgan PATH | --no-esrgan]
                                               [--relief auto|rules] [--force] [--jobs N]

  base colour   Real-ESRGAN (realesrgan-x4plus) 4x of the original, wrap-padded so tiling textures
                stay seamless, when build/texai/realesrgan/realesrgan-ncnn-vulkan.exe exists (or
                --esrgan PATH); otherwise Lanczos + sharpening (--no-esrgan forces that).
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
  T_<grd>_M.png   timber textures only: the beam mask at the original's size (255 = beam), from
                  which build_zone_art.py can extrude real beams.
  T_<grd>_N.png   tangent-space normal map, DirectX convention (green = down), from the height. Filters
                  wrap around the tile edges so tiling textures stay seamless. Blue is a constant: UE
                  stores normal maps as BC5 and rebuilds Z from X/Y.
  T_WaterNormal.png   tileable ripple normal map for the water material.
  T_MacroNoise.png    tileable low-frequency noise (R large, G medium, B small blobs) for breaking up
                      tiling and tinting ground and grass across the world (world-aligned in UE).
  placeholders.json   per texture: file names, masked (has transparency), roughness, normal strength;
                      "extras": shared textures (macro noise).

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

from PIL import Image, ImageChops, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "build" / "textures"
OUT = ROOT / "build" / "textures_placeholder"

# (name pattern, roughness, normal strength); first match wins
SURFACE_RULES = [
    (r"water", 0.08, 0.15),
    (r"marble", 0.35, 0.6),
    (r"sign|tapestry|carpet|cloth|clock|picture", 0.8, 0.35),
    (r"cage|fence|gate|torch", 0.55, 0.8),
    (r"grass|field|forest|tree", 0.95, 0.7),
    (r"rock|stone|path|brick|flagston|mausoleum|wall", 0.9, 1.0),
    (r"wood|roof|door|floor|cab|shelf|table|bar|chair|barrel", 0.75, 0.8),
]
DEFAULT_SURFACE = (0.85, 0.8)
# height mode by name; first match wins, default "luma"
HEIGHT_RULES = [
    (r"sign|tapestry|carpet|cloth|clock|picture|water|fence|cage|gate|torch|tree|forest", "luma"),
    (r"\bint|floor|ceil|cieling|cab\b|table|chair|barrel|tos-", "luma"),  # interiors and furniture
    (r"roof|chimney", "stones"),  # tiles and chimney bricks dome; their dark lines are gaps
    (r"bldg-?[abgj]\b", "timber"),
    (r"stone|rock|cobble|brick|flagston|mausoleum|bldg-?[hilm]\b|bldgl|def|new building|raz-wall|path|repeating", "stones"),
]
STONE_RADIUS = 3.5  # dome radius in texels of the original art (typical stone half-width)
DEDITHER = r"grass|field"  # originals de-dithered (median) before upscaling
BUMP = 6.0  # height of a black-to-white luminance step, in texels of the original art


def surface(name: str) -> tuple[float, float]:
    for pat, rough, strength in SURFACE_RULES:
        if re.search(pat, name, re.I):
            return rough, strength
    return DEFAULT_SURFACE


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


ESRGAN_DEFAULT = ROOT / "build" / "texai" / "realesrgan" / "realesrgan-ncnn-vulkan.exe"
ESRGAN_PAD = 8  # texels of wrap-around context on each side, so tile edges upscale seamlessly
RELIEF_PYTHON = ROOT / "build" / "texai" / ".venv" / "Scripts" / "python.exe"


def esrgan_upscale(exe: Path, src: Image.Image) -> Image.Image:
    """4x with realesrgan-ncnn-vulkan (realesrgan-x4plus) of the wrap-padded image, cropped back;
    alpha is handled separately (ESRGAN drops it)."""
    with tempfile.TemporaryDirectory() as tmp:
        rgb_in, rgb_out = Path(tmp) / "in.png", Path(tmp) / "out.png"
        wrap_crop(src.convert("RGB"), ESRGAN_PAD).save(rgb_in)
        subprocess.run([str(exe), "-i", str(rgb_in), "-o", str(rgb_out), "-n", "realesrgan-x4plus", "-s", "4"],
                       check=True, capture_output=True)
        up = Image.open(rgb_out).convert("RGB")
        p = ESRGAN_PAD * 4
        return up.crop((p, p, up.width - p, up.height - p))


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


def make_set(key: str, info: dict, rules: dict, scale: int, max_side: int, esrgan: str | None) -> tuple[dict, list[str]]:
    """One texture set (D, N, H and, for timber, M) -> (manifest entry, files written). `rules` holds
    what the name-based rules resolved to, so editing a rule only remakes the textures it changes."""
    src = SRC / (key + ".png")
    orig = Image.open(src).convert("RGBA")
    size = target_size(*orig.size, scale, max_side)
    masked = bool(info.get("has_transparency"))
    # cut-outs: replace the key colour before anything filters the image (see fill_cutout)
    src_rgb = fill_cutout(orig) if masked else orig.convert("RGB")
    if esrgan:
        rgb = esrgan_upscale(Path(esrgan), src_rgb).resize(size, Image.Resampling.LANCZOS)
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
    return entry, files


# the code a texture set depends on (rule tables are resolved per texture instead, see set_rules)
SET_CODE = (make_set, fill_cutout, height_map, normal_map, beam_mask, _beam_mask, otsu, _normalise, target_size, pow2,
            wrap_crop, esrgan_upscale)


def set_rules(info: dict) -> dict:
    rough, strength = surface(info["name"])
    return {"roughness": rough, "normal_strength": strength, "height_mode": height_mode(info["name"]),
            "dedither": bool(re.search(DEDITHER, info["name"], re.I))}


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
    if args.esrgan:
        args.jobs = min(args.jobs, 3)  # the upscales share one GPU

    started = time.time()
    OUT.mkdir(parents=True, exist_ok=True)
    cache_path = OUT / "cache.json"
    cache = {} if args.force or not cache_path.exists() else json.loads(cache_path.read_text())
    code = hashlib.sha1(("".join(inspect.getsource(f) for f in SET_CODE) + repr((STONE_RADIUS, BUMP))).encode()).hexdigest()
    esrgan = str(args.esrgan) if args.esrgan else None
    options = [args.scale, args.max, esrgan, args.esrgan.stat().st_mtime if args.esrgan else None]

    manifest, new_cache, jobs = {}, {}, []
    for key, info in sorted(catalog.items()):
        rules = set_rules(info)
        k = set_key(key, info, rules, options, code)
        old = cache.get(key)
        if old and old["key"] == k and all((OUT / f).exists() for f in old["files"]):
            manifest[key], new_cache[key] = old["entry"], old
        else:
            jobs.append((key, info, rules, args.scale, args.max, esrgan))
            new_cache[key] = {"key": k}

    if jobs:
        with ProcessPoolExecutor(max_workers=max(1, min(args.jobs, len(jobs)))) as pool:
            for key, (entry, files) in pool.map(_make_job, jobs):
                manifest[key] = entry
                new_cache[key].update(entry=entry, files=files)
                print("%s %-34s %4dx%-4d %s rough %.2f %s" % (key, entry["name"][:34], entry["w"], entry["h"],
                                                              "masked" if entry["masked"] else "      ",
                                                              entry["roughness"], entry["height_mode"]))

    extras = {"macro": "T_MacroNoise.png", "water_normal": "T_WaterNormal.png"}
    extras_code = hashlib.sha1("".join(inspect.getsource(f) for f in (macro_noise, water_normal)).encode()).hexdigest()
    if cache.get("_extras", {}).get("key") != extras_code or not all((OUT / f).exists() for f in extras.values()):
        macro_noise().save(OUT / extras["macro"])
        water_normal().save(OUT / extras["water_normal"])
    new_cache["_extras"] = {"key": extras_code, "files": list(extras.values())}

    # anything else in the folder is from a texture no longer in the catalog
    keep = {f for entry in new_cache.values() for f in entry.get("files", [])} | {"placeholders.json", "cache.json", "relief.json"}
    for f in OUT.iterdir():
        if f.is_file() and f.name not in keep:
            f.unlink()

    manifest = dict(sorted(manifest.items()))
    (OUT / "placeholders.json").write_text(json.dumps({"textures": manifest, "extras": extras}, indent=1, sort_keys=True))
    cache_path.write_text(json.dumps(new_cache, indent=0, sort_keys=True))
    print("%d placeholder texture sets in %s: %d remade, %d unchanged, base colour %s (%.0f s)"
          % (len(manifest), OUT.relative_to(ROOT), len(jobs), len(manifest) - len(jobs),
             "Real-ESRGAN" if args.esrgan else "Lanczos", time.time() - started))

    if args.relief == "auto":
        if RELIEF_PYTHON.exists():
            cmd = [str(RELIEF_PYTHON), str(ROOT / "tools" / "textures" / "ai_maps.py"), "--apply"]
            subprocess.run(cmd + (["--force"] if args.force else []), check=True)
        else:
            print("relief: rule-based height/normal maps (no %s; see README to install the AI relief step)"
                  % RELIEF_PYTHON.relative_to(ROOT))


if __name__ == "__main__":
    main()
