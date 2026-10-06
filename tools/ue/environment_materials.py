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
  Materials/M_Fire, MI_Fire_<preset>             flame sprites (flipbooks of the original flames)
  Materials/M_Precip, MI_Precip[_<rid>]          rain, snow and sand (AMRPrecipitationActor), with each
                                                 zone's shelter map (Textures/T_Shelter_<rid>)
  Materials/M_Splash, MI_Splash[_<rid>]          rain splashes on the highest surface (the shelter map)
  Materials/M_Bolt, MI_Bolt                      distant lightning bolts (T_Bolts)
  Materials/M_Ice                                the pond's ice in snow (an overlay on water meshes)
Every surface master also gets wet and snowy with MPC_Environment.Wetness and SnowCover
(_weather_surface, docs/adr/0005 phase 4).
"""
import json
import os
import re

import unreal

import build_cache

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLACEHOLDERS = os.path.join(REPO, "build", "textures_placeholder")
SHELTER = os.path.join(REPO, "build", "environment", "shelter")  # tools/environment/shelter.py
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
RESET_PROPERTIES = ("material_domain", "blend_mode", "shading_model", "two_sided", "is_sky", "use_material_attributes", "used_with_nanite",
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
    key = cache.key(build_cache.source(builder, _expr, _world_uv, _srgb_to_linear, _new_material, _window_glow, _atlas_uv,
                                       _weather_surface, _collection, _custom, _ripples, _season, _camera_box_inputs, _puff_inputs,
                                       _matte, _undergrowth, _tree_wind),
                    name, args + (RIPPLE_HLSL, SEASON_HLSL, UNDERGROWTH_HLSL, TREE_WIND_HLSL))
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


MPC_NAME = "MPC_Environment"
# scalars in MPC_Environment: moods set them (moods.json "Collection"; tools/ue/zone_mood.py in the
# editor, UMREnvironmentSubsystem in game), the game clock writes GameHour, the director LampsOn
MPC_SCALARS = {"WindowGlow": 0.0, "GameHour": 0.0, "LampsOn": 1.0, "Stars": 0.0, "WindowDaylight": 0.0,
               "SectorAmbient": 0.0,
               # weather (UMREnvironmentSubsystem, docs/adr/0005 phase 4): how wet / snowed on the
               # outdoor surfaces are, the wind (1 calm), how much rain or snow falls, and which
               "Wetness": 0.0, "SnowCover": 0.0, "Wind": 1.0, "Precip": 0.0, "Snow": 0.0, "Sand": 0.0,
               # atmosphere (phase 5): how dark it is (1 night, 0 day: the sun's elevation), the
               # season's tint (0 or 1 each; summer is the original), how much the chimneys smoke and
               # how many of each ambient particle show (0..1)
               "Night": 0.0, "Spring": 0.0, "Autumn": 0.0, "Winter": 0.0, "Smoke": 0.0,
               "Motes": 0.0, "Pollen": 0.0, "Fireflies": 0.0, "Leaves": 0.0}
# vectors in MPC_Environment (moods.json "Collection" arrays)
MPC_VECTORS = {"AmbientTint": (1.0, 1.0, 1.0, 1.0),
               # the lit windows' colour at night (moods.json "Collection"; _window_glow)
               "WindowGlowColor": (1.0, 0.62, 0.3, 1.0)}


def ensure_mpc():
    """/Game/Generated/Environment/Materials/MPC_Environment with MPC_SCALARS (values kept when it
    exists: the mood sets them)."""
    path = "%s/%s" % (MAT_DIR, MPC_NAME)
    if not eal.does_asset_exist(path):
        asset_tools.create_asset(MPC_NAME, MAT_DIR, unreal.MaterialParameterCollection,
                                 unreal.MaterialParameterCollectionFactoryNew())
    mpc = eal.load_asset(path)
    params = list(mpc.get_editor_property("scalar_parameters"))
    have = {str(p.get_editor_property("parameter_name")) for p in params}
    missing = [k for k in MPC_SCALARS if k not in have]
    for name in missing:
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", MPC_SCALARS[name])
        params.append(p)
    if missing:
        mpc.set_editor_property("scalar_parameters", params)
    vectors = list(mpc.get_editor_property("vector_parameters"))
    have_v = {str(p.get_editor_property("parameter_name")) for p in vectors}
    missing_v = [k for k in MPC_VECTORS if k not in have_v]
    for name in missing_v:
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(*MPC_VECTORS[name]))
        vectors.append(p)
    if missing_v:
        mpc.set_editor_property("vector_parameters", vectors)
    if missing or missing_v:
        eal.save_loaded_asset(mpc)
    return path


def _atlas_uv(mat, atlas, mpc, x, y):
    """UVs into a clock atlas (make_placeholders.clock_layout: `atlas` = (columns, rows), cell i =
    hour i mod 12): the cell for floor(MPC_Environment.GameHour) mod 12, with the face's 0..1 UVs
    clamped inside it (a clock face is mapped once)."""
    cols, rows = atlas
    tc = _expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y)
    clamp = _expr(mat, unreal.MaterialExpressionClamp, x + 150, y, min_default=0.0005, max_default=0.9995)
    mel.connect_material_expressions(tc, "", clamp, "")
    hour = _expr(mat, unreal.MaterialExpressionCollectionParameter, x, y + 150,
                 collection=eal.load_asset(mpc), parameter_name="GameHour")
    h12 = _expr(mat, unreal.MaterialExpressionFmod, x + 150, y + 150)
    mel.connect_material_expressions(hour, "", h12, "A")
    twelve = _expr(mat, unreal.MaterialExpressionConstant, x, y + 250, r=12.0)
    mel.connect_material_expressions(twelve, "", h12, "B")
    frame = _expr(mat, unreal.MaterialExpressionFloor, x + 300, y + 150)
    mel.connect_material_expressions(h12, "", frame, "")
    ncols = _expr(mat, unreal.MaterialExpressionConstant, x + 300, y + 250, r=float(cols))
    col = _expr(mat, unreal.MaterialExpressionFmod, x + 450, y + 150)
    mel.connect_material_expressions(frame, "", col, "A")
    mel.connect_material_expressions(ncols, "", col, "B")
    rowf = _expr(mat, unreal.MaterialExpressionDivide, x + 450, y + 250)
    mel.connect_material_expressions(frame, "", rowf, "A")
    mel.connect_material_expressions(ncols, "", rowf, "B")
    row = _expr(mat, unreal.MaterialExpressionFloor, x + 600, y + 250)
    mel.connect_material_expressions(rowf, "", row, "")
    cell = _expr(mat, unreal.MaterialExpressionAppendVector, x + 750, y + 200)
    mel.connect_material_expressions(col, "", cell, "A")
    mel.connect_material_expressions(row, "", cell, "B")
    add = _expr(mat, unreal.MaterialExpressionAdd, x + 900, y + 50)
    mel.connect_material_expressions(clamp, "", add, "A")
    mel.connect_material_expressions(cell, "", add, "B")
    size = _expr(mat, unreal.MaterialExpressionConstant2Vector, x + 900, y + 200, r=float(cols), g=float(rows))
    uv = _expr(mat, unreal.MaterialExpressionDivide, x + 1050, y + 100)
    mel.connect_material_expressions(add, "", uv, "A")
    mel.connect_material_expressions(size, "", uv, "B")
    return uv


def _window_glow(mat, base_rgb, glow, x, y, uv=None):
    """Emissive for painted windows, inside the Emissive mask (T_<grd>_E): at night, the glass's
    brightness pattern (GlowTint of its own colour) * MPC_Environment.WindowGlowColor * WindowGlow *
    GlowGain, so the leading and frames stay dark; by day from inside, the glass colour * cool
    daylight * WindowDaylight (set only while the view is in an interior, docs/adr/0005).

    Plus the ambient floor of interiors and underground zones: base colour * the original sector light
    level (vertex colour R, roo2gltf) * MPC_Environment.SectorAmbient * AmbientTint, as the original
    lit each sector at its own level. SectorAmbient is 0 outdoors, so it only shows inside.
    glow = (MPC path, default mask texture path)."""
    mpc, default_mask = glow
    mask = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name="Emissive",
                 sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, texture=eal.load_asset(default_mask))
    if uv:
        mel.connect_material_expressions(uv, "", mask, "UVs")
    collection = eal.load_asset(mpc)
    # night, seen from outside: one colour for every window (MPC_Environment.WindowGlowColor x
    # WindowGlow), patterned by the painted glass's brightness so the leading and frames stay dark.
    # GlowGain evens out how brightly each texture's glass is painted (make_placeholders.py
    # glow_gain, x facades.json "glow_gain"); GlowTint keeps that much of the glass's own colour
    # (stained glass). 2026-10-06: the glass's own colour made every building glow differently.
    lum = _expr(mat, unreal.MaterialExpressionDotProduct, x, y + 160)
    mel.connect_material_expressions(base_rgb, "", lum, "A")
    weights = _expr(mat, unreal.MaterialExpressionConstant3Vector, x - 150, y + 200, constant=unreal.LinearColor(0.2126, 0.7152, 0.0722, 0))
    mel.connect_material_expressions(weights, "", lum, "B")
    tint = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 260, parameter_name="GlowTint", default_value=1.0)
    glass = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 150, y + 200)
    mel.connect_material_expressions(lum, "", glass, "A")
    mel.connect_material_expressions(base_rgb, "", glass, "B")
    mel.connect_material_expressions(tint, "", glass, "Alpha")
    color = _expr(mat, unreal.MaterialExpressionCollectionParameter, x, y + 330, collection=collection, parameter_name="WindowGlowColor")
    color_rgb = _expr(mat, unreal.MaterialExpressionComponentMask, x + 150, y + 330, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(color, "", color_rgb, "")
    level = _expr(mat, unreal.MaterialExpressionCollectionParameter, x, y + 400, collection=collection, parameter_name="WindowGlow")
    gain = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 470, parameter_name="GlowGain", default_value=1.0)
    k = _expr(mat, unreal.MaterialExpressionMultiply, x + 150, y + 430)
    mel.connect_material_expressions(level, "", k, "A")
    mel.connect_material_expressions(gain, "", k, "B")
    lamp = _expr(mat, unreal.MaterialExpressionMultiply, x + 300, y + 380)
    mel.connect_material_expressions(color_rgb, "", lamp, "A")
    mel.connect_material_expressions(k, "", lamp, "B")
    night = _expr(mat, unreal.MaterialExpressionMultiply, x + 450, y + 250)
    mel.connect_material_expressions(glass, "", night, "A")
    mel.connect_material_expressions(lamp, "", night, "B")
    # by day, seen from inside: daylight through the glass, in the glass's own colour
    cool = _expr(mat, unreal.MaterialExpressionConstant3Vector, x, y + 560, constant=unreal.LinearColor(0.85, 0.92, 1.0, 0))
    day = _expr(mat, unreal.MaterialExpressionCollectionParameter, x, y + 640, collection=collection, parameter_name="WindowDaylight")
    cool_l = _expr(mat, unreal.MaterialExpressionMultiply, x + 150, y + 600)
    mel.connect_material_expressions(cool, "", cool_l, "A")
    mel.connect_material_expressions(day, "", cool_l, "B")
    daylit = _expr(mat, unreal.MaterialExpressionMultiply, x + 300, y + 560)
    mel.connect_material_expressions(base_rgb, "", daylit, "A")
    mel.connect_material_expressions(cool_l, "", daylit, "B")
    col = _expr(mat, unreal.MaterialExpressionAdd, x + 600, y + 300)
    mel.connect_material_expressions(night, "", col, "A")
    mel.connect_material_expressions(daylit, "", col, "B")
    em = _expr(mat, unreal.MaterialExpressionMultiply, x + 750, y + 100)
    mel.connect_material_expressions(col, "", em, "A")
    mel.connect_material_expressions(mask, "R", em, "B")
    # ambient floor: base * sector light (vertex colour) * SectorAmbient * AmbientTint
    vcol = _expr(mat, unreal.MaterialExpressionVertexColor, x, y + 700)
    amb = _expr(mat, unreal.MaterialExpressionCollectionParameter, x, y + 820, collection=collection, parameter_name="SectorAmbient")
    tint = _expr(mat, unreal.MaterialExpressionCollectionParameter, x, y + 920, collection=collection, parameter_name="AmbientTint")
    tint_rgb = _expr(mat, unreal.MaterialExpressionComponentMask, x + 150, y + 920, r=True, g=True, b=True)
    mel.connect_material_expressions(tint, "", tint_rgb, "")
    level = _expr(mat, unreal.MaterialExpressionMultiply, x + 150, y + 760)
    mel.connect_material_expressions(vcol, "R", level, "A")
    mel.connect_material_expressions(amb, "", level, "B")
    lit = _expr(mat, unreal.MaterialExpressionMultiply, x + 300, y + 820)
    mel.connect_material_expressions(level, "", lit, "A")
    mel.connect_material_expressions(tint_rgb, "", lit, "B")
    ambient = _expr(mat, unreal.MaterialExpressionMultiply, x + 450, y + 760)
    mel.connect_material_expressions(base_rgb, "", ambient, "A")
    mel.connect_material_expressions(lit, "", ambient, "B")
    total = _expr(mat, unreal.MaterialExpressionAdd, x + 750, y + 300)
    mel.connect_material_expressions(em, "", total, "A")
    mel.connect_material_expressions(ambient, "", total, "B")
    return total


def _collection(mat, name, x, y):
    """MPC_Environment.<name>."""
    return _expr(mat, unreal.MaterialExpressionCollectionParameter, x, y,
                 collection=eal.load_asset(ensure_mpc()), parameter_name=name)


def _weather_surface(mat, base, normal, rough, x, y):
    """Wet and snowy surfaces (docs/adr/0005 phase 4), from MPC_Environment.Wetness and SnowCover
    (set by the environment director outdoors; 0 inside). base, normal, rough: (expression, output)
    or None -> the same, weathered:
      wet: darker base colour (x0.6), glossier (floors to roughness 0.12, walls only to 0.45);
      snow: on surfaces facing up (world normal z > 0.55), white, rough, with a flattened normal."""
    def link(src, dst, pin):
        mel.connect_material_expressions(src[0], src[1], dst, pin)
    wet = _collection(mat, "Wetness", x, y)
    cover = _collection(mat, "SnowCover", x, y + 100)
    vn = _expr(mat, unreal.MaterialExpressionVertexNormalWS, x, y + 200)
    nz = _expr(mat, unreal.MaterialExpressionComponentMask, x + 150, y + 200, r=False, g=False, b=True, a=False)
    mel.connect_material_expressions(vn, "", nz, "")
    # 0 on walls, 1 on floors
    up0 = _expr(mat, unreal.MaterialExpressionSubtract, x + 300, y + 200, const_b=0.2)
    mel.connect_material_expressions(nz, "", up0, "A")
    up1 = _expr(mat, unreal.MaterialExpressionMultiply, x + 420, y + 200, const_b=1.25)
    mel.connect_material_expressions(up0, "", up1, "A")
    up = _expr(mat, unreal.MaterialExpressionSaturate, x + 540, y + 200)
    mel.connect_material_expressions(up1, "", up, "")
    # snow lies where the surface faces up
    s0 = _expr(mat, unreal.MaterialExpressionSubtract, x + 300, y + 320, const_b=0.55)
    mel.connect_material_expressions(nz, "", s0, "A")
    s1 = _expr(mat, unreal.MaterialExpressionMultiply, x + 420, y + 320, const_b=4.0)
    mel.connect_material_expressions(s0, "", s1, "A")
    s2 = _expr(mat, unreal.MaterialExpressionSaturate, x + 540, y + 320)
    mel.connect_material_expressions(s1, "", s2, "")
    c1 = _expr(mat, unreal.MaterialExpressionMultiply, x + 300, y + 100, const_b=1.5)
    mel.connect_material_expressions(cover, "", c1, "A")
    c2 = _expr(mat, unreal.MaterialExpressionSaturate, x + 420, y + 100)
    mel.connect_material_expressions(c1, "", c2, "")
    snow0 = _expr(mat, unreal.MaterialExpressionMultiply, x + 660, y + 300)
    mel.connect_material_expressions(s2, "", snow0, "A")
    mel.connect_material_expressions(c2, "", snow0, "B")
    snow = snow0
    if normal:
        # the texture's own relief breaks it up: snow lies on the flat tops and in the joints, the
        # sloped edges of stones and boards show through (tangent-space normal z; floors only)
        tz = _expr(mat, unreal.MaterialExpressionComponentMask, x + 150, y + 420, r=False, g=False, b=True, a=False)
        link(normal, tz, "")
        t0 = _expr(mat, unreal.MaterialExpressionSubtract, x + 300, y + 420, const_b=0.9)
        mel.connect_material_expressions(tz, "", t0, "A")
        t1 = _expr(mat, unreal.MaterialExpressionMultiply, x + 420, y + 420, const_b=12.0)
        mel.connect_material_expressions(t0, "", t1, "A")
        t2 = _expr(mat, unreal.MaterialExpressionSaturate, x + 540, y + 420)
        mel.connect_material_expressions(t1, "", t2, "")
        # a full cover still buries everything
        t3 = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 660, y + 420, const_a=0.35, const_b=1.0)
        mel.connect_material_expressions(t2, "", t3, "Alpha")
        t4 = _expr(mat, unreal.MaterialExpressionMultiply, x + 780, y + 420)
        mel.connect_material_expressions(t3, "", t4, "A")
        mel.connect_material_expressions(snow0, "", t4, "B")
        snow = _expr(mat, unreal.MaterialExpressionMultiply, x + 900, y + 420, const_b=0.95)
        mel.connect_material_expressions(t4, "", snow, "A")
    out_base = out_normal = out_rough = None
    if base:
        dark = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 300, y - 150, const_a=1.0, const_b=0.6)
        mel.connect_material_expressions(wet, "", dark, "Alpha")
        bw = _expr(mat, unreal.MaterialExpressionMultiply, x + 450, y - 150)
        link(base, bw, "A")
        mel.connect_material_expressions(dark, "", bw, "B")
        white = _expr(mat, unreal.MaterialExpressionConstant3Vector, x + 450, y - 50, constant=unreal.LinearColor(0.8, 0.82, 0.86, 1))
        bs = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 800, y - 150)
        mel.connect_material_expressions(bw, "", bs, "A")
        mel.connect_material_expressions(white, "", bs, "B")
        mel.connect_material_expressions(snow, "", bs, "Alpha")
        out_base = (bs, "")
    if rough:
        gloss = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 660, y + 450, const_a=0.45, const_b=0.12)
        mel.connect_material_expressions(up, "", gloss, "Alpha")
        rw = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 800, y + 450)
        link(rough, rw, "A")
        mel.connect_material_expressions(gloss, "", rw, "B")
        mel.connect_material_expressions(wet, "", rw, "Alpha")
        rs = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 950, y + 450, const_b=0.7)
        mel.connect_material_expressions(rw, "", rs, "A")
        mel.connect_material_expressions(snow, "", rs, "Alpha")
        out_rough = (rs, "")
    if normal:
        flat = _expr(mat, unreal.MaterialExpressionConstant3Vector, x + 660, y + 600, constant=unreal.LinearColor(0, 0, 1, 0))
        sn = _expr(mat, unreal.MaterialExpressionMultiply, x + 800, y + 650, const_b=0.6)
        mel.connect_material_expressions(snow, "", sn, "A")
        ns = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 950, y + 600)
        link(normal, ns, "A")
        mel.connect_material_expressions(flat, "", ns, "B")
        mel.connect_material_expressions(sn, "", ns, "Alpha")
        out_normal = (ns, "")
    return out_base, out_normal, out_rough, (up, ""), (wet, "")


RIPPLE_HLSL = """
// rain ripples: in a grid of Cell-sized cells each throws rings from a random point at a random
// time (two offset layers), as drops hit standing water. -> a tangent-space normal offset (x, y).
float2 n = 0;
if (Amount > 0.001)
{
    for (int L = 0; L < 2; L++)
    {
        float2 p = WP / Cell + L * float2(0.37, 0.61);
        float2 c = floor(p);
        float2 f = frac(p);
        for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
        {
            float2 cc = c + float2(i, j) + L * 17.0;
            float3 h = frac(sin(float3(dot(cc, float2(127.1, 311.7)), dot(cc, float2(269.5, 183.3)), dot(cc, float2(419.2, 371.9)))) * 43758.5453);
            float t = frac(T * Rate * (0.7 + 0.6 * h.z) + h.z);
            float2 d = f - (float2(i, j) + 0.2 + 0.6 * h.xy);
            float r = length(d);
            float R = t * 0.8;
            float band = saturate(1.0 - abs(r - R) / 0.1);
            float wave = sin((r - R) * 50.0) * band * (1.0 - t) * (1.0 - t);
            n += d / max(r, 0.0001) * wave;
        }
    }
}
return float3(n * Strength * Amount, 0.0);
"""


def _ripples(mat, x, y, strength=0.6, cell_cm=45.0, rate=1.4):
    """Rain ripples (RIPPLE_HLSL) where rain falls: MPC_Environment.Precip, not in snow or sand.
    -> a float3 normal offset to add to a tangent-space normal."""
    wp = _expr(mat, unreal.MaterialExpressionWorldPosition, x, y)
    xy = _expr(mat, unreal.MaterialExpressionComponentMask, x + 150, y, r=True, g=True, b=False, a=False)
    mel.connect_material_expressions(wp, "", xy, "")
    time = _expr(mat, unreal.MaterialExpressionTime, x, y + 100)
    precip = _collection(mat, "Precip", x, y + 200)
    snow = _collection(mat, "Snow", x, y + 300)
    sand = _collection(mat, "Sand", x, y + 400)
    other = _expr(mat, unreal.MaterialExpressionAdd, x + 150, y + 350)
    mel.connect_material_expressions(snow, "", other, "A")
    mel.connect_material_expressions(sand, "", other, "B")
    rain = _expr(mat, unreal.MaterialExpressionOneMinus, x + 280, y + 350)
    mel.connect_material_expressions(other, "", rain, "")
    amount = _expr(mat, unreal.MaterialExpressionMultiply, x + 400, y + 250)
    mel.connect_material_expressions(precip, "", amount, "A")
    mel.connect_material_expressions(rain, "", amount, "B")
    params = [("Cell", cell_cm), ("Rate", rate), ("Strength", strength)]
    consts = [(n, _expr(mat, unreal.MaterialExpressionConstant, x + 150, y + 500 + i * 60, r=v), "") for i, (n, v) in enumerate(params)]
    return _custom(mat, x + 600, y, RIPPLE_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   [("WP", xy, ""), ("T", time, ""), ("Amount", amount, "")] + consts, "RainRipples")


SEASON_HLSL = """
// The season on foliage and grass (docs/adr/0005 phase 5; MPC_Environment Spring, Autumn, Winter, set
// from the game's season; summer is the original). Only plant colours change: greens and
// yellow-greens; stone, wood, earth and roof tiles keep theirs. Autumn turns them gold, orange and
// rust in patches (a value noise over the world), winter to a dormant straw, spring a fresher green.
// Grass (Grass 1) turns a muted gold-olive in autumn rather than the leaves' orange.
float3 c = Base;
float g = max(c.g, 0.0001);
float plant = saturate((c.g - c.b) / g * 2.0 - 0.3) * saturate(1.0 - (c.r - c.g) / g * 3.0);
float lum = dot(c, float3(0.3, 0.59, 0.11));
float2 q = WP.xy / 450.0;
float2 i0 = floor(q);
float2 fq = frac(q);
fq = fq * fq * (3.0 - 2.0 * fq);
float n00 = frac(sin(dot(i0, float2(127.1, 311.7))) * 43758.5453);
float n10 = frac(sin(dot(i0 + float2(1.0, 0.0), float2(127.1, 311.7))) * 43758.5453);
float n01 = frac(sin(dot(i0 + float2(0.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
float n11 = frac(sin(dot(i0 + float2(1.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
float n = lerp(lerp(n00, n10, fq.x), lerp(n01, n11, fq.x), fq.y);
float t = saturate(n * 1.3 - 0.15 + (lum - 0.1) * 2.0);
float3 fall = lerp(float3(0.62, 0.17, 0.04), lerp(float3(1.0, 0.42, 0.07), float3(1.0, 0.75, 0.16), saturate(t * 2.0 - 1.0)), saturate(t * 2.0));
float3 autumn = lerp(fall * lum * 2.3, lerp(c, float3(0.95, 0.78, 0.4) * lum * 1.9, 0.6), Grass);
float3 winter = lerp(lum.xxx, float3(1.12, 0.98, 0.74) * lum, 0.7) * 0.85;
float3 spring = c * float3(0.94, 1.1, 0.88);
float3 o = lerp(c, spring, plant * Spring);
o = lerp(o, autumn, plant * Autumn);
o = lerp(o, winter, plant * Winter);
return o;
"""


def _matte(mat, wet, x, y, default=0.2):
    """Specular for painted masonry, plaster and wood: matte (Specular 0.2, F0 ~0.016) rather than
    the engine's 0.5, which at grazing angles reflected enough sky and lamp light to wash the walls'
    colours out (2026-10-06). Wet surfaces (MPC Wetness) get their gloss back. Glass and panes set
    Specular 0.5 (materials.json variants). -> expression."""
    spec = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name="Specular", default_value=default)
    out = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x + 200, y, const_b=0.5)
    mel.connect_material_expressions(spec, "", out, "A")
    mel.connect_material_expressions(wet[0], wet[1], out, "Alpha")
    return out


UNDERGROWTH_HLSL = """
// Under the canopy (the painted tree walls, mapped once up the wall: v 0 at the top, 1 at the
// ground): the original paints its lowest band with saturated red undergrowth and blue sky gaps
// between the trunks, which in the lit scene read as light leaking out under the trees (2026-10-06).
// Darken it towards the ground, as a canopy shades it, and take the red and blue out there.
float under = smoothstep(Start, 1.0, UV.y) * Strength;
float3 c = Base;
float lum = dot(c, float3(0.3, 0.59, 0.11));
float g = max(c.g, 0.0001);
float off = saturate((max(c.r, c.b) - c.g) / g * 1.5);
c = lerp(c, lum * float3(0.9, 0.95, 0.8), under * off);
return c * lerp(1.0, Shade, under);
"""


def _undergrowth(mat, base, x, y):
    """UNDERGROWTH_HLSL on a foliage colour: Undergrowth (strength), from UndergrowthStart (texture v)
    down, to UndergrowthShade at the ground. base: (expression, output) -> expression."""
    uv = _expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y + 100)
    params = [(n, _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 200 + i * 80, parameter_name=p, default_value=v), "")
              for i, (n, p, v) in enumerate((("Strength", "Undergrowth", 1.0), ("Start", "UndergrowthStart", 0.55),
                                             ("Shade", "UndergrowthShade", 0.4)))]
    return _custom(mat, x + 300, y, UNDERGROWTH_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   [("Base", base[0], base[1]), ("UV", uv, "")] + params, "Undergrowth")


def _season(mat, base, x, y, grass=False):
    """The season's tint on a plant colour (SEASON_HLSL). base: (expression, output) -> expression."""
    wp = _expr(mat, unreal.MaterialExpressionWorldPosition, x, y + 100)
    kind = _expr(mat, unreal.MaterialExpressionConstant, x, y + 500, r=1.0 if grass else 0.0)
    inputs = [("Base", base[0], base[1]), ("WP", wp, ""), ("Grass", kind, "")]
    inputs += [(n, _collection(mat, n, x, y + 200 + i * 100), "") for i, n in enumerate(("Spring", "Autumn", "Winter"))]
    return _custom(mat, x + 300, y, SEASON_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, "Season")


def build_master(name, masked, glow=None, atlas=None, seasonal=False):
    """BaseColor (sRGB) * Tint -> Base Color; lerp(flat, Normal, NormalStrength) -> Normal;
    Roughness scalar; masked variant clips on BaseColor alpha. seasonal (foliage): the season's
    tint (_season)."""
    mat = _new_material(name)
    base = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -200,
                 parameter_name="BaseColor", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                 texture=eal.load_asset("/Engine/EngineResources/DefaultTexture"))
    tint = _expr(mat, unreal.MaterialExpressionVectorParameter, -700, 50,
                 parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    mul = _expr(mat, unreal.MaterialExpressionMultiply, -350, -150)
    mel.connect_material_expressions(base, "RGB", mul, "A")
    mel.connect_material_expressions(tint, "", mul, "B")

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

    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -350, 700,
                  parameter_name="Roughness", default_value=0.85)
    # foliage: the season's tint, and the canopy's shade over the painted undergrowth
    colour = (_undergrowth(mat, (_season(mat, (mul, ""), -1500, -700), ""), -1100, -1100), "") if seasonal else (mul, "")
    w_base, w_normal, w_rough, _, wet = _weather_surface(mat, colour, (lerp, ""), (rough, ""), -350, 1100)
    mel.connect_material_property(w_base[0], w_base[1], unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(_matte(mat, wet, -350, 900), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(w_normal[0], w_normal[1], unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(w_rough[0], w_rough[1], unreal.MaterialProperty.MP_ROUGHNESS)
    uv = _atlas_uv(mat, atlas, glow[0], -2400, 0) if atlas and glow else None
    if uv:
        for sample in (base, nrm):
            mel.connect_material_expressions(uv, "", sample, "UVs")
    if glow:
        mel.connect_material_property(_window_glow(mat, mul, glow, -1100, 800, uv), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

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


def _texture_settings(tex, srgb, normal, clamp_v=False):
    tex.set_editor_property("srgb", srgb)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", False)  # written DirectX-style already
    # cut-outs mapped once vertically: wrapping would blend the opaque bottom row into the clear top
    # row at the wall's top edge (a thin line along the top of tree lines, worst in distant mips)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP if clamp_v else unreal.TextureAddress.TA_WRAP)


def vertically_tiled():
    """Texture ids whose blockout UVs leave 0..1 vertically anywhere (build/zones/*.glb): those
    repeat up a wall and must keep wrapping (wall torches, for one)."""
    import glob
    import blockout
    tiled = set()
    for f in sorted(glob.glob(os.path.join(REPO, "build", "zones", "*.glb"))):
        for name, prim in blockout.read_glb(f).items():
            vs = [uv[1] for uv in prim.uvs or []]
            if vs and (min(vs) < -0.01 or max(vs) > 1.01):
                tiled.add(name.split("__")[0])
    return tiled


def ensure_textures(specs):
    """specs: [(file in build/textures_placeholder, srgb, normal map[, clamp V])] -> {file: texture
    asset path}. Imports, in one batch, only the files whose contents (or settings) changed since
    the last build."""
    cache = build_cache.CACHE
    out, stale = {}, []
    for f, srgb, normal, clamp_v in dict(((s[0], tuple(s) + (False,) * (4 - len(s))) for s in specs)).values():
        path = "%s/%s" % (TEX_DIR, os.path.splitext(f)[0])
        key = cache.key(build_cache.file_digest(os.path.join(PLACEHOLDERS, f)), srgb, normal, clamp_v,
                        build_cache.source(_texture_settings))
        out[f] = path
        if cache.fresh(path, key):
            cache.done(path, key, "textures", False)
        else:
            stale.append((f, srgb, normal, clamp_v, path, key))
    if stale:
        import_textures([s[0] for s in stale])
        for f, srgb, normal, clamp_v, path, key in stale:
            tex = eal.load_asset(path)
            if not tex:
                log("WARNING: %s did not import" % f)
                out.pop(f)
                continue
            _texture_settings(tex, srgb, normal, clamp_v)
            eal.save_loaded_asset(tex)
            cache.done(path, key, "textures", True)
    return out


def build_displaced_master(name, default_height, tessellation=True, range_cm=DISPLACEMENT_CM, glow=None, atlas=None):
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

    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -500, 450, parameter_name="Roughness", default_value=0.85)
    w_base, w_normal, w_rough, _, wet = _weather_surface(mat, (mul, ""), (lerp, ""), (rough, ""), -500, 1300)
    mel.connect_material_expressions(w_base[0], w_base[1], attrs, "BaseColor")
    mel.connect_material_expressions(_matte(mat, wet, -500, 1100), "", attrs, "Specular")
    mel.connect_material_expressions(w_normal[0], w_normal[1], attrs, "Normal")
    mel.connect_material_expressions(w_rough[0], w_rough[1], attrs, "Roughness")
    uv = _atlas_uv(mat, atlas, glow[0], -2800, 0) if atlas and glow else None
    if uv:
        for sample in (base, nrm):
            mel.connect_material_expressions(uv, "", sample, "UVs")
    if glow:
        mel.connect_material_expressions(_window_glow(mat, mul, glow, -1500, -700, uv), "", attrs, "EmissiveColor")

    height = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 650,
                   parameter_name="Height", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
                   texture=eal.load_asset(default_height))
    if uv:
        mel.connect_material_expressions(uv, "", height, "UVs")
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


def build_grime_master(name, macro):
    """Mesh-decal material for the grime strips (tools/blender/zone_grime.py): Color at the strip's
    contact edge (UV v = 0: the ground or the eave), fading to nothing at its far side as
    (1 - v)^Falloff, times Opacity, broken up by the macro noise along the wall (UV u = metres)."""
    mat = _new_material(name)
    # blend mode first: switching the domain to decal while opaque logs a (transient) compile error
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    uv = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    v = _expr(mat, unreal.MaterialExpressionComponentMask, -900, 0, r=False, g=True, b=False, a=False)
    mel.connect_material_expressions(uv, "", v, "")
    inv = _expr(mat, unreal.MaterialExpressionOneMinus, -750, 0)
    mel.connect_material_expressions(v, "", inv, "")
    clamp = _expr(mat, unreal.MaterialExpressionSaturate, -620, 0)
    mel.connect_material_expressions(inv, "", clamp, "")
    falloff = _expr(mat, unreal.MaterialExpressionScalarParameter, -620, 100, parameter_name="Falloff", default_value=1.6)
    fade = _expr(mat, unreal.MaterialExpressionPower, -450, 0)
    mel.connect_material_expressions(clamp, "", fade, "Base")
    mel.connect_material_expressions(falloff, "", fade, "Exp")
    # noise: u in metres along the wall, v across the strip
    scale = _expr(mat, unreal.MaterialExpressionConstant2Vector, -1100, 250, r=0.11, g=0.35)
    nuv = _expr(mat, unreal.MaterialExpressionMultiply, -900, 250)
    mel.connect_material_expressions(uv, "", nuv, "A")
    mel.connect_material_expressions(scale, "", nuv, "B")
    noise = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -750, 250, parameter_name="Macro",
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, texture=eal.load_asset(macro))
    mel.connect_material_expressions(nuv, "", noise, "UVs")
    vary = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -450, 250, const_a=0.45, const_b=1.25)
    mel.connect_material_expressions(noise, "G", vary, "Alpha")
    opacity = _expr(mat, unreal.MaterialExpressionScalarParameter, -450, 400, parameter_name="Opacity", default_value=0.5)
    m1 = _expr(mat, unreal.MaterialExpressionMultiply, -280, 100)
    mel.connect_material_expressions(fade, "", m1, "A")
    mel.connect_material_expressions(vary, "", m1, "B")
    m2 = _expr(mat, unreal.MaterialExpressionMultiply, -150, 200)
    mel.connect_material_expressions(m1, "", m2, "A")
    mel.connect_material_expressions(opacity, "", m2, "B")
    sat = _expr(mat, unreal.MaterialExpressionSaturate, -30, 200)
    mel.connect_material_expressions(m2, "", sat, "")
    mel.connect_material_property(sat, "", unreal.MaterialProperty.MP_OPACITY)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -300, -200, parameter_name="Color",
                  default_value=unreal.LinearColor(0.045, 0.035, 0.025, 1))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -300, -80, r=0.95)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


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

    # the normal map at both scales, blended like the colour, so the large-scale stones get their
    # own relief (sampled only at the small scale, they showed the small stones' bumps)
    default_normal = eal.load_asset("/Engine/EngineMaterials/DefaultNormal")
    nrm = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 650, parameter_name="Normal",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, texture=default_normal)
    mel.connect_material_expressions(uv1, "", nrm, "UVs")
    nrm2 = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 950, parameter_name="Normal",
                 sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, texture=default_normal)
    mel.connect_material_expressions(uv2, "", nrm2, "UVs")
    flat = _expr(mat, unreal.MaterialExpressionConstant3Vector, -650, 800, constant=unreal.LinearColor(0, 0, 1, 0))
    # the large layer stretches the same relief 2.73x wider at the same height, so its slopes come
    # out flatter; LargeNormalScale lifts them (1 = as stretched, 2.73 = as steep as the small stones)
    large_scale = _expr(mat, unreal.MaterialExpressionScalarParameter, -900, 1250, parameter_name="LargeNormalScale", default_value=1.5)
    nrm2s = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -650, 1000)
    mel.connect_material_expressions(flat, "", nrm2s, "A")
    mel.connect_material_expressions(nrm2, "RGB", nrm2s, "B")
    mel.connect_material_expressions(large_scale, "", nrm2s, "Alpha")
    nblend = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -650, 650)
    mel.connect_material_expressions(nrm, "RGB", nblend, "A")
    mel.connect_material_expressions(nrm2s, "", nblend, "B")
    mel.connect_material_expressions(wsat, "", nblend, "Alpha")
    strength = _expr(mat, unreal.MaterialExpressionScalarParameter, -650, 900, parameter_name="NormalStrength", default_value=0.8)
    lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -400, 700)
    mel.connect_material_expressions(flat, "", lerp, "A")
    mel.connect_material_expressions(nblend, "", lerp, "B")
    mel.connect_material_expressions(strength, "", lerp, "Alpha")
    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -400, 1000, parameter_name="Roughness", default_value=0.92)
    # the season on the grass (stone and earth keep their colour)
    w_base, w_normal, w_rough, up, wet = _weather_surface(mat, (_season(mat, (col, ""), -200, -900, grass=True), ""), (lerp, ""), (rough, ""), 0, 1400)
    # puddles where the ground is flat and the noise is high, growing with the wetness: dark, still
    # and mirror-glossy (the original's paths and squares hold water)
    uvp = _world_uv(mat, 0, 2200, "PuddleCm", 700.0, offset=0.13)
    pn = _expr(mat, unreal.MaterialExpressionTextureSample, 450, 2200, texture=macro,
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    mel.connect_material_expressions(uvp, "", pn, "UVs")
    level = _expr(mat, unreal.MaterialExpressionLinearInterpolate, 450, 2400, const_a=1.2, const_b=0.58)
    mel.connect_material_expressions(wet[0], wet[1], level, "Alpha")
    p0 = _expr(mat, unreal.MaterialExpressionSubtract, 650, 2250)
    mel.connect_material_expressions(pn, "B", p0, "A")
    mel.connect_material_expressions(level, "", p0, "B")
    p1 = _expr(mat, unreal.MaterialExpressionMultiply, 780, 2250, const_b=8.0)
    mel.connect_material_expressions(p0, "", p1, "A")
    p2 = _expr(mat, unreal.MaterialExpressionSaturate, 900, 2250)
    mel.connect_material_expressions(p1, "", p2, "")
    puddle = _expr(mat, unreal.MaterialExpressionMultiply, 1020, 2250)
    mel.connect_material_expressions(p2, "", puddle, "A")
    mel.connect_material_expressions(up[0], up[1], puddle, "B")
    pdark = _expr(mat, unreal.MaterialExpressionLinearInterpolate, 1150, 1400, const_a=1.0, const_b=0.55)
    mel.connect_material_expressions(puddle, "", pdark, "Alpha")
    pbase = _expr(mat, unreal.MaterialExpressionMultiply, 1300, 1400)
    mel.connect_material_expressions(w_base[0], w_base[1], pbase, "A")
    mel.connect_material_expressions(pdark, "", pbase, "B")
    prough = _expr(mat, unreal.MaterialExpressionLinearInterpolate, 1300, 1600, const_b=0.03)
    mel.connect_material_expressions(w_rough[0], w_rough[1], prough, "A")
    mel.connect_material_expressions(puddle, "", prough, "Alpha")
    pflat = _expr(mat, unreal.MaterialExpressionConstant3Vector, 1150, 1800, constant=unreal.LinearColor(0, 0, 1, 0))
    # standing water: flat, and rippled by the rain
    prip = _expr(mat, unreal.MaterialExpressionAdd, 1150, 1900)
    mel.connect_material_expressions(pflat, "", prip, "A")
    mel.connect_material_expressions(_ripples(mat, 0, 2600), "", prip, "B")
    pnormal = _expr(mat, unreal.MaterialExpressionLinearInterpolate, 1300, 1800)
    mel.connect_material_expressions(w_normal[0], w_normal[1], pnormal, "A")
    mel.connect_material_expressions(prip, "", pnormal, "B")
    mel.connect_material_expressions(puddle, "", pnormal, "Alpha")
    mel.connect_material_property(pbase, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(pnormal, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(prough, "", unreal.MaterialProperty.MP_ROUGHNESS)
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
    # materials.json grass "vertex_colors": false draws the look from before the export fix
    # (2026-10-06): every blade read colour 1, so all at the tip colour, varied x1.2, wind at the roots
    if grass.get("vertex_colors", True):
        vc = _expr(mat, unreal.MaterialExpressionVertexColor, -1000, 0)
    else:
        vc = _expr(mat, unreal.MaterialExpressionConstant4Vector, -1000, 0, constant=unreal.LinearColor(1, 1, 1, 1))
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
    # snow settles on the tips (vertex colour R: root 0, tip 1) as it covers the ground
    cover = _collection(mat, "SnowCover", -600, -500)
    tips = _expr(mat, unreal.MaterialExpressionMultiply, -450, -450)
    mel.connect_material_expressions(cover, "", tips, "A")
    mel.connect_material_expressions(vc, "R", tips, "B")
    tips2 = _expr(mat, unreal.MaterialExpressionMultiply, -330, -450, const_b=0.8)
    mel.connect_material_expressions(tips, "", tips2, "A")
    white = _expr(mat, unreal.MaterialExpressionConstant3Vector, -330, -350, constant=unreal.LinearColor(0.78, 0.8, 0.84, 1))
    snowy = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -150, -100)
    mel.connect_material_expressions(_season(mat, (c2, ""), -600, -1100, grass=True), "", snowy, "A")
    mel.connect_material_expressions(white, "", snowy, "B")
    mel.connect_material_expressions(tips2, "", snowy, "Alpha")
    mel.connect_material_property(snowy, "", unreal.MaterialProperty.MP_BASE_COLOR)
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
        storm = _collection(mat, "Wind", -800, 750)  # 1 calm, more in a storm (the director)
        windy = _expr(mat, unreal.MaterialExpressionMultiply, -650, 700)
        mel.connect_material_expressions(base_wind, "", windy, "A")
        mel.connect_material_expressions(storm, "", windy, "B")
        intensity = _expr(mat, unreal.MaterialExpressionMultiply, -500, 850)
        mel.connect_material_expressions(windy, "", intensity, "A")
        mel.connect_material_expressions(gust, "", intensity, "B")
        speed = _expr(mat, unreal.MaterialExpressionScalarParameter, -600, 950, parameter_name="WindSpeed",
                      default_value=float(grass.get("wind_speed", 0.3)))
        mel.connect_material_expressions(intensity, "", wind, "WindIntensity")
        mel.connect_material_expressions(vc, "R", wind, "WindWeight")
        mel.connect_material_expressions(speed, "", wind, "WindSpeed")
        # winter: the tufts die back to stubble (tips drop by WinterDropCm, vertex colour R = tip)
        down = _expr(mat, unreal.MaterialExpressionConstant3Vector, -900, 1300, constant=unreal.LinearColor(0, 0, -1, 0))
        drop = _expr(mat, unreal.MaterialExpressionScalarParameter, -900, 1400, parameter_name="WinterDropCm",
                     default_value=float(grass.get("winter_drop_cm", 14.0)))
        winter = _collection(mat, "Winter", -900, 1500)
        d0 = _expr(mat, unreal.MaterialExpressionMultiply, -750, 1400)
        mel.connect_material_expressions(drop, "", d0, "A")
        mel.connect_material_expressions(winter, "", d0, "B")
        d1 = _expr(mat, unreal.MaterialExpressionMultiply, -650, 1450)
        mel.connect_material_expressions(d0, "", d1, "A")
        mel.connect_material_expressions(vc, "R", d1, "B")
        stubble = _expr(mat, unreal.MaterialExpressionMultiply, -550, 1350)
        mel.connect_material_expressions(down, "", stubble, "A")
        mel.connect_material_expressions(d1, "", stubble, "B")
        mel.connect_material_expressions(stubble, "", wind, "AdditionalWPO")
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
    # rain rings on the surface (docs/adr/0005 phase 4)
    rippled = _expr(mat, unreal.MaterialExpressionAdd, -250, 150)
    mel.connect_material_expressions(lerp, "", rippled, "A")
    mel.connect_material_expressions(_ripples(mat, -1600, 600, strength=0.9), "", rippled, "B")
    norm = _expr(mat, unreal.MaterialExpressionNormalize, -200, 100)
    mel.connect_material_expressions(rippled, "", norm, "")
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


def build_prop_master(name, mpc):
    """M_PropSurface: Color, Emissive, Metallic, Roughness parameters. With LampSwitch 1 (props.json
    "night_only": lamp glass) the emissive follows MPC_Environment.LampsOn: lamps off by day."""
    mat = _new_material(name)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -400, 0, parameter_name="Color",
                  default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1))
    emissive = _expr(mat, unreal.MaterialExpressionVectorParameter, -700, 200, parameter_name="Emissive",
                     default_value=unreal.LinearColor(0, 0, 0, 1))
    lamps_on = _expr(mat, unreal.MaterialExpressionCollectionParameter, -1000, 320,
                     collection=eal.load_asset(mpc), parameter_name="LampsOn")
    switch = _expr(mat, unreal.MaterialExpressionScalarParameter, -1000, 420, parameter_name="LampSwitch", default_value=0.0)
    gate = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -700, 360, const_a=1.0)
    mel.connect_material_expressions(lamps_on, "", gate, "B")
    mel.connect_material_expressions(switch, "", gate, "Alpha")
    lit = _expr(mat, unreal.MaterialExpressionMultiply, -400, 200)
    mel.connect_material_expressions(emissive, "", lit, "A")
    mel.connect_material_expressions(gate, "", lit, "B")
    mel.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    metallic = _expr(mat, unreal.MaterialExpressionScalarParameter, -400, 400, parameter_name="Metallic", default_value=0.0)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -400, 520, parameter_name="Roughness", default_value=0.6)
    w_base, _, w_rough, _, _ = _weather_surface(mat, (color, ""), None, (rough, ""), -400, 800)
    mel.connect_material_property(w_base[0], w_base[1], unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(w_rough[0], w_rough[1], unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_prop_materials(cfg, mpc):
    """M_PropSurface + one MI per props.json "materials" slot (ironwork, lamp glass, embers)."""
    if not cfg:
        return {}
    master = _master("M_PropSurface", build_prop_master, mpc)
    return {slot: _instance("MI_Prop_" + slot, master,
                            vectors={"Color": v.get("color", [0.5, 0.5, 0.5]) + [1],
                                     "Emissive": v.get("emissive", [0, 0, 0]) + [1]},
                            scalars={"Metallic": v.get("metallic", 0.0), "Roughness": v.get("roughness", 0.6),
                                     "LampSwitch": 1.0 if v.get("night_only") else 0.0})
            for slot, v in cfg.items() if not slot.startswith("_")}


def default_orm():
    """-> path of T_DefaultORM: a linear 4x4 texture (G roughness 1, B metallic 0), the default of
    M_PropTextured's MetallicRoughness. An engine default won't do: WhiteSquareTexture is sRGB, which
    the Linear Color sampler rejects, so the master failed to compile and every AI prop drew the grey
    default material (2026-10-06, found after a full shader recompile)."""
    import struct
    import zlib
    name = "T_DefaultORM.png"
    path = os.path.join(PLACEHOLDERS, name)
    if not os.path.exists(path):
        os.makedirs(PLACEHOLDERS, exist_ok=True)
        raw = b"".join(bytes([0]) + bytes((255, 255, 0)) * 4 for _ in range(4))  # filter 0 + RGB rows

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        png = (bytes([137, 80, 78, 71, 13, 10, 26, 10]) + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        with open(path, "wb") as f:
            f.write(png)
    return ensure_textures([(name, False, False)])[name]


def build_textured_prop_master(name, mpc, orm_default):
    """M_PropTextured, for the AI-generated props (tools/aigen, docs/adr/0007): their glTF PBR set
    (BaseColor sRGB, Normal, MetallicRoughness: G roughness, B metallic) with what every other surface
    master has: matte specular, wet and snowy weather, and the ambient floor of interiors
    (base * SectorLight * MPC_Environment.SectorAmbient * AmbientTint), without which props went
    near-black inside while the walls around them were lit (2026-10-06). SectorLight: the original
    sector light level (0..1) where the prop stands, 1 by default. Two-sided, as Tripo exports."""
    mat = _new_material(name)
    base = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -300, parameter_name="BaseColor",
                 sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                 texture=eal.load_asset("/Engine/EngineResources/DefaultTexture"))
    nrm = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0, parameter_name="Normal",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                texture=eal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
    orm = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 300, parameter_name="MetallicRoughness",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
                texture=eal.load_asset(orm_default))
    w_base, w_normal, w_rough, _, wet = _weather_surface(mat, (base, "RGB"), (nrm, "RGB"), (orm, "G"), -400, 700)
    mel.connect_material_property(w_base[0], w_base[1], unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(w_normal[0], w_normal[1], unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(w_rough[0], w_rough[1], unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(orm, "B", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(_matte(mat, wet, -400, 500), "", unreal.MaterialProperty.MP_SPECULAR)
    collection = eal.load_asset(mpc)
    sector = _expr(mat, unreal.MaterialExpressionScalarParameter, -900, 600, parameter_name="SectorLight", default_value=1.0)
    amb = _expr(mat, unreal.MaterialExpressionCollectionParameter, -900, 700, collection=collection, parameter_name="SectorAmbient")
    tint = _expr(mat, unreal.MaterialExpressionCollectionParameter, -900, 800, collection=collection, parameter_name="AmbientTint")
    tint_rgb = _expr(mat, unreal.MaterialExpressionComponentMask, -700, 800, r=True, g=True, b=True)
    mel.connect_material_expressions(tint, "", tint_rgb, "")
    level = _expr(mat, unreal.MaterialExpressionMultiply, -700, 650)
    mel.connect_material_expressions(sector, "", level, "A")
    mel.connect_material_expressions(amb, "", level, "B")
    lit = _expr(mat, unreal.MaterialExpressionMultiply, -550, 700)
    mel.connect_material_expressions(level, "", lit, "A")
    mel.connect_material_expressions(tint_rgb, "", lit, "B")
    ambient = _expr(mat, unreal.MaterialExpressionMultiply, -400, 650)
    mel.connect_material_expressions(base, "RGB", ambient, "A")
    mel.connect_material_expressions(lit, "", ambient, "B")
    mel.connect_material_property(ambient, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mat.set_editor_property("two_sided", True)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


# texture parameters of a material on an imported AI prop: ours (M_PropTextured) or Interchange's glTF ones
_AI_PROP_TEXTURES = {"BaseColor": ("BaseColor", "BaseColorTexture"), "Normal": ("Normal", "NormalTexture"),
                     "MetallicRoughness": ("MetallicRoughness", "MetallicRoughnessTexture")}


def ai_prop_material(mesh_path, slot_material):
    """-> path of MI_AIProp_<mesh> on M_PropTextured with the textures of the material the mesh came
    with (Interchange's glTF instance, or ours on a later run), or None when it has none."""
    mi = eal.load_asset(slot_material) if isinstance(slot_material, str) else slot_material
    if not isinstance(mi, unreal.MaterialInstance):
        return None
    found = {}
    for p in mi.get_editor_property("texture_parameter_values"):
        pname = str(p.get_editor_property("parameter_info").get_editor_property("name"))
        tex = p.get_editor_property("parameter_value")
        for ours, names in _AI_PROP_TEXTURES.items():
            if pname in names and tex:
                found[ours] = tex.get_path_name().split(".")[0]
    if "BaseColor" not in found:
        return None
    master = _master("M_PropTextured", build_textured_prop_master, ensure_mpc(), default_orm())
    return _instance("MI_AIProp_" + mesh_path.rsplit("/", 1)[-1], master, textures=found)


TREE_WIND_HLSL = """
// Wind on the procedural trees (tools/blender/build_tree_kit.py, docs/adr/0007 "Trees"). Vertex
// colour R is the wind weight (0 at the trunk's foot, 1 at the crown's rim), G a random value per
// card, Leaf its A (1 on leaves). The whole crown sways downwind (the rain's direction) in slow gusts phased
// by the tree's position, and the leaf cards flutter on top. MPC Wind: 1 calm, more in a storm.
// The sway bends with the square of the height in the mesh (LP.z / HeightCm), so the trunk's foot
// stays planted whatever the vertex colours say.
float2 dir = normalize(float2(1.0, 0.35));
float phase = dot(Obj.xy, float2(0.0013, 0.0021));
float gust = 0.55 + 0.45 * sin(T * 0.23 + phase * 0.7);
float sway = (0.35 + 0.6 * sin(T * 0.9 + phase) + 0.25 * sin(T * 2.1 + phase * 1.7)) * gust;
float w = Wind * Strength;
float h = saturate(LP.z / HeightCm);
float bend = h * h;
float3 o = float3(dir * sway * SwayCm * w * bend, -abs(sway) * SwayCm * 0.15 * w * bend);
float fl = sin(T * 5.3 + VC.g * 40.0 + dot(WP, float3(0.031, 0.027, 0.019)));
float fl2 = sin(T * 3.7 + VC.g * 23.0);
o += float3(fl, fl2, fl * fl2) * FlutterCm * w * VC.r * Leaf * h;
return o;
"""


def _tree_wind(mat, x, y):
    """TREE_WIND_HLSL -> expression (World Position Offset). Bark and leaves share it exactly, so
    the cards stay on their branches."""
    vc = _expr(mat, unreal.MaterialExpressionVertexColor, x, y)
    vc_a = _expr(mat, unreal.MaterialExpressionVertexColor, x - 200, y)
    inputs = [("VC", vc, ""), ("Leaf", vc_a, "A"), ("T", _expr(mat, unreal.MaterialExpressionTime, x, y + 100), ""),
              ("Obj", _expr(mat, unreal.MaterialExpressionObjectPositionWS, x, y + 200), ""),
              ("WP", _expr(mat, unreal.MaterialExpressionWorldPosition, x, y + 300), ""),
              ("LP", _expr(mat, unreal.MaterialExpressionLocalPosition, x - 200, y + 300), ""),
              ("Wind", _collection(mat, "Wind", x, y + 400), "")]
    inputs += [(n, _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 500 + i * 80, parameter_name=n, default_value=v), "")
               for i, (n, v) in enumerate((("Strength", 1.0), ("SwayCm", 9.0), ("FlutterCm", 2.5), ("HeightCm", 490.0)))]
    return _custom(mat, x + 400, y, TREE_WIND_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, "TreeWind")


def build_tree_master(name, leaves):
    """M_TreeLeaves / M_TreeBark for the procedural trees (tools/blender/build_tree_kit.py,
    docs/adr/0007 "Trees"). BaseColor * per-card variation (vertex colour G, leaves only) * the
    crown's ambient occlusion (vertex colour B: lerp(AoMin, 1)), the season's tint on leaves, wet and
    snowy weather, matte specular (leaves Specular 0.05, Roughness 0.95: no sheen in direct sun), and
    the shared wind (_tree_wind). Leaves: masked on the atlas's
    alpha, Two Sided Foliage shading (light comes through the leaves, tinted by Transmission), and
    not two-sided: the cards are doubled in geometry, so both sides keep the crown's normals."""
    mat = _new_material(name)
    tex = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1500, -300, parameter_name="BaseColor",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                texture=eal.load_asset("/Engine/EngineResources/DefaultTexture"))
    vc = _expr(mat, unreal.MaterialExpressionVertexColor, -1500, 0)
    ao_min = _expr(mat, unreal.MaterialExpressionScalarParameter, -1500, 150, parameter_name="AoMin",
                   default_value=0.5 if leaves else 0.55)
    ao = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -1250, 100, const_b=1.0)
    mel.connect_material_expressions(ao_min, "", ao, "A")
    mel.connect_material_expressions(vc, "B", ao, "Alpha")
    col = _expr(mat, unreal.MaterialExpressionMultiply, -1050, -200)
    mel.connect_material_expressions(tex, "RGB", col, "A")
    mel.connect_material_expressions(ao, "", col, "B")
    if leaves:
        var = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -1250, 250, const_a=0.82, const_b=1.18)
        mel.connect_material_expressions(vc, "G", var, "Alpha")
        brightness = _expr(mat, unreal.MaterialExpressionScalarParameter, -1250, 350, parameter_name="Brightness", default_value=1.3)
        v2 = _expr(mat, unreal.MaterialExpressionMultiply, -1100, 300)
        mel.connect_material_expressions(var, "", v2, "A")
        mel.connect_material_expressions(brightness, "", v2, "B")
        col2 = _expr(mat, unreal.MaterialExpressionMultiply, -900, -150)
        mel.connect_material_expressions(col, "", col2, "A")
        mel.connect_material_expressions(v2, "", col2, "B")
        col = _season(mat, (col2, ""), -900, -900)
    rough = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 500, parameter_name="Roughness",
                  default_value=0.95 if leaves else 0.9)
    w_base, _, w_rough, _, wet = _weather_surface(mat, (col, ""), None, (rough, ""), -500, 800)
    mel.connect_material_property(w_base[0], w_base[1], unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(w_rough[0], w_rough[1], unreal.MaterialProperty.MP_ROUGHNESS)
    # leaves nearly without a sheen in direct sun (the user, 2026-10-06): lit, but diffuse
    mel.connect_material_property(_matte(mat, wet, -500, 650, default=0.05 if leaves else 0.2), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(_tree_wind(mat, -1100, 1300), "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    if leaves:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mel.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        transmission = _expr(mat, unreal.MaterialExpressionVectorParameter, -700, 300, parameter_name="Transmission",
                             default_value=unreal.LinearColor(0.22, 0.32, 0.12, 1))
        sss = _expr(mat, unreal.MaterialExpressionMultiply, -450, 300)
        mel.connect_material_expressions(col, "", sss, "A")
        mel.connect_material_expressions(transmission, "", sss, "B")
        mel.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def tree_materials(kind):
    """{"tree_bark": MI, "tree_leaves": MI} for SM_Tree_<kind>_* (textures T_TreeBark_<kind> and
    T_TreeLeaves_<kind> from tools/textures/make_tree_textures.py), or {} when they're missing."""
    files = ["T_TreeBark_%s.png" % kind, "T_TreeLeaves_%s.png" % kind]
    if not all(os.path.exists(os.path.join(PLACEHOLDERS, f)) for f in files):
        log("WARNING: no tree textures for %s (run tools/textures/make_tree_textures.py)" % kind)
        return {}
    tex = ensure_textures([(f, True, False) for f in files])
    # The leaf atlas is small (1024 px) and streaming judged the cards' texel density badly: the
    # leaves were drawn from a low mip, and the alpha cut turned them into blobs (2026-10-06,
    # build/lookdev/trees_v8). Keep every mip resident.
    leaves = eal.load_asset(tex[files[1]])
    if leaves and not leaves.get_editor_property("never_stream"):
        leaves.set_editor_property("never_stream", True)
        eal.save_loaded_asset(leaves)
    out = {}
    for slot, f, leaves in (("tree_bark", files[0], False), ("tree_leaves", files[1], True)):
        master = _master("M_TreeLeaves" if leaves else "M_TreeBark", build_tree_master, leaves)
        out[slot] = _instance("MI_Tree_%s_%s" % (kind, "Leaves" if leaves else "Bark"), master, textures={"BaseColor": tex[f]})
    return out


def build_night_sky_master(name, stars, mpc):
    """M_NightSky (docs/adr/0005): the sky dome's material. It is a sky material (is_sky), so the sky
    atmosphere is drawn through it (view luminance + the sun / moon disc) exactly as without a dome,
    and the star map (T_Stars, equirectangular around the zenith) is added on top, scaled by
    MPC_Environment.Stars (night 1, day 0) and StarBrightness, faded out at the horizon and turned a
    full circle per game day with GameHour."""
    mat = _new_material(name)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("is_sky", True)
    mat.set_editor_property("two_sided", True)
    collection = eal.load_asset(mpc)
    # direction towards the sky from the camera: -CameraVector
    cam = _expr(mat, unreal.MaterialExpressionCameraVectorWS, -2200, 300)
    d = _expr(mat, unreal.MaterialExpressionMultiply, -2050, 300, const_b=-1.0)
    mel.connect_material_expressions(cam, "", d, "A")
    masks = {}
    for i, ch in enumerate("xyz"):
        m = _expr(mat, unreal.MaterialExpressionComponentMask, -1900, 150 + i * 120, r=ch == "x", g=ch == "y", b=ch == "z")
        mel.connect_material_expressions(d, "", m, "")
        masks[ch] = m
    lon = _expr(mat, unreal.MaterialExpressionArctangent2, -1700, 150)
    mel.connect_material_expressions(masks["y"], "", lon, "Y")
    mel.connect_material_expressions(masks["x"], "", lon, "X")
    u0 = _expr(mat, unreal.MaterialExpressionMultiply, -1550, 150, const_b=1.0 / (2.0 * 3.14159265))
    mel.connect_material_expressions(lon, "", u0, "A")
    hour = _expr(mat, unreal.MaterialExpressionCollectionParameter, -1700, 0, collection=collection, parameter_name="GameHour")
    turn = _expr(mat, unreal.MaterialExpressionMultiply, -1550, 0, const_b=1.0 / 24.0)
    mel.connect_material_expressions(hour, "", turn, "A")
    u = _expr(mat, unreal.MaterialExpressionAdd, -1400, 100)
    mel.connect_material_expressions(u0, "", u, "A")
    mel.connect_material_expressions(turn, "", u, "B")
    acos = _expr(mat, unreal.MaterialExpressionArccosine, -1700, 400)
    mel.connect_material_expressions(masks["z"], "", acos, "")
    v = _expr(mat, unreal.MaterialExpressionMultiply, -1550, 400, const_b=1.0 / 3.14159265)
    mel.connect_material_expressions(acos, "", v, "A")
    uv = _expr(mat, unreal.MaterialExpressionAppendVector, -1250, 250)
    mel.connect_material_expressions(u, "", uv, "A")
    mel.connect_material_expressions(v, "", uv, "B")
    # mip 0 always: the longitude wraps at the seam, where screen-space derivatives would jump
    tex = _expr(mat, unreal.MaterialExpressionTextureSample, -1050, 250, texture=eal.load_asset(stars),
                mip_value_mode=unreal.TextureMipValueMode.TMVM_MIP_LEVEL, const_mip_value=0)
    mel.connect_material_expressions(uv, "", tex, "UVs")
    horizon = _expr(mat, unreal.MaterialExpressionMultiply, -1550, 550, const_b=8.0)
    mel.connect_material_expressions(masks["z"], "", horizon, "A")
    horizon_sat = _expr(mat, unreal.MaterialExpressionSaturate, -1400, 550)
    mel.connect_material_expressions(horizon, "", horizon_sat, "")
    night = _expr(mat, unreal.MaterialExpressionCollectionParameter, -1250, 650, collection=collection, parameter_name="Stars")
    bright = _expr(mat, unreal.MaterialExpressionScalarParameter, -1250, 750, parameter_name="StarBrightness", default_value=0.035)
    k1 = _expr(mat, unreal.MaterialExpressionMultiply, -1100, 600)
    mel.connect_material_expressions(horizon_sat, "", k1, "A")
    mel.connect_material_expressions(night, "", k1, "B")
    k2 = _expr(mat, unreal.MaterialExpressionMultiply, -950, 650)
    mel.connect_material_expressions(k1, "", k2, "A")
    mel.connect_material_expressions(bright, "", k2, "B")
    star_rgb = _expr(mat, unreal.MaterialExpressionMultiply, -800, 300)
    mel.connect_material_expressions(tex, "RGB", star_rgb, "A")
    mel.connect_material_expressions(k2, "", star_rgb, "B")
    # the sky atmosphere as it would be drawn without a dome
    sky = _expr(mat, unreal.MaterialExpressionSkyAtmosphereViewLuminance, -800, -100)
    disc = _expr(mat, unreal.MaterialExpressionSkyAtmosphereLightDiskLuminance, -800, 50)
    atmos = _expr(mat, unreal.MaterialExpressionAdd, -600, 0)
    mel.connect_material_expressions(sky, "", atmos, "A")
    mel.connect_material_expressions(disc, "", atmos, "B")
    out = _expr(mat, unreal.MaterialExpressionAdd, -400, 150)
    mel.connect_material_expressions(atmos, "", out, "A")
    mel.connect_material_expressions(star_rgb, "", out, "B")
    mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


FIRE_HLSL = """
// the flipbook cell for this fire's time: each fire starts from its own phase (a hash of where it
// stands), and each frame fades into the next over its last part, so 3 frames read as a moving flame
float n = max(Frames, 1.0);
float3 p = frac(Pos * 0.0123);
float ph = frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453);
float t = T * Fps + ph * n;
float f0 = fmod(floor(t), n);
float f1 = fmod(f0 + 1.0, n);
float a = smoothstep(1.0 - Blend, 1.0, frac(t));
float2 cell = float2(1.0 / Cols, 1.0 / Rows);
float2 uv0 = (float2(fmod(f0, Cols), floor(f0 / Cols)) + UV) * cell;
float2 uv1 = (float2(fmod(f1, Cols), floor(f1 / Cols)) + UV) * cell;
return lerp(Texture2DSample(Tex, TexSampler, uv0), Texture2DSample(Tex, TexSampler, uv1), a);
"""


def build_fire_master(name, default_atlas, hlsl):
    """M_Fire (docs/adr/0005 phase 3): the flame sprite of AMRFireActor. Unlit, translucent: emissive
    = the flipbook cell (Atlas, Cols x Rows cells, Frames, Fps) * Brightness, opacity = its alpha.
    (Component masks are given all four channels: a new mask has R on, and an "A only" mask that
    kept it read the opacity from the red channel.)"""
    mat = _new_material(name)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    tex = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, -1100, 0, parameter_name="Atlas",
                texture=eal.load_asset(default_atlas))
    uv = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 150)
    time = _expr(mat, unreal.MaterialExpressionTime, -1100, 250)
    pos = _expr(mat, unreal.MaterialExpressionObjectPositionWS, -1100, 350)
    params = {}
    for i, (pname, default) in enumerate((("Cols", 4.0), ("Rows", 1.0), ("Frames", 3.0), ("Fps", 8.0), ("Blend", 0.6))):
        params[pname] = _expr(mat, unreal.MaterialExpressionScalarParameter, -1100, 450 + i * 100,
                              parameter_name=pname, default_value=default)
    custom = _expr(mat, unreal.MaterialExpressionCustom, -700, 200, code=hlsl.strip(),
                   output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4, description="FireFlipbook")
    names = ["Tex", "UV", "T", "Pos"] + list(params)
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    for n, src in zip(names, [tex, uv, time, pos] + list(params.values())):
        mel.connect_material_expressions(src, "", custom, n)
    rgb = _expr(mat, unreal.MaterialExpressionComponentMask, -450, 100, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(custom, "", rgb, "")
    alpha = _expr(mat, unreal.MaterialExpressionComponentMask, -450, 300, r=False, g=False, b=False, a=True)
    mel.connect_material_expressions(custom, "", alpha, "")
    bright = _expr(mat, unreal.MaterialExpressionScalarParameter, -450, 0, parameter_name="Brightness", default_value=4.0)
    em = _expr(mat, unreal.MaterialExpressionMultiply, -250, 50)
    mel.connect_material_expressions(rgb, "", em, "A")
    mel.connect_material_expressions(bright, "", em, "B")
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_fires():
    """-> {preset: (MI path, placeholders.json "fires" entry)}: MI_Fire_<preset> on M_Fire with its
    flipbook (make_placeholders.py, props.json "fires"). Empty without them."""
    manifest_path = os.path.join(PLACEHOLDERS, "placeholders.json")
    if not os.path.exists(manifest_path):
        return {}
    fires = json.load(open(manifest_path, encoding="utf-8")).get("fires", {})
    if not fires:
        return {}
    atlases = ensure_textures([(e["file"], True, False) for e in fires.values()])
    if not all(e["file"] in atlases for e in fires.values()):
        return {}
    for path in atlases.values():
        # small, and only ever drawn by sprites (material billboards): kept resident rather than
        # left to the texture streamer's view of a primitive it doesn't size like a mesh
        tex = eal.load_asset(path)
        if not tex.get_editor_property("never_stream"):
            tex.set_editor_property("never_stream", True)
            eal.save_loaded_asset(tex)
    master = _master("M_Fire", build_fire_master, atlases[sorted(fires.values(), key=lambda e: e["file"])[0]["file"]], FIRE_HLSL)
    return {name: (_instance("MI_Fire_" + name, master, textures={"Atlas": atlases[e["file"]]},
                             scalars={"Cols": e["cols"], "Rows": e["rows"], "Frames": e["frames"], "Fps": e["fps"],
                                      "Brightness": e["brightness"]}), e)
            for name, e in sorted(fires.items())}


PRECIP_WPO = """
// One tiny quad of SM_Precip -> a falling streak (rain) or flake (snow) near the camera.
// LocalPos: the quad's seed in the actor's box (cm); D: actor - camera; CS: camera - shelter origin
// (x, y) and floor (z). Positions wrap in a Box x Box x BoxH box around the camera, in world space
// (the actor sits on a grid of whole boxes), so streaks stay put as the camera moves.
float snow = Snow;
float sand = Sand;
float speed = lerp(lerp(Fall, SnowFall, snow), SandFall, sand);
float2 windv = float2(1.0, 0.35) * Wind * lerp(lerp(WindDrift, SnowDrift, snow), SandDrift, sand);
float3 drift = float3(windv * T, -speed * T);
float3 box = float3(Box, Box, BoxH);
// the quad's place in the box comes from its seed, the same on all four corners (wrapping each
// corner's own position split quads lying on the wrap into slivers across the whole box)
float3 home = frac(sin(float3(dot(Seed, float2(12.9898, 78.233)), dot(Seed, float2(39.346, 11.135)), dot(Seed, float2(73.156, 52.235)))) * 43758.5453);
float3 rel = frac(home + (D + drift) / box) * box - 0.5 * box;
rel.xy += snow * Sway * float2(sin(T * 0.9 + Seed.y * 40.0), cos(T * 0.7 + Seed.x * 40.0));
// how much falls: a share of the quads
float vis = step(Seed.x, Precip);
// nothing under a roof, nothing below the ground (the shelter map: highest surface per 25 cm)
float3 sp = CS + rel;
float2 suv = sp.xy / float2(SizeX, SizeY);
float inside = step(0.0, suv.x) * step(suv.x, 1.0) * step(0.0, suv.y) * step(suv.y, 1.0);
float top = Texture2DSampleLevel(Shelter, ShelterSampler, saturate(suv), 0).r * ZScale;
vis *= lerp(1.0, step(top, sp.z), inside);
// sand blows low, within SandHeight of the ground
vis *= lerp(1.0, step(sp.z - top, SandHeight), sand * inside);
// not right in the camera's face (a streak frozen there in a still smears across the view)
vis *= step(150.0, length(rel));
// face the camera around the fall direction
float2 hz = normalize(-rel.xy + 0.001);
float3 right = float3(-hz.y, hz.x, 0.0);
float3 fallDir = normalize(float3(windv, -speed));
float3 axis = lerp(-fallDir, float3(0.0, 0.0, 1.0), snow * (1.0 - sand));
float size = lerp(0.7, 1.3, Seed.y);
float len = lerp(lerp(Length * size, Flake * size, snow), SandLength * size, sand);
float wid = lerp(lerp(Width, Flake * size, snow), SandWidth, sand);
float2 c = Corner - 0.5;
float3 target = rel + (right * c.x * wid - axis * c.y * len) * vis;
return target - (LocalPos + D);
"""

PRECIP_SHAPE = """
// rain: a thin streak, brightest in the middle; snow: a soft round flake. Fades out near the box's
// edge (where it wraps) and within a few metres of the camera.
float2 c = Corner - 0.5;
float streak = saturate(1.0 - abs(c.x) * 2.0) * sin(3.14159 * saturate(Corner.y));
float flake = pow(saturate(1.0 - length(c) * 2.0), 1.5);
float a = lerp(lerp(streak * RainOpacity, flake * SnowOpacity, Snow), streak * SandOpacity, Sand);
float fade = saturate(1.0 - Depth / (Box * 0.48)) * saturate((Depth - 150.0) / 300.0);
return a * fade;
"""


def _custom(mat, x, y, code, out_type, inputs, description):
    """A Custom HLSL expression with named inputs [(name, expression, output)] wired up."""
    custom = _expr(mat, unreal.MaterialExpressionCustom, x, y, code=code.strip(), output_type=out_type, description=description)
    pins = []
    for name, _, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        pins.append(ci)
    custom.set_editor_property("inputs", pins)
    for name, src, out in inputs:
        mel.connect_material_expressions(src, out, custom, name)
    return custom


def build_precip_master(name, shelter_default, wpo, shape):
    """M_Precip (docs/adr/0005 phase 4): SM_Precip's quads as rain streaks or snowflakes. Translucent,
    lit by the translucency volume (so it follows sun, moon and lamps), moved entirely by world
    position offset (PRECIP_WPO), shaped and faded in the pixel shader (PRECIP_SHAPE). Reads
    MPC_Environment.Precip, Snow and Wind; a zone's instance carries its shelter map."""
    mat = _new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    mat.set_editor_property("two_sided", True)
    params = {}
    for i, (pname, default) in enumerate((
            ("Fall", 1100.0), ("SnowFall", 110.0), ("WindDrift", 120.0), ("SnowDrift", 60.0), ("Sway", 35.0),
            ("Length", 70.0), ("Width", 1.2), ("Flake", 2.6), ("Box", 3200.0), ("BoxH", 1600.0),
            ("SizeX", 1.0), ("SizeY", 1.0), ("ZScale", 1.0), ("RainOpacity", 0.3), ("SnowOpacity", 0.9),
            ("SandFall", 40.0), ("SandDrift", 900.0), ("SandLength", 45.0), ("SandWidth", 2.0), ("SandHeight", 600.0),
            ("SandOpacity", 0.45))):
        params[pname] = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, -600 + i * 80,
                              parameter_name=pname, default_value=default)
    local = _expr(mat, unreal.MaterialExpressionPreSkinnedPosition, -1800, 700)
    corner = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1800, 800)
    seed = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1800, 900, coordinate_index=1)
    time = _expr(mat, unreal.MaterialExpressionTime, -1800, 1000)
    actor = _expr(mat, unreal.MaterialExpressionActorPositionWS, -1800, 1100)
    cam = _expr(mat, unreal.MaterialExpressionCameraPositionWS, -1800, 1200)
    d = _expr(mat, unreal.MaterialExpressionSubtract, -1600, 1150)
    mel.connect_material_expressions(actor, "", d, "A")
    mel.connect_material_expressions(cam, "", d, "B")
    ox = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, 1300, parameter_name="OriginX", default_value=0.0)
    oy = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, 1380, parameter_name="OriginY", default_value=0.0)
    oz = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, 1460, parameter_name="FloorZ", default_value=-1.0e7)
    oxy = _expr(mat, unreal.MaterialExpressionAppendVector, -1650, 1340)
    mel.connect_material_expressions(ox, "", oxy, "A")
    mel.connect_material_expressions(oy, "", oxy, "B")
    oxyz = _expr(mat, unreal.MaterialExpressionAppendVector, -1500, 1380)
    mel.connect_material_expressions(oxy, "", oxyz, "A")
    mel.connect_material_expressions(oz, "", oxyz, "B")
    cs = _expr(mat, unreal.MaterialExpressionSubtract, -1350, 1250)
    mel.connect_material_expressions(cam, "", cs, "A")
    mel.connect_material_expressions(oxyz, "", cs, "B")
    shelter = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, -1800, 1600, parameter_name="Shelter",
                    texture=eal.load_asset(shelter_default), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    precip = _collection(mat, "Precip", -1800, 1750)
    snow = _collection(mat, "Snow", -1800, 1850)
    wind = _collection(mat, "Wind", -1800, 1950)
    sand = _collection(mat, "Sand", -1800, 2050)
    inputs = [("LocalPos", local, ""), ("Corner", corner, ""), ("Seed", seed, ""), ("T", time, ""), ("D", d, ""),
              ("CS", cs, ""), ("Shelter", shelter, ""), ("Precip", precip, ""), ("Snow", snow, ""), ("Wind", wind, ""),
              ("Sand", sand, "")]
    inputs += [(k, params[k], "") for k in ("Fall", "SnowFall", "WindDrift", "SnowDrift", "Sway", "Length", "Width",
                                            "Flake", "Box", "BoxH", "SizeX", "SizeY", "ZScale", "SandFall", "SandDrift",
                                            "SandLength", "SandWidth", "SandHeight")]
    move = _custom(mat, -900, 600, wpo, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, "PrecipMotion")
    mel.connect_material_property(move, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    depth = _expr(mat, unreal.MaterialExpressionPixelDepth, -1100, -200)
    alpha = _custom(mat, -700, -200, shape, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                    [("Corner", corner, ""), ("Snow", snow, ""), ("Sand", sand, ""), ("Depth", depth, ""), ("Box", params["Box"], ""),
                     ("RainOpacity", params["RainOpacity"], ""), ("SnowOpacity", params["SnowOpacity"], ""),
                     ("SandOpacity", params["SandOpacity"], "")], "PrecipShape")
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -500, -400, parameter_name="Color",
                  default_value=unreal.LinearColor(0.75, 0.78, 0.82, 1))
    sand_color = _expr(mat, unreal.MaterialExpressionVectorParameter, -500, -500, parameter_name="SandColor",
                       default_value=unreal.LinearColor(0.55, 0.42, 0.26, 1))
    tinted = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -300, -450)
    mel.connect_material_expressions(color, "", tinted, "A")
    mel.connect_material_expressions(sand_color, "", tinted, "B")
    mel.connect_material_expressions(sand, "", tinted, "Alpha")
    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -500, -300, r=0.35)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


SPLASH_WPO = """
// One tiny quad of SM_Precip -> a rain splash: every Period seconds it springs up at a new random
// spot within Area of the camera, on the highest surface there (the shelter map: ground, roofs,
// the pond), grows and fades. Not world-stable: each lasts a fraction of a second.
float t = T / Period + Seed.y * 13.0;
float cyc = floor(t);
float f = frac(t);
float2 h = frac(sin(float2(cyc * 12.9898 + Seed.x * 78.233, cyc * 39.346 + Seed.y * 11.135)) * 43758.5453);
float2 xy = (h - 0.5) * Area;
float2 suv = (CS.xy + xy) / float2(SizeX, SizeY);
float inside = step(0.0, suv.x) * step(suv.x, 1.0) * step(0.0, suv.y) * step(suv.y, 1.0);
float top = Texture2DSampleLevel(Shelter, ShelterSampler, saturate(suv), 0).r * ZScale;
float3 rel = float3(xy, top - CS.z + 1.0);
float vis = step(Seed.x, Precip * Density) * inside * step(1.0, top);
vis *= step(150.0, length(rel));
float2 hz = normalize(-rel.xy + 0.001);
float3 right = float3(-hz.y, hz.x, 0.0);
float s = Size * (0.35 + 0.65 * f) * lerp(0.7, 1.3, Seed.y);
float2 c = float2(Corner.x - 0.5, 1.0 - Corner.y);
float3 target = rel + (right * c.x * s + float3(0.0, 0.0, c.y * s)) * vis;
return target - (LocalPos + D);
"""

SPLASH_SHAPE = """
// a little crown of spray, fading as it grows
float t = T / Period + Seed.y * 13.0;
float f = frac(t);
float2 c = float2(Corner.x - 0.5, 1.0 - Corner.y);
float r = length(float2(c.x * 2.0, c.y * 1.6));
float crown = saturate(1.0 - r * 1.1) * (0.45 + 0.55 * smoothstep(0.0, 0.5, r));
float fade = saturate(1.0 - Depth / 2500.0);
return crown * (1.0 - f) * Opacity * fade;
"""


def build_splash_master(name, shelter_default, wpo, shape):
    """M_Splash (docs/adr/0005 phase 4): SM_Precip's quads as rain splashes (SPLASH_WPO): each springs
    up for Period seconds at a random spot near the camera on the shelter map's top surface. Lit
    translucent, like M_Precip; MPC_Environment.Precip x Density of the quads show."""
    mat = _new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    mat.set_editor_property("two_sided", True)
    params = {}
    for i, (pname, default) in enumerate((("Period", 0.45), ("Area", 2600.0), ("Density", 0.5), ("Size", 9.0),
                                          ("SizeX", 1.0), ("SizeY", 1.0), ("ZScale", 1.0), ("Opacity", 0.55))):
        params[pname] = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, -400 + i * 80,
                              parameter_name=pname, default_value=default)
    local = _expr(mat, unreal.MaterialExpressionPreSkinnedPosition, -1800, 400)
    corner = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1800, 500)
    seed = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1800, 600, coordinate_index=1)
    time = _expr(mat, unreal.MaterialExpressionTime, -1800, 700)
    actor = _expr(mat, unreal.MaterialExpressionActorPositionWS, -1800, 800)
    cam = _expr(mat, unreal.MaterialExpressionCameraPositionWS, -1800, 900)
    d = _expr(mat, unreal.MaterialExpressionSubtract, -1600, 850)
    mel.connect_material_expressions(actor, "", d, "A")
    mel.connect_material_expressions(cam, "", d, "B")
    ox = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, 1000, parameter_name="OriginX", default_value=0.0)
    oy = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, 1080, parameter_name="OriginY", default_value=0.0)
    oz = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, 1160, parameter_name="FloorZ", default_value=0.0)
    oxy = _expr(mat, unreal.MaterialExpressionAppendVector, -1650, 1040)
    mel.connect_material_expressions(ox, "", oxy, "A")
    mel.connect_material_expressions(oy, "", oxy, "B")
    oxyz = _expr(mat, unreal.MaterialExpressionAppendVector, -1500, 1080)
    mel.connect_material_expressions(oxy, "", oxyz, "A")
    mel.connect_material_expressions(oz, "", oxyz, "B")
    cs = _expr(mat, unreal.MaterialExpressionSubtract, -1350, 950)
    mel.connect_material_expressions(cam, "", cs, "A")
    mel.connect_material_expressions(oxyz, "", cs, "B")
    shelter = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, -1800, 1300, parameter_name="Shelter",
                    texture=eal.load_asset(shelter_default), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    precip = _collection(mat, "Precip", -1800, 1450)
    inputs = [("LocalPos", local, ""), ("Corner", corner, ""), ("Seed", seed, ""), ("T", time, ""), ("D", d, ""),
              ("CS", cs, ""), ("Shelter", shelter, ""), ("Precip", precip, "")]
    inputs += [(k, params[k], "") for k in ("Period", "Area", "Density", "Size", "SizeX", "SizeY", "ZScale")]
    move = _custom(mat, -900, 400, wpo, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, "SplashMotion")
    mel.connect_material_property(move, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    depth = _expr(mat, unreal.MaterialExpressionPixelDepth, -1100, -300)
    alpha = _custom(mat, -700, -300, shape, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                    [("Corner", corner, ""), ("Seed", seed, ""), ("T", time, ""), ("Depth", depth, ""),
                     ("Period", params["Period"], ""), ("Opacity", params["Opacity"], "")], "SplashShape")
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -500, -500, parameter_name="Color",
                  default_value=unreal.LinearColor(0.8, 0.83, 0.86, 1))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -500, -400, r=0.3)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_bolt_master(name, bolts):
    """M_Bolt (docs/adr/0005 phase 4): a distant lightning bolt, drawn by AMRPrecipitationActor's
    sprite for one stroke. Unlit, additive, not fogged (the storm's haze would swallow it): one of
    T_Bolts' four cells (Variant, Mirror) x BoltColor x Brightness x Flash (the stroke's envelope)."""
    mat = _new_material(name)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", True)
    for prop in ("apply_fogging", "use_translucency_vertex_fog"):
        try:
            mat.set_editor_property(prop, False)
        except Exception:
            pass
    uv = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    variant = _expr(mat, unreal.MaterialExpressionScalarParameter, -1200, 150, parameter_name="Variant", default_value=0.0)
    mirror = _expr(mat, unreal.MaterialExpressionScalarParameter, -1200, 250, parameter_name="Mirror", default_value=0.0)
    cell = _custom(mat, -900, 0, """
float u = lerp(UV.x, 1.0 - UV.x, Mirror);
return float2((floor(Variant + 0.5) + u) / 4.0, UV.y);
""", unreal.CustomMaterialOutputType.CMOT_FLOAT2, [("UV", uv, ""), ("Variant", variant, ""), ("Mirror", mirror, "")], "BoltCell")
    tex = _expr(mat, unreal.MaterialExpressionTextureSample, -650, 0, texture=eal.load_asset(bolts),
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_expressions(cell, "", tex, "UVs")
    flash = _expr(mat, unreal.MaterialExpressionScalarParameter, -650, 250, parameter_name="Flash", default_value=0.0)
    bright = _expr(mat, unreal.MaterialExpressionScalarParameter, -650, 350, parameter_name="Brightness", default_value=40.0)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -650, 450, parameter_name="BoltColor",
                  default_value=unreal.LinearColor(0.8, 0.86, 1.0, 1))
    k = _expr(mat, unreal.MaterialExpressionMultiply, -450, 300)
    mel.connect_material_expressions(flash, "", k, "A")
    mel.connect_material_expressions(bright, "", k, "B")
    kc = _expr(mat, unreal.MaterialExpressionMultiply, -300, 350)
    mel.connect_material_expressions(k, "", kc, "A")
    mel.connect_material_expressions(color, "", kc, "B")
    em = _expr(mat, unreal.MaterialExpressionMultiply, -150, 100)
    mel.connect_material_expressions(tex, "RGB", em, "A")
    mel.connect_material_expressions(kc, "", em, "B")
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_ice_master(name, macro):
    """M_Ice (docs/adr/0005 phase 4): the pond freezes in snow, as the original turns its water to
    ice (kod room.kod StartSnow). Drawn on a copy of each water mesh just above the water: masked,
    freezing in patches that spread as MPC_Environment.SnowCover grows (from 0.15), grey-blue and
    fairly glossy, whitening and roughening with the snow on it in macro-noise drifts."""
    mat = _new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    macro = eal.load_asset(macro)
    cover = _collection(mat, "SnowCover", -1200, 0)
    i0 = _expr(mat, unreal.MaterialExpressionSubtract, -1050, 0, const_b=0.15)
    mel.connect_material_expressions(cover, "", i0, "A")
    i1 = _expr(mat, unreal.MaterialExpressionMultiply, -930, 0, const_b=4.0)
    mel.connect_material_expressions(i0, "", i1, "A")
    ice = _expr(mat, unreal.MaterialExpressionSaturate, -810, 0)
    mel.connect_material_expressions(i1, "", ice, "")
    uvm = _world_uv(mat, -1500, 300, "DriftCm", 900.0)
    m = _expr(mat, unreal.MaterialExpressionTextureSample, -1100, 300, texture=macro,
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    mel.connect_material_expressions(uvm, "", m, "UVs")
    # it freezes in patches that spread and join (macro noise B against the cover); clip at 1/3
    patch0 = _expr(mat, unreal.MaterialExpressionMultiply, -650, 0, const_b=1.6)
    mel.connect_material_expressions(ice, "", patch0, "A")
    patches = _expr(mat, unreal.MaterialExpressionSubtract, -520, 0)
    mel.connect_material_expressions(patch0, "", patches, "A")
    mel.connect_material_expressions(m, "B", patches, "B")
    mel.connect_material_property(patches, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    drift0 = _expr(mat, unreal.MaterialExpressionMultiply, -900, 300)
    mel.connect_material_expressions(m, "G", drift0, "A")
    mel.connect_material_expressions(cover, "", drift0, "B")
    drift1 = _expr(mat, unreal.MaterialExpressionMultiply, -780, 300, const_b=1.6)
    mel.connect_material_expressions(drift0, "", drift1, "A")
    drift = _expr(mat, unreal.MaterialExpressionSaturate, -660, 300)
    mel.connect_material_expressions(drift1, "", drift, "")
    ice_col = _expr(mat, unreal.MaterialExpressionConstant3Vector, -660, 450, constant=unreal.LinearColor(0.22, 0.3, 0.34, 1))
    snow_col = _expr(mat, unreal.MaterialExpressionConstant3Vector, -660, 550, constant=unreal.LinearColor(0.8, 0.82, 0.86, 1))
    base = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -450, 450)
    mel.connect_material_expressions(ice_col, "", base, "A")
    mel.connect_material_expressions(snow_col, "", base, "B")
    mel.connect_material_expressions(drift, "", base, "Alpha")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -450, 650, const_a=0.12, const_b=0.7)
    mel.connect_material_expressions(drift, "", rough, "Alpha")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def ensure_shelter_textures(entries):
    """{rid: shelter.json entry} -> {rid: T_Shelter_<rid> path}: 8-bit heights, linear, no mips,
    nearest filtering, clamped (re-imported only when the file changes)."""
    cache = build_cache.CACHE
    out, stale = {}, []
    for rid, e in entries.items():
        if e.get("shares"):
            continue
        src = os.path.join(SHELTER, e["file"])
        name = "T_Shelter_%s" % rid
        path = "%s/%s" % (TEX_DIR, name)
        key = cache.key(build_cache.file_digest(src), build_cache.source(ensure_shelter_textures))
        out[rid] = path
        if cache.fresh(path, key):
            cache.done(path, key, "textures", False)
            continue
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = TEX_DIR
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        asset_tools.import_asset_tasks([task])
        tex = eal.load_asset(path)
        if not tex:
            log("WARNING: %s did not import" % src)
            out.pop(rid)
            continue
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("never_stream", True)
        eal.save_loaded_asset(tex)
        cache.done(path, key, "textures", True)
    for rid, e in entries.items():
        if e.get("shares") and str(e["shares"]) in out:
            out[rid] = out[str(e["shares"])]
    return out


def build_precip(shelter_default, cfg):
    """-> {"default": MI_Precip, rid: MI_Precip_<rid>}: M_Precip with materials.json "precip" values
    and, per zone, its shelter map (tools/environment/shelter.py). Empty without a shelter default."""
    if not shelter_default:
        return {}
    master = _master("M_Precip", build_precip_master, shelter_default, PRECIP_WPO, PRECIP_SHAPE)
    splash_master = _master("M_Splash", build_splash_master, shelter_default, SPLASH_WPO, SPLASH_SHAPE)
    look = {k: float(v) for k, v in (cfg or {}).items() if not k.startswith("_") and isinstance(v, (int, float))}
    splash_look = {k: float(v) for k, v in (cfg or {}).get("splash", {}).items() if not k.startswith("_")}
    out = {"default": _instance("MI_Precip", master, scalars=look)}
    # without a shelter map there's no surface to splash on: the default splash shows nothing
    _instance("MI_Splash", splash_master, scalars=dict(splash_look, Density=0.0))
    meta_path = os.path.join(SHELTER, "shelter.json")
    if not os.path.exists(meta_path):
        log("no %s (run tools/environment/shelter.py): rain and snow fall under roofs" % meta_path)
        return out
    entries = json.load(open(meta_path, encoding="utf-8"))
    textures = ensure_shelter_textures(entries)
    for rid, e in sorted(entries.items()):
        if rid not in textures:
            continue
        where = dict(OriginX=e["origin_cm"][0], OriginY=e["origin_cm"][1], FloorZ=e["z0_cm"],
                     SizeX=e["size_cm"][0], SizeY=e["size_cm"][1], ZScale=e["z_scale_cm"])
        out[rid] = _instance("MI_Precip_%s" % rid, master, textures={"Shelter": textures[rid]}, scalars=dict(look, **where))
        _instance("MI_Splash_%s" % rid, splash_master, textures={"Shelter": textures[rid]}, scalars=dict(splash_look, **where))
    return out


AMBIENT_WPO = """
// One quad of SM_Precip -> an ambient particle near the camera (docs/adr/0005 phase 5). A quarter of
// the quads each: dust motes (inside), pollen (by day), fireflies (dusk and night, near the ground)
// and leaves (the forest); MPC_Environment Motes, Pollen, Fireflies, Leaves say how many of each
// show (times the kind's Share). Each kind wraps in its own box around the camera, in world space
// (box sizes divide the actor's grid, so they stay put as it moves).
float k = floor(Seed.y * 4.0);
float r = frac(Seed.y * 4.0);
float m = k == 0.0 ? 1.0 : 0.0;
float p = k == 1.0 ? 1.0 : 0.0;
float f = k == 2.0 ? 1.0 : 0.0;
float l = k == 3.0 ? 1.0 : 0.0;
float amount = m * Motes * MoteShare + p * Pollen * PollenShare + f * Fireflies * FireflyShare + l * Leaves * LeafShare;
float vis = step(Seed.x, amount);
float3 box = m > 0.5 ? float3(800.0, 800.0, 400.0) : (l > 0.5 ? float3(3200.0, 3200.0, 1600.0) : float3(1600.0, 1600.0, 800.0));
float ph = Seed.x * 61.0 + r * 17.0;
float2 windv = float2(1.0, 0.35) * Wind;
float3 move = 0;
// motes: a slow drift and wander
move += m * (float3(4.0, 2.5, 1.5) * T + float3(sin(T * 0.13 + ph), cos(T * 0.11 + ph * 1.3), sin(T * 0.09 + ph * 0.7)) * float3(25.0, 25.0, 12.0));
// pollen: carried on the wind, bobbing
move += p * (float3(windv * 35.0, 3.0) * T + float3(sin(T * 0.4 + ph), cos(T * 0.33 + ph), sin(T * 0.7 + ph)) * float3(40.0, 40.0, 25.0));
// fireflies: lazy loops
move += f * float3(sin(T * 0.35 + ph) * 120.0 + sin(T * 1.1 + ph * 2.0) * 25.0, cos(T * 0.29 + ph) * 120.0 + cos(T * 0.9 + ph) * 25.0, 0.0);
// leaves: falling, carried by the wind, fluttering side to side
move += l * (float3(windv * 90.0, -LeafFall) * T + float3(sin(T * 1.7 + ph) * 45.0, cos(T * 1.3 + ph) * 30.0, 0.0));
// the quad's place in the box comes from its seed, the same on all four corners (wrapping each
// corner's own position split quads lying on the wrap into slivers across the whole box)
float3 home = frac(sin(float3(dot(Seed, float2(12.9898, 78.233)), dot(Seed, float2(39.346, 11.135)), dot(Seed, float2(73.156, 52.235)))) * 43758.5453);
float3 rel = frac(home + (D + move) / box) * box - 0.5 * box;
// the shelter map (highest surface per 25 cm): the ground outdoors
float2 suv = (CS.xy + rel.xy) / float2(SizeX, SizeY);
float inside = step(0.0, suv.x) * step(suv.x, 1.0) * step(0.0, suv.y) * step(suv.y, 1.0);
float top = Texture2DSampleLevel(Shelter, ShelterSampler, saturate(suv), 0).r * ZScale;
// fireflies hover 0.3 to 2.3 m over open ground below eye level (not on roofs)
float hover = 30.0 + r * 200.0 + sin(T * 0.6 + ph) * 25.0;
rel.z = lerp(rel.z, top + hover - CS.z, f);
vis *= lerp(1.0, inside * step(top, CS.z - 60.0), f);
// pollen and leaves: not under roofs or below the ground
vis *= lerp(1.0, lerp(1.0, step(top, CS.z + rel.z), inside), saturate(p + l));
// not right in front of the lens (pollen that close is a blurred orb)
vis *= step(40.0 + 110.0 * p, length(rel));
float2 hz = normalize(-rel.xy + 0.001);
float3 right = float3(-hz.y, hz.x, 0.0);
// leaves tumble: their width turns with a spin
float spin = cos(T * (2.0 + r * 3.0) + ph);
float size = (m * MoteSize + p * PollenSize + f * FireflySize + l * LeafSize) * lerp(0.7, 1.3, frac(ph));
float2 c = Corner - 0.5;
float3 target = rel + (right * c.x * size * lerp(1.0, spin, l) - float3(0.0, 0.0, c.y * size)) * vis;
return target - (LocalPos + D);
"""

AMBIENT_LOOK = """
// colour (rgb) and opacity (a) of an ambient particle: soft round motes and pollen, fireflies' dark
// bodies (their light is AMBIENT_GLOW), leaves as small solid ovals, green or (MPC Autumn) turned
float k = floor(Seed.y * 4.0);
float r = frac(Seed.y * 4.0);
float2 c = Corner - 0.5;
float soft = saturate(1.0 - length(c) * 2.0);
float blink = pow(saturate(sin(T * (1.6 + r) + frac(Seed.x * 97.0) * 6.2832)), 4.0);
float4 o;
if (k == 0.0)
    o = float4(MoteColor, pow(soft, 1.5) * MoteOpacity * saturate(1.0 - Depth / 350.0));
else if (k == 1.0)
    o = float4(PollenColor, pow(soft, 1.5) * PollenOpacity * saturate(1.0 - Depth / 700.0));
else if (k == 2.0)
    o = float4(0.06, 0.06, 0.03, lerp(pow(soft, 4.0) * 0.4, pow(soft, 1.5), blink));
else
{
    float leaf = step(length(float2(c.x / 0.3, c.y / 0.5)), 1.0);
    float3 green = float3(0.1, 0.17, 0.035) * lerp(0.8, 1.2, r);
    float3 turned = lerp(float3(0.42, 0.12, 0.03), float3(0.55, 0.38, 0.05), r);
    o = float4(lerp(green, turned, saturate(Autumn + 0.35)), leaf * saturate(1.0 - Depth / 2500.0));
}
return o;
"""

AMBIENT_GLOW = """
// the fireflies' light: each blinks on for about a second every few seconds
float k = floor(Seed.y * 4.0);
float r = frac(Seed.y * 4.0);
float2 c = Corner - 0.5;
float soft = saturate(1.0 - length(c) * 2.0);
float blink = pow(saturate(sin(T * (1.6 + r) + frac(Seed.x * 97.0) * 6.2832)), 4.0);
return (k == 2.0 ? 1.0 : 0.0) * FireflyColor * FireflyGlow * blink * (pow(soft, 4.0) * 3.0 + soft);
"""


def _camera_box_inputs(mat, x, y, shelter_default):
    """The inputs a camera-box material (M_Ambient) reads: [(name, expression, output)] for LocalPos,
    Corner, Seed, T, D (actor - camera), CS (camera - shelter origin and floor), Shelter, and the
    shelter map's size parameters (a zone's instance sets them, as M_Precip's)."""
    local = _expr(mat, unreal.MaterialExpressionPreSkinnedPosition, x, y)
    corner = _expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y + 100)
    seed = _expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y + 200, coordinate_index=1)
    time = _expr(mat, unreal.MaterialExpressionTime, x, y + 300)
    actor = _expr(mat, unreal.MaterialExpressionActorPositionWS, x, y + 400)
    cam = _expr(mat, unreal.MaterialExpressionCameraPositionWS, x, y + 500)
    d = _expr(mat, unreal.MaterialExpressionSubtract, x + 200, y + 450)
    mel.connect_material_expressions(actor, "", d, "A")
    mel.connect_material_expressions(cam, "", d, "B")
    ox = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 600, parameter_name="OriginX", default_value=0.0)
    oy = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 680, parameter_name="OriginY", default_value=0.0)
    oz = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 760, parameter_name="FloorZ", default_value=-1.0e7)
    oxy = _expr(mat, unreal.MaterialExpressionAppendVector, x + 150, y + 640)
    mel.connect_material_expressions(ox, "", oxy, "A")
    mel.connect_material_expressions(oy, "", oxy, "B")
    oxyz = _expr(mat, unreal.MaterialExpressionAppendVector, x + 300, y + 680)
    mel.connect_material_expressions(oxy, "", oxyz, "A")
    mel.connect_material_expressions(oz, "", oxyz, "B")
    cs = _expr(mat, unreal.MaterialExpressionSubtract, x + 450, y + 550)
    mel.connect_material_expressions(cam, "", cs, "A")
    mel.connect_material_expressions(oxyz, "", cs, "B")
    shelter = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, x, y + 900, parameter_name="Shelter",
                    texture=eal.load_asset(shelter_default), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    sizes = [(n, _expr(mat, unreal.MaterialExpressionScalarParameter, x, y + 1000 + i * 80, parameter_name=n, default_value=1.0), "")
             for i, n in enumerate(("SizeX", "SizeY", "ZScale"))]
    return [("LocalPos", local, ""), ("Corner", corner, ""), ("Seed", seed, ""), ("T", time, ""), ("D", d, ""),
            ("CS", cs, ""), ("Shelter", shelter, "")] + sizes


def build_ambient_master(name, shelter_default, wpo, look, glow):
    """M_Ambient (docs/adr/0005 phase 5): SM_Precip's quads as ambient particles around the camera
    (AMBIENT_WPO): dust motes, pollen, fireflies and leaves, as many as MPC_Environment Motes, Pollen,
    Fireflies and Leaves say. Lit translucent like M_Precip (motes catch the lamps and torches, pollen
    the sun), plus the fireflies' blinking light as emissive."""
    mat = _new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    mat.set_editor_property("two_sided", True)
    params = {}
    for i, (pname, default) in enumerate((
            ("MoteShare", 0.5), ("PollenShare", 0.12), ("FireflyShare", 0.08), ("LeafShare", 0.2),
            ("MoteSize", 0.9), ("PollenSize", 0.8), ("FireflySize", 6.0), ("LeafSize", 7.0), ("LeafFall", 70.0),
            ("MoteOpacity", 0.7), ("PollenOpacity", 0.8), ("FireflyGlow", 6.0))):
        params[pname] = _expr(mat, unreal.MaterialExpressionScalarParameter, -2200, -900 + i * 80,
                              parameter_name=pname, default_value=default)
    colours = {}
    for i, (pname, default) in enumerate((("MoteColor", (0.85, 0.78, 0.66)), ("PollenColor", (1.0, 0.9, 0.55)),
                                          ("FireflyColor", (0.55, 1.0, 0.12)))):
        colours[pname] = _expr(mat, unreal.MaterialExpressionVectorParameter, -2200, 100 + i * 100, parameter_name=pname,
                               default_value=unreal.LinearColor(*default, 1.0))
    box = _camera_box_inputs(mat, -2200, 500, shelter_default)
    by_name = {n: (e, o) for n, e, o in box}
    amounts = [(n, _collection(mat, n, -2200, 1900 + i * 100), "") for i, n in enumerate(("Motes", "Pollen", "Fireflies", "Leaves", "Wind"))]
    inputs = box + amounts + [(k, params[k], "") for k in ("MoteShare", "PollenShare", "FireflyShare", "LeafShare",
                                                             "MoteSize", "PollenSize", "FireflySize", "LeafSize", "LeafFall")]
    move = _custom(mat, -1000, 600, wpo, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, "AmbientMotion")
    mel.connect_material_property(move, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    depth = _expr(mat, unreal.MaterialExpressionPixelDepth, -1200, -400)
    autumn = _collection(mat, "Autumn", -1200, -300)
    common = [("Corner",) + by_name["Corner"], ("Seed",) + by_name["Seed"], ("T",) + by_name["T"]]
    rgba = _custom(mat, -800, -400, look, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                   common + [("Depth", depth, ""), ("Autumn", autumn, ""), ("MoteColor", colours["MoteColor"], ""),
                             ("PollenColor", colours["PollenColor"], ""), ("MoteOpacity", params["MoteOpacity"], ""),
                             ("PollenOpacity", params["PollenOpacity"], "")], "AmbientLook")
    rgb = _expr(mat, unreal.MaterialExpressionComponentMask, -550, -450, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(rgba, "", rgb, "")
    alpha = _expr(mat, unreal.MaterialExpressionComponentMask, -550, -300, r=False, g=False, b=False, a=True)
    mel.connect_material_expressions(rgba, "", alpha, "")
    mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    light = _custom(mat, -800, -150, glow, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                    common + [("FireflyColor", colours["FireflyColor"], ""), ("FireflyGlow", params["FireflyGlow"], "")], "FireflyGlow")
    mel.connect_material_property(light, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -550, -100, r=0.6)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


PUFF_HASH = """
float3 hp = frac(Obj * 0.0123);
float ph = frac(sin(dot(hp, float3(12.9898, 78.233, 37.719))) * 43758.5453);
"""

SMOKE_WPO = PUFF_HASH + """
// One puff of a chimney's plume (docs/adr/0005 phase 5). Each puff rises from the chimney top over
// Life seconds, slowing as it goes, bent downwind (MPC Wind, the same direction as the rain's) and
// growing; SM_Puffs spreads the puffs evenly in age, and each chimney starts from its own phase.
float a = frac(T / Life + Seed.x + ph);
float2 wdir = normalize(float2(1.0, 0.35));
float rise = Rise * (a - 0.35 * a * a) / 0.65;
float2 drift = wdir * Wind * Bend * pow(a, 1.5) + float2(sin(T * 0.5 + Seed.y * 30.0), cos(T * 0.43 + Seed.y * 20.0)) * 40.0 * a;
float3 centre = Obj + float3(drift, rise);
float3 toCam = normalize(Cam - centre);
float3 right = normalize(cross(float3(0.0, 0.0, 1.0), toCam) + float3(0.0001, 0.0, 0.0));
float3 up = cross(toCam, right);
float size = lerp(Size0, Size1, pow(a, 0.7)) * lerp(0.8, 1.2, Seed.y) * lerp(0.75, 1.0, Smoke);
float spin = Seed.y * 6.2832 + a * 1.2;
float2 c = Corner - 0.5;
float2 cr = float2(c.x * cos(spin) - c.y * sin(spin), c.x * sin(spin) + c.y * cos(spin));
return centre + (right * cr.x - up * cr.y) * size - WPN;
"""

SMOKE_LOOK = PUFF_HASH + """
// a soft, lumpy puff (two octaves of value noise on the quad, drifting), fading in as it leaves the
// chimney and out as it spreads; MPC Smoke says how thick the chimneys smoke
float a = frac(T / Life + Seed.x + ph);
float2 c = Corner - 0.5;
float n = 0.0;
float w = 0.65;
float2 q = Corner * 3.0 + Seed.y * 17.0 + T * 0.04;
for (int o = 0; o < 2; o++)
{
    float2 i0 = floor(q);
    float2 fq = frac(q);
    fq = fq * fq * (3.0 - 2.0 * fq);
    float n00 = frac(sin(dot(i0, float2(127.1, 311.7))) * 43758.5453);
    float n10 = frac(sin(dot(i0 + float2(1.0, 0.0), float2(127.1, 311.7))) * 43758.5453);
    float n01 = frac(sin(dot(i0 + float2(0.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    float n11 = frac(sin(dot(i0 + float2(1.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    n += w * lerp(lerp(n00, n10, fq.x), lerp(n01, n11, fq.x), fq.y);
    q = q * 2.1 + 3.7;
    w *= 0.5;
}
float puff = saturate(1.0 - length(c) * 2.0);
puff = puff * puff * (3.0 - 2.0 * puff);
float body = saturate(puff * (0.45 + n) - 0.2);
float life = smoothstep(0.0, 0.04, a) * pow(1.0 - a, 1.6);
return body * life * Opacity * Smoke;
"""

MOTH_WPO = PUFF_HASH + """
// A moth circling a lamp at night (docs/adr/0005 phase 5): Share of SM_Puffs' quads, each on its own
// erratic orbit around the lantern, wings flapping (the quad's width). Only while the lamps are lit
// in the dark (MPC LampsOn x Night) and not in winter.
float on = LampsOn * Night * (1.0 - Winter);
float vis = step(Seed.x, Share) * step(0.5, on);
float s = frac(Seed.y * 7.0 + ph);
float dir = Seed.y > 0.5 ? 1.0 : -1.0;
float th = T * (1.0 + s * 1.5) * dir + Seed.y * 40.0;
float R = Radius * (0.45 + s);
float3 pos = float3(cos(th) * R, sin(th) * R * 0.8, sin(T * 1.15 + Seed.y * 30.0) * 18.0 + (frac(Seed.y * 13.0) - 0.5) * 40.0);
pos += float3(sin(T * 8.5 + Seed.y * 50.0), cos(T * 6.5 + Seed.y * 40.0), sin(T * 5.5 + Seed.y * 20.0)) * 3.0;
float3 centre = Obj + pos;
float3 toCam = normalize(Cam - centre);
float3 right = normalize(cross(float3(0.0, 0.0, 1.0), toCam) + float3(0.0001, 0.0, 0.0));
float3 up = cross(toCam, right);
float flap = 0.3 + 0.7 * abs(sin(T * 24.0 + Seed.y * 90.0));
float2 c = Corner - 0.5;
return centre + (right * c.x * Size * flap - up * c.y * Size * 0.7) * vis - WPN;
"""

MOTH_SHAPE = """
// two wings and a body
float2 c = Corner - 0.5;
float wings = step(length(float2((abs(c.x) - 0.22) / 0.24, c.y / 0.4)), 1.0);
float body = step(abs(c.x), 0.05) * step(abs(c.y), 0.3);
return saturate(wings + body);
"""


def _puff_inputs(mat, x, y):
    """Corner, Seed, T, Obj (the actor's origin: not ObjectPositionWS, which is the centre of the
    bounds and so moves with the actor's scale), Cam and WPN (the vertex's world position before any
    offset) for the SM_Puffs materials."""
    corner = _expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y)
    seed = _expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y + 100, coordinate_index=1)
    time = _expr(mat, unreal.MaterialExpressionTime, x, y + 200)
    obj = _expr(mat, unreal.MaterialExpressionActorPositionWS, x, y + 300)
    cam = _expr(mat, unreal.MaterialExpressionCameraPositionWS, x, y + 400)
    wpn = _expr(mat, unreal.MaterialExpressionWorldPosition, x, y + 500,
                world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    return {"Corner": corner, "Seed": seed, "T": time, "Obj": obj, "Cam": cam, "WPN": wpn}


def build_smoke_master(name, wpo, look):
    """M_Smoke (docs/adr/0005 phase 5): SM_Puffs as a chimney's plume (SMOKE_WPO, SMOKE_LOOK): grey,
    lit translucent (it takes the sun's, the sky's and the moon's colour), soft where it meets the
    chimney (depth fade). MPC_Environment.Smoke (the director: thicker on cold mornings and in
    winter) and Wind (bends it)."""
    mat = _new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    mat.set_editor_property("two_sided", True)
    params = {}
    for i, (pname, default) in enumerate((("Life", 14.0), ("Rise", 650.0), ("Bend", 420.0), ("Size0", 70.0),
                                          ("Size1", 420.0), ("Opacity", 0.9))):
        params[pname] = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, -600 + i * 80,
                              parameter_name=pname, default_value=default)
    pin = _puff_inputs(mat, -1800, 0)
    smoke = _collection(mat, "Smoke", -1800, 700)
    wind = _collection(mat, "Wind", -1800, 800)
    move = _custom(mat, -900, 300, wpo, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   [(k, pin[k], "") for k in ("Corner", "Seed", "T", "Obj", "Cam", "WPN")]
                   + [("Wind", wind, ""), ("Smoke", smoke, "")]
                   + [(k, params[k], "") for k in ("Life", "Rise", "Bend", "Size0", "Size1")], "SmokeMotion")
    mel.connect_material_property(move, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    alpha = _custom(mat, -900, -300, look, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                    [(k, pin[k], "") for k in ("Corner", "Seed", "T", "Obj")]
                    + [("Smoke", smoke, ""), ("Life", params["Life"], ""), ("Opacity", params["Opacity"], "")], "SmokeLook")
    fade = _expr(mat, unreal.MaterialExpressionDepthFade, -650, -150)
    fade.set_editor_property("fade_distance_default", 80.0)
    soft = _expr(mat, unreal.MaterialExpressionMultiply, -450, -250)
    mel.connect_material_expressions(alpha, "", soft, "A")
    mel.connect_material_expressions(fade, "", soft, "B")
    mel.connect_material_property(soft, "", unreal.MaterialProperty.MP_OPACITY)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -450, -500, parameter_name="Color",
                  default_value=unreal.LinearColor(0.4, 0.39, 0.38, 1))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -450, -400, r=1.0)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_moth_master(name, wpo, shape):
    """M_Moth (docs/adr/0005 phase 5): SM_Puffs as moths around a lamp (MOTH_WPO): translucent (no
    velocity, so motion blur doesn't smear the fast little quads into sticks, 2026-10-06), lit by
    the translucency volume (the lamp's own light), with a faint emissive so they read at night."""
    mat = _new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    mat.set_editor_property("two_sided", True)
    params = {}
    for i, (pname, default) in enumerate((("Share", 0.25), ("Radius", 40.0), ("Size", 1.4), ("Glow", 0.2))):
        params[pname] = _expr(mat, unreal.MaterialExpressionScalarParameter, -1800, -400 + i * 80,
                              parameter_name=pname, default_value=default)
    pin = _puff_inputs(mat, -1800, 0)
    mpc = [(n, _collection(mat, n, -1800, 700 + i * 100), "") for i, n in enumerate(("LampsOn", "Night", "Winter"))]
    move = _custom(mat, -900, 300, wpo, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   [(k, pin[k], "") for k in ("Corner", "Seed", "T", "Obj", "Cam", "WPN")] + mpc
                   + [(k, params[k], "") for k in ("Share", "Radius", "Size")], "MothMotion")
    mel.connect_material_property(move, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mask = _custom(mat, -900, -300, shape, unreal.CustomMaterialOutputType.CMOT_FLOAT1, [("Corner", pin["Corner"], "")], "MothShape")
    mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -650, -500, parameter_name="Color",
                  default_value=unreal.LinearColor(0.25, 0.21, 0.16, 1))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = _expr(mat, unreal.MaterialExpressionMultiply, -450, -200)
    mel.connect_material_expressions(color, "", glow, "A")
    mel.connect_material_expressions(params["Glow"], "", glow, "B")
    lit = _expr(mat, unreal.MaterialExpressionMultiply, -300, -200)
    mel.connect_material_expressions(glow, "", lit, "A")
    mel.connect_material_expressions(mpc[0][1], "", lit, "B")
    mel.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def build_atmosphere(shelter_default, cfg):
    """-> {"ambient": {"default": MI_Ambient, rid: MI_Ambient_<rid>}, "smoke": MI_Smoke, "moth": MI_Moth}
    (docs/adr/0005 phase 5) with materials.json "atmosphere" "ambient" / "smoke" / "moths" values;
    per zone the ambient instance carries the shelter map, as MI_Precip_<rid> does. Empty without a
    shelter default."""
    if not shelter_default:
        return {}
    cfg = cfg or {}

    def look(section):
        return {k: float(v) for k, v in cfg.get(section, {}).items() if not k.startswith("_") and isinstance(v, (int, float))}

    def colours(section):
        return {k: list(v) + [1.0] * (4 - len(v)) for k, v in cfg.get(section, {}).items() if not k.startswith("_") and isinstance(v, list)}
    master = _master("M_Ambient", build_ambient_master, shelter_default, AMBIENT_WPO, AMBIENT_LOOK, AMBIENT_GLOW)
    ambient = {"default": _instance("MI_Ambient", master, scalars=look("ambient"), vectors=colours("ambient"))}
    meta_path = os.path.join(SHELTER, "shelter.json")
    if os.path.exists(meta_path):
        entries = json.load(open(meta_path, encoding="utf-8"))
        textures = ensure_shelter_textures(entries)
        for rid, e in sorted(entries.items()):
            if rid in textures:
                where = dict(OriginX=e["origin_cm"][0], OriginY=e["origin_cm"][1], FloorZ=e["z0_cm"],
                             SizeX=e["size_cm"][0], SizeY=e["size_cm"][1], ZScale=e["z_scale_cm"])
                ambient[rid] = _instance("MI_Ambient_%s" % rid, master, textures={"Shelter": textures[rid]},
                                         scalars=dict(look("ambient"), **where), vectors=colours("ambient"))
    smoke = _instance("MI_Smoke", _master("M_Smoke", build_smoke_master, SMOKE_WPO, SMOKE_LOOK),
                      scalars=look("smoke"), vectors=colours("smoke"))
    moth = _instance("MI_Moth", _master("M_Moth", build_moth_master, MOTH_WPO, MOTH_SHAPE),
                     scalars=look("moths"), vectors=colours("moths"))
    return {"ambient": ambient, "smoke": smoke, "moth": moth}


def _glow_scalars(t):
    """A window texture's GlowGain and GlowTint (placeholders.json glow_gain, glow_tint)."""
    return {"GlowGain": float(t.get("glow_gain", 1.0)), "GlowTint": float(t.get("glow_tint", 1.0))}


def build_placeholders(normals_for=lambda key, entry: True, glow=None):
    """-> ({grd key: MI path}, {grd key: (manifest entry, D, N, H texture paths)}).
    Both empty if make_placeholders.py hasn't been run."""
    manifest_path = os.path.join(PLACEHOLDERS, "placeholders.json")
    if not os.path.exists(manifest_path):
        log("no %s; run tools/textures/make_placeholders.py for textured placeholders" % manifest_path)
        return {}, {}
    textures = json.load(open(manifest_path, encoding="utf-8"))["textures"]
    masters = {}
    seasons = _materials_json("seasons")

    def seasonal(t):
        return bool(seasons.get("foliage") and re.search(seasons["foliage"], t.get("name", ""), re.I))

    def master_for(t):
        """M_Placeholder / M_PlaceholderMasked, a clock variant per atlas layout, and Seasonal variants
        for foliage (materials.json "seasons" "foliage")."""
        atlas = tuple(t["atlas"]) if t.get("atlas") and glow else None
        key = (t["masked"], atlas, seasonal(t))
        if key not in masters:
            name = ("M_Placeholder" + ("Masked" if t["masked"] else "") + ("Clock%dx%d" % atlas if atlas else "")
                    + ("Seasonal" if key[2] else ""))
            masters[key] = _master(name, build_master, t["masked"], glow, atlas, key[2])
        return masters[key]

    specs = []
    tiled = vertically_tiled()
    for key, t in textures.items():
        clamp = bool(t["masked"]) and key not in tiled  # cut-outs mapped once up the wall
        specs += [(t["d"], True, False, clamp), (t["n"], False, True, clamp)]
        specs += [(t["height"], False, False, clamp)] if "height" in t else []
        specs += [(t["emissive"], False, False)] if t.get("emissive") and glow else []
    paths = ensure_textures(specs)
    out, loaded = {}, {}
    for key, t in sorted(textures.items()):
        d, n, h = paths.get(t["d"]), paths.get(t["n"]), paths.get(t.get("height"))
        if not d or not n:
            log("WARNING: textures for %s did not import" % key)
            continue
        e = paths.get(t.get("emissive")) if glow else None
        loaded[key] = (t, d, n, h, e)
        scalars = {"Roughness": t["roughness"], "NormalStrength": 1.0 if normals_for(key, t) else 0.0}
        if e:
            scalars.update(_glow_scalars(t))
        out[key] = _instance("MI_" + key, master_for(t), textures=dict({"BaseColor": d, "Normal": n}, **({"Emissive": e} if e else {})),
                             scalars=scalars)
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


def relief_config():
    """materials.json "relief": {"normals": bool, "displacement": bool, "flat": name regex,
    "flat_openings": bool}; true / false switch both on / off."""
    value = _materials_value("relief", True)
    if not isinstance(value, dict):
        value = {"normals": bool(value), "displacement": bool(value)}
    return dict({"normals": True, "displacement": True, "flat": "", "flat_openings": True}, **value)


def normals_policy(facades_textures):
    """-> normals_for(key, placeholders.json entry): whether the texture gets its normal map
    (materials.json "relief"). Off for textures with painted windows or doors (facades.json
    "openings") and for names matching "flat": inferred relief reads wrong on glass, doors and
    pictures."""
    cfg = relief_config()
    openings = {k for k, v in facades_textures.items() if v.get("openings")} if cfg["flat_openings"] else set()

    def normals_for(key, entry):
        if not cfg["normals"] or key in openings:
            return False
        return not (cfg["flat"] and re.search(cfg["flat"], entry.get("name", ""), re.I))
    return normals_for


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
        facades_json = os.path.join(REPO, "data", "environment", "facades.json")
        facades_textures = (json.load(open(facades_json, encoding="utf-8")).get("textures", {})
                            if os.path.exists(facades_json) else {})
        self.relief = relief_config()
        self.normals_for = normals_policy(facades_textures)
        no_emissive = self._import_extra("T_NoEmissive.png", srgb=False)
        self.glow = (ensure_mpc(), no_emissive) if no_emissive else None
        self.placeholders, self.textures = build_placeholders(self.normals_for, self.glow)
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
                                          if os.path.exists(props_cfg) else {}, ensure_mpc())
        stars = self._import_extra("T_Stars.png", srgb=True)
        self.night_sky = _master("M_NightSky", build_night_sky_master, stars, ensure_mpc()) if stars else None
        self.fires = build_fires()
        self.precip = build_precip(self.glow[1] if self.glow else None, _materials_json("precip"))
        bolts = self._import_extra("T_Bolts.png", srgb=True)
        self.bolt = (_instance("MI_Bolt", _master("M_Bolt", build_bolt_master, bolts),
                               scalars={k: float(v) for k, v in _materials_json("precip").get("bolt", {}).items() if not k.startswith("_")})
                     if bolts else None)
        self.ice = _master("M_Ice", build_ice_master, self.macro) if self.macro else None
        self.atmosphere = build_atmosphere(self.glow[1] if self.glow else None, _materials_json("atmosphere"))

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

    def grime_instance(self, kind):
        """MI_grime_<kind> (base, eave) on M_GrimeDecal, from materials.json "grime"."""
        cfg = _materials_json("grime").get(kind)
        if not cfg or not self.macro:
            return None
        master = _master("M_GrimeDecal", build_grime_master, self.macro)
        return _instance("MI_grime_" + kind, master,
                         scalars={"Opacity": cfg.get("opacity", 0.5), "Falloff": cfg.get("falloff", 1.6)},
                         vectors={"Color": list(cfg.get("color", [0.04, 0.035, 0.03])) + [1.0]})

    def ground_instance(self, key):
        """MI_<grd>__ground on M_Ground for floors listed in materials.json "ground"."""
        if key in self.ground:
            return self.ground[key]
        cfg = self.ground_config.get(key)
        if not cfg or not self.ground_master or key not in self.textures:
            return None
        t, d, n, h, _ = self.textures[key]
        tile = float(cfg.get("tile_m", 2.2)) * 100.0
        scalars = {"TileCm": tile, "TileCm2": tile * 2.73, "Roughness": t["roughness"]}
        if "normal_strength" in cfg:
            scalars["NormalStrength"] = float(cfg["normal_strength"])  # else M_Ground's default (0.8)
        if "large_normal_scale" in cfg:
            scalars["LargeNormalScale"] = float(cfg["large_normal_scale"])  # else M_Ground's default (1.5)
        if not self.normals_for(key, t):
            scalars["NormalStrength"] = 0.0
        self.ground[key] = _instance("MI_%s__ground" % key, self.ground_master, textures={"BaseColor": d, "Normal": n},
                                     scalars=scalars)
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
        if key.startswith("grime_"):
            mat = self.grime_instance(key[len("grime_"):])
            return (mat, 1) if mat else (None, 2)
        flat = flat or not self.relief["displacement"]  # no displacement: no Nanite tessellation either
        cache = self.art_flat if flat else self.art
        if key in cache:
            return cache[key], 1
        base, _, variant = key.partition("__")
        if base not in self.textures:
            return None, 2
        t, d, n, h, e = self.textures[base]
        if not h or (t["masked"] and variant != "solid"):
            # cut-out originals copied into the art (signs, fences) stay cut-out; "__solid" ones
            # were rebuilt as solid geometry (merlons)
            return self.material_for(key)
        atlas = tuple(t["atlas"]) if t.get("atlas") and self.glow else None
        if atlas and base not in cache:
            # clock faces: their own master per atlas layout (no displacement)
            name = "M_PlaceholderArtFlatClock%dx%d" % atlas
            master = _master(name, build_displaced_master, h, False, DISPLACEMENT_CM, self.glow, atlas)
            cache[base] = _instance("MI_%s__flat" % base, master,
                                    textures=dict({"BaseColor": d, "Normal": n, "Height": h}, **({"Emissive": e} if e else {})),
                                    scalars={"Roughness": t["roughness"], "NormalStrength": 1.0 if self.normals_for(base, t) else 0.0,
                                             **(_glow_scalars(t) if e else {}),
                                             "DisplacementStrength": 0.0})
        if base not in cache:
            if flat:
                # relief already in the geometry: same look, no Nanite tessellation
                if not self.flat_master:
                    self.flat_master = _master("M_PlaceholderArtFlat", build_displaced_master, h, False,
                                               DISPLACEMENT_CM, self.glow)
                master, suffix = self.flat_master, "__flat"
            else:
                if not self.displaced_master:
                    self.displaced_master = _master("M_PlaceholderDisplaced", build_displaced_master, h, True,
                                                    displacement_range_cm(), self.glow)
                master, suffix = self.displaced_master, "__art"
            cache[base] = _instance("MI_%s%s" % (base, suffix), master,
                                    textures=dict({"BaseColor": d, "Normal": n, "Height": h}, **({"Emissive": e} if e else {})),
                                    scalars={"Roughness": t["roughness"], "NormalStrength": 1.0 if self.normals_for(base, t) else 0.0,
                                             **(_glow_scalars(t) if e else {}),
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
