"""
Build a MakeHuman (MPFB) character kit on the exact UE5 mannequin skeleton. Headless Blender:

    blender -b -P tools/blender/mpfb_character.py -- --settings tools/blender/characters/mpfb_male.json

Needs: the MPFB extension and its asset packs (tools/blender/install_mpfb_packs.py), and
build/mannequin/SKM_{Manny,Quinn}_Simple.fbx (tools/ue/export_mannequin.py).

Output, art_src/characters/<name>/ (CC0, committed):
    body.fbx              body + eyes + eyebrows + eyelashes in ONE skinned mesh on the mannequin
                          skeleton, with the head-slider morph targets; material slots by role
                          (MR_Skin, MR_Eyes, MR_Brows, MR_Lashes)
    hair_<style>.fbx      each hairstyle as a separate mesh on the same skeleton, carrying the same
                          morph targets so it follows the head shape (slot MR_Hair)
    textures/*.png        the textures those slots use
    manifest.json         what tools/ue/import_character.py needs: files, slot textures, sliders

Character model (docs/characters.md): one shared body mesh per gender; faces differ only by
morph-target weights (head sliders) and material parameters; hair is a swappable part.

Steps:
  1. MPFB human from macro settings + MPFB "game_engine" rig and weights; eyes, eyebrows,
     eyelashes and hairstyles fitted as MPFB proxies (mhclo).
  2. Head sliders: every MPFB face target used by a slider is loaded as a shape key at 0.
  3. Fit: each rig bone is constrained onto its mannequin joint (see fit_rig_to_manny).
  4. For the basis and for each slider end, the targets are set, the proxies are refitted to the
     new head shape, and the fitted (armature-deformed) vertex positions of every object are
     recorded. That makes the morphs consistent for skin, eyes, brows, lashes and hair.
  5. Bake everything at zero, apply the fit, add the recorded morphs as shape keys, move the
     weights to UE5 bones, join body + eyes + brows + lashes, bind to the mannequin, export.
"""
import json
import os
import shutil
import sys

import addon_utils
import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SETTINGS = arg("--settings", os.path.join(REPO, "tools", "blender", "characters", "mpfb_male.json"))
MANNEQUIN_DIR = os.path.join(REPO, "build", "mannequin")

# MPFB game_engine (UE4 mannequin names) -> UE5 mannequin, for *placing* bones.  Names match
# except the root; the UE4 spine is three bones where UE5 has five, so the spine is mapped by
# position (MPFB spine_01/02/03 onto UE5 spine_01/02/03, the chest bone stretching up to the neck)
# and its weights are spread over all five UE5 spine bones by height (see spread_chain_weights).
UE4_TO_UE5 = {"Root": "root"}

# Weight chains spread by vertex height: MPFB groups -> UE5 bones from bottom to top
# (the last entry is only the upper end of the chain, not a weight target).
SPINE_CHAIN = (["spine_01", "spine_02", "spine_03"],
               ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01"])
NECK_CHAIN = (["neck_01"], ["neck_01", "neck_02", "head"])

# Bones with several children: aim at this child's mannequin joint (rotate, no stretch) ...
PRIMARY_CHILD = {"pelvis": "spine_01", "hand_l": "middle_01_l", "hand_r": "middle_01_r"}
# ... or stretch to it: the UE4 chest bone spans UE5 spine_03..05, so it reaches the neck.
STRETCH_TO_CHILD = {"spine_03": "neck_01"}


def _sym(t):
    return ["l-" + t, "r-" + t]


# Head sliders: (name, MPFB targets at the "decrease" end, at the "increase" end).  Each end
# becomes one morph target ("<name>_decr" / "<name>_incr"; one-ended sliders just "<name>").
# Left/right targets are applied together so faces stay symmetric.
HEAD_SLIDERS = [
    ("head_oval", [], ["head-oval"]),
    ("head_round", [], ["head-round"]),
    ("head_square", [], ["head-square"]),
    ("head_triangular", [], ["head-triangular"]),
    ("head_width", ["head-scale-horiz-decr"], ["head-scale-horiz-incr"]),
    ("head_height", ["head-scale-vert-decr"], ["head-scale-vert-incr"]),
    ("head_fat", ["head-fat-decr"], ["head-fat-incr"]),
    ("head_age", ["head-age-decr"], ["head-age-incr"]),
    ("forehead_height", ["forehead-scale-vert-decr"], ["forehead-scale-vert-incr"]),
    ("forehead_slope", ["forehead-trans-backward"], ["forehead-trans-forward"]),
    ("forehead_temples", ["forehead-temple-decr"], ["forehead-temple-incr"]),
    ("brow_height", ["eyebrows-trans-down"], ["eyebrows-trans-up"]),
    ("brow_angle", ["eyebrows-angle-down"], ["eyebrows-angle-up"]),
    ("eye_size", _sym("eye-scale-decr"), _sym("eye-scale-incr")),
    ("eye_spacing", _sym("eye-trans-in"), _sym("eye-trans-out")),
    ("eye_height", _sym("eye-trans-down"), _sym("eye-trans-up")),
    ("eye_openness", _sym("eye-height2-decr"), _sym("eye-height2-incr")),
    ("eye_epicanthus", _sym("eye-epicanthus-in"), _sym("eye-epicanthus-out")),
    ("nose_width", ["nose-scale-horiz-decr"], ["nose-scale-horiz-incr"]),
    ("nose_length", ["nose-scale-vert-decr"], ["nose-scale-vert-incr"]),
    ("nose_depth", ["nose-scale-depth-decr"], ["nose-scale-depth-incr"]),
    ("nose_height", ["nose-trans-down"], ["nose-trans-up"]),
    ("nose_tip", ["nose-point-down"], ["nose-point-up"]),
    ("nose_tip_width", ["nose-point-width-decr"], ["nose-point-width-incr"]),
    ("nose_hump", ["nose-hump-decr"], ["nose-hump-incr"]),
    ("nose_curve", ["nose-curve-concave"], ["nose-curve-convex"]),
    ("nose_nostrils", ["nose-nostrils-width-decr"], ["nose-nostrils-width-incr"]),
    ("nose_flare", ["nose-flaring-decr"], ["nose-flaring-incr"]),
    ("mouth_width", ["mouth-scale-horiz-decr"], ["mouth-scale-horiz-incr"]),
    ("mouth_height", ["mouth-trans-down"], ["mouth-trans-up"]),
    ("mouth_depth", ["mouth-trans-backward"], ["mouth-trans-forward"]),
    ("lip_upper", ["mouth-upperlip-volume-decr"], ["mouth-upperlip-volume-incr"]),
    ("lip_lower", ["mouth-lowerlip-volume-decr"], ["mouth-lowerlip-volume-incr"]),
    ("mouth_corners", ["mouth-angles-down"], ["mouth-angles-up"]),
    ("mouth_cupidsbow", ["mouth-cupidsbow-decr"], ["mouth-cupidsbow-incr"]),
    ("chin_width", ["chin-width-decr"], ["chin-width-incr"]),
    ("chin_height", ["chin-height-decr"], ["chin-height-incr"]),
    ("chin_prominence", ["chin-prominent-decr"], ["chin-prominent-incr"]),
    ("jaw_drop", ["chin-jaw-drop-decr"], ["chin-jaw-drop-incr"]),
    ("jaw_width", ["chin-bones-decr"], ["chin-bones-incr"]),
    ("chin_cleft", ["chin-cleft-decr"], ["chin-cleft-incr"]),
    ("cheek_bones", _sym("cheek-bones-decr"), _sym("cheek-bones-incr")),
    ("cheek_volume", _sym("cheek-volume-decr"), _sym("cheek-volume-incr")),
    ("cheek_inner", _sym("cheek-inner-decr"), _sym("cheek-inner-incr")),
    ("cheek_height", _sym("cheek-trans-down"), _sym("cheek-trans-up")),
    ("ear_size", _sym("ear-scale-decr"), _sym("ear-scale-incr")),
    ("ear_pointed", [], _sym("ear-shape-pointed")),
    ("ear_angle", _sym("ear-rot-backward"), _sym("ear-rot-forward")),
    ("ear_flap", _sym("ear-flap-decr"), _sym("ear-flap-incr")),
]

DEFAULT_SETTINGS = {
    "name": "MPFB_Male",
    "macro": {"gender": 1.0, "age": 0.5, "muscle": 0.6, "weight": 0.5, "proportions": 0.6, "height": 0.5,
              "cupsize": 0.5, "firmness": 0.5, "race": {"asian": 0.33, "caucasian": 0.34, "african": 0.33}},
    "rig": "game_engine",
    "fit_to": "SKM_Manny_Simple",  # mannequin whose joints the body is fitted to (Quinn for female)
    "skin": "young_caucasian_male",
    "eyes": "low-poly",
    "eye_material": "brown",
    "eyebrows": "eyebrow001",
    "eyelashes": "eyelashes01",
    "hair": ["short02"],
}


def log(msg):
    print("[mpfb_character] " + msg, flush=True)


def load_settings():
    s = json.loads(json.dumps(DEFAULT_SETTINGS))
    if SETTINGS and os.path.exists(SETTINGS):
        with open(SETTINGS, encoding="utf-8") as f:
            s.update(json.load(f))
        log("settings: " + SETTINGS)
    return s


# ------------------------------------------------------------------------------------ MPFB

class Mpfb:
    def __init__(self):
        self.pkg = None
        for mod in addon_utils.modules():
            if mod.__name__.endswith(".mpfb") or mod.__name__ == "mpfb":
                addon_utils.enable(mod.__name__, default_set=True)
                self.pkg = mod.__name__
        if not self.pkg:
            raise RuntimeError("MPFB extension not found (Blender -> Preferences -> Get Extensions -> MPFB)")

        def svc(n):
            return sys.modules[self.pkg + ".services." + n]

        self.human = svc("humanservice").HumanService
        self.targets = svc("targetservice").TargetService
        self.clothes = svc("clothesservice").ClothesService
        self.location = svc("locationservice").LocationService
        self._target_index = None

    def asset_path(self, kind, name, ext):
        """<user data>/<kind>/<name>/<name>.<ext>, falling back to the extension's own data."""
        for root in (self.location.get_user_data(kind), self.location.get_mpfb_data(kind)):
            p = os.path.join(root, name, name + "." + ext)
            if os.path.exists(p):
                return p
        raise FileNotFoundError("%s %s.%s not found (install the MakeHuman asset packs)" % (kind, name, ext))

    def target_path(self, name):
        if self._target_index is None:
            self._target_index = {}
            for root in (self.location.get_mpfb_data("targets"), self.location.get_user_data("targets")):
                for dirpath, _, files in os.walk(root):
                    for f in files:
                        for suffix in (".target.gz", ".target"):
                            if f.endswith(suffix):
                                self._target_index.setdefault(f[:-len(suffix)], os.path.join(dirpath, f))
        if name not in self._target_index:
            raise FileNotFoundError("MPFB target not found: " + name)
        return self._target_index[name]


def mhmat_texture(mhmat_path, key="diffuseTexture"):
    """Absolute path of a texture named in an .mhmat file, or None."""
    with open(mhmat_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.strip().split(None, 1)
            if len(parts) == 2 and parts[0] == key:
                p = os.path.join(os.path.dirname(mhmat_path), parts[1].strip())
                return p if os.path.exists(p) else None
    return None


# ----------------------------------------------------------------------------- skeleton fit

def import_manny(fit_to):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=os.path.join(MANNEQUIN_DIR, fit_to + ".fbx"))
    new = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in new if o.type == "ARMATURE")
    for o in new:
        if o.type == "MESH":
            bpy.data.objects.remove(o, do_unlink=True)  # keep only the skeleton
    return arm


def manny_targets(manny):
    """World-space head/tail of every mannequin bone."""
    mw = manny.matrix_world
    return {b.name: (mw @ b.head_local, mw @ b.tail_local) for b in manny.data.bones}


def fit_rig_to_manny(rig, joints):
    """Pose the MPFB rig so each bone sits on its mannequin counterpart.

    - every mapped bone: COPY_LOCATION onto the mannequin joint (bone head)
    - single-child bones (limb, spine, neck and finger segments): STRETCH_TO the next joint, so
      chains line up end to end and bone lengths match the mannequin
    - branch bones (pelvis, hands): DAMPED_TRACK toward their primary child only
    - leaf bones (head, fingertips, balls): position only, orientation inherited
    FBX has no bone tails (Blender guesses them on import), so tails are never used as targets.
    """
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="POSE")
    empties = []

    def empty_at(name, loc):
        e = bpy.data.objects.new(name, None)
        e.location = loc
        bpy.context.scene.collection.objects.link(e)
        empties.append(e)
        return e

    stretched = aimed = placed = 0
    for pb in rig.pose.bones:
        target = UE4_TO_UE5.get(pb.name, pb.name)
        if target not in joints or target == "root":
            continue
        c = pb.constraints.new("COPY_LOCATION")
        c.target = empty_at("fit_h_" + pb.name, joints[target][0])

        mapped_children = [ch for ch in pb.bone.children if UE4_TO_UE5.get(ch.name, ch.name) in joints]
        if pb.name in STRETCH_TO_CHILD:
            c = pb.constraints.new("STRETCH_TO")
            c.target = empty_at("fit_t_" + pb.name, joints[STRETCH_TO_CHILD[pb.name]][0])
            c.volume = "NO_VOLUME"
            stretched += 1
        elif len(mapped_children) == 1 and pb.name not in PRIMARY_CHILD:
            child = UE4_TO_UE5.get(mapped_children[0].name, mapped_children[0].name)
            c = pb.constraints.new("STRETCH_TO")
            c.target = empty_at("fit_t_" + pb.name, joints[child][0])
            c.volume = "NO_VOLUME"
            stretched += 1
        elif pb.name in PRIMARY_CHILD and PRIMARY_CHILD[pb.name] in joints:
            c = pb.constraints.new("DAMPED_TRACK")
            c.target = empty_at("fit_t_" + pb.name, joints[PRIMARY_CHILD[pb.name]][0])
            aimed += 1
        else:
            placed += 1
    bpy.context.view_layer.update()
    log("fit: %d bones stretched along chains, %d aimed, %d placed" % (stretched, aimed, placed))
    bpy.ops.object.mode_set(mode="OBJECT")
    return empties


# ---------------------------------------------------------------------------------- morphs

def evaluated_coords(obj):
    """World-space vertex positions after modifiers (fit armature, helper mask)."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    mesh = ev.to_mesh()
    mw = obj.matrix_world
    coords = [mw @ v.co for v in mesh.vertices]
    ev.to_mesh_clear()
    return coords


def set_targets(human, names, value):
    keys = human.data.shape_keys.key_blocks
    for n in names:
        keys[n].value = value


def record_morphs(mpfb, human, proxies, morph_ends):
    """Basis positions and {object name: {morph: positions}} for every morph that moves it."""
    objects = [human] + proxies

    def refit():
        for p in proxies:
            mpfb.clothes.fit_clothes_to_human(p, human)
        bpy.context.view_layer.update()

    refit()
    basis = {o.name: evaluated_coords(o) for o in objects}
    morphs = {o.name: {} for o in objects}
    for i, (morph, names) in enumerate(morph_ends):
        set_targets(human, names, 1.0)
        refit()
        for o in objects:
            coords = evaluated_coords(o)
            moved = max((a - b).length for a, b in zip(coords, basis[o.name]))
            if moved > 1e-5:
                morphs[o.name][morph] = coords
        set_targets(human, names, 0.0)
        if (i + 1) % 20 == 0:
            log("  morphs recorded: %d/%d" % (i + 1, len(morph_ends)))
    refit()
    return basis, morphs


def add_shape_keys(obj, basis_world, morphs):
    """Add the recorded morphs to obj (its current mesh must equal basis_world)."""
    if len(obj.data.vertices) != len(basis_world):
        raise RuntimeError("%s: %d verts but %d recorded" % (obj.name, len(obj.data.vertices), len(basis_world)))
    inv = obj.matrix_world.inverted()
    obj.shape_key_add(name="Basis", from_mix=False)
    for morph, coords in morphs.items():
        key = obj.shape_key_add(name=morph, from_mix=False)
        for v, co in zip(key.data, coords):
            v.co = inv @ co


# --------------------------------------------------------------------------------- weights

def spread_chain_weights(obj, joints, chain):
    """Move the weights of the MPFB groups in chain[0] onto the UE5 bones in chain[1], splitting
    each vertex's weight between the two UE5 joints it sits between (by height)."""
    src_names, dst = chain
    src = [obj.vertex_groups.get(n) for n in src_names]
    src = [g for g in src if g is not None]
    if not src:
        return
    src_idx = {g.index for g in src}
    heights = [joints[b][0].z for b in dst]
    mw = obj.matrix_world
    moves = []
    for v in obj.data.vertices:
        w = sum(g.weight for g in v.groups if g.group in src_idx)
        if w <= 0.0:
            continue
        z = (mw @ v.co).z
        if z <= heights[0]:
            moves.append((v.index, [(dst[0], w)]))
            continue
        for i in range(len(dst) - 1):
            lo, hi = heights[i], heights[i + 1]
            if z <= hi or i == len(dst) - 2:
                t = 0.0 if hi <= lo else min(max((z - lo) / (hi - lo), 0.0), 1.0)
                upper = dst[i + 1] if i + 1 < len(dst) - 1 else dst[i]  # top end isn't a target
                moves.append((v.index, [(dst[i], w * (1 - t)), (upper, w * t)]))
                break
    for g in src:
        obj.vertex_groups.remove(g)
    targets = {b: obj.vertex_groups.get(b) or obj.vertex_groups.new(name=b) for b in dst[:-1]}
    for idx, parts in moves:
        for bone, w in parts:
            if w > 1e-4:
                targets[bone].add([idx], w, "ADD")


def remap_weights(obj, manny):
    bones = {b.name for b in manny.data.bones}
    for g in list(obj.vertex_groups):
        new = UE4_TO_UE5.get(g.name, g.name)
        if new in bones:
            g.name = new
        else:
            obj.vertex_groups.remove(g)


def set_single_material(obj, name):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for poly in obj.data.polygons:
        poly.material_index = 0


def apply_modifiers(obj, types):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    for m in list(obj.modifiers):
        if m.type in types:
            bpy.ops.object.modifier_apply(modifier=m.name)
    for m in list(obj.modifiers):  # anything else (e.g. subdivision) is dropped
        obj.modifiers.remove(m)


def unparent_keep_transform(obj):
    mw = obj.matrix_world.copy()
    obj.parent = None
    obj.matrix_world = mw


def bind(obj, manny, name):
    mw = obj.matrix_world.copy()
    obj.parent = manny
    obj.matrix_world = mw
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = manny
    obj.name = obj.data.name = name


def export_fbx(path, manny, obj):
    bpy.ops.object.select_all(action="DESELECT")
    manny.select_set(True)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = manny
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"},
        add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X",
        use_armature_deform_only=False, bake_anim=False, mesh_smooth_type="FACE",
        use_mesh_modifiers=False, apply_scale_options="FBX_SCALE_NONE")


# ------------------------------------------------------------------------------------ main

def main():
    s = load_settings()
    out_dir = os.path.abspath(arg("--out", os.path.join(REPO, "art_src", "characters", s["name"])))
    os.makedirs(os.path.join(out_dir, "textures"), exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    mpfb = Mpfb()
    manny = import_manny(s["fit_to"])
    joints = manny_targets(manny)

    human = mpfb.human.create_human(macro_detail_dict=s["macro"])
    rig = mpfb.human.add_builtin_rig(human, s["rig"], import_weights=True)
    log("MPFB human: %d verts, %.3f m tall" % (len(human.data.vertices), human.dimensions.z))

    # proxies that become part of the body mesh, and hairstyles that stay separate
    def add(kind, name):
        o = mpfb.human.add_mhclo_asset(mpfb.asset_path(kind, name, "mhclo"), human, asset_type=kind,
                                       subdiv_levels=0, set_up_rigging=True, interpolate_weights=True)
        o.name = "%s_%s" % (kind, name)
        return o

    face_parts = {"MR_Eyes": add("eyes", s["eyes"]), "MR_Brows": add("eyebrows", s["eyebrows"]),
                  "MR_Lashes": add("eyelashes", s["eyelashes"])}
    hairs = {h: add("hair", h) for h in s["hair"]}

    # head sliders -> morph ends; load every target they use, at 0
    morph_ends = []
    for name, decr, incr in HEAD_SLIDERS:
        if decr:
            morph_ends.append((name + "_decr", decr))
        if incr:
            morph_ends.append((name + "_incr" if decr else name, incr))
    for t in sorted({t for _, names in morph_ends for t in names}):
        mpfb.targets.load_target(human, mpfb.target_path(t), weight=0.0, name=t)
    log("%d head sliders -> %d morph targets" % (len(HEAD_SLIDERS), len(morph_ends)))

    empties = fit_rig_to_manny(rig, joints)
    proxies = list(face_parts.values()) + list(hairs.values())
    basis, morphs = record_morphs(mpfb, human, proxies, morph_ends)

    # bake at zero: macros + face targets into the mesh, then the fit and the helper mask
    mpfb.targets.bake_targets(human)
    apply_modifiers(human, {"ARMATURE", "MASK"})
    for p in proxies:
        if p.data.shape_keys:
            bpy.context.view_layer.objects.active = p
            bpy.ops.object.shape_key_remove(all=True)
        apply_modifiers(p, {"ARMATURE"})
    for e in empties:
        bpy.data.objects.remove(e, do_unlink=True)
    for obj in [human] + proxies:
        unparent_keep_transform(obj)
    bpy.data.objects.remove(rig, do_unlink=True)

    for obj in [human] + proxies:
        add_shape_keys(obj, basis[obj.name], morphs[obj.name])
        spread_chain_weights(obj, joints, SPINE_CHAIN)
        spread_chain_weights(obj, joints, NECK_CHAIN)
        remap_weights(obj, manny)

    set_single_material(human, "MR_Skin")
    for slot, obj in face_parts.items():
        set_single_material(obj, slot)
    for obj in hairs.values():
        set_single_material(obj, "MR_Hair")

    # join eyes / brows / lashes into the body (shape keys merge by name)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in face_parts.values():
        obj.select_set(True)
    human.select_set(True)
    bpy.context.view_layer.objects.active = human
    bpy.ops.object.join()
    bind(human, manny, "SKM_" + s["name"])
    log("body: %d verts, %d morph targets" % (len(human.data.vertices), len(human.data.shape_keys.key_blocks) - 1))

    files = {"body": "body.fbx", "hair": []}
    export_fbx(os.path.join(out_dir, "body.fbx"), manny, human)
    hair_mhmats = {}
    for style, obj in hairs.items():
        bind(obj, manny, "SKM_%s_Hair_%s" % (s["name"], style))
        fn = "hair_%s.fbx" % style
        export_fbx(os.path.join(out_dir, fn), manny, obj)
        keys = obj.data.shape_keys
        log("hair %s: %d verts, %d morph targets" % (style, len(obj.data.vertices), (len(keys.key_blocks) - 1) if keys else 0))
        files["hair"].append({"style": style, "fbx": fn})
        hair_mhmats[style] = mpfb.asset_path("hair", style, "mhmat")

    # textures
    def copy_tex(src, role):
        if not src:
            return None
        dst = "%s_%s" % (role, os.path.basename(src))
        shutil.copyfile(src, os.path.join(out_dir, "textures", dst))
        return "textures/" + dst

    eye_mhmat = os.path.join(mpfb.location.get_user_data("eyes"), "materials", s["eye_material"] + ".mhmat")
    materials = {
        "MR_Skin": {"texture": copy_tex(mhmat_texture(mpfb.asset_path("skins", s["skin"], "mhmat")), "skin")},
        "MR_Eyes": {"texture": copy_tex(mhmat_texture(eye_mhmat), "eyes")},
        "MR_Brows": {"texture": copy_tex(mhmat_texture(mpfb.asset_path("eyebrows", s["eyebrows"], "mhmat")), "brows"),
                     "masked": True},
        "MR_Lashes": {"texture": copy_tex(mhmat_texture(mpfb.asset_path("eyelashes", s["eyelashes"], "mhmat")), "lashes"),
                      "masked": True},
    }
    for h in files["hair"]:
        mhmat = hair_mhmats[h["style"]]
        h["texture"] = copy_tex(mhmat_texture(mhmat), "hair")
        h["normal"] = copy_tex(mhmat_texture(mhmat, "normalmapTexture"), "hair")

    manifest = {
        "name": s["name"], "settings": s, "mpfb": mpfb.pkg, "skeleton": "SK_Mannequin",
        "files": files, "materials": materials,
        "sliders": [{"name": n,
                     "decr": (n + "_decr") if d else None,
                     "incr": ((n + "_incr") if d else n) if i else None}
                    for n, d, i in HEAD_SLIDERS],
    }
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    log("wrote " + out_dir)


try:
    main()
except Exception as e:  # noqa: BLE001
    import traceback
    traceback.print_exc()
    log("FAILED: %s" % e)
    sys.exit(1)
