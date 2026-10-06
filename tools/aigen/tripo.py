"""
Image -> 3D step (Tripo). Two backends behind one folder layout (build/aigen/<kind>/<name>/):

    03_tripo_in/<variant>_<view>.png  the images to send, plus SETTINGS.md
    04_tripo_out/<variant>.glb        one model per variant

manual (now): prepare() fills 03_tripo_in and writes the Studio settings card; the user generates in
Tripo Studio (tripo3d.ai, the Max plan's credits) and saves each GLB as 04_tripo_out/<variant>.glb.
api (phase 3): the same variants through the V3 API (https://openapi.tripo3d.com/v3, key
TRIPO_API_KEY; billed from the separate API wallet). V2 and the tripo3d Python SDK stop working
2026-10-31, so only V3 is used. Facts and prices: .claude/skills/sprite-to-3d/reference/tripo.md.
"""
import shutil
from pathlib import Path

# The Studio settings tried first for props (docs/adr/0007); the card repeats them per variant.
STUDIO_DEFAULTS = {
    "mode": "Image to 3D (single image)",
    "model": "latest H model (v3.1) and, as a second try, P2 low-poly",
    "texture": "on, PBR on, texture quality standard",
    "mesh": "smart low-poly / face limit at the manifest's face_target",
    "export": "GLB",
}


def prepare(work: Path, variants: dict, manifest: dict) -> Path:
    """variants {variant: {view: image path}} -> 03_tripo_in/ + SETTINGS.md; returns the folder."""
    out = work / "03_tripo_in"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    (work / "04_tripo_out").mkdir(exist_ok=True)
    lines = ["# Tripo Studio handoff: %s" % manifest["name"], "",
             "Generate one model per variant in Tripo Studio (tripo3d.ai, signed in on the Max plan) and save the GLB as",
             "`%s\\<variant>.glb` (e.g. `A.glb`; add a suffix for retries with other settings, e.g. `A_p2.glb`)." % (work / "04_tripo_out"),
             "Then run `aigen.py %s tripo-ingest`." % manifest["name"], "",
             "Object: %s" % manifest.get("describe", ""),
             "Real height: %.2f m (normalised later; Tripo's size doesn't matter)." % manifest.get("height_m", 0),
             "Face target: %s triangles." % manifest.get("face_target", "?"), "",
             "## Settings to try", ""]
    lines += ["- **%s:** %s" % (k, v) for k, v in STUDIO_DEFAULTS.items()]
    lines += ["", "Multi-view variants (more than one image): use Studio's multi-view mode, with the images in",
              "the slots named by their view (front / left / back / right).", "", "## Variants", ""]
    for variant, views in variants.items():
        names = []
        for view, path in views.items():
            name = "%s_%s.png" % (variant, view)
            shutil.copyfile(path, out / name)
            names.append(name)
        lines.append("- **%s** (%s): %s" % (variant, "single image" if len(views) == 1 else "multi-view", ", ".join(names)))
    lines += ["", "Note in the manifest's tripo.runs which model and settings each GLB used (or tell Claude)."]
    (out / "SETTINGS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return out


def outputs(work: Path) -> list:
    return sorted((work / "04_tripo_out").glob("*.glb"))
