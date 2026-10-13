"""
The world remaster's overnight batch build (docs/adr/0003, 2026-10-10): every map of the given passes
still drawn at runtime becomes a built zone with the batch recipe (tools/world/zone_configs.py), its
art, its level, its minimap and two look-dev cameras, ready to be looked at. No look-dev runs.

    python tools/world/overnight.py --passes 3-7                       # the whole run (resumable)
    python tools/world/overnight.py --passes 3 --only-region Jixa      # a trial on one region
    python tools/world/overnight.py --report                           # rewrite the report only
    python tools/world/overnight.py --locked compile -- <command ...>  # run anything under the Unreal lock

Per pass, in order:
  1 rooms      one room id per map (data/world/maps.json, runtime, the pass), the lowest outside the
               Death Realm, added to data/world/build_rids.json (read by tools/kod_extract/extract.py)
  2 data       extract.py, bgf2png.py --textures-for-zones, roo2gltf.py --preview
  3 guard      no two zones' boxes overlap (bar Raza and its Outskirts), no earlier zone moved: else stop
  4 textures   make_placeholders.py, extract_audio.py, zone_textures.py for the new zones
  5 configs    zone_configs.py for the new zones (art config, mood profile, ground floors, cameras)
  6 art        build_zone_art.py --strict per zone (headless Blender), check_overlaps.py on its
               cut-outs (with --overlaps), shelter.py outdoors; a failing zone's config is set aside (zone_<rid>.json.failed)
               so it builds as a plain blockout
  7 world      [Unreal] build_world.ps1 -Headless, retried once after 10 minutes
After the last pass: [Unreal] run_map_capture.ps1 and import_ui.ps1, [Unreal] run_step_survey.ps1 and
run_net_test.ps1 -Render (when the local gateway answers), then build/overnight/report.md and npm run maps.

The Unreal lock (build/locks/unreal.lock) keeps this and other sessions (the props pipeline) from
running Unreal at the same time: before an [Unreal] step it waits until the lock is free and no
UnrealEditor / UnrealMeridian process has run for two checks a minute apart, then holds the lock
until the step ends. State: build/overnight/state.json; logs: build/overnight/logs/.
"""
from __future__ import annotations

import argparse
import collections
import datetime
import glob
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "build", "overnight")
LOGS = os.path.join(OUT, "logs")
STATE = os.path.join(OUT, "state.json")
LOCK = os.path.join(REPO, "build", "locks", "unreal.lock")
BUILD_RIDS = os.path.join(REPO, "data", "world", "build_rids.json")
ENV = os.path.join(REPO, "data", "environment")
BLENDER = os.environ.get("MR_BLENDER", r"H:\Steam\steamapps\common\Blender\blender.exe")
PY = sys.executable
OWNER = "overnight-maps"
# check_overlaps.py on each zone's cut-outs (--overlaps): up to 14 min a big outdoor map (2026-10-10), so off by default
CHECK_OVERLAPS = False


def log(msg):
    line = "[%s] %s" % (datetime.datetime.now().strftime("%H:%M:%S"), msg)
    print(line, flush=True)
    with open(os.path.join(OUT, "overnight.log"), "a", encoding="utf-8") as f:
        f.write(line + "\n")


def load(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    os.replace(tmp, path)


def run(name, cmd, timeout_s, ok=None):
    """Run a command with its output in logs/<name>.log; True when it exits 0 (or ok(log text) says so)."""
    os.makedirs(LOGS, exist_ok=True)
    path = os.path.join(LOGS, re.sub(r"[^\w.-]+", "_", name) + ".log")
    t0 = time.time()
    with open(path, "w", encoding="utf-8", errors="replace") as f:
        try:
            code = subprocess.run(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT, timeout=timeout_s).returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
    text = open(path, encoding="utf-8", errors="replace").read()
    good = ok(text, code) if ok else code == 0
    log("%s: %s (exit %s, %.0f s)" % (name, "ok" if good else "FAILED", code, time.time() - t0))
    return good


def ps(script, *args):
    return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(REPO, "tools", "ue", script), *args]


# ------------------------------------------------------------------------------ the Unreal lock

def unreal_running():
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "Get-Process UnrealEditor*, UnrealMeridian* -ErrorAction SilentlyContinue | Measure-Object | % Count"],
                         capture_output=True, text=True).stdout.strip()
    return out not in ("", "0")


def lock_holder():
    data = load(LOCK)
    if not data:
        return None
    since = data.get("since_epoch", 0)
    if time.time() - since > 8 * 3600 and not unreal_running():
        log("stale lock from %s (%s) removed" % (data.get("owner"), data.get("step")))
        os.remove(LOCK)
        return None
    return data


class UnrealLock:
    def __init__(self, step):
        self.step = step

    def __enter__(self):
        waited = False
        while True:
            holder = lock_holder()
            if holder and holder.get("owner") != OWNER:
                if not waited:
                    log("waiting for the Unreal lock (%s: %s)" % (holder.get("owner"), holder.get("step")))
                    waited = True
                time.sleep(60)
                continue
            if unreal_running():
                time.sleep(60)
                continue
            time.sleep(60)
            if unreal_running() or (lock_holder() or {}).get("owner", OWNER) != OWNER:
                continue
            break
        os.makedirs(os.path.dirname(LOCK), exist_ok=True)
        save(LOCK, {"owner": OWNER, "pid": os.getpid(), "step": self.step,
                    "since": datetime.datetime.now().isoformat(timespec="seconds"), "since_epoch": time.time()})
        return self

    def __exit__(self, *exc):
        if (load(LOCK) or {}).get("owner") == OWNER and os.path.exists(LOCK):
            os.remove(LOCK)


# ------------------------------------------------------------------------------ the steps

def pick_rooms(pass_no, only_region):
    maps = load(os.path.join(REPO, "data", "world", "maps.json"))["maps"]
    maps = maps.values() if isinstance(maps, dict) else maps
    built = {z["rid"] for z in load(os.path.join(REPO, "data", "zone_layout.json"))["zones"]}
    rids, regions = [], {}
    for m in maps:
        if m["pass"] != pass_no or (only_region and m["region"] != only_region) or m.get("keep"):
            continue  # "keep": the historic towns stay the original; unused maps and the Death Realm wait
        rooms = [r for r in m["rooms"] if r.get("rid") is not None]
        if not rooms or any(r["rid"] in built for r in rooms):
            continue
        living = [r for r in rooms if "deathrealm" not in (r.get("folder") or "")] or rooms
        rid = min(r["rid"] for r in living)
        rids.append(rid)
        regions[rid] = (m["roo"], m["region"])
    return rids, regions


def add_build_rids(pass_no, rids):
    data = load(BUILD_RIDS, {"_doc": "Rooms the world remaster's batch builds add to the built zones (tools/world/overnight.py; "
                                     "read by tools/kod_extract/extract.py with its DEMO_RIDS), per pass.", "passes": {}})
    have = data["passes"].setdefault(str(pass_no), [])
    have += [r for r in rids if r not in have]
    save(BUILD_RIDS, data)


def boxes():
    out = {}
    for z in load(os.path.join(REPO, "data", "zone_layout.json"))["zones"]:
        o, b = z["world_origin_cm"], z["bounds_m"]
        out[z["rid"]] = (o, (o[0] + 100 * b["min"][0], o[1] + 100 * b["min"][2], o[0] + 100 * b["max"][0], o[1] + 100 * b["max"][2]))
    return out


def guard(before):
    now = boxes()
    moved = [r for r, (o, _) in before.items() if r in now and now[r][0] != o]
    items = list(now.items())
    overlaps = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            (ra, (_, a)), (rb, (_, c)) = items[i], items[j]
            if {ra, rb} == {300, 330}:
                continue
            if a[0] < c[2] and c[0] < a[2] and a[1] < c[3] and c[1] < a[3]:
                overlaps.append((ra, rb))
    if moved or overlaps:
        log("GUARD: zones moved %s, overlapping %s" % (moved, overlaps[:20]))
        return False
    log("guard: %d zones, none moved, no overlaps" % len(now))
    return True


def zone_kind(rid):
    prof = (load(os.path.join(ENV, "moods.json"))["zones"]).get(str(rid), {})
    return prof.get("kind", "outdoor")


def zone_art(rid, results, set_aside=True):
    cfg = os.path.join(ENV, "zone_%d.json" % rid)
    if not os.path.exists(cfg):
        results[str(rid)] = {"art": "no config"}
        return
    ok = run("art_%d" % rid, [BLENDER, "-b", "--factory-startup", "-P", "tools/blender/build_zone_art.py", "--",
                              "--rid", str(rid), "--preview", "--strict"], 3600)
    text = open(os.path.join(LOGS, "art_%d.log" % rid), encoding="utf-8", errors="replace").read()
    warnings = [l.strip() for l in text.splitlines() if "WARNING" in l]
    entry = {"art": "ok" if ok else "failed", "warnings": warnings[:10], "kind": zone_kind(rid)}
    if not ok and set_aside:
        os.replace(cfg, cfg + ".failed")  # build it as a plain blockout rather than stop the world build
        entry["reason"] = (text.strip().splitlines() or ["?"])[-1][:300]
    else:
        blend = os.path.join(REPO, "build", "environment", "zone_%d" % rid, "zone_%d_art.blend" % rid)
        if CHECK_OVERLAPS and os.path.exists(os.path.join(REPO, "build", "environment", "zone_%d" % rid, "SM_Z%d_Cutouts.glb" % rid)):
            run("overlaps_%d" % rid, [BLENDER, "-b", blend, "--factory-startup", "-P", "tools/blender/check_overlaps.py",
                                      "--", "SM_Z%d_Cutouts" % rid], 1800)
            m = re.search(r"(\d+) overlapping coplanar pairs",
                          open(os.path.join(LOGS, "overlaps_%d.log" % rid), encoding="utf-8", errors="replace").read())
            entry["overlaps"] = int(m.group(1)) if m else None
        if entry["kind"] == "outdoor":
            run("shelter_%d" % rid, [PY, "tools/environment/shelter.py", str(rid)], 1800)
    results[str(rid)] = entry


def world_build(name):
    """build_world.ps1 until it finishes. A long headless build runs out of video memory (2026-10-11: 35 GB
    used after 81 zones) and dies without a Python error, so build_world.ps1 still says OK: done only when
    build_world.py logged its last line. The build is incremental, so each restart goes on where the last
    one stopped."""
    def ok(text, code):
        return code == 0 and "[build_world] done in" in text
    for attempt in range(1, 9):
        with UnrealLock(name):
            if run("%s_%d" % (name, attempt), ps("build_world.ps1", "-Headless"), 8 * 3600, ok):
                return True
        log("world build stopped before the end; starting it again in a minute (attempt %d)" % (attempt + 1))
        time.sleep(60)
    return False


def gateway_up():
    try:
        with socket.create_connection(("localhost", 8059), timeout=3):
            return True
    except OSError:
        return False


# ------------------------------------------------------------------------------ the report

def missing_props(rids):
    """Objects in these zones with no prop, read-only through tools/aigen/inventory.py (as the Jasper pass did)."""
    sys.path.insert(0, os.path.join(REPO, "tools", "aigen"))
    sys.path.insert(0, os.path.join(REPO, "tools"))
    import inventory as inv
    props = load(os.path.join(ENV, "props.json"))
    cls_keys, types, icons = set(props["classes"]), set(map(str, props["types"])), set(props.get("icons", {}))
    oo = inv.oo_table()
    miss = collections.OrderedDict()
    for z in load(os.path.join(REPO, "data", "zone_layout.json"))["zones"]:
        if z["rid"] not in rids:
            continue
        for o in z.get("objects", []):
            cls, params = o["class"], o.get("params") or {}
            if cls is None:
                key = ("(no class)", "", "")
            elif cls == "OrnamentalObject":
                t = str(int(params.get("type", 0)))
                if t in types or (params.get("icon") or "").lower() in icons:
                    continue
                info = oo.get(int(t), {})
                key = ("OrnamentalObject %s (%s)" % (t, info.get("const")), (info.get("icon") or "").lower(), info.get("name") or "")
            else:
                if cls in cls_keys or any(k.startswith(cls + "/") for k in cls_keys):
                    continue
                try:
                    icon, name, chain = inv.class_icon(cls)
                    kind = inv.kind_of(cls, chain)
                except Exception:
                    icon, name, kind = None, None, "?"
                if kind in ("npc", "monster", "logic", "item"):
                    continue
                key = (cls, (icon or "").lower(), name or "")
            miss.setdefault(key, collections.Counter())[z["rid"]] += 1
    return miss


def game_log_numbers():
    out = []
    for path in sorted(glob.glob(os.path.join(REPO, "game", "UnrealMeridian", "Saved", "Logs", "*.log")), key=os.path.getmtime)[-6:]:
        text = open(path, encoding="utf-8", errors="replace").read()
        loads = re.findall(r"Loaded (\d+) zone levels in ([\d.]+) s", text)
        mem = re.findall(r"Process Physical Memory: ([\d.]+) MB used, ([\d.]+) MB peak", text)
        if loads:
            out.append("%s: %s zone levels in %s s; memory peak %s MB" % (os.path.basename(path), loads[-1][0], loads[-1][1],
                                                                          mem[-1][1] if mem else "?"))
    return out


def report(state):
    lines = ["# Overnight world build", "", "Written by `tools/world/overnight.py` at %s." % datetime.datetime.now().isoformat(timespec="minutes"), ""]
    zones = {z["rid"]: z for z in load(os.path.join(REPO, "data", "zones.json"))}
    all_rids = []
    for p, ps_ in sorted(state.get("passes", {}).items()):
        lines += ["## Pass %s" % p, ""]
        lines += ["Steps: " + ", ".join("%s %s" % (k, v) for k, v in ps_.get("steps", {}).items()), ""]
        art = ps_.get("art", {})
        lines += ["| Zone | Map | Region | Kind | Art | Notes |", "|---|---|---|---|---|---|"]
        for rid in ps_.get("rids", []):
            all_rids.append(rid)
            roo, region = ps_.get("regions", {}).get(str(rid), ["?", "?"])
            e = art.get(str(rid), {})
            notes = []
            if e.get("warnings"):
                notes.append("%d warnings: %s" % (len(e["warnings"]), e["warnings"][0][:120]))
            if e.get("overlaps"):
                notes.append("%d overlapping pairs" % e["overlaps"])
            if e.get("reason"):
                notes.append(e["reason"][:160])
            name = (zones.get(rid) or {}).get("name") or ""
            lines.append("| %d %s | `%s` | %s | %s | %s | %s |" % (rid, name.strip(), roo, region, e.get("kind", ""), e.get("art", "-"),
                                                                    "; ".join(notes).replace("|", "/")))
        lines.append("")
    rb = state.get("reblockout")
    if rb:
        failed = {r: e for r, e in rb.get("art", {}).items() if e.get("art") == "failed"}
        lines += ["## Blockouts rebuilt (--reblockout)", "", "Steps: " + ", ".join("%s %s" % kv for kv in rb.get("steps", {}).items()),
                  "", "Zones rebuilt: %d; art failed: %s" % (len(rb.get("art", {})), ", ".join("%s (%s)" % (r, e.get("reason", "")[:100]) for r, e in failed.items()) or "none"), ""]
        lines += ["- %s: %s" % kv for kv in rb.get("final", {}).items()] + [""]
    lines += ["## After the build", ""]
    for k, v in state.get("final", {}).items():
        lines.append("- %s: %s" % (k, v))
    lines += ["", "### Zone loading (the game logs)", ""] + ["- " + l for l in game_log_numbers()] + [""]
    survey = os.path.join(LOGS, "step_survey.log")
    if os.path.exists(survey):
        text = open(survey, encoding="utf-8", errors="replace").read()
        done = re.findall(r"MRStepSurvey: DONE[^\n]*", text)
        fails = re.findall(r"MRStepSurvey: FAIL[^\n]*", text)
        lines += ["### Step survey", "", "- " + (done[-1] if done else "no DONE line"), ""] + ["  - " + f[:220] for f in fails[:60]] + [""]
    net = os.path.join(LOGS, "net_test.log")
    if os.path.exists(net):
        done = re.findall(r"MRNetTest: DONE[^\n]*", open(net, encoding="utf-8", errors="replace").read())
        lines += ["### Online smoke test", "", "- " + (done[-1] if done else "no DONE line (see logs/net_test.log)"), ""]
    if all_rids:
        try:
            miss = missing_props(set(all_rids))
            lines += ["### Objects with no prop (for the props session)", "", "| Class | Sprite | Name | Count | Zones |", "|---|---|---|---|---|"]
            for (cls, bgf, name), c in sorted(miss.items(), key=lambda kv: -sum(kv[1].values())):
                lines.append("| %s | %s | %s | %d | %s |" % (cls, bgf or "-", name, sum(c.values()),
                                                         ", ".join("%d×%d" % (r, n) if n > 1 else str(r) for r, n in sorted(c.items()))))
            lines.append("")
        except Exception as e:  # the props tool may be mid-edit in the other session
            lines += ["(missing props: %s)" % e, ""]
    with open(os.path.join(OUT, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    log("report: build/overnight/report.md")


# ------------------------------------------------------------------------------ main

def do_pass(state, pass_no, only_region):
    ps_ = state["passes"].setdefault(str(pass_no), {"steps": {}, "rids": [], "regions": {}, "art": {}})
    steps = ps_["steps"]

    def step(name, fn):
        if steps.get(name) == "done":
            return True
        log("pass %d: %s" % (pass_no, name))
        good = fn()
        steps[name] = "done" if good else "failed"
        save(STATE, state)
        return good

    def rooms():
        rids, regions = pick_rooms(pass_no, only_region)
        ps_["rids"] = sorted(set(ps_["rids"]) | set(rids))
        ps_["regions"].update({str(r): v for r, v in regions.items()})
        add_build_rids(pass_no, rids)
        log("pass %d: %d maps" % (pass_no, len(ps_["rids"])))
        return True

    if not step("rooms", rooms) or not ps_["rids"]:
        return True
    rids = ps_["rids"]
    before = {}

    def data():
        before.update(boxes())
        return (run("p%d_extract" % pass_no, [PY, "tools/kod_extract/extract.py"], 3600)
                and run("p%d_bgf2png" % pass_no, [PY, "tools/bgf2png/bgf2png.py", "--textures-for-zones"], 3600)
                and run("p%d_roo2gltf" % pass_no, [PY, "tools/roo2gltf/roo2gltf.py", "--preview"], 4 * 3600))
    if not step("data", data):
        return False
    if not before:
        before = {r: v for r, v in boxes().items() if r not in rids}
    if not step("guard", lambda: guard({r: v for r, v in before.items() if r not in rids})):
        return False
    built = {z["rid"] for z in load(os.path.join(REPO, "data", "zone_layout.json"))["zones"]}
    missing = [r for r in rids if r not in built]
    if missing:
        log("pass %d: not built by roo2gltf (no room class or .roo): %s" % (pass_no, missing))
        ps_["not_built"] = missing
    rids = [r for r in rids if r in built]

    def textures():
        return (run("p%d_placeholders" % pass_no, [PY, "tools/textures/make_placeholders.py"], 4 * 3600)
                and run("p%d_audio" % pass_no, [PY, "tools/audio/extract_audio.py"], 3600)
                and run("p%d_zone_textures" % pass_no, [PY, "tools/environment/zone_textures.py", *map(str, rids)], 3600))
    if not step("textures", textures):
        return False
    if not step("configs", lambda: run("p%d_configs" % pass_no, [PY, "tools/world/zone_configs.py", *map(str, rids)], 4 * 3600)):
        return False

    def art():
        for rid in rids:
            if ps_["art"].get(str(rid), {}).get("art") in ("ok", "failed"):
                continue
            zone_art(rid, ps_["art"])
            save(STATE, state)
        return True
    step("art", art)
    return step("world", lambda: world_build("p%d_world" % pass_no))


def finish(state, final=None):
    final = state.setdefault("final", {}) if final is None else final

    def once(name, fn):
        if final.get(name, "").startswith("done"):
            return
        final[name] = fn()
        save(STATE, state)

    def maps():
        with UnrealLock("map capture"):
            a = run("map_capture", ps("run_map_capture.ps1", "-TimeoutSeconds", "18000"), 6 * 3600)
            b = run("import_ui", ps("import_ui.ps1", "-Headless"), 3 * 3600, lambda t, c: "OK in" in t)
        return "done" if a and b else "failed (map capture %s, import %s)" % (a, b)
    once("minimaps", maps)

    def survey():
        with UnrealLock("step survey"):
            run("step_survey", ps("run_step_survey.ps1"), 6 * 3600, lambda t, c: "MRStepSurvey: DONE" in t)
        m = re.findall(r"MRStepSurvey: DONE[^\n]*", open(os.path.join(LOGS, "step_survey.log"), encoding="utf-8", errors="replace").read())
        return "done: " + (m[-1] if m else "no DONE line")
    once("step survey", survey)

    def net():
        if not gateway_up():
            return "skipped: nothing on ws://localhost:8059"
        with UnrealLock("net test"):
            run("net_test", ps("run_net_test.ps1", "-Render"), 3600, lambda t, c: "MRNetTest: DONE" in t)
        m = re.findall(r"MRNetTest: DONE[^\n]*", open(os.path.join(LOGS, "net_test.log"), encoding="utf-8", errors="replace").read())
        return "done: " + (m[-1] if m else "no DONE line")
    once("net test", net)
    run("maps", ["npm.cmd" if os.name == "nt" else "npm", "run", "maps"], 600)
    run("tags", [PY, "tools/world/zone_tags.py"], 600)  # ground types and biomes for the new zones


UE_BUILD = r"G:/Unreal Engine/UE_5.8/Engine/Build/BatchFiles/Build.bat"
UPROJECT = os.path.join(REPO, "game", "UnrealMeridian", "UnrealMeridian.uproject")


def reblockout(state):
    """Every built zone again from its .roo after a change to the blockout (2026-10-12: untextured
    surfaces left out of the drawn mesh, docs/findings.md): compile (the runtime mesher), roo2gltf,
    the zones' texture lists and art (the art copies the drawn blockout), the world, then finish()."""
    rb = state.setdefault("reblockout", {"steps": {}, "art": {}, "final": {}})
    steps = rb["steps"]

    def step(name, fn):
        if steps.get(name) == "done":
            return True
        log("reblockout: %s" % name)
        good = fn()
        steps[name] = "done" if good else "failed"
        save(STATE, state)
        return good

    def compile_game():
        with UnrealLock("compile"):
            return run("rb_compile", [UE_BUILD, "UnrealMeridianEditor", "Win64", "Development", "-Project=" + UPROJECT, "-WaitMutex"],
                       3600, lambda t, c: "Result: Succeeded" in t)
    if not step("compile", compile_game):
        return False
    before = {}

    def blockouts():
        before.update(boxes())
        return run("rb_roo2gltf", [PY, "tools/roo2gltf/roo2gltf.py", "--preview"], 4 * 3600)
    if not step("blockouts", blockouts):
        return False
    if not step("guard", lambda: guard(before or boxes())):
        return False
    rids = sorted(z["rid"] for z in load(os.path.join(REPO, "data", "zone_layout.json"))["zones"] if "shares_geometry_with" not in z)
    if not step("textures", lambda: run("rb_zone_textures", [PY, "tools/environment/zone_textures.py", *map(str, rids)], 3600)):
        return False

    def art():
        for rid in rids:
            if rb["art"].get(str(rid), {}).get("art") in ("ok", "failed", "no config"):
                continue
            zone_art(rid, rb["art"], set_aside=False)  # an approved zone keeps its config: logged, not stripped
            save(STATE, state)
        return True
    step("art", art)
    if not step("world", lambda: world_build("rb_world")):
        return False
    finish(state, rb["final"])
    return True


def parse_passes(text):
    out = []
    for part in text.split(","):
        a, _, b = part.partition("-")
        out += list(range(int(a), int(b or a) + 1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--passes", default="3-7")
    ap.add_argument("--only-region", default="")
    ap.add_argument("--no-finish", action="store_true", help="stop after the world builds (no minimaps, tests or report)")
    ap.add_argument("--report", action="store_true", help="only rewrite build/overnight/report.md")
    ap.add_argument("--overlaps", action="store_true", help="also run check_overlaps.py on each zone's cut-outs (slow on big maps)")
    ap.add_argument("--locked", metavar="STEP", help="run the command after -- under the Unreal lock")
    ap.add_argument("--reblockout", action="store_true", help="rebuild every zone's blockout, art and the world (after a roo2gltf change)")
    ap.add_argument("cmd", nargs="*")
    args = ap.parse_args()
    os.makedirs(LOGS, exist_ok=True)
    if args.locked:
        global OWNER
        OWNER = "overnight-maps:" + args.locked
        with UnrealLock(args.locked):
            sys.exit(subprocess.run(args.cmd, cwd=REPO).returncode)
    global CHECK_OVERLAPS
    CHECK_OVERLAPS = args.overlaps
    state = load(STATE, {"passes": {}})
    if args.report:
        report(state)
        return
    if args.reblockout:
        log("start: reblockout")
        try:
            reblockout(state)
        finally:
            report(state)
            log("end")
        return
    log("start: passes %s%s" % (args.passes, " (%s only)" % args.only_region if args.only_region else ""))
    try:
        for p in parse_passes(args.passes):
            if not do_pass(state, p, args.only_region):
                log("pass %d stopped; see the log" % p)
                break
        if not args.no_finish:
            finish(state)
    finally:
        report(state)
        log("end")


if __name__ == "__main__":
    main()
