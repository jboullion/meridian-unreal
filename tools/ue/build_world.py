"""
Build /Game/Generated/Maps/L_World from the zone blockouts. Runs inside the Unreal Editor (Python):

    UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript="<repo>/tools/ue/build_world.py" -unattended -nosplash -RenderOffscreen

(It needs a real editor world, so it runs as -ExecutePythonScript rather than the pythonscript
commandlet, and quits the editor when it finishes; pass -keep-open in MR_BUILD_WORLD_ARGS to stay.)

Steps:
  1. import build/zones/<rid>_<class>.glb -> /Game/Generated/Zones/Z<rid>/ (static mesh, complex-as-simple collision)
  2. create L_World with one actor per zone at its world_origin_cm (data/zone_layout.json)
     zones that share geometry (Raza town + Outskirts) get one mesh, placed once
  3. add sun, sky, sky light, fog, post process; save

Re-running replaces the zone assets and the level, so the level stays a pure function of the data.
Every zone is in one persistent level for now; per-zone streaming levels come with the client
streaming subsystem (Phase 2, step 3).
"""
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAYOUT = os.path.join(REPO, "data", "zone_layout.json")
# Everything this script writes lives under /Game/Generated (git-ignored; rebuild any time).
LEVEL_PATH = "/Game/Generated/Maps/L_World"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def log(msg):
    unreal.log("[build_world] " + msg)


def import_zone_mesh(glb_path, dest_dir):
    if eal.does_directory_exist(dest_dir):
        eal.delete_directory(dest_dir)
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
    body = mesh.get_editor_property("body_setup")
    if body:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    eal.save_loaded_asset(mesh)
    return mesh


def expected_bounds_cm(zone):
    """layout bounds (glTF metres x east, z south) -> UE cm (X east, Y south)."""
    lo, hi = zone["bounds_m"]["min"], zone["bounds_m"]["max"]
    return (lo[0] * 100, lo[2] * 100), (hi[0] * 100, hi[2] * 100)


def spawn(cls, loc=unreal.Vector(0, 0, 0), rot=unreal.Rotator(0, 0, 0), label=None):
    a = actors.spawn_actor_from_class(cls, loc, rot)
    if label:
        a.set_actor_label(label)
    return a


def main():
    layout = json.load(open(LAYOUT, encoding="utf-8"))
    zones = layout["zones"]

    # The level may be open already (it is the editor startup map), so reuse it and clear it
    # rather than deleting the asset.
    if eal.does_asset_exist(LEVEL_PATH):
        if not level_sub.load_level(LEVEL_PATH):
            raise RuntimeError("could not load " + LEVEL_PATH)
        for actor in actors.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
                actors.destroy_actor(actor)
    elif not level_sub.new_level(LEVEL_PATH):
        raise RuntimeError("could not create " + LEVEL_PATH)

    for z in zones:
        if "shares_geometry_with" in z:
            log("zone %d shares geometry with %d, no separate mesh" % (z["rid"], z["shares_geometry_with"]["rid"]))
            continue
        glb = os.path.join(REPO, z["mesh"])
        dest = "/Game/Generated/Zones/Z%d" % z["rid"]
        mesh = import_zone_mesh(glb, dest)
        ox, oy, oz = z["world_origin_cm"]
        a = actors.spawn_actor_from_object(mesh, unreal.Vector(ox, oy, oz), unreal.Rotator(0, 0, 0))
        a.set_actor_label("Zone_%d_%s" % (z["rid"], z["class"]))
        a.set_folder_path("Zones")
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        smc.set_mobility(unreal.ComponentMobility.STATIC)
        a.tags = [unreal.Name("Zone"), unreal.Name("Zone%d" % z["rid"])]

        # orientation check: the importer must map glTF (x, y-up, z) to UE (X east, Y south, Z up)
        bmin, bmax = mesh.get_bounding_box().min, mesh.get_bounding_box().max
        (ex0, ey0), (ex1, ey1) = expected_bounds_cm(z)
        ok = abs(bmin.x - ex0) < 50 and abs(bmax.x - ex1) < 50 and abs(bmin.y - ey0) < 50 and abs(bmax.y - ey1) < 50
        log("zone %d %-14s bounds X[%.0f..%.0f] Y[%.0f..%.0f] Z[%.0f..%.0f] expected X[%.0f..%.0f] Y[%.0f..%.0f] %s"
            % (z["rid"], z["class"], bmin.x, bmax.x, bmin.y, bmax.y, bmin.z, bmax.z, ex0, ex1, ey0, ey1,
               "OK" if ok else "ORIENTATION MISMATCH"))

    # lighting: one sun + sky for the outdoor zones (interiors get their own lights later)
    sun = spawn(unreal.DirectionalLight, rot=unreal.Rotator(roll=0, pitch=-40, yaw=-30), label="Sun")
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sun.light_component.set_editor_property("intensity", 8.0)
    spawn(unreal.SkyAtmosphere, label="SkyAtmosphere")
    sky = spawn(unreal.SkyLight, label="SkyLight")
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    spawn(unreal.ExponentialHeightFog, label="HeightFog")
    spawn(unreal.VolumetricCloud, label="Clouds")
    pp = spawn(unreal.PostProcessVolume, label="GlobalPostProcess")
    pp.set_editor_property("unbound", True)
    # fallback start if the zone subsystem has no data: Raza town square
    spawn(unreal.PlayerStart, unreal.Vector(8910, 4950, 300), label="FallbackStart")

    level_sub.save_current_level()
    log("saved " + LEVEL_PATH)


try:
    main()
finally:
    if "-keep-open" not in os.environ.get("MR_BUILD_WORLD_ARGS", ""):
        unreal.SystemLibrary.quit_editor()
