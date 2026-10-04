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
  Materials/M_PlaceholderDisplaced, MI_<grd>__art   for Nanite zone-art meshes: + Height texture and
                                                 Nanite displacement (materials.json "displacement")
  Textures/T_<grd>_D, T_<grd>_N, T_<grd>_H
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
DISPLACEMENT_CM = 5.0  # full range of Nanite displacement at DisplacementStrength 1 (+-2.5 cm)

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
    mat.set_editor_property("used_with_nanite", True)  # zone art meshes are Nanite
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


def build_displaced_master(default_height):
    """M_PlaceholderDisplaced, for Nanite zone-art meshes (docs/adr/0003, displacement test).
    Same inputs as M_Placeholder plus a Height texture; built with Material Attributes because the
    Python MaterialProperty enum has no Displacement pin:
        Displacement = 0.5 + (Height - 0.5) * DisplacementStrength * VertexColor.R
    The art meshes paint vertex colour R = 0 along corners and openings (build_zone_art.py), so
    faces meeting at an angle don't crack apart. The material's DisplacementScaling turns the
    0..1 output into +-DISPLACEMENT_CM / 2."""
    mat = asset_tools.create_asset("M_PlaceholderDisplaced", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("use_material_attributes", True)
    mat.set_editor_property("used_with_nanite", True)
    mat.set_editor_property("enable_tessellation", True)
    scaling = mat.get_editor_property("displacement_scaling")
    scaling.set_editor_property("magnitude", DISPLACEMENT_CM)
    scaling.set_editor_property("center", 0.5)
    mat.set_editor_property("displacement_scaling", scaling)
    attrs = _expr(mat, unreal.MaterialExpressionMakeMaterialAttributes, 0, 0)

    base = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -300,
                 parameter_name="BaseColor", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                 texture=eal.load_asset("/Engine/EngineResources/DefaultTexture"))
    tint = _expr(mat, unreal.MaterialExpressionVectorParameter, -900, -50,
                 parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    mul = _expr(mat, unreal.MaterialExpressionMultiply, -500, -250)
    mel.connect_material_expressions(base, "RGB", mul, "A")
    mel.connect_material_expressions(tint, "", mul, "B")
    mel.connect_material_expressions(mul, "", attrs, "BaseColor")

    nrm = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 100,
                parameter_name="Normal", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                texture=eal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
    flat = _expr(mat, unreal.MaterialExpressionConstant3Vector, -900, 350, constant=unreal.LinearColor(0, 0, 1, 0))
    strength = _expr(mat, unreal.MaterialExpressionScalarParameter, -900, 450,
                     parameter_name="NormalStrength", default_value=1.0)
    lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -500, 200)
    mel.connect_material_expressions(flat, "", lerp, "A")
    mel.connect_material_expressions(nrm, "RGB", lerp, "B")
    mel.connect_material_expressions(strength, "", lerp, "Alpha")
    mel.connect_material_expressions(lerp, "", attrs, "Normal")

    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -500, 450, parameter_name="Roughness", default_value=0.85)
    mel.connect_material_expressions(rough, "", attrs, "Roughness")

    height = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 650,
                   parameter_name="Height", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
                   texture=default_height)
    centred = _expr(mat, unreal.MaterialExpressionSubtract, -600, 650, const_b=0.5)
    mel.connect_material_expressions(height, "R", centred, "A")
    disp_strength = _expr(mat, unreal.MaterialExpressionScalarParameter, -900, 900,
                          parameter_name="DisplacementStrength", default_value=0.0)
    vcol = _expr(mat, unreal.MaterialExpressionVertexColor, -900, 1000)
    mask = _expr(mat, unreal.MaterialExpressionMultiply, -600, 900)
    mel.connect_material_expressions(disp_strength, "", mask, "A")
    mel.connect_material_expressions(vcol, "R", mask, "B")
    scaled = _expr(mat, unreal.MaterialExpressionMultiply, -400, 700)
    mel.connect_material_expressions(centred, "", scaled, "A")
    mel.connect_material_expressions(mask, "", scaled, "B")
    disp = _expr(mat, unreal.MaterialExpressionAdd, -250, 700, const_b=0.5)
    mel.connect_material_expressions(scaled, "", disp, "A")
    mel.connect_material_expressions(disp, "", attrs, "Displacement")

    mel.connect_material_property(attrs, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def _srgb_to_linear(c):
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


def _world_uv(mat, x, y, scale_param, default_cm, offset=0.0):
    """WorldPosition.xy / <scale_param> (+ offset) -> UV expression."""
    wp = _expr(mat, unreal.MaterialExpressionWorldPosition, x, y)
    rg = _expr(mat, unreal.MaterialExpressionComponentMask, x + 150, y, r=True, g=True, b=False, a=False)
    mel.connect_material_expressions(wp, "", rg, "")
    size = _expr(mat, unreal.MaterialExpressionScalarParameter, x + 150, y + 80, parameter_name=scale_param, default_value=default_cm)
    div = _expr(mat, unreal.MaterialExpressionDivide, x + 300, y)
    mel.connect_material_expressions(rg, "", div, "A")
    mel.connect_material_expressions(size, "", div, "B")
    if not offset:
        return div
    add = _expr(mat, unreal.MaterialExpressionAdd, x + 420, y, const_b=offset)
    mel.connect_material_expressions(div, "", add, "A")
    return add


def build_ground_master(macro):
    """M_Ground: world-aligned floors. The original texture is sampled at its own repeat size and
    at 2.73x that, blended by macro noise G, so the 64 px originals stop reading as a grid of
    repeats; macro noise R tints between TintA and TintB for large-scale colour variation."""
    mat = asset_tools.create_asset("M_Ground", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_nanite", True)
    uv1 = _world_uv(mat, -1500, -300, "TileCm", 220.0)
    uv2 = _world_uv(mat, -1500, 0, "TileCm2", 600.0, offset=0.37)
    uvm = _world_uv(mat, -1500, 300, "MacroCm", 2600.0)
    default = eal.load_asset("/Engine/EngineResources/DefaultTexture")
    c1 = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -400, parameter_name="BaseColor",
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, texture=default)
    c2 = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -100, parameter_name="BaseColor",
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, texture=default)
    mel.connect_material_expressions(uv1, "", c1, "UVs")
    mel.connect_material_expressions(uv2, "", c2, "UVs")
    m = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 250, parameter_name="Macro",
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, texture=macro)
    mel.connect_material_expressions(uvm, "", m, "UVs")
    # blend weight: macro G stretched around the middle
    wsub = _expr(mat, unreal.MaterialExpressionSubtract, -650, 150, const_b=0.35)
    mel.connect_material_expressions(m, "G", wsub, "A")
    wmul = _expr(mat, unreal.MaterialExpressionMultiply, -520, 150, const_b=2.5)
    mel.connect_material_expressions(wsub, "", wmul, "A")
    wsat = _expr(mat, unreal.MaterialExpressionSaturate, -400, 150)
    mel.connect_material_expressions(wmul, "", wsat, "")
    blend = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -400, -250)
    mel.connect_material_expressions(c1, "RGB", blend, "A")
    mel.connect_material_expressions(c2, "RGB", blend, "B")
    mel.connect_material_expressions(wsat, "", blend, "Alpha")
    ta = _expr(mat, unreal.MaterialExpressionVectorParameter, -650, 350, parameter_name="TintA", default_value=unreal.LinearColor(0.82, 0.86, 0.74, 1))
    tb = _expr(mat, unreal.MaterialExpressionVectorParameter, -650, 500, parameter_name="TintB", default_value=unreal.LinearColor(1.12, 1.08, 0.95, 1))
    tint = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -400, 400)
    mel.connect_material_expressions(ta, "", tint, "A")
    mel.connect_material_expressions(tb, "", tint, "B")
    mel.connect_material_expressions(m, "R", tint, "Alpha")
    col = _expr(mat, unreal.MaterialExpressionMultiply, -200, -100)
    mel.connect_material_expressions(blend, "", col, "A")
    mel.connect_material_expressions(tint, "", col, "B")
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)

    nrm = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 650, parameter_name="Normal",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                texture=eal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
    mel.connect_material_expressions(uv1, "", nrm, "UVs")
    flat = _expr(mat, unreal.MaterialExpressionConstant3Vector, -650, 800, constant=unreal.LinearColor(0, 0, 1, 0))
    strength = _expr(mat, unreal.MaterialExpressionScalarParameter, -650, 900, parameter_name="NormalStrength", default_value=0.8)
    lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -400, 700)
    mel.connect_material_expressions(flat, "", lerp, "A")
    mel.connect_material_expressions(nrm, "RGB", lerp, "B")
    mel.connect_material_expressions(strength, "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_NORMAL)
    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -400, 1000, parameter_name="Roughness", default_value=0.92)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def build_grass_material(macro, grass):
    """M_Grass for the scatter tufts (build_grass_kit.py): root-to-tip gradient from vertex colour R,
    per-blade variation from G, the same macro tint as M_Ground, two-sided, wind from the engine's
    SimpleGrassWind weighted by height."""
    mat = asset_tools.create_asset("M_Grass", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    vc = _expr(mat, unreal.MaterialExpressionVertexColor, -1000, 0)
    root = _expr(mat, unreal.MaterialExpressionVectorParameter, -1000, -300, parameter_name="Root",
                 default_value=unreal.LinearColor(*(_srgb_to_linear(grass["root"]) + [1])))
    tip = _expr(mat, unreal.MaterialExpressionVectorParameter, -1000, -150, parameter_name="Tip",
                default_value=unreal.LinearColor(*(_srgb_to_linear(grass["tip"]) + [1])))
    grad = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -700, -200)
    mel.connect_material_expressions(root, "", grad, "A")
    mel.connect_material_expressions(tip, "", grad, "B")
    mel.connect_material_expressions(vc, "R", grad, "Alpha")
    var = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -700, 50, const_a=0.8, const_b=1.2)
    mel.connect_material_expressions(vc, "G", var, "Alpha")
    uvm = _world_uv(mat, -1500, 300, "MacroCm", 2600.0)
    m = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 300, parameter_name="Macro",
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, texture=macro)
    mel.connect_material_expressions(uvm, "", m, "UVs")
    mtint = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -600, 300)
    ta = _expr(mat, unreal.MaterialExpressionVectorParameter, -800, 500, parameter_name="TintA", default_value=unreal.LinearColor(0.82, 0.86, 0.74, 1))
    tb = _expr(mat, unreal.MaterialExpressionVectorParameter, -800, 650, parameter_name="TintB", default_value=unreal.LinearColor(1.12, 1.08, 0.95, 1))
    mel.connect_material_expressions(ta, "", mtint, "A")
    mel.connect_material_expressions(tb, "", mtint, "B")
    mel.connect_material_expressions(m, "R", mtint, "Alpha")
    c1 = _expr(mat, unreal.MaterialExpressionMultiply, -450, -100)
    mel.connect_material_expressions(grad, "", c1, "A")
    mel.connect_material_expressions(var, "", c1, "B")
    c2 = _expr(mat, unreal.MaterialExpressionMultiply, -300, 0)
    mel.connect_material_expressions(c1, "", c2, "A")
    mel.connect_material_expressions(mtint, "", c2, "B")
    mel.connect_material_property(c2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -300, 200, r=0.9)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = _expr(mat, unreal.MaterialExpressionConstant, -300, 280, r=0.25)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    wind_fn = eal.load_asset("/Engine/Functions/Engine_MaterialFunctions01/WorldPositionOffset/SimpleGrassWind")
    if wind_fn:
        wind = _expr(mat, unreal.MaterialExpressionMaterialFunctionCall, -300, 500)
        wind.set_editor_property("material_function", wind_fn)
        intensity = _expr(mat, unreal.MaterialExpressionScalarParameter, -600, 800, parameter_name="Wind", default_value=float(grass.get("wind", 0.6)))
        speed = _expr(mat, unreal.MaterialExpressionConstant, -600, 900, r=0.8)
        mel.connect_material_expressions(intensity, "", wind, "WindIntensity")
        mel.connect_material_expressions(vc, "R", wind, "WindWeight")
        mel.connect_material_expressions(speed, "", wind, "WindSpeed")
        zero = _expr(mat, unreal.MaterialExpressionConstant3Vector, -600, 1000, constant=unreal.LinearColor(0, 0, 0, 0))
        mel.connect_material_expressions(zero, "", wind, "AdditionalWPO")
        mel.connect_material_property(wind, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    else:
        log("WARNING: SimpleGrassWind not found; grass without wind")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def build_placeholders():
    """-> ({grd key: MaterialInstanceConstant}, {grd key: (manifest entry, D, N, H textures)}).
    Both empty if make_placeholders.py hasn't been run."""
    manifest_path = os.path.join(PLACEHOLDERS, "placeholders.json")
    if not os.path.exists(manifest_path):
        log("no %s; run tools/textures/make_placeholders.py for textured placeholders" % manifest_path)
        return {}, {}
    textures = json.load(open(manifest_path, encoding="utf-8"))["textures"]
    masters = {False: build_master("M_Placeholder", False), True: build_master("M_PlaceholderMasked", True)}

    import_textures([t[k] for t in textures.values() for k in ("d", "n", "height") if k in t])
    out, loaded = {}, {}
    for key, t in sorted(textures.items()):
        d = eal.load_asset("%s/%s" % (TEX_DIR, t["d"][:-4]))
        n = eal.load_asset("%s/%s" % (TEX_DIR, t["n"][:-4]))
        h = eal.load_asset("%s/%s" % (TEX_DIR, t["height"][:-4])) if "height" in t else None
        if not d or not n:
            log("WARNING: textures for %s did not import" % key)
            continue
        n.set_editor_property("srgb", False)
        n.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        n.set_editor_property("flip_green_channel", False)  # written DirectX-style already
        eal.save_loaded_asset(n)
        eal.save_loaded_asset(d)
        if h:
            h.set_editor_property("srgb", False)
            eal.save_loaded_asset(h)
        loaded[key] = (t, d, n, h)

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
    return out, loaded


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


def _materials_json(section):
    if not os.path.exists(OVERRIDES):
        return {}
    return {k: v for k, v in json.load(open(OVERRIDES, encoding="utf-8")).get(section, {}).items() if not k.startswith("_")}


def load_variants():
    """data/environment/materials.json "variants": {name: {material parameter: value}}"""
    return _materials_json("variants")


class ZoneMaterials:
    def __init__(self):
        # build_world.reset_generated() has already emptied ENV
        self.placeholders, self.textures = build_placeholders()
        self.overrides = load_overrides()
        self.variants = load_variants()
        self.displacement = _materials_json("displacement")
        self.art = {}  # slot -> material for zone-art meshes
        self.displaced_master = None
        self.ground_config = _materials_json("ground")
        self.grass_config = _materials_json("grass")
        self.macro = self._import_macro()
        self.ground_master = build_ground_master(self.macro) if self.macro and self.ground_config else None
        self.grass = build_grass_material(self.macro, self.grass_config) if self.macro and self.grass_config else None
        self.ground = {}

    def _import_macro(self):
        path = os.path.join(PLACEHOLDERS, "T_MacroNoise.png")
        if not os.path.exists(path):
            log("no %s; ground and grass without macro variation (run make_placeholders.py)" % path)
            return None
        import_textures(["T_MacroNoise.png"])
        tex = eal.load_asset(TEX_DIR + "/T_MacroNoise")
        tex.set_editor_property("srgb", False)
        eal.save_loaded_asset(tex)
        return tex

    def ground_instance(self, key):
        """MI_<grd>__ground on M_Ground for floors listed in materials.json "ground"."""
        if key in self.ground:
            return self.ground[key]
        cfg = self.ground_config.get(key)
        if not cfg or not self.ground_master or key not in self.textures:
            return None
        t, d, n, h = self.textures[key]
        mi = asset_tools.create_asset("MI_%s__ground" % key, MAT_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, self.ground_master)
        mel.set_material_instance_texture_parameter_value(mi, "BaseColor", d)
        mel.set_material_instance_texture_parameter_value(mi, "Normal", n)
        tile = float(cfg.get("tile_m", 2.2)) * 100.0
        mel.set_material_instance_scalar_parameter_value(mi, "TileCm", tile)
        mel.set_material_instance_scalar_parameter_value(mi, "TileCm2", tile * 2.73)
        mel.set_material_instance_scalar_parameter_value(mi, "Roughness", t["roughness"])
        eal.save_loaded_asset(mi)
        self.ground[key] = mi
        return mi

    def material_for(self, key):
        """-> (material, kind) for a slot name; kind 0 = materials.json, 1 = placeholder, 2 = none.
        "<grd>__<variant>" slots (zone art: stained glass, ...) get the <grd> material with the
        variant's parameters from materials.json "variants"."""
        mat = self.overrides.get(key)
        if mat:
            return mat, 0
        ground = self.ground_instance(key)
        if ground:
            return ground, 1
        if key in self.placeholders:
            return self.placeholders[key], 1
        base, _, variant = key.partition("__")
        if variant and base in self.placeholders:
            mi = self.variant_instance(base, variant)
            if mi:
                self.placeholders[key] = mi
                return mi, 1
        return None, 2

    def variant_instance(self, base, variant, parent=None, suffix=""):
        parent = parent or self.placeholders[base]
        params = self.variants.get(variant)
        if params is None:
            log("WARNING: no variant %r in materials.json; %s__%s uses %s" % (variant, base, variant, base))
            return parent
        mi = asset_tools.create_asset("MI_%s__%s%s" % (base, variant, suffix), MAT_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, parent)
        for name, value in params.items():
            if isinstance(value, list):
                mel.set_material_instance_vector_parameter_value(mi, name, unreal.LinearColor(*value))
            else:
                mel.set_material_instance_scalar_parameter_value(mi, name, float(value))
        eal.save_loaded_asset(mi)
        return mi

    def art_material_for(self, key):
        """Zone-art (Nanite) slots: an opaque instance of M_PlaceholderDisplaced, displaced as much as
        materials.json "displacement" says (0 = not at all). Cut-out originals that the art rebuilt
        as solid geometry (crenellations) are opaque here too. materials.json "materials" still wins."""
        if key in self.overrides:
            return self.overrides[key], 0
        if key in self.art:
            return self.art[key], 1
        base, _, variant = key.partition("__")
        if base not in self.textures:
            return None, 2
        t, d, n, h = self.textures[base]
        if not h:
            return self.material_for(key)
        if base not in self.art:
            if not self.displaced_master:
                self.displaced_master = build_displaced_master(h)
            mi = asset_tools.create_asset("MI_%s__art" % base, MAT_DIR, unreal.MaterialInstanceConstant,
                                          unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(mi, self.displaced_master)
            mel.set_material_instance_texture_parameter_value(mi, "BaseColor", d)
            mel.set_material_instance_texture_parameter_value(mi, "Normal", n)
            mel.set_material_instance_texture_parameter_value(mi, "Height", h)
            mel.set_material_instance_scalar_parameter_value(mi, "Roughness", t["roughness"])
            mel.set_material_instance_scalar_parameter_value(mi, "NormalStrength", 1.0)
            mel.set_material_instance_scalar_parameter_value(mi, "DisplacementStrength", float(self.displacement.get(base, 0.0)))
            eal.save_loaded_asset(mi)
            self.art[base] = mi
        if variant:
            self.art[key] = self.variant_instance(base, variant, parent=self.art[base], suffix="__art")
        return self.art[key], 1

    def apply(self, mesh, art=False):
        """Assign by slot name; returns (overridden, placeholder, untouched) slot counts.
        All slots are written in one go: set_material() per slot rebuilds the mesh (and its
        distance field) every time, which took minutes on the Raza blockout."""
        counts = [0, 0, 0]
        slots = list(mesh.get_editor_property("static_materials"))
        for slot in slots:
            name = str(slot.get_editor_property("material_slot_name"))
            mat, kind = self.art_material_for(name) if art else self.material_for(name)
            counts[kind] += 1
            if mat:
                slot.set_editor_property("material_interface", mat)
        mesh.set_editor_property("static_materials", slots)
        eal.save_loaded_asset(mesh)
        return counts
