"""
Summarise a look-dev profile run (tools/ue/run_lookdev.ps1 -Profile).

    python tools/lookdev/profile_report.py perf1            # markdown tables to stdout
    python tools/lookdev/profile_report.py perf1 --top 14   # more GPU passes

Reads build/lookdev/<label>/<camera>__<variant>.csv (CSV-profiler captures with per-pass GPU
timings, r.GPUCsvStatsEnabled) and prints:
  1. GPU frame time per camera and variant, and what tessellation, grass and the fire lights cost;
  2. the most expensive GPU passes, averaged over all cameras, per variant.
"""
from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VARIANTS = ("full", "no_tess", "no_grass", "no_fire")


def read_capture(path: Path) -> dict[str, float]:
    """Column -> mean over the captured frames (the CSV profiler appends metadata rows; skip them)."""
    with path.open(newline="", encoding="utf-8", errors="replace") as f:
        rows = list(csv.reader(f))
    header, data = rows[0], []
    for row in rows[1:]:
        if len(row) != len(header):
            continue
        try:
            data.append([float(v) if v else 0.0 for v in row])
        except ValueError:
            continue  # metadata / event rows
    return {name: statistics.fmean(r[i] for r in data) for i, name in enumerate(header)} if data else {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("label")
    ap.add_argument("--top", type=int, default=10)
    args = ap.parse_args()
    folder = ROOT / "build" / "lookdev" / args.label
    caps: dict[str, dict[str, dict[str, float]]] = defaultdict(dict)
    for p in sorted(folder.glob("*__*.csv")):
        if p.stat().st_size == 0:
            print("(skipping %s: empty, the run quit before the profiler wrote it)" % p.name)
            continue
        cam, variant = p.stem.split("__", 1)
        caps[cam][variant] = read_capture(p)
    if not caps:
        raise SystemExit("no captures in %s (run tools/ue/run_lookdev.ps1 -Profile)" % folder)

    def gpu_total(c):
        return sum(v for k, v in c.items() if k.startswith("GPU/"))

    print("| camera | GPU full (ms) | tessellation cost | grass cost | fire lights cost | frame time full (ms) |")
    print("|---|---:|---:|---:|---:|---:|")
    tess, grass, fire = [], [], []
    for cam, v in caps.items():
        if "full" not in v or "no_grass" not in v:
            continue
        full, ng = gpu_total(v["full"]), gpu_total(v["no_grass"])
        nt = gpu_total(v["no_tess"]) if "no_tess" in v else full  # no variant since tessellation went off (2026-10-10)
        nf = gpu_total(v["no_fire"]) if "no_fire" in v else full  # captures from before the variant
        tess.append(full - nt)
        grass.append(full - ng)
        fire.append(full - nf)
        print("| %s | %.2f | %+.2f | %+.2f | %+.2f | %.2f |" % (cam, full, full - nt, full - ng, full - nf, v["full"].get("FrameTime", 0)))
    if tess:
        print("| **mean** | | **%+.2f** | **%+.2f** | **%+.2f** | |" % (statistics.fmean(tess), statistics.fmean(grass), statistics.fmean(fire)))

    print()
    passes: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    for v in caps.values():
        for variant, c in v.items():
            for k, val in c.items():
                if k.startswith("GPU/"):
                    passes[variant][k[4:]].append(val)
    means = {var: {k: statistics.fmean(vals) for k, vals in p.items()} for var, p in passes.items()}
    top = sorted(means["full"], key=lambda k: -means["full"][k])[: args.top]
    print("| GPU pass (mean over cameras, ms) | " + " | ".join(VARIANTS) + " |")
    print("|---|" + "---:|" * len(VARIANTS))
    for k in top:
        print("| %s | " % k + " | ".join("%.2f" % means.get(var, {}).get(k, 0.0) for var in VARIANTS) + " |")


if __name__ == "__main__":
    main()
