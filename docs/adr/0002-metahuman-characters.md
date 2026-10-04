# Brainstorm: MetaHuman characters, creator, equipment and animation

- Status: **Decided 2026-10-04: MakeHuman (MPFB) on the UE5 mannequin skeleton; MetaHuman dropped.** See [Decision](#decision-makehuman-2026-10-04). The MetaHuman sections below are kept as background.
- Date: 2026-10-04
- Related: [ADR 0001](../adr/0001-engine-and-architecture.md), which already picks MetaHuman for characters.

> **Update (2026-10-04): decided for MakeHuman, see [Decision](#decision-makehuman-2026-10-04).** Earlier note: MetaHuman is being re-evaluated. A modular character on the UE mannequin skeleton, with a MakeHuman (MPFB) base body, may suit a performant online game and a public repo better. See [Alternatives to MetaHuman](#alternatives-to-metahuman) and the [proposed MPFB vs MetaHuman spike](#proposed-spike-mpfb-vs-metahuman). The MetaHuman plan below stays as the baseline until the spike decides.

## Why MetaHuman fits Meridian 59
- **Every player character is human.** The original creator offered gender, face parts (head, eyes, mouth), hair style and colour, skin tone, then stats and spell schools. There are no other playable races, so MetaHuman's "humans only" limit costs us nothing.
- **One shared skeleton.** Every MetaHuman body uses the same skeleton, so one animation set covers every player and every human NPC.
- **Items already split by body.** `data/items.json` has `vrMaleIcon` / `vrFemaleIcon` per wearable (for example the ant mask: `mmant.bgf` / `mfant.bgf`). That maps onto per-body-type equipment meshes.

## Decisions so far
1. **Face freedom: presets now, sliders later (Option A, upgradeable to B).** Players pick from authored head presets. The save format stores parameters, not only preset IDs, so we can add DNA blend sliders later without migrating characters.
2. **NPCs are custom MetaHumans too.** They're authored in the in-editor MetaHuman Creator with full freedom, not limited to the player presets.
3. **Slightly stylized look.** Not photoreal. See [Stylization](#stylization-and-lod-policy).
4. **Performance first.** We use aggressive LODs, even up close.

## What MetaHuman gives us

| Piece | Role | Where it runs |
|---|---|---|
| MetaHuman Creator (*MetaHuman Character* asset, in-editor since UE 5.6) | Sculpts faces, bodies, skin, eyes and hair | **Editor only.** Players can't use it in the shipped game. |
| Assembled MetaHuman | Face mesh, body mesh, grooms, materials and a **DNA** file | Runtime |
| DNA + RigLogic | Face rig data plus a runtime solver that maps about 250 facial controls onto joints and blendshapes | Runtime (CPU) |
| Shared body skeleton | Fixed body types (height, weight, gender) | Runtime |
| Grooms / hair cards / helmet mesh | Hair, falling back from strands to cards to a mesh as LODs drop | Runtime |
| Outfit fitting | Fits clothing to MetaHuman body types | Mostly editor-time |
| MetaHuman Animator | Facial and body capture from video or audio | Content creation |

> Check the UE 5.8 release notes for any official **runtime** MetaHuman customization. Epic has moved fast here, and that could change Option B's cost.

## In-game character creator

### Option A (now): presets plus parameters
- About 8–16 head presets per body type, built in MetaHuman Creator. These replace M59's face-part pickers.
- Body type, from MetaHuman's standard set.
- Hair from a curated groom list. Every groom needs good cards and a helmet-mesh fallback (see the LOD policy).
- Colours as material parameters: skin tone, hair colour (melanin / redness), eye colour.
- Optional extras through material masks: scars, tattoos, face paint, stubble.

### Option B (later): DNA blend sliders
- Epic's open-source **DNACalib** library can blend between the DNAs of 2–3 presets. Morph blending on the face mesh goes with it.
- Sliders like "jaw width" or "nose length" are weights between presets.
- Cost: a unique DNA and mesh instance per character, more memory, and more QA to keep blends from looking broken.

### Option C (spike): Mutable (Customizable Object plugin)
- Epic's runtime customization system. It merges meshes and textures and generates LODs at runtime.
- It's the strongest candidate for **equipment merging** (below), even if the face stays on Option A or B.

### Save format sketch (designed so Option B can be added later)
```jsonc
{
  "appearance_version": 1,
  "body_type": "f_med_nrw",        // MetaHuman body type id
  "head": {
    "preset": "head_f_07",          // Option A
    "blend": []                      // Option B later: [{"preset": "head_f_03", "w": 0.3}, ...]
  },
  "hair":  { "groom": "hair_braid_02", "melanin": 0.62, "redness": 0.15 },
  "skin":  { "tone": 0.41, "marks": ["scar_cheek_l"] },
  "eyes":  { "color": "#5a7a3a" }
}
```
- Stored as a JSONB column on the character row in Supabase. Only the server writes it, after validating every id against a whitelist DataAsset.
- Replicated as a compact struct: preset and groom ids as small ints, plus quantized floats.
- Older saves stay valid when we add `blend` weights; an empty array means "pure preset".

### Stats and the rest of M59 creation
Stats (Might, Intellect, Stamina, Agility, Mystic Aim) and the spell/skill school picks are pure data on the GAS attribute set. MetaHuman doesn't affect them. Name and description go to Supabase as before.

## NPCs
- Author each named NPC (the Raza innkeeper, Gamos the banker, Bentu the vault keeper, etc.) as a full MetaHuman Character asset. NPCs aren't limited to the player presets.
- NPCs follow the same LOD and stylization rules as players, so they don't stand out from them.
- Promote good NPC faces into the player preset pool, and mix player presets with different hair and colours to make generic NPCs (guards, townsfolk) cheaply.
- Shopkeepers and quest givers can use MetaHuman Animator (audio-driven) for dialogue lip-sync.

## Equipment
- Equipment pieces are skinned to the MetaHuman body skeleton and driven by the body with **Leader Pose**, or **Copy Pose** for pieces with physics.
- **One mesh per body type** (or per body-type family), fitted with MetaHuman's outfit tooling. This replaces the original male/female icons.
- **Hiding the body:** each item lists the body regions it covers (torso, arms, legs, feet, head), so hidden body sections don't poke through.
- **Helmets:** each helmet sets a hair rule: hide the hair, swap it for a short "helmet hair" groom, or leave it as is.
- **Weapons and shields** attach to hand and back sockets.
- **Mapping item data:** the `parents` chain in `items.json` (`Helmet`, `FaceMask`, `DefenseModifier`, …) maps onto equipment slots in a DataAsset made at import time.
- **Performance:** merge the body and armour into one skeletal mesh (Mutable or the skeletal mesh merge utilities) when equipment changes, so a geared player is 1–3 draw calls.

## Animation
- **Body:** Motion Matching locomotion, GAS ability montages and Chaos ragdoll deaths (all from ADR 0001) on the shared skeleton. They work for every body type without retargeting.
- **Face:** RigLogic drives emotes (M59 had `wave`, `point`, etc.), combat pain and effort faces, and NPC dialogue. Face animation only runs near the camera (see LOD policy).
- **First person:** a separate arms mesh, or the full body with the head hidden. Decide this alongside the camera work.

## Stylization and LOD policy
The idea: always render characters at a reduced LOD, even up close. That saves resources and moves the look away from photoreal.

### Caveats
- A low LOD isn't *stylized*, just *lower detail*. Fewer polygons give faceted silhouettes (jaw, ears, fingers), and hair cards look stringy up close. On its own, this reads as "low settings", not "art direction".
- Facial expression detail drops with LOD. The lower face LODs use fewer joints, and as far as we know blendshapes and wrinkle maps are only present at the top LODs. **Check this against 5.8.** If close-up emotes matter, the face LOD can't go too low.
- **The stylized look should come mostly from shading, not geometry:**
  - painterly or hand-tuned albedo
  - reduced skin micro-detail and pore normals
  - softer, simpler subsurface scattering
  - slightly flattened specular
  - limited, consistent colour palettes
  - optionally a light post-process (edge darkening, colour grading) shared with the environment art so characters and world match

### Proposed policy (tune by profiling)

| Situation | Body/face LOD | Hair | Face rig |
|---|---|---|---|
| Character creator / inventory paper-doll | LOD 0–1 | Strands or best cards | Full |
| Own character, 1st/3rd person | LOD 1–2 (forced minimum) | Cards | Full |
| Others within ~5 m | LOD 2 | Cards | Reduced (joints only) |
| 5–20 m | LOD 3–4 | Cards / helmet mesh | Off (static face, or blink only) |
| > 20 m | LOD 5+ | Helmet mesh | Off; animation update rate reduced (URO / budget allocator) |
| Dedicated server | Skeleton + collision only | none | Off |

- Use a **forced minimum LOD** (`MinLOD` / quality scalability), not only distance-based LODs, so the look stays the same at every distance.
- The creator screen is the one place that can spend more, because only one character is on screen.
- Add a crowd-quality setting for low-end machines that shifts the whole table down.

## Alternatives to MetaHuman

### Concerns with MetaHuman as the in-game character
MetaHuman is built for film-quality hero characters. Its value is in the face rig (RigLogic), strand hair and realistic skin. We plan to cut back all three (stylized look, forced low LODs, face rig off past ~5 m), so we would pay its costs without using its strengths.
- **The creator stays hard.** MetaHuman Creator is editor-only. A player creator means presets or our own DNA blending (Option B), and new heads depend on Epic's cloud auto-rigging.
- **Cost per character.** Every player has a unique face rig solved on the CPU, several meshes, grooms and large skin textures. That adds up with 50 players in Raza, even at low LODs.
- **It works against the stylized look.** Every item in the stylization list fights MetaHuman's design.
- **Public repo.** Nothing MetaHuman can be committed, so every contributor rebuilds characters by hand.
- **Crowds.** The MetaHuman Crowd plugin only helps if everything else is MetaHuman too.

### The common MMO approach: a modular character on one skeleton
This is roughly how WoW, ESO and FFXIV work, and what Unreal's Mutable is built for.

| Piece | How it works |
|---|---|
| Skeleton | The UE5 mannequin skeleton (already our animation driver). All animation is shared. |
| Body | One male and one female base mesh, with morph targets for build (height, weight, muscle). |
| Head | One head mesh with shared topology, plus morph targets. Creator sliders set morph weights; presets are saved slider values. |
| Hair | Hair meshes (cards), no strand grooms. |
| Colour and marks | Material parameters and masks: skin tone, hair and eye colour, scars, tattoos. |
| Armour | Modular skinned pieces per slot, each hiding the body regions it covers. |
| Merging | Mutable merges body and gear into one mesh with a shared texture when equipment changes (1–3 draw calls per player). |

- **The save format still works.** `head.preset` / `head.blend` become morph weights instead of DNA blends, which is simpler.
- **Existing code survives.** `UMRCharacterAppearance` (hidden Manny driver plus leader-pose parts) doesn't depend on MetaHuman. A modular body is just a different list of parts, without the face AnimBP. Mutable could replace the parts list later.

### Where the base human comes from
Morph sliders need every head to share the same topology. That rules out AI generators (Meshy etc.) for bodies and heads: each output has different, uneven topology that deforms poorly.

| | Cost | Can go in the public repo | Game-ready | Creator sliders | Look |
|---|---|---|---|---|---|
| **MakeHuman (MPFB, Blender add-on)** | Free | Yes (exports are CC0) | Light; a good starting point | Yes, morph-based | Realistic to lightly stylized; pushed with shading |
| VRoid Studio | Free | Only content we make ourselves | Medium; has an export reduction option | Yes; VRM face expressions come free | Anime / toon |
| MetaHuman | Free | No | Heavy | Hard (presets or DNA blending) | Photoreal; hard to stylize |
| Daz3D (Genesis) | Paid; game use needs an Interactive License per product | No | Heavy (many materials, 4K textures, render-oriented hair) | Yes, extensive morphs | Realistic |
| Reallusion Character Creator 4 | Paid, plus export licences | Probably not | Medium | Yes | Realistic |
| Hand-made or commissioned base | Time or money | Yes | Whatever we build | Yes | Fully controlled |

Notes:
- **MPFB:** the best fit for a free public fan project. We'd fit or re-skin it to the UE skeleton once in Blender (5.2 is installed).
- **MetaHuman as a face source (hybrid):** all MetaHuman heads share topology. We could sculpt faces in Creator, export a mid LOD of each, and turn them into morph targets on one plain head mesh, with no RigLogic or grooms. The files still can't be committed.
- **VRoid:**
  - Community assets (BOOTH, VRoid Hub) each carry their creator's licence. Many are paid, ban redistribution or ban use in other games, so we can't simply offer them in the creator. Content we make in VRoid Studio, or its bundled presets, is usable (confirm the current terms).
  - The VRM animation community is mostly Unity/VRChat and MMD dances. Game locomotion and combat come from UE sources anyway.
  - It uses a VRM humanoid skeleton (re-skin or live retarget), and body sliders are baked per export, so we'd fix one or two base bodies.
  - The toon style is a big restyle, and every Meshy item would need a toon pass.
  - The VRM4U plugin imports into UE; check that it supports 5.8.
- **Daz3D:**
  - Its marketplace and morph system are the strongest, and there's an official Daz to Unreal bridge.
  - Every shipped asset needs an Interactive License, and shipped files must be protected against extraction. That's unworkable for a public repo.
  - Figures need heavy optimization (polygon count, material slots, texture packing) before they can run in an MMO.
  - Still useful for reference renders and concept work.

### Meshy and AI-generated assets

| Asset | Fit |
|---|---|
| Swords, shields, staffs, wands | Very good: rigid, on hand or back sockets. Reduce polygons and texture size in Blender. |
| Props, furniture, town clutter | Very good. |
| Helmets and rigid armour (pauldrons, bracers) | Good: attach to a bone or skin fully to one bone. |
| Armour that bends (tunics, chainmail, pants) | Only as a starting point. Rebuild the mesh and skin it to the base body in Blender. |
| Base bodies and heads | No (topology). |

- **Consistent style:** use a shared style prompt or reference, and run a texture pass so items look like they belong together.
- **Licence:** check Meshy's terms for our plan. Free-tier output has historically required attribution (CC BY).

### Townsfolk crowds
- **Ambient townsfolk run only on each client and aren't replicated.** Each client spawns its own crowd from a per-zone seed. That costs no bandwidth or server CPU, and nobody notices that two players see slightly different crowds. Only NPCs players interact with (shopkeepers, quest givers) are server actors.
- Rendering options, from simplest to most powerful:
  1. **Lightweight actors.** A few dozen per zone, built from 10–20 pre-made outfit and face combinations. Use the Animation Budget Allocator and a lower animation update rate at distance. M59 town rooms are small, so this may be enough for Raza.
  2. **Vertex animation textures** (AnimToTexture plugin) **on instanced static meshes** for distant or background crowds, swapped for skeletal actors up close.
  3. **Mass Entity and Mass Crowd** (as in the City Sample), with ZoneGraph lanes and Smart Objects. It works with any skeletal mesh, so the MetaHuman Crowd plugin isn't required. Adopt it when option 1 runs out.
- Townsfolk use the same modular system and materials as players, so they look like they belong in the same world.

## Proposed spike: MPFB vs MetaHuman
**Goal:** decide the base character with evidence: visual fit in Raza, runtime cost, creator feasibility and pipeline effort. Both candidates go through the same `UMRCharacterAppearance` / driver path, so the comparison is fair and the code doesn't fork.

### Build the MPFB candidate
1. **Base body.** In Blender 5.2 with MPFB, make one male base body with a game-friendly setup (eyebrows, eyelashes, a mid-detail mesh, one material where possible). Export the figure and record its MPFB settings so they can be reproduced.
2. **Skeleton.** Re-skin (or fit) it to the UE5 mannequin skeleton in Blender, so it follows the Manny driver with **leader pose**, like the MetaHuman body does. Fall back to a live IK retarget only if re-skinning fails.
3. **Morphs.** Export 4–5 face and body shape keys (for example jaw width, nose length, weight) as UE morph targets.
4. **Hair.** One hair-card mesh from MPFB's assets.
5. **Pipeline.** Script the Blender export (`tools/blender/`) and extend `tools/ue/make_appearance.ps1` with a `-Source mpfb:<fbx>` mode. The output is an appearance asset like `DA_PlayerAppearance_MPFB`. Commit the CC0 exports, so a fresh checkout has a real character without any MetaHuman.
6. **Stylization.** Give both candidates the same simple stylized skin material, so the comparison isn't about texture quality.

### Compare against the current MetaHuman build
Run both appearances in the Raza square with `-MRAppearance=...` and the screenshot tour (`-MRScreenshots`).

| Measure | How |
|---|---|
| Visual fit | Tour screenshots side by side, one with a Meshy sword in hand. |
| Deformation | Shoulders, elbows, hips and knees through `ABP_Unarmed` idle, walk, run and jump. |
| Draw calls and triangles | `stat scenerendering` and `stat rhi`, one character on screen. |
| CPU per character | `stat anim` and `stat game` with 1, 20 and 50 instances (a debug spawn command). |
| GPU and memory | `stat gpu`, `memreport -full` (skeletal mesh and texture memory) with 50 instances. |
| Creator feasibility | MPFB: drive one morph from a test slider at runtime. MetaHuman: note what Option A or B would take. |
| Pipeline effort | Time from "new face" to "in game" for each, and what a fresh public checkout gets. |

### Decision rule
- **Prefer MPFB** if it looks acceptable next to the environment, deforms cleanly, and costs clearly less at 50 instances.
- **Keep MetaHuman** only if its visual advantage survives stylization and the 50-player cost fits the minimum-spec budget.
- **Consider the hybrid** (MetaHuman-sourced faces as morph targets on a plain head) if MPFB faces look weak but MPFB bodies are fine.
- Optional follow-up: a VRoid candidate through the same steps, if a toon restyle is still on the table.

## Open questions
- [ ] **MPFB vs MetaHuman spike** (above). Decide the base character before building the creator UI.
- [ ] Mutable spike with the winning base: body, one Meshy helmet and one skinned armour piece merged. Count draw calls.
- [ ] Townsfolk: 40 client-only walkers in Raza, profiled with and without the Animation Budget Allocator.
- [ ] Licences: confirm MPFB/MakeHuman export terms (CC0), Meshy output terms for our plan, and the VRoid Studio terms if VRoid is tried.
- [x] What does UE 5.8 actually offer for runtime MetaHuman customization? See the findings below.
- [ ] Which face LOD keeps enough expression for emotes and dialogue? Prototype one face at LOD 1, 2 and 3.
- [ ] Mutable spike: can it merge MetaHuman body and armour while RigLogic still drives the face?
- [ ] Stylization art test: one MetaHuman with a stylized skin and hair material in the Raza blockout, next to the environment art.
- [ ] Profile 50 players in Raza at the proposed LOD policy on the minimum-spec machine.
- [x] First-person approach: **full body, head hidden** (decided 2026-10-04; built, see [characters.md](../characters.md)).
- [ ] Licensing: confirm the current MetaHuman licence terms for a non-commercial fan project.

## Findings and first build (2026-10-04)

**What UE 5.8 offers (from the engine's MetaHumanCharacter plugin):**
- **Scripting:** Creator authoring and assembly are scriptable from Python (`MetaHumanCharacterEditorSubsystem`: create, conform, sculpt, grooms, clothing, auto-rig, texture requests, `build_meta_human`). Auto-rigging and texture synthesis are Epic cloud services, so the editor must be signed in to an Epic account.
- **Assembly pipelines:** "UE Optimized" (the one for games), "UE Cine" and "UEFN". Quality levels apply too.
- **Pipeline output:** an Actor class of our choosing, as long as it implements `IMetaHumanCharacterActorInterface` (`SetMetaHumanInstance` / `GetMetaHumanInstance`).
- **Runtime customization:** the closest thing is the *Palette / Collection / MetaHuman Instance* system (`UMetaHumanCollection`, `UMetaHumanInstance`). An Instance is a selection of palette items plus instance parameters (bool/float/name/colour), assembled into an output. Its override results distinguish "reassembly required" from "post-assembly parameters modified", which matches Option A (presets plus parameters) closely. **Spike this before building the creator UI.**
- **Also in 5.8:** Mutable and a new **MetaHuman Crowd** plugin (built on Mass, for MetaHuman crowds). Worth a look for townsfolk.

**What was built** (see [characters.md](../characters.md)):
- **Appearance data:** `UMRCharacterAppearance` holds parts plus a mannequin animation driver. This is Option A's "parts" without the creator UI yet.
- **Script:** `tools/ue/make_appearance.ps1` turns any assembled MetaHuman Blueprint (or Character asset) into an appearance.
- **Licensing:** MetaHumans and the engine mannequin stay out of git (public repo).
- **Animation:** engine mannequin animations (`ABP_Unarmed`) for now. Motion Matching later replaces the driver's anim class only.

### MetaHuman baseline for the spike (2026-10-04)
**Character:** Fab `MHC_Base_Male`, assembled with UE Optimized at medium quality by `tools/ue/make_appearance.ps1`, giving body + face parts on the Manny driver. This preset has no grooms.

**Pipeline notes:**
- The Fab MetaHuman Character came already auto-rigged and textured, so assembly needed no Epic cloud sign-in.
- Assembly logs RigVM "Cannot break link" errors while copying the face control rigs. These are harmless, but Python turns them into exceptions, so the script tolerates them, checks the output and saves it.
- **The engine's optional "MetaHuman Creator Core Data" isn't installed.** The editor reports "MetaHuman Optional Content folder not found", and skin microtiling textures and texture synthesis are missing. The assembled character still renders acceptably.
- Steps to a working character, with nothing committable: add from Fab → `make_appearance.ps1 -Source /Game/Fab/MetaHuman/MHC_Base_Male` → play.

**First person:**
- Eyes are now placed from the driver's head bone (172 cm for this body).
- Looking straight down with a naked body and a 59° vertical FOV shows shoulder tops, hands and feet, and the torso and limbs edge-on. That's geometry, not a bug. Revisit with clothing.

**Cost.** `-MRProfile` in a standalone development build on an RTX 3070 and i5-8600K, with the editor also running, so treat these as relative numbers. Each figure is an average of 180 frames.

| Extra characters | MetaHuman game / render / GPU ms | MetaHuman draws | Mannequin game / render / GPU ms | Mannequin draws |
|---|---|---|---|---|
| 0  | 3.9 / 13.0 / 7.0  | 545  | 2.5 / 8.7 / 6.3  | 524 |
| 1  | 3.7 / 12.8 / 6.8  | 567  | 2.4 / 8.6 / 6.3  | 530 |
| 20 | 7.0 / 13.6 / 7.6  | 1004 | 4.1 / 9.8 / 7.0  | 663 |
| 50 | 12.9 / 17.9 / 8.0 | 1282 | 6.7 / 12.7 / 7.3 | 781 |

**Reading it:**
- Each extra MetaHuman costs about **0.18 ms of game thread** (face AnimBP/RigLogic plus two skinned meshes) and **about 15 draw calls**. The mannequin costs about 0.08 ms and 5 draw calls.
- The GPU isn't the bottleneck at these counts. CPU (game + render thread) is.
- No LOD policy, LODSync or animation budgeting yet. Those would narrow the gap, but not close it.
- Spare mannequin numbers stand in for "one skinned mesh per character", which is roughly what a merged MPFB character would cost.

### MPFB candidate for the spike (2026-10-04)
**Pipeline.** All scripted and repeatable; output is CC0, so it is committed.
1. `tools/blender/mpfb_character.py` (headless Blender 5.2 + MPFB 2.0.17) builds a male from `tools/blender/characters/mpfb_male.json` (macro settings) with MPFB's `game_engine` rig and weights.
2. Every rig bone is constrained onto its **UE5 mannequin** joint: chains stretch to the next joint, branch bones aim at their main child, leaves only move. The fit error is 0 mm on all 52 joints.
3. The fitted shape, pose and helper mask are baked into the mesh.
4. Weights are renamed to UE5 bones, and the UE4 spine (3 bones) and neck (1) are **spread by height** over UE5 `spine_01`–`05` and `neck_01`–`02`.
5. The mesh is bound to the real `SK_Mannequin` armature and exported to `art_src/characters/mpfb_male.fbx`.
6. `tools/ue/import_character.ps1` imports it **onto `SK_Mannequin`** (`/Game/Characters/MPFB_Male/SKM_MPFB_Male`), adds the simple `M_SkinStylized`, and writes `DA_MPFB_Male`.

**At runtime** the MPFB mesh *is* the visible animation driver: one skinned mesh, no follower meshes and no face AnimBP. Every mannequin animation plays on it unchanged.

**Mesh:** 13,380 vertices (about 29k triangles) at 1.78 m, no LODs yet.

**Pitfalls found (now handled in the scripts):**
- Bone *tails* from an FBX import are guesses, so the fit never targets them.
- Mapping the UE4 spine by *name* dragged the chest up 17 cm; it has to be mapped by position.
- New materials need `used_with_skeletal_mesh`, or a standalone game silently draws them in default grey. The world's blockout materials had the same problem with Nanite, which is now disabled on the blockouts.

**Cost, per added character.** `-MRProfile`, same machine and setup as the MetaHuman baseline:

| | Game thread | Draw calls | Render thread at 50 | Triangles |
|---|---|---|---|---|
| **MPFB** (1 mesh, 1 material) | **0.07 ms** | **~3** | +1.1 ms | ~29k (no LODs yet) |
| Engine mannequin | 0.08 ms | ~5 | +4.0 ms | ~25k |
| MetaHuman (body + face, medium) | 0.18 ms | ~15 | +5.0 ms | LODs reduce it |

**So far MPFB wins clearly on cost:** less than half the CPU and a fifth of the draw calls of MetaHuman. GPU isn't a factor at 50. Adding auto-generated LODs would cut its triangle count too.

**Still to do before deciding:**
- **MakeHuman system asset pack (CC0):** eyes, eyebrows, eyelashes, hair, and a skin texture. The base MPFB install has none, so the face currently has no eyes or brows. Hair and brows would be card meshes skinned to `head`.
- **Deformation check in motion:** walk, run and jump, looking at the shoulders, elbows, hips and knees. Only the idle pose has been looked at so far.
- **Morph targets for creator sliders:** keep a few MPFB targets as UE morph targets instead of baking them all.
- **Visual comparison:** both candidates with the same stylized skin.

## Decision: MakeHuman (2026-10-04)
**Characters are MakeHuman (MPFB) bodies fitted to the UE5 mannequin skeleton.**
- MetaHuman is removed from the project:
  - plugins: MetaHumanCharacter, MetaHumanSDK, RigLogic, HairStrands
  - `Content/MetaHumans` and `Content/Fab` content
  - `tools/ue/make_appearance.*`
- Reasons:
  - **Cost:** MetaHuman needed about 2× the game-thread time and 3–5× the draw calls per character.
  - **Licensing:** MetaHuman content can't be committed to a public repo. MPFB output is CC0.
  - **Scope:** a 100–200 player online game doesn't need cinematic faces.
- How it's built and used is in [docs/characters.md](../characters.md).

**Character model** (planned now, so the creator only needs UI later):
- **One shared skinned body per gender.** `SKM_MPFB_Male` and `SKM_MPFB_Female` each combine body, eyes, brows and lashes in one mesh with 4 material slots.
  - Skinned meshes aren't GPU-instanced, but the asset (mesh, weights, morphs) is shared in memory.
- **Editable heads through 49 sliders.**
  - Each slider drives a decrease/increase pair of head-only morph targets: 93 morphs in total.
  - When the kit is built, eyes, brows, lashes and hair are refitted for every target, so they follow the face.
- **Hair is a swappable leader-pose part** carrying the same morph targets.
- **Colours are material parameters.**
- **Saved and replicated face:** `{body, sliders[49], hair, colours}`, about 60 bytes.

**v2 kit cost** (body with eyes, brows, lashes and textures, plus a hair part, random faces; `-MRProfile -MRRandomFaces`, same machine as the baseline):

| | Game thread per character | Draw calls per character | 50 extra characters |
|---|---|---|---|
| MPFB male kit | ~0.09 ms | ~12 (5 sections, plus shadow passes) | game 6.7 ms, render 9.4 ms, 1,125 draws |
| MPFB female kit | ~0.10 ms | ~12 | game 7.4 ms, 1,125 draws |
| MetaHuman (medium) | 0.18 ms | ~15 | |

**Cheap wins still open:**
- atlas brows and lashes into one material (−2 sections)
- auto LODs, with morphs on LOD0–1 only
- a forced minimum LOD for distant players

**Animation:**
- **Now:** CC0 Quaternius Universal Animation Library 1+2 (76 clips: locomotion, sword, shield, spells, hits, death, everyday), retargeted onto the mannequin by `tools/blender/retarget_ual.py`. They are committed, with montages for one-shot actions.
- **Later:** the Game Animation Sample's Motion Matching is still the plan for movement.
- **Mixamo is not used:** its terms forbid redistributing raw files, which a public repo would do.

