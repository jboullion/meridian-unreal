"""
Where the Server-104 reference checkout lives (git-ignored, reference only).

It sits at ReferenceServers/Server-104 (older clones: Server-104/ at the repo root). A worktree
(.claude/worktrees/<name>) has neither, so the nearest one up the tree (the main checkout's) is used.
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def find_server104() -> Path:
    for d in (ROOT, *ROOT.parents):
        for cand in (d / "ReferenceServers" / "Server-104", d / "Server-104"):
            if cand.is_dir():
                return cand
    return ROOT / "ReferenceServers" / "Server-104"


SERVER104 = find_server104()
