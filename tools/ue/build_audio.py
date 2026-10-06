"""
The original's sounds in the editor (docs/adr/0006 phase 1a). Runs inside the Unreal Editor, from
tools/ue/build_world.py (or on its own: build_world.ps1 -Script build_audio.py).

Every sound in data/audio/sounds.json (tools/audio/extract_audio.py) is copied from the original
client's resources into build/audio/original (raw assets never enter git) and imported to
/Game/Generated/Audio/Original/<name> (the file name without .ogg, characters other than letters,
digits and _ replaced by _; UMRAudioSubsystem resolves names the same way). Sounds used as loops
loop. Each gets the sound class of its category, under the classes the settings drive:

  SC_Music                  music volume, music on/off
  SC_Sound                  sound volume, sound on/off
    SC_Ambience
      SC_Loops              loop sounds on/off
      SC_Periodic           random sounds on/off
      SC_Weather
    SC_Effects (SC_Combat, SC_Spells, SC_World)
    SC_UI

and SM_Settings, the sound mix UMRAudioSubsystem sets their volumes with. Re-imported only when
the file changes.
"""
import json
import os
import re
import shutil

import unreal

import build_cache
from build_cache import file_digest, source

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SOUNDS = os.path.join(REPO, "data", "audio", "sounds.json")
AUDIO_BUILD = os.path.join(REPO, "build", "audio", "original")
AUDIO_DIR = "/Game/Generated/Audio/Original"
MIX_DIR = "/Game/Generated/Audio/Mix"
OLD_DIRS = ["/Game/Generated/Audio/Weather"]  # phase 4's weather sounds, now under Original
# the original client's resources (as tools/bgf2png/bgf2png.py looks for them)
CLIENT_RES = [os.path.join(os.environ.get("LOCALAPPDATA", ""), "Meridian-104", "resource"),
              r"H:\Steam\steamapps\common\Meridian 59\resource"]

# sound class tree: name -> parent
CLASSES = {"SC_Music": None, "SC_Sound": None, "SC_Ambience": "SC_Sound", "SC_Loops": "SC_Ambience",
           "SC_Periodic": "SC_Ambience", "SC_Weather": "SC_Ambience", "SC_Effects": "SC_Sound",
           "SC_Combat": "SC_Effects", "SC_Spells": "SC_Effects", "SC_World": "SC_Effects", "SC_UI": "SC_Sound"}
CATEGORY_CLASS = {"music": "SC_Music", "loop": "SC_Loops", "periodic": "SC_Periodic", "weather": "SC_Weather",
                  "combat": "SC_Combat", "spell": "SC_Spells", "world": "SC_World", "ui": "SC_UI"}

eal = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[build_audio] " + msg)


def asset_name(file_name):
    """'Rs_wind.ogg' -> 'Rs_wind' (UMRAudioSubsystem::AssetName does the same)."""
    return re.sub(r"[^A-Za-z0-9_]", "_", os.path.splitext(file_name)[0])


def _create(name, folder, cls, factory):
    path = "%s/%s" % (folder, name)
    if eal.does_asset_exist(path):
        return eal.load_asset(path), False
    return asset_tools.create_asset(name, folder, cls, factory), True


def ensure_mix():
    """The sound classes and SM_Settings -> {class name: asset path}."""
    made = {}
    for name in CLASSES:
        asset, _ = _create(name, MIX_DIR, unreal.SoundClass, unreal.SoundClassFactory())
        made[name] = asset
    for name, parent in CLASSES.items():
        if not parent:
            continue
        p, c = made[parent], made[name]
        children = list(p.get_editor_property("child_classes"))
        if c not in children:
            p.set_editor_property("child_classes", children + [c])
        try:
            c.set_editor_property("parent_class", p)
        except Exception:
            pass
    for asset in made.values():
        eal.save_loaded_asset(asset)
    mix, _ = _create("SM_Settings", MIX_DIR, unreal.SoundMix, unreal.SoundMixFactory())
    eal.save_loaded_asset(mix)
    return {name: "%s/%s" % (MIX_DIR, name) for name in made}


def find_original(file_name):
    return next((os.path.join(d, f) for d in CLIENT_RES if os.path.isdir(d)
                 for f in os.listdir(d) if f.lower() == file_name.lower()), None)


def build_audio():
    """Import every sound of data/audio/sounds.json; returns the number imported this run."""
    if not os.path.exists(SOUNDS):
        log("no %s (run python tools/audio/extract_audio.py)" % SOUNDS)
        return 0
    for old in OLD_DIRS:
        if eal.does_directory_exist(old):
            eal.delete_directory(old)
            log("removed %s" % old)
    classes = ensure_mix()
    sounds = json.load(open(SOUNDS, encoding="utf-8"))["sounds"]
    cache = build_cache.CACHE
    os.makedirs(AUDIO_BUILD, exist_ok=True)
    imported, missing = 0, []
    for entry in sounds.values():
        src = None if entry.get("missing") else find_original(entry["file"])
        if not src:
            missing.append(entry["file"])
            continue
        name = asset_name(entry["file"])
        copy = os.path.join(AUDIO_BUILD, os.path.basename(src))
        if not os.path.exists(copy) or file_digest(copy) != file_digest(src):
            shutil.copyfile(src, copy)
        path = "%s/%s" % (AUDIO_DIR, name)
        sound_class = classes[CATEGORY_CLASS.get(entry["category"], "SC_World")]
        k = cache.key(file_digest(copy), entry["loop"], sound_class, source(build_audio, asset_name))
        if cache.fresh(path, k, exists=lambda: eal.does_asset_exist(path)):
            cache.done(path, k, "sounds", False)
            continue
        task = unreal.AssetImportTask()
        task.filename = copy
        task.destination_path = AUDIO_DIR
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        asset_tools.import_asset_tasks([task])
        sound = eal.load_asset(path)
        if not sound:
            log("WARNING: %s did not import" % copy)
            continue
        sound.set_editor_property("looping", bool(entry["loop"]))
        sound.set_editor_property("sound_class_object", eal.load_asset(sound_class))
        eal.save_loaded_asset(sound)
        cache.done(path, k, "sounds", True)
        imported += 1
    if missing:
        log("WARNING: %d sounds not in the original client's resources (%s): %s"
            % (len(missing), ", ".join(CLIENT_RES), ", ".join(sorted(missing)[:20])))
    log("%d sounds, %d imported this run" % (len(sounds) - len(missing), imported))
    return imported


if __name__ == "__main__":
    build_cache.begin()
    try:
        build_audio()
    finally:
        build_cache.CACHE.save()
