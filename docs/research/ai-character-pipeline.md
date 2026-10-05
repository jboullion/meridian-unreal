# Research: AI-generated outfits and NPCs on one base body

- Status: **Investigation and test plan. Nothing has been generated or built yet.**
- Date: 2026-10-05
- Question: can image models plus image-to-3D generators (Tripo and similar) produce **whole-outfit bodies** that we cut into head / top / bottom and wear on a shared base body? And can the same pipeline turn **original NPC sprites** into rigged 3D NPCs that still look like the classic game?
- Related: [ADR 0002](../adr/0002-metahuman-characters.md), [character-options.md](character-options.md), [character-bakeoff-plan.md](character-bakeoff-plan.md), [ai-monster-prop-pipeline.md](ai-monster-prop-pipeline.md) (monsters and props, same front end).

> Tool facts below come from vendor pages and search extracts (October 2026). Vendor topology claims especially need checking on our own assets; that is what the test is for.

## Short answer
**Feasible, with one condition: the AI mesh must never define the body's shape or skeleton.** The base body owns proportions, joints, skin weights, head, hands and seam positions. AI output is only the *surface* of a region, fitted back onto the base and skinned from it.

Done that way, it fits Meridian 59 well:
- Only **two body types** and **no body sliders** (presets are fine), so every outfit is fitted to exactly two bodies. That is the case where this works best.
- The original game already swaps **whole regions**, not layers (see [below](#how-the-original-game-does-it)). A "top replaces torso and arms" model is a faithful upgrade, not a departure.
- One skinned mesh per region, one material each: cost similar to today's MPFB kit.

What doesn't work is the naive version: generate a body in armour, rig it on its own and swap halves between generations. Each generation has slightly different proportions, joint positions and seam heights, so a scale-armour top would not line up with leather-armour legs.

## How the original game does it
From `data/items.json` and the client resources:

| Region | Item field | Example |
|---|---|---|
| Torso | `vrIcon_male` / `vrIcon_female` | `ScaleArmor`: `bti.bgf` / `btj.bgf` |
| Left / right arm | `vrLeftarm_*`, `vrRightarm_*` | `Shirt`: `bla` / `bra` |
| Legs | `vrLegs_*` | `PantsA`: `bfa` / `bfb` |
| Head, helmet | separate | `Helmet` (`viLayer` 1) |

- Body armour (`ScaleArmor`, `LeatherArmor`, chain, plate, nerudite) only sets the **torso**. The arms come from the shirt underneath.
- Robes set torso, both arms **and** legs.
- Player overlays are drawn in **8 directions** (front, 3/4, side, back): `bti` has 24 bitmaps in 22 groups. They are ready-made multi-view references for each armour's look.

**Proposed remaster regions** (per gender): `Head` (always the base body's, plus hair/helmet), `Top` (torso + arms), `Hands` (base hands; gloves later), `Bottom` (hips, legs, feet). Robes are a `Top` that hides `Bottom`, or a full-body piece.
Arms move into `Top` because "shirt sleeves under armour" is a layering rule we'd have to build and test twice. If we want it back, an armour `Top` can mark its arms as "use the shirt's arms" later.

## The pipeline

```
base body (A-pose, ours)                original sprite(s) / prompt
   │ ortho renders: front, side, back      │ style reference
   ▼                                       ▼
 [1] image model: "dress this exact body in <outfit>", same pose, camera, proportions
   ▼ front/side/back concept views (checked: silhouette vs base)
 [2] image-to-3D (multi-view): quad mesh, ~15–25k faces, PBR, UVs
   ▼
 [3] Blender fit (scripted): align → cut by region → conform seams → transfer weights → export on SK_Mannequin
   ▼
 [4] UE: Top / Bottom leader-pose parts, hide covered body sections
```

### 1. Base body renders
- One script renders the chosen base (MPFB refit, Quaternius or Synty, from the bake-off) in its **A-pose** rest: orthographic front, side and back, flat light, plain grey background, about 1024×2048.
- Also save a **depth** and **normal** pass. They're for the ControlNet fallback below.
- Pose: A-pose with clear gaps under the arms and between the legs. A T-pose separates the armpits even better but deforms worse at the shoulders once rigged. Try A-pose first.

### 2. Image model: outfit concepts
- **Edit, don't generate.** Give the model the base render and ask it to dress *this* figure, keeping pose, camera, framing and proportions. Current leaders for identity/shape preservation are Nano Banana Pro and GPT Image 2; Flux Kontext is the open-weight option.
- Add the original sprite (for example `bti` front and side frames) as a **style reference**, so the remaster keeps the original's colours and shapes (red shoulder pads, dark leather straps). That matches the "stay close to the original look" rule.
- Make the front view first, then side and back views from the front result, so they agree.
- Prompt rules: full body, feet visible, no props, no cape unless wanted, hands open and empty, even neutral lighting (no baked shadows), plain background.
- **Automatic check:** overlay the outfit's alpha mask on the base render's. Body outline within a few pixels (except where armour adds bulk) passes; drift in pose or limb length fails and is regenerated.
- **Fallback if pose drift is common:** ComfyUI with Flux + depth/normal ControlNet from the base render. The output then can't leave the base body's silhouette. More setup, but deterministic.

### 3. Image to 3D
| Generator | Why test it | Licence / access |
|---|---|---|
| **Tripo P2.0 / H3.1** (primary) | Native quad-dominant meshes (P2.0, Sept 2026), up to ~25k quads, multi-view input (front/left/right/back), Smart UV, part segmentation, retopology to a target count | Hosted. Paid plan needed: free-plan models are public and CC BY 4.0 without commercial rights. |
| **Rodin Gen-2.5** or **Meshy 7** | A second commercial opinion on the same inputs | Hosted, paid |
| **TRELLIS.2** | Open weights control (MIT), PBR textures; can be scripted | ~24 GB VRAM, so a RunPod job, not the 3070 |

- Export as glb with PBR. Generate at the target face count (15–25k for a full outfit body) rather than decimating a 500k mesh.
- Hunyuan3D is skipped for now: the useful versions (3.x) are hosted-only, and the open weights' licence excludes the EU, UK and South Korea, which is awkward for a public repo with contributors anywhere.

### 4. Blender fit (the important part)
A script like `tools/blender/fit_outfit.py --base <kit> --outfit <glb> --regions top,bottom`:

1. **Align.** Scale and translate the generated mesh onto the base by bounding box, then refine with ICP on the base's surface. Rotation should already match (same views).
2. **Conform.** Shrink-wrap inward wherever the outfit sits inside the base or barely above it (tight cloth, sleeves), with a small offset so skin never pokes through. Leave real bulk (pauldrons, scale) alone.
3. **Cut by region.** Region masks live on the **base body** as vertex groups (`top`, `bottom`, `head`, `hands`). Each outfit face takes the region of the nearest base surface point. The cut lines are therefore identical for every outfit.
4. **Seam contract.** Every outfit uses the same cut rings on the base, and every seam is hidden by overlap:
   - **Waist:** the `Top` hem extends 5–8 cm below the belt line. In the overlap band, `Bottom` is pulled in slightly, so any top covers any bottom.
   - **Neck:** the base head includes the neck down to a fixed ring that every `Top` collar covers.
   - **Wrists / ankles:** sleeves and trousers overlap the base hands and feet by a fixed band.
5. **Skin weights.** Transfer from the base body (nearest face, interpolated), then smooth. Rigid plates can optionally be locked to their main bone. The AI's own rig, if any, is ignored.
6. **Material pass.** One material per piece, 1–2K texture. Run the same albedo clean-up and texel density as the environment work, so armour sits next to Raza's upscaled textures.
7. **Export** each region as its own FBX on the base armature, with the base's reference pose (the bake-off's "orientation from Manny, length from the character" rule applies unchanged).

### 5. In Unreal
- Each piece is a leader-pose part on the base body, like hair today. Covered base sections are hidden per region.
- An outfit is a data asset: `{Top, Bottom, hides: [...]}` per gender, keyed from the item class (`ScaleArmor` → `DA_Outfit_ScaleArmor`).
- Later, Mutable or a skeletal mesh merge combines body + pieces into one mesh when equipment changes.

## NPCs from the original sprites
Same pipeline with a different first step. Most named NPCs are **one high-res front view** (shrink 7): `tsinnk` (the Tos innkeeper) is about 156 px wide, `gamos` 173, `bqsmith` 206. A few, like `innkeepF`, are tiny multi-frame sprites.

1. **Extract** with `tools/bgf2png/bgf2png.py <name>`.
2. **Upscale** (the same `4xTextures_GTAV` model used for textures) to give the image model detail to keep.
3. **Turnaround:** "this exact character, A-pose, front / side / back, same clothes, colours and face". Here the sprite is the identity reference, and the base body render is the pose reference. Back views are invented, so review them.
4. **Image to 3D** as above, one whole-body mesh.
5. **Rig:** NPCs keep **their own proportions** (the innkeeper is stocky). So skip the "conform to base" step and fit the mannequin skeleton to the NPC instead:
   - place joints from landmarks (or Tripo's auto-rig joints, renamed),
   - orient bones like Manny, with the NPC's own lengths (the bake-off Test 1 code),
   - weights by transfer from a base body scaled to the NPC, with heat-diffuse weights as the fallback.
6. **Swap the hands** for base hands where the AI fingers are fused, and drop an existing base head in where the AI face looks wrong.
7. Import like a body kit: one skinned mesh, one material, `ABP` / UAL clips play unchanged.

**Long robes and skirts** (Gamos, Raza's elder) are the hard case for both NPCs and robes: leg-weighted skirts tear when the legs stride. Options, in order: blend skirt weights between both thighs and the pelvis; add 2–4 skirt bones driven by AnimDynamics; Chaos cloth only for hero NPCs.

## Test plan
Two small spikes, run after (or alongside) bake-off Test 1, since both need the orientation-only fit.

### Spike A: three outfits on the male base
| Outfit | Why |
|---|---|
| **Scale armour** (`bti`) | Rigid, detailed surface; the "does it look good" test |
| **Leather armour** (`btg`) + **plain pants** (`bfa`) | Mid case; mixing a top and bottom from different generations |
| **Robe** (`btc` / `bfc`) | Deformation worst case (long skirt over legs) |

Steps: base renders → 3 image-edit concepts (front/side/back) → Tripo P2.0 (plus the same input through one second generator and TRELLIS.2) → `fit_outfit.py` → UE parts.

### Spike B: two NPCs
- **Tos innkeeper** (`tsinnk`): apron, shorter tunic, own proportions.
- **Gamos** (`gamos`): long robe and cloak.

### Checks
| Check | How | Pass |
|---|---|---|
| Looks like the original | Render the 3D at the sprite's 8 angles and put them next to the `bgf` frames | Recognisably the same item/person |
| Fits the world | `-MRScreenshots` tour in Raza | Not out of place next to the environment |
| Silhouette fit (A) | Alpha IoU, concept vs base render | Pose and limbs within a few px |
| Seams (A) | Every top × every bottom, plus bare | No gaps or poke-through at 3rd-person distance |
| Deformation | Idle, walk, jog, sprint, jump, sword attack, sit | Shoulders, elbows, hips, knees, skirts acceptable |
| Topology | Wireframe at joints; tris; UV islands | Loops around elbows/knees, ≤ 25k tris per outfit |
| Cost | `-MRProfile` with 50 geared characters | Within ~10 % of the MPFB v2 kit per character |
| Effort | Time per piece, generation to in-game | ≤ 2 h manual work per outfit or NPC after scripting |
| Licence | Terms of the plan used, image model and generator | Output may sit in a public repo |

### Decision rule
- **Adopt** for outfits if all three outfits pass seams and deformation and land under the effort budget. Robes may need the skirt-bone fix to pass.
- **Partial:** if tight clothes fail to look better than paint, use **texture-only** outfits for shirts, leather and pants (project the concept views onto the base body's own UVs, no new geometry), and AI meshes only for bulky armour.
- **Adopt** for NPCs if both resemble their sprites and deform acceptably. NPCs can pass even if outfits fail: they don't need seams.

## Risks
- **Proportion and pose drift** in the image step. The fit absorbs small drift; the ControlNet fallback removes large drift.
- **Joint topology.** Quad-dominant isn't the same as loops placed for elbows. That's the main thing Tripo P2.0 has to prove.
- **Baked lighting** in AI textures. Ask for flat lighting, use PBR output, and run a delight / albedo clean-up if needed.
- **Style drift** between outfits made weeks apart. Keep a fixed style block, the base render and a reference sheet in `tools/`, and record prompts, seeds and model versions per asset in a manifest, like the kit manifests.
- **Licences.** Tripo needs a paid plan for owned, private output; check that its terms (and the image model's) allow publishing the files in a public repo. The original sprites are fine as inputs (we have permission to use original art).
- **Hands and faces** are where generators are weakest. Base hands always; base heads as the NPC fallback.

## Sources
- [Tripo P2.0 native quad meshes (aicybr)](https://aicybr.com/blog/tripo-p2-native-quad-mesh-ai-3d-generation), [Tripo low-poly generator](https://www.tripo3d.ai/features/low-poly-3d-model-generator), [Tripo retopology limits (assethub)](https://assethub.io/blog/tripo-retopology), [Scenario: Tripo in 2026](https://www.scenario.com/blog/tripo-image-to-3d-model-ai-2026)
- [Tripo commercial-use licensing](https://www.tripo3d.ai/blog/ai-3d-commercial-use-license), [Tripo pricing (costbench)](https://costbench.com/software/ai-3d-generation/tripo-ai/)
- [AI 3D model generation timeline 2026 (cinevva)](https://app.cinevva.com/guides/ai-3d-model-generation-timeline-2026): TRELLIS.2, Hunyuan3D 3.x, Rodin Gen-2.5, Meshy 7, UniRig
- [Hunyuan3D-Part](https://github.com/JDScript/Hunyuan3D-Part), [Hunyuan 3D community licence](https://www.tencentcloud.com/techpedia/148273?lang=en)
- [Character consistency methods compared (flick.art)](https://flick.art/blog/img2img-consistent-character), [Character turnaround sheets 2026 (apatero)](https://apatero.com/blog/ai-character-turnaround-sheet-generation-guide-2026)
