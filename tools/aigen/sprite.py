"""
Sprite steps of the aigen pipeline: extract a .bgf frame, its world size, and a 4x upscale that keeps
the cut-out (the texture upscaler drops alpha).

The upscale uses the environment's base-colour model (4xTextures_GTAV_rgt-s_dither, docs/adr/0003)
through tools/textures/upscale.py, so it needs build/texai/.venv (tools/textures/setup_ai.ps1).
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bgf2png"))
sys.path.insert(0, str(ROOT / "tools" / "textures"))
import bgf2png  # noqa: E402

BGF_OUT = ROOT / "build" / "bgf"
MODELS_DIR = ROOT / "build" / "texai" / "models"
UPSCALE_MODEL = "4xTextures_GTAV_rgt-s_dither"
PAD = 8          # transparent border around the sprite before upscaling, so edges get context
SCALE = 4


def extract(bgf: str) -> Path:
    """build/bgf/<bgf>/ (frame_NN.png + meta.json), exported from the client's .bgf if missing."""
    out = BGF_OUT / bgf.lower()
    if not (out / "meta.json").exists():
        f = bgf2png.find_file(bgf2png.CLIENT_RES, bgf)
        if not f:
            raise SystemExit("%s.bgf not found in the client resources or Server-104/resource" % bgf)
        bgf2png.export_sprite(bgf2png.BGF(f), out, bgf2png.load_palette())
    return out


def frame(bgf: str, index: int):
    """(RGBA image, meta) of one bitmap."""
    d = extract(bgf)
    return Image.open(d / ("frame_%02d.png" % index)).convert("RGBA"), json.loads((d / "meta.json").read_text())


def fill_transparent(img: Image.Image) -> Image.Image:
    """RGB with every transparent pixel set to the mean of its nearest opaque neighbours, grown ring
    by ring (like make_placeholders.fill_cutout, but without wrapping: sprites don't tile). Stops the
    palette's cyan key bleeding into the upscaled edge."""
    a = np.asarray(img.convert("RGBA"), dtype=np.float32)
    rgb, known = a[..., :3].copy(), a[..., 3] >= 128
    if not known.any():
        return img.convert("RGB")
    rgb[~known] = 0
    h, w = known.shape
    while not known.all():
        k = np.pad(known.astype(np.float32), 1)
        c = np.pad(rgb, ((1, 1), (1, 1), (0, 0))) * k[..., None]
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, np.float32)
        for dy in (0, 1, 2):
            for dx in (0, 1, 2):
                if dx != 1 or dy != 1:
                    acc += c[dy:dy + h, dx:dx + w]
                    cnt += k[dy:dy + h, dx:dx + w]
        ring = (~known) & (cnt > 0)
        if not ring.any():
            break
        rgb[ring] = acc[ring] / cnt[ring][:, None]
        known = known | ring
    return Image.fromarray(np.clip(rgb + 0.5, 0, 255).astype(np.uint8))


def upscale_rgba(img: Image.Image, model_name: str = UPSCALE_MODEL) -> Image.Image:
    """4x upscale of an RGBA sprite: colour by the model, alpha from the 1-bit original, smoothed at
    the new size (as the fire flipbooks in make_placeholders.py)."""
    import spandrel
    import torch
    import upscale as up

    padded = Image.new("RGBA", (img.width + 2 * PAD, img.height + 2 * PAD), (0, 0, 0, 0))
    padded.paste(img, (PAD, PAD))
    weights = MODELS_DIR / (model_name + ".safetensors")
    if not weights.exists():
        raise SystemExit("%s missing: run tools/textures/setup_ai.ps1" % weights)
    model = spandrel.ModelLoader().load_from_file(str(weights)).cuda().eval()
    half = model.supports_half
    if half:
        model.model.half()
    rgb = up.upscale(model, fill_transparent(padded), half)
    del model
    torch.cuda.empty_cache()
    big = rgb.size
    alpha = padded.getchannel("A").point(lambda v: 255 if v >= 128 else 0).resize(big, Image.Resampling.BILINEAR)
    alpha = alpha.filter(ImageFilter.GaussianBlur(SCALE * 0.3)).point(lambda v: max(0, min(255, (v - 64) * 2)))
    out = Image.merge("RGBA", (*rgb.split(), alpha))
    p = PAD * SCALE
    return out.crop((p, p, out.width - p, out.height - p))


def on_canvas(img: Image.Image, size: int = 1024, fill: float = 0.86, bg=(200, 200, 200)) -> Image.Image:
    """The sprite centred on a square canvas, scaled so its longer side is `fill` of the canvas.
    bg None keeps it transparent (RGBA), else a flat colour (RGB)."""
    s = fill * size / max(img.size)
    im = img.resize((max(1, round(img.width * s)), max(1, round(img.height * s))), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (*(bg or (0, 0, 0)), 0 if bg is None else 255))
    canvas.alpha_composite(im, ((size - im.width) // 2, (size - im.height) // 2))
    return canvas if bg is None else canvas.convert("RGB")
