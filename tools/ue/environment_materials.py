"""
Zone materials for build_world.py (ADR 0003, pass 1). Runs inside the Unreal Editor.

Every blockout material slot is named after its original texture ("grdNNNNN"). A slot gets:
  1. the material named in data/environment/materials.json, if there is one (real art, hand-made,
     anywhere under /Game outside /Game/Generated), else
  2. a generated placeholder instance MI_<grd> built from tools/textures/make_placeholders.py
     (upscaled original + luminance normal map), else
  3. whatever the glTF importer made (flat pastel colour).

Generated assets (all under /Game/Generated/Environment, git-ignored). Each is rebuilt only when
its inputs change (tools/ue/build_cache.py), in place, so meshes and instances that use it stay valid;
functions here pass assets around by path and load them only to build something:
  Materials/M_Placeholder, M_PlaceholderMasked   masters: BaseColor * Tint, Normal (strength), Roughness
  Materials/MI_<grd>                             one instance per original texture
  Materials/M_PlaceholderDisplaced, MI_<grd>__art   for Nanite zone-art meshes: + Height texture and
                                                 Nanite displacement (materials.json "displacement")
  Textures/T_<grd>_D, T_<grd>_N, T_<grd>_H
"""
import json
import os

import unreal

import build_cache

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLACEHOLDERS = os.path.join(REPO, "build", "textures_placeholder")
OVERRIDES = os.path.join(REPO, "data", "environment", "materials.json")
ENV = "/Game/Generated/Environment"
MAT_DIR = ENV + "/Materials"
TEX_DIR = ENV + "/Textures"
# full range of Nanite displacement at DisplacementStrength 1 (5 cm: +-2.5 cm); see displacement_range_cm()
DISPLACEMENT_CM = 5.0

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


# material settings the builders below change; a rebuilt master starts from the defaults again
RESET_PROPERTIES = ("blend_mode", "shading_model", "two_sided", "use_material_attributes", "used_with_nanite",
                    "used_with_instanced_static_meshes", "enable_tessellation", "displacement_scaling")


def _new_material(name):
    """The master material to (re)build: a new asset, or the existing one emptied (the same object,
    so instances and meshes that use it keep pointing at it)."""
    path = "%s/%s" % (MAT_DIR, name)
    if not eal.does_asset_exist(path):
        return asset_tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat = eal.load_asset(path)
    mel.delete_all_material_expressions(mat)
    defaults = unreal.get_default_object(unreal.Material)
    for prop in RESET_PROPERTIES:
        mat.set_editor_property(prop, defaults.get_editor_property(prop))
    return mat


def _master(name, builder, *args):
    """-> path of master material <name>; builder(name, *args) runs only when it or its inputs changed."""
    cache = build_cache.CACHE
    key = cache.key(build_cache.source(builder, _expr, _world_uv, _srgb_to_linear, _new_material), name, args)
    return cache.get_or_build("%s/%s" % (MAT_DIR, name), key, "masters", lambda: builder(name, *args))


def _instance(name, parent, textures=None, scalars=None, vectors=None):
    """-> path of material instance <name> on `parent` (textures and parent are asset paths); rebuilt
    in place when the parent or a value changed."""
    textures, scalars, vectors = textures or {}, scalars or {}, vectors or {}
    path = "%s/%s" % (MAT_DIR, name)

    def build():
        mi = eal.load_asset(path) if eal.does_asset_exist(path) else asset_tools.create_asset(
            name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.clear_all_material_instance_parameters(mi)
        mel.set_material_instance_parent(mi, eal.load_asset(parent))
        for k, v in sorted(textures.items()):
            mel.set_material_instance_texture_parameter_value(mi, k, eal.load_asset(v))
        for k, v in sorted(scalars.items()):
            mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
        for k, v in sorted(vectors.items()):
            mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v))
        eal.save_loaded_asset(mi)

    cache = build_cache.CACHE
    key = cache.key(build_cache.source(_instance), parent, textures, scalars, vectors)
    return cache.get_or_build(path, key, "instances", build)


def build_master(name, masked):
    """BaseColor (sRGB) * Tint -> Base Color; lerp(flat, Normal, NormalStrength) -> Normal;
    Roughness scalar; masked variant clips on BaseColor alpha."""
    mat = _new_material(name)
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


def _texture_settings(tex, srgb, normal):
    tex.set_editor_property("srgb", srgb)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", False)  # written DirectX-style already


def ensure_textures(specs):
    """specs: [(file in build/textures_placeholder, srgb, normal map)] -> {file: texture asset path}.
    Imports, in one batch, only the files whose contents (or settings) changed since the last build."""
    cache = build_cache.CACHE
    out, stale = {}, []
    for f, srgb, normal in dict(((s[0], s) for s in specs)).values():
        path = "%s/%s" % (TEX_DIR, os.path.splitext(f)[0])
        key = cache.key(build_cache.file_digest(os.path.join(PLACEHOLDERS, f)), srgb, normal,
                        build_cache.source(_texture_settings))
        out[f] = path
        if cache.fresh(path, key):
            cache.done(path, key, "textures", False)
        else:
            stale.append((f, srgb, normal, path, key))
    if stale:
        import_textures([s[0] for s in stale])
        for f, srgb, normal, path, key in stale:
            tex = eal.load_asset(path)
            if not tex:
                log("WARNING: %s did not import" % f)
                out.pop(f)
                continue
            _texture_settings(tex, srgb, normal)
            eal.save_loaded_asset(tex)
            cache.done(path, key, "textures", True)
    return out


def build_displaced_master(name, default_height, tessellation=True, range_cm=DISPLACEMENT_CM):
    """M_PlaceholderDisplaced, for Nanite zone-art meshes (docs/adr/0003, displacement test).
    Same inputs as M_Placeholder plus a Height texture; built with Material Attributes because the
    Python MaterialProperty enum has no Displacement pin:
        Displacement = 0.5 + (Height - 0.5) * DisplacementStrength * VertexColor.R
    The art meshes paint vertex colour R = 0 along corners and openings (build_zone_art.py), so
    faces meeting at an angle don't crack apart. The material's DisplacementScaling turns the
    0..1 output into +-DISPLACEMENT_CM / 2."""
    mat = _new_material(name)
    mat.set_editor_property("use_material_attributes", True)
    mat.set_editor_property("used_with_nanite", True)
    mat.set_editor_property("enable_tessellation", tessellation)
    scaling = mat.get_editor_property("displacement_scaling")
    scaling.set_editor_property("magnitude", range_cm)
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
                   texture=eal.load_asset(default_height))
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


def build_ground_master(name, macro):
    """M_Ground: world-aligned floors. The original texture is sampled at its own repeat size and
    at 2.73x that, blended by macro noise G, so the 64 px originals stop reading as a grid of
    repeats; macro noise R tints between TintA and TintB for large-scale colour variation."""
    mat = _new_material(name)
    macro = eal.load_asset(macro)
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
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, texture=macro)
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


def build_grass_material(name, macro, grass):
    """M_Grass for the scatter tufts (build_grass_kit.py): root-to-tip gradient from vertex colour R,
    per-blade variation from G, the same macro tint as M_Ground, two-sided, wind from the engine's
    SimpleGrassWind weighted by height."""
    mat = _new_material(name)
    macro = eal.load_asset(macro)
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
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, texture=macro)
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
        # gusts: a slowly drifting macro-noise field (B) scales the wind between calm and full
        gust_uv = _world_uv(mat, -1500, 1100, "GustCm", float(grass.get("gust_cm", 9000.0)))
        gust_pan = _expr(mat, unreal.MaterialExpressionPanner, -1100, 1100, speed_x=0.012, speed_y=0.005)
        mel.connect_material_expressions(gust_uv, "", gust_pan, "Coordinate")
        gust_tex = _expr(mat, unreal.MaterialExpressionTextureSample, -950, 1100, texture=macro,
                         sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        mel.connect_material_expressions(gust_pan, "", gust_tex, "UVs")
        gust_sq = _expr(mat, unreal.MaterialExpressionMultiply, -800, 1100)
        mel.connect_material_expressions(gust_tex, "B", gust_sq, "A")
        mel.connect_material_expressions(gust_tex, "B", gust_sq, "B")
        gust = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -650, 1100,
                     const_a=float(grass.get("calm", 0.25)), const_b=1.0)
        mel.connect_material_expressions(gust_sq, "", gust, "Alpha")
        base_wind = _expr(mat, unreal.MaterialExpressionScalarParameter, -650, 800, parameter_name="Wind",
                          default_value=float(grass.get("wind", 0.3)))
        intensity = _expr(mat, unreal.MaterialExpressionMultiply, -500, 850)
        mel.connect_material_expressions(base_wind, "", intensity, "A")
        mel.connect_material_expressions(gust, "", intensity, "B")
        speed = _expr(mat, unreal.MaterialExpressionScalarParameter, -600, 950, parameter_name="WindSpeed",
                      default_value=float(grass.get("wind_speed", 0.3)))
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


def build_water_material(name, normal_tex, cfg):
    """M_Water: Single Layer Water for zone water surfaces (the Raza pond). Two world-aligned
    ripple normals panning in different directions, absorption/scattering from materials.json
    "water" (per metre), near-mirror roughness; Lumen gives the reflections and the bed shows
    through by depth."""
    mat = _new_material(name)
    normal_tex = eal.load_asset(normal_tex)

    def lin(name, default):
        c = cfg.get(name, default)
        return unreal.LinearColor(c[0], c[1], c[2], 1.0)

    normals = []
    for i, (scale, sx, sy) in enumerate(((cfg.get("ripple_cm", 260.0), 0.012, 0.005),
                                         (cfg.get("ripple_cm", 260.0) * 0.53, -0.008, 0.011))):
        uv = _world_uv(mat, -1600, -200 + i * 300, "RippleCm%d" % i, scale)
        pan = _expr(mat, unreal.MaterialExpressionPanner, -1000, -200 + i * 300,
                    speed_x=sx * cfg.get("speed", 1.0), speed_y=sy * cfg.get("speed", 1.0))
        mel.connect_material_expressions(uv, "", pan, "Coordinate")
        n = _expr(mat, unreal.MaterialExpressionTextureSample, -800, -200 + i * 300,
                  texture=normal_tex, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        mel.connect_material_expressions(pan, "", n, "UVs")
        normals.append(n)
    add = _expr(mat, unreal.MaterialExpressionAdd, -550, 0)
    mel.connect_material_expressions(normals[0], "RGB", add, "A")
    mel.connect_material_expressions(normals[1], "RGB", add, "B")
    flat = _expr(mat, unreal.MaterialExpressionConstant3Vector, -550, 150, constant=unreal.LinearColor(0, 0, 1, 0))
    strength = _expr(mat, unreal.MaterialExpressionScalarParameter, -550, 250, parameter_name="RippleStrength",
                     default_value=float(cfg.get("ripple_strength", 0.5)))
    lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -350, 100)
    mel.connect_material_expressions(flat, "", lerp, "A")
    mel.connect_material_expressions(add, "", lerp, "B")
    mel.connect_material_expressions(strength, "", lerp, "Alpha")
    norm = _expr(mat, unreal.MaterialExpressionNormalize, -200, 100)
    mel.connect_material_expressions(lerp, "", norm, "")
    mel.connect_material_property(norm, "", unreal.MaterialProperty.MP_NORMAL)

    base = _expr(mat, unreal.MaterialExpressionVectorParameter, -350, -300, parameter_name="Color",
                 default_value=lin("color", [0.02, 0.035, 0.03]))
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -350, 350, parameter_name="Roughness",
                  default_value=float(cfg.get("roughness", 0.04)))
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    scatter = _expr(mat, unreal.MaterialExpressionVectorParameter, -350, 500, parameter_name="Scattering",
                    default_value=lin("scattering", [0.02, 0.06, 0.05]))
    absorb = _expr(mat, unreal.MaterialExpressionVectorParameter, -350, 650, parameter_name="Absorption",
                   default_value=lin("absorption", [0.45, 0.12, 0.18]))
    phase = _expr(mat, unreal.MaterialExpressionConstant, -350, 800, r=0.3)
    # created last: an unconnected water output fails every intermediate compile
    out = _expr(mat, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 0, 500)
    for src, names in ((scatter, ("ScatteringCoefficients", "Scattering Coefficients")),
                       (absorb, ("AbsorptionCoefficients", "Absorption Coefficients")),
                       (phase, ("PhaseG", "Phase G"))):
        if not any(mel.connect_material_expressions(src, "", out, n) for n in names):
            log("WARNING: could not connect %s on the Single Layer Water output" % names[0])
    # switch the shading model last: earlier, every edit recompiles a water material without its
    # output node and logs "No inputs to Single Layer Water Material"
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_prop_master(name):
    """M_PropSurface: Color, Emissive, Metallic, Roughness parameters."""
    mat = _new_material(name)
    for i, (prop, name, default) in enumerate((
            (unreal.MaterialProperty.MP_BASE_COLOR, "Color", unreal.LinearColor(0.5, 0.5, 0.5, 1)),
            (unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive", unreal.LinearColor(0, 0, 0, 1)))):
        e = _expr(mat, unreal.MaterialExpressionVectorParameter, -400, i * 200, parameter_name=name, default_value=default)
        mel.connect_material_property(e, "", prop)
    for i, (prop, name, default) in enumerate((
            (unreal.MaterialProperty.MP_METALLIC, "Metallic", 0.0),
            (unreal.MaterialProperty.MP_ROUGHNESS, "Roughness", 0.6))):
        e = _expr(mat, unreal.MaterialExpressionScalarParameter, -400, 400 + i * 120, parameter_name=name, default_value=default)
        mel.connect_material_property(e, "", prop)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_prop_materials(cfg):
    """M_PropSurface + one MI per props.json "materials" slot (ironwork, lamp glass, embers)."""
    if not cfg:
        return {}
    master = _master("M_PropSurface", build_prop_master)
    return {slot: _instance("MI_Prop_" + slot, master,
                            vectors={"Color": v.get("color", [0.5, 0.5, 0.5]) + [1],
                                     "Emissive": v.get("emissive", [0, 0, 0]) + [1]},
                            scalars={"Metallic": v.get("metallic", 0.0), "Roughness": v.get("roughness", 0.6)})
            for slot, v in cfg.items() if not slot.startswith("_")}


def build_placeholders():
    """-> ({grd key: MI path}, {grd key: (manifest entry, D, N, H texture paths)}).
    Both empty if make_placeholders.py hasn't been run."""
    manifest_path = os.path.join(PLACEHOLDERS, "placeholders.json")
    if not os.path.exists(manifest_path):
        log("no %s; run tools/textures/make_placeholders.py for textured placeholders" % manifest_path)
        return {}, {}
    textures = json.load(open(manifest_path, encoding="utf-8"))["textures"]
    masters = {False: _master("M_Placeholder", build_master, False), True: _master("M_PlaceholderMasked", build_master, True)}

    specs = []
    for t in textures.values():
        specs += [(t["d"], True, False), (t["n"], False, True)] + ([(t["height"], False, False)] if "height" in t else [])
    paths = ensure_textures(specs)
    out, loaded = {}, {}
    for key, t in sorted(textures.items()):
        d, n, h = paths.get(t["d"]), paths.get(t["n"]), paths.get(t.get("height"))
        if not d or not n:
            log("WARNING: textures for %s did not import" % key)
            continue
        loaded[key] = (t, d, n, h)
        out[key] = _instance("MI_" + key, masters[t["masked"]], textures={"BaseColor": d, "Normal": n},
                             scalars={"Roughness": t["roughness"], "NormalStrength": 1.0})
    log("%d placeholder material instances (%s)" % (len(out), build_cache.CACHE.summary()))
    return out, loaded


def load_overrides():
    """data/environment/materials.json: {"materials": {"grdNNNNN": "/Game/Environment/.../MI_X"}}"""
    if not os.path.exists(OVERRIDES):
        return {}
    out = {}
    for key, path in json.load(open(OVERRIDES, encoding="utf-8")).get("materials", {}).items():
        if eal.does_asset_exist(path):
            out[key] = path
        else:
            log("WARNING: %s -> %s does not exist; using the placeholder" % (key, path))
    return out


def _materials_json(section):
    if not os.path.exists(OVERRIDES):
        return {}
    return {k: v for k, v in json.load(open(OVERRIDES, encoding="utf-8")).get(section, {}).items() if not k.startswith("_")}


def _materials_value(key, default):
    if not os.path.exists(OVERRIDES):
        return default
    return json.load(open(OVERRIDES, encoding="utf-8")).get(key, default)


def displacement_range_cm():
    """materials.json "displacement_range_cm" (default DISPLACEMENT_CM); the MR_DISPLACEMENT_RANGE_CM
    environment variable overrides it for experiments (tools/lookdev/ai_maps_test.ps1)."""
    if os.environ.get("MR_DISPLACEMENT_RANGE_CM"):
        return float(os.environ["MR_DISPLACEMENT_RANGE_CM"])
    if not os.path.exists(OVERRIDES):
        return DISPLACEMENT_CM
    return float(json.load(open(OVERRIDES, encoding="utf-8")).get("displacement_range_cm", DISPLACEMENT_CM))


def load_variants():
    """data/environment/materials.json "variants": {name: {material parameter: value}}"""
    return _materials_json("variants")


class ZoneMaterials:
    def __init__(self):
        self.placeholders, self.textures = build_placeholders()
        self.overrides = load_overrides()
        self.variants = load_variants()
        self.displacement = _materials_json("displacement")
        facades_json = os.path.join(REPO, "data", "environment", "facades.json")
        self.facade_textures = (set(json.load(open(facades_json, encoding="utf-8")).get("textures", {}))
                                if os.path.exists(facades_json) else set())
        self.art = {}  # slot -> material for zone-art meshes
        self.displaced_master = None
        self.ground_config = _materials_json("ground")
        self.grass_config = _materials_json("grass")
        self.macro = self._import_extra("T_MacroNoise.png", srgb=False)
        self.water_normal = self._import_extra("T_WaterNormal.png", srgb=False, normal=True)
        water_cfg = _materials_json("water")
        self.water = _master("M_Water", build_water_material, self.water_normal, water_cfg) if self.water_normal else None
        self.ground_master = _master("M_Ground", build_ground_master, self.macro) if self.macro and self.ground_config else None
        self.grass = (_master("M_Grass", build_grass_material, self.macro, self.grass_config)
                      if self.macro and self.grass_config else None)
        self.ground = {}
        self.art_flat = {}  # slot -> material for art whose relief is geometry (baked / modelled)
        self.flat_master = None
        props_cfg = os.path.join(REPO, "data", "environment", "props.json")
        self.props = build_prop_materials(json.load(open(props_cfg, encoding="utf-8")).get("materials", {})
                                          if os.path.exists(props_cfg) else {})

    def _import_extra(self, filename, srgb=True, normal=False):
        """Shared textures from make_placeholders.py (macro noise, water normals)."""
        path = os.path.join(PLACEHOLDERS, filename)
        if not os.path.exists(path):
            log("no %s (run make_placeholders.py)" % path)
            return None
        return ensure_textures([(filename, srgb, normal)]).get(filename)

    def displacement_strength(self, base):
        """materials.json "displacement" for the texture, scaled by "displacement_facade_scale" when
        facades.json describes it (the wall is rebuilt with real openings and trims)."""
        strength = float(self.displacement.get(base, 0.0))
        if base in self.facade_textures:
            strength *= float(_materials_value("displacement_facade_scale", 1.0))
        return strength

    def ground_instance(self, key):
        """MI_<grd>__ground on M_Ground for floors listed in materials.json "ground"."""
        if key in self.ground:
            return self.ground[key]
        cfg = self.ground_config.get(key)
        if not cfg or not self.ground_master or key not in self.textures:
            return None
        t, d, n, h = self.textures[key]
        tile = float(cfg.get("tile_m", 2.2)) * 100.0
        self.ground[key] = _instance("MI_%s__ground" % key, self.ground_master, textures={"BaseColor": d, "Normal": n},
                                     scalars={"TileCm": tile, "TileCm2": tile * 2.73, "Roughness": t["roughness"]})
        return self.ground[key]

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
        return _instance("MI_%s__%s%s" % (base, variant, suffix), parent,
                         vectors={k: v for k, v in params.items() if isinstance(v, list)},
                         scalars={k: v for k, v in params.items() if not isinstance(v, list)})

    def art_material_for(self, key, flat=False):
        """Zone-art (Nanite) slots: an opaque instance of M_PlaceholderDisplaced, displaced as much as
        materials.json "displacement" says (0 = not at all). Cut-out originals that the art rebuilt
        as solid geometry (crenellations) are opaque here too. materials.json "materials" still wins."""
        if key in self.overrides:
            return self.overrides[key], 0
        if key == "water":
            return (self.water, 1) if self.water else (None, 2)
        if key.startswith("prop_"):
            mat = self.props.get(key[len("prop_"):])
            return (mat, 1) if mat else (None, 2)
        cache = self.art_flat if flat else self.art
        if key in cache:
            return cache[key], 1
        base, _, variant = key.partition("__")
        if base not in self.textures:
            return None, 2
        t, d, n, h = self.textures[base]
        if not h or (t["masked"] and variant != "solid"):
            # cut-out originals copied into the art (signs, fences) stay cut-out; "__solid" ones
            # were rebuilt as solid geometry (merlons)
            return self.material_for(key)
        if base not in cache:
            if flat:
                # relief already in the geometry: same look, no Nanite tessellation
                if not self.flat_master:
                    self.flat_master = _master("M_PlaceholderArtFlat", build_displaced_master, h, False)
                master, suffix = self.flat_master, "__flat"
            else:
                if not self.displaced_master:
                    self.displaced_master = _master("M_PlaceholderDisplaced", build_displaced_master, h, True, displacement_range_cm())
                master, suffix = self.displaced_master, "__art"
            cache[base] = _instance("MI_%s%s" % (base, suffix), master, textures={"BaseColor": d, "Normal": n, "Height": h},
                                    scalars={"Roughness": t["roughness"], "NormalStrength": 1.0,
                                             "DisplacementStrength": 0.0 if flat else self.displacement_strength(base)})
        if variant:
            cache[key] = self.variant_instance(base, variant, parent=cache[base], suffix="__flat" if flat else "__art")
        return cache[key], 1

    def apply(self, mesh, art=False, flat=False):
        """Assign by slot name (mesh: StaticMesh asset path); returns (overridden, placeholder,
        untouched) slot counts."""
        return assign_materials(mesh, lambda name: self.art_material_for(name, flat) if art else self.material_for(name))


def assign_materials(mesh, choose):
    """choose(slot name) -> (material path or None, kind 0/1/2); returns the counts per kind.
    The mesh is only written when an assignment changed (or it was re-imported), and then all slots
    in one go: set_material() per slot rebuilds the mesh (and its distance field) every time, which
    took minutes on the Raza blockout."""
    cache = build_cache.CACHE
    record = mesh + "#materials"
    slots = cache.info(record).get("slots")  # forgotten when the mesh is re-imported
    obj = None
    if slots is None:
        obj = eal.load_asset(mesh)
        slots = [str(s.get_editor_property("material_slot_name")) for s in obj.get_editor_property("static_materials")]
    counts, wanted = [0, 0, 0], []
    for name in slots:
        mat, kind = choose(name)
        counts[kind] += 1
        wanted.append(mat)
    key = cache.key(slots, wanted, deps=False)  # instances are rebuilt in place: same paths, nothing to reassign
    if cache.fresh(record, key, exists=lambda: True):
        cache.done(record, key, "material assignments", False)
        return counts
    obj = obj or eal.load_asset(mesh)
    current = list(obj.get_editor_property("static_materials"))
    for slot, mat in zip(current, wanted):
        if mat:
            slot.set_editor_property("material_interface", eal.load_asset(mat))
    obj.set_editor_property("static_materials", current)
    eal.save_loaded_asset(obj)
    cache.done(record, key, "material assignments", True, slots=slots)
    return counts
