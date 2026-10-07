"""
Stitch the smoothing variants of a sprite clip (run_sprite_clips.ps1) side by side into one GIF at
30 fps: build/sprites/clips/<clip>.gif. Each variant's frames are cropped around the character
(a fixed box: the chase camera keeps the character in place).

    python tools/sprites/clip_gif.py walk original tweens tweens_motion crossfade
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
CLIPS = ROOT / "build" / "sprites" / "clips"


def main():
    clip, variants = sys.argv[1], sys.argv[2:]
    seqs = {v: sorted((CLIPS / f"{clip}_{v}").glob("frame_*.png")) for v in variants}
    seqs = {v: s for v, s in seqs.items() if s}
    if not seqs:
        sys.exit("no frames")
    # the chase camera keeps the character in the same place on a 1280x720 frame
    box = (360, 320, 720, 720)
    cw, ch = box[2] - box[0], box[3] - box[1]
    n = min(len(s) for s in seqs.values())
    frames = []
    for i in range(n):
        sheet = Image.new("RGB", (cw * len(seqs), ch + 22), (30, 32, 38))
        d = ImageDraw.Draw(sheet)
        for j, (v, s) in enumerate(seqs.items()):
            sheet.paste(Image.open(s[i]).convert("RGB").crop(box), (j * cw, 22))
            d.text((j * cw + 6, 5), v, fill=(230, 230, 230))
        frames.append(sheet.resize((sheet.width // 2 * 2 // 2, (ch + 22) // 2)) if cw * len(seqs) > 1600 else sheet)
    out = CLIPS / f"{clip}.gif"
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=33, loop=0)
    print(out, f"{n} frames, {len(seqs)} variants")


if __name__ == "__main__":
    main()
