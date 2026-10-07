"""
The original client's player sprite model, ported from the Server-104 source so the remaster can
draw players (later monsters) the way Meridian 59 did: a torso bitmap with overlays (arms, legs,
head, eyes, mouth, nose, hair, weapon) placed on hotspots, chosen by viewing angle, palette
translated for skin / hair / clothes, and animated by group ranges.

    clientd3d/draw.c:108       GetObjectPdib       angle -> bitmap of a group
    clientd3d/d3drender.c:5848 relative angle      angle = (facing - atan2(viewer - object)) & 4095
    clientd3d/d3drender.c:6246 D3DRenderOverlaysDraw  overlay placement and depth passes
    clientd3d/object3d.c:714   FindHotspot         over/under rules (same result as the D3D code)
    clientd3d/xlat.c:479       InitStandardXlats   palette translations
    clientd3d/animate.c:322    AnimateSingle       ANIMATE_NONE / CYCLE / ONCE
    kod/.../battler/player.kod SendAnimation, SendOverlays, SendMoveOverlays (groups and timing)

Coordinates: "base pixels" are pixels of the torso bitmap (the object's own icon), x right, y down,
origin at the torso bitmap's top-left. An overlay sits at hotspot + its own offset (both in base
pixels, the original's quirk) and is scaled by shrink_base / shrink_overlay. An overlay on an
overlay (face parts and hair on the head) chains: head position + hotspot2 and offset2 scaled by
shrink_base / shrink_head.

World size (d3drender.c:5933): a bitmap is w/shrink*16 by h/shrink*16 Kod fine units (1024 per
grid square). The torso is lifted by -yoffset*4 fine units and shifted by xoffset fine units.
"""
from __future__ import annotations

import colorsys
import json
import sys
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bgf2png"))
import bgf2png  # noqa: E402

DATA = ROOT / "data" / "sprites"
NUMDEGREES = 4096
FINE_PER_SQUARE = 1024
SQUARE_M = 2.2           # one Kod grid square in the remaster (docs/adr/0006)
TRANSPARENT = bgf2png.TRANSPARENT

# FindHotspot return values (object3d.h)
NONE, OVER, UNDER, OVERUNDER, OVEROVER, UNDERUNDER, UNDEROVER, OVERUNDEROVERUNDER = range(8)
HS_HELM = 2
UNDER_PASSES = [UNDERUNDER, UNDER, UNDEROVER]
OVER_PASSES = [OVERUNDEROVERUNDER, OVERUNDER, OVER, OVEROVER]


# ---------------------------------------------------------------------------------------------
# palette translations (xlat.c)

@lru_cache(maxsize=1)
def palette() -> np.ndarray:
    return np.array(bgf2png.load_palette(), dtype=np.uint8)


RAMPS = [0x10, 0x20, 0x30, 0x40, 0x50, 0x70, 0x90, 0xA0, 0xC0, 0xD0, 0xE0]   # 11 guild/clothes ramps
XLAT_GUILDCOLOR_BASE = 0x87
_TABLES = {
    "oldhair1": [0x23, 0x32, 0x34, 0x36, 0x59, 0x39, 0x3A, 0x3B, 0x36, 0x38, 0x5B, 0x47, 0x3C, 0x5C, 0x5E, 0x5E],
    "oldhair2": [0x50, 0x23, 0x24, 0x33, 0x25, 0x34, 0x26, 0x35, 0x27, 0x36, 0x28, 0x37, 0x29, 0x38, 0x2A, 0x39],
    "oldhair3": [0xC4, 0x52, 0xC6, 0x53, 0xC8, 0x54, 0xCA, 0x55, 0x56, 0x57, 0x58, 0x58, 0x59, 0x59, 0x5A, 0x5B],
    "platblond": [0xB0, 0xB0, 0xB1, 0xB1, 0xB2, 0xB2, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD9, 0xDA],
    "skin1": [0x20, 0xF0, 0xF0, 0x21, 0x21, 0x22, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B],
    "skin2": [0x20, 0x20, 0xF0, 0x21, 0x22, 0x23, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C],
    "green_skin": [0xD0, 0xD0, 0xB0, 0xB8, 0x60, 0x60, 0x61, 0x61, 0x62, 0x63, 0x64, 0x66, 0x67, 0x68, 0x6A, 0x6C],
    "yellow_skin": [0xD0, 0xD0, 0xB0, 0xB0, 0xB1, 0xB2, 0xB3, 0xC0, 0xC6, 0xC9, 0xCA, 0xCC, 0xCD, 0xCE, 0xCF, 0xCF],
}
LIGHT_LEVELS = 64


def _ramp(t, frm, to):              # CalcRampXlat, mask 0xF0
    for i in range(256):
        if i & 0xF0 == frm:
            t[i] = (i & 0x0F) | to


def _half_ramp(t, frm, to, off):    # CalcHalfRampXlat
    for i in range(256):
        if i & 0xF0 == frm:
            t[i] = (((i & 0x0F) // 2) | to) + off


def _move(t, frm, idx):             # CalcRampMoveXlat
    n = 0
    for i in range(256):
        if i & 0xF0 == frm:
            t[i] = idx[n]
            n += 1


def _light(t, frm, level):          # CalcLightXlat: nearest palette colour at a light level
    pal = palette().astype(np.float32)
    scale = level / LIGHT_LEVELS
    for i in range(256):
        if i & 0xF0 == frm:
            target = pal[t[i]] * scale
            t[i] = int(np.argmin(((pal - target) ** 2).sum(axis=1)))


@lru_cache(maxsize=None)
def xlat(xid: int) -> tuple[int, ...]:
    """Palette translation table for a standard xlat id (xlat.h). Unknown ids are identity."""
    t = list(range(256))
    gray, dblue, red = 0xD0, 0x90, 0x10
    ramp_to = {0x12: 0x10, 0x0A: 0x50, 0x0B: 0x60, 0x0C: 0x70, 0x0D: 0x80, 0x0E: 0x90, 0x0F: 0xA0,
               0x10: 0xC0, 0x11: 0xE0, 0x1A: 0x20, 0x1B: 0x30, 0x1D: 0x40}
    half = {0x21: (0x10, 0), 0x13: (0x50, 0), 0x14: (0x60, 0), 0x15: (0x70, 0), 0x16: (0x80, 0),
            0x20: (0x90, 0), 0x18: (0xA0, 0), 0x19: (0xC0, 0), 0x17: (0xE0, 0),
            0x2A: (0x10, 8), 0x22: (0x50, 8), 0x23: (0x60, 8), 0x24: (0x70, 8), 0x25: (0x80, 8),
            0x29: (0x90, 8), 0x27: (0xA0, 8), 0x28: (0xC0, 8), 0x26: (0xE0, 8), 0x2B: (0xD0, 8),
            0x1C: (0x30, 5), 0x1E: (0x40, 8)}
    if xid == 0:
        pass
    elif xid in ramp_to:                       # XLAT_GRAYTO<colour>, GRAYTOSKIN1/2/4
        _ramp(t, gray, ramp_to[xid])
    elif xid in half:                          # light (L) / dark (K) half ramps, GRAYTOSKIN3/5
        _half_ramp(t, gray, *half[xid])
    elif xid == 0x2C:                          # GRAYTOBLACK
        _light(t, gray, LIGHT_LEVELS // 6)
    elif xid == 0x2D:
        _move(t, gray, _TABLES["oldhair1"])
    elif xid == 0x2E:
        _move(t, gray, _TABLES["oldhair2"])
    elif xid == 0x2F:                          # GRAYTOBLOND == OLDHAIR3
        _move(t, gray, _TABLES["oldhair3"])
    elif xid == 0x30:
        _move(t, gray, _TABLES["platblond"])
    elif xid == 0x01:                          # DBLUETOSKIN1..4 (faces)
        _move(t, dblue, _TABLES["skin1"])
    elif xid == 0x02:
        _move(t, dblue, _TABLES["skin2"])
    elif xid == 0x03:
        _ramp(t, dblue, 0x30)
    elif xid == 0x04:
        _ramp(t, dblue, 0x40)
    elif xid == 0x05:
        _move(t, dblue, _TABLES["green_skin"])
    elif xid == 0x06:
        _move(t, dblue, _TABLES["yellow_skin"])
    elif xid == 0x07:
        _ramp(t, dblue, 0xD0)
    elif xid == 0x08:
        _ramp(t, dblue, 0x80)
    elif xid == 0x09:                          # ASHEN: blue -> gray while gray -> red
        _ramp(t, dblue, 0xD0)
        for i in range(256):
            if i & 0xF0 == gray:
                t[i] = (i & 0x0F) | red
    elif XLAT_GUILDCOLOR_BASE <= xid <= 0xFF:  # two-colour clothes: red -> ramp i, blue -> ramp j
        i, j = divmod(xid - XLAT_GUILDCOLOR_BASE, len(RAMPS))
        if i < len(RAMPS):
            _ramp(t, red, RAMPS[i])
            _ramp(t, dblue, RAMPS[j])
    return tuple(t)


# Kod constants (blakston.khd)
XLAT_TO = {"red": 0, "skin1": 1, "skin2": 2, "skin4": 3, "orange": 4, "green": 5, "blue": 6,
           "purple": 7, "yellow": 8, "gray": 9, "sky": 10}
SKIN = {"skin1": 0x01, "skin2": 0x02, "skin3": 0x03, "skin4": 0x04}       # PT_BLUE_TO_SKIN*
SKIN_TO_CLOTHES = {"skin1": "skin1", "skin2": "skin2", "skin3": "skin4", "skin4": "skin4"}
HAIR = {"none": 0, "orange": 0x0A, "red": 0x12, "skin1": 0x1A, "skin2": 0x1B, "skin3": 0x1C,
        "skin4": 0x1D, "skin5": 0x1E, "platblond": 0x30, "korange": 0x22, "kred": 0x2A,
        "kgray": 0x2B, "black": 0x2C, "blond": 0x2F}                     # the creator's list


def two_color(color1: str, skin: str) -> int:
    """EncodeTwoColorXLAT: red parts -> color1, blue (skin) parts -> the skin's clothes ramp."""
    return XLAT_GUILDCOLOR_BASE + XLAT_TO[color1] * len(RAMPS) + XLAT_TO[SKIN_TO_CLOTHES[skin]]


# ---------------------------------------------------------------------------------------------
# bitmaps

CUSTOM = ROOT / "build" / "sprites" / "custom"   # new parts (tools/sprites/new_hair.py): meta.json + bitmap_NN.png


class CustomBGF:
    """A part that isn't in the client: drawn in grey at the atlas scale (4 texels per "original"
    pixel) and coloured like the original's grey ramp (recolor_gray), placed by the same rules."""

    def __init__(self, folder: Path):
        meta = json.loads((folder / "meta.json").read_text())
        self.folder, self.name, self.shrink, self.groups = folder, folder.name, meta["shrink"], meta["groups"]
        self.bitmaps = []
        for m in meta["bitmaps"]:
            b = bgf2png.Bitmap()
            b.w, b.h, b.xoff, b.yoff = m["w"], m["h"], m["xoff"], m["yoff"]
            b.hotspots = [{"num": n, "x": x, "y": y} for n, x, y in m["hotspots"]]
            b.pixels = None
            self.bitmaps.append(b)

    def image(self, index: int) -> Image.Image:
        return Image.open(self.folder / f"bitmap_{index:02d}.png").convert("RGBA")


def is_custom(name: str) -> bool:
    return (CUSTOM / name / "meta.json").exists()


@lru_cache(maxsize=None)
def load_bgf(name: str):
    if is_custom(name):
        return CustomBGF(CUSTOM / name)
    f = bgf2png.find_file(bgf2png.CLIENT_RES, name)
    if not f:
        raise FileNotFoundError(f"{name}.bgf not found in the client resources")
    return bgf2png.BGF(f)


def recolor_gray(img: Image.Image, xid: int) -> Image.Image:
    """Colour a grey-drawn image the way a palette translation colours the grey ramp (0xD0-0xDF):
    each pixel's brightness finds its place on the ramp, and takes the translated ramp's colour."""
    pal = palette().astype(np.float32)
    ramp = np.arange(0xD0, 0xE0)
    grey = pal[ramp].mean(axis=1)                       # light -> dark
    target = pal[np.array(xlat(xid), dtype=np.uint8)[ramp]]
    a = np.asarray(img.convert("RGBA"), dtype=np.float32)
    lum = a[..., :3].mean(axis=-1)
    pos = np.interp(-lum, -grey, np.arange(16, dtype=np.float32))   # grey falls along the ramp
    lo = np.floor(pos).astype(int)
    hi = np.minimum(lo + 1, 15)
    f = (pos - lo)[..., None]
    rgb = target[lo] * (1 - f) + target[hi] * f
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), a[..., 3]]).astype(np.uint8), "RGBA")


@lru_cache(maxsize=4096)
def bitmap_rgba(name: str, index: int, xid: int = 0) -> Image.Image:
    if is_custom(name):
        b = load_bgf(name).bitmaps[index]
        return recolor_gray(load_bgf(name).image(index), xid).resize((b.w, b.h), Image.LANCZOS)
    b = load_bgf(name).bitmaps[index]
    idx = np.frombuffer(b.pixels, dtype=np.uint8).reshape(b.h, b.w)
    rgb = palette()[np.array(xlat(xid), dtype=np.uint8)[idx]]
    a = np.where(idx == TRANSPARENT, 0, 255).astype(np.uint8)
    return Image.fromarray(np.dstack([rgb, a]), "RGBA")


def view_slot(angle: int, n: int) -> int:
    """GetObjectPdib: which of a group's n bitmaps is seen at a relative angle (0 = facing us)."""
    interval = NUMDEGREES // n + 1
    return ((angle + interval // 2) % NUMDEGREES) // interval


def bitmap_index(name: str, group: int, angle: int) -> int | None:
    """Group is 0-based (client); None if the group or the view is missing (index -1)."""
    groups = load_bgf(name).groups
    if group < 0 or group >= len(groups) or not groups[group]:
        return None
    idx = groups[group][view_slot(angle, len(groups[group]))]
    return None if idx < 0 else idx


def _find_hotspot(name: str, index: int, hotspot: int):
    for h in load_bgf(name).bitmaps[index].hotspots:
        if abs(h["num"]) == hotspot:
            kind = (OVERUNDEROVERUNDER if h["num"] == HS_HELM else OVER) if h["num"] > 0 else UNDER
            return kind, h["x"], h["y"]
    return NONE, 0, 0


# ---------------------------------------------------------------------------------------------
# compositing

@dataclass
class Layer:
    """One overlay as sent by Kod: bgf, hotspot (0 = none), xlat, current group (0-based)."""
    name: str
    hotspot: int
    xid: int = 0
    group: int = 0


@dataclass
class Placed:
    name: str
    index: int
    xid: int
    x: float        # top-left, base pixels
    y: float
    scale: float    # overlay pixels -> base pixels
    depth: int      # pass kind, for draw order
    order: int      # position in the overlay list


def place(body: Layer, overlays: list[Layer], angle: int) -> tuple[int, list[Placed]] | None:
    """Port of D3DRenderOverlaysDraw / DrawObject: returns the torso index and every drawable
    bitmap in draw order (underlay passes, torso, overlay passes), positioned in base pixels."""
    bi = bitmap_index(body.name, body.group, angle)
    if bi is None:
        return None
    s_base = load_bgf(body.name).shrink
    placed: list[Placed] = []
    for order, ov in enumerate(overlays):
        oi = bitmap_index(ov.name, ov.group, angle)
        if oi is None or not ov.hotspot:
            continue
        ob = load_bgf(ov.name)
        bm = ob.bitmaps[oi]
        kind, hx, hy = _find_hotspot(body.name, bi, ov.hotspot)
        if kind != NONE:
            x, y = hx + bm.xoff, hy + bm.yoff
        else:   # an overlay on an overlay: find which overlay carries the hotspot
            for base_ov in overlays:
                b2i = bitmap_index(base_ov.name, base_ov.group, angle)
                if b2i is None or base_ov is ov:
                    continue
                k2, h2x, h2y = _find_hotspot(base_ov.name, b2i, ov.hotspot)
                if k2 == NONE:
                    continue
                k1, h1x, h1y = _find_hotspot(body.name, bi, base_ov.hotspot)
                if k1 == NONE:
                    continue
                b2 = load_bgf(base_ov.name)
                r = s_base / b2.shrink
                bm2 = b2.bitmaps[b2i]
                x = h1x + bm2.xoff + (h2x + bm.xoff) * r
                y = h1y + bm2.yoff + (h2y + bm.yoff) * r
                kind = ({OVER: OVEROVER, UNDER: OVERUNDER}[k2] if k1 == OVER
                        else {OVER: UNDEROVER, UNDER: UNDERUNDER}[k2])
                break
            else:
                continue
        placed.append(Placed(ov.name, oi, ov.xid, x, y, s_base / ob.shrink, kind, order))
    rank = {d: i for i, d in enumerate(UNDER_PASSES + [None] + OVER_PASSES)}
    placed.sort(key=lambda p: (rank.get(p.depth, len(rank)), p.order))
    return bi, placed


@dataclass
class Composite:
    image: Image.Image          # RGBA, base pixels * scale
    origin: tuple[float, float]  # torso top-left inside the image, in output pixels
    scale: float                # output pixels per base pixel
    shrink: int                 # torso shrink
    torso: tuple[int, int, int, int]  # torso index, w, h, (unused)
    feet_y: float               # output-pixel row of the floor (object position)
    center_x: float             # output-pixel column of the object position


def composite(body: Layer, overlays: list[Layer], angle: int, scale: int = 1,
              canvas: tuple[int, int, int, int] | None = None, source=None) -> Composite | None:
    """Draw the player at a relative angle. canvas = fixed (left, top, right, bottom) in base
    pixels relative to the torso origin, so every frame of an animation lines up; else fit.
    source(name, index, xid) -> a higher-resolution RGBA of that bitmap (e.g. the 4x upscale),
    resampled to its place; default: the original pixels, nearest-neighbour (as the client)."""
    res = place(body, overlays, angle)
    if res is None:
        return None
    bi, placed = res
    tb = load_bgf(body.name).bitmaps[bi]
    items = []
    under = [p for p in placed if p.depth in UNDER_PASSES]
    over = [p for p in placed if p.depth not in UNDER_PASSES]
    for p in under + [Placed(body.name, bi, body.xid, 0, 0, 1.0, -1, -1)] + over:
        im = bitmap_rgba(p.name, p.index, p.xid)
        items.append((p, im, im.width * p.scale, im.height * p.scale))
    if canvas is None:
        l = min(p.x for p, im, w, h in items)
        t = min(p.y for p, im, w, h in items)
        r = max(p.x + w for p, im, w, h in items)
        b = max(p.y + h for p, im, w, h in items)
        canvas = (int(np.floor(l)) - 1, int(np.floor(t)) - 1, int(np.ceil(r)) + 1, int(np.ceil(b)) + 1)
    l, t, r, b = canvas
    out = Image.new("RGBA", ((r - l) * scale, (b - t) * scale), (0, 0, 0, 0))
    for p, im, w, h in items:
        sw, sh = max(1, round(w * scale)), max(1, round(h * scale))
        hi = source(p.name, p.index, p.xid) if source else None
        im = hi.resize((sw, sh), Image.LANCZOS) if hi is not None else im.resize((sw, sh), Image.NEAREST)
        out.alpha_composite(im, (round((p.x - l) * scale), round((p.y - t) * scale)))
    fx, fy = feet(body.name, bi)
    feet_y = (fy - t) * scale
    center_x = (fx - l) * scale
    return Composite(out, (-l * scale, -t * scale), scale, load_bgf(body.name).shrink, (bi, tb.w, tb.h, 0),
                     feet_y, center_x)


def feet(name: str, index: int) -> tuple[float, float]:
    """The object's position (where it stands) in base pixels of torso bitmap `index`: the D3D
    client draws the torso quad with its bottom at -yoffset*4 fine units and its centre at
    +xoffset fine units (one base pixel = 16/shrink fine units)."""
    b = load_bgf(name)
    tb = b.bitmaps[index]
    fine_per_px = 16.0 / b.shrink
    return tb.w / 2.0 - tb.xoff / fine_per_px, tb.h - tb.yoff * 4.0 / fine_per_px


def base_px_to_m(px: float, shrink: int) -> float:
    return px / shrink * 16.0 / FINE_PER_SQUARE * SQUARE_M


# ---------------------------------------------------------------------------------------------
# animation (animate.c AnimateSingle), Kod groups are 1-based

@dataclass
class Track:
    mode: str = "none"     # none | cycle | once
    period: int = 0        # ms per group
    low: int = 1
    high: int = 1
    final: int = 1
    group: int = 1         # current Kod group
    tick: int = 0

    @staticmethod
    def from_json(d: dict | int) -> "Track":
        if isinstance(d, int):
            return Track("none", group=d)
        tr = Track(d.get("mode", "none"), d.get("period", 0), d.get("low", 1), d.get("high", d.get("low", 1)),
                   d.get("final", 1))
        tr.group = tr.low
        tr.tick = tr.period
        return tr

    def step(self, dt: int):
        if self.mode == "none":
            return
        self.tick -= dt
        if self.tick > 0:
            return
        if self.mode == "cycle":
            self.group = self.low + (self.group - self.low + 1) % (self.high - self.low + 1)
        elif self.group >= self.high:
            self.mode, self.group = "none", self.final
        else:
            self.group += 1
        self.tick = self.period

    def length_ms(self) -> int:
        n = self.high - self.low + 1
        return n * self.period if self.mode != "none" else 0


def load_json(name: str) -> dict:
    return json.loads((DATA / name).read_text(encoding="utf-8"))


@dataclass
class Look:
    """A player appearance: bgf names per part plus colours (Kod names)."""
    gender: str = "male"
    body: str = "bta"
    legs: str = "bfa"
    right_arm: str = "bra"
    left_arm: str = "bla"
    head: str = "phax"
    eyes: str = "peax"
    mouth: str = "pmax"
    nose: str = "pnax"
    hair: str | None = "ptcd"
    skin: str = "skin3"
    hair_color: str = "blond"
    shirt: str = "gray"
    pants: str = "skin1"
    weapon: str | None = None
    action_face: int = 1            # piAction: eyes / mouth group (expression)
    extra: dict = field(default_factory=dict)

    @staticmethod
    def from_json(d: dict) -> "Look":
        return Look(**{k: v for k, v in d.items() if k in Look.__dataclass_fields__})

    def layers(self, groups: dict[str, int]) -> tuple[Layer, list[Layer]]:
        """Body + overlays in SendOverlays order, groups 1-based per part."""
        skin = SKIN[self.skin]
        shirt = two_color(self.shirt, self.skin)
        pants = two_color(self.pants, self.skin)
        g = lambda k, d=1: groups.get(k, d) - 1  # noqa: E731
        ovs = [Layer(self.left_arm, 31, shirt, g("left_arm")),
               Layer(self.right_arm, 21, shirt, g("right_arm")),
               Layer(self.legs, 41, pants, g("legs")),
               Layer(self.head, 1, skin, g("head")),
               Layer(self.mouth, 12, skin, g("mouth", self.action_face)),
               Layer(self.eyes, 11, skin, g("eyes", self.action_face)),
               Layer(self.nose, 14, skin, g("nose"))]
        if self.hair:
            ovs.append(Layer(self.hair, 13, HAIR[self.hair_color], g("hair")))
        if self.weapon:
            ovs.append(Layer(self.weapon, 22, 0, g("weapon")))
        return Layer(self.body, 0, shirt, g("body")), ovs
