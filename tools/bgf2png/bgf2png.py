"""
Decode Meridian 59 .bgf sprite/texture files (BGF v10) to PNG.

    python tools/bgf2png/bgf2png.py cow mummy bunny        # creatures -> build/bgf/<name>/
    python tools/bgf2png/bgf2png.py --textures 9592 1022    # wall/floor textures -> build/textures/grdNNNNN.png
    python tools/bgf2png/bgf2png.py --textures-for-zones    # every texture the demo .roo files use
    python tools/bgf2png/bgf2png.py --list cow              # print header / groups only

Output for a sprite:
    build/bgf/<name>/frame_NN.png   every bitmap, RGBA (palette index 254 = transparent)
    build/bgf/<name>/sheet.png      contact sheet: one row per group, columns = the group's indices
    build/bgf/<name>/meta.json      shrink, sizes, offsets, hotspots, groups

The extracted art is the original game's (not covered by the GPL) — it is written to
build/ (git-ignored) and used only as reference input for the remaster pipeline.

Format (makebgf/writebgf.c, clientd3d/dibutil.c):
    "BGF\\x11", int version(10), char name[32], int num_bitmaps, int num_groups,
    int max_indices, int shrink,
    per bitmap: int w, int h, int xoff, int yoff, u8 n_hotspots, n * (i8 num, int x, int y),
                u8 compressed, int clen, bytes (zlib if compressed else w*h raw)
    per group:  int n, n * int index
Wall/floor textures (grd*) are written rotated 90 degrees (makebgf -r): the stored
image is the transpose of the original.
World size: one texel = shrink^-1 Kod fineness units (64 per grid square), see
clientd3d/d3drender.c wall UV code (t = length * shrink / height).
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RES = Path(r"H:\Steam\steamapps\common\Meridian 59\resource")
PALETTE_FILE = ROOT / "Server-104" / "blakston.pal"
TRANSPARENT = 254


def load_palette() -> list[tuple[int, int, int]]:
    pal = []
    for line in PALETTE_FILE.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3:
            pal.append(tuple(int(v) for v in parts[:3]))
    if len(pal) != 256:
        raise ValueError(f"palette has {len(pal)} entries")
    return pal


class Bitmap:
    __slots__ = ("w", "h", "xoff", "yoff", "hotspots", "pixels")


class BGF:
    def __init__(self, path: Path):
        d = path.read_bytes()
        if d[:4] != b"BGF\x11":
            raise ValueError(f"{path.name}: not a BGF file")
        p = 4
        self.version, = struct.unpack_from("<i", d, p); p += 4
        if self.version < 10:
            raise ValueError(f"{path.name}: BGF version {self.version} unsupported")
        self.name = d[p:p + 32].split(b"\0")[0].decode("latin-1"); p += 32
        n_bmp, n_grp, self.max_indices, self.shrink = struct.unpack_from("<4i", d, p); p += 16
        self.bitmaps: list[Bitmap] = []
        for _ in range(n_bmp):
            b = Bitmap()
            b.w, b.h, b.xoff, b.yoff = struct.unpack_from("<4i", d, p); p += 16
            nh = d[p]; p += 1
            b.hotspots = []
            for _ in range(nh):
                num, = struct.unpack_from("<b", d, p); p += 1
                x, y = struct.unpack_from("<2i", d, p); p += 8
                b.hotspots.append({"num": num, "x": x, "y": y})
            comp = d[p]; p += 1
            clen, = struct.unpack_from("<i", d, p); p += 4
            if comp:
                b.pixels = zlib.decompress(d[p:p + clen]); p += clen
            else:
                b.pixels = d[p:p + b.w * b.h]; p += b.w * b.h
            self.bitmaps.append(b)
        self.groups: list[list[int]] = []
        for _ in range(n_grp):
            n, = struct.unpack_from("<i", d, p); p += 4
            self.groups.append(list(struct.unpack_from("<%di" % n, d, p))); p += 4 * n

    def image(self, i: int, pal, rotated: bool = False) -> Image.Image:
        b = self.bitmaps[i]
        img = Image.new("RGBA", (b.w, b.h))
        px = [(*pal[v], 0 if v == TRANSPARENT else 255) for v in b.pixels]
        img.putdata(px)
        if rotated:
            img = img.transpose(Image.Transpose.TRANSPOSE)
        return img

    def meta(self, rotated=False) -> dict:
        return {
            "name": self.name, "version": self.version, "shrink": self.shrink, "rotated": rotated,
            "bitmaps": [{"w": (b.h if rotated else b.w), "h": (b.w if rotated else b.h),
                         "xoff": b.xoff, "yoff": b.yoff, "hotspots": b.hotspots} for b in self.bitmaps],
            "groups": self.groups,
        }


FALLBACK_RES = ROOT / "Server-104" / "resource"   # Server 104 additions not in the Steam client
_fallback_index: dict[str, Path] | None = None


def find_file(res: Path, stem: str) -> Path | None:
    """Look in the client resource dir first, then anywhere under Server-104/resource."""
    global _fallback_index
    stem = stem.lower().removesuffix(".bgf")
    cand = res / f"{stem}.bgf"
    if cand.exists():
        return cand
    for p in res.glob("*.bgf"):
        if p.stem.lower() == stem:
            return p
    if _fallback_index is None:
        _fallback_index = {p.stem.lower(): p for p in FALLBACK_RES.rglob("*.bgf")}
    return _fallback_index.get(stem)


def export_sprite(bgf: BGF, out: Path, pal):
    out.mkdir(parents=True, exist_ok=True)
    imgs = [bgf.image(i, pal) for i in range(len(bgf.bitmaps))]
    for i, im in enumerate(imgs):
        im.save(out / f"frame_{i:02d}.png")
    # contact sheet: rows = groups (or one row of all frames if no groups)
    rows = bgf.groups or [list(range(len(imgs)))]
    cw = max(im.width for im in imgs) + 8
    ch = max(im.height for im in imgs) + 8
    cols = max(len(r) for r in rows)
    sheet = Image.new("RGBA", (cw * cols, ch * len(rows)), (40, 40, 48, 255))
    for r, grp in enumerate(rows):
        for c, idx in enumerate(grp):
            if 0 <= idx < len(imgs):
                im = imgs[idx]
                sheet.alpha_composite(im, (c * cw + (cw - im.width) // 2, r * ch + (ch - im.height) // 2))
    sheet.save(out / "sheet.png")
    (out / "meta.json").write_text(json.dumps(bgf.meta(), indent=1))


def export_texture(bgf: BGF, out_png: Path, pal) -> dict:
    out_png.parent.mkdir(parents=True, exist_ok=True)
    im = bgf.image(0, pal, rotated=True)
    im.save(out_png)
    m = bgf.meta(rotated=True)
    w, h = m["bitmaps"][0]["w"], m["bitmaps"][0]["h"]
    # world size in grid squares (64 Kod units per square)
    return {"name": bgf.name, "w": w, "h": h, "shrink": bgf.shrink, "frames": len(bgf.bitmaps),
            "squares_w": w / bgf.shrink / 64.0, "squares_h": h / bgf.shrink / 64.0,
            "has_transparency": any(v == TRANSPARENT for v in bgf.bitmaps[0].pixels)}


def zone_texture_ids() -> set[int]:
    sys.path.insert(0, str(ROOT / "Server-104" / "roomedit" / "roogen"))
    from roofile import Room
    zones = json.loads((ROOT / "data" / "zones.json").read_text(encoding="utf-8"))
    rooms = ROOT / "Server-104" / "resource" / "rooms"
    ids: set[int] = set()
    for z in zones:
        roo = next((p for p in rooms.iterdir() if p.name.lower() == z["roo"].lower()), None)
        if not roo:
            continue
        r = Room.load(str(roo))
        for s in r.sectors:
            ids |= {s.floor_type, s.ceiling_type}
        for sd in r.sidedefs:
            ids |= {sd.type_normal, sd.type_above, sd.type_below}
    ids.discard(0)
    return ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("--res", type=Path, default=DEFAULT_RES)
    ap.add_argument("--textures", nargs="*", type=int)
    ap.add_argument("--textures-for-zones", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    pal = load_palette()

    for n in a.names:
        f = find_file(a.res, n)
        if not f:
            print(f"!! {n}: not found in {a.res}")
            continue
        bgf = BGF(f)
        print(f"{f.name}: {len(bgf.bitmaps)} bitmaps, {len(bgf.groups)} groups, shrink {bgf.shrink}, "
              f"sizes {sorted({(b.w, b.h) for b in bgf.bitmaps})[:4]}")
        if a.list:
            for i, g in enumerate(bgf.groups):
                print(f"   group {i}: {g}")
            continue
        export_sprite(bgf, ROOT / "build" / "bgf" / f.stem.lower(), pal)

    tex_ids = set(a.textures or [])
    if a.textures_for_zones:
        tex_ids |= zone_texture_ids()
    if tex_ids:
        out_dir = ROOT / "build" / "textures"
        catalog, missing = {}, []
        for t in sorted(tex_ids):
            f = find_file(a.res, "grd%05d" % t)
            if not f:
                missing.append(t)
                continue
            catalog["grd%05d" % t] = export_texture(BGF(f), out_dir / ("grd%05d.png" % t), pal)
        (out_dir / "catalog.json").write_text(json.dumps({"textures": catalog, "missing": missing}, indent=1))
        print(f"textures: {len(catalog)} exported, {len(missing)} missing from client: {missing}")


if __name__ == "__main__":
    main()
