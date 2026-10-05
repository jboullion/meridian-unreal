"""
AI height and normal maps for the placeholder textures (docs/adr/0003, "AI relief maps").

The pipeline step (run by make_placeholders.py --relief auto, or on its own):

    build/texai/.venv/Scripts/python tools/textures/ai_maps.py --apply [--force]

replaces every T_<grd>_H.png / T_<grd>_N.png in build/textures_placeholder/ with Marigold's, made
from that texture's base colour (the Real-ESRGAN upscale). Incremental: a texture is redone only
when its base colour, this code or the settings changed, or its maps no longer are the ones written
here (make_placeholders.py rewrote them); keys in build/textures_placeholder/relief.json.

The rest of this file is the comparison test that chose Marigold:
make_placeholders.py derives height from brightness (with a few rule-based modes); this asks
learned models for the surface shape instead, from the same upscaled base colour:

  deepbump   DeepBump (github.com/HugoTini/DeepBump, ONNX, CPU): colour -> normals, then
             Frankot-Chellappa integration -> height. Trained on material textures.
  marigold   Marigold normals v1.1 (prs-eth, diffusion, GPU): colour -> normals, same integration.
  dav2       Depth Anything V2 Small (GPU): colour -> relative depth, used as height; normals
             from the height.
  combo      dav2 for the large shapes (windows back, beams and pilasters forward) plus the
             make_placeholders.py maps for the fine detail (stones, mortar), which dav2 lacks
             (run after dav2).

Every height map is high-passed (the models add broad tilts and bowls a flat wall shouldn't have)
and stretched to 0..1 between its 1st and 99th percentiles; normals are written like
make_placeholders.py's (DirectX: green down). Textures tile, so the input is wrap-padded and the
result cropped back.

Runs in its own environment (torch, diffusers, onnxruntime; see README "AI relief maps"):

    build/texai/.venv/Scripts/python tools/textures/ai_maps.py [--methods deepbump,marigold,dav2] [grd...]

Output: build/texai/out/<method>/T_<grd>_H.png, T_<grd>_N.png (same names and sizes as
build/textures_placeholder/), default set = the textures data/environment/materials.json displaces.

The models normally see make_placeholders.py's upscaled base colour. To try a sharper input:

    ... ai_maps.py --esrgan realesrgan-x4plus            # originals -> build/texai/in/<model>/ (Real-ESRGAN)
    ... ai_maps.py --methods marigold --input realesrgan-x4plus   # -> out/marigold_realesrgan-x4plus/

(only the models' input changes; the game keeps its base colour).
"""
import argparse
import hashlib
import inspect
import json
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image
from scipy import ndimage

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLACEHOLDERS = os.path.join(REPO, "build", "textures_placeholder")
OUT = os.path.join(REPO, "build", "texai", "out")
DEEPBUMP = os.path.join(REPO, "build", "texai", "DeepBump")
INPUTS = os.path.join(REPO, "build", "texai", "in")
ORIGINALS = os.path.join(REPO, "build", "textures")
ESRGAN = os.path.join(REPO, "build", "texai", "realesrgan", "realesrgan-ncnn-vulkan.exe")
MODEL_SIDE = 1024   # longest side the models see
PAD = 0.125         # wrap padding, fraction of each side
HIGHPASS = 1 / 10.0  # gaussian sigma for the high-pass, fraction of the longest side


def esrgan_inputs(model, grds, pad=8):
    """Real-ESRGAN 4x of each original (wrap-padded so tiling edges stay seamless; cut-out key colour
    filled first), resized to the game's base colour size -> build/texai/in/<model>/T_<grd>_D.png."""
    sys.path.insert(0, os.path.join(REPO, "tools", "textures"))
    from make_placeholders import fill_cutout
    out = os.path.join(INPUTS, model)
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for grd in grds:
            orig = Image.open(os.path.join(ORIGINALS, grd + ".png")).convert("RGBA")
            rgb = np.asarray(fill_cutout(orig), dtype=np.uint8)
            padded = np.pad(rgb, ((pad, pad), (pad, pad), (0, 0)), mode="wrap")
            src, dst = os.path.join(tmp, "in.png"), os.path.join(tmp, "out.png")
            Image.fromarray(padded).save(src)
            subprocess.run([ESRGAN, "-i", src, "-o", dst, "-n", model, "-s", "4"], check=True, capture_output=True)
            up = Image.open(dst).convert("RGB")
            up = up.crop((pad * 4, pad * 4, up.width - pad * 4, up.height - pad * 4))
            ph = Image.open(os.path.join(PLACEHOLDERS, "T_%s_D.png" % grd))
            up = up.resize(ph.size, Image.Resampling.LANCZOS)
            if ph.mode == "RGBA":
                up = Image.merge("RGBA", (*up.split(), ph.getchannel("A")))  # cut-outs keep their hard alpha
            up.save(os.path.join(out, "T_%s_D.png" % grd))
    print("%s: %d inputs -> %s" % (model, len(grds), out))


def displaced_textures():
    m = json.load(open(os.path.join(REPO, "data", "environment", "materials.json"), encoding="utf-8"))
    return sorted(k for k in m.get("displacement", {}) if not k.startswith("_"))


def load_rgb(grd, source=None):
    """The base colour the models see (make_placeholders.py's, or build/texai/in/<source>/), and the
    size the maps are written at (always the game's base colour size)."""
    full = Image.open(os.path.join(PLACEHOLDERS, "T_%s_D.png" % grd)).size
    im = Image.open(os.path.join(INPUTS, source, "T_%s_D.png" % grd) if source else
                    os.path.join(PLACEHOLDERS, "T_%s_D.png" % grd)).convert("RGB")
    if im.size != full:
        im = im.resize(full, Image.Resampling.LANCZOS)
    k = MODEL_SIDE / max(full)
    if k < 1:
        im = im.resize((round(full[0] * k), round(full[1] * k)), Image.Resampling.LANCZOS)
    return np.asarray(im, dtype=np.float32) / 255.0, full


def wrap_pad(a):
    py, px = round(a.shape[0] * PAD), round(a.shape[1] * PAD)
    pads = ((py, py), (px, px)) + ((0, 0),) * (a.ndim - 2)
    return np.pad(a, pads, mode="wrap"), (py, px)


def crop(a, p):
    py, px = p
    return a[py:a.shape[0] - py, px:a.shape[1] - px]


def finish_height(h):
    """High-pass (wrapping) and stretch to 0..1."""
    sigma = HIGHPASS * max(h.shape)
    h = h - ndimage.gaussian_filter(h, sigma, mode="wrap")
    lo, hi = np.percentile(h, 1), np.percentile(h, 99)
    return np.clip((h - lo) / max(hi - lo, 1e-6), 0, 1)


def normals_from_height(h, strength=6.0):
    """Tangent-space normals (OpenGL, y up) from a 0..1 height, rows top to bottom."""
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength
    dy = (np.roll(h, 1, 0) - np.roll(h, -1, 0)) * 0.5 * strength  # +y = up the image
    n = np.stack([-dx, -dy, np.ones_like(h)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def height_from_normals(n_gl):
    """Frankot-Chellappa (DeepBump's implementation) on OpenGL normals in -1..1, (H, W, 3)."""
    sys.path.insert(0, DEEPBUMP)  # its modules, not the package (that is the Blender add-on)
    import module_normals_to_height as n2h
    chw = np.transpose(n_gl * 0.5 + 0.5, (2, 0, 1)).astype(np.float32)
    h = n2h.apply(chw, True, None)
    return np.asarray(h).reshape(n_gl.shape[:2]) if np.asarray(h).ndim == 2 else np.asarray(h)[0]


# --------------------------------------------------------------------------------- the models

def deepbump_normals(rgb):
    sys.path.insert(0, DEEPBUMP)
    import module_color_to_normals as c2n
    chw = np.transpose(rgb, (2, 0, 1)).astype(np.float32)
    n = c2n.apply(chw, "MEDIUM", None)  # (3, H, W) in 0..1, OpenGL
    return np.transpose(np.asarray(n), (1, 2, 0)) * 2 - 1


_marigold = None


def marigold_normals(rgb):
    global _marigold
    import torch
    from diffusers import MarigoldNormalsPipeline
    if _marigold is None:
        _marigold = MarigoldNormalsPipeline.from_pretrained(
            "prs-eth/marigold-normals-v1-1", variant="fp16", torch_dtype=torch.float16).to("cuda")
        _marigold.set_progress_bar_config(disable=True)
    img = Image.fromarray((rgb * 255).astype(np.uint8))
    out = _marigold(img, num_inference_steps=4, ensemble_size=1, processing_resolution=768,
                    match_input_resolution=True, generator=torch.Generator("cuda").manual_seed(0))
    n = np.asarray(out.prediction)[0]  # (H, W, 3) in -1..1: x right, y up, z towards the camera
    return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)


_dav2 = None


def dav2_height(rgb):
    global _dav2
    import torch
    from transformers import pipeline
    if _dav2 is None:
        _dav2 = pipeline("depth-estimation", model="depth-anything/Depth-Anything-V2-Small-hf", device=0)
    img = Image.fromarray((rgb * 255).astype(np.uint8))
    d = _dav2(img)["predicted_depth"]  # relative inverse depth: larger = nearer = higher
    d = torch.nn.functional.interpolate(d[None] if d.ndim == 3 else d[None, None], size=rgb.shape[:2],
                                        mode="bicubic", align_corners=False)
    return d.squeeze().float().cpu().numpy()


COMBO_MACRO, COMBO_MICRO = 0.6, 0.4   # height weights
COMBO_NORMALS = 0.8                    # dav2 slopes added to the current normals


def combo(grd):
    """-> (height, OpenGL normals) at full size from dav2's output and the current placeholders."""
    def gray(path):
        return np.asarray(Image.open(path).convert("L"), dtype=np.float32) / 255.0

    def normals(path):
        n = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0 * 2 - 1
        n[..., 1] = -n[..., 1]  # DirectX -> OpenGL
        return n

    cur_h = gray(os.path.join(PLACEHOLDERS, "T_%s_H.png" % grd))
    macro = gray(os.path.join(OUT, "dav2", "T_%s_H.png" % grd))
    micro = cur_h - ndimage.gaussian_filter(cur_h, max(cur_h.shape) / 40.0, mode="wrap")
    micro = micro / max(np.percentile(np.abs(micro), 99), 1e-6) * 0.5
    h = finish_height(COMBO_MACRO * (macro - 0.5) + COMBO_MICRO * micro)
    n_cur = normals(os.path.join(PLACEHOLDERS, "T_%s_N.png" % grd))
    n_mac = normals(os.path.join(OUT, "dav2", "T_%s_N.png" % grd))
    n = n_cur.copy()
    n[..., :2] += COMBO_NORMALS * n_mac[..., :2]
    return h, n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)


def maps(method, rgb):
    """-> (height 0..1, OpenGL normals -1..1) at the model resolution, for an unpadded rgb."""
    padded, p = wrap_pad(rgb)
    if method == "dav2":
        h = finish_height(crop(dav2_height(padded), p))
        return h, normals_from_height(h)
    n = deepbump_normals(padded) if method == "deepbump" else marigold_normals(padded)
    n = crop(n, p)
    return finish_height(height_from_normals(n)), n


def save(method, grd, h, n_gl, full, folder=None):
    d = folder or os.path.join(OUT, method)
    os.makedirs(d, exist_ok=True)
    Image.fromarray((h * 255).round().astype(np.uint8), "L").resize(full, Image.Resampling.BICUBIC) \
        .save(os.path.join(d, "T_%s_H.png" % grd))
    rgb = n_gl * 0.5 + 0.5
    rgb[..., 1] = 1.0 - rgb[..., 1]  # DirectX, like make_placeholders.py
    Image.fromarray((np.clip(rgb, 0, 1) * 255).round().astype(np.uint8), "RGB").resize(full, Image.Resampling.BICUBIC) \
        .save(os.path.join(d, "T_%s_N.png" % grd))


def lit(base, normal_dx):
    """Base colour lit from the top left by a DirectX normal map (both PIL RGB, same size)."""
    b = np.asarray(base, dtype=np.float32) / 255.0
    n = np.asarray(normal_dx, dtype=np.float32) / 255.0 * 2 - 1
    n[..., 1] = -n[..., 1]
    light = np.array([-0.5, 0.5, 0.7]) / np.linalg.norm([-0.5, 0.5, 0.7])
    shade = np.clip((n * light).sum(-1) / np.maximum(np.linalg.norm(n, axis=-1), 1e-6), 0, 1)
    return Image.fromarray((np.clip(b * (0.25 + 0.95 * shade[..., None]), 0, 1) * 255).astype(np.uint8))


def sheet(grds, methods, side=320):
    """build/texai/out/sheet.png: per texture, row 1 = colour + each method's height, row 2 =
    the normals lit from the top left (first column: current make_placeholders maps)."""
    from PIL import ImageDraw
    cols = ["current"] + methods
    rows = []
    for grd in grds:
        d = Image.open(os.path.join(PLACEHOLDERS, "T_%s_D.png" % grd)).convert("RGB")
        k = side / max(d.size)
        size = (round(d.width * k), round(d.height * k))
        d = d.resize(size, Image.Resampling.LANCZOS)
        hrow, nrow = [d], [d]
        for c in cols:
            folder = PLACEHOLDERS if c == "current" else os.path.join(OUT, c)
            h = Image.open(os.path.join(folder, "T_%s_H.png" % grd)).convert("RGB").resize(size)
            n = Image.open(os.path.join(folder, "T_%s_N.png" % grd)).convert("RGB").resize(size)
            hrow.append(h)
            nrow.append(lit(d, n))
        rows += [(grd + " height", hrow), (grd + " lit normals", nrow)]
    w = (len(cols) + 1) * (side + 8)
    hs = [max(im.height for im in r) + 22 for _, r in rows]
    out = Image.new("RGB", (w, sum(hs)), (24, 24, 24))
    dr = ImageDraw.Draw(out)
    y = 0
    for (label, r), hh in zip(rows, hs):
        for i, im in enumerate(r):
            out.paste(im, (i * (side + 8), y + 20))
            dr.text((i * (side + 8) + 4, y + 4), label if i == 0 else cols[i - 1], fill=(255, 255, 255))
        y += hh
    path = os.path.join(OUT, "sheet.png")
    out.save(path)
    print(path)


APPLY_METHOD = "marigold"
RELIEF_CACHE = os.path.join(PLACEHOLDERS, "relief.json")


def _digest(path):
    return hashlib.sha1(open(path, "rb").read()).hexdigest()


def apply_relief(force=False):
    """Marigold height + normal maps for every placeholder texture, in place (see the docstring)."""
    textures = json.load(open(os.path.join(PLACEHOLDERS, "placeholders.json")))["textures"]
    cache = {} if force or not os.path.exists(RELIEF_CACHE) else json.load(open(RELIEF_CACHE))
    code = hashlib.sha1("".join(inspect.getsource(f) for f in (
        maps, marigold_normals, height_from_normals, finish_height, wrap_pad, crop, load_rgb, save, apply_relief)
    ).encode()).hexdigest()
    started, done = time.time(), 0
    out = {}
    for grd in sorted(textures):
        d_path = os.path.join(PLACEHOLDERS, "T_%s_D.png" % grd)
        h_path, n_path = (os.path.join(PLACEHOLDERS, "T_%s_%s.png" % (grd, k)) for k in ("H", "N"))
        key = hashlib.sha1(json.dumps([_digest(d_path), APPLY_METHOD, code, MODEL_SIDE, PAD, HIGHPASS]).encode()).hexdigest()
        old = cache.get(grd, {})
        if (old.get("key") == key and os.path.exists(h_path) and os.path.exists(n_path)
                and old.get("h") == _digest(h_path) and old.get("n") == _digest(n_path)):
            out[grd] = old
            continue
        rgb, full = load_rgb(grd)
        h, n = maps(APPLY_METHOD, rgb)
        save(APPLY_METHOD, grd, h, n, full, folder=PLACEHOLDERS)
        d_img = Image.open(d_path)
        if d_img.mode == "RGBA":
            # cut-outs: flat normals in the holes, like make_placeholders.py
            hole = np.asarray(d_img.getchannel("A")) < 128
            nm = np.asarray(Image.open(n_path).convert("RGB")).copy()
            nm[hole] = (128, 128, 255)
            Image.fromarray(nm).save(n_path)
        out[grd] = {"key": key, "h": _digest(h_path), "n": _digest(n_path)}
        done += 1
        if done % 25 == 0:
            json.dump(out | {k: v for k, v in cache.items() if k not in out}, open(RELIEF_CACHE, "w"), indent=0)
    json.dump(out, open(RELIEF_CACHE, "w"), indent=0, sort_keys=True)
    print("relief: Marigold maps for %d textures: %d made, %d unchanged (%.0f s)"
          % (len(textures), done, len(textures) - done, time.time() - started))


def main():
    sys.stdout.reconfigure(encoding="utf-8")  # DeepBump prints arrows
    ap = argparse.ArgumentParser()
    ap.add_argument("textures", nargs="*")
    ap.add_argument("--methods", default="deepbump,marigold,dav2")
    ap.add_argument("--sheet", action="store_true", help="only write the comparison sheet")
    ap.add_argument("--esrgan", help="only make Real-ESRGAN inputs with this model")
    ap.add_argument("--input", help="models see build/texai/in/<input>/ instead (output: <method>_<input>)")
    ap.add_argument("--all", action="store_true", help="every placeholder texture, not just the displaced ones")
    ap.add_argument("--apply", action="store_true", help="the pipeline step: Marigold maps into build/textures_placeholder")
    ap.add_argument("--force", action="store_true", help="with --apply: redo every texture")
    a = ap.parse_args()
    if a.apply:
        os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")
        apply_relief(a.force)
        return
    grds = a.textures or (sorted(json.load(open(os.path.join(PLACEHOLDERS, "placeholders.json")))["textures"])
                          if a.all else displaced_textures())
    if a.esrgan:
        esrgan_inputs(a.esrgan, grds)
        return
    if a.sheet:
        sheet(grds, a.methods.split(","))
        return
    for method in a.methods.split(","):
        started = time.time()
        name = method + ("_" + a.input if a.input else "")
        for grd in grds:
            rgb, full = load_rgb(grd, a.input)
            h, n = combo(grd) if method == "combo" else maps(method, rgb)
            save(name, grd, h, n, full)
        print("%s: %d textures in %.0f s -> %s" % (name, len(grds), time.time() - started, os.path.join(OUT, name)))


if __name__ == "__main__":
    main()
