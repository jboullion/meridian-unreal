"""
Phase 1 of docs/sprites.md: the runtime data for sprite players.

    build/texai/.venv/Scripts/python tools/sprites/build_player_sprites.py [--with-ai]

For every look in data/sprites/looks.json it gathers each bitmap the look can show (all actions,
all angles), plus every bitmap of every face part and hair the character creator offers
(data/charinfo.json), upscales it 4x (upscale_parts.py: the method per part is
data/sprites/upscale.json; cached) and packs one atlas per bgf:

    build/sprites/atlas/T_Spr_<bgf>.png              imported by tools/ue/import_sprites.py into
                                                     /Game/Generated/Sprites; alpha = coverage
    build/sprites/atlas/T_SprRamp_<bgf>.png          player parts: each original pixel's palette
                                                     ramp (luts.ramp_cell), 1/4 the atlas's size
    build/sprites/lut/T_SprXlat.png, T_SprClass.png  palette lookups for the runtime colours (luts.py)
    data/sprites/player_parts.json                   read at runtime (UMRSpriteData): every bgf's
                                                     shrink, groups and bitmaps (size, offset,
                                                     hotspots), every atlas's cells, and the looks
                                                     with their palette translations resolved

Each part is stored once, in its original untranslated colours: M_SpriteBody recolours it with the
look's palette translation at runtime (luts.py), so any number of colour combinations share the
same atlases. Atlases are rewritten only when their cells change.

Looks marked "ai" (an AI-made part, tools/sprites/new_hair.py) are left out unless --with-ai.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from collections import defaultdict

from PIL import Image

import numpy as np

import luts
import m59sprites as ms
import monsters as mon
import tweens as tw
import upscale_parts as up

OUT = ms.ROOT / "build" / "sprites" / "atlas"
LAYOUT = ms.DATA / "player_parts.json"
SCALE = 4
PAD = 4                   # transparent pixels around each cell (mips, bilinear)
EXTRA = [("povhand", 0), ("povsword", 0)]  # first-person hand / sword (window overlays), no translation
TWEENS = 3               # in-betweens per original step (tweens.py), 0 = none
PARTS = ["body", "left_arm", "right_arm", "legs", "head", "eyes", "mouth", "nose", "hair", "weapon"]


def pow2(n: int) -> int:
    p = 64
    while p < n:
        p *= 2
    return p


def pack(cells: dict[int, Image.Image]) -> tuple[int, int, dict[int, list[int]]]:
    """Shelf packing, tallest first: -> atlas w, h, {index: [x, y, w, h]} (cell without padding)."""
    area = sum((im.width + 2 * PAD) * (im.height + 2 * PAD) for im in cells.values())
    widest = max(im.width for im in cells.values()) + 2 * PAD
    w = max(pow2(int(area ** 0.5 * 1.05)), pow2(widest))
    x = y = shelf = 0
    rects = {}
    for i, im in sorted(cells.items(), key=lambda kv: (-kv[1].height, kv[0])):
        cw, ch = im.width + 2 * PAD, im.height + 2 * PAD
        if x + cw > w:
            x, y, shelf = 0, y + shelf, 0
        rects[i] = [x + PAD, y + PAD, im.width, im.height]
        x += cw
        shelf = max(shelf, ch)
    return w, pow2(y + shelf), rects


def look_parts(look: ms.Look, materials: dict) -> dict[str, dict]:
    """Per part: bgf, palette translation, attach hotspot and surface class (materials.json)."""
    body, ovs = look.layers({})
    by_hotspot = {31: "left_arm", 21: "right_arm", 41: "legs", 1: "head", 12: "mouth", 11: "eyes", 14: "nose",
                  13: "hair", 22: "weapon"}
    names = [c["name"] for c in materials["classes"]]

    def cls(role, bgf):
        return names.index(materials["parts"].get(bgf, materials["roles"].get(role, "cloth")))
    parts = {"body": {"bgf": body.name, "xlat": body.xid, "hotspot": 0, "class": cls("body", body.name)}}
    for o in ovs:
        role = by_hotspot[o.hotspot]
        parts[role] = {"bgf": o.name, "xlat": o.xid, "hotspot": o.hotspot, "class": cls(role, o.name)}
    return parts


def bounds(look: ms.Look, actions: dict) -> list[float]:
    """[left, top, right, bottom] in base pixels around the feet (the object position) that holds
    every frame of every action at every angle: the runtime's render target box."""
    import composite as cp
    box = [1e9, 1e9, -1e9, -1e9]
    for act in actions.values():
        for groups, _ in cp.timeline(look, act):
            body, ovs = look.layers(groups)
            for a in cp.ANGLES:
                res = ms.place(body, ovs, a)
                if not res:
                    continue
                bi, placed = res
                fx, fy = ms.feet(body.name, bi)
                tb = ms.load_bgf(body.name).bitmaps[bi]
                rects = [(0, 0, tb.w, tb.h)] + [(p.x, p.y, p.x + ms.load_bgf(p.name).bitmaps[p.index].w * p.scale,
                                                 p.y + ms.load_bgf(p.name).bitmaps[p.index].h * p.scale) for p in placed]
                for l, t, r, b in rects:
                    box = [min(box[0], l - fx), min(box[1], t - fy), max(box[2], r - fx), max(box[3], b - fy)]
    return [round(v - 1 if i < 2 else v + 1, 2) for i, v in enumerate(box)]


def monster_bounds(bgf: str, actions: dict) -> list[float]:
    """As bounds(), for a single-bitmap creature: every group its actions use, at every angle."""
    b = ms.load_bgf(bgf)
    groups = {1}
    for act in actions.values():
        t = act.get("body")
        if isinstance(t, dict) and t:
            groups |= set(range(t["low"], t["high"] + 1)) | ({t["final"]} if "final" in t else set())
    box = [1e9, 1e9, -1e9, -1e9]
    for g in groups:
        for a in range(0, ms.NUMDEGREES, 256):
            bi = ms.bitmap_index(bgf, g - 1, a)
            if bi is None:
                continue
            fx, fy = ms.feet(bgf, bi)
            tb = b.bitmaps[bi]
            box = [min(box[0], -fx), min(box[1], -fy), max(box[2], tb.w - fx), max(box[3], tb.h - fy)]
    return [round(v - 1 if i < 2 else v + 1, 2) for i, v in enumerate(box)]


MONSTER_SCALE = 2.0   # creatures: at most 2x their original pixels (their atlases are big: 50-70 frames)
MAX_ATLAS = 4096


def cell_scale(bgf: str, monster_bgfs: set) -> float:
    """Atlas texels per original pixel. Player parts: the 4x upscale. Creatures: up to one texel per
    Kod fine unit, at most MONSTER_SCALE, so a high-resolution original (bunny2: shrink 18) isn't
    blown up: 16 / shrink; 1 = the original pixels, no upscale."""
    if bgf not in monster_bgfs:
        return SCALE
    return min(MONSTER_SCALE, max(1.0, 16.0 / ms.load_bgf(bgf).shrink))


def cell_image(bgf: str, i: int, scale: float) -> Image.Image:
    b = ms.load_bgf(bgf).bitmaps[i]
    if scale <= 1.0:
        return ms.bitmap_rgba(bgf, i, 0)
    im = up.get(bgf, i, 0)
    return im if scale >= SCALE else im.resize((max(1, round(b.w * scale)), max(1, round(b.h * scale))), Image.LANCZOS)


def coverage(im: Image.Image) -> Image.Image:
    """The cell with PAD texels around it: alpha 0 or 255 (the canvas clips at 0.5 anyway), and the
    colour of transparent texels near the part grown from it, into the padding too: filtering and
    mips at the edges then blend only the part's own colours, never black or the key colour."""
    a = np.zeros((im.height + 2 * PAD, im.width + 2 * PAD, 4), np.uint8)
    a[PAD:PAD + im.height, PAD:PAD + im.width] = np.asarray(im.convert("RGBA"))
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    return Image.fromarray(np.dstack([fill_near(a), a[..., 3]]), "RGBA")


def fill_near(a: np.ndarray, rings: int = 2 * PAD) -> np.ndarray:
    """RGB with transparent texels within `rings` of the part taken from their opaque neighbours;
    farther ones get the part's mean colour (mips only ever reach them averaged)."""
    rgb, known = a[..., :3].astype(np.float32), a[..., 3] >= 128
    if not known.any():
        return a[..., :3]
    rgb[~known] = 0
    h, w = known.shape
    for _ in range(rings):
        if known.all():
            break
        k = np.pad(known.astype(np.float32), 1)
        c = np.pad(rgb, ((1, 1), (1, 1), (0, 0))) * k[..., None]
        acc, cnt = np.zeros_like(rgb), np.zeros(known.shape, np.float32)
        for dy in (0, 1, 2):
            for dx in (0, 1, 2):
                if dx != 1 or dy != 1:
                    acc += c[dy:dy + h, dx:dx + w]
                    cnt += k[dy:dy + h, dx:dx + w]
        ring = (~known) & (cnt > 0)
        if not ring.any():
            break
        rgb[ring] = acc[ring] / cnt[ring][:, None]
        known |= ring
    rgb[~known] = rgb[a[..., 3] >= 128].mean(axis=0)
    return np.clip(rgb + 0.5, 0, 255).astype(np.uint8)


def creator_bounds(look: ms.Look, actions: dict, box: list[float]) -> list[float]:
    """bounds() of a look with each of its gender's creator hair styles, all together."""
    f = ms.ROOT / "data" / "charinfo.json"
    if not f.exists():
        return box
    import dataclasses
    hairs = json.loads(f.read_text(encoding="utf-8"))["faces"].get(look.gender, {}).get("hair", [])
    for h in hairs:
        b = bounds(dataclasses.replace(look, hair=None if h == "blank" else h), actions)
        box = [min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3])]
    return box


def creator_parts() -> set[str]:
    """Every face part and hair the creator offers (data/charinfo.json; "blank" is bald)."""
    f = ms.ROOT / "data" / "charinfo.json"
    if not f.exists():
        print("  data/charinfo.json missing (run tools/kod_extract/extract.py): no creator parts")
        return set()
    faces = json.loads(f.read_text(encoding="utf-8"))["faces"]
    return {b for g in faces.values() for lst in g.values() for b in lst if b != "blank"}


def bgf_meta(name: str, tween_keys: list, xid: int) -> dict:
    """The bgf's bitmaps, then its in-betweens (tween_keys: (a, b, k) in index order)."""
    b = ms.load_bgf(name)
    meta = {"shrink": b.shrink, "groups": b.groups,
            "bitmaps": [{"w": m.w, "h": m.h, "xoff": m.xoff, "yoff": m.yoff,
                         "hotspots": [[h["num"], h["x"], h["y"]] for h in m.hotspots]} for m in b.bitmaps]}
    if tween_keys:
        meta["tweens"] = defaultdict(list)
        for i, (a, bb, k) in enumerate(tween_keys):
            meta["bitmaps"].append(tw.get(name, a, bb, k, TWEENS, xid)[1])
            meta["tweens"][f"{a}>{bb}"].append(len(b.bitmaps) + i)
        meta["tweens"] = dict(meta["tweens"])
    return meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-ai", action="store_true", help='also the looks with an AI-made part (looks.json "ai")')
    args = ap.parse_args()
    looks_src = ms.load_json("looks.json")
    actions = {k: v for k, v in ms.load_json("player_actions.json").items() if not k.startswith("_")}
    materials = ms.load_json("materials.json")
    need: dict[str, set[int]] = defaultdict(set)   # bgf -> bitmaps
    looks = {}
    for name, d in looks_src.items():
        if name.startswith("_"):
            continue
        if d.get("ai") and not args.with_ai:
            print(f"  skipping look {name}: an AI-made part (--with-ai to include it)")
            continue
        look = ms.Look.from_json(d)
        missing = [v for v in (look.body, look.legs, look.left_arm, look.right_arm, look.head, look.eyes, look.mouth,
                               look.nose, look.hair, look.weapon) if v and not ms.is_custom(v)
                   and not ms.bgf2png.find_file(ms.bgf2png.CLIENT_RES, v)]
        if missing:   # e.g. an AI part (build/sprites/custom) not generated on this machine
            print(f"  skipping look {name}: missing {missing}")
            continue
        for bgf, idx, xid in up.look_bitmaps(look, actions):
            need[bgf].add(idx)
        looks[name] = {"gender": look.gender, "action_face": look.action_face, "parts": look_parts(look, materials),
                       "bounds": bounds(look, actions)}
        if name.startswith("player_"):
            # the bases players are drawn on wear any of the creator's hair: room for all of them
            looks[name]["bounds"] = creator_bounds(look, actions, looks[name]["bounds"])
        # a look may also wear its parts in groups no action uses: add every bitmap of the face
        for part in ("eyes", "mouth"):
            p = looks[name]["parts"][part]
            need[p["bgf"]] |= {i for g in ms.load_bgf(p["bgf"]).groups for i in g if i >= 0}
    for bgf, _ in EXTRA:
        need[bgf] |= set(range(len(ms.load_bgf(bgf).bitmaps)))
    # the creator's face parts and hair: every bitmap (all views, all expressions)
    for bgf in sorted(creator_parts()):
        need[bgf] |= set(range(len(ms.load_bgf(bgf).bitmaps)))

    # monsters and NPCs (monsters.py): one body bitmap, their own Kod animations; corpses
    names = [c["name"] for c in materials["classes"]]
    creature = names.index(materials["roles"].get("monster", "cloth"))
    monster_table, monster_actions, monster_bgfs = {}, {}, set()
    for cls, d in mon.definitions().items():
        look = f"m_{cls}"
        b = ms.load_bgf(d["bgf"])
        need[d["bgf"]] |= set(range(len(b.bitmaps)))
        monster_bgfs.add(d["bgf"])
        looks[look] = {"kind": "monster", "action_face": 1, "actions": d["actions"],
                       "parts": {"body": {"bgf": d["bgf"], "xlat": 0, "hotspot": 0, "class": creature}},
                       "bounds": monster_bounds(d["bgf"], d["actions"])}
        monster_actions[d["bgf"]] = d["actions"]
        dead = None
        if d["dead_bgf"]:
            dead = f"m_{cls}_dead"
            need[d["dead_bgf"]] |= set(range(len(ms.load_bgf(d["dead_bgf"]).bitmaps)))
            monster_bgfs.add(d["dead_bgf"])
            looks[dead] = {"kind": "corpse", "action_face": 1, "actions": {"stand": {}},
                           "parts": {"body": {"bgf": d["dead_bgf"], "xlat": 0, "hotspot": 0, "class": creature}},
                           "bounds": monster_bounds(d["dead_bgf"], {})}
        monster_table[cls] = {"look": look, "dead_look": dead, **{k: d[k] for k in (
            "name", "speed_cms", "vision_cm", "aggressive", "npc", "stationary", "sounds")}}
    print(f"  {len(monster_table)} monster classes")

    # in-betweens (tweens.py) for every step the animated parts take
    pairs: dict[str, set] = defaultdict(set)
    if TWEENS:
        for name, d in looks_src.items():
            if name.startswith("_") or name not in looks:
                continue
            for part in tw.TWEEN_PARTS:
                pd = looks[name]["parts"].get(part)
                if pd:
                    pairs[pd["bgf"]] |= set(tw.bitmap_pairs(pd["bgf"], tw.transitions(actions, part)))
        for bgf, acts in monster_actions.items():
            pairs[bgf] |= set(tw.bitmap_pairs(bgf, tw.transitions(acts, "body")))
    tween_keys = {}
    for bgf, ps in pairs.items():
        if bgf in monster_bgfs and cell_scale(bgf, monster_bgfs) <= 1.0:
            continue   # an original already finer than the atlas needs: no 4x upscale to flow on
        ok = [(a, b) for a, b in sorted(ps) if tw.plan(bgf, a, b)]
        tween_keys[bgf] = [(a, b, k) for a, b in ok for k in range(1, TWEENS + 1)]
        print(f"  {bgf}: {len(ok)}/{len(ps)} steps get in-betweens")

    OUT.mkdir(parents=True, exist_ok=True)
    atlases, total = {}, 0
    for bgf, idxs in sorted(need.items()):
        # colour cells (alpha = coverage), and for player parts each original pixel's palette ramp
        # (luts.ramp_cell): from the palette indices, or for AI parts (all grey) and in-betweens
        # from their colours
        b = ms.load_bgf(bgf)
        sc = cell_scale(bgf, monster_bgfs)
        if ms.is_custom(bgf):
            cells = {i: up.get(bgf, i, 0) for i in sorted(idxs)}
            ids = {i: np.full((cells[i].height // SCALE + 1, cells[i].width // SCALE + 1), 3, np.int8) for i in cells}
        else:
            cells = {i: cell_image(bgf, i, sc) for i in sorted(idxs)}
            ids = {i: luts.ramp_ids_from_indices(b.bitmaps[i].pixels, b.bitmaps[i].w, b.bitmaps[i].h) for i in cells}
        base = len(b.bitmaps)
        tween_meta = bgf_meta(bgf, tween_keys.get(bgf, []), 0)["bitmaps"][base:] if tween_keys.get(bgf) else []
        for i, (a, bb, k) in enumerate(tween_keys.get(bgf, [])):
            im = tw.get(bgf, a, bb, k, TWEENS, 0)[0]
            if sc != SCALE:   # in-betweens are made at the 4x upscale
                t = tween_meta[i]
                im = im.resize((max(1, round(t["w"] * sc)), max(1, round(t["h"] * sc))), Image.LANCZOS)
            cells[base + i] = im
            ids[base + i] = None
        w, h, rects = pack(cells)
        while max(w, h) > MAX_ATLAS:   # too many frames for one texture: smaller cells
            cells = {i: im.resize((max(1, int(im.width * 0.8)), max(1, int(im.height * 0.8))), Image.LANCZOS)
                     for i, im in cells.items()}
            w, h, rects = pack(cells)
        # player parts (4x, translated at runtime) get the ramp atlas; creatures are never translated
        ramp = sc == SCALE and all(v % SCALE == 0 for r in rects.values() for v in r)
        if sc == SCALE and not ramp:
            print(f"  WARNING {bgf}: cells not on the {SCALE}x grid (shrunk to fit): no ramp atlas")
        key = bgf
        tex = f"T_Spr_{key}"
        rtex = f"T_SprRamp_{key}"
        digest = hashlib.sha1(json.dumps([w, h, sorted(rects.items()), ramp, 3]).encode()
                              + b"".join(im.tobytes() for _, im in sorted(cells.items()))).hexdigest()[:16]
        path = OUT / f"{tex}.png"
        rpath = OUT / f"{rtex}.png"
        stamp = OUT / f"{tex}.sha"
        if not path.exists() or not stamp.exists() or stamp.read_text() != digest or (ramp and not rpath.exists()):
            atlas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            ratlas = np.zeros((h // SCALE, w // SCALE, 4), np.uint8) if ramp else None
            for i, im in cells.items():
                x, y, _, _ = rects[i]
                atlas.paste(coverage(im), (x - PAD, y - PAD))
                if ramp:
                    rc = luts.ramp_cell(im, ids[i], SCALE)
                    ratlas[y // SCALE:y // SCALE + rc.shape[0], x // SCALE:x // SCALE + rc.shape[1]] = rc
            atlas.save(path)
            if ramp:
                Image.fromarray(ratlas, "RGBA").save(rpath)
            stamp.write_text(digest)
            print(f"  wrote {path.name} {w}x{h} ({len(cells)} cells){' + ramp' if ramp else ''}")
        atlases[key] = {"texture": tex, "bgf": bgf, "w": w, "h": h, "hash": digest,
                        "cells": {str(i): r for i, r in sorted(rects.items())}}
        if ramp:
            atlases[key]["ramp"] = rtex
        total += w * h * 4 + (w * h // (SCALE * SCALE) * 4 if ramp else 0)
    layout = {
        "_comment": "Generated by tools/sprites/build_player_sprites.py - do not edit. Sprite player data "
                    "(docs/sprites.md): bitmaps in original pixels, atlas cells in texture pixels "
                    f"({SCALE}x upscale). Kod groups are 1-based, 'groups' here are 0-based (client).",
        "version": 3, "scale": SCALE, "upscale": up.config(), "texture_dir": "/Game/Generated/Sprites",
        "square_cm": ms.SQUARE_M * 100.0, "fine_per_square": ms.FINE_PER_SQUARE,
        "bgfs": {b: bgf_meta(b, tween_keys.get(b, []), 0) for b in sorted(need)},
        "material_classes": materials["classes"],
        "atlases": atlases,
        "looks": looks,
        "monsters": monster_table,
    }
    LAYOUT.write_text(json.dumps(layout, indent=1), encoding="utf-8")
    luts.main()   # the palette lookups M_SpriteBody recolours with
    print(f"{len(atlases)} atlases ({total / 2**20:.0f} MB uncompressed), {len(looks)} looks -> {LAYOUT}")


if __name__ == "__main__":
    main()
