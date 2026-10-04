"""
Extract game data from the Server-104 Kod sources into data/*.json.

    python tools/kod_extract/extract.py [--kod Server-104/kod] [--out data]

Outputs:
    zones.json     demo zones (rooms): exits, edge exits, placed objects, spawns, generators
    monsters.json  every monster/NPC class referenced by the demo zones (+ shop lists)
    spells.json    all spells (SID, school, level, mana, reagents, ...)
    skills.json    all skills (SKID, school, level, ...)
    items.json     weapons, armour and every item class referenced by shops/zones
    constants.json the constant tables the game code needs (SS_*, SKS_*, AI_*, ATCK_*, ...)
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from kodparse import KodIndex  # noqa: E402

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

    def zones(self) -> list[dict]:
        out = []
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

    # items -------------------------------------------------------------------
    ITEM_KEYS = ["vrName", "vrDesc", "vrIcon", "viValue_average", "viWeight", "viBulk", "viItem_type",
                 "viUse_type", "viWeaponType", "viWeaponQuality", "viProficiency_needed", "viRange",
                 "viHits_init_min", "viHits_init_max", "viDefense_base", "viDamage_base", "viSpell_modifier",
                 "viGround_group", "viInventory_group", "viBroken_group"]

    def items(self) -> list[dict]:
        names = set(self.idx.subclasses("Weapon")) | set(self.idx.subclasses("DefenseModifier"))
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
    ap.add_argument("--kod", type=Path, default=root / "Server-104" / "kod")
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


if __name__ == "__main__":
    main()
