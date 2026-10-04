"""
Retarget the Quaternius Universal Animation Library (CC0) onto the UE5 mannequin skeleton. Headless:

    blender -b -P tools/blender/retarget_ual.py -- [--only Walk_Loop,Sword_Attack] [--preview build/previews/anims]

Inputs:
    UnrealAssets/quaternius/Universal Animation Library[Standard]/Unreal-Godot/UAL1_Standard.glb
    UnrealAssets/quaternius/Universal Animation Library 2[Standard]/Unreal-Godot/UAL2_Standard.glb
      (free download from quaternius.itch.io; CC0, so the retargeted output is committed)
    build/mannequin/SKM_Manny_Simple.fbx  (tools/ue/export_mannequin.py)

Output: art_src/animations/quaternius/A_<clip>.fbx, one in-place clip per file on the mannequin
armature (30 fps), plus clips.json; tools/ue/import_animations.py brings them into UE.

The UAL rig uses UE4 mannequin bone names (spine_01..03, neck_01, Head) in a T-pose; the UE5
mannequin has spine_01..05, neck_01..02 and an A-pose rest. The retarget is done on world-space
rotations:
  1. "matched pose": the mannequin is posed to the UAL rest pose by swinging each limb/finger
     bone onto the direction of the matching UAL bone (hands also match the palm plane).
  2. each frame, every mapped bone gets the UAL bone's world rotation delta from its rest
     applied on top of the matched pose; UE5 spine/neck bones between two UAL bones take a
     slerp of both deltas; unmapped bones (twist, metacarpal, ik) follow their parent;
     the pelvis translation is scaled by the hip-height ratio; ik_* bones copy their FK bones.
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
UAL_DIR = os.path.join(REPO, "UnrealAssets", "quaternius")
LIBRARIES = [
    os.path.join(UAL_DIR, "Universal Animation Library[Standard]", "Unreal-Godot", "UAL1_Standard.glb"),
    os.path.join(UAL_DIR, "Universal Animation Library 2[Standard]", "Unreal-Godot", "UAL2_Standard.glb"),
]
MANNY = os.path.join(REPO, "build", "mannequin", "SKM_Manny_Simple.fbx")
OUT_DIR = os.path.abspath(arg("--out", os.path.join(REPO, "art_src", "animations", "quaternius")))
FPS = 30

# clips not worth carrying (modern props / reference pose)
SKIP_PREFIXES = ("A_TPose", "Pistol_", "Driving_", "Idle_TalkingPhone")

# UE5 bone -> (UAL bone) or (UAL bone a, UAL bone b, t): world rotation delta, slerped for spans
SPINE_MAP = {
    "root": ("root",), "pelvis": ("pelvis",),
    "spine_01": ("spine_01",), "spine_02": ("spine_01", "spine_02", 0.5), "spine_03": ("spine_02",),
    "spine_04": ("spine_02", "spine_03", 0.5), "spine_05": ("spine_03",),
    "neck_01": ("neck_01",), "neck_02": ("neck_01", "Head", 0.5), "head": ("Head",),
}
LIMB_BONES = ["clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball"]
FINGERS = ["thumb", "index", "middle", "ring", "pinky"]

# bones swung onto the UAL bone direction for the matched pose: bone -> next joint (same names)
ALIGN = {}
for s in ("l", "r"):
    ALIGN.update({f"upperarm_{s}": f"lowerarm_{s}", f"lowerarm_{s}": f"hand_{s}",
                  f"thigh_{s}": f"calf_{s}", f"calf_{s}": f"foot_{s}", f"foot_{s}": f"ball_{s}"})
    for f in FINGERS:
        ALIGN.update({f"{f}_01_{s}": f"{f}_02_{s}", f"{f}_02_{s}": f"{f}_03_{s}"})

IK_COPY = {"ik_foot_l": "foot_l", "ik_foot_r": "foot_r", "ik_hand_gun": "hand_r",
           "ik_hand_l": "hand_l", "ik_hand_r": "hand_r"}


def log(msg):
    print("[retarget_ual] " + msg, flush=True)


def bone_map():
    m = dict(SPINE_MAP)
    for s in ("l", "r"):
        for b in LIMB_BONES:
            m[f"{b}_{s}"] = (f"{b}_{s}",)
        for f in FINGERS:
            for i in (1, 2, 3):
                m[f"{f}_0{i}_{s}"] = (f"{f}_0{i}_{s}",)
    return m


def rot3(m):
    """Rotation part of a matrix with scale removed."""
    return m.to_3x3().normalized()


def frame_from(primary, secondary):
    """Orthonormal basis (as a matrix) from a primary direction and a secondary in-plane vector."""
    x = primary.normalized()
    z = x.cross(secondary).normalized()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed()


class Source:
    def __init__(self, arm):
        self.arm = arm
        self.rest = {b.name: rot3(arm.matrix_world @ b.matrix_local) for b in arm.data.bones}
        self.rest_pos = {b.name: arm.matrix_world @ b.head_local for b in arm.data.bones}

    def joint(self, name):
        return self.rest_pos[name]

    def sample(self):
        """World rotation delta from rest and world position of every bone at the current frame."""
        mw = self.arm.matrix_world
        deltas, pos = {}, {}
        for pb in self.arm.pose.bones:
            m = mw @ pb.matrix
            deltas[pb.name] = (rot3(m) @ self.rest[pb.name].inverted()).to_quaternion()
            pos[pb.name] = m.translation.copy()
        return deltas, pos


class Target:
    def __init__(self, arm, src, mapping):
        self.arm = arm
        self.map = mapping
        bones = arm.data.bones
        self.order = [b.name for b in bones if not b.parent]
        i = 0
        while i < len(self.order):
            self.order += [c.name for c in bones[self.order[i]].children]
            i += 1
        self.rest = {b.name: b.matrix_local.copy() for b in bones}  # armature space
        self.w2a = rot3(arm.matrix_world).inverted()  # world -> armature rotation
        self.a2w = self.w2a.inverted()
        self.mw_inv = arm.matrix_world.inverted()
        self.matched = self.matched_pose(src)
        tgt_hip = (arm.matrix_world @ bones["pelvis"].head_local).z
        self.hip_scale = tgt_hip / src.joint("pelvis").z
        self.pelvis_rest_world = arm.matrix_world @ bones["pelvis"].head_local
        log("hip scale %.3f" % self.hip_scale)

    def to_arm(self, v):
        return self.w2a @ v

    def matched_pose(self, src):
        """Armature-space rotation of every bone with the mannequin posed like the UAL rest."""
        bones = self.arm.data.bones
        delta = {}  # accumulated armature-space rotation delta from rest
        matched = {}
        for name in self.order:
            b = bones[name]
            d = delta[b.parent.name].copy() if b.parent else Matrix.Identity(3)
            rest3 = self.rest[name].to_3x3()
            head = self.rest[name].translation
            if name in ALIGN and ALIGN[name] in bones:
                nxt = self.rest[ALIGN[name]].translation
                now = d @ (nxt - head)
                want = self.to_arm(src.joint(ALIGN[name]) - src.joint(name))
                d = now.rotation_difference(want).to_matrix() @ d
            elif name.startswith("hand_"):
                s = name[-1]
                mid, idx, pky = (self.rest[f"{f}_01_{s}"].translation for f in ("middle", "index", "pinky"))
                now = frame_from(d @ (mid - head), d @ (pky - idx))
                want = frame_from(self.to_arm(src.joint(f"middle_01_{s}") - src.joint(name)),
                                  self.to_arm(src.joint(f"pinky_01_{s}") - src.joint(f"index_01_{s}")))
                d = want @ now.inverted() @ d
            delta[name] = d
            matched[name] = (d @ rest3).normalized()
        return matched

    def pose(self, deltas, src_pos, src):
        """Armature-space 4x4 pose matrix of every mannequin bone for one sampled UAL frame."""
        bones = self.arm.data.bones
        out = {}
        for name in self.order:
            b = bones[name]
            if b.parent:
                parent_pose = out[b.parent.name]
                follow = parent_pose @ self.rest[b.parent.name].inverted() @ self.rest[name]
            else:
                follow = self.rest[name].copy()
            spec = self.map.get(name)
            if spec is None:
                out[name] = follow
                continue
            q = deltas[spec[0]] if len(spec) == 1 else deltas[spec[0]].slerp(deltas[spec[1]], spec[2])
            qa = (self.w2a @ q.to_matrix() @ self.a2w)
            r = qa @ self.matched[name]
            m = r.to_4x4()
            if name == "pelvis":
                off = (src_pos["pelvis"] - src.joint("pelvis")) * self.hip_scale
                m.translation = self.mw_inv @ (self.pelvis_rest_world + off)
            else:
                m.translation = follow.translation
            out[name] = m
        for ik, fk in IK_COPY.items():
            if ik in out and fk in out:
                out[ik] = out[fk].copy()
        return out


def load_libraries():
    clips = []
    for path in LIBRARIES:
        before_obj, before_act = set(bpy.data.objects), set(bpy.data.actions)
        bpy.ops.import_scene.gltf(filepath=path)
        arm = next(o for o in bpy.data.objects if o.type == "ARMATURE" and o not in before_obj)
        lib = os.path.splitext(os.path.basename(path))[0].split("_")[0]
        arm.name = lib
        for a in sorted(set(bpy.data.actions) - before_act, key=lambda a: a.name):
            if not a.name.startswith(SKIP_PREFIXES):
                clips.append((lib, arm, a))
        for o in bpy.data.objects:
            if o not in before_obj and o.type == "MESH" and o.parent != arm and not o.find_armature():
                bpy.data.objects.remove(o, do_unlink=True)  # the reference prop (Icosphere)
    return clips


def import_manny():
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=MANNY)
    new = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in new if o.type == "ARMATURE")
    meshes = [o for o in new if o.type == "MESH"]
    return arm, meshes


def bake(manny, target, src, action):
    scene = bpy.context.scene
    src.arm.animation_data_create()
    src.arm.animation_data.action = action
    if hasattr(src.arm.animation_data, "action_slot") and action.slots:
        src.arm.animation_data.action_slot = action.slots[0]
    f0, f1 = (int(round(v)) for v in action.frame_range)
    out = bpy.data.actions.new("A_" + action.name)
    manny.animation_data_create()
    manny.animation_data.action = out
    rest = {b.name: b.matrix_local for b in manny.data.bones}
    prev = {}
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        deltas, pos = src.sample()
        poses = target.pose(deltas, pos, src)
        for pb in manny.pose.bones:
            b = pb.bone
            m = poses[pb.name]
            if b.parent:
                basis = rest[pb.name].inverted() @ rest[b.parent.name] @ poses[b.parent.name].inverted() @ m
            else:
                basis = rest[pb.name].inverted() @ m
            loc, rot, _ = basis.decompose()
            if pb.name in prev:
                rot.make_compatible(prev[pb.name])
            prev[pb.name] = rot
            pb.rotation_mode = "QUATERNION"
            pb.location, pb.rotation_quaternion, pb.scale = loc, rot, (1, 1, 1)
            pb.keyframe_insert("location", frame=f - f0)
            pb.keyframe_insert("rotation_quaternion", frame=f - f0)
            pb.keyframe_insert("scale", frame=f - f0)
    return out, f1 - f0


def export(manny, path, frames):
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = 0, frames
    bpy.ops.object.select_all(action="DESELECT")
    manny.select_set(True)
    bpy.context.view_layer.objects.active = manny
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"ARMATURE"},
        add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X",
        use_armature_deform_only=False, apply_scale_options="FBX_SCALE_NONE",
        bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
        bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0)


def preview(manny, meshes, src, action, out_png, frac):
    """Side by side render: UAL source (left) and retargeted mannequin (right)."""
    scene = bpy.context.scene
    f = int(action.frame_range[0] + (action.frame_range[1] - action.frame_range[0]) * frac)
    scene.frame_set(f)
    if not scene.camera:
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        scene.collection.objects.link(cam)
        cam.location = (-0.6, -4.2, 1.0)
        cam.rotation_euler = (math.radians(86), 0, 0)
        cam.data.lens = 40
        scene.camera = cam
        scene.render.engine = "BLENDER_WORKBENCH"
        scene.display.shading.light = "STUDIO"
        scene.display.shading.show_shadows = True
        scene.render.resolution_x, scene.render.resolution_y = 900, 700
    for o in bpy.data.objects:
        if o.type == "MESH":
            arm = o.find_armature()
            o.hide_render = arm not in (src.arm, manny)
    scene.render.filepath = out_png
    bpy.ops.render.render(write_still=True)


def main():
    only = set(arg("--only", "").split(",")) - {""}
    preview_dir = arg("--preview")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    manny, manny_meshes = import_manny()
    # after the FBX import (which sets the scene rate from the file), before the glTF import
    # (which converts its seconds to frames at the scene rate)
    scene.render.fps, scene.render.fps_base = FPS, 1.0
    clips = load_libraries()
    # stand the source beside the mannequin for previews (world deltas don't depend on placement)
    sources = {}
    for lib, arm, _ in clips:
        if lib not in sources:
            arm.location.x -= 1.2 if preview_dir else 0.0
            bpy.context.view_layer.update()
            sources[lib] = Source(arm)
    targets = {}

    os.makedirs(OUT_DIR, exist_ok=True)
    index = []
    for lib, arm, action in clips:
        if only and action.name not in only:
            continue
        src = sources[lib]
        if lib not in targets:
            targets[lib] = Target(manny, src, bone_map())
        out, frames = bake(manny, targets[lib], src, action)
        name = "A_" + action.name
        export(manny, os.path.join(OUT_DIR, name + ".fbx"), frames)
        index.append({"name": name, "source": f"{lib}/{action.name}", "frames": frames + 1, "fps": FPS,
                      "loop": action.name.endswith("_Loop")})
        log("%-28s %4d frames" % (name, frames + 1))
        if preview_dir:
            os.makedirs(preview_dir, exist_ok=True)
            # replay the baked clip for the preview
            manny.animation_data.action = out
            preview(manny, manny_meshes, src, action, os.path.join(os.path.abspath(preview_dir), name + ".png"), 0.4)
        bpy.data.actions.remove(out)
    if not only:
        with open(os.path.join(OUT_DIR, "clips.json"), "w", encoding="utf-8") as f:
            json.dump({"source": "Quaternius Universal Animation Library 1+2 (Standard), CC0",
                       "skeleton": "UE5 mannequin (SK_Mannequin)", "clips": index}, f, indent=1)
    log("done: %d clips -> %s" % (len(index), OUT_DIR))


main()
