"""
4x upscale every PNG in a folder with a PyTorch super-resolution model loaded by spandrel (ESRGAN,
DAT, RGT, HAT, ... from https://openmodeldb.info, .safetensors in build/texai/models/). Run by
make_placeholders.py for the models in its ESRGAN_RULES that aren't Real-ESRGAN's own:

    build/texai/.venv/Scripts/python tools/textures/upscale.py --model build/texai/models/<name>.safetensors IN_DIR OUT_DIR

The image is processed in overlapping tiles (transformer models run out of memory on whole
textures on an 8 GB GPU); the caller wrap-pads tiling textures, as for Real-ESRGAN.
"""
import argparse
import os
import sys

import numpy as np
import spandrel
import torch
from PIL import Image

TILE = 128    # input pixels per tile side
OVERLAP = 16  # context on each side of a tile, cropped from its output


def upscale(model, img, half):
    src = np.asarray(img.convert("RGB"), dtype=np.float32) / 255.0
    h, w = src.shape[:2]
    s = model.scale
    out = np.zeros((h * s, w * s, 3), np.float32)
    for y0 in range(0, h, TILE):
        for x0 in range(0, w, TILE):
            y1, x1 = min(y0 + TILE, h), min(x0 + TILE, w)
            cy0, cx0, cy1, cx1 = max(0, y0 - OVERLAP), max(0, x0 - OVERLAP), min(h, y1 + OVERLAP), min(w, x1 + OVERLAP)
            x = torch.from_numpy(np.ascontiguousarray(src[cy0:cy1, cx0:cx1])).permute(2, 0, 1)[None].cuda()
            with torch.inference_mode():
                y = model(x.half() if half else x)[0].float().clamp(0, 1).permute(1, 2, 0).cpu().numpy()
            oy, ox = (y0 - cy0) * s, (x0 - cx0) * s
            out[y0 * s:y1 * s, x0 * s:x1 * s] = y[oy:oy + (y1 - y0) * s, ox:ox + (x1 - x0) * s]
    return Image.fromarray((out * 255.0 + 0.5).astype(np.uint8))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("inputs")
    ap.add_argument("outputs")
    a = ap.parse_args()
    model = spandrel.ModelLoader().load_from_file(a.model).cuda().eval()
    if model.scale != 4:
        sys.exit("%s is a %dx model; make_placeholders.py expects 4x" % (a.model, model.scale))
    half = model.supports_half
    if half:
        model.model.half()
    os.makedirs(a.outputs, exist_ok=True)
    for name in sorted(os.listdir(a.inputs)):
        if name.lower().endswith(".png"):
            upscale(model, Image.open(os.path.join(a.inputs, name)), half).save(os.path.join(a.outputs, name))
            torch.cuda.empty_cache()


if __name__ == "__main__":
    main()
