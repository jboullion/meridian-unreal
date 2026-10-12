"""
The overnight prop batch (docs/adr/0007 "Overnight batch"): every stage of the sprite-to-3d pipeline
after the sprite step, unattended, through the OpenAI Batch API and the Tripo API. Nothing on screen,
no Unreal, no commits.

    python tools/aigen/overnight.py --new a,b,c --upgrade d,e [--dry-run]

--new: assets made from scratch (manifests written, sprite step and "describe" done beforehand); each
  is restyled (front, then its own side views), generated (MV when the sprite draws its sides, else C),
  chosen and normalised into build/environment/kit/.
--upgrade: finished assets that get a 4-view model too (variant MV); their chosen model is left alone
  for the user to compare in the morning.

Stages, each resumable (a rerun skips what's done; Tripo task ids are saved as soon as they're sent):
  1. restyle fronts (and the upgrades' sides): aigen.py batch-submit, then batch-collect until done
  2. restyle the new assets' sides (they take the front's colours)
  3. tripo-prepare   4. tripo-api   5. tripo-ingest (review sheets)
  6. new assets: choose + normalize   7. overview sheets and report.md
Log and report: build/aigen/overnight/<date>/. Keeps Windows awake while it runs (SetThreadExecutionState).
"""
import argparse
import datetime
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AIGEN = ROOT / "tools" / "aigen" / "aigen.py"
DATA = ROOT / "data" / "aigen" / "props"
WORK = ROOT / "build" / "aigen"
OUT = WORK / "overnight" / datetime.date.today().isoformat()
POLL_S = 120
BATCH_WAIT_S = 8 * 3600

LOG = None


def log(msg: str):
    line = "%s %s" % (datetime.datetime.now().strftime("%H:%M:%S"), msg)
    print(line, flush=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def aigen(*args, quiet=False) -> str:
    """Run aigen.py (it re-runs itself in build/texai/.venv); its output goes to the log."""
    r = subprocess.run([sys.executable, str(AIGEN), *args], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    if not quiet:
        for line in out.splitlines():
            if line.strip():
                log("  " + line)
    if r.returncode:
        log("  (aigen.py %s exited %d)" % (" ".join(args[:2]), r.returncode))
    return out


def wait_batches():
    t0 = time.time()
    while True:
        out = aigen("batch-collect", quiet=True)
        running = re.findall(r"batch \S+: (validating|in_progress|finalizing)", out)
        for line in out.splitlines():
            if "FAILED" in line or "->" in line or "completed" in line:
                log("  " + line)
        if not running:
            return
        if time.time() - t0 > BATCH_WAIT_S:
            log("restyle batches still running after %d h; going on without them" % (BATCH_WAIT_S // 3600))
            return
        time.sleep(POLL_S)


def manifest(name: str) -> dict:
    return json.loads((DATA / ("%s.json" % name)).read_text(encoding="utf-8"))


def keep_awake():
    if sys.platform == "win32":
        import ctypes
        ES_CONTINUOUS, ES_SYSTEM_REQUIRED = 0x80000000, 0x00000001
        ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)


def views_sheet(names: list, path: Path):
    """The upgrades: the kept model (C) beside the new 4-view one (MV), front and above."""
    from PIL import Image, ImageDraw
    cell, rows = 300, []
    for n in names:
        imgs = []
        for variant in ("C", "MV"):
            for view in ("front", "left", "above"):
                p = WORK / "props" / n / "05_review" / "tripo" / variant / ("%s.png" % view)
                imgs.append((variant + " " + view, Image.open(p).convert("RGB") if p.exists() else None))
        rows.append((n, imgs))
    sheet = Image.new("RGB", (cell * 6 + 160, len(rows) * (cell + 24)), (40, 40, 44))
    d = ImageDraw.Draw(sheet)
    for i, (n, imgs) in enumerate(rows):
        y = i * (cell + 24)
        d.text((8, y + cell // 2), n, fill=(255, 255, 0))
        for j, (label, im) in enumerate(imgs):
            x = 160 + j * cell
            if im:
                im.thumbnail((cell - 6, cell - 6))
                sheet.paste(im, (x + 3, y + 22))
            d.text((x + 4, y + 4), label, fill=(220, 220, 220))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def report(new: list, upgrade: list, balance_before: str, balance_after: str, t0: float):
    lines = ["# Overnight prop batch, %s" % datetime.date.today().isoformat(), "",
             "Ran %.1f h. Tripo wallet: before %s, after %s." % ((time.time() - t0) / 3600, balance_before, balance_after), ""]
    credits, ok, bad = 0.0, [], []
    for n in new + upgrade:
        m = manifest(n)
        for variant, run in m.get("tripo", {}).get("runs", {}).items():
            if run.get("backend") != "api":
                continue
            credits += float(run.get("credits") or 0)
            glb = WORK / "props" / n / "04_tripo_out" / ("%s.glb" % variant)
            (ok if glb.exists() else bad).append("%s %s%s" % (n, variant, ": " + run["failed"] if run.get("failed") else ""))
    normalized = [n for n in new if (ROOT / "build" / "environment" / "kit" / ("%s.glb" % manifest(n)["mesh"])).exists()]
    missing_restyle = [n for n in new + upgrade if not (WORK / "props" / n / "02_restyle" / "openai_front.png").exists()]
    lines += ["- Tripo models made: %d (%.0f credits)" % (len(ok), credits),
              "- New assets normalised: %d of %d" % (len(normalized), len(new)),
              "- Failed or missing: %s" % (", ".join(bad) or "none"),
              "- Without a front restyle: %s" % (", ".join(missing_restyle) or "none"),
              "- Not normalised: %s" % (", ".join(sorted(set(new) - set(normalized))) or "none"), "",
              "Sheets:", "- new assets: build/aigen/overview_05_review_normalized_front.png",
              "- upgrades (kept C beside the new MV): %s" % (OUT / "upgrades.png").relative_to(ROOT).as_posix(),
              "- per asset: build/aigen/props/<name>/05_review/sheet2_tripo.png", ""]
    (OUT / "report.md").write_text("\n".join(lines), encoding="utf-8")
    log("report: %s" % (OUT / "report.md"))


def main():
    global LOG
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--new", default="")
    ap.add_argument("--upgrade", default="")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    new = [n for n in a.new.split(",") if n]
    upgrade = [n for n in a.upgrade.split(",") if n]
    OUT.mkdir(parents=True, exist_ok=True)
    LOG = OUT / "log.txt"
    for n in new:
        m = manifest(n)
        if not m.get("describe") or "height_m" not in m:
            raise SystemExit("%s: write its describe and run the sprite step first" % n)
    for n in upgrade:
        if "MV" not in manifest(n).get("tripo", {}).get("variants", {}):
            raise SystemExit("%s: add the MV variant first" % n)
    jobs = sum(1 for n in new) + len(upgrade)
    mv = [n for n in new if manifest(n).get("angles")]
    log("overnight: %d new (%d with their own side views: %s), %d upgrades; about %d Tripo jobs" % (
        len(new), len(mv), ",".join(mv), len(upgrade), jobs))
    if a.dry_run:
        log("dry run: nothing sent")
        return
    keep_awake()
    t0 = time.time()
    both = ",".join(new + upgrade)
    before = aigen("tripo-balance", quiet=True).strip().splitlines()[-1:]
    log("stage 1: restyle fronts (and the upgrades' sides)")
    wait_batches()  # a batch from an interrupted run is collected first, not sent again
    aigen(both, "batch-submit")
    wait_batches()
    log("stage 2: restyle the new assets' own side views")
    aigen(both, "batch-submit")
    wait_batches()
    log("stage 3: tripo-prepare")
    aigen(both, "tripo-prepare", quiet=True)
    log("stage 4: tripo-api")
    if new:
        aigen(",".join(new), "tripo-api")
    if upgrade:
        aigen(",".join(upgrade), "tripo-api", "MV")
    log("stage 5: tripo-ingest")
    aigen(both, "tripo-ingest", quiet=True)
    log("stage 6: choose and normalize the new assets")
    for n in new:
        glbs = {p.stem for p in (WORK / "props" / n / "04_tripo_out").glob("*.glb")}
        variant = "MV" if "MV" in glbs else "C" if "C" in glbs else None
        if not variant:
            log("  %s: no model to choose" % n)
            continue
        aigen(n, "choose", variant, quiet=True)
        aigen(n, "normalize")
    log("stage 7: sheets and report")
    if new:
        aigen("overview", ",".join(new), "05_review/normalized/front.png")
    if upgrade:
        views_sheet(upgrade, OUT / "upgrades.png")
    after = aigen("tripo-balance", quiet=True).strip().splitlines()[-1:]
    report(new, upgrade, " ".join(before), " ".join(after), t0)
    log("overnight: done")


if __name__ == "__main__":
    main()
