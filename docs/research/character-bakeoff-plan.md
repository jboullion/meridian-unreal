# Plan: character bake-off (MPFB refit vs Quaternius vs Synty Sidekick)

- Status: **Plan only. No code has been written for this yet.**
- Date: 2026-10-05
- Goal: build one basic character from each candidate, run all three through the same checks, then decide.
- Background: [character-options.md](character-options.md).
- Requirements:
  - **Preset parts** (heads, hair, outfits) are fine. Face sliders are not needed.
  - **Face blend shapes for emotes are a plus.** The original game had facial emotions.

## The shared problem: different proportions on one skeleton
All three candidates need to play the mannequin animations (`ABP_Unarmed`, the UAL clips, later Motion Matching) **without being forced into Manny's proportions**.

**Why MPFB warps today.** `fit_rig_to_manny` puts a `COPY_LOCATION` on every bone onto Manny's joint, plus `STRETCH_TO` the next joint. The mesh follows the bones, so the whole body is reshaped to Manny's proportions (shoulder width, limb lengths, neck, hips).

**What animations actually need from a mesh:**
1. The **bone names and hierarchy** of `SK_Mannequin`.
2. The **same rest orientation per bone** as Manny: the same A-pose and the same local axes (roll). Animations store local rotations, so if the rest rotation differs, every rotation lands wrong.
3. Joint **positions** do not need to match. UE handles that with the Skeleton's **bone translation retargeting**: *Skeleton* mode for most bones, *Animation Scaled* for the pelvis, *Animation* for the root and IK bones. The engine keeps each mesh's own bone lengths and applies only the animation's rotations.

**So the fix is "orientation from Manny, length from the character":**
1. Pose the character into Manny's rest pose with its own rig: swing each limb onto Manny's bone directions. This is an ordinary skinned pose. It bends the arms down into the A-pose but doesn't change proportions.
2. Apply that pose as the mesh's new rest shape.
3. Rebuild the armature: each bone gets Manny's rest rotation and roll, placed at the character's own joint position. The bone lengths are the character's own.
4. Bind and export. Import onto `SK_Mannequin` **with** the mesh's own reference pose. `import_character.py` already passes `update_skeleton_reference_pose=False`, which keeps the skeleton's pose unchanged; the mesh keeps its own.
5. Set translation retargeting on `SK_Mannequin` once. Check Manny itself still animates correctly: in *Skeleton* mode it's unchanged, since its bones match the skeleton.

The UAL retarget (`retarget_ual.py`) already does step 1 in reverse ("matched pose": swing the mannequin onto the source directions), so most of the maths exists.

**Leader pose caveat.** A leader-pose follower copies the leader's bone transforms, so it takes on the **leader's** proportions.
- Parts (hair, outfits, heads) must be built for the same body proportions as the driver mesh they follow. That holds within each candidate's own kit (Quaternius outfits fit Quaternius bodies; Sidekick parts fit the Sidekick base).
- The **visible body must be the animation driver** (as MPFB is now), or the hidden driver must have the same proportions. A hidden Manny driver would re-warp everything.

**Fallback if rest-orientation matching is fiddly:** give the candidate its own Skeleton and use a runtime **IK Retargeter** from a hidden Manny (Retarget Pose From Mesh). It's simpler to set up but costs CPU per character. Measure it if we go that way.

## Test 1: MPFB with the proportions fixed
- **Change:**
  - In `mpfb_character.py`, replace the location-snapping fit (`fit_rig_to_manny`) with the orientation-only fit above.
  - Remove the `fit_to` joint positions. Keep using Manny for rest orientations.
  - Keep the morph and proxy refit steps. They run before the fit, so they shouldn't change.
- **Watch:**
  - The UE4-to-UE5 spine spread (3 bones onto 5, neck 1 onto 2). It already splits weights by height, so it should carry over, but the 5 UE5 spine joints now come from where along the MPFB spine? Spread them evenly between the MPFB pelvis and neck, at Manny's relative fractions.
  - Hips and shoulders, where the snapping was most visible.
  - Eye height for first person. `EyeOffsetFromHeadBone` is read from the mesh's head bone, so it follows automatically.
- **Output:** `SKM_MPFB_Male` v3, with no other changes to `DA_MPFB_Male`.

## Test 2: Quaternius Universal Base Characters
- **Get it:** from [itch.io](https://quaternius.itch.io/universal-base-characters), CC0. The Source version ($19.99+) adds the .blend files and the eye and skin colour shaders. Also get [Modular Character Outfits – Fantasy](https://quaternius.itch.io/modular-character-outfits-fantasy) to test equipment. Unzip into `UnrealAssets/quaternius/`.
- **Rig:** the same UAL humanoid rig (UE4 names, T-pose) that `retarget_ual.py` already maps, so the bone mapping and spine spread are reused from there.
- **Build** (a Blender script like `mpfb_character.py`, or a mode of it):
  1. Import one base: Regular proportions, male.
  2. Orientation-only fit to Manny (shared with Test 1).
  3. Spread the spine weights.
  4. Export the body, one hairstyle and one outfit as separate FBXs on the fitted armature.
- **Import:**
  - Reuse `import_character.py` with a manifest: body as the driver, hair and outfit as leader-pose parts.
  - Material: the Quaternius textures on `M_CharacterSkin`, or a port of their colour shader.
- **Licence:** CC0, so the kit is committed like MPFB.
- **Check:**
  - Do outfit parts replace body regions or sit over them? If over, we need hidden body sections per slot.
  - Draw calls per assembled character.

## Test 3: Synty Sidekick (starter pack)
- **Get it:** the free [Sidekick starter pack](https://www.fab.com/listings/8d8e9639-d93f-4f1d-8332-32ae0ef14bca) via Fab → Add to Project. It lands in `Content/Fab/`, which is git-ignored (not committable).
- **Skeleton:** Synty says it is "compatible with Unreal's Mannequin Skeleton". First check whether it ships its own Skeleton asset, and whether that means UE5 Manny or the UE4 mannequin. Then:
  - same bones and rest pose as `SK_Mannequin`: assign it as a compatible skeleton and use it as the driver directly
  - otherwise: an IK Retargeter from Manny (the fallback above), or re-import onto `SK_Mannequin` if the rest orientations match
- **Assemble:**
  - Build one character with Synty's editor tool and note what it produces: a merged mesh, or a set of parts?
  - Make an appearance data asset by hand. The parts become leader-pose parts on the Sidekick body.
- **Blend shapes:**
  - Set one ARKit face shape (`mouthSmileLeft` + `mouthSmileRight`, `browInnerUp`) from Blueprint. This proves emotes.
  - Set one body shape (heavy, muscular). Note which are morph targets and whether the clothing parts carry the same ones.
- **Check:** how many parts and materials per character, and whether Synty uses a single shared palette texture (usually it does, which is good for draw calls).

## Checks for all three (same as the MetaHuman/MPFB spike)

| Check | How |
|---|---|
| Look | `-MRScreenshots` tour in the Raza square. One shot of all three plus the current MPFB, side by side. |
| Deformation | Idle, walk, jog, sprint, jump, sword attack, sit, interact. Look at the shoulders, elbows, hips, knees and hand contact. |
| Proportion fidelity | Overlay the T-pose or rest silhouette from the source tool against the UE result. It should match exactly; that's what Test 1 is fixing. |
| Cost | `-MRProfile` at 0/1/20/50: game ms, render ms and draws per character. Compare with MPFB v2 (~0.09 ms, ~12 draws). |
| First person | Looking down; head hidden; hair hidden. |
| Pipeline | Steps from a fresh clone to an in-game character. Can it be scripted? Is it committable? |
| Variety | How many distinct characters can the parts make without new art? |
| Emotes | Sidekick ARKit shapes. MPFB has expression targets too (MPFB `expression` modifiers / face poses), worth noting. Quaternius: probably none, so emotes would be bone-driven (jaw, eyes) or texture swaps. |

## Where it runs
Blender 5.2 and UE 5.8 are on the Windows dev machine. The cloud session has neither, so scripts written there can't be run or tested in it.

## Order
1. **Test 1 first.** The orientation-only fit is shared code that Test 2 reuses.
2. Then Test 2 and Test 3 in either order.
3. Test 3 needs the user to add the starter pack from Fab.
