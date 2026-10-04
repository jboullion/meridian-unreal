"""
Export the engine mannequin (SKM_Manny_Simple, skeleton SK_Mannequin) to FBX so Blender tools can
fit other bodies onto the exact same skeleton. Runs inside the Unreal Editor:

    UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript="<repo>/tools/ue/export_mannequin.py" -unattended -nosplash -RenderOffscreen

Writes build/mannequin/SKM_Manny_Simple.fbx and SKM_Quinn_Simple.fbx (git-ignored; engine
content). Both share the SK_Mannequin skeleton; Quinn has female proportions, so female MakeHuman
bodies are fitted to Quinn's joints and male ones to Manny's.
"""
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_DIR = os.path.join(REPO, "build", "mannequin")
MESHES = ["SKM_Manny_Simple", "SKM_Quinn_Simple"]


def export(name):
    out = os.path.join(OUT_DIR, name + ".fbx")
    path = "/Game/Characters/Mannequins/Meshes/" + name
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        raise RuntimeError("mannequin pack missing (run tools/setup.ps1)")
    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = out
    task.exporter = unreal.SkeletalMeshExporterFBX()
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    options = unreal.FbxExportOption()
    options.ascii = False
    options.export_morph_targets = False
    task.options = options
    ok = unreal.Exporter.run_asset_export_task(task)
    unreal.log("[export_mannequin] %s -> %s (%s)" % (path, out, "ok" if ok and os.path.exists(out) else "FAILED"))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name in MESHES:
        export(name)


try:
    main()
except Exception as e:  # noqa: BLE001
    unreal.log_error("[export_mannequin] FAILED: %s" % e)
finally:
    unreal.SystemLibrary.quit_editor()
