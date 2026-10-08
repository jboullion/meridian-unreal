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
  /Game/Generated/Zones/Z<rid>/...          imported blockout mesh + materials, and the collision blockout
                                            (roo2gltf <rid>_<class>_collision.glb: without the walls the
                                            original lets you walk through) as hidden complex-as-simple
                                            collision. Zones with art (data/environment/zone_<rid>.json + the meshes
                                            tools/blender/build_zone_art.py wrote to build/environment/zone_<rid>/)
                                            get three parts instead: the collision blockout as hidden collision,
                                            the blockout without the rebuilt buildings for rendering, and
                                            the art meshes (Nanite, no collision) under Z<rid>/Art,
                                            plus ground scatter (grass tufts, AMRScatterActor) from the
                                            zone's "scatter" rules and the meshes in build/environment/kit/.
  /Game/Generated/Environment/...           placeholder textures + material instances per original
                                            texture, assigned to the zone meshes (environment_materials.py)
  /Game/Generated/Maps/Zones/L_Zone_<rid>_<KodClass>   one streaming sublevel per zone *geometry*: the zone mesh
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
from environment_materials import ENV as ENVIRONMENT_DIR, ZoneMaterials, ai_prop_material, assign_materials, build_runtime_room_material, tree_materials  # noqa: E402
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
        two_sided_distance_field(mesh, dest_dir)
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
    # the importer files the mesh under a folder named after the glb; a folder that once held
    # another glb keeps that one's mesh too (Raza's collision went on using the full blockout,
    # 2026-10-07), so take the mesh named after this file
    stem = os.path.splitext(os.path.basename(glb_path))[0]
    mesh = next((m for m in meshes if m.rsplit("/", 1)[-1] == stem), meshes[0])
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
    two_sided_distance_field(mesh, dest_dir)
    return mesh


def two_sided_distance_field(mesh, dest_dir):
    """Build the mesh's distance field as if two-sided. The zone walls are single-sided planes, and
    their one-sided distance fields put the wall's own surface "inside", so Lumen's world-space
    rays from it start occluded: unlit walls got no sky light at all, only what screen traces
    picked up from the sky on screen, and went black as the camera came close (2026-10-06,
    build/lookdev/wall_ef.png). Set on the existing mesh (no re-import), once per import of it: a
    re-import resets it (`dest_dir`'s key; 2026-10-07 every mesh re-imported and the town went dark)."""
    cache = build_cache.CACHE
    record = mesh + "#distance_field"
    key = cache.key(source(two_sided_distance_field), dest_dir)
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
        log("two-sided distance field: %s" % mesh)
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


def collision_blockout(glb):
    """roo2gltf's <rid>_<class>_collision.glb next to the blockout: the same geometry without the
    walls the original lets you walk through (WF_PASSABLE: Raza's field borders, hanging signs, wall
    torches). Falls back to the full blockout (everything blocks) for blockouts made before it."""
    path = glb[:-4] + "_collision.glb"
    if os.path.exists(path):
        return path
    log("WARNING: %s missing (run tools/roo2gltf/roo2gltf.py); passable walls will block" % path)
    return glb


def import_zone_parts(zone, materials):
    """-> [(mesh path, label, role)], role "geometry" (render + collision), "collision" (hidden),
    "render" (no collision), "art" (Nanite, no collision) or "decal" (mesh decals: not Nanite, no
    collision, no shadows)."""
    rid, cls = zone["rid"], zone["class"]
    base = "%s/Zones/Z%d" % (GENERATED, rid)
    glb = os.path.join(REPO, zone["mesh"])
    collision_glb = collision_blockout(glb)
    art = zone_art(zone)
    if not art:
        if collision_glb == glb:
            mesh = import_zone_mesh(glb, base)
            counts = materials.apply(mesh)
            return [(mesh, "ZoneGeometry_%d_%s" % (rid, cls), "geometry")], "materials %s" % counts
        mesh = import_zone_mesh(glb, base, collision=False)
        counts = materials.apply(mesh)
        collision = import_zone_mesh(collision_glb, base + "/Collision")
        return [(mesh, "ZoneGeometry_%d_%s" % (rid, cls), "render"),
                (collision, "ZoneCollision_%d_%s" % (rid, cls), "collision")], "materials %s" % counts

    config, manifest = art
    art_dir = os.path.join(REPO, "build", "environment", "zone_%d" % rid)
    render_glb = os.path.join(art_dir, "blockout_render.glb")
    hidden = write_render_blockout(glb, render_glb, config)
    collision = import_zone_mesh(collision_glb, base + "/Collision")
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


def import_kit_mesh(name, materials, slot_materials=None, albedo=1.0, glow=None, collision=None):
    """A kit mesh from build/environment/kit/<name>.glb. Scatter meshes get M_Grass; props get their
    slots' materials from props.json (`slot_materials`); an AI prop's base colour is scaled by its
    class's props.json "albedo", and its "glow" lights the glass of a lamp (M_PropTextured).
    collision: the class's props.json "blocks" shape for an AI prop (prop_collision), else none."""
    if name in _kit:
        return _kit[name]
    path = os.path.join(KIT_DIR, name + ".glb")
    if not os.path.exists(path):
        log("WARNING: %s missing; run tools/blender/build_grass_kit.py" % path)
        return None
    dest = "%s/Kit/%s" % (ENVIRONMENT_DIR, name)
    mesh = import_zone_mesh(path, dest, collision=False)
    if name.startswith("SM_AI_"):
        prop_collision(mesh, dest, collision)
        # AI props (tools/aigen): their textures on M_PropTextured, which adds the interiors' ambient
        # floor and the weather the imported glTF material lacks (docs/adr/0007)
        slots = eal.load_asset(mesh).get_editor_property("static_materials")
        mi = ai_prop_material(mesh, slots[0].get_editor_property("material_interface"), prop_ambient_scale(), albedo,
                              glow) if slots else None
        if mi:
            assign_materials(mesh, lambda slot: (mi, 1))
            _kit[name] = mesh
            return mesh
    if name.startswith("SM_Tree_"):
        # procedural trees (tools/blender/build_tree_kit.py): bark and leaf slots on M_TreeBark /
        # M_TreeLeaves with the tree kind's textures (docs/adr/0007 "Trees")
        slot_materials = tree_materials(name.split("_")[2])
    if slot_materials is None:
        assign_materials(mesh, lambda slot: (materials.grass, 1) if materials.grass else (None, 2))
    else:
        assign_materials(mesh, lambda slot: (slot_materials[slot], 1) if slot_materials.get(slot) else (None, 2))
    _kit[name] = mesh
    return mesh


# props.json "blocks" -> the simple collision shape UE fits around the mesh (StaticMeshEditorSubsystem)
PROP_COLLISION_SHAPES = {"kdop": "NDOP26", "box": "BOX", "capsule": "CAPSULE", "sphere": "SPHERE"}


def prop_collision(mesh, dest, shape):
    """Give an AI prop's mesh simple collision, or none. The original's objects never blocked;
    props.json "blocks" (true for a 26-sided hull, or "box" / "capsule" / "sphere") makes a class
    solid (2026-10-07: lamps, braziers, tables). Without simple collision the mesh blocks nothing,
    as it imports with simple-as-complex. Redone when the mesh is re-imported (`dest`'s key)."""
    if shape is True:
        shape = "kdop"
    shape = PROP_COLLISION_SHAPES.get(shape) if shape else None
    cache = build_cache.CACHE
    record = mesh + "#collision"
    key = cache.key(dest, shape, source(prop_collision))
    if cache.fresh(record, key, exists=lambda: True):
        cache.done(record, key, "prop collision", False)
        return
    obj = eal.load_asset(mesh)
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    sub.remove_collisions(obj)
    if shape:
        sub.add_simple_collisions(obj, getattr(unreal.ScriptCollisionShapeType, shape))
    eal.save_loaded_asset(obj)
    cache.done(record, key, "prop collision", True)


def prop_ambient_scale():
    """props.json "ambient_scale": a factor on the AI props' ambient floor indoors, over the sector
    light of the floor they stand on (M_PropTextured AmbientScale)."""
    if not os.path.exists(PROPS):
        return 1.0
    return float(json.load(open(PROPS, encoding="utf-8")).get("ambient_scale", 1.0))


def light_scale():
    """props.json "light_scale": one factor on the intensity of every light the world build adds
    (props, wall torches, the original's room lights); their reach stays. The environment light
    carries the scene, fires add warmth (2026-10-06, docs/adr/0005)."""
    try:
        return float(json.load(open(PROPS, encoding="utf-8")).get("light_scale", 1.0))
    except (OSError, ValueError):
        return 1.0


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


# glTF +Z (the side Tripo faces to the camera, docs/adr/0007) lands on UE +Y (south) on import, which
# is Kod angle 1024: a Kod angle turns the model by (angle - 1024) / 4096 of a turn
KOD_ANGLE_FRONT = 1024


def prop_config(props, obj):
    """The props.json entry for a Kod object: OrnamentalObjects by their type ("types", keyed by the
    OO number as a string), every other class by "classes": "<Class>/<classtype>" first for an
    object made with a classtype (a FoodDispenser of Apple: the apple tree), then "<Class>"."""
    params = obj.get("params") or {}
    if obj["class"] == "OrnamentalObject":
        return props.get("types", {}).get(str(params.get("type")))
    classes = props.get("classes", {})
    classtype = params.get("classtype")
    if isinstance(classtype, dict) and classtype.get("class"):
        cfg = classes.get("%s/%s" % (obj["class"], classtype["class"]))
        if cfg:
            return cfg
    return classes.get(obj["class"])


# props.json "facing": where a model's front looks, as a UE yaw (its front, glTF +Z, is UE +Y: south)
FACINGS = {"south": 0.0, "west": 90.0, "north": 180.0, "east": -90.0}


def facing_yaw(facing):
    """A props.json "facing": a compass direction or degrees (UE yaw, 0 = south, clockwise from above)."""
    return FACINGS[facing.lower()] if isinstance(facing, str) else float(facing)


def prop_yaw(cfg, obj, label, placed=None):
    """Yaw in degrees, then plus the entry's "yaw_offset":
    - the object's own "facing" (props.json "placed", by actor label) when it has one;
    - else a stable pseudo-random turn when the entry has "random_yaw" (plants, rocks: the original
      drew them as camera-facing sprites, and wrote 0 for their angle, which means none);
    - else the object's Kod angle;
    - else the entry's "facing" (signs: east), for the objects the original gives no angle;
    - else 0 (south)."""
    placed = placed or {}
    if "facing" in placed:
        yaw = facing_yaw(placed["facing"])
    elif cfg.get("random_yaw"):
        yaw = (zlib.crc32(label.encode()) % 3600) / 10.0
    elif obj.get("yaw_kod") is not None:
        yaw = (float(obj["yaw_kod"]) - KOD_ANGLE_FRONT) * 360.0 / 4096.0
    else:
        yaw = facing_yaw(cfg.get("facing", 0.0))
    return yaw + float(cfg.get("yaw_offset", 0.0))


def prop_mesh_available(name):
    """A props.json mesh: a kit GLB name (build/environment/kit/<name>.glb) or a UE asset path
    (/Game/..., e.g. a tree exported from the Procedural Vegetation Editor)."""
    if name.startswith("/"):
        return eal.does_asset_exist(name)
    return os.path.exists(os.path.join(KIT_DIR, name + ".glb"))


def prop_mesh_name(cfg, label, ordinal):
    """The mesh for one placed object: a custom model when one exists, else the entry's mesh.
    Custom: the entry's "mesh_custom" (a kit GLB name or UE asset path), or else <mesh>_Custom, the
    kit mesh tools/aigen builds from a manifest's "custom" model (docs/adr/0007 "Custom models").
    Either falls back to "mesh" (the HD model) while its file doesn't exist."""
    for custom in (cfg.get("mesh_custom"), cfg.get("mesh") and cfg["mesh"] + "_Custom"):
        if custom and prop_mesh_available(custom):
            return custom
    return prop_mesh_choice(cfg, label, ordinal)


def prop_mesh_choice(cfg, label, ordinal):
    """The entry's own mesh. "mesh" by default. An entry with "mesh_options" ({option:
    [meshes]}) uses the option named by "mesh_use"; "compare" cycles the options over the zone's
    objects of this kind (the ordinal-th object gets option ordinal % n), so they stand side by side
    (docs/adr/0007 "Trees"). Within an option a stable pseudo-random variant per object. Options
    whose meshes don't exist yet are left out; None when nothing is available."""
    options = cfg.get("mesh_options")
    if not options:
        name = cfg.get("mesh")
        return name if name and prop_mesh_available(name) else None
    avail = {k: [m for m in v if prop_mesh_available(m)] for k, v in options.items() if not k.startswith("_")}
    avail = {k: v for k, v in avail.items() if v}
    use = cfg.get("mesh_use", "compare")
    if use == "compare":
        keys = [k for k in options if k in avail]
        if not keys:
            return None
        use = keys[ordinal % len(keys)]
    meshes = avail.get(use)
    if not meshes:
        name = cfg.get("mesh")
        return name if name and prop_mesh_available(name) else None
    return meshes[zlib.crc32(label.encode()) % len(meshes)]


def hanging_y(mesh, scale, obj, y, label):
    """A props.json "hanging" prop (Kod OF_HANGING: the chandelier) hangs from the ceiling: its mesh's
    top at the ceiling above it (zone_layout.json "ceiling_y"), as the original pins the sprite's top
    there (clientd3d object.c RoomObjectSetHeight: ceiling - the sprite's height; the chandelier's
    drawing fills its frame to the top). Under open sky it stays where it is."""
    if obj.get("ceiling_y") is None:
        log("WARNING: %s is hanging but has no ceiling above it (run tools/roo2gltf/roo2gltf.py)" % label)
        return y
    top_m = eal.load_asset(mesh).get_bounding_box().max.z * scale / 100.0
    return float(obj["ceiling_y"]) - top_m


def zone_props(zone, materials, prop_materials):
    """-> [(label, mesh path or None, [x, y, z] cm, light config or None, fire or None, yaw deg, scale,
    sector light, blocks)] for the
    zone's Kod objects with an entry in data/environment/props.json ("classes", or "types" for
    OrnamentalObjects). Meshes are the AI kit meshes (tools/aigen, docs/adr/0007), placed only once
    their GLB exists. A light with "flicker" (true, or "sector" in the original's flickering sectors)
    and an entry with a "fire" become AMRFireActors. Sector light: the original light level of the floor
    under the prop (blockout.floor_light), for M_PropTextured's ambient floor. blocks: the entry's
    "blocks": a shape fitted to the mesh (prop_collision), or {"radius_m", "height_m"} for a hidden
    cylinder (spawn_blocker: a tree's trunk, a sign's post); False to walk through."""
    if not os.path.exists(PROPS):
        return []
    props = json.load(open(PROPS, encoding="utf-8"))
    out = []
    ordinals = {}
    anchors = []  # where the lights of hanging props (the chandelier's candles) are, cm
    for i, obj in enumerate(zone.get("objects", [])):
        cfg = prop_config(props, obj)
        if not cfg:
            continue
        x, y, z = obj["pos"]
        kind = obj["class"] if obj["class"] != "OrnamentalObject" else "OO%s" % (obj.get("params") or {}).get("type")
        label = "Prop_%d_%s_%d" % (zone["rid"], kind, i)
        # one placed object's own settings (props.json "placed", by its actor label), over its entry's
        placed = props.get("placed", {}).get(label, {})
        cfg = dict(cfg, **{k: v for k, v in placed.items() if k != "facing"})
        ordinals[kind] = ordinals.get(kind, -1) + 1
        mesh_name = prop_mesh_name(cfg, label, ordinals[kind])  # None: not generated yet
        scale = 1.0
        if mesh_name and mesh_name.startswith("/"):
            mesh = mesh_name  # already a UE asset: scaled to the entry's "fit_height_m"
            if cfg.get("fit_height_m"):
                scale = float(cfg["fit_height_m"]) * 100.0 / max(1.0, 2.0 * eal.load_asset(mesh).get_bounds().box_extent.z)
        else:
            blocks = cfg.get("blocks")
            mesh = import_kit_mesh(mesh_name, materials, prop_materials, float(cfg.get("albedo", 1.0)), cfg.get("glow"),
                                   None if isinstance(blocks, dict) else blocks) if mesh_name else None
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
        if not (mesh or light or fire):
            continue
        sector = round(blockout.floor_light(os.path.join(REPO, zone["mesh"]), x, z, y), 3) if mesh else 1.0
        if mesh and cfg.get("hanging"):
            y = hanging_y(mesh, scale, obj, y, label)
            if cfg.get("lights_at_m") is not None:
                anchors.append((x * 100.0, z * 100.0, (y + float(cfg["lights_at_m"]) * scale) * 100.0))
        out.append((label, mesh, [x * 100.0, z * 100.0, y * 100.0], light, fire, prop_yaw(cfg, obj, label, placed), scale, sector,
                    cfg.get("blocks") or False))
    return lights_to_hanging(out, anchors)


TORCH_LIGHT_MERGE_CM = 100.0


HANGING_LIGHT_MERGE_CM = 50.0


def lights_to_hanging(props, anchors):
    """The original lit its chandelier with a room light (DynamicLight) on the same spot, which the
    build put 1.8 m up: under the hanging chandelier, a glowing ball in mid-air (2026-10-07). A
    DynamicLight within HANGING_LIGHT_MERGE_CM (in plan) of a hanging prop with "lights_at_m" moves
    up to that height (its candles)."""
    out = []
    for p in props:
        label, mesh, pos, light = p[:4]
        if light and not mesh and "_DynamicLight_" in label:
            near = [a for a in anchors if ((a[0] - pos[0]) ** 2 + (a[1] - pos[1]) ** 2) ** 0.5 < HANGING_LIGHT_MERGE_CM]
            if near:
                log("%s moved up to its chandelier's candles" % label)
                p = (label, mesh, [pos[0], pos[1], near[0][2]], dict(light, offset_m=0.0)) + tuple(p[4:])
        out.append(p)
    return out


def merge_torch_lights(props, wall_fires):
    """The original lit its wall torches with a room light (DynamicLight) placed under each flame,
    so a torch and that light stood about half a metre apart: two lights per torch (2026-10-06).
    A DynamicLight within TORCH_LIGHT_MERGE_CM (in plan) of a torch flame is dropped, and its light
    (the original's strength and colour) moves onto the flame. -> (props, wall fires)."""
    kept, used = [], set()
    merged = []
    for label, mesh, pos, light, fire, yaw in wall_fires:
        best, best_d = None, TORCH_LIGHT_MERGE_CM
        for i, p in enumerate(props):
            plabel, pmesh, ppos, plight, pfire = p[:5]
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
        merged.append((label, mesh, pos, light, fire, yaw))
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
        out.append(("Fire_%d_%s_%d" % (zone["rid"], f["preset"], i), None, [x * 100.0, z * 100.0, y * 100.0], light, fire, 0.0))
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
        a.set_light(float(light["candela"]) * light_scale(), float(light["radius_m"]) * 100.0,
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
    """Rebuild L_Zone_<rid>_<KodClass> when what it places changed (not when a mesh it places was re-imported:
    actors reference the asset, so they show the new mesh as is)."""
    path = zone_level_path(zone)
    cache = build_cache.CACHE
    recipe = {"origin": zone["world_origin_cm"], "sharers": sharers, "parts": parts, "scatter": scatter_inputs,
              "props": props, "light_scale": light_scale(), "code": source(build_zone_level, spawn_fire, spawn_blocker),
              "fire_actor": hasattr(unreal, "MRFireActor"),
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
    for label, mesh, (x, y, z), light, fire, yaw, *rest in props:
        loc = origin + unreal.Vector(x, y, z)
        if (fire or (light and light.get("flicker"))) and has_fire_actor:
            spawn_fire(label, loc, light, fire, zone["rid"])
            light = None  # the fire actor has it
        if mesh:
            a = actors.spawn_actor_from_object(eal.load_asset(mesh), loc, unreal.Rotator(0, 0, 0))
            a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False)
            if rest and rest[0] != 1.0:
                a.set_actor_scale3d(unreal.Vector(rest[0], rest[0], rest[0]))
            sector = rest[1] if len(rest) > 1 else 1.0
            a.set_actor_label(label)
            comp = a.get_component_by_class(unreal.StaticMeshComponent)
            if comp:
                comp.set_mobility(unreal.ComponentMobility.STATIC)
            else:  # a skeletal mesh (a PVE tree with Dynamic Wind bones)
                comp = a.get_component_by_class(unreal.SkeletalMeshComponent)
            # props.json "blocks": solid (simple collision on the mesh), or a hidden cylinder
            # (spawn_blocker); else walked through, as every object in the original
            blocks = rest[2] if len(rest) > 2 else False
            comp.set_collision_profile_name("BlockAll" if blocks and not isinstance(blocks, dict) else "NoCollision")
            # the original steps 0.825 m (UMRCharacterMovementComponent): not onto a chest or a table
            comp.set_editor_property("can_character_step_up_on", unreal.CanBeCharacterBase.ECB_NO)
            if isinstance(blocks, dict):
                spawn_blocker(label, loc, rest[0] if rest else 1.0, blocks, zone["rid"])
            # M_PropTextured's SectorLight; the property, not set_custom_primitive_data_float, which
            # isn't saved with the level (an unset index reads 0: black props)
            cpd = comp.get_editor_property("custom_primitive_data")
            cpd.set_editor_property("data", [float(sector)])
            comp.set_editor_property("custom_primitive_data", cpd)
            a.tags = [unreal.Name("ZoneProp"), unreal.Name("Zone%d" % zone["rid"])]
        if light:
            pl = actors.spawn_actor_from_class(unreal.PointLight, loc + unreal.Vector(0, 0, light["offset_m"] * 100.0))
            pl.set_actor_label(label + "_Light")
            lc = pl.light_component
            lc.set_mobility(unreal.ComponentMobility.MOVABLE)
            lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
            lc.set_editor_property("intensity", float(light["candela"]) * light_scale())
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


BLOCKER_MESH = "/Engine/BasicShapes/Cylinder"  # 100 cm across and tall, pivot at its centre


def spawn_blocker(label, loc, scale, blocks, zone_rid):
    """A hidden cylinder that blocks: props.json "blocks" {"radius_m", "height_m"}, for a prop of
    which only a part should stop you (a tree's trunk, not its crown; a sign's post). Tagged
    ZoneProp, so floor traces look under it (UMRZoneSubsystem::TraceFloor)."""
    r = float(blocks.get("radius_m", 0.2)) * scale
    h = float(blocks.get("height_m", 2.0)) * scale
    cylinder = eal.load_asset(BLOCKER_MESH)
    geom = cylinder.get_editor_property("body_setup").get_editor_property("agg_geom")
    if not any(len(geom.get_editor_property(k)) for k in ("convex_elems", "sphyl_elems", "box_elems", "sphere_elems")):
        log("WARNING: %s has no simple collision; %s blocks nothing" % (BLOCKER_MESH, label))
    a = actors.spawn_actor_from_object(cylinder, loc + unreal.Vector(0, 0, h * 50.0), unreal.Rotator(0, 0, 0))
    a.set_actor_label(label + "_Block")
    a.set_actor_scale3d(unreal.Vector(r * 2.0, r * 2.0, h))
    a.set_actor_hidden_in_game(True)
    comp = a.get_component_by_class(unreal.StaticMeshComponent)
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    comp.set_visibility(False)
    comp.set_cast_shadow(False)
    comp.set_collision_profile_name("BlockAll")
    comp.set_editor_property("can_character_step_up_on", unreal.CanBeCharacterBase.ECB_NO)
    a.tags = [unreal.Name("ZoneProp"), unreal.Name("ZoneBlocker"), unreal.Name("Zone%d" % zone_rid)]


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
    build_runtime_room_material()  # rooms built at runtime from the server's files (docs/adr/0012)

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
