"""
Inventory, spell and skill icons for the in-game UI (docs/adr/0009-user-interface.md), decoded
from the original client's .bgf files (git-ignored output):

    python tools/ui/build_icons.py                    # every variant in data/ui/ui_style.json "art_variants"
    python tools/ui/build_icons.py --variant nearest

Every vrIcon in data/items.json, spells.json and skills.json becomes
build/ui/icons/<variant>/T_Icon_<bgf>.png: square, 4x the original pixels, the sprite centred.
An item is drawn with its inventory group (viInventory_group, 1-based like Kod's groups: the
first bitmap of that group), a spell or skill with its first bitmap. build/ui/icons/icons.json
lists each icon's source and original size.
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bgf2png"))
sys.path.insert(0, str(ROOT / "tools" / "ui"))
import bgf2png  # noqa: E402
import build_ui_art as art  # noqa: E402

DATA = ROOT / "data"
OUT = ROOT / "build" / "ui" / "icons"
SCALE = art.SCALE


def wanted() -> dict[str, int]:
    """bgf stem -> bitmap group (1-based; 0 = first bitmap)."""
    icons: dict[str, int] = {}
    for item in json.loads((DATA / "items.json").read_text(encoding="utf-8")):
        if item.get("vrIcon"):
            icons.setdefault(stem(item["vrIcon"]), item.get("viInventory_group") or 1)
    for name in ("spells.json", "skills.json"):
        for rec in json.loads((DATA / name).read_text(encoding="utf-8")):
            if rec.get("vrIcon"):
                icons.setdefault(stem(rec["vrIcon"]), 0)
    return icons


def stem(icon: str) -> str:
    return icon.lower().removesuffix(".bgf")


def decode(pal, name: str, group: int) -> Image.Image | None:
    f = bgf2png.find_file(bgf2png.CLIENT_RES, name)
    if not f:
        return None
    bgf = bgf2png.BGF(f)
    index = 0
    if group and 0 < group <= len(bgf.groups) and bgf.groups[group - 1]:
        index = bgf.groups[group - 1][0]
    index = min(max(index, 0), len(bgf.bitmaps) - 1)
    im = bgf.image(index, pal)
    box = im.getchannel("A").getbbox()
    if not box:
        return None
    im = im.crop(box)
    side = max(im.width, im.height)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
    return sq


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", action="append")
    a = ap.parse_args()
    style = json.loads(art.STYLE.read_text(encoding="utf-8"))
    pal = bgf2png.load_palette()
    srcs: dict[str, Image.Image] = {}
    index, missing = {}, []
    for name, group in sorted(wanted().items()):
        im = decode(pal, name, group)
        if im is None:
            missing.append(name)
            continue
        srcs[name] = im
        index[name] = {"group": group, "size": im.width}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "icons.json").write_text(json.dumps({"icons": index, "missing": missing}, indent=1))
    for variant in a.variant or list(style["art_variants"]):
        out = OUT / variant
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        if variant == "nearest":
            big = {n: im.resize((im.width * SCALE, im.height * SCALE), Image.Resampling.NEAREST) for n, im in srcs.items()}
        else:
            rgb = art.ai_upscale({n: (im, False) for n, im in srcs.items()}, style["art_variants"][variant]["model"])
            big = {}
            for n, im in srcs.items():
                b = rgb[n].convert("RGBA")
                b.putalpha(art.upscale_alpha(im.getchannel("A")))
                big[n] = b
        for n, im in big.items():
            im.save(out / ("T_Icon_%s.png" % n))
        print("%s: %d icons -> %s (missing: %s)" % (variant, len(big), out, ", ".join(missing) or "none"))


if __name__ == "__main__":
    main()
