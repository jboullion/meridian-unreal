"""
Placeholder PBR textures from the extracted originals, for lighting / post-process / VFX tests
(ADR 0003, Phase 0). Needs only Pillow. Run after tools/bgf2png --textures-for-zones:

    python tools/textures/make_placeholders.py [--scale 4] [--max 2048] [--esrgan PATH]

For every texture in build/textures/catalog.json it writes, to build/textures_placeholder/:
  T_<grd>_D.png   base colour: upscaled (Lanczos, or Real-ESRGAN when --esrgan points at
                  realesrgan-ncnn-vulkan.exe) and resized to power-of-two sides so UE gets mips.
                  The UVs from roo2gltf span one texture repeat, so the stretch doesn't change the mapping.
  T_<grd>_N.png   tangent-space normal map, DirectX convention (green = down), derived from the base
                  colour's luminance as a height field (dark cracks/mortar = low). Filters wrap around
                  the tile edges so tiling textures stay seamless. Blue is a constant: UE stores normal
                  maps as BC5 and rebuilds Z from X/Y.
  placeholders.json   per texture: file names, masked (has transparency), roughness, normal strength.

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

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "build" / "textures"
OUT = ROOT / "build" / "textures_placeholder"

# (name pattern, roughness, normal strength); first match wins
SURFACE_RULES = [
    (r"water", 0.08, 0.15),
    (r"marble", 0.35, 0.6),
    (r"sign|tapestry|carpet|cloth|clock|picture|stained", 0.8, 0.35),
    (r"cage|fence|gate|torch", 0.55, 0.8),
    (r"grass|field|forest|tree", 0.95, 0.7),
    (r"rock|stone|path|brick|flagston|mausoleum|wall", 0.9, 1.0),
    (r"wood|roof|door|floor|cab|shelf|table|bar|chair|barrel", 0.75, 0.8),
]
DEFAULT_SURFACE = (0.85, 0.8)
BUMP = 6.0  # height of a black-to-white luminance step, in texels of the original art


def surface(name: str) -> tuple[float, float]:
    for pat, rough, strength in SURFACE_RULES:
        if re.search(pat, name, re.I):
            return rough, strength
    return DEFAULT_SURFACE


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


def normal_map(color: Image.Image, strength: float, texel_scale: float) -> Image.Image:
    """Height = blurred luminance; slopes from Sobel; X/Y squashed into [-1, 1] by g/sqrt(1+g^2)."""
    w, h = color.size
    blur = max(1.0, texel_scale * 0.6)  # hide the original's dither, keep its shapes
    pad = int(blur * 3) + 2
    height = wrap_crop(color.convert("L"), pad).filter(ImageFilter.GaussianBlur(blur))
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
            rgb = orig.convert("RGB").resize(size, Image.Resampling.LANCZOS)
            rgb = rgb.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
        # alpha stays hard-edged (the originals are 1-bit cut-outs)
        alpha = orig.getchannel("A").resize(size, Image.Resampling.BILINEAR).point(lambda a: 255 if a >= 128 else 0)
        masked = bool(info.get("has_transparency"))
        if masked:
            # bleed colour into cut-out texels so mips and filtering don't pull in the palette's key colour
            solid = rgb.filter(ImageFilter.MaxFilter(5))
            rgb = Image.composite(rgb, solid, alpha)
        rough, strength = surface(info["name"])
        texel_scale = size[0] / orig.size[0]
        nrm = normal_map(rgb, strength, texel_scale)
        if masked:
            # flat normals in the holes so the cut edge doesn't shade as a cliff
            nrm = Image.composite(nrm, Image.new("RGB", size, (128, 128, 255)), alpha)

        d_name, n_name = "T_%s_D.png" % key, "T_%s_N.png" % key
        (Image.merge("RGBA", (*rgb.split(), alpha)) if masked else rgb).save(OUT / d_name)
        nrm.save(OUT / n_name)
        manifest[key] = {"name": info["name"], "d": d_name, "n": n_name, "w": size[0], "h": size[1],
                         "masked": masked, "roughness": rough, "normal_strength": strength}
        print("%s %-34s %4dx%-4d %s rough %.2f" % (key, info["name"][:34], size[0], size[1],
                                                   "masked" if masked else "      ", rough))

    (OUT / "placeholders.json").write_text(json.dumps({"textures": manifest}, indent=1))
    print("wrote %d placeholder texture sets to %s" % (len(manifest), OUT.relative_to(ROOT)))


if __name__ == "__main__":
    main()
