"""
Import a MakeHuman character kit (tools/blender/mpfb_character.py output) into the project.
Runs inside the Unreal Editor:

    set MR_CHAR_KIT=E:/.../art_src/characters/MPFB_Male
    UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript="<repo>/tools/ue/import_character.py" -unattended -nosplash -RenderOffscreen

(tools/ue/import_character.ps1 wraps this.)  Everything written is our own CC0-derived content
and is committed:

  /Game/Characters/Materials/M_CharacterSkin   shared masters, created if missing
                            /M_CharacterCards  (alpha-masked, two-sided: brows, lashes, hair)
                            /M_CharacterEye
  /Game/Characters/<Name>/SKM_<Name>            body + eyes + brows + lashes, head morph targets,
                                                on /Game/Characters/Mannequins/Meshes/SK_Mannequin
                         /SKM_<Name>_Hair_<s>   one per hairstyle, same morphs
                         /Textures/*            the kit's textures
                         /MI_<Name>_<Slot>      material instances (skin, eyes, brows, lashes, hair)
                         /DA_<Name>             appearance: body is the visible animation driver,
                                                default hair as a part, head sliders, hair options
"""
import json
import os

import unreal

KIT = os.environ.get("MR_CHAR_KIT", "")
REBUILD_MATERIALS = os.environ.get("MR_REBUILD_MATERIALS", "") == "1"
SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
DRIVER_ANIM = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"
MATERIALS = "/Game/Characters/Materials"
WHITE = "/Engine/EngineResources/WhiteSquareTexture"

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[import_character] " + msg)


# ------------------------------------------------------------------------------- materials

def _new_material(name, setup):
    path = "%s/%s" % (MATERIALS, name)
    if eal.does_asset_exist(path):
        if not REBUILD_MATERIALS:
            return eal.load_asset(path)
        eal.delete_asset(path)
    mat = tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    # Usage flags are only set automatically in the editor UI; without them a standalone game
    # silently renders the default grey material on skinned / morphing meshes.
    mat.set_editor_property("used_with_skeletal_mesh", True)
    mat.set_editor_property("used_with_morph_targets", True)
    setup(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("created " + path)
    return mat


def _tex_param(mat, name, x, y):
    t = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    t.set_editor_property("parameter_name", name)
    t.set_editor_property("texture", eal.load_asset(WHITE))
    return t


def _scalar(mat, name, value, x, y):
    s = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property("parameter_name", name)
    s.set_editor_property("default_value", value)
    return s


def _tinted_base_color(mat):
    tex = _tex_param(mat, "BaseColorTex", -700, -100)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 150)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, -50)
    mel.connect_material_expressions(tex, "RGB", mul, "A")
    mel.connect_material_expressions(tint, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    return tex


def master_materials():
    def skin(mat):
        _tinted_base_color(mat)
        mel.connect_material_property(_scalar(mat, "Roughness", 0.6, -350, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)

    def cards(mat):
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("two_sided", True)
        tex = _tinted_base_color(mat)
        mel.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        mel.connect_material_property(_scalar(mat, "Roughness", 0.75, -350, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)

    def eye(mat):
        tex = _tex_param(mat, "BaseColorTex", -500, -100)
        mel.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(_scalar(mat, "Roughness", 0.15, -350, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)

    return {"skin": _new_material("M_CharacterSkin", skin),
            "cards": _new_material("M_CharacterCards", cards),
            "eye": _new_material("M_CharacterEye", eye)}


def material_instance(folder, name, parent, texture):
    mi = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    if texture is not None:
        mel.set_material_instance_texture_parameter_value(mi, "BaseColorTex", texture)
    eal.save_loaded_asset(mi)
    return mi


# ---------------------------------------------------------------------------------- import

def import_texture(path, folder):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = folder
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    asset = eal.load_asset("%s/%s" % (folder, os.path.splitext(os.path.basename(path))[0]))
    if asset is None:
        raise RuntimeError("texture import failed: " + path)
    return asset


def import_skeletal(fbx, folder, name, skeleton):
    # The legacy FBX importer takes the target skeleton reliably from Python.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeleton = skeleton
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    ui.create_physics_asset = False
    sk = ui.skeletal_mesh_import_data
    for prop, value in (("import_morph_targets", True), ("update_skeleton_reference_pose", False),
                        ("use_t0_as_ref_pose", False), ("convert_scene", True)):
        try:
            sk.set_editor_property(prop, value)
        except Exception:  # noqa: BLE001 - option names vary between engine versions
            log("import option %s not available; skipped" % prop)
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    mesh = eal.load_asset("%s/%s" % (folder, name))
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError("import failed: " + fbx)
    if mesh.skeleton.get_path_name() != skeleton.get_path_name():
        raise RuntimeError("%s imported onto %s, not SK_Mannequin" % (name, mesh.skeleton.get_path_name()))
    return mesh


def assign_slots(mesh, by_slot):
    """Material per slot name (struct elements are copies: build new ones)."""
    mats = []
    for m in mesh.get_editor_property("materials"):
        slot = str(m.get_editor_property("material_slot_name"))
        mi = by_slot.get(slot) or by_slot.get(slot.split(".")[0])
        if mi is None:
            raise RuntimeError("%s: no material for slot %s" % (mesh.get_name(), slot))
        mats.append(unreal.SkeletalMaterial(material_interface=mi, material_slot_name=m.get_editor_property("material_slot_name")))
    mesh.set_editor_property("materials", mats)
    eal.save_loaded_asset(mesh)
    return [str(m.get_editor_property("material_slot_name")) for m in mats]


def morph_count(mesh):
    try:
        return len(mesh.get_editor_property("morph_targets"))
    except Exception:  # noqa: BLE001
        return -1


# ------------------------------------------------------------------------------------ main

def main():
    if not KIT or not os.path.exists(os.path.join(KIT, "manifest.json")):
        raise RuntimeError("set MR_CHAR_KIT to a character kit folder with manifest.json (got %r)" % KIT)
    manifest = json.load(open(os.path.join(KIT, "manifest.json"), encoding="utf-8"))
    name = manifest["name"]
    folder = "/Game/Characters/" + name
    skeleton = eal.load_asset(SKELETON)
    if skeleton is None:
        raise RuntimeError("SK_Mannequin missing (run tools/setup.ps1)")

    # rebuild this character's folder from scratch (nothing else references its contents)
    if eal.does_directory_exist(folder):
        unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        unreal.SystemLibrary.collect_garbage()
        if not eal.delete_directory(folder):
            raise RuntimeError("could not clear %s (open in another editor?)" % folder)

    masters = master_materials()
    tex_folder = folder + "/Textures"

    def tex(rel):
        return import_texture(os.path.join(KIT, rel), tex_folder) if rel else None

    m = manifest["materials"]
    slot_mis = {
        "MR_Skin": material_instance(folder, "MI_%s_Skin" % name, masters["skin"], tex(m["MR_Skin"]["texture"])),
        "MR_Eyes": material_instance(folder, "MI_%s_Eyes" % name, masters["eye"], tex(m["MR_Eyes"]["texture"])),
        "MR_Brows": material_instance(folder, "MI_%s_Brows" % name, masters["cards"], tex(m["MR_Brows"]["texture"])),
        "MR_Lashes": material_instance(folder, "MI_%s_Lashes" % name, masters["cards"], tex(m["MR_Lashes"]["texture"])),
    }

    body = import_skeletal(os.path.join(KIT, manifest["files"]["body"]), folder, "SKM_" + name, skeleton)
    slots = assign_slots(body, slot_mis)
    log("body SKM_%s: slots %s, %d morph targets" % (name, slots, morph_count(body)))

    hairs = []
    for h in manifest["files"]["hair"]:
        hname = "SKM_%s_Hair_%s" % (name, h["style"])
        mi = material_instance(folder, "MI_%s_Hair_%s" % (name, h["style"]), masters["cards"], tex(h["texture"]))
        hair = import_skeletal(os.path.join(KIT, h["fbx"]), folder, hname, skeleton)
        assign_slots(hair, {"MR_Hair": mi})
        log("hair %s: %d morph targets" % (hname, morph_count(hair)))
        hairs.append(hair)

    da = tools.create_asset("DA_" + name, folder, unreal.MRCharacterAppearance, unreal.DataAssetFactory())
    da.set_editor_property("driver_mesh", body)
    da.set_editor_property("driver_anim_class", eal.load_asset(DRIVER_ANIM).generated_class())
    da.set_editor_property("driver_visible", True)
    da.set_editor_property("first_person_hidden_bone", unreal.Name("head"))
    if hairs:
        part = unreal.MRAppearancePart()
        part.set_editor_property("name", unreal.Name("Hair"))
        part.set_editor_property("mesh", hairs[0])
        part.set_editor_property("attach_to", unreal.Name("Driver"))
        part.set_editor_property("leader_pose", True)
        part.set_editor_property("hide_in_first_person", True)
        da.set_editor_property("parts", [part])
        da.set_editor_property("hair_styles", hairs)
    sliders = []
    for s in manifest["sliders"]:
        hs = unreal.MRHeadSlider()
        hs.set_editor_property("name", unreal.Name(s["name"]))
        hs.set_editor_property("decr_morph", unreal.Name(s["decr"] or "None"))
        hs.set_editor_property("incr_morph", unreal.Name(s["incr"] or "None"))
        sliders.append(hs)
    da.set_editor_property("head_sliders", sliders)
    eal.save_loaded_asset(da)
    log("wrote %s/DA_%s: %d sliders, %d hairstyles" % (folder, name, len(sliders), len(hairs)))


try:
    main()
except Exception as e:  # noqa: BLE001
    unreal.log_error("[import_character] FAILED: %s" % e)
finally:
    if "-keep-open" not in os.environ.get("MR_BUILD_WORLD_ARGS", ""):
        unreal.SystemLibrary.quit_editor()
