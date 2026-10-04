"""
Build the streamed world from the zone blockouts. Runs inside the Unreal Editor (Python):

    UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript="<repo>/tools/ue/build_world.py" -unattended -nosplash -RenderOffscreen

(It needs a real editor world, so it runs as -ExecutePythonScript rather than the pythonscript
commandlet, and quits the editor when it finishes; set MR_BUILD_WORLD_ARGS=-keep-open to stay.)

Output (everything under /Game/Generated, git-ignored, rebuilt from scratch each run):
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
"""
import json
import os
import shutil
import sys

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools", "environment"))
import blockout  # noqa: E402
import scatter  # noqa: E402
from environment_materials import ENV as ENVIRONMENT_DIR, ZoneMaterials  # noqa: E402
from zone_mood import apply_level_mood  # noqa: E402

LAYOUT = os.path.join(REPO, "data", "zone_layout.json")
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


def zone_level_path(rid):
    """Must match UMRZoneSubsystem's naming: L_Zone_<geometry rid>."""
    return "%s/L_Zone_%d" % (ZONE_LEVEL_DIR, rid)


def reset_generated():
    """Switch to a blank map so nothing this script made is in use, then delete what it owns
    (the maps, zone meshes and zone materials; other generated content under /Game/Generated is
    left alone)."""
    # The editor opens L_World at startup with every zone sublevel loaded; unload them so the
    # blank map + garbage collection frees them before their files go.
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


def import_zone_mesh(glb_path, dest_dir, nanite=False, collision=True):
    task = unreal.AssetImportTask()
    task.filename = glb_path
    task.destination_path = dest_dir
    task.automated = True
    task.replace_existing = True
    task.save = True
    asset_tools.import_asset_tasks([task])
    meshes = [p for p in eal.list_assets(dest_dir, recursive=True, include_folder=False)
              if isinstance(eal.load_asset(p), unreal.StaticMesh)]
    if not meshes:
        raise RuntimeError("no static mesh imported from " + glb_path)
    mesh = eal.load_asset(meshes[0])
    # The importer enables Nanite, but its glTF materials lack the Nanite usage flag, so a
    # standalone game would render them with the default grey material. Blockouts are small
    # and temporary; plain static meshes are fine. Art meshes get our own materials (which have
    # the flag) and need Nanite for displacement.
    settings = mesh.get_editor_property("nanite_settings")
    settings.set_editor_property("enabled", nanite)
    mesh.set_editor_property("nanite_settings", settings)
    body = mesh.get_editor_property("body_setup")
    if body:
        body.set_editor_property("collision_trace_flag",
                                 unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE if collision
                                 else unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    eal.save_loaded_asset(mesh)
    return mesh


def check_orientation(zone, mesh):
    """The importer must map glTF (x east, y up, z south) to UE (X east, Y south, Z up)."""
    lo, hi = zone["bounds_m"]["min"], zone["bounds_m"]["max"]
    ex0, ey0, ex1, ey1 = lo[0] * 100, lo[2] * 100, hi[0] * 100, hi[2] * 100
    b = mesh.get_bounding_box()
    ok = all(abs(a - e) < 50 for a, e in ((b.min.x, ex0), (b.max.x, ex1), (b.min.y, ey0), (b.max.y, ey1)))
    log("zone %d %-14s bounds X[%.0f..%.0f] Y[%.0f..%.0f] Z[%.0f..%.0f] %s"
        % (zone["rid"], zone["class"], b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z,
           "OK" if ok else "ORIENTATION MISMATCH (expected X[%.0f..%.0f] Y[%.0f..%.0f])" % (ex0, ex1, ey0, ey1)))
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


def import_zone_parts(zone, materials):
    """-> [(mesh, label, role)], role "geometry" (render + collision), "collision" (hidden),
    "render" (no collision) or "art" (Nanite, no collision)."""
    rid, cls = zone["rid"], zone["class"]
    base = "%s/Zones/Z%d" % (GENERATED, rid)
    glb = os.path.join(REPO, zone["mesh"])
    art = zone_art(zone)
    if not art:
        mesh = import_zone_mesh(glb, base)
        log("zone %d materials: %d real, %d placeholder, %d left as imported" % ((rid,) + tuple(materials.apply(mesh))))
        return [(mesh, "ZoneGeometry_%d_%s" % (rid, cls), "geometry")]

    config, manifest = art
    art_dir = os.path.dirname(os.path.join(REPO, "build", "environment", "zone_%d" % rid, "manifest.json"))
    prims = blockout.read_glb(glb)
    render_glb = os.path.join(art_dir, "blockout_render.glb")
    hidden = blockout.write_render_blockout(glb, render_glb, blockout.all_building_triangles(prims, config))
    collision = import_zone_mesh(glb, base + "/Collision")
    render = import_zone_mesh(render_glb, base + "/Render", collision=False)
    log("zone %d render blockout: %d triangles hidden under art; materials %s"
        % (rid, hidden, materials.apply(render)))
    parts = [(collision, "ZoneCollision_%d_%s" % (rid, cls), "collision"),
             (render, "ZoneGeometry_%d_%s" % (rid, cls), "render")]
    for m in manifest["meshes"]:
        mesh = import_zone_mesh(os.path.join(art_dir, m["file"]), "%s/Art/%s" % (base, m["name"]),
                                nanite=True, collision=False)
        log("zone %d art %s: materials %s" % (rid, m["name"], materials.apply(mesh, art=True)))
        parts.append((mesh, "ZoneArt_%d_%s" % (rid, m["building"]), "art"))
    return parts


KIT_DIR = os.path.join(REPO, "build", "environment", "kit")


def import_kit_mesh(name, materials):
    """A scatter mesh from build/environment/kit/<name>.glb, imported once, with M_Grass."""
    dest = "%s/Kit/%s" % (ENVIRONMENT_DIR, name)
    existing = [p for p in eal.list_assets(dest, recursive=True, include_folder=False)
                if isinstance(eal.load_asset(p), unreal.StaticMesh)] if eal.does_directory_exist(dest) else []
    if existing:
        return eal.load_asset(existing[0])
    path = os.path.join(KIT_DIR, name + ".glb")
    if not os.path.exists(path):
        log("WARNING: %s missing; run tools/blender/build_grass_kit.py" % path)
        return None
    mesh = import_zone_mesh(path, dest, collision=False)
    if materials.grass:
        slots = list(mesh.get_editor_property("static_materials"))
        for slot in slots:
            slot.set_editor_property("material_interface", materials.grass)
        mesh.set_editor_property("static_materials", slots)
        eal.save_loaded_asset(mesh)
    return mesh


def zone_scatter(zone, materials):
    """-> [(label, mesh, [Transform], rule)] from data/environment/zone_<rid>.json "scatter"."""
    config = blockout.zone_art_config(zone["rid"])
    rules = (config or {}).get("scatter", [])
    if not rules:
        return []
    prims = blockout.read_glb(os.path.join(REPO, zone["mesh"]))
    out = []
    for rule in rules:
        for name, points in scatter.scatter(prims, rule).items():
            mesh = import_kit_mesh(name, materials)
            if not mesh or not points:
                continue
            transforms = [unreal.Transform(location=unreal.Vector(x * 100.0, z * 100.0, y * 100.0),
                                           rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw),
                                           scale=unreal.Vector(sc, sc, sc))
                          for x, y, z, yaw, sc in points]
            out.append(("Scatter_%d_%s_%s" % (zone["rid"], rule["name"], name), mesh, transforms, rule))
            log("zone %d scatter %s/%s: %d instances" % (zone["rid"], rule["name"], name, len(points)))
    return out


def new_level(path):
    if not level_sub.new_level(path):
        raise RuntimeError("could not create " + path)


def build_zone_level(zone, parts, sharers, scatters=()):
    path = zone_level_path(zone["rid"])
    new_level(path)
    ox, oy, oz = zone["world_origin_cm"]
    for mesh, label, role in parts:
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(ox, oy, oz), unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        if role == "collision":
            comp.set_visibility(False)  # still blocks; not rendered, no shadows, no Lumen
        elif role in ("render", "art"):
            comp.set_collision_profile_name("NoCollision")
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        a.tags = [unreal.Name("Zone" + role.capitalize())] + [unreal.Name("Zone%d" % r) for r in [zone["rid"]] + sharers]
    for label, mesh, transforms, rule in scatters:
        a = actors.spawn_actor_from_class(unreal.MRScatterActor, unreal.Vector(ox, oy, oz), unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        cull = rule.get("cull_m", [40, 60])
        a.set_scatter(mesh, transforms, cull[0] * 100.0, cull[1] * 100.0, bool(rule.get("shadows", True)))
        a.tags = [unreal.Name("ZoneScatter")] + [unreal.Name("Zone%d" % r) for r in [zone["rid"]] + sharers]
    if not level_sub.save_current_level():
        raise RuntimeError("could not save " + path)
    return path


def spawn(cls, loc=unreal.Vector(0, 0, 0), rot=unreal.Rotator(0, 0, 0), label=None):
    a = actors.spawn_actor_from_class(cls, loc, rot)
    if label:
        a.set_actor_label(label)
    return a


def build_persistent_level(zone_levels):
    new_level(WORLD_PATH)
    # one sun + sky for the outdoor zones (interiors get their own lights later)
    sun = spawn(unreal.DirectionalLight, rot=unreal.Rotator(roll=0, pitch=-40, yaw=-30), label="Sun")
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sun.light_component.set_editor_property("intensity", 8.0)
    spawn(unreal.SkyAtmosphere, label="SkyAtmosphere")
    sky = spawn(unreal.SkyLight, label="SkyLight")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.ExponentialHeightFog, label="HeightFog")
    spawn(unreal.VolumetricCloud, label="Clouds")
    pp = spawn(unreal.PostProcessVolume, label="GlobalPostProcess")
    pp.set_editor_property("unbound", True)
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


def main():
    layout = json.load(open(LAYOUT, encoding="utf-8"))
    zones = layout["zones"]
    reset_generated()
    materials = ZoneMaterials()

    # which zones reuse another zone's geometry
    sharers = {}
    for z in zones:
        sg = z.get("shares_geometry_with")
        if sg:
            sharers.setdefault(sg["rid"], []).append(z["rid"])

    zone_levels, bad = [], 0
    for z in zones:
        if "shares_geometry_with" in z:
            log("zone %d uses the geometry level of zone %d" % (z["rid"], z["shares_geometry_with"]["rid"]))
            continue
        parts = import_zone_parts(z, materials)
        bad += 0 if check_orientation(z, parts[0][0]) else 1
        scatters = zone_scatter(z, materials) if hasattr(unreal, "MRScatterActor") else []
        zone_levels.append(build_zone_level(z, parts, sharers.get(z["rid"], []), scatters))

    build_persistent_level(zone_levels)
    if bad:
        raise RuntimeError("%d zone meshes imported with the wrong orientation" % bad)


try:
    main()
finally:
    if "-keep-open" not in os.environ.get("MR_BUILD_WORLD_ARGS", ""):
        unreal.SystemLibrary.quit_editor()
