"""
Sprite -> 3D asset pipeline (docs/adr/0007, skill .claude/skills/sprite-to-3d). One asset at a time,
driven by its manifest data/aigen/<kind>/<name>.json; working files in build/aigen/<kind>/<name>/.

    python tools/aigen/aigen.py models                       # image model ids the API keys can use
    python tools/aigen/aigen.py brazier sprite               # extract + 4x upscale (alpha kept)  -> 01_upscale/
    python tools/aigen/aigen.py brazier restyle [--provider vertex,fal@fal-ai/qwen-image-edit] [--views front,above]
    python tools/aigen/aigen.py brazier review               # sheet 1: original | upscale | restyles -> 05_review/
    python tools/aigen/aigen.py brazier all                  # sprite + restyle + review
    python tools/aigen/aigen.py all sprite                   # a step over every manifest not "status": "done"
    python tools/aigen/aigen.py every normalize              # every kit mesh, rebuilt from art_src/ (fresh clone)
    python tools/aigen/aigen.py all batch-submit             # uncached OpenAI restyles as one Batch API job (50% off)
    python tools/aigen/aigen.py batch-collect                # fetch finished batches into each 02_restyle/
    python tools/aigen/aigen.py brazier tripo-prepare        # 03_tripo_in/ + SETTINGS.md for Tripo Studio
    python tools/aigen/aigen.py brazier bridge C --task 8859502e   # start, then Studio: Export > Send To Blender
    python tools/aigen/aigen.py brazier tripo-ingest         # renders every 04_tripo_out/*.glb, sheet 2
    python tools/aigen/aigen.py brazier choose B             # B.glb -> art_src/aigen/<kind>/<name>/ (LFS)
    python tools/aigen/aigen.py brazier normalize            # -> build/environment/kit/<mesh>.glb

Runs in build/texai/.venv (torch for the upscale, requests for the APIs); started with another
Python it re-runs itself there. Each step is cached by a hash of its inputs, model and prompt
(--force reruns it); paid calls (restyle) are never repeated for the same inputs.
"""
import argparse
import hashlib
import inspect
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VENV_PY = ROOT / "build" / "texai" / ".venv" / "Scripts" / "python.exe"

if __name__ == "__main__" and VENV_PY.exists() and Path(sys.executable).resolve() != VENV_PY.resolve():
    sys.exit(subprocess.run([str(VENV_PY), __file__] + sys.argv[1:]).returncode)

import warnings  # noqa: E402

from PIL import Image  # noqa: E402

warnings.filterwarnings("ignore", message=".*meshgrid.*")

import blender_link  # noqa: E402
import restyle  # noqa: E402
import review  # noqa: E402
import sprite  # noqa: E402
import tripo  # noqa: E402

DATA = ROOT / "data" / "aigen"
WORK = ROOT / "build" / "aigen"
ART_SRC = ROOT / "art_src" / "aigen"
KIT = ROOT / "build" / "environment" / "kit"
BLENDER = Path(os.environ.get("MR_BLENDER", r"H:\Steam\steamapps\common\Blender\blender.exe"))
PROP_GLB = ROOT / "tools" / "blender" / "prop_glb.py"


# --- manifest and cache helpers -------------------------------------------------------------------

def manifest_path(name: str) -> Path:
    hits = sorted(DATA.glob("*/%s.json" % name))
    if not hits:
        raise SystemExit("no manifest data/aigen/<kind>/%s.json" % name)
    return hits[0]


def load(name: str) -> dict:
    m = json.loads(manifest_path(name).read_text(encoding="utf-8"))
    m.setdefault("name", name)
    return m


def pretty(v, indent: int = 0) -> str:
    """JSON with short values on one line (as the hand-written data/ files)."""
    flat = json.dumps(v, ensure_ascii=False)
    if len(flat) + indent <= 110 or not isinstance(v, (dict, list)) or not v:
        return flat
    pad = " " * (indent + 2)
    if isinstance(v, list):
        return "[\n%s\n%s]" % (",\n".join(pad + pretty(x, indent + 2) for x in v), " " * indent)
    return "{\n%s\n%s}" % (",\n".join("%s%s: %s" % (pad, json.dumps(k), pretty(x, indent + 2)) for k, x in v.items()), " " * indent)


def save(m: dict):
    manifest_path(m["name"]).write_text(pretty(m) + "\n", encoding="utf-8")


def work_dir(m: dict) -> Path:
    return WORK / m.get("kind", "props") / m["name"]


def digest(*parts) -> str:
    h = hashlib.sha1()
    for p in parts:
        if isinstance(p, Path):
            h.update(p.read_bytes())
        elif isinstance(p, bytes):
            h.update(p)
        else:
            h.update(json.dumps(p, sort_keys=True, default=str).encode())
    return h.hexdigest()[:16]


def fresh(stamp: Path, key: str, force: bool) -> bool:
    return not force and stamp.exists() and json.loads(stamp.read_text()).get("key") == key


def rel(p: Path) -> str:
    return p.resolve().relative_to(ROOT).as_posix()


def blender(*args) -> dict:
    """Run tools/blender/prop_glb.py; returns its stats line."""
    r = subprocess.run([str(BLENDER), "-b", "--factory-startup", "-P", str(PROP_GLB), "--"] + [str(a) for a in args],
                       capture_output=True, text=True)
    line = next((l for l in r.stdout.splitlines() if l.startswith("[prop_glb] ")), None)
    if r.returncode or not line:
        sys.stderr.write(r.stdout[-3000:] + r.stderr[-3000:])
        raise SystemExit("blender prop_glb.py %s failed" % args[0])
    return json.loads(line[len("[prop_glb] "):])


# --- steps ----------------------------------------------------------------------------------------

def step_sprite(m: dict, a):
    out = work_dir(m) / "01_upscale"
    img, meta = sprite.frame(m["bgf"], m.get("frame", 0))
    # world size from the visible pixels of the frame that sets it ("size_frame", default "frame"):
    # height, width, and the empty rows under it (a hanging chandelier: "lift_m" off the floor)
    sized, _ = sprite.frame(m["bgf"], m.get("size_frame", m.get("frame", 0)))
    x0, y0, x1, y1 = sized.getchannel("A").getbbox()
    mpp = sprite.bgf2png.M_PER_SQUARE / 64.0 / meta["shrink"]
    m["height_m"], m["width_m"] = round((y1 - y0) * mpp, 4), round((x1 - x0) * mpp, 4)
    m["lift_m"] = round((sized.height - y1) * mpp, 4)
    m["sprite_px"] = list(img.size)
    img = img.crop(img.getchannel("A").getbbox())
    # the original's own side and back drawings, when the sprite is drawn from several directions
    # (sprite.angle_frames): restyled per view from their own drawing (docs/adr/0007 "Original angles")
    angles = sprite.angle_frames(meta, m.get("frame", 0))
    if angles:
        m["angles"] = angles
    else:
        m.pop("angles", None)
    views = {v: sprite.frame(m["bgf"], b)[0] for v, b in angles.items()}
    views = {v: im.crop(im.getchannel("A").getbbox()) for v, im in views.items()}
    fit = max(max(im.size) for im in [img] + list(views.values())) * sprite.SCALE if views else 0
    model = m.get("upscale_model", sprite.UPSCALE_MODEL)
    key = digest(img.tobytes(), model, inspect.getsource(sprite), *[views[v].tobytes() for v in sorted(views)])
    stamp = out / "stamp.json"
    if fresh(stamp, key, a.force):
        print("sprite: cached (%s)" % rel(out))
        return
    out.mkdir(parents=True, exist_ok=True)
    img.save(out / "sprite.png")
    big = sprite.upscale_rgba(img, model)
    big.save(out / "upscaled.png")
    sprite.on_canvas(big, bg=None, fit=fit).save(out / "canvas.png")      # transparent, for Tripo
    sprite.on_canvas(big, fit=fit).save(out / "canvas_grey.png")          # flat grey, for the image models
    for v, im in views.items():  # 01_upscale/<view>_upscaled.png, <view>_grey.png, at the front's scale
        vbig = sprite.upscale_rgba(im, model)
        vbig.save(out / ("%s_upscaled.png" % v))
        sprite.on_canvas(vbig, fit=fit).save(out / ("%s_grey.png" % v))
    stamp.write_text(json.dumps({"key": key, "model": model}))
    print("sprite: %s frame %d %dx%d -> %dx%d, height %.3f m%s (%s)" % (
        m["bgf"], m.get("frame", 0), *img.size, *big.size, m["height_m"],
        ", original views %s" % ", ".join("%s=%d" % kv for kv in angles.items()) if angles else "", rel(out)))


def restyle_jobs(m: dict, a) -> list:
    """The asset's restyles: [{provider, model, slug, view, target, refs, text, key, fresh}]."""
    src = work_dir(m) / "01_upscale" / "canvas_grey.png"
    if not src.exists():
        raise SystemExit("%s: run the sprite step first" % m["name"])
    cfg = m.get("restyle", {})
    providers = a.provider.split(",") if a.provider else cfg.get("providers", ["openai"])
    # views: the manifest's "views" if set, else the front plus every view the original draws itself
    angles = m.get("angles", {})
    views = a.views.split(",") if a.views else cfg.get("views") or ["front"] + list(angles)
    if m.get("symmetrical") and not a.views:
        views = ["front"]  # the back reuses the front (tripo.SYMMETRICAL_VIEWS): nothing else to paint
    out = work_dir(m) / "02_restyle"
    out.mkdir(parents=True, exist_ok=True)
    jobs = []
    for entry in providers:
        provider, model, slug = restyle.split(entry, cfg)
        for view in ["front"] + [v for v in views if v != "front"]:
            target = out / ("%s_%s.png" % (slug, view))
            front = out / ("%s_front.png" % slug)
            own = view in angles  # redraw the original's own drawing of this side, not a guess from the front
            front_job = next((j for j in jobs if j["slug"] == slug and j["view"] == "front"), None)
            if own and (not front.exists() or (front_job and not front_job["fresh"])):
                print("restyle %s %s %s: waiting for the front restyle (its colours are the reference)" % (m["name"], slug, view))
                continue
            vsrc = src.parent / ("%s_grey.png" % view) if own else src
            refs = [vsrc] + ([front] if view != "front" and front.exists() else [])
            text = restyle.prompt(m["describe"], view, len(refs) > 1, cfg.get("extra", ""), own_view=own)
            key = digest(vsrc, text, model, front if len(refs) > 1 else "")
            jobs.append({"provider": provider, "model": model, "slug": slug, "view": view, "target": target,
                         "refs": refs, "text": text, "key": key, "fresh": fresh(target.with_suffix(".json"), key, a.force)})
    return jobs


def record_restyle(m: dict, job: dict, img, info: dict):
    img.save(job["target"])
    info.update(key=job["key"], prompt=job["text"], size=list(img.size))
    job["target"].with_suffix(".json").write_text(json.dumps(info, indent=1))
    m.setdefault("restyle_runs", {})["%s_%s" % (job["slug"], job["view"])] = {
        "model": job["model"], "seconds": info.get("seconds"), "batch": info.get("batch"), "file": rel(job["target"])}


def step_restyle(m: dict, a):
    for job in restyle_jobs(m, a):
        if job["fresh"]:
            print("restyle %s %s %s: cached" % (m["name"], job["slug"], job["view"]))
            continue
        print("restyle %s %s %s (%s) ..." % (m["name"], job["slug"], job["view"], job["model"]), flush=True)
        try:
            img, info = restyle.run(job["provider"], [Image.open(r) for r in job["refs"]], job["text"], job["model"])
        except (RuntimeError, SystemExit) as e:
            print("  FAILED: %s" % e)
            continue
        record_restyle(m, job, img, info)
        print("  %s (%.0f s)" % (rel(job["target"]), info["seconds"]))


BATCHES = WORK / "batches"


def batch_submit(ms: list, a):
    """Every uncached OpenAI restyle of `ms` as one Batch API job (50% off, done within 24 h)."""
    jobs = []
    for m in ms:
        for job in restyle_jobs(m, a):
            if job["provider"] == "openai" and not job["fresh"]:
                jobs.append((m["name"], job))
    if not jobs:
        print("batch-submit: nothing to restyle")
        return
    lines = [restyle.openai_batch_line("%s|%s|%s" % (name, rel(job["target"]), job["key"]),
                                       [Image.open(r) for r in job["refs"]], job["text"], job["model"])
             for name, job in jobs]
    batch = restyle.openai_batch_create(lines)
    BATCHES.mkdir(parents=True, exist_ok=True)
    state = {"id": batch["id"], "created": batch.get("created_at"), "status": batch.get("status"),
             "jobs": [{"asset": name, "target": rel(job["target"]), "key": job["key"], "text": job["text"],
                       "model": job["model"], "slug": job["slug"], "view": job["view"]} for name, job in jobs]}
    (BATCHES / ("%s.json" % batch["id"])).write_text(json.dumps(state, indent=1))
    print("batch-submit: %d restyles in batch %s (%s)" % (len(jobs), batch["id"], batch.get("status")))


def batch_collect() -> int:
    """Collect finished OpenAI batches into each asset's 02_restyle/ (the same stamps as a direct
    restyle, so the restyle step sees them as cached). Returns how many batches are still running."""
    pending = 0
    for f in sorted(BATCHES.glob("*.json")) if BATCHES.exists() else []:
        state = json.loads(f.read_text())
        if state.get("collected"):
            continue
        batch = restyle.openai_batch_get(state["id"])
        counts = batch.get("request_counts", {})
        print("batch %s: %s (%s/%s done, %s failed)" % (state["id"], batch["status"], counts.get("completed"),
                                                        counts.get("total"), counts.get("failed")))
        if batch["status"] not in ("completed", "failed", "expired", "cancelled"):
            pending += 1
            continue
        results = restyle.openai_batch_results(batch)
        by_asset = {}
        for job in state["jobs"]:
            res = results.get("%s|%s|%s" % (job["asset"], job["target"], job["key"]))
            if not res or res.get("error"):
                print("  %s: FAILED %s" % (job["asset"], (res or {}).get("error")))
                continue
            m = by_asset.setdefault(job["asset"], load(job["asset"]))
            record_restyle(m, dict(job, target=ROOT / job["target"]), res["image"],
                           {"model": job["model"], "batch": state["id"], "usage": res.get("usage")})
            print("  %s -> %s" % (job["asset"], job["target"]))
        for m in by_asset.values():
            save(m)
        state.update(collected=True, status=batch["status"])
        f.write_text(json.dumps(state, indent=1))
    return pending


def step_review(m: dict, a):
    w = work_dir(m)
    cols = [("original %dx%d" % tuple(m.get("sprite_px", (0, 0))), Image.open(w / "01_upscale" / "sprite.png")),
            ("upscaled 4x", Image.open(w / "01_upscale" / "upscaled.png"))]
    for p in sorted((w / "02_restyle").glob("*.png")):
        cols.append((p.stem.replace("_", " "), Image.open(p)))
    out = review.sheet(cols, w / "05_review" / "sheet1_restyle.png", title="%s: sprite -> restyle" % m["name"])
    print("review: %s" % rel(out))


def resolve_input(m: dict, source: str, view: str) -> Path:
    """A Tripo input image: "upscale" (the upscaled sprite), "<provider>" (that provider's restyle of
    this view) or "<provider>@<view>" (its restyle of another view: "openai@front" gives a side or the
    back the front's image, for props the original draws once, rocks and dung, docs/adr/0007)."""
    w = work_dir(m)
    source, _, mirror = source.partition(":")
    if source == "upscale":
        path = w / "01_upscale" / "canvas.png"
    else:
        source, _, own = source.partition("@")
        path = w / "02_restyle" / ("%s_%s.png" % (source, own or view))
    if mirror == "mirror" and path.exists():
        # "<source>:mirror": that image flipped left to right (a symmetrical prop's back, tripo.SYMMETRICAL_VIEWS)
        flipped = path.with_name(path.stem + "_mirrored.png")
        if not flipped.exists() or flipped.stat().st_mtime < path.stat().st_mtime:
            Image.open(path).transpose(Image.Transpose.FLIP_LEFT_RIGHT).save(flipped)
        return flipped
    return path


def step_tripo_prepare(m: dict, a):
    if tripo.model(m) == "custom" and not m.get("tripo", {}).get("variants"):
        print("tripo-prepare: %s is a custom model (manifest \"model\": \"custom\"); nothing to generate" % m["name"])
        return
    w = work_dir(m)
    variants = {}
    wanted = dict(m.get("tripo", {}).get("variants") or tripo.default_variants(m))
    if m.get("symmetrical"):  # the front sprite in the Front and Back slots (tripo.SYMMETRICAL_VIEWS)
        wanted.setdefault(tripo.symmetrical_variant(m), tripo.SYMMETRICAL_VIEWS)
    for variant, views in wanted.items():
        paths = {view: resolve_input(m, source, view) for view, source in views.items()}
        missing = [str(p) for p in paths.values() if not p.exists()]
        if missing:
            print("tripo-prepare: skipping %s, missing %s" % (variant, ", ".join(missing)))
            continue
        variants[variant] = paths
    out = tripo.prepare(w, variants, m)
    print("tripo-prepare: %d variants -> %s (see SETTINGS.md)" % (len(variants), rel(out)))


def step_bridge(m: dict, a):
    """Collect a model the Tripo DCC Bridge sends into the open Blender as 04_tripo_out/<variant>.glb."""
    if not a.variant:
        raise SystemExit("usage: aigen.py <asset> bridge <variant> [--task <tripo task id>]")
    blender_link.snapshot()
    print("bridge: waiting for %s (Studio: DCC Bridge -> Blender on, then Export > Send To Blender) ..." % a.variant, flush=True)
    out = blender_link.grab(work_dir(m) / "04_tripo_out" / ("%s.glb" % a.variant), a.wait, a.task)
    run = m.setdefault("tripo", {}).setdefault("runs", {}).setdefault(a.variant, {})
    run.update(tripo_task=out["tripo_task"], tripo_name=out["tripo_name"])
    if a.settings:
        run["settings"] = a.settings
    print("bridge: %s <- %s (%s, %d tris)" % (a.variant, out["tripo_task"], out["tripo_name"], out["tris"]))


def step_tripo_ingest(m: dict, a):
    w = work_dir(m)
    glbs = tripo.outputs(w)
    if not glbs:
        raise SystemExit("no GLBs in %s" % rel(w / "04_tripo_out"))
    runs = m.setdefault("tripo", {}).setdefault("runs", {})
    rows = []
    sprite_img = Image.open(w / "01_upscale" / "upscaled.png")
    for glb in glbs:
        variant = glb.stem
        pv = w / "05_review" / "tripo" / variant
        key = digest(glb, inspect.getsource(tripo), (ROOT / "tools" / "blender" / "prop_glb.py").read_text(), "views v3")
        stamp = pv / "stamp.json"
        if fresh(stamp, key, a.force):
            stats = json.loads(stamp.read_text())["stats"]
        else:
            stats = blender("preview", glb, pv, "--views", "front,above,left")
            blender("preview", glb, pv / "wire", "--views", "above,back", "--wire")  # the geometry; the back shows invented unseen sides
            stamp.write_text(json.dumps({"key": key, "stats": stats}))
        run = runs.setdefault(variant, {})
        run.update(file=rel(glb), digest=key, tris=stats["tris"], size_m=stats["size_m"])
        rows.append(("%s  (%d tris)  %s" % (variant, stats["tris"], run.get("settings", "")),
                     [("sprite", sprite_img)] + [(v, Image.open(pv / (v + ".png"))) for v in stats["views"]]
                     + [("mesh " + v, Image.open(pv / "wire" / (v + ".png"))) for v in ("above", "back")]))
        print("tripo-ingest: %s %d tris, size %s" % (variant, stats["tris"], stats["size_m"]))
    out = review.grid(rows, w / "05_review" / "sheet2_tripo.png", title="%s: Tripo variants" % m["name"])
    print("review: %s" % rel(out))


def step_choose(m: dict, a):
    src = work_dir(m) / "04_tripo_out" / ("%s.glb" % a.variant)
    if not src.exists():
        raise SystemExit("%s not found" % rel(src))
    dst = ART_SRC / m.get("kind", "props") / m["name"] / ("%s_tripo.glb" % m["name"])
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    m.setdefault("tripo", {})["chosen"] = {"variant": a.variant, "file": rel(dst), **m["tripo"].get("runs", {}).get(a.variant, {})}
    print("choose: %s -> %s" % (a.variant, rel(dst)))


def step_custom(m: dict, a):
    """A custom model shown in game instead of the HD one: `custom <variant>` keeps that Tripo
    output as art_src/aigen/<kind>/<name>/<name>_custom.glb (or set "custom": {"file": ...} by hand,
    e.g. a model made in Blender); `custom none` removes it. `normalize` builds it as
    SM_AI_<Name>_Custom, which build_world.py places in preference to SM_AI_<Name>; the HD model
    stays the fallback (docs/adr/0007 "Custom models")."""
    if not a.variant:
        raise SystemExit("usage: aigen.py <asset> custom <variant>|none")
    dst = ART_SRC / m.get("kind", "props") / m["name"] / ("%s_custom.glb" % m["name"])
    if a.variant == "none":
        m.pop("custom", None)
        for f in (dst, KIT / ("%s_Custom.glb" % m["mesh"])):
            if f.exists():
                f.unlink()
        print("custom: removed; %s shows the HD model" % m["mesh"])
        return
    src = work_dir(m) / "04_tripo_out" / ("%s.glb" % a.variant)
    if not src.exists():
        raise SystemExit("%s not found" % rel(src))
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    m["custom"] = {"variant": a.variant, "file": rel(dst), **m.get("tripo", {}).get("runs", {}).get(a.variant, {})}
    print("custom: %s -> %s (run normalize to build %s_Custom)" % (a.variant, rel(dst), m["mesh"]))


def normalize_glb(m: dict, src: Path, name: str) -> dict:
    """A raw model -> the kit mesh <name>.glb, scaled and placed like the sprite."""
    dst = KIT / ("%s.glb" % name)
    size_by = m.get("size_by", "height")
    stats = blender("normalize", src, dst, "--size-by", size_by,
                    "--size", m["width_m"] if size_by == "length" else m["height_m"], "--lift", m.get("lift_m", 0),
                    "--name", name, "--yaw", m.get("yaw_deg", 0), "--max-tris", m.get("max_tris", 0),
                    *(["--lay-flat"] if m.get("lay_flat") else []))
    print("normalize: %s %d tris, size %s m" % (rel(dst), stats["tris"], stats["size_m"]))
    return {"file": rel(dst), "tris": stats["tris"], "size_m": stats["size_m"], "scale": stats["scale"]}


def step_normalize(m: dict, a):
    """The chosen (HD) variant -> the kit mesh SM_AI_<Name>.glb, and the manifest's custom model, if
    any, -> SM_AI_<Name>_Custom.glb. With a variant (`normalize SM8k`), that variant's raw GLB ->
    SM_AI_<Name>_<variant>.glb instead, for a side-by-side test through props.json "mesh_custom" or
    "mesh_options" (ADR 0007 "Smart Mesh props"); the chosen mesh is untouched."""
    if a.variant:
        src = work_dir(m) / "04_tripo_out" / ("%s.glb" % a.variant)
        if not src.exists():
            raise SystemExit("%s not found" % rel(src))
        m.setdefault("normalized_variants", {})[a.variant] = normalize_glb(m, src, "%s_%s" % (m["mesh"], a.variant))
        return
    chosen = m.get("tripo", {}).get("chosen")
    if not chosen:
        raise SystemExit("choose a variant first (aigen.py %s choose <variant>)" % m["name"])
    m["normalized"] = normalize_glb(m, ROOT / chosen["file"], m["mesh"])
    pv = work_dir(m) / "05_review" / "normalized"
    blender("preview", KIT / ("%s.glb" % m["mesh"]), pv, "--views", "front,left,above")
    custom = KIT / ("%s_Custom.glb" % m["mesh"])
    if m.get("custom"):
        m["custom"]["normalized"] = normalize_glb(m, ROOT / m["custom"]["file"], custom.stem)
    elif custom.exists():
        custom.unlink()


STEPS = {"sprite": step_sprite, "restyle": step_restyle, "review": step_review,
         "tripo-prepare": step_tripo_prepare, "bridge": step_bridge, "tripo-ingest": step_tripo_ingest,
         "choose": step_choose, "custom": step_custom, "normalize": step_normalize}


def bridge_collect(spec: str = "all"):
    """Wait for the Tripo DCC Bridge to deliver every recorded Tripo task (manifest tripo.runs.<variant>
    .tripo_task) that has no GLB yet, in any order, and file each as 04_tripo_out/<variant>.glb.
    Start it, then Send To Blender each model in Studio."""
    targets, owners = {}, {}
    for m in manifests(spec):
        for variant, run in m.get("tripo", {}).get("runs", {}).items():
            path = work_dir(m) / "04_tripo_out" / ("%s.glb" % variant)
            if run.get("tripo_task") and not path.exists():
                targets[run["tripo_task"]] = path
                owners[run["tripo_task"]] = (m["name"], variant)
    print("bridge-collect: waiting for %d models (Studio: DCC Bridge -> Blender on; Export > Send To Blender)" % len(targets),
          flush=True)
    got = blender_link.collect(targets, log=lambda s: print(s, flush=True))
    for task, out in got.items():
        name, variant = owners[task]
        m = load(name)
        m["tripo"]["runs"][variant].update(tripo_name=out["root"], tris_raw=out["tris"])
        save(m)
    print("bridge-collect: %d/%d collected" % (len(got), len(targets)))


def overview(spec: str, restyle_name: str = "openai_front", cols: int = 6):
    """One sheet of every asset: the 4x upscale next to an image of it: a restyle name ("openai_front"),
    or a path under the asset's work folder ("05_review/normalized/front.png") -> build/aigen/overview_<name>.png."""
    from PIL import ImageDraw
    cell, pad = 300, 8
    ms = [m for m in manifests(spec) if (work_dir(m) / "01_upscale" / "upscaled.png").exists()]
    rows = -(-len(ms) // cols)
    sheet = Image.new("RGB", (cols * (2 * cell + 3 * pad), rows * (cell + 34)), review.BG)
    d = ImageDraw.Draw(sheet)
    for i, m in enumerate(ms):
        x0, y0 = (i % cols) * (2 * cell + 3 * pad), (i // cols) * (cell + 34)
        imgs = [Image.open(work_dir(m) / "01_upscale" / "upscaled.png")]
        r = work_dir(m) / restyle_name if "/" in restyle_name else work_dir(m) / "02_restyle" / ("%s.png" % restyle_name)
        if r.exists():
            imgs.append(Image.open(r))
        for j, im in enumerate(imgs):
            im = review._flatten(im)
            s = cell / max(im.size)
            im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.Resampling.LANCZOS)
            sheet.paste(im, (x0 + pad + j * (cell + pad) + (cell - im.width) // 2, y0 + (cell - im.height) // 2))
        d.text((x0 + pad, y0 + cell + 6), "%s  %.2f m" % (m["name"], m.get("height_m", 0)), fill=(230, 230, 230), font=review._font(20))
    out = WORK / ("overview_%s.png" % restyle_name.replace("/", "_").removesuffix(".png"))
    sheet.save(out)
    print("overview: %s (%d assets)" % (rel(out), len(ms)))


def manifests(spec: str) -> list:
    """An asset name, comma-separated names, "all" (every manifest not marked "status": "done") or
    "every" (all manifests: e.g. `aigen.py every normalize` rebuilds every kit mesh from art_src/)."""
    if spec in ("all", "every"):
        ms = [load(p.stem) for p in sorted(DATA.glob("*/*.json"))]
        return ms if spec == "every" else [m for m in ms if m.get("status") != "done"]
    return [load(n) for n in spec.split(",")]


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "models":
        print(json.dumps(restyle.list_models(), indent=1))
        return
    if len(sys.argv) > 1 and sys.argv[1] == "batch-collect":
        batch_collect()
        return
    if len(sys.argv) > 1 and sys.argv[1] == "bridge-collect":
        bridge_collect(sys.argv[2] if len(sys.argv) > 2 else "all")
        return
    if len(sys.argv) > 1 and sys.argv[1] == "overview":
        overview(sys.argv[2] if len(sys.argv) > 2 else "all", sys.argv[3] if len(sys.argv) > 3 else "openai_front")
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("asset", help='a manifest name, "a,b,c", or "all"')
    ap.add_argument("step", choices=list(STEPS) + ["all", "batch-submit"])
    ap.add_argument("variant", nargs="?")
    ap.add_argument("--provider")
    ap.add_argument("--views")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--task", help="bridge: the Tripo task id (prefix) expected")
    ap.add_argument("--settings", help="bridge: the Studio settings used, recorded in the manifest")
    ap.add_argument("--wait", type=float, default=300, help="bridge: seconds to wait")
    a = ap.parse_args()
    ms = manifests(a.asset)
    if a.step == "batch-submit":
        batch_submit(ms, a)
        return
    steps = ["sprite", "restyle", "review"] if a.step == "all" else [a.step]
    for m in ms:
        for step in steps:
            try:
                STEPS[step](m, a)
            except SystemExit as e:
                if len(ms) == 1:
                    raise
                print("%s %s: %s" % (m["name"], step, e))
            save(m)


if __name__ == "__main__":
    main()
