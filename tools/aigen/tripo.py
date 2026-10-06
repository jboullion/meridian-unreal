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

# The Studio settings in use (docs/adr/0007 "Next run"); the card repeats them per variant.
DEFAULT_POLYCOUNT = 4000  # triangles; a manifest's "polycount" overrides it
# manifest "model": how the asset's mesh is made. "hd" (the default): HD Model H3.1. "smart_mesh":
# Studio's Smart Mesh (P2.0, clean low-poly topology, textured afterwards). "custom": no Tripo run;
# the manifest's "custom" model (aigen.py <name> custom <variant>, or a hand-made GLB) is the mesh.
MODELS = ("hd", "smart_mesh", "custom")
STUDIO = {
    "hd": {
        "mode": "HD Model; single image, or the multi-view tab (front/left/back/right slots) for several images",
        "model": "H3.1, Ultra Mesh Quality + AI Complete",
        "texture": "on, 2K, Remove Lighting on, PBR on",
        "mesh": "Triangle topology, polycount below",
        "privacy": "Private",
        "cost": "45 credits",
    },
    "smart_mesh": {
        "mode": "Smart Mesh; single image, or the multi-view tab for several images",
        "model": "P2.0, Triangle; generations: 1 (or up to 4 at different counts to compare)",
        "mesh": "polycount below",
        "privacy": "Private (it resets to Sharing Only when you switch to Smart Mesh)",
        "texture": "afterwards: left bar Texture, 2K, Remove Lighting on (10 credits); same task id",
        "cost": "100 credits a run + 10 per texture",
    },
}
EXPORT = "Send To Blender over the DCC Bridge (aigen.py bridge-collect)"


def model(manifest: dict) -> str:
    """The asset's mesh source: manifest "model", else "hd"."""
    m = manifest.get("model", "hd")
    if m not in MODELS:
        raise SystemExit("%s: model must be one of %s, not %r" % (manifest["name"], ", ".join(MODELS), m))
    return m


def polycount(manifest: dict) -> int:
    """Triangles to ask Tripo for: the manifest's "polycount", else DEFAULT_POLYCOUNT."""
    return int(manifest.get("polycount", DEFAULT_POLYCOUNT))


def retopology(manifest: dict):
    """The optional Retopology step after generation: manifest "retopology" is false/absent (off),
    true (Studio's Retopo tool at the asset's polycount) or {"polycount": n, "topology": "quad"|"triangle"}.
    Returns None or {"polycount", "topology"}. Not applied to any asset yet (docs/adr/0007 "Next run")."""
    r = manifest.get("retopology")
    if not r:
        return None
    r = r if isinstance(r, dict) else {}
    return {"polycount": int(r.get("polycount", polycount(manifest))), "topology": r.get("topology", "triangle")}


def default_variants(manifest: dict) -> dict:
    """The Tripo inputs when a manifest names none: all four views when the original draws its own
    sides and back (manifest "angles", the sprite step), else the front. Named by model: "C"/"MV"
    for HD, "SM"/"SMMV" for Smart Mesh; none for a custom model."""
    kind = model(manifest)
    if kind == "custom":
        return {}
    views = ["front"] + list(manifest.get("angles", {}))
    single, multi = ("C", "MV") if kind == "hd" else ("SM", "SMMV")
    return {multi: {v: "openai" for v in views}} if len(views) > 1 else {single: {"front": "openai"}}


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
             "Polycount: %d triangles%s." % (polycount(manifest), "" if "polycount" in manifest else " (the default)"), "",
             "## Settings (%s)" % model(manifest), ""]
    lines += ["- **%s:** %s" % (k, v) for k, v in STUDIO[model(manifest)].items()]
    lines += ["- **export:** %s" % EXPORT]
    retopo = retopology(manifest)
    if retopo:
        lines += ["", "## Retopology (after generation)", "",
                  "Left bar **Retopo** on the finished model: %s topology, %d polycount. Re-texture if Studio asks;"
                  % (retopo["topology"], retopo["polycount"]),
                  "send the retopologised model, not the raw one, and record it as its own variant (e.g. `C_retopo`)."]
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
