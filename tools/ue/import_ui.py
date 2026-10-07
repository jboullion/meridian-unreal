"""
The in-game UI's art (docs/adr/0009-user-interface.md): import what tools/ui/*.py wrote into
build/ui/ as textures under /Game/Generated/UI. Runs inside the Unreal Editor
(tools/ue/import_ui.ps1, or build_world.ps1 -Script import_ui.py).

    /Game/Generated/UI/Art/T_UI_<piece>     frames, backgrounds, bars, tabs (build/ui/art/<art_variant>/)
    /Game/Generated/UI/Icons/T_Icon_<bgf>   item, spell and skill icons (build/ui/icons/<icon_variant>/)
    /Game/Generated/UI/Minimap/T_Map_<rid>  minimap pictures (build/minimap/final/, tools/ui/minimap.py)

The variants come from data/ui/ui_style.json ("art_variant", "icon_variant") and the minimap
style from data/ui/minimap.json. A file is re-imported only when its content changed (hashes in
Saved/MRBuild/ui_cache.json, keyed by asset and variant).
"""
import hashlib
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STYLE = os.path.join(REPO, "data", "ui", "ui_style.json")
CACHE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "MRBuild", "ui_cache.json")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[import_ui] " + msg)


def file_hash(path):
    return hashlib.sha1(open(path, "rb").read()).hexdigest()


def import_folder(src, dest, prefix, cache, tag, tiles=()):
    if not os.path.isdir(src):
        log("WARNING: %s missing (run tools/ui/*.py)" % src)
        return 0, 0
    files = sorted(f for f in os.listdir(src) if f.lower().endswith(".png") and f.startswith(prefix))
    stale = []
    for f in files:
        name = os.path.splitext(f)[0]
        key = "%s/%s" % (dest, name)
        h = tag + ":" + file_hash(os.path.join(src, f))
        if not eal.does_asset_exist(key) or cache.get(key) != h:
            stale.append((f, name, key, h))
    tasks = []
    for f, name, key, h in stale:
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src, f)
        t.destination_path = dest
        t.automated = True
        t.replace_existing = True
        t.save = False
        tasks.append(t)
    if tasks:
        asset_tools.import_asset_tasks(tasks)
    for f, name, key, h in stale:
        tex = eal.load_asset(key)
        if not tex:
            log("WARNING: %s did not import" % key)
            continue
        tile = any(name.endswith(s) or s in name for s in tiles)
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)  # RGBA, uncompressed
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        # drawn at about half the art's resolution (4x art at ui_scale 2): mips keep it clean
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
        address = unreal.TextureAddress.TA_WRAP if tile else unreal.TextureAddress.TA_CLAMP
        tex.set_editor_property("address_x", address)
        tex.set_editor_property("address_y", address)
        tex.set_editor_property("never_stream", True)
        eal.save_loaded_asset(tex)
        cache[key] = h
    return len(files), len(stale)


def main():
    style = json.load(open(STYLE, encoding="utf-8"))
    cache = json.load(open(CACHE)) if os.path.exists(CACHE) else {}
    art = style.get("art_variant", "nearest")
    icons = style.get("icon_variant", art)
    # the AI variants need build/texai (tools/textures/setup_ai.ps1); nearest always builds
    if not os.path.isdir(os.path.join(REPO, "build", "ui", "art", art)):
        log("%s art not built; using nearest" % art)
        art = "nearest"
    if not os.path.isdir(os.path.join(REPO, "build", "ui", "icons", icons)):
        log("%s icons not built; using nearest" % icons)
        icons = "nearest"
    # tiled pieces: backgrounds and every frame's repeaters
    tiles = ("bkgnd", "_top", "_bottom", "_left", "_right", "bar_top", "bar_bottom")
    n, k = import_folder(os.path.join(REPO, "build", "ui", "art", art), "/Game/Generated/UI/Art", "T_UI_", cache, art, tiles)
    log("art (%s): %d pieces, %d imported" % (art, n, k))
    n, k = import_folder(os.path.join(REPO, "build", "ui", "icons", icons), "/Game/Generated/UI/Icons", "T_Icon_", cache, icons)
    log("icons (%s): %d, %d imported" % (icons, n, k))
    n, k = import_folder(os.path.join(REPO, "build", "minimap", "final"), "/Game/Generated/UI/Minimap", "T_Map_", cache, "map")
    log("minimaps: %d, %d imported" % (n, k))
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    json.dump(cache, open(CACHE, "w"), indent=1)


main()
