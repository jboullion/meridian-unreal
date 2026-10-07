"""
Monsters and NPCs as sprites (docs/sprites.md "Monsters"): their looks and animations, from
data/monsters.json (tools/kod_extract) and their Kod source.

Each monster class's Kod says how it animates (monster/<name>.kod, or a parent class's file):
  SendMoveAnimation  ANIMATE_CYCLE period, low, high       -> "walk"
  SendAnimation      ANIM_ATTACK: ANIMATE_ONCE period, low, high, final -> "attack"
  nothing else       group 1                                -> "stand"
When it dies, the original replaces it with a corpse object drawn from vrDead_icon (one group).
Ranges are clamped to the groups the bgf has (a range past the last group would draw nothing).

    python tools/sprites/monsters.py      # prints what it finds

Used by build_player_sprites.py, which adds "m_<Class>" and "m_<Class>_dead" looks and a
"monsters" table (speed, vision, aggression, NPC, sounds) to data/sprites/player_parts.json.
"""
from __future__ import annotations

import json
import re
from functools import lru_cache
from pathlib import Path

import m59sprites as ms

KOD = ms.bgf2png.SERVER104 / "kod"
# Kod speed units -> cm/s: the client sends the player's walk as 25 and run as 55 in the same units
# (move.c USER_WALKING_SPEED / USER_RUNNING_SPEED); the remaster runs at 450 cm/s
CM_PER_SPEED = 450.0 / 55.0
FINE_PER_SQUARE = 64                    # Kod fineness (distances, viVisionDistance is in squares)


@lru_cache(maxsize=None)
def class_files() -> dict[str, Path]:
    out = {}
    for f in KOD.rglob("*.kod"):
        try:
            text = f.read_text(encoding="latin-1")
        except OSError:
            continue
        for m in re.finditer(r"^(\w+)\s+is\s+(\w+)", text, re.M):
            out.setdefault(m.group(1), f)
    return out


def _handler(text: str, name: str) -> str | None:
    """The body of message handler `name()` in a Kod file (to its closing brace)."""
    m = re.search(r"^\s*%s\s*\([^)]*\)\s*\n\s*\{" % re.escape(name), text, re.M)
    if not m:
        return None
    depth, i = 1, m.end()
    while i < len(text) and depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def _tokens(body: str) -> list[str]:
    """Every AddPacket argument, in order (comments removed)."""
    body = re.sub(r"//[^\n]*", "", body)
    toks = []
    for args in re.findall(r"AddPacket\s*\(([^)]*)\)", body):
        toks += [a.strip() for a in args.split(",") if a.strip()]
    return toks


def _anim(toks: list[str], kind: str) -> dict | None:
    """ANIMATE_CYCLE / ANIMATE_ONCE after its size byte: 4,period, 2,low, 2,high[, 2,final]."""
    for i, t in enumerate(toks):
        if t == "1" and i + 1 < len(toks) and toks[i + 1] == kind:
            v = toks[i + 2:]
            try:
                if kind == "ANIMATE_CYCLE" and len(v) >= 6:
                    return {"mode": "cycle", "period": int(v[1]), "low": int(v[3]), "high": int(v[5])}
                if kind == "ANIMATE_ONCE" and len(v) >= 8:
                    return {"mode": "once", "period": int(v[1]), "low": int(v[3]), "high": int(v[5]), "final": int(v[7])}
            except ValueError:
                return None
    return None


def kod_animations(cls: str, parents: list[str]) -> dict:
    """walk / attack definitions for a class, looking up its parents' files when it has none."""
    files = class_files()
    out = {}
    for c in [cls] + parents:
        f = files.get(c)
        if not f:
            continue
        text = f.read_text(encoding="latin-1")
        if "walk" not in out:
            body = _handler(text, "SendMoveAnimation")
            a = _anim(_tokens(body), "ANIMATE_CYCLE") if body else None
            if a:
                out["walk"] = a
        if "attack" not in out:
            body = _handler(text, "SendAnimation")
            if body and "ANIM_ATTACK" in body:
                part = body[body.index("ANIM_ATTACK"):]
                a = _anim(_tokens(part), "ANIMATE_ONCE")
                if a:
                    out["attack"] = a
        if c == "Monster":
            break
    return out


def clamp(anim: dict, groups: int) -> dict | None:
    """Keep a Kod range inside the bgf's groups (1-based)."""
    a = dict(anim)
    a["high"] = min(a["high"], groups)
    a["low"] = min(a["low"], a["high"])
    if "final" in a:
        a["final"] = min(a["final"], groups)
    return a if groups > 1 else None


def definitions() -> dict[str, dict]:
    """Class -> {bgf, dead_bgf, actions, speed_cms, vision_cm, aggressive, npc, sounds}."""
    defs = {}
    for m in json.loads((ms.ROOT / "data" / "monsters.json").read_text(encoding="utf-8")):
        bgf = (m.get("vrIcon") or "").removesuffix(".bgf").lower()
        if not bgf:
            continue
        try:
            groups = len(ms.load_bgf(bgf).groups)
        except FileNotFoundError:
            print(f"  {m['class']}: {bgf}.bgf not found, skipped")
            continue
        flags = m.get("behavior_flags") or []
        actions = {"stand": {}}
        for name, anim in kod_animations(m["class"], m.get("parents", [])).items():
            c = clamp(anim, groups)
            if c:
                actions[name] = {"body": c}
        dead = (m.get("vrDead_icon") or "").removesuffix(".bgf").lower() or None
        if dead:
            try:
                ms.load_bgf(dead)
            except FileNotFoundError:
                dead = None
        defs[m["class"]] = {
            "name": m.get("vrName"), "bgf": bgf, "dead_bgf": dead, "actions": actions,
            "speed_cms": round((m.get("viSpeed") or 10) * CM_PER_SPEED, 1),
            "vision_cm": round((m.get("viVisionDistance") or 10) * ms.SQUARE_M * 100.0, 1),
            "aggressive": "AI_FIGHT_AGGRESSIVE" in flags,
            "npc": bool(m.get("is_npc")) or "AI_NPC" in flags,
            "stationary": "AI_NOMOVE" in flags,
            "sounds": {k: m.get(v) for k, v in (("aware", "vrSound_aware"), ("hit", "vrSound_hit"), ("miss", "vrSound_miss"),
                                                   ("death", "vrSound_death")) if m.get(v)},
        }
    return defs


if __name__ == "__main__":
    for cls, d in definitions().items():
        print(cls, d["bgf"], d["dead_bgf"], json.dumps(d["actions"]), d["speed_cms"], "aggr" if d["aggressive"] else "",
              "npc" if d["npc"] else "")
