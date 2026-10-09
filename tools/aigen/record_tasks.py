"""
Record Tripo Studio task ids in the manifests (tripo.runs.<variant>.tripo_task), so bridge-collect
knows what to wait for (docs/adr/0007, .claude/skills/sprite-to-3d/reference/studio-automation.md).

    python tools/aigen/record_tasks.py tasks.json [--variant C] [--settings "Studio HD Model, H3.1, ..."]

tasks.json: {"<asset name>": "<task id>", ...}. Keeps an earlier task as "stuck_task" when replaced.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "data" / "aigen"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tasks")
    ap.add_argument("--variant", default="C")
    ap.add_argument("--settings", default="")
    a = ap.parse_args()
    for name, task in json.loads(Path(a.tasks).read_text()).items():
        path = next(DATA.glob("*/%s.json" % name))
        m = json.loads(path.read_text(encoding="utf-8"))
        runs = m.setdefault("tripo", {}).setdefault("runs", {})
        run = runs.setdefault(a.variant, {})
        if run.get("tripo_task") and run["tripo_task"] != task:
            run["stuck_task"] = run["tripo_task"]
        run["tripo_task"] = task
        if a.settings:
            run["settings"] = a.settings % {"polycount": m.get("polycount", 2000)} if "%(" in a.settings else a.settings
        path.write_text(json.dumps(m, indent=1) + "\n", encoding="utf-8")
        print("%s %s: %s" % (name, a.variant, task))


if __name__ == "__main__":
    main()
