"""The original client's skyboxes as textures for the sky dome (M_NightSky; docs/performance.md, docs/adr/0005).

The Direct3D client draws one of five skyboxes behind the world (resource/sky{a,b,c,d}.bsf, redsky.bsf): each
.bsf is six PNG faces one after another, in the order back, bottom, front, left, right, top (clientd3d's skybox
tables). The room's background picks the box (room.kod: dawn skyc, day skya, dusk skyb, night skyd, the same
in storms; redsky for Chaos nights). Each box is written as one atlas, faces in a 3 x 2 grid in that order
(face i at column i % 3, row i // 3), to build/textures_placeholder/T_Skybox_<name>.png. M_NightSky finds the
face and its texel for a view direction (SKYBOX_HLSL in tools/ue/environment_materials.py).

    python tools/textures/make_skyboxes.py
"""
import io
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bgf2png"))
from bgf2png import CLIENT_RES  # noqa: E402  (where the original client's resource/ folder is)

OUT = ROOT / "build" / "textures_placeholder"
SKIES = ["skya", "skyb", "skyc", "skyd", "redsky"]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def find_bsf(name):
    for folder in CLIENT_RES:
        path = folder / (name + ".bsf")
        if path.exists():
            return path
    return None


def faces(data):
    """The PNG images in a .bsf, in file order."""
    out, pos = [], 0
    while True:
        start = data.find(PNG_SIGNATURE, pos)
        if start < 0:
            return out
        end = data.find(b"IEND", start) + 8  # the chunk type, then its CRC
        out.append(data[start:end])
        pos = end


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name in SKIES:
        path = find_bsf(name)
        if not path:
            print("no %s.bsf in %s" % (name, ", ".join(str(p) for p in CLIENT_RES)))
            continue
        images = [Image.open(io.BytesIO(png)).convert("RGB") for png in faces(path.read_bytes())]
        if len(images) != 6:
            print("%s: %d faces, expected 6" % (path, len(images)))
            continue
        size = images[0].size[0]
        atlas = Image.new("RGB", (size * 3, size * 2))
        for i, face in enumerate(images):
            atlas.paste(face.resize((size, size)), ((i % 3) * size, (i // 3) * size))
        target = OUT / ("T_Skybox_%s.png" % name)
        atlas.save(target)
        print("%s -> %s (%dx%d)" % (path.name, target.relative_to(ROOT), atlas.width, atlas.height))


if __name__ == "__main__":
    main()
