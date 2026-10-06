"""
Build the streamed world from the zone blockouts. Runs inside the Unreal Editor (Python). Use
tools/ue/build_world.ps1, which runs it in the editor you have open on the project (remote
execution, tools/ue/run_in_editor.py) or else starts a headless one:

    UnrealEditor-Cmd.exe <project>.uproject /Engine/Maps/Entry -ExecutePythonScript="<repo>/tools/ue/build_world.py" -unattended -nosplash -RenderOffscreen

(It needs a real editor world, so it runs as -ExecutePythonScript rather than the pythonscript
commandlet. Started that way it quits the editor when it finishes.)

Incremental: every asset and level below is stored with a hash of its inputs (tools/ue/build_cache.py)
and rebuilt, in place, only when those change. Editing one building re-imports that building's mesh
and nothing else; the levels that place it don't change, so an open editor shows the new mesh at once.
`--clean` (build_world.ps1 -Clean) deletes everything below and builds it from scratch.

Output (everything under /Game/Generated, git-ignored):
  /Game/Generated/Zones/Z<rid>/...          imported blockout mesh + materials (complex-as-simple collision).
                                            Zones with art (data/environment/zone_<rid>.json + the meshes
                                            tools/blender/build_zone_art.py wrote to build/environment/zone_<rid>/)
                                            get three parts instead: the full blockout as hidden collision,
                                            the blockout without the rebuilt buildings for rendering, and
                                            the art meshes (Nanite, no collision) under Z<rid>/Art,
                                            plus ground scatter (grass tufts, AMRScatterActor) from the
                                            zone's "scatter" rules and the meshes in build/environment/kit/.
  /Game/Generated/Environment/...           placeholder textures + material instances per original
                                            texture, assigned to the zone meshes (environment_materials.py)
  /Game/Generated/Maps/Zones/L_Zone_<rid>   one streaming sublevel per zone *geometry*: the zone mesh
                                            at its world_origin_cm (data/zone_layout.json). Zones that
                                            share geometry (Raza town + Outskirts) share one sublevel,
                                            named after the zone that owns the geometry.
  /Game/Generated/Maps/L_World              persistent level: sun, sky, fog, post process, and every
                                            zone sublevel registered as a dynamic streaming level that
                                            is NOT loaded at startup. At runtime the server loads all
                                            of them and each client streams its zone + neighbours
                                            (UMRZoneSubsystem / AMRPlayerController).
  Fires (docs/adr/0005 phase 3)             AMRFireActor per wall torch (tools/environment/fires.py),
                                            brazier and candle (props.json "fire"), and per original
                                            light that flickers: a flame sprite (MI_Fire_<preset>) and
                                            a light that UMRFireSubsystem flickers in game.
"""
import importlib
import json
import os
import shutil
import sys
import time
import zlib

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
for _p in (os.path.dirname(os.path.abspath(__file__)), os.path.join(REPO, "tools", "environment")):
    if _p not in sys.path:
        sys.path.insert(0, _p)
# an editor that stays open (run_in_editor.py) keeps modules from the last run; pick up edits
for _m in ("build_cache", "blockout", "scatter", "fires", "chimneys", "zone_mood", "environment_materials", "build_audio"):
    if _m in sys.modules:
        importlib.reload(sys.modules[_m])
import blockout  # noqa: E402
import build_audio  # noqa: E402
import build_cache  # noqa: E402
import chimneys  # noqa: E402
import fires  # noqa: E402
import scatter  # noqa: E402
from build_cache import file_digest, source  # noqa: E402
from environment_materials import ENV as ENVIRONMENT_DIR, ZoneMaterials, assign_materials  # noqa: E402
from zone_mood import MOODS, apply_level_mood  # noqa: E402

LAYOUT = os.path.join(REPO, "data", "zone_layout.json")
PROPS = os.path.join(REPO, "data", "environment", "props.json")
GENERATED = "/Game/Generated"
WORLD_PATH = GENERATED + "/Maps/L_World"
ZONE_LEVEL_DIR = GENERATED + "/Maps/Zones"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def log(msg):
    unreal.log("[build_world] " + msg)


def zone_level_path(zone):
    """Must match UMRZoneSubsystem's naming: L_Zone_<rid>_<Kod class> of the zone owning the geometry,
    e.g. L_Zone_307_RazaBar (the original's room id and room class, so the Levels window says what
    each one is)."""
    return "%s/L_Zone_%d_%s" % (ZONE_LEVEL_DIR, zone["rid"], zone["class"])


def remove_stale_levels(zone_levels):
    """Delete zone levels this build no longer makes (renamed or removed zones; the old
    L_Zone_<rid> names). Run after L_World has been rebuilt to stream only zone_levels."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    keep = {p.split(".")[0] for p in zone_levels}
    for data in registry.get_assets_by_path(ZONE_LEVEL_DIR, recursive=False):
        path = str(data.package_name)
        if path not in keep and str(data.asset_class_path.asset_name) == "World":
            eal.delete_asset(path)
            # a world package can stay on disk after delete_asset in a headless editor; it's ours
            # and git-ignored, and nothing streams it any more
            disk = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()),
                                path[len("/Game/"):] + ".umap")
            if os.path.exists(disk):
                os.remove(disk)
            log("removed old zone level %s" % path)


def reset_generated():
    """--clean in a running editor: switch to a blank map so nothing this script made is in use,
    then delete what it owns (the maps, zone meshes and zone materials; other generated content
    under /Game/Generated is left alone). Slow (the editor gathers references for every asset it
    deletes); build_world.ps1 -Clean deletes the files before a headless editor starts instead."""
    world = editor_sub.get_editor_world()
    if world:
        for level in unreal.EditorLevelUtils.get_levels(world)[1:]:
            unreal.EditorLevelUtils.remove_level_from_world(level)
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    unreal.SystemLibrary.collect_garbage()
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    # Maps: remove the files and rescan instead of delete_directory(), which loads each world to
    # force-delete it and leaves the last one referenced in memory (new_level() then fails on it).
    maps_disk = os.path.join(content, "Generated", "Maps")
    if os.path.isdir(maps_disk):
        shutil.rmtree(maps_disk)
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([GENERATED + "/Maps"], True)
    for folder in (GENERATED + "/Maps", GENERATED + "/Zones", ENVIRONMENT_DIR):
        if eal.does_directory_exist(folder):
            eal.delete_directory(folder)
        # Package files the asset registry doesn't know about (e.g. from an interrupted run) survive
        # delete_directory; they're ours and git-ignored, so remove them from disk. A file still open
        # in another editor makes rmtree fail, which is the error we want.
        disk = os.path.join(content, folder[len("/Game/"):])
        if os.path.isdir(disk):
            try:
                shutil.rmtree(disk)
            except OSError as e:
                raise RuntimeError("could not delete %s (is it open in another editor?): %s" % (folder, e))
    unreal.SystemLibrary.collect_garbage()


def _static_meshes(folder):
    """StaticMesh package paths in a content folder, from the asset registry (nothing is loaded)."""
    out = []
    for p in eal.list_assets(folder, recursive=True, include_folder=False):
        if str(eal.find_asset_data(p).asset_class_path.asset_name) == "StaticMesh":
            out.append(p.split(".")[0])
    return out


def import_zone_mesh(glb_path, dest_dir, nanite=False, collision=True):
    """-> StaticMesh asset path. Re-imports (in place) only when the glb or these settings changed."""
    cache = build_cache.CACHE
    key = cache.key(file_digest(glb_path), nanite, collision, source(import_zone_mesh))
    mesh = cache.info(dest_dir).get("mesh")
    if mesh and cache.fresh(dest_dir, key, exists=lambda: eal.does_asset_exist(mesh)):
        cache.done(dest_dir, key, "meshes", False)
        two_sided_distance_field(mesh)
        return mesh
    task = unreal.AssetImportTask()
    task.filename = glb_path
    task.destination_path = dest_dir
    task.automated = True
    task.replace_existing = True
    task.save = True
    asset_tools.import_asset_tasks([task])
    meshes = _static_meshes(dest_dir)
    if not meshes:
        raise RuntimeError("no static mesh imported from " + glb_path)
    mesh = meshes[0]
    obj = eal.load_asset(mesh)
    # The importer enables Nanite, but its glTF materials lack the Nanite usage flag, so a
    # standalone game would render them with the default grey material. Blockouts are small
    # and temporary; plain static meshes are fine. Art meshes get our own materials (which have
    # the flag) and need Nanite for displacement.
    settings = obj.get_editor_property("nanite_settings")
    settings.set_editor_property("enabled", nanite)
    obj.set_editor_property("nanite_settings", settings)
    body = obj.get_editor_property("body_setup")
    if body:
        body.set_editor_property("collision_trace_flag",
                                 unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE if collision
                                 else unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    eal.save_loaded_asset(obj)
    cache.forget(mesh + "#materials")  # the import put the glTF materials back
    cache.done(dest_dir, key, "meshes", True, mesh=mesh)
    log("imported %s" % mesh)
    two_sided_distance_field(mesh)
    return mesh


def two_sided_distance_field(mesh):
    """Build the mesh's distance field as if two-sided. The zone walls are single-sided planes, and
    their one-sided distance fields put the wall's own surface "inside", so Lumen's world-space
    rays from it start occluded: unlit walls got no sky light at all, only what screen traces
    picked up from the sky on screen, and went black as the camera came close (2026-10-06,
    build/lookdev/wall_ef.png). Set on the existing mesh (no re-import), once."""
    cache = build_cache.CACHE
    record = mesh + "#distance_field"
    key = cache.key(source(two_sided_distance_field), deps=False)
    if cache.fresh(record, key, exists=lambda: True):
        cache.done(record, key, "distance fields", False)
        return
    obj = eal.load_asset(mesh)
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    settings = sub.get_lod_build_settings(obj, 0)
    if not settings.get_editor_property("generate_distance_field_as_if_two_sided"):
        settings.set_editor_property("generate_distance_field_as_if_two_sided", True)
        sub.set_lod_build_settings(obj, 0, settings)
        eal.save_loaded_asset(obj)
    cache.done(record, key, "distance fields", True)


def check_orientation(zone, mesh):
    """The importer must map glTF (x east, y up, z south) to UE (X east, Y south, Z up)."""
    lo, hi = zone["bounds_m"]["min"], zone["bounds_m"]["max"]
    ex0, ey0, ex1, ey1 = lo[0] * 100, lo[2] * 100, hi[0] * 100, hi[2] * 100
    b = eal.load_asset(mesh).get_bounding_box()
    ok = all(abs(a - e) < 50 for a, e in ((b.min.x, ex0), (b.max.x, ex1), (b.min.y, ey0), (b.max.y, ey1)))
    if not ok:
        log("zone %d %-14s bounds X[%.0f..%.0f] Y[%.0f..%.0f] ORIENTATION MISMATCH (expected X[%.0f..%.0f] Y[%.0f..%.0f])"
            % (zone["rid"], zone["class"], b.min.x, b.max.x, b.min.y, b.max.y, ex0, ex1, ey0, ey1))
    return ok


def zone_art(zone):
    """-> (config, manifest) when the zone has rebuilt art, else None."""
    config = blockout.zone_art_config(zone["rid"])
    manifest = os.path.join(REPO, "build", "environment", "zone_%d" % zone["rid"], "manifest.json")
    if not config:
        return None
    if not os.path.exists(manifest):
        log("WARNING: zone %d has data/environment/zone_%d.json but no art; run tools/blender/build_zone_art.py"
            % (zone["rid"], zone["rid"]))
        return None
    return config, json.load(open(manifest, encoding="utf-8"))


def write_render_blockout(glb, render_glb, config):
    """The blockout minus the triangles the zone's art replaces (skipped when its inputs are unchanged)."""
    cache = build_cache.CACHE
    key = cache.key(file_digest(glb), config, file_digest(blockout.__file__))
    if cache.fresh(render_glb, key, exists=lambda: os.path.exists(render_glb)):
        cache.done(render_glb, key, "render blockouts", False)
        return cache.info(render_glb).get("hidden", 0)
    hidden = blockout.write_render_blockout(glb, render_glb, blockout.all_building_triangles(blockout.read_glb(glb), config))
    cache.done(render_glb, key, "render blockouts", True, hidden=hidden)
    return hidden


def import_zone_parts(zone, materials):
    """-> [(mesh path, label, role)], role "geometry" (render + collision), "collision" (hidden),
    "render" (no collision), "art" (Nanite, no collision) or "decal" (mesh decals: not Nanite, no
    collision, no shadows)."""
    rid, cls = zone["rid"], zone["class"]
    base = "%s/Zones/Z%d" % (GENERATED, rid)
    glb = os.path.join(REPO, zone["mesh"])
    art = zone_art(zone)
    if not art:
        mesh = import_zone_mesh(glb, base)
        counts = materials.apply(mesh)
        return [(mesh, "ZoneGeometry_%d_%s" % (rid, cls), "geometry")], "materials %s" % counts

    config, manifest = art
    art_dir = os.path.join(REPO, "build", "environment", "zone_%d" % rid)
    render_glb = os.path.join(art_dir, "blockout_render.glb")
    hidden = write_render_blockout(glb, render_glb, config)
    collision = import_zone_mesh(glb, base + "/Collision")
    render = import_zone_mesh(render_glb, base + "/Render", collision=False)
    materials.apply(render)
    parts = [(collision, "ZoneCollision_%d_%s" % (rid, cls), "collision"),
             (render, "ZoneGeometry_%d_%s" % (rid, cls), "render")]
    for m in manifest["meshes"]:
        mesh = import_zone_mesh(os.path.join(art_dir, m["file"]), "%s/Art/%s" % (base, m["name"]),
                                nanite=not (m.get("water") or m.get("decal")), collision=False)
        materials.apply(mesh, art=True, flat=m.get("displacement", "runtime") != "runtime")
        parts.append((mesh, "ZoneArt_%d_%s" % (rid, m["building"]), "decal" if m.get("decal") else "art"))
        if m.get("water") and materials.ice:
            # the same surface just above the water, in ice that shows only in snow (M_Ice)
            parts.append((mesh, "ZoneIce_%d_%s" % (rid, m["building"]), "ice|" + materials.ice))
    return parts, "%d art meshes, %d blockout triangles hidden under art" % (len(manifest["meshes"]), hidden)


def prune_art(zone, parts):
    """Delete art meshes of buildings no longer in the zone's manifest (after the level stopped using them)."""
    folder = "%s/Zones/Z%d/Art" % (GENERATED, zone["rid"])
    if not eal.does_directory_exist(folder):
        return
    keep = {p[0].split("/Art/")[1].split("/")[0] for p in parts if p[2] in ("art", "decal")}
    gone = {p.split("/Art/")[1].split("/")[0] for p in eal.list_assets(folder, recursive=True, include_folder=False)} - keep
    for name in sorted(gone):
        log("zone %d: removing art %s (no longer in the manifest)" % (zone["rid"], name))
        eal.delete_directory("%s/%s" % (folder, name))
        build_cache.CACHE.forget("%s/%s" % (folder, name))


KIT_DIR = os.path.join(REPO, "build", "environment", "kit")
_kit = {}


def import_kit_mesh(name, materials, slot_materials=None):
    """A kit mesh from build/environment/kit/<name>.glb. Scatter meshes get M_Grass; props get their
    slots' materials from props.json (`slot_materials`)."""
    if name in _kit:
        return _kit[name]
    path = os.path.join(KIT_DIR, name + ".glb")
    if not os.path.exists(path):
        log("WARNING: %s missing; run tools/blender/build_grass_kit.py" % path)
        return None
    mesh = import_zone_mesh(path, "%s/Kit/%s" % (ENVIRONMENT_DIR, name), collision=False)
    if slot_materials is None:
        assign_materials(mesh, lambda slot: (materials.grass, 1) if materials.grass else (None, 2))
    else:
        assign_materials(mesh, lambda slot: (slot_materials[slot], 1) if slot_materials.get(slot) else (None, 2))
    _kit[name] = mesh
    return mesh


# Kod light intensity (0-255) -> candela and reach: a lamp (Kod 50) is 12 cd and reaches 11.5 m,
# a brazier (40) about 10 cd (docs/adr/0005)
KOD_CANDELA_PER_UNIT = 0.24
KOD_RADIUS_M = (4.0, 0.15)  # base + per unit


def kod_light(light, params):
    """A props.json light with "kod_intensity" / "kod_color" (the class's Kod defaults; the object's
    own iIntensity / iColor params win) -> candela, radius and colour (15-bit Kod RGB -> sRGB)."""
    params = params or {}
    intensity = float(params.get("iIntensity", light["kod_intensity"]))
    color = int(params.get("iColor", light.get("kod_color", 0x7FFF)))
    out = dict(light)
    out["candela"] = intensity * KOD_CANDELA_PER_UNIT * float(light.get("candela_scale", 1.0))
    out["radius_m"] = KOD_RADIUS_M[0] + KOD_RADIUS_M[1] * intensity
    # Kod's 15-bit colours are fully saturated (fire: 31, 24, 6); 40% towards white reads as firelight
    out["color"] = [round(((color >> s) & 31) * 255 / 31 * 0.6 + 255 * 0.4) for s in (10, 5, 0)]
    return out


def fire_spec(materials, preset, centre_cm):
    """A flame for AMRFireActor: {"material", "size_cm": [w, h], "centre_cm"} from the preset's
    flipbook (make_placeholders.py), or None when the flipbooks haven't been made."""
    made = materials.fires.get(preset)
    if not made:
        log("WARNING: no flame flipbook %r (run tools/textures/make_placeholders.py)" % preset)
        return None
    mi, entry = made
    return {"material": mi, "size_cm": [v * 100.0 for v in entry["size_m"]], "centre_cm": centre_cm}


def zone_props(zone, materials, prop_materials):
    """-> [(label, mesh path or None, [x, y, z] cm, light config or None, fire or None)] for the zone's
    Kod objects whose class is in data/environment/props.json. A light with "flicker" (true, or
    "sector" in the original's flickering sectors) and a class with a "fire" become AMRFireActors."""
    if not os.path.exists(PROPS):
        return []
    classes = json.load(open(PROPS, encoding="utf-8")).get("classes", {})
    out = []
    for i, obj in enumerate(zone.get("objects", [])):
        cfg = classes.get(obj["class"])
        if not cfg:
            continue
        x, y, z = obj["pos"]
        mesh = import_kit_mesh(cfg["mesh"], materials, prop_materials) if cfg.get("mesh") else None
        light = cfg.get("light")
        if light and "kod_intensity" in light:
            light = kod_light(light, obj.get("params"))
        if light and "flicker" in light:
            light = dict(light, flicker=light["flicker"] is True or (light["flicker"] == "sector" and bool(obj.get("flicker"))))
        fire = None
        if cfg.get("fire"):
            preset = cfg["fire"]["preset"]
            entry = materials.fires.get(preset, (None, {}))[1]
            fire = fire_spec(materials, preset, (cfg["fire"].get("base_m", 0.0) + entry.get("flame_m", [0, 0])[1] / 2) * 100.0)
        out.append(("Prop_%d_%s_%d" % (zone["rid"], obj["class"], i), mesh, [x * 100.0, z * 100.0, y * 100.0], light, fire))
    return out


TORCH_LIGHT_MERGE_CM = 100.0


def merge_torch_lights(props, wall_fires):
    """The original lit its wall torches with a room light (DynamicLight) placed under each flame,
    so a torch and that light stood about half a metre apart: two lights per torch (2026-10-06).
    A DynamicLight within TORCH_LIGHT_MERGE_CM (in plan) of a torch flame is dropped, and its light
    (the original's strength and colour) moves onto the flame. -> (props, wall fires)."""
    kept, used = [], set()
    merged = []
    for label, mesh, pos, light, fire in wall_fires:
        best, best_d = None, TORCH_LIGHT_MERGE_CM
        for i, (plabel, pmesh, ppos, plight, pfire) in enumerate(props):
            if i in used or not plight or pmesh or pfire or "_DynamicLight_" not in plabel:
                continue
            d = ((ppos[0] - pos[0]) ** 2 + (ppos[1] - pos[1]) ** 2) ** 0.5
            if d < best_d:
                best, best_d = i, d
        if best is not None:
            used.add(best)
            room = props[best][3]
            light = dict(light or {}, candela=room["candela"], radius_m=room["radius_m"], offset_m=0.0, flicker=True)
            if "color" in room:
                light["color"] = room["color"]
        merged.append((label, mesh, pos, light, fire))
    kept = [p for i, p in enumerate(props) if i not in used]
    if used:
        log("%d torch room lights moved onto their flames" % len(used))
    return kept, merged


def zone_wall_fires(zone, materials):
    """-> [(label, None, [x, y, z] cm, light, fire)] for the wall torches the blockout draws
    (props.json "fires" presets with "walls", found by tools/environment/fires.py)."""
    presets = fires.load_presets()
    if not any(p.get("walls") for p in presets.values()):
        return []
    out = []
    for i, f in enumerate(fires.wall_flames(blockout.read_glb(os.path.join(REPO, zone["mesh"])), presets)):
        x, y, z = f["pos"]
        light = presets[f["preset"]].get("light")
        if light and "kod_intensity" in light:
            light = dict(kod_light(light, None), offset_m=0.0)
        fire = fire_spec(materials, f["preset"], 0.0)
        out.append(("Fire_%d_%s_%d" % (zone["rid"], f["preset"], i), None, [x * 100.0, z * 100.0, y * 100.0], light, fire))
    if out:
        log("zone %d: %d wall flames" % (zone["rid"], len(out)))
    return out


# extent of an effect's actor (SM_Puffs is a 1 m box standing on its origin): its bounds; the
# material moves the quads anywhere inside, and a plume bends further downwind in a storm
EFFECT_SCALE = {"smoke": (12.0, 12.0, 10.0), "moth": (1.6, 1.6, 1.6)}
EFFECT_BOUNDS = {"smoke": 1.6, "moth": 1.0}


def zone_effects(zone, materials):
    """-> [(label, kind, [x, y, z] cm)] for the zone's atmosphere (docs/adr/0005 phase 5): a smoke
    plume on every chimney top (props.json "smoke", tools/environment/chimneys.py) and moths around
    the lights of classes with "moths" (props.json "classes")."""
    atmo = materials.atmosphere
    if not atmo or "SM_Puffs" not in _kit:
        return []
    out = []
    for i, (x, y, z) in enumerate(chimneys.chimney_tops(blockout.read_glb(os.path.join(REPO, zone["mesh"])))):
        out.append(("Smoke_%d_%d" % (zone["rid"], i), "smoke", [x * 100.0, z * 100.0, y * 100.0]))
    classes = json.load(open(PROPS, encoding="utf-8")).get("classes", {}) if os.path.exists(PROPS) else {}
    for i, obj in enumerate(zone.get("objects", [])):
        moths = classes.get(obj["class"], {}).get("moths")
        if moths:
            x, y, z = obj["pos"]
            out.append(("Moths_%d_%s_%d" % (zone["rid"], obj["class"], i), "moth",
                        [x * 100.0, z * 100.0, y * 100.0 + float(moths.get("offset_m", 2.0)) * 100.0]))
    if out:
        log("zone %d: %d chimney plumes, %d lamps with moths" % (zone["rid"], sum(1 for e in out if e[1] == "smoke"),
                                                                 sum(1 for e in out if e[1] == "moth")))
    return out


def _srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def spawn_fire(label, loc, light, fire, zone_rid):
    """An AMRFireActor at the flame's centre (or the light, without a flame)."""
    centre = fire["centre_cm"] if fire else light["offset_m"] * 100.0
    a = actors.spawn_actor_from_class(unreal.MRFireActor, loc + unreal.Vector(0, 0, centre))
    a.set_actor_label(label + "_Fire")
    if fire:
        w, h = fire["size_cm"]
        a.set_flame(eal.load_asset(fire["material"]), w, h)
    if light:
        r, g, b = light.get("color") or [255, 170, 90]
        a.set_light(float(light["candela"]), float(light["radius_m"]) * 100.0,
                    unreal.LinearColor(_srgb_to_linear(r), _srgb_to_linear(g), _srgb_to_linear(b), 1.0),
                    unreal.Vector(0, 0, light.get("offset_m", 0.0) * 100.0 - centre),
                    float(light.get("source_radius_cm", 5)), bool(light.get("flicker", True)),
                    bool(light.get("shadows", True)), zlib.crc32(label.encode()) & 0x7FFFFFFF)
    a.tags = [unreal.Name("ZoneFire"), unreal.Name("Zone%d" % zone_rid)]
    return a


def zone_scatter(zone, materials):
    """-> (inputs, compute): JSON-able inputs of the zone's ground scatter (data/environment/zone_<rid>.json
    "scatter"), and compute() -> [(label, mesh, [Transform], rule)], run only when a level needs them."""
    config = blockout.zone_art_config(zone["rid"])
    rules = (config or {}).get("scatter", []) if hasattr(unreal, "MRScatterActor") else []
    if not rules:
        return None, lambda: []
    glb = os.path.join(REPO, zone["mesh"])
    meshes = {name: import_kit_mesh(name, materials) for rule in rules for name in rule["meshes"]}
    inputs = [file_digest(glb), rules, meshes, file_digest(scatter.__file__)]

    def compute():
        prims = blockout.read_glb(glb)
        out = []
        for rule in rules:
            for name, points in scatter.scatter(prims, rule).items():
                if not meshes.get(name) or not points:
                    continue
                transforms = [unreal.Transform(location=unreal.Vector(x * 100.0, z * 100.0, y * 100.0),
                                               rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw),
                                               scale=unreal.Vector(sc, sc, sc))
                              for x, y, z, yaw, sc in points]
                out.append(("Scatter_%d_%s_%s" % (zone["rid"], rule["name"], name), meshes[name], transforms, rule))
                log("zone %d scatter %s/%s: %d instances" % (zone["rid"], rule["name"], name, len(points)))
        return out
    return inputs, compute


class MapSwitch:
    """Rebuilding a level means opening it. The first time, leave whatever map the editor has open
    (discarding unsaved changes: our maps are generated) so the old world lets go of the levels;
    restore() reopens it at the end when it was one of ours or any other project map."""

    def __init__(self):
        world = editor_sub.get_editor_world()
        self.start = world.get_path_name().split(".")[0] if world else None
        self.left = False

    def leave(self):
        if not self.left:
            unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
            unreal.SystemLibrary.collect_garbage()
            self.left = True

    def restore(self):
        if not (self.left and self.start and self.start.startswith("/Game/") and eal.does_asset_exist(self.start)):
            return
        world = editor_sub.get_editor_world()
        if not world or world.get_path_name().split(".")[0] != self.start:
            level_sub.load_level(self.start)


def open_level(path, maps):
    """Make `path` the editor's level, empty. An existing level is opened and its actors removed:
    rebuilding in place keeps the package, so the world that streams it stays valid."""
    maps.leave()
    if eal.does_asset_exist(path):
        if not level_sub.load_level(path):
            raise RuntimeError("could not open " + path)
        actors.destroy_actors([a for a in actors.get_all_level_actors()
                               if not isinstance(a, (unreal.WorldSettings, unreal.Brush))])
    elif not level_sub.new_level(path):
        raise RuntimeError("could not create " + path)


def build_zone_level(zone, parts, sharers, scatter_inputs, compute_scatter, props, maps, effects=(), atmosphere=None):
    """Rebuild L_Zone_<rid> when what it places changed (not when a mesh it places was re-imported:
    actors reference the asset, so they show the new mesh as is)."""
    path = zone_level_path(zone)
    cache = build_cache.CACHE
    recipe = {"origin": zone["world_origin_cm"], "sharers": sharers, "parts": parts, "scatter": scatter_inputs,
              "props": props, "code": source(build_zone_level, spawn_fire), "fire_actor": hasattr(unreal, "MRFireActor"),
              "effects": list(effects), "effect_scale": EFFECT_SCALE, "effect_bounds": EFFECT_BOUNDS,
              "atmosphere": {k: v for k, v in (atmosphere or {}).items() if k != "ambient"}}
    key = cache.key(recipe, deps=False)
    if cache.fresh(path, key):
        cache.done(path, key, "levels", False)
        return path, False
    open_level(path, maps)
    ox, oy, oz = zone["world_origin_cm"]
    origin = unreal.Vector(ox, oy, oz)
    zone_tags = [unreal.Name("Zone%d" % r) for r in [zone["rid"]] + sharers]
    for mesh, label, role in parts:
        role, _, override = role.partition("|")
        a = actors.spawn_actor_from_object(eal.load_asset(mesh), origin + unreal.Vector(0, 0, 1.5 if role == "ice" else 0),
                                           unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        if override:
            for i in range(comp.get_num_materials()):
                comp.set_material(i, eal.load_asset(override))
        if role == "collision":
            comp.set_visibility(False)  # still blocks; not rendered, no shadows, no Lumen
        elif role in ("render", "art", "decal", "ice"):
            comp.set_collision_profile_name("NoCollision")
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        if role in ("decal", "ice"):
            comp.set_cast_shadow(False)
        a.tags = [unreal.Name("Zone" + role.capitalize())] + zone_tags
    for label, mesh, transforms, rule in compute_scatter():
        a = actors.spawn_actor_from_class(unreal.MRScatterActor, origin, unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        cull = rule.get("cull_m", [40, 60])
        a.set_scatter(eal.load_asset(mesh), transforms, cull[0] * 100.0, cull[1] * 100.0, bool(rule.get("shadows", True)))
        a.tags = [unreal.Name("ZoneScatter")] + zone_tags
    has_fire_actor = hasattr(unreal, "MRFireActor")
    for label, mesh, (x, y, z), light, fire in props:
        loc = origin + unreal.Vector(x, y, z)
        if (fire or (light and light.get("flicker"))) and has_fire_actor:
            spawn_fire(label, loc, light, fire, zone["rid"])
            light = None  # the fire actor has it
        if mesh:
            a = actors.spawn_actor_from_object(eal.load_asset(mesh), loc, unreal.Rotator(0, 0, 0))
            a.set_actor_label(label)
            comp = a.get_component_by_class(unreal.StaticMeshComponent)
            comp.set_mobility(unreal.ComponentMobility.STATIC)
            comp.set_collision_profile_name("NoCollision")
            a.tags = [unreal.Name("ZoneProp"), unreal.Name("Zone%d" % zone["rid"])]
        if light:
            pl = actors.spawn_actor_from_class(unreal.PointLight, loc + unreal.Vector(0, 0, light["offset_m"] * 100.0))
            pl.set_actor_label(label + "_Light")
            lc = pl.light_component
            lc.set_mobility(unreal.ComponentMobility.MOVABLE)
            lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
            lc.set_editor_property("intensity", float(light["candela"]))
            lc.set_editor_property("attenuation_radius", float(light["radius_m"]) * 100.0)
            if "color" in light:
                r, g, b = light["color"]
                lc.set_editor_property("use_temperature", False)
                lc.set_editor_property("light_color", unreal.Color(r=r, g=g, b=b, a=255))
            else:
                lc.set_editor_property("use_temperature", True)
                lc.set_editor_property("temperature", float(light["temperature"]))
            lc.set_editor_property("source_radius", float(light.get("source_radius_cm", 5)))
            if not light.get("shadows", True):
                lc.set_editor_property("cast_shadows", False)
            pl.tags = [unreal.Name("ZoneLight"), unreal.Name("Zone%d" % zone["rid"])]
            if light.get("night_only"):
                pl.tags = pl.tags + [unreal.Name("NightLamp")]  # off by day (UMREnvironmentSubsystem)
    puffs = _kit.get("SM_Puffs")
    for label, kind, (x, y, z) in effects:
        # chimney smoke and lamp moths: SM_Puffs moved entirely by its material (M_Smoke, M_Moth)
        a = actors.spawn_actor_from_object(eal.load_asset(puffs), origin + unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        a.set_actor_scale3d(unreal.Vector(*EFFECT_SCALE[kind]))
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_collision_profile_name("NoCollision")
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_cast_shadow(False)
        comp.set_editor_property("affect_distance_field_lighting", False)
        comp.set_editor_property("affect_dynamic_indirect_lighting", False)
        comp.set_editor_property("bounds_scale", EFFECT_BOUNDS[kind])
        comp.set_material(0, eal.load_asset(atmosphere[kind]))
        a.tags = [unreal.Name("ZoneEffect"), unreal.Name("Zone%d" % zone["rid"])]
    if not level_sub.save_current_level():
        raise RuntimeError("could not save " + path)
    cache.done(path, key, "levels", True)
    return path, True


def spawn(cls, loc=unreal.Vector(0, 0, 0), rot=unreal.Rotator(0, 0, 0), label=None):
    """Spawn an actor; its label is also a tag, which is how UMREnvironmentSubsystem finds the
    lighting actors in game (labels exist only in the editor)."""
    a = actors.spawn_actor_from_class(cls, loc, rot)
    if label:
        a.set_actor_label(label)
        a.tags = [unreal.Name(label)]
    return a


def build_persistent_level(zone_levels, maps, night_sky=None):
    cache = build_cache.CACHE
    recipe = {"levels": zone_levels, "code": source(build_persistent_level, spawn, apply_level_mood),
              "moods": file_digest(MOODS), "mood": os.environ.get("MR_MOOD"), "night_sky": night_sky}
    key = cache.key(recipe, deps=False)
    if cache.fresh(WORLD_PATH, key):
        cache.done(WORLD_PATH, key, "levels", False)
        return False
    if eal.does_asset_exist(WORLD_PATH):
        maps.leave()
        if not level_sub.load_level(WORLD_PATH):
            raise RuntimeError("could not open " + WORLD_PATH)
        world = editor_sub.get_editor_world()
        for level in unreal.EditorLevelUtils.get_levels(world)[1:]:
            unreal.EditorLevelUtils.remove_level_from_world(level)
        actors.destroy_actors([a for a in actors.get_all_level_actors()
                               if not isinstance(a, (unreal.WorldSettings, unreal.Brush))])
    else:
        open_level(WORLD_PATH, maps)
    # one sun + sky for the outdoor zones (interiors get their own lights later). The sun is also
    # the moon at night, moved by the environment director in game (docs/adr/0005), so it's movable
    sun = spawn(unreal.DirectionalLight, rot=unreal.Rotator(roll=0, pitch=-40, yaw=-30), label="Sun")
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sun.light_component.set_editor_property("intensity", 8.0)
    # lightning (docs/adr/0005 phase 4): a second, unshadowed directional light the environment
    # director flashes in rainstorms; never the atmosphere's sun, so it adds no disc to the sky.
    # The original's lightning colour is a bluish white (kod LIGHT_LIGHTNING)
    bolt = spawn(unreal.DirectionalLight, rot=unreal.Rotator(roll=0, pitch=-62, yaw=40), label="Lightning")
    bolt_light = bolt.light_component
    bolt_light.set_mobility(unreal.ComponentMobility.MOVABLE)
    bolt_light.set_editor_property("atmosphere_sun_light", False)
    bolt_light.set_editor_property("cast_shadows", False)
    bolt_light.set_editor_property("intensity", 0.0)
    bolt_light.set_editor_property("use_temperature", False)
    bolt_light.set_editor_property("light_color", unreal.Color(r=190, g=205, b=255, a=255))
    bolt_light.set_editor_property("visible", False)
    spawn(unreal.SkyAtmosphere, label="SkyAtmosphere")
    sky = spawn(unreal.SkyLight, label="SkyLight")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.ExponentialHeightFog, label="HeightFog")
    spawn(unreal.VolumetricCloud, label="Clouds")
    pp = spawn(unreal.PostProcessVolume, label="GlobalPostProcess")
    pp.set_editor_property("unbound", True)
    if night_sky:
        # sky dome for the stars (M_NightSky draws the atmosphere through it): far beyond the clouds
        dome = spawn(unreal.StaticMeshActor, label="NightSky")
        dome.set_actor_scale3d(unreal.Vector(2.0e5, 2.0e5, 2.0e5))  # engine sphere: 50 cm -> 100 km
        comp = dome.static_mesh_component
        comp.set_static_mesh(eal.load_asset("/Engine/BasicShapes/Sphere"))
        comp.set_material(0, eal.load_asset(night_sky))
        comp.set_collision_profile_name("NoCollision")
        comp.set_editor_property("cast_shadow", False)
        comp.set_editor_property("affect_distance_field_lighting", False)
        comp.set_editor_property("affect_dynamic_indirect_lighting", False)
    apply_level_mood("L_World")  # sun angle, fog, exposure... from data/environment/moods.json

    world = editor_sub.get_editor_world()
    for path in zone_levels:
        streaming = unreal.EditorLevelUtils.add_level_to_world(world, path, unreal.LevelStreamingDynamic)
        if not streaming:
            raise RuntimeError("could not add streaming level " + path)
        streaming.set_editor_property("initially_loaded", False)
        streaming.set_editor_property("initially_visible", False)
    # adding a sublevel makes it the editor's current level; save the persistent level itself
    level_sub.set_current_level_by_name("PersistentLevel")
    if not level_sub.save_all_dirty_levels():
        raise RuntimeError("could not save " + WORLD_PATH)

    # verify: reload the map and count its streaming levels
    if not level_sub.load_level(WORLD_PATH):
        raise RuntimeError("could not reload " + WORLD_PATH)
    world = editor_sub.get_editor_world()
    sublevels = len(unreal.EditorLevelUtils.get_levels(world)) - 1  # minus the persistent level
    log("saved %s; on reload it has %d zone sublevels (expected %d)" % (WORLD_PATH, sublevels, len(zone_levels)))
    if sublevels != len(zone_levels):
        raise RuntimeError("streaming levels were not saved into " + WORLD_PATH)
    cache.done(WORLD_PATH, key, "levels", True)
    return True


def main(args):
    if level_sub.is_in_play_in_editor():
        raise RuntimeError("stop Play-In-Editor before building the world")
    started = time.time()
    clean = "--clean" in args
    cache = build_cache.begin(clean)
    if clean:
        reset_generated()
    maps = MapSwitch()
    layout = json.load(open(LAYOUT, encoding="utf-8"))
    zones = layout["zones"]
    materials = ZoneMaterials()
    prop_materials = materials.props

    # which zones reuse another zone's geometry
    sharers = {}
    for z in zones:
        sg = z.get("shares_geometry_with")
        if sg:
            sharers.setdefault(sg["rid"], []).append(z["rid"])

    if materials.precip:
        # rain and snow (AMRPrecipitationActor loads it at runtime): no Nanite, M_Precip moves it
        import_kit_mesh("SM_Precip", materials, {"precip": materials.precip["default"]})
    if materials.atmosphere:
        # chimney smoke and moths (phase 5); the ambient particles reuse SM_Precip
        import_kit_mesh("SM_Puffs", materials, {"puffs": materials.atmosphere["smoke"]})
    build_audio.build_audio()  # the original's sounds (docs/adr/0006)

    zone_levels, bad = [], 0
    try:
        for z in zones:
            if "shares_geometry_with" in z:
                continue
            parts, summary = import_zone_parts(z, materials)
            if any(parts[0][0].startswith(d + "/") for d in cache.built.get("meshes", [])):  # re-imported
                bad += 0 if check_orientation(z, parts[0][0]) else 1
            scatter_inputs, compute_scatter = zone_scatter(z, materials)
            room_props, torches = merge_torch_lights(zone_props(z, materials, prop_materials), zone_wall_fires(z, materials))
            props = room_props + torches
            effects = zone_effects(z, materials)
            path, rebuilt = build_zone_level(z, parts, sharers.get(z["rid"], []), scatter_inputs, compute_scatter, props, maps,
                                             effects, materials.atmosphere)
            zone_levels.append(path)
            prune_art(z, parts)
            if rebuilt:
                log("zone %d %s: level rebuilt (%s)" % (z["rid"], z["class"], summary))
        build_persistent_level(zone_levels, maps, materials.night_sky)
        remove_stale_levels(zone_levels)
    finally:
        cache.save()
    if bad:
        raise RuntimeError("%d zone meshes imported with the wrong orientation" % bad)
    maps.restore()
    log("done in %.0f s; rebuilt/total: %s" % (time.time() - started, cache.summary()))


def _launched_for_this_script():
    """True when this editor was started just to run a script (-ExecutePythonScript): quit after."""
    return "-executepythonscript" in unreal.SystemLibrary.get_command_line().lower()


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    finally:
        if _launched_for_this_script() and "-keep-open" not in os.environ.get("MR_BUILD_WORLD_ARGS", ""):
            unreal.SystemLibrary.quit_editor()
