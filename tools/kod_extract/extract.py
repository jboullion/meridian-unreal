"""
Extract game data from the Server-104 Kod sources into data/*.json.

    python tools/kod_extract/extract.py [--kod ReferenceServers/Server-104/kod] [--out data]

Outputs:
    zones.json     demo zones (rooms): exits, edge exits, placed objects, spawns, generators
    monsters.json  every monster/NPC class referenced by the demo zones (+ shop lists)
    spells.json    all spells (SID, school, level, mana, reagents, ...)
    skills.json    all skills (SKID, school, level, ...)
    items.json     weapons, armour and every item class referenced by shops/zones
    constants.json the constant tables the game code needs (SS_*, SKS_*, AI_*, ATCK_*, ...)
    charinfo.json  what the character creator offers (face parts, colours, spells, skills, costs)
    net/rooms.json every room: RID, class, .roo file, name and the rooms its exits lead to (the
                   online client builds rooms we haven't converted at runtime and preloads their
                   neighbours: docs/adr/0012-client-parity-and-world-coverage.md)
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from kodparse import KodIndex  # noqa: E402
from server104 import SERVER104  # noqa: E402  (ReferenceServers/Server-104)

# Raza demo: town, interiors, mausoleum, outskirts, western Farol.
DEMO_RIDS = [300, 301, 302, 303, 304, 305, 306, 307, 308, 330, 331, 332, 333]

# Kod angles: 0..4096 (MAX_ANGLE), 0 = east, increasing toward south, i.e. measured from
# ROO +X toward ROO +Y (rows grow southward).  yaw_deg = angle * 360 / 4096.


# ----------------------------------------------------------------- text utils

def split_top(s: str, sep: str = ",") -> list[str]:
    """Split on `sep` at nesting depth 0 (respects (), [], {} and strings)."""
    out, depth, buf, in_str, i = [], 0, [], False, 0
    while i < len(s):
        c = s[i]
        if in_str:
            buf.append(c)
            if c == "\\" and i + 1 < len(s):
                buf.append(s[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            buf.append(c)
        elif c in "([{":
            depth += 1
            buf.append(c)
        elif c in ")]}":
            depth -= 1
            buf.append(c)
        elif c == sep and depth == 0:
            out.append("".join(buf).strip())
            buf = []
        else:
            buf.append(c)
        i += 1
    tail = "".join(buf).strip()
    if tail:
        out.append(tail)
    return out


def matching(s: str, open_idx: int) -> int:
    """Index of the bracket closing the one at open_idx."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    o, c = s[open_idx], pairs[s[open_idx]]
    depth, i, in_str = 0, open_idx, False
    while i < len(s):
        ch = s[i]
        if in_str:
            if ch == "\\":
                i += 2
                continue
            if ch == '"':
                in_str = False
        elif ch == '"':
            in_str = True
        elif ch == o:
            depth += 1
        elif ch == c:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def call_args(s: str, func_start: int) -> tuple[list[str], int]:
    """Given index of a function name followed by '(', return (args, end_index)."""
    p = s.index("(", func_start)
    e = matching(s, p)
    return split_top(s[p + 1:e]), e


def named_args(args: list[str]) -> dict[str, str]:
    out = {}
    for a in args:
        m = re.match(r"#(\w+)\s*=\s*(.*)$", a, re.S)
        if m:
            out[m.group(1)] = m.group(2).strip()
    return out


def parse_list_literal(s: str) -> list | str:
    """Kod list literal '[a, [b, c], $]' -> nested python lists of raw strings."""
    s = s.strip()
    if s.startswith("[") and matching(s, 0) == len(s) - 1:
        return [parse_list_literal(x) for x in split_top(s[1:-1])]
    return s


# ---------------------------------------------------------------- extractors

class Extractor:
    def __init__(self, kod_root: Path):
        self.idx = KodIndex(kod_root)
        self.kod_root = kod_root
        self.referenced_classes: set[str] = set()

    # value helpers -----------------------------------------------------------
    def val(self, cls: str, expr: str):
        expr = expr.strip()
        if expr.startswith("&"):
            name = self.idx.canon(expr[1:]) or expr[1:]
            self.referenced_classes.add(name)
            return {"class": name}
        if expr.startswith("Create("):
            return self.create_expr(cls, expr)
        if expr.startswith("["):
            lit = parse_list_literal(expr)
            return self._val_tree(cls, lit)
        return self.idx.eval_in(cls, expr)

    def _val_tree(self, cls, node):
        if isinstance(node, list):
            return [self._val_tree(cls, x) for x in node]
        return self.val(cls, node)

    def create_expr(self, cls: str, expr: str) -> dict:
        args, _ = call_args(expr, 0)
        target = args[0].lstrip("&").strip()
        target = self.idx.canon(target) or target
        self.referenced_classes.add(target)
        params = {k: self.val(cls, v) for k, v in named_args(args[1:]).items()}
        return {"class": target, "params": params} if params else {"class": target}

    def body(self, cls: str, msg: str) -> str:
        hit = self.idx.message(cls, msg)
        return hit[0] if hit else ""

    def own_body(self, cls: str, msg: str) -> str:
        return KodIndex._ci_get(self.idx.classes[cls].messages, msg) or ""

    def record(self, cls: str, base: str, keys: list[str]) -> dict:
        """Curated keys (resolved over full chain) + every var overridden below `base`."""
        chain = self.idx.chain(cls)
        resolved = self.idx.resolved_vars(cls)
        rec = {"class": cls, "parents": [c.name for c in chain[1:]],
               "file": chain[0].path.relative_to(self.kod_root.parent.parent).as_posix()}
        for k in keys:
            if k in resolved:
                rec[k] = resolved[k]
        overrides = {}
        taken = {k.lower() for k in rec}
        for c in chain:
            if c.name.lower() == base.lower():
                break
            for k in list(c.classvars) + list(c.properties):
                if k.lower() not in taken:
                    taken.add(k.lower())
                    overrides[k] = resolved.get(k)
        rec["vars"] = overrides
        return rec

    # zones -------------------------------------------------------------------
    def room_class_for(self, rid: int) -> str | None:
        hits = [n for n, c in self.idx.classes.items()
                if KodIndex._ci_get(c.properties, "piRoom_num") is not None
                and self.idx.eval_in(n, KodIndex._ci_get(c.properties, "piRoom_num")) == rid]
        # prefer the most derived class
        hits.sort(key=lambda n: -len(self.idx.chain(n)))
        return hits[0] if hits else None

    def parse_exits(self, cls: str) -> tuple[list, list]:
        body = self.body(cls, "CreateStandardExits")
        exits, edges = [], []
        for m in re.finditer(r"Cons\s*\(", body, re.I):
            args, _ = call_args(body, m.start())
            if len(args) != 2:
                continue
            target = args[1].strip().lower()
            items = parse_list_literal(args[0])
            if not isinstance(items, list):
                continue
            vals = [self.val(cls, x) if isinstance(x, str) else x for x in items]
            if target == "plexits":
                if len(vals) >= 3 and items[2] == "ROOM_LOCKED_DOOR":
                    exits.append({"row": vals[0], "col": vals[1], "locked": True,
                                  "message": vals[3] if len(vals) > 3 else None})
                elif len(vals) >= 5:
                    exits.append({"row": vals[0], "col": vals[1], "dest_rid": vals[2],
                                  "dest_row": vals[3], "dest_col": vals[4],
                                  "rotate": items[5] if len(items) > 5 else None})
            elif target == "pledge_exits":
                edges.append({"side": items[0], "dest_rid": vals[1], "dest_row": vals[2],
                              "dest_col": vals[3], "rotate": items[4] if len(items) > 4 else None})
        # dedupe (Kod files sometimes list the same square twice)
        seen, uniq = set(), []
        for e in exits:
            key = json.dumps(e, sort_keys=True)
            if key not in seen:
                seen.add(key)
                uniq.append(e)
        return uniq, edges

    def parse_placements(self, cls: str, msg: str) -> list[dict]:
        body = self.own_body(cls, msg)
        if not body:
            return []
        vars_: dict[str, dict] = {}
        out = []
        for stmt in split_top(body, ";"):
            m = re.match(r"^\s*(\w+)\s*=\s*Create\s*\(", stmt)
            if m:
                vars_[m.group(1)] = self.create_expr(cls, stmt[stmt.index("Create"):])
            for hm in re.finditer(r"Send\s*\(\s*self\s*,\s*@NewHold\b", stmt, re.I):
                args, _ = call_args(stmt, hm.start())
                na = named_args(args[2:])
                what = na.get("what", "")
                if what.startswith("Create"):
                    obj = self.create_expr(cls, what)
                elif what in vars_:
                    obj = vars_[what]
                else:
                    obj = {"expr": what}
                place = {k: self.val(cls, na[k]) for k in
                         ("new_row", "new_col", "fine_row", "fine_col", "new_angle") if k in na}
                out.append({**obj, **{k.replace("new_", ""): v for k, v in place.items()}})
        return out

    def parse_monsters_list(self, cls: str) -> list:
        for c in self.idx.chain(cls):
            for body in c.messages.values():
                m = re.search(r"plMonsters\s*=\s*\[", body)
                if m:
                    lst = body[m.end() - 1: matching(body, m.end() - 1) + 1]
                    out = []
                    for entry in parse_list_literal(lst):
                        if isinstance(entry, list) and entry:
                            name = entry[0].lstrip("&")
                            name = self.idx.canon(name) or name
                            self.referenced_classes.add(name)
                            out.append({"class": name, "weight": self.val(cls, entry[1]) if len(entry) > 1 else None})
                    return out
        return []

    def parse_generators(self, cls: str) -> list:
        body = self.own_body(cls, "SetMonsterGenPoints") or self.body(cls, "SetMonsterGenPoints")
        m = re.search(r"plGenerators\s*=\s*\[", body)
        if not m:
            return []
        lst = body[m.end() - 1: matching(body, m.end() - 1) + 1]
        return [[self.val(cls, x) for x in e] for e in parse_list_literal(lst) if isinstance(e, list)]

    def flag_rids(self) -> set[int]:
        """The rooms the territory game puts a flagpole in (util/factgame/territry.kod Recreate:
        plFlagRIDs, then the main towns consed on). The pole stands at the room's viFlag_row /
        viFlag_col, else its teleport point (CreateOneFlagpole)."""
        path = self.kod_root / "util" / "factgame" / "territry.kod"
        if not path.exists():
            return set()
        text = path.read_text(encoding="latin-1")
        names: list[str] = []
        m = re.search(r"plFlagRIDs\s*=\s*\[", text)
        if m:
            names += re.findall(r"RID_\w+", text[m.end() - 1: matching(text, m.end() - 1) + 1])
            f = re.search(r"foreach\s+i\s+in\s*\[", text[m.end():])
            if f:
                start = m.end() + f.end() - 1
                names += re.findall(r"RID_\w+", text[start: matching(text, start) + 1])
        return {v for v in (self.idx.consts.value(n) for n in names) if isinstance(v, int)}

    def zones(self) -> list[dict]:
        out = []
        flag_rids = self.flag_rids()
        for rid in DEMO_RIDS:
            cls = self.room_class_for(rid)
            if not cls:
                print(f"warning: no room class for RID {rid}", file=sys.stderr)
                continue
            r = self.idx.resolved_vars(cls)
            exits, edges = self.parse_exits(cls)
            flags = r.get("viPermanent_flags") or 0
            objects = self.parse_placements(cls, "CreateStandardObjects")
            # some rooms (e.g. the crypt) also place things in Constructed / FirstUserEntered
            for extra in ("Constructed", "CreateStaticObjects"):
                objects += self.parse_placements(cls, extra)
            if rid in flag_rids:
                # the faction flagpole (flag.kod): created by the territory game, not the room, so
                # placed here by hand; its flag is the server's overlay, drawn as the original's sprite
                row = r.get("viFlag_row") if r.get("viFlag_row") is not None else r.get("viTeleport_row")
                col = r.get("viFlag_col") if r.get("viFlag_col") is not None else r.get("viTeleport_col")
                if isinstance(row, int) and isinstance(col, int):
                    objects.append({"class": "Flagpole", "row": row, "col": col, "angle": 0, "by": "territry.kod CreateOneFlagpole"})
            out.append({
                "rid": rid,
                "class": cls,
                "parents": [c.name for c in self.idx.chain(cls)[1:]],
                "name": r.get("vrName"),
                "roo": r.get("prRoom"),
                "music": r.get("prMusic"),
                "flags": self.idx.flag_names(flags, "ROOM_") if isinstance(flags, int) else flags,
                "terrain": self.idx.flag_names(r["viTerrain_type"], "TERRAIN_") if isinstance(r.get("viTerrain_type"), int) else None,
                "teleport": {"row": r.get("viTeleport_row"), "col": r.get("viTeleport_col"),
                             "angle": r.get("viTeleport_angle")},
                "base_light": r.get("piBaseLight"),
                "outside_factor": r.get("piOutside_factor"),
                "info": r.get("vrInfo"),
                "exits": exits,
                "edge_exits": edges,
                "spawning": {
                    "monsters": self.parse_monsters_list(cls),
                    "generators": self.parse_generators(cls),
                    "gen_time_ms": r.get("piGen_time"),
                    "gen_percent": r.get("piGen_percent"),
                    "init_count_min": r.get("piInit_count_min"),
                    "init_count_max": r.get("piInit_count_max"),
                    "monster_count_max": r.get("piMonster_count_max"),
                } if self.idx.is_a(cls, "MonsterRoom") else None,
                "objects": objects,
                "file": self.idx.classes[cls].path.relative_to(self.kod_root.parent.parent).as_posix(),
            })
        return out

    def rooms(self) -> list[dict]:
        """Every room class with its own RID (the most derived class per RID), for the online client."""
        by_rid: dict[int, str] = {}
        for name, c in self.idx.classes.items():
            expr = KodIndex._ci_get(c.properties, "piRoom_num")
            if expr is None:
                continue
            rid = self.idx.eval_in(name, expr)
            if not isinstance(rid, int) or rid <= 0:
                continue
            if rid not in by_rid or len(self.idx.chain(name)) > len(self.idx.chain(by_rid[rid])):
                by_rid[rid] = name
        out = []
        for rid in sorted(by_rid):
            cls = by_rid[rid]
            r = self.idx.resolved_vars(cls)
            roo = r.get("prRoom")
            if not isinstance(roo, str) or not roo.lower().endswith(".roo"):
                continue
            try:
                exits, edges = self.parse_exits(cls)
            except Exception as e:  # a room whose exits we can't read still gets its entry
                print(f"warning: exits of {cls}: {e}", file=sys.stderr)
                exits, edges = [], []
            links = sorted({e["dest_rid"] for e in exits + edges
                            if isinstance(e.get("dest_rid"), int) and e["dest_rid"] != rid})
            name = r.get("vrName")
            out.append({"rid": rid, "class": cls, "roo": roo, "name": name if isinstance(name, str) else None,
                        "links": links})
        return out

    # monsters / npcs -----------------------------------------------------------
    MONSTER_KEYS = ["vrName", "vrDesc", "vrIcon", "vrDead_icon", "viLevel", "viDifficulty", "viKarma",
                    "viSpeed", "viAttack_type", "viDefault_behavior", "viTreasure_type", "viCashmin",
                    "viCashmax", "viVisionDistance", "viAttackRange", "viSpellChance", "vbIsUndead",
                    "viOccupation", "viAttributes", "viGender", "viMerchant_markup", "vrSound_hit",
                    "vrSound_miss", "vrSound_aware", "vrSound_death"]

    def monster(self, cls: str) -> dict:
        rec = self.record(cls, "Monster", self.MONSTER_KEYS)
        r = self.idx.resolved_vars(cls)
        if isinstance(r.get("viDefault_behavior"), int):
            rec["behavior_flags"] = self.idx.flag_names(r["viDefault_behavior"], "AI_")
        if isinstance(r.get("viAttack_type"), int):
            rec["attack_flags"] = self.idx.flag_names(r["viAttack_type"], "ATCK_")
        rec["is_npc"] = self.idx.is_a(cls, "Towns")
        body = self.body(cls, "SetForSale")
        m = re.search(r"plFor_sale\s*=\s*\[", body)
        if m:
            lst = parse_list_literal(body[m.end() - 1: matching(body, m.end() - 1) + 1])
            labels = ["items", "skills", "spells", "coft"]
            shop = {}
            for i, part in enumerate(lst if isinstance(lst, list) else []):
                if isinstance(part, list) and i < len(labels):
                    vals = []
                    for x in part:
                        v = self.val(cls, x) if isinstance(x, str) else x
                        vals.append({"id": v, "const": x} if isinstance(v, int) else v)
                    shop[labels[i]] = vals
            rec["for_sale"] = shop
        return rec

    def npc_roles(self) -> list[dict]:
        """Every monster that trades, banks or keeps a vault (monster.kod MobIsBuyer... read viAttributes),
        by its name: the online client offers its Look dialog's Buy, Sell, Vault and Bank buttons by these."""
        roles = {"MOB_BUYER": "buyer", "MOB_SELLER": "seller", "MOB_BANKER": "banker", "MOB_VAULTMAN": "vaultman"}
        out = []
        for cls in self.idx.subclasses("Monster"):
            r = self.idx.resolved_vars(cls)
            name, attrs = r.get("vrName"), r.get("viAttributes")
            if not isinstance(name, str) or not isinstance(attrs, int):
                continue
            got = [roles[f] for f in self.idx.flag_names(attrs, "MOB_") if f in roles]
            if got:
                out.append({"name": name, "class": cls, "roles": got})
        return sorted(out, key=lambda n: (n["name"].lower(), n["class"]))

    # spells / skills ----------------------------------------------------------
    SPELL_KEYS = ["vrName", "vrDesc", "vrIcon", "vrSpell_intro", "viSpell_num", "viSchool", "viSpell_level",
                  "viMana", "viSpellExertion", "viChance_To_Increase", "viMeditate_ratio", "viCast_time",
                  "viPostCast_time", "viHarmful", "viOutlaw", "viPersonal_ench", "vbIsAreaEffect",
                  "viDefensive", "viOffensive", "viResistanceType", "viFlash", "vrSucceed_wav"]

    def reagents(self, cls: str) -> list:
        body = self.body(cls, "ResetReagents")
        out = []
        for m in re.finditer(r"Cons\s*\(\s*\[\s*&(\w+)\s*,\s*([^\]]+)\]", body, re.I):
            name = self.idx.canon(m.group(1)) or m.group(1)
            self.referenced_classes.add(name)
            out.append({"class": name, "count": self.val(cls, m.group(2))})
        return out

    def spells(self) -> list[dict]:
        out = []
        for cls in self.idx.subclasses("Spell"):
            r = self.idx.resolved_vars(cls)
            if not isinstance(r.get("viSpell_num"), int):
                continue
            rec = self.record(cls, "Spell", self.SPELL_KEYS)
            rec["reagents"] = self.reagents(cls)
            rec["category"] = [c.name for c in self.idx.chain(cls)[1:-2]]
            out.append(rec)
        return sorted(out, key=lambda s: s["viSpell_num"])

    SKILL_KEYS = ["vrName", "vrDesc", "vrIcon", "vrSkill_intro", "viSkill_num", "viSchool", "viSkill_level",
                  "viChance_to_Increase", "viMeditate_ratio", "viskillExertion", "vbAutomatic", "vbIsAreaEffect"]

    def skills(self) -> list[dict]:
        out = []
        for cls in self.idx.subclasses("Skill"):
            r = self.idx.resolved_vars(cls)
            if not isinstance(r.get("viSkill_num"), int):
                continue
            out.append(self.record(cls, "Skill", self.SKILL_KEYS))
        return sorted(out, key=lambda s: s["viSkill_num"])

    # equipment on players -----------------------------------------------------
    # blakston.khd HS_*: where an item's overlay attaches (shield.kod, weapon.kod, bow.kod, helmet.kod);
    # a bow's top goes in the left hand (HS_TOP_BOW = HS_LEFT_WEAPON), its bottom on HS_BOTTOM_BOW
    EQUIPMENT_VARS = [("vrWeapon_overlay", "weapon", 22), ("vrShield_overlay", "shield", 32),
                      ("prBowTop", "shield", 32), ("prBowBottom", "bow", 33)]
    # player part bitmaps by name: bt? torso, bl? / br? arms, bf? legs; a, c, e... male, b, d, f... female
    PART_RE = re.compile(r"^b([tlrf])([a-z])\.bgf$", re.I)
    PART_KIND = {"t": ("body", 0), "l": ("left_arm", 31), "r": ("right_arm", 21), "f": ("legs", 41)}

    def equipment(self) -> dict:
        """Every bitmap an item can put on a player (docs/adr/0012 M2b): torsos (shirts, armour, robes:
        SetPlayerIcon), arms (shirts, gauntlets: SetPlayerArms) and legs (pants, robes: SetPlayerLegs)
        by their names in the items' resources, and the overlays items send (SendOverlayInformation):
        weapons, shields, bows, helmets and hats. Plus each weapon's and shield's first-person window
        overlay, and the weapons' groups (weapon.kod SendWindowOverlayAnimation)."""
        parts: dict[str, dict] = {}
        first_person: dict[str, dict] = {}

        def add(bgf, kind, hotspot, cls, gender=None):
            if not isinstance(bgf, str) or not bgf.strip().lower().endswith(".bgf"):
                return
            name = bgf.strip().lower()[:-4]
            p = parts.setdefault(name, {"kind": kind, "hotspot": hotspot, "items": []})
            if gender:
                p["gender"] = gender
            if cls not in p["items"]:
                p["items"].append(cls)

        for cls in sorted(self.idx.classes):
            if not self.idx.is_a(cls, "Item"):
                continue
            for c in self.idx.chain(cls):
                for v in c.resources.values():
                    m = self.PART_RE.match(v.strip()) if isinstance(v, str) else None
                    if m:
                        kind, hs = self.PART_KIND[m.group(1).lower()]
                        add(v, kind, hs, cls, "male" if (ord(m.group(2).lower()) - 97) % 2 == 0 else "female")
            r = self.idx.resolved_vars(cls)
            for var, kind, hs in self.EQUIPMENT_VARS:
                add(KodIndex._ci_get(r, var), kind, hs, cls)
            if self.idx.is_a(cls, "Helmet"):
                # masks send one per gender (mask.kod GetOverlay: vrMaleIcon / vrFemaleIcon)
                male, female = KodIndex._ci_get(r, "vrMaleIcon"), KodIndex._ci_get(r, "vrFemaleIcon")
                if isinstance(male, str) or isinstance(female, str):
                    add(male, "helmet", 13, cls, "male")
                    add(female, "helmet", 13, cls, "female")
                else:
                    add(KodIndex._ci_get(r, "vrIcon"), "helmet", 13, cls)
            # first person, keyed by the third-person overlay (the offline swing; online the server sends them)
            third = KodIndex._ci_get(r, "vrWeapon_overlay")
            window = KodIndex._ci_get(r, "vrWeapon_window_overlay")
            if isinstance(third, str) and isinstance(window, str) and window.strip().lower().endswith(".bgf"):
                g = [KodIndex._ci_get(r, f"vrWeapon_window_{k}") for k in ("attack_start", "attack_end", "hold")]
                low, high, hold = (v if isinstance(v, int) else d for v, d in zip(g, (1, 4, 5)))
                first_person.setdefault(third.strip().lower()[:-4], {
                    "bgf": window.strip().lower()[:-4], "hold": hold,
                    "attack": {"mode": "once", "period": 150, "low": low, "high": high, "final": hold}})
                add(window, "first_person", 5, cls)   # HS_SE
            add(KodIndex._ci_get(r, "vrShield_window_overlay"), "first_person", 7, cls)   # HS_SW
        return {"parts": dict(sorted(parts.items())), "first_person": dict(sorted(first_person.items()))}

    # character creation ------------------------------------------------------
    def charinfo(self, spells: list[dict], skills: list[dict]) -> dict:
        """What the System object's SendCharInfo offers a new character (system.kod): the hair and
        skin translations, the face parts per gender (GetAllowed*Icons, bgf names; "blank" = bald),
        and the spells and skills with their creation cost. The server sends all of this in
        BP_CHARINFO; this copy builds every face part's sprites and is the creator's offline stand-in."""
        sys_cls = self.idx.canon("System")
        send = self.body(sys_cls, "SendCharInfo")

        def packets(text: str) -> list[int]:
            vals = []
            for m in re.finditer(r"AddPacket\s*\(([^;]*)\)\s*;", text, re.I):
                args = [a.strip() for a in split_top(m.group(1))]
                for size, v in zip(args[0::2], args[1::2]):
                    vals.append(self.idx.eval_in(sys_cls, v))
            return vals
        # BP_CHARINFO, the hair count and translations, then the skin count and translations
        vals = packets(send[:send.lower().index("addfaceiconstopacket")])
        nh = vals[1]
        hair, skin = vals[2:2 + nh], vals[3 + nh:3 + nh + vals[2 + nh]]

        def icons(msg: str) -> dict[str, list[str]]:
            body = self.body(sys_cls, msg)
            lists = [re.findall(r"\w+", m) for m in re.findall(r"return\s*\[([^\]]*)\]", body, re.I)]
            name = lambda r: (self.idx.resource(sys_cls, r) or r).rsplit(".", 1)[0].lower()  # noqa: E731
            return {"male": [name(r) for r in lists[0]], "female": [name(r) for r in lists[1]]}
        parts = {k: icons(m) for k, m in (("hair", "GetAllowedHairIcons"), ("head", "GetAllowedHeadIcons"),
                                           ("eyes", "GetAllowedEyeIcons"), ("nose", "GetAllowedNoseIcons"),
                                           ("mouth", "GetAllowedMouthIcons"))}
        faces = {g: {k: v[g] for k, v in parts.items()} for g in ("male", "female")}

        def offered_spell(s: dict) -> bool:
            # Spell::OfferToNewCharacters (spell.kod): enabled, level <= 2, Shal'ille/Qor/Kraanan/Faren;
            # every override (fade, cloak, the walls, ...) returns FALSE. pbEnabled/pbAccessible are
            # the server's state: taken as true. SendCharInfo also keeps schools 1..6 only.
            hit = self.idx.message(s["class"], "OfferToNewCharacters")
            if hit and hit[1].name.lower() != "spell":
                return False
            return s.get("viSpell_level", 9) <= 2 and s.get("viSchool") in (1, 2, 3, 4)
        cost = lambda level: 25 if level == 2 else 10  # noqa: E731
        new_spells = [{"num": s["viSpell_num"], "class": s["class"], "name": s.get("vrName"),
                       "desc": s.get("vrSpell_intro"), "cost": cost(s.get("viSpell_level")),
                       "school": s.get("viSchool"), "level": s.get("viSpell_level")}
                      for s in spells if offered_spell(s)]
        # SendCharInfo lists every skill of level <= 2 (not OfferToNewCharacters); the server only
        # creates leaf classes (WeaponProficiency, UnarmedCombat... are bases of the real skills)
        bases = {p.lower() for s in skills for p in s["parents"]}
        new_skills = [{"num": s["viSkill_num"], "class": s["class"], "name": s.get("vrName"),
                       "desc": s.get("vrSkill_intro"), "cost": cost(s.get("viSkill_level")),
                       "school": s.get("viSchool"), "level": s.get("viSkill_level")}
                      for s in skills if (s.get("viSkill_level") or 9) <= 2 and s["class"].lower() not in bases]
        return {"_comment": "Generated by tools/kod_extract/extract.py from system.kod SendCharInfo and "
                            "GetAllowed*Icons - do not edit. Face parts are bgf names ('blank' = bald). The "
                            "spell list assumes every spell is enabled; the server's own BP_CHARINFO is the truth.",
                "hair_xlats": hair, "skin_xlats": skin, "faces": faces,
                "spells": new_spells, "skills": new_skills}

    # items -------------------------------------------------------------------
    ITEM_KEYS = ["vrName", "vrDesc", "vrIcon", "viValue_average", "viWeight", "viBulk", "viItem_type",
                 "viUse_type", "viWeaponType", "viWeaponQuality", "viProficiency_needed", "viRange",
                 "viHits_init_min", "viHits_init_max", "viDefense_base", "viDamage_base", "viSpell_modifier",
                 "viGround_group", "viInventory_group", "viBroken_group"]

    def items(self) -> list[dict]:
        names = set(self.idx.subclasses("Weapon")) | set(self.idx.subclasses("DefenseModifier"))
        # worn jewellery (the UI's ring and amulet slots)
        names |= set(self.idx.subclasses("Ring")) | set(self.idx.subclasses("Necklace"))
        names |= {n for n in self.referenced_classes if n in self.idx.classes and self.idx.is_a(n, "Item")}
        out = []
        for cls in sorted(names):
            base = "Weapon" if self.idx.is_a(cls, "Weapon") else (
                "DefenseModifier" if self.idx.is_a(cls, "DefenseModifier") else "Item")
            rec = self.record(cls, base, self.ITEM_KEYS)
            rec["kind"] = base
            out.append(rec)
        return out

    # constants ---------------------------------------------------------------
    def constants(self) -> dict:
        prefixes = ["SS_", "SKS_", "SID_", "SKID_", "AI_", "ATCK_", "ROOM_", "TERRAIN_", "WEAPON_TYPE_",
                    "WEAPON_QUALITY_", "SPEED_", "TID_", "RID_", "STATE_", "MAX_", "LIGHT_", "FINENESS",
                    "LEAVE_", "ANGLE_", "OO_"]
        return {p.rstrip("_"): dict(sorted(self.idx.const_names(p).items(), key=lambda kv: kv[1]))
                for p in prefixes}


def main() -> None:
    ap = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[2]
    ap.add_argument("--kod", type=Path, default=SERVER104 / "kod")
    ap.add_argument("--out", type=Path, default=root / "data")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)

    ex = Extractor(a.kod)
    zones = ex.zones()

    # monsters & NPCs referenced by the zones
    mon_names = set()
    for z in zones:
        for o in z["objects"]:
            c = o.get("class")
            if c in ex.idx.classes and ex.idx.is_a(c, "Monster"):
                mon_names.add(c)
        for s in (z["spawning"] or {}).get("monsters", []):
            mon_names.add(s["class"])
    monsters = [ex.monster(n) for n in sorted(mon_names) if n in ex.idx.classes]

    spells = ex.spells()
    skills = ex.skills()
    items = ex.items()

    def dump(name, obj):
        p = a.out / name
        p.write_text(json.dumps(obj, indent=1, ensure_ascii=False), encoding="utf-8")
        print(f"wrote {p.relative_to(root)}  ({len(obj) if isinstance(obj, list) else len(obj.keys())} entries)")

    dump("zones.json", zones)
    dump("monsters.json", monsters)
    dump("spells.json", spells)
    dump("skills.json", skills)
    dump("items.json", items)
    dump("constants.json", ex.constants())
    dump("charinfo.json", ex.charinfo(spells, skills))
    (a.out / "sprites").mkdir(exist_ok=True)
    dump("sprites/equipment.json", {"_doc": "Generated by tools/kod_extract/extract.py - do not edit. Every bitmap an "
                                            "item puts on a player, by bgf: kind (body, left_arm, right_arm, legs, "
                                            "weapon, shield, bow, helmet, first_person), the hotspot it attaches to "
                                            "(blakston.khd HS_*; first person: the screen corner), gender for body "
                                            "parts, and the item classes. 'first_person': each weapon's window overlay "
                                            "by its third-person overlay. tools/sprites/build_player_sprites.py "
                                            "converts them (docs/adr/0012-client-parity-and-world-coverage.md M2b).",
                                    **ex.equipment()})
    (a.out / "net").mkdir(exist_ok=True)
    dump("net/rooms.json", {"_doc": "Every Kod room (tools/kod_extract): rid, class, roo file, name, and the rooms its "
                                    "exits lead to. The online client uses it for rooms it builds at runtime "
                                    "(docs/adr/0012-client-parity-and-world-coverage.md).",
                            "rooms": ex.rooms()})
    dump("net/npcs.json", {"_doc": "Every NPC that buys, sells, banks or keeps a vault (tools/kod_extract, from Kod's "
                                   "viAttributes MOB_BUYER, MOB_SELLER, MOB_BANKER, MOB_VAULTMAN), by name. The online "
                                   "client shows the Look dialog's Buy, Sell, Vault and Bank buttons by these "
                                   "(docs/adr/0012-client-parity-and-world-coverage.md M6).",
                           "npcs": ex.npc_roles()})


if __name__ == "__main__":
    main()
