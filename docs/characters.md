# Characters: bodies, editable heads and animation

**Decision (2026-10-04): characters are MakeHuman (MPFB) bodies on the UE5 mannequin skeleton. MetaHuman is dropped.** It cost about 2× the game-thread time and 3–5× the draw calls per character, and it can't be committed to a public repo. The full reasoning and the profile numbers are in [ADR 0002](adr/0002-metahuman-characters.md#decision-makehuman-2026-10-04).

## The character model
One shared body per gender. A face is just numbers on top of it.

```
AMRCharacter (capsule, movement, GAS on the player state)
 └─ Mesh = SKM_MPFB_Male / _Female   body + eyes + brows + lashes, ONE skinned mesh on SK_Mannequin
     │                               93 head-only morph targets; animation driver (ABP for now)
     └─ Hair  SKM_MPFB_<G>_Hair_<style>   leader pose -> body; carries the same morph targets
                                         hidden for the owner in first person
```

| What varies per character | How | Replicates as |
|---|---|---|
| Body (male/female) | which `DA_MPFB_<G>` appearance | 1 byte |
| Face shape | 49 **head sliders** (-1..1); each drives a decrease/increase morph pair | 49 bytes (quantised) |
| Hair style | which hair mesh from the appearance's `HairStyles` | 1 byte |
| Skin, eye, hair colour | material parameters on the shared `MI_*` (`Tint`) | a few bytes |

- **The body asset is shared.** Every male uses the same `SKM_MPFB_Male`, so mesh, skin weights and morph data live in memory once.
  - Skinned meshes are not GPU-instanced like static meshes. Each character still has its own skinning work and draw calls.
  - At about 0.09 ms of game thread and 12 draws per character, the budget is fine for the demo's crowds. See [profiling](#measuring-character-cost).
- **Head sliders.**
  - `UMRCharacterAppearance::HeadSliders` lists `{Name, DecrMorph, IncrMorph}`. `AMRCharacter::ApplyHeadSliders(values)` sets the morph weights on the body and the hair.
  - Morph targets touch only head vertices, and the eyes, brows, lashes and hair were refitted for every target when the kit was built. Changing a face therefore moves everything on the head together.
- **What the character creator will do.** It only writes a slider array, a hair index and colours. Saved to Supabase and replicated, those are enough to rebuild the face on every client. No per-character assets are needed.
- **Equipment (later):** clothing meshes on the same skeleton, as further leader-pose parts. MPFB can fit clothes to the body, and the kits already include CC0 shirts, pants and shoes for that.

### Head sliders
They come from MPFB's face modifiers: forehead, eyebrows, eyes, nose, mouth, ears, chin, cheeks, jaw and head shape, about 49 in all. The list is `HEAD_SLIDERS` in [mpfb_character.py](../tools/blender/mpfb_character.py) and is written to each kit's `manifest.json`. Symmetric pairs (left and right) share one slider.

## Building the character kits
MPFB output is CC0, so kits are committed (`art_src/characters/*`, `Content/Characters/MPFB_*`).

```bash
# once: install the MPFB asset packs into Blender (packs downloaded from static.makehumancommunity.org)
blender -b -P tools/blender/install_mpfb_packs.py -- "UnrealAssets/makehuman/*.zip"
# once: export the engine mannequins for Blender (needs tools/setup.ps1 first)
"G:/Unreal Engine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" -ExecutePythonScript="E:/2026_Experiments/meridian-unreal/tools/ue/export_mannequin.py" -unattended -nosplash -RenderOffscreen
# build a kit (body.fbx, hair_*.fbx, textures, manifest.json) from its settings
blender -b -P tools/blender/mpfb_character.py -- --settings tools/blender/characters/mpfb_male.json
# preview random faces without UE
blender -b -P tools/blender/preview_character.py -- art_src/characters/MPFB_Male build/previews/face.png --seed 3 --face
# import into UE (close the editor first): SKM_, hair, textures, MI_*, DA_MPFB_Male
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/import_character.ps1 -Kit art_src/characters/MPFB_Male
```

- **Kit settings** (`tools/blender/characters/*.json`) choose:
  - gender and the other macro values
  - skin, eye material, brows, lashes and hairstyles
  - which mannequin to fit: Manny for the male, Quinn for the female
- **Fitting.** Each MPFB `game_engine` rig bone is constrained onto its mannequin joint. The three UE4 spine bones' weights are spread by height over the five UE5 spine bones. That's why mannequin animations play on the body directly.
- **Materials.** Shared masters are in `/Game/Characters/Materials`: `M_CharacterSkin`, `M_CharacterCards` (alpha-masked, two-sided) and `M_CharacterEye`. Each kit gets material instances.
  - **New skeletal materials need `used_with_skeletal_mesh` and `used_with_morph_targets`.** Without them a standalone game renders the default grey.

## Animation
- **Current state.** The body is the animation driver, running the engine's `ABP_Unarmed`. That ABP is template content and is git-ignored.
- **Motion Matching later.** The Game Animation Sample (`UnrealAssets/GameAnimationSample`, UE-only licence, not committed) will replace `ABP_Unarmed` when movement is built out (Phase 3).

### Quaternius Universal Animation Library (CC0, committed)
About 80 clips from Quaternius' UAL 1 and 2 (Standard), retargeted onto the UE5 mannequin:

| Group | Clips |
|---|---|
| Locomotion | idle, walk, jog, sprint, crouch idle and walk, jump start/loop/land, roll, swim, slide, climb up |
| Combat | sword idle, attack, regular A/B/C (each with a recovery), regular and heavy combos, dash, block; shield idle, block, break, dash; punches, melee hook; overhand throw |
| Spells | spell enter / idle / shoot / exit |
| Reactions | hits (chest, head, knockback), death |
| Everyday | interact, pick up, sit, talk, open chest, consume, farming, chop wood, carry |
| Monsters | zombie idle, walk and scratch, likely mummy placeholders |

```bash
# 1. download UAL1 + UAL2 "Standard" (free) from quaternius.itch.io into UnrealAssets/quaternius/ and unzip
# 2. retarget (writes art_src/animations/quaternius/A_*.fbx + clips.json; --preview renders source vs result)
blender -b -P tools/blender/retarget_ual.py -- --preview build/previews/anims
# 3. import (close the editor first): /Game/Characters/Animations/Quaternius/A_* and AM_* montages
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/import_animations.ps1
```

- **Retarget method.** See the docstring in [retarget_ual.py](../tools/blender/retarget_ual.py).
  - The mannequin is first posed to match the UAL T-pose: limbs and fingers are swung onto the source bone directions, and the hands are matched to the palm plane.
  - Each frame then applies the source bones' world rotation changes on top of that pose. UE5 spine and neck bones between two UAL bones take a blend of both.
  - Hips are scaled by the hip-height ratio. The `ik_*` bones copy their FK bones.
- **All clips are in place** (no root motion).
- **One-shot actions also get an `AM_*` montage** in `DefaultSlot`: attacks, blocks, hits, deaths, casts and interactions. Gameplay code plays them with `PlayAnimMontage`.
- **UAL has no strafe or backward locomotion.** Directional movement will come from Motion Matching (GASP), or from orientation warping on the forward cycles.
- **Mixamo is not used.** Its files may not be redistributed raw, which a public repo would do, and downloading needs an Adobe account login.

## Fallbacks and first person
- **Appearance fallbacks:**
  1. the configured appearance (`DefaultGame.ini` → `DefaultAppearance`, currently `DA_MPFB_Male`)
  2. the plain engine mannequin
  3. a placeholder cylinder
  
  To try another appearance: `-MRAppearance=/Game/Characters/MPFB_Female/DA_MPFB_Female`.
- **Dedicated server:** builds only the driver, without the hair part.
- **First person:**
  - The full body stays visible to its owner, and the `head` bone is hidden for the owner. The hair is owner-no-see but still casts shadows.
  - The camera sits at the eyes, placed from the head bone's reference pose (`EyeOffsetFromHeadBone`).

## Checking it visually
```bash
"G:/Unreal Engine/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" /Game/Generated/Maps/L_World -game -windowed -resx=1280 -resy=720 -MRStartZone=300 -MRScreenshots
```
This writes `Saved/Screenshots/MRTour/` in the Raza town square, then quits:
- third-person and first-person views
- first person looking down
- three mid-swing sword montage shots (`attack_*`)

## Measuring character cost
`-MRProfile` (standalone, rendering) spawns 0, 1, 20 and 50 extra characters in front of the player. For each crowd size it logs one `MRProfile:` line and saves `Saved/Screenshots/MRProfile/crowd_NN.png`. Add `-MRRandomFaces` to give each spawned character random head sliders.

```bash
"G:/Unreal Engine/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" /Game/Generated/Maps/L_World -game -windowed -resx=1280 -resy=720 -MRStartZone=300 -MRProfile -MRRandomFaces
```

## Licensing: what is not in git

| Content | Where | How it gets there |
|---|---|---|
| Engine mannequin pack (skeleton, `ABP_Unarmed`) | `Content/Characters/Mannequins/` | `tools/setup.ps1` copies it from your UE install |
| Anything from Fab | `Content/Fab/` | Fab → Add to Project |
| Downloads (MPFB packs, Quaternius zips, GASP) | `UnrealAssets/` | Downloaded by hand |

## Known limits (next steps)
- **Appearance isn't replicated yet.** Everyone shows `DefaultAppearance`. The character creator and Supabase characters will add a replicated `{body, sliders[49], hair, colours}` struct.
- **No LODs on the MPFB meshes yet.** Generate them with UE's skeletal mesh reduction, and keep morph targets on LOD0–1 only.
- **Draw calls.** Brows and lashes could share one atlased material, saving 2 sections per character.
- **The player still runs `ABP_Unarmed`.** Its own locomotion AnimBP (or Motion Matching) using the UAL clips comes with Phase 3 movement.
- **Weapons:** first-person weapons should use UE's first-person rendering (`FirstPersonPrimitiveType`) to stop them clipping walls.
