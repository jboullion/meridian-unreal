"""Summarise a CSV profile from the hitch test (run_hitch_test.ps1 -Extra -MRHitchCsv): the slow frames
(over 33 ms) against the rest, by GPU pass, and the timeline of the passes that grew.

    python tools/lookdev/hitch_report.py                 (the newest profile in Saved/Profiling/CSV)
    python tools/lookdev/hitch_report.py path/to/Profile(...).csv
"""
import csv
import glob
import os
import statistics
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CSV_DIR = os.path.join(ROOT, "game", "UnrealMeridian", "Saved", "Profiling", "CSV")


def load(path):
    lines = list(csv.reader(open(path, encoding="utf-8", errors="replace")))
    headers = [i for i, line in enumerate(lines) if line and line[0] == "EVENTS"]
    header = lines[headers[-1]]  # the last header row has every column (stats appear during the run)
    index = {h: i for i, h in enumerate(header)}
    rows = [line for i, line in enumerate(lines) if i not in headers and line and not line[0].startswith("[")]
    return header, index, rows


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else max(glob.glob(os.path.join(CSV_DIR, "*.csv")), key=os.path.getmtime)
    header, index, rows = load(path)

    def v(row, key):
        try:
            return float(row[index[key]])
        except (KeyError, IndexError, ValueError):
            return 0.0

    rows = rows[60:]  # the first second: loading
    slow = [r for r in rows if v(r, "FrameTime") > 33]
    fast = [r for r in rows if v(r, "FrameTime") <= 20]
    print(f"{os.path.basename(path)}: {len(rows)} frames, {len(slow)} over 33 ms")
    for key in ("FrameTime", "GameThreadTime", "RenderThreadTime", "GPUTime"):
        print(f"  {key:18s} median {statistics.median(v(r, key) for r in rows):6.1f}"
              f"   slow {statistics.mean(v(r, key) for r in slow) if slow else 0:6.1f}")
    if not slow or not fast:
        return
    passes = [h for h in header if h.startswith("GPU/")]
    grew = sorted(((statistics.mean(v(r, h) for r in slow) - statistics.mean(v(r, h) for r in fast), h) for h in passes),
                  reverse=True)[:10]
    print("GPU passes, slow frames minus fast frames (ms):")
    for d, h in grew:
        print(f"  {d:6.2f}  {h}  (slow {statistics.mean(v(r, h) for r in slow):.2f})")
    mem = [("GPUMem/LocalUsedMB", "VRAM used"), ("GPUMem/LocalBudgetMB", "VRAM budget"), ("RenderTargetPoolSize", "RT pool")]
    print("  " + "  ".join(f"{label} {statistics.median(v(r, k) for r in rows):.0f} MB" for k, label in mem))


if __name__ == "__main__":
    main()
