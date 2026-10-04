"""
Zone materials for build_world.py (ADR 0003, pass 1). Runs inside the Unreal Editor.

Every blockout material slot is named after its original texture ("grdNNNNN"). A slot gets:
  1. the material named in data/environment/materials.json, if there is one (real art, hand-made,
     anywhere under /Game outside /Game/Generated), else
  2. a generated placeholder instance MI_<grd> built from tools/textures/make_placeholders.py
     (upscaled original + luminance normal map), else
  3. whatever the glTF importer made (flat pastel colour).

Generated assets (all under /Game/Generated/Environment, git-ignored, rebuilt each run):
  Materials/M_Placeholder, M_PlaceholderMasked   masters: BaseColor * Tint, Normal (strength), Roughness
  Materials/MI_<grd>                             one instance per original texture
  Textures/T_<grd>_D, T_<grd>_N
"""
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLACEHOLDERS = os.path.join(REPO, "build", "textures_placeholder")
OVERRIDES = os.path.join(REPO, "data", "environment", "materials.json")
ENV = "/Game/Generated/Environment"
MAT_DIR = ENV + "/Materials"
TEX_DIR = ENV + "/Textures"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[environment_materials] " + msg)


def _expr(mat, cls, x, y, **props):
    e = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def build_master(name, masked):
    """BaseColor (sRGB) * Tint -> Base Color; lerp(flat, Normal, NormalStrength) -> Normal;
    Roughness scalar; masked variant clips on BaseColor alpha."""
    mat = asset_tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    base = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -200,
                 parameter_name="BaseColor", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                 texture=eal.load_asset("/Engine/EngineResources/DefaultTexture"))
    tint = _expr(mat, unreal.MaterialExpressionVectorParameter, -700, 50,
                 parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    mul = _expr(mat, unreal.MaterialExpressionMultiply, -350, -150)
    mel.connect_material_expressions(base, "RGB", mul, "A")
    mel.connect_material_expressions(tint, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)

    nrm = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 250,
                parameter_name="Normal", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                texture=eal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
    flat = _expr(mat, unreal.MaterialExpressionConstant3Vector, -700, 500,
                 constant=unreal.LinearColor(0, 0, 1, 0))
    strength = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 600,
                     parameter_name="NormalStrength", default_value=1.0)
    lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -350, 350)
    mel.connect_material_expressions(flat, "", lerp, "A")
    mel.connect_material_expressions(nrm, "RGB", lerp, "B")
    mel.connect_material_expressions(strength, "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_NORMAL)

    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -350, 700,
                  parameter_name="Roughness", default_value=0.85)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    if masked:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mel.connect_material_property(base, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def import_textures(files):
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = os.path.join(PLACEHOLDERS, f)
        t.destination_path = TEX_DIR
        t.automated = True
        t.replace_existing = True
        t.save = False
        tasks.append(t)
    asset_tools.import_asset_tasks(tasks)


def build_placeholders():
    """-> {grd key: MaterialInstanceConstant}. Empty if make_placeholders.py hasn't been run."""
    manifest_path = os.path.join(PLACEHOLDERS, "placeholders.json")
    if not os.path.exists(manifest_path):
        log("no %s; run tools/textures/make_placeholders.py for textured placeholders" % manifest_path)
        return {}
    textures = json.load(open(manifest_path, encoding="utf-8"))["textures"]
    masters = {False: build_master("M_Placeholder", False), True: build_master("M_PlaceholderMasked", True)}

    import_textures([t[k] for t in textures.values() for k in ("d", "n")])
    out = {}
    for key, t in sorted(textures.items()):
        d = eal.load_asset("%s/%s" % (TEX_DIR, t["d"][:-4]))
        n = eal.load_asset("%s/%s" % (TEX_DIR, t["n"][:-4]))
        if not d or not n:
            log("WARNING: textures for %s did not import" % key)
            continue
        n.set_editor_property("srgb", False)
        n.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        n.set_editor_property("flip_green_channel", False)  # written DirectX-style already
        eal.save_loaded_asset(n)
        eal.save_loaded_asset(d)

        mi = asset_tools.create_asset("MI_" + key, MAT_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, masters[t["masked"]])
        mel.set_material_instance_texture_parameter_value(mi, "BaseColor", d)
        mel.set_material_instance_texture_parameter_value(mi, "Normal", n)
        mel.set_material_instance_scalar_parameter_value(mi, "Roughness", t["roughness"])
        mel.set_material_instance_scalar_parameter_value(mi, "NormalStrength", 1.0)
        eal.save_loaded_asset(mi)
        out[key] = mi
    log("built %d placeholder material instances" % len(out))
    return out


def load_overrides():
    """data/environment/materials.json: {"materials": {"grdNNNNN": "/Game/Environment/.../MI_X"}}"""
    if not os.path.exists(OVERRIDES):
        return {}
    out = {}
    for key, path in json.load(open(OVERRIDES, encoding="utf-8")).get("materials", {}).items():
        mat = eal.load_asset(path)
        if mat:
            out[key] = mat
        else:
            log("WARNING: %s -> %s does not exist; using the placeholder" % (key, path))
    return out


class ZoneMaterials:
    def __init__(self):
        # build_world.reset_generated() has already emptied ENV
        self.placeholders = build_placeholders()
        self.overrides = load_overrides()

    def apply(self, mesh):
        """Assign by slot name; returns (overridden, placeholder, untouched) slot counts."""
        counts = [0, 0, 0]
        for i, slot in enumerate(mesh.get_editor_property("static_materials")):
            key = str(slot.get_editor_property("material_slot_name"))
            mat = self.overrides.get(key)
            kind = 0
            if not mat:
                mat, kind = self.placeholders.get(key), 1
            if not mat:
                counts[2] += 1
                continue
            mesh.set_material(i, mat)
            counts[kind] += 1
        eal.save_loaded_asset(mesh)
        return counts
