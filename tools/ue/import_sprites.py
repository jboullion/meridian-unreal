"""
Sprite players (docs/sprites.md): import the atlases and palette lookups written by
tools/sprites/build_player_sprites.py and build the sprite body materials. Runs inside the Unreal
Editor (tools/ue/import_sprites.ps1).

    /Game/Generated/Sprites/T_Spr_<bgf>       one atlas per bgf, in its untranslated colours
    /Game/Generated/Sprites/T_SprRamp_<bgf>   player parts: each original pixel's palette ramp
    /Game/Generated/Sprites/T_SprXlat         palette translations (row = xlat id, column = index)
    /Game/Generated/Sprites/T_SprClass        colour classifier (tools/sprites/luts.py)
    /Game/Generated/Sprites/M_SpriteBody      lit, masked: a character's render targets
                                              (UMRSpriteBodyComponent) on its quad
    /Game/Generated/Sprites/M_SpriteBodyUnlit the same, unlit (closest to the original look)
    /Game/Generated/Sprites/M_PreviewBackdrop the flat grey behind the character creator's previews

M_SpriteBody reads three render targets (UMRSpriteBodyComponent):
  Sprite      the parts in their untranslated colours, drawn alpha-tested; alpha = coverage
  SpriteCode  per pixel the part's palette translation (R * 255) and surface class (G * 255),
              clipped like Sprite
  SpriteRamp  per pixel the palette ramp of the original pixel under it (B * 3: 0 none, 1 red,
              2 dark blue, 3 grey) and that part's translation (R * 255), drawn unfiltered from the
              ramp atlases (tools/sprites/luts.py ramp_cell)
and recolours like the original client: T_SprClass gives the colour's position on its ramp,
T_SprXlat that ramp entry's colour under the part's translation. (Until 2026-10-07 the ramp was
coded in the atlas alpha; the canvas filtered it into other ramps' codes at the parts' edges, which
showed as dark blue and red specks round the eyes, nose, mouth and hair.) Surface classes
(data/sprites/materials.json) give roughness, metallic and specular (Class0..7 parameters).
Lighting: a mostly-up world normal (NormalUp) keeps the brightness steady as the camera orbits;
Albedo scales the colour for the lit material; indoors the environment's ambient floor
(MPC_Environment.SectorAmbient * AmbientTint, 0 outdoors) is added as emissive * SpriteAmbient, as
the zone materials do.

An atlas is re-imported only when its hash in data/sprites/player_parts.json changed (the imported
hashes are kept in Saved/MRBuild/sprites_cache.json); import_sprites.ps1 deletes atlases no longer
in the layout before the editor starts. Everything here is
git-ignored and rebuilt by tools/setup.ps1.
"""
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAYOUT = os.path.join(REPO, "data", "sprites", "player_parts.json")
ATLAS_DIR = os.path.join(REPO, "build", "sprites", "atlas")
LUT_DIR = os.path.join(REPO, "build", "sprites", "lut")
DIR = "/Game/Generated/Sprites"
MPC = "/Game/Generated/Environment/Materials/MPC_Environment"
CACHE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "MRBuild", "sprites_cache.json")
MAX_CLASSES = 8

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[import_sprites] " + msg)


def _expr(mat, cls, x, y, **props):
    e = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def _import(files, folder):
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = os.path.join(folder, f)
        t.destination_path = DIR
        t.automated = True
        t.replace_existing = True
        t.save = False
        tasks.append(t)
    if tasks:
        asset_tools.import_asset_tasks(tasks)


def import_atlases(layout):
    wanted = {a["texture"] for a in layout["atlases"].values()}
    cache = json.load(open(CACHE)) if os.path.exists(CACHE) else {}
    stale = [a for _, a in sorted(layout["atlases"].items())
             if not (eal.does_asset_exist("%s/%s" % (DIR, a["texture"])) and cache.get(a["texture"]) == a["hash"])]
    _import([a["texture"] + ".png" for a in stale], ATLAS_DIR)
    _import([a["ramp"] + ".png" for a in stale if a.get("ramp")], ATLAS_DIR)
    for a in stale:
        if a.get("ramp"):
            _ramp_settings(a["ramp"])
        tex = eal.load_asset("%s/%s" % (DIR, a["texture"]))
        if not tex:
            log("WARNING: %s did not import" % a["texture"])
            continue
        tex.set_editor_property("srgb", True)
        # alpha edges matter (they are the cut-out): BC7 keeps them, DXT5 bands them
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        # drawn into render targets by the canvas, which the streamer can't see: keep resident;
        # mips stay on: face parts (shrink 14) are drawn ~3x smaller than their atlas cells
        tex.set_editor_property("never_stream", True)
        # the original pixels (data/sprites/upscale.json store.scale 1): square, as the original drew them
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST if layout.get("scale", 4) == 1
                                else unreal.TextureFilter.TF_DEFAULT)
        eal.save_loaded_asset(tex)
        cache[a["texture"]] = a["hash"]
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    json.dump(cache, open(CACHE, "w"), indent=1)
    # (atlases of earlier layouts are deleted by import_sprites.ps1 before the editor starts)
    log("%d atlases, %d imported" % (len(wanted), len(stale)))


def _ramp_settings(name):
    """Exact values, one texel per original pixel: 16-bit (TC_LQ: A1RGB555, which keeps red 255,
    the ramp's four levels and the 1-bit coverage exactly; DXT5 on Mac, within the decoder's
    rounding), linear, unfiltered, no mips."""
    tex = eal.load_asset("%s/%s" % (DIR, name))
    if not tex:
        log("WARNING: %s did not import" % name)
        return
    tex.set_editor_property("srgb", False)
    # TC_LQ is hidden from the editor's list, so Python's enum lacks it: set from C++
    if not unreal.MREditorScripting.set_low_quality_compression(tex):
        log("WARNING: %s: couldn't store it 16-bit; left uncompressed" % name)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("never_stream", True)
    eal.save_loaded_asset(tex)


def import_luts():
    """Uncompressed and unfiltered where it matters: the lookups must return exact values."""
    _import(["T_SprXlat.png", "T_SprClass.png"], LUT_DIR)
    for name, srgb, nearest in (("T_SprXlat", True, False), ("T_SprClass", False, True)):
        tex = eal.load_asset("%s/%s" % (DIR, name))
        tex.set_editor_property("srgb", srgb)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON if srgb
                                else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST if nearest else unreal.TextureFilter.TF_BILINEAR)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("never_stream", True)
        eal.save_loaded_asset(tex)
    log("lookups imported")


def _new_material(name):
    path = "%s/%s" % (DIR, name)
    if eal.does_asset_exist(path):
        mat = eal.load_asset(path)
        mel.delete_all_material_expressions(mat)
        return mat
    return asset_tools.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())


def _custom(mat, x, y, desc, out_type, code, inputs):
    c = _expr(mat, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type, description=desc)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    for n, src in inputs.items():
        mel.connect_material_expressions(src, "", c, n)
    return c


RECOLOUR_HLSL = """
// the parts, untranslated; alpha = coverage (the texels round a part carry its own colours, so the
// filtered colour at an edge is the part's, never black)
float4 c = Texture2DSample(Sprite, SpriteSampler, UV);
float cover = c.a;
float3 rgb = c.rgb;
uint w, h;
Code.GetDimensions(w, h);
int2 px = min(int2(UV * float2(w, h)), int2(w - 1, h - 1));
const int2 offs[13] = {int2(0, 0), int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1), int2(1, 1), int2(-1, -1),
                       int2(1, -1), int2(-1, 1), int2(2, 0), int2(-2, 0), int2(0, 2), int2(0, -2)};
// the part here: its translation (exact texels, no filtering)
float4 code = Code.Load(int3(px, 0));
if (cover > 0.5 && code.a < 0.5)
{
    [unroll] for (int j = 1; j < 13; j++)
    {
        float4 cq = Code.Load(int3(clamp(px + offs[j], int2(0, 0), int2(w - 1, h - 1)), 0));
        if (cq.a > 0.5) { code = cq; break; }
    }
}
float xl = round(code.r * 255.0);
// The ramp of the original pixel under this texel: from the ramp target where it holds the same
// part (the same translation), else the nearest texel that does (the ramp target covers whole
// original pixels, the colour the upscaled outline), else the colour's likeliest ramp (T_SprClass).
float rid = -1.0;
[unroll] for (int k0 = 0; k0 < 13; k0++)
{
    float4 r = Ramp.Load(int3(clamp(px + offs[k0], int2(0, 0), int2(w - 1, h - 1)), 0));
    if (r.a > 0.5 && abs(r.r * 255.0 - xl) < 0.5) { rid = round(r.b * 3.0); break; }
}
// Where the filter's 2x2 texels mix two parts (another translation) or two ramps, the blended
// colour sits on neither ramp and can recolour to any shade of it: use the exact texel there.
int2 p0 = int2(floor(UV * float2(w, h) - 0.5));
[unroll] for (int i = 0; i < 4; i++)
{
    int2 q = clamp(p0 + int2(i & 1, i >> 1), int2(0, 0), int2(w - 1, h - 1));
    if (Sprite.Load(int3(q, 0)).a > 0.5)
    {
        float4 qc = Code.Load(int3(q, 0));
        float4 qr = Ramp.Load(int3(q, 0));
        bool other_part = qc.a > 0.5 && abs(qc.r * 255.0 - xl) > 0.5;
        bool other_ramp = rid >= 0.0 && qr.a > 0.5 && abs(qr.r * 255.0 - xl) < 0.5 && abs(round(qr.b * 3.0) - rid) > 0.5;
        if (other_part || other_ramp)
        {
            rgb = Sprite.Load(int3(px, 0)).rgb;
            break;
        }
    }
}
if (xl > 0.5)
{
    // where the colour sits on its ramp (T_SprClass: 64^3 sRGB cells; R, G, B = red, blue, grey; A = likeliest ramp)
    float3 s = pow(saturate(rgb), 1.0 / 2.2);
    int3 q = min(int3(s * 64.0), int3(63, 63, 63));
    float4 k = Cls.Load(int3((q.b % 8) * 64 + q.r, (q.b / 8) * 64 + q.g, 0));
    // no ramp found, or the original pixel had none but the upscaler blended a ramp colour into it
    if (rid < 0.5) rid = round(k.a * 3.0);
    if (rid > 0.5)
    {
        float pos = rid < 1.5 ? k.r : (rid < 2.5 ? k.g : k.b);
        float base = rid < 1.5 ? 16.0 : (rid < 2.5 ? 144.0 : 208.0);
        rgb = Texture2DSampleLevel(Xlat, XlatSampler, float2((base + pos * 15.0 + 0.5) / 256.0, (xl + 0.5) / 256.0), 0).rgb;
    }
}
return float4(rgb, cover);
"""


CLASS_HLSL = """
uint w, h;
Code.GetDimensions(w, h);
float4 code = Code.Load(int3(min(int2(UV * float2(w, h)), int2(w - 1, h - 1)), 0));
int k = (int)round(code.g * 255.0);
float3 c = C0;
if (k == 1) c = C1; else if (k == 2) c = C2; else if (k == 3) c = C3;
else if (k == 4) c = C4; else if (k == 5) c = C5; else if (k == 6) c = C6; else if (k == 7) c = C7;
return c;
"""


def build_body_material(name, lit, classes):
    """The sprite body (see the module docstring). SwapUV / FlipU / FlipV map the quad's UVs."""
    mat = _new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT if lit
                            else unreal.MaterialShadingModel.MSM_UNLIT)
    uv = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    flip = {}
    for i, n in enumerate(("FlipU", "FlipV", "SwapUV")):
        flip[n] = _expr(mat, unreal.MaterialExpressionScalarParameter, -1600, 120 + i * 100, parameter_name=n, default_value=0.0)
    uvs = _custom(mat, -1350, 60, "Flip", unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                  "float2 uv = SW > 0.5 ? UV.yx : UV; return float2(lerp(uv.x, 1 - uv.x, FU), lerp(uv.y, 1 - uv.y, FV));",
                  {"UV": uv, "FU": flip["FlipU"], "FV": flip["FlipV"], "SW": flip["SwapUV"]})
    default = eal.load_asset("%s/T_SprClass" % DIR)
    tex = {}
    for i, (n, t) in enumerate((("Sprite", default), ("SpriteCode", default), ("SpriteRamp", default),
                                ("Xlat", eal.load_asset("%s/T_SprXlat" % DIR)), ("Cls", default))):
        tex[n] = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, -1350, 300 + i * 120, parameter_name=n, texture=t,
                       sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if n == "Xlat"
                       else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    colour = _custom(mat, -1000, 100, "Recolour", unreal.CustomMaterialOutputType.CMOT_FLOAT4, RECOLOUR_HLSL.strip(),
                     {"Sprite": tex["Sprite"], "Code": tex["SpriteCode"], "Ramp": tex["SpriteRamp"], "Xlat": tex["Xlat"],
                      "Cls": tex["Cls"], "UV": uvs})
    rgb = _expr(mat, unreal.MaterialExpressionComponentMask, -750, 50, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(colour, "", rgb, "")
    alpha = _expr(mat, unreal.MaterialExpressionComponentMask, -750, 150, r=False, g=False, b=False, a=True)
    mel.connect_material_expressions(colour, "", alpha, "")
    tint = _expr(mat, unreal.MaterialExpressionVectorParameter, -750, 250, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    col = _expr(mat, unreal.MaterialExpressionMultiply, -550, 50)
    mel.connect_material_expressions(rgb, "", col, "A")
    mel.connect_material_expressions(tint, "", col, "B")
    # "Opacity" < 1 dithers the body away (a logged-off player's ghost: the original's
    # DRAWFX_DITHERINVIS), as M_RuntimeRoom does for bitmap sprites
    opacity = _expr(mat, unreal.MaterialExpressionScalarParameter, -750, 380, parameter_name="Opacity", default_value=1.0)
    dither = _expr(mat, unreal.MaterialExpressionMaterialFunctionCall, -550, 380)
    dither.set_editor_property("material_function", eal.load_asset("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"))
    mel.connect_material_expressions(opacity, "", dither, "Alpha Threshold")
    mask = _expr(mat, unreal.MaterialExpressionMultiply, -350, 300)
    mel.connect_material_expressions(alpha, "", mask, "A")
    mel.connect_material_expressions(dither, "Result", mask, "B")
    mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    if lit:
        albedo = _expr(mat, unreal.MaterialExpressionScalarParameter, -550, 200, parameter_name="Albedo", default_value=0.8)
        base = _expr(mat, unreal.MaterialExpressionMultiply, -350, 50)
        mel.connect_material_expressions(col, "", base, "A")
        mel.connect_material_expressions(albedo, "", base, "B")
        mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        # surface classes: Class<k> = (roughness, metallic, specular)
        cls_in = {"Code": tex["SpriteCode"], "UV": uvs}
        for k in range(MAX_CLASSES):
            c = classes[k] if k < len(classes) else classes[0]
            cls_in["C%d" % k] = _expr(mat, unreal.MaterialExpressionVectorParameter, -1000, 600 + k * 90, parameter_name="Class%d" % k,
                                      default_value=unreal.LinearColor(c["roughness"], c["metallic"], c["specular"], 0))
        surf = _custom(mat, -700, 600, "SurfaceClass", unreal.CustomMaterialOutputType.CMOT_FLOAT3, CLASS_HLSL.strip(), cls_in)
        for ch, prop in (("R", unreal.MaterialProperty.MP_ROUGHNESS), ("G", unreal.MaterialProperty.MP_METALLIC),
                         ("B", unreal.MaterialProperty.MP_SPECULAR)):
            m = _expr(mat, unreal.MaterialExpressionComponentMask, -450, 600 + "RGB".index(ch) * 80,
                      r=ch == "R", g=ch == "G", b=ch == "B", a=False)
            mel.connect_material_expressions(surf, "", m, "")
            mel.connect_material_property(m, "", prop)
        # The shading normal. A sprite has no real 3D shape, so its brightness shouldn't swing as the
        # camera orbits it: it faces the sun's side (SunFace 1; 0 = the camera), then leans up by
        # NormalUp (1 = straight up). SunDir: the direction the sun's light travels (set per frame).
        mat.set_editor_property("tangent_space_normal", False)
        cam = _expr(mat, unreal.MaterialExpressionCameraVectorWS, -700, 1400)
        up_w = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 1500, parameter_name="NormalUp", default_value=0.4)
        sun_face = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 1560, parameter_name="SunFace", default_value=0.6)
        sun_n = _expr(mat, unreal.MaterialExpressionVectorParameter, -900, 1500, parameter_name="SunDir",
                      default_value=unreal.LinearColor(0, 0, -1, 0))
        normal = _custom(mat, -450, 1400, "Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                         "float3 l = float3(-S.x, -S.y, 0); l = dot(l, l) > 1e-4 ? normalize(l) : C;\n"
                         "float3 n = normalize(lerp(C, l, F));\n"
                         "return normalize(lerp(n, float3(0, 0, 1), W));", {"C": cam, "S": sun_n, "F": sun_face, "W": up_w})
        mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
        # indoors: the environment's ambient floor (0 outdoors), as the zone materials add it
        emissive = None
        if eal.does_asset_exist(MPC):
            coll = eal.load_asset(MPC)
            amb = _expr(mat, unreal.MaterialExpressionCollectionParameter, -700, 1650, collection=coll, parameter_name="SectorAmbient")
            atint = _expr(mat, unreal.MaterialExpressionCollectionParameter, -700, 1750, collection=coll, parameter_name="AmbientTint")
            k = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 1850, parameter_name="SpriteAmbient", default_value=0.6)
            emissive = _custom(mat, -450, 1700, "AmbientFloor", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                               "return C * A * T.rgb * K;", {"C": col, "A": amb, "T": atint, "K": k})
        else:
            log("WARNING: %s missing (build the world first): no ambient floor" % MPC)
        if emissive:
            mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        bright = _expr(mat, unreal.MaterialExpressionScalarParameter, -550, 200, parameter_name="Brightness", default_value=1.0)
        em = _expr(mat, unreal.MaterialExpressionMultiply, -350, 100)
        mel.connect_material_expressions(col, "", em, "A")
        mel.connect_material_expressions(bright, "", em, "B")
        mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Shadow pass only: push the sun-facing shadow card along the light (SunDir, the direction the
    # light travels) so it no longer stands between the sun and the sprite itself. Each point moves
    # along its own light ray, so the shadow on the ground doesn't change; points near the feet move
    # less (they would go under the ground). FeetZ: world height of the feet.
    # (the Shadow Pass Switch node isn't scriptable; the shadow depth shaders define SHADOW_DEPTH_SHADER)
    wp = _expr(mat, unreal.MaterialExpressionWorldPosition, -700, 2000)
    sun = _expr(mat, unreal.MaterialExpressionVectorParameter, -700, 2100, parameter_name="SunDir",
                default_value=unreal.LinearColor(0, 0, -1, 0))
    feet = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 2200, parameter_name="FeetZ", default_value=0.0)
    max_push = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 2300, parameter_name="MaxPush", default_value=120.0)
    push = _custom(mat, -450, 2100, "ShadowPush", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   "#if SHADOW_DEPTH_SHADER\n"
                   "float3 d = normalize(S.xyz); float h = max(P.z - F, 0);\n"
                   "return d * min(M, 0.9 * h / max(-d.z, 0.1));\n"
                   "#else\nreturn 0;\n#endif", {"P": wp, "S": sun, "F": feet, "M": max_push})
    mel.connect_material_property(push, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("built " + name)


def build_backdrop_material():
    """M_PreviewBackdrop: an unlit flat colour (Color) behind the character creator's previews."""
    mat = _new_material("M_PreviewBackdrop")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    col = _expr(mat, unreal.MaterialExpressionVectorParameter, -400, 0, parameter_name="Color",
                default_value=unreal.LinearColor(0.33, 0.33, 0.33, 1))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("built M_PreviewBackdrop")


def main():
    layout = json.load(open(LAYOUT, encoding="utf-8"))
    import_luts()
    import_atlases(layout)
    classes = layout.get("material_classes") or [{"roughness": 0.85, "metallic": 0.0, "specular": 0.2}]
    build_body_material("M_SpriteBody", True, classes)
    build_body_material("M_SpriteBodyUnlit", False, classes)
    build_backdrop_material()
    log("done")


main()
