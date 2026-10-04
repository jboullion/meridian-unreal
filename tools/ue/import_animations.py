"""
Import the retargeted Quaternius animation clips (tools/blender/retarget_ual.py output) onto the
UE5 mannequin skeleton. Runs inside the Unreal Editor (tools/ue/import_animations.ps1 wraps it):

    UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript="<repo>/tools/ue/import_animations.py" -unattended -nosplash

Writes (CC0-derived, committed):
  /Game/Characters/Animations/Quaternius/A_<clip>    AnimSequence per clip, in place, 30 fps
                                        /AM_<clip>   AnimMontage (DefaultSlot) for one-shot actions:
                                                     attacks, blocks, hits, deaths, casts, interactions
"""
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLIP_DIR = os.environ.get("MR_ANIM_DIR") or os.path.join(REPO, "art_src", "animations", "quaternius")
DEST = "/Game/Characters/Animations/Quaternius"
SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"

# clips that get a montage (one-shot actions played from gameplay code)
MONTAGE_PREFIXES = ("Sword_", "Punch_", "Melee_", "Shield_", "Spell_Simple_Shoot", "OverhandThrow",
                    "Hit_", "Death", "Interact", "PickUp", "Roll", "Consume", "Chest_Open", "Idle_Shield_Break")

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[import_animations] " + msg)


def set_props(obj, values):
    for prop, value in values.items():
        try:
            obj.set_editor_property(prop, value)
        except Exception:  # noqa: BLE001 - option names vary between engine versions
            log("import option %s not available; skipped" % prop)


def import_clip(fbx, name, skeleton):
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.skeleton = skeleton
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.create_physics_asset = False
    set_props(ui.anim_sequence_import_data, {
        "animation_length": unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
        "import_bone_tracks": True,
        "import_meshes_in_bone_hierarchy": False,
        "remove_redundant_keys": False,
        "use_default_sample_rate": False,
        "custom_sample_rate": 30,
        "convert_scene": True,
    })
    before = set(eal.list_assets(DEST, recursive=False)) if eal.does_directory_exist(DEST) else set()
    target = "%s/%s" % (DEST, name)
    if eal.does_asset_exist(target):
        eal.delete_asset(target)
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    if not eal.does_asset_exist(target):
        # the FBX importer may name a single take "<file>_<take>": find it and rename
        new = [p for p in eal.list_assets(DEST, recursive=False) if p not in before]
        seqs = [p for p in new if isinstance(eal.load_asset(p), unreal.AnimSequence)]
        if len(seqs) != 1:
            raise RuntimeError("import of %s produced %s" % (fbx, new))
        eal.rename_asset(seqs[0], target)
    seq = eal.load_asset(target)
    if not isinstance(seq, unreal.AnimSequence):
        raise RuntimeError("import failed: " + fbx)
    if seq.get_editor_property("skeleton").get_path_name() != skeleton.get_path_name():
        raise RuntimeError("%s imported onto the wrong skeleton" % name)
    seq.set_editor_property("enable_root_motion", False)
    eal.save_loaded_asset(seq)
    return seq


def make_montage(seq, skeleton):
    name = "AM_" + seq.get_name()[2:]
    path = "%s/%s" % (DEST, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    factory = unreal.AnimMontageFactory()
    factory.set_editor_property("target_skeleton", skeleton)
    factory.set_editor_property("source_animation", seq)
    montage = tools.create_asset(name, DEST, unreal.AnimMontage, factory)
    eal.save_loaded_asset(montage)
    return montage


def main():
    try:
        with open(os.path.join(CLIP_DIR, "clips.json"), encoding="utf-8") as f:
            index = json.load(f)
        skeleton = eal.load_asset(SKELETON)
        if not skeleton:
            raise RuntimeError("missing " + SKELETON + " (tools/setup.ps1 copies the engine mannequins)")
        montages = 0
        for clip in index["clips"]:
            seq = import_clip(os.path.join(CLIP_DIR, clip["name"] + ".fbx"), clip["name"], skeleton)
            line = "%-28s %5.2fs" % (clip["name"], seq.get_play_length())
            if clip["name"][2:].startswith(MONTAGE_PREFIXES):
                make_montage(seq, skeleton)
                montages += 1
                line += "  + montage"
            log(line)
        log("done: %d sequences, %d montages in %s" % (len(index["clips"]), montages, DEST))
    except Exception as e:  # noqa: BLE001 - report and fail the wrapper
        log("FAILED: %s" % e)
        raise


main()
