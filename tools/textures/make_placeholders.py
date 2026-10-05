"""
Placeholder PBR textures from the extracted originals, for lighting / post-process / VFX tests
(ADR 0003, Phase 0). Needs only Pillow. Run after tools/bgf2png --textures-for-zones:

    python tools/textures/make_placeholders.py [--scale 4] [--max 2048] [--esrgan PATH]

For every texture in build/textures/catalog.json it writes, to build/textures_placeholder/:
  T_<grd>_D.png   base colour: upscaled (Lanczos, or Real-ESRGAN when --esrgan points at
                  realesrgan-ncnn-vulkan.exe) and resized to power-of-two sides so UE gets mips.
                  The UVs from roo2gltf span one texture repeat, so the stretch doesn't change the mapping.
  T_<grd>_H.png   height (0 = deepest), for normals and Nanite displacement. Two modes:
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
import json
import re
import shutil
import subprocess
import sys
import tempfile
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


def esrgan_upscale(exe: Path, src: Path) -> Image.Image:
    """4x with realesrgan-ncnn-vulkan; alpha is upscaled separately by Lanczos (ESRGAN drops it)."""
    with tempfile.TemporaryDirectory() as tmp:
        rgb_in, rgb_out = Path(tmp) / "in.png", Path(tmp) / "out.png"
        Image.open(src).convert("RGB").save(rgb_in)
        subprocess.run([str(exe), "-i", str(rgb_in), "-o", str(rgb_out), "-n", "realesrgan-x4plus"],
                       check=True, capture_output=True)
        return Image.open(rgb_out).convert("RGB")


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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scale", type=int, default=4, help="upscale factor before power-of-two rounding")
    ap.add_argument("--max", type=int, default=2048, help="largest side in pixels")
    ap.add_argument("--esrgan", type=Path, help="path to realesrgan-ncnn-vulkan.exe (optional)")
    args = ap.parse_args()

    catalog_path = SRC / "catalog.json"
    if not catalog_path.exists():
        sys.exit("missing %s: run tools/bgf2png/bgf2png.py --textures-for-zones first" % catalog_path)
    catalog = json.loads(catalog_path.read_text())["textures"]
    if args.esrgan and not args.esrgan.exists():
        sys.exit("--esrgan: %s not found" % args.esrgan)

    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True)
    manifest = {}
    for key, info in sorted(catalog.items()):
        src = SRC / (key + ".png")
        orig = Image.open(src).convert("RGBA")
        size = target_size(*orig.size, args.scale, args.max)
        if args.esrgan:
            rgb = esrgan_upscale(args.esrgan, src).resize(size, Image.Resampling.LANCZOS)
        else:
            src_rgb = orig.convert("RGB")
            if re.search(DEDITHER, info["name"], re.I):
                # 64 px grass/field art is mostly dither; upscaled, it reads as coloured noise
                src_rgb = src_rgb.filter(ImageFilter.MedianFilter(3))
                rgb = src_rgb.resize(size, Image.Resampling.BICUBIC)
            else:
                rgb = src_rgb.resize(size, Image.Resampling.LANCZOS)
                rgb = rgb.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
        # alpha stays hard-edged (the originals are 1-bit cut-outs)
        alpha = orig.getchannel("A").resize(size, Image.Resampling.BILINEAR).point(lambda a: 255 if a >= 128 else 0)
        masked = bool(info.get("has_transparency"))
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
        rough, strength = surface(info["name"])
        texel_scale = size[0] / orig.size[0]
        mode = height_mode(info["name"])
        height = height_map(rgb, mode, texel_scale)
        nrm = normal_map(height, strength, texel_scale)
        if masked:
            # flat normals in the holes so the cut edge doesn't shade as a cliff
            nrm = Image.composite(nrm, Image.new("RGB", size, (128, 128, 255)), alpha)

        d_name, n_name, h_name = "T_%s_D.png" % key, "T_%s_N.png" % key, "T_%s_H.png" % key
        (Image.merge("RGBA", (*rgb.split(), alpha)) if masked else rgb).save(OUT / d_name)
        nrm.save(OUT / n_name)
        height.save(OUT / h_name)
        if mode == "timber":
            beam_mask(rgb, texel_scale, orig.size).save(OUT / ("T_%s_M.png" % key))
        manifest[key] = {"name": info["name"], "d": d_name, "n": n_name, "height": h_name, "w": size[0], "h": size[1],
                         "height_mode": mode, "masked": masked, "roughness": rough, "normal_strength": strength}
        print("%s %-34s %4dx%-4d %s rough %.2f %s" % (key, info["name"][:34], size[0], size[1],
                                                      "masked" if masked else "      ", rough, mode))

    macro_noise().save(OUT / "T_MacroNoise.png")
    water_normal().save(OUT / "T_WaterNormal.png")
    extras = {"macro": "T_MacroNoise.png", "water_normal": "T_WaterNormal.png"}
    (OUT / "placeholders.json").write_text(json.dumps({"textures": manifest, "extras": extras}, indent=1))
    print("wrote %d placeholder texture sets to %s" % (len(manifest), OUT.relative_to(ROOT)))


if __name__ == "__main__":
    main()
