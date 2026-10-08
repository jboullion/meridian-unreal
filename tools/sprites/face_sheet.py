"""
Character creator faces under each upscale method (data/sprites/upscale.json chooses per part):

    build/texai/.venv/Scripts/python tools/sprites/face_sheet.py [--methods nearest scale4x gtav ...]

Faces are drawn the way the original creator drew its preview (module/char/charface.c): the head
as the object, then mouth, eyes, nose and hair on hotspots 12, 11, 14 and 13, seen from the front
(angle 0), each part recoloured with its own palette translation (skin for the face, hair colour
for the hair). The upscaled parts are recoloured per original pixel's ramp (luts.translate), as the
runtime does since 2026-10-07.

Output (build/sprites/faces/):
    compare.png          rows = the original (nearest) and every method; columns = sample faces,
                         at the creator's size, then the in-world size (about 1.15 render-target
                         texels per face pixel) blown up 3x
    sheet_<method>.png   every option per gender: hair, eyes, nose, mouth, skin and hair colours
    zoom_<method>.png    the eyes and mouth of one face, 3x: edges and grain
    zooms.png            every method's zoom side by side
"""
from __future__ import annotations

import argparse
import json

import numpy as np
from PIL import Image, ImageDraw

import luts
import m59sprites as ms
import upscale_parts as up

OUT = ms.ROOT / "build" / "sprites" / "faces"
BG = (40, 40, 40, 255)
FACE_SCALE = 8          # head pixels -> sheet pixels: 4 per face pixel (shrink 14 vs the head's 7), as the atlas
WORLD_TEXELS = 2.3      # head pixels -> render target texels in the world (mr.Sprite.TexelsPerCm 4.65)
HOTSPOTS = {"mouth": 12, "eyes": 11, "nose": 14, "hair": 13}
_ramps: dict = {}


def charinfo() -> dict:
    return json.loads((ms.ROOT / "data" / "charinfo.json").read_text(encoding="utf-8"))


def source(method: str | None):
    """composite() source: the bitmap upscaled by `method`, recoloured per original pixel's ramp;
    None = the original pixels (nearest, translated exactly by palette index)."""
    if method is None:
        return None

    def get(name: str, index: int, xid: int):
        im = up.get(name, index, 0, method=method)
        key = (name, index, method)
        if key not in _ramps:
            b = ms.load_bgf(name).bitmaps[index]
            ids = luts.ramp_ids_from_indices(b.pixels, b.w, b.h)
            _ramps[key] = luts.ids_at(luts.ramp_cell(im, ids, up.SCALE), im.size)
        return luts.translate(im, _ramps[key], xid)
    return get


def face(parts: dict, skin: int, hair_xlat: int, method: str | None, scale: float = FACE_SCALE) -> Image.Image:
    body = ms.Layer(parts["head"], 0, skin, 0)
    ovs = [ms.Layer(parts[k], HOTSPOTS[k], hair_xlat if k == "hair" else skin, 0)
           for k in ("mouth", "eyes", "nose", "hair") if parts.get(k) and parts[k] != "blank"]
    # a fixed canvas around the head so every face lines up (hair reaches above and around it)
    tb = ms.load_bgf(parts["head"]).bitmaps[0]
    canvas = (-int(tb.w * 0.6), -int(tb.h * 0.5), int(tb.w * 1.6), int(tb.h * 1.15))
    im = ms.composite(body, ovs, 0, scale=FACE_SCALE, canvas=canvas, source=source(method)).image
    if scale < FACE_SCALE:   # smaller (the in-world size): drawn at the atlas's detail, then scaled down
        w, h = im.size
        f = scale / FACE_SCALE
        im = im.resize((max(1, round(w * f)), max(1, round(h * f))), Image.LANCZOS)
    out = Image.new("RGBA", im.size, BG)
    out.alpha_composite(im)
    return out


def label(im: Image.Image, text: str) -> Image.Image:
    out = Image.new("RGBA", (im.width, im.height + 14), BG)
    out.alpha_composite(im, (0, 14))
    ImageDraw.Draw(out).text((3, 1), text, fill=(230, 220, 160, 255))
    return out


def grid(rows: list[list[Image.Image]], gap: int = 4) -> Image.Image:
    w = max(sum(im.width for im in r) + gap * (len(r) + 1) for r in rows)
    h = sum(max(im.height for im in r) for r in rows) + gap * (len(rows) + 1)
    out = Image.new("RGBA", (w, h), (24, 24, 24, 255))
    y = gap
    for r in rows:
        x = gap
        for im in r:
            out.alpha_composite(im, (x, y))
            x += im.width + gap
        y += max(im.height for im in r) + gap
    return out


def defaults(faces: dict, gender: str) -> dict:
    f = faces[gender]
    return {k: v[0] for k, v in f.items()}


def samples(info: dict) -> list[tuple[str, dict, int, int]]:
    """A handful of faces across both genders, skins and hair colours."""
    f, hx, sx = info["faces"], info["hair_xlats"], info["skin_xlats"]
    m, w = f["male"], f["female"]
    return [("male 1", {"head": m["head"][0], "hair": m["hair"][0], "eyes": m["eyes"][0], "nose": m["nose"][0],
                        "mouth": m["mouth"][0]}, sx[2], hx[13]),
            ("male 2", {"head": m["head"][0], "hair": m["hair"][3], "eyes": m["eyes"][2], "nose": m["nose"][2],
                        "mouth": m["mouth"][1]}, sx[0], hx[12]),
            ("male 3", {"head": m["head"][0], "hair": m["hair"][4], "eyes": m["eyes"][3], "nose": m["nose"][1],
                        "mouth": m["mouth"][2]}, sx[3], hx[10]),
            ("female 1", {"head": w["head"][0], "hair": w["hair"][1], "eyes": w["eyes"][0], "nose": w["nose"][0],
                          "mouth": w["mouth"][0]}, sx[1], hx[2]),
            ("female 2", {"head": w["head"][0], "hair": w["hair"][6], "eyes": w["eyes"][1], "nose": w["nose"][1],
                          "mouth": w["mouth"][1]}, sx[2], hx[8]),
            ("female 3", {"head": w["head"][0], "hair": w["hair"][7], "eyes": w["eyes"][2], "nose": w["nose"][2],
                          "mouth": w["mouth"][2]}, sx[3], hx[12])]


def compare(info: dict, methods: list[str]):
    rows = []
    for m in [None] + methods:
        name = m or "original"
        row = []
        for title, parts, skin, hair in samples(info):
            big = face(parts, skin, hair, m)
            world = face(parts, skin, hair, m, WORLD_TEXELS)
            world = world.resize((world.width * 3, world.height * 3), Image.NEAREST)
            row += [label(big, f"{name}: {title}"), label(world, "in world, 3x")]
        rows.append(row)
    out = grid(rows)
    out.save(OUT / "compare.png")
    print("wrote", OUT / "compare.png", out.size)


def sheet(info: dict, method: str | None):
    f, hx, sx = info["faces"], info["hair_xlats"], info["skin_xlats"]
    rows = []
    for g in ("male", "female"):
        d = defaults(f, g)
        for part in ("hair", "eyes", "nose", "mouth"):
            rows.append([label(face({**d, part: v}, sx[2], hx[13], method, 4), f"{g} {part} {i + 1}: {v}")
                         for i, v in enumerate(f[g][part])])
        rows.append([label(face(d, s, hx[13], method, 4), f"{g} skin {i + 1}") for i, s in enumerate(sx)])
        rows.append([label(face(d, sx[2], h, method, 4), f"hair colour {i + 1}") for i, h in enumerate(hx)])
    out = grid(rows)
    name = method or "original"
    out.save(OUT / f"sheet_{name}.png")
    print("wrote", OUT / f"sheet_{name}.png", out.size)


def zoom(info: dict, method: str | None):
    _, parts, skin, hair = samples(info)[0]
    im = face(parts, skin, hair, method)
    w, h = im.size
    crop = im.crop((int(w * 0.28), int(h * 0.36), int(w * 0.72), int(h * 0.7)))
    crop = crop.resize((crop.width * 3, crop.height * 3), Image.NEAREST)
    name = method or "original"
    label(crop, f"{name}: eyes, nose, mouth 3x").save(OUT / f"zoom_{name}.png")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--methods", nargs="*", default=up.METHODS)
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    info = charinfo()
    compare(info, a.methods)
    for m in [None] + a.methods:
        sheet(info, m)
        zoom(info, m)
    zooms = [Image.open(OUT / f"zoom_{m or 'original'}.png") for m in [None] + a.methods]
    zooms = [z.resize((z.width // 2, z.height // 2), Image.LANCZOS) for z in zooms]
    grid([zooms[i:i + 4] for i in range(0, len(zooms), 4)]).save(OUT / "zooms.png")
    print("wrote", OUT / "zooms.png")


if __name__ == "__main__":
    main()
