# Brainstorm: MetaHuman characters, creator, equipment and animation

- Status: Brainstorm (not a decision yet; promote to an ADR when we commit)
- Date: 2026-10-04
- Related: [ADR 0001](../adr/0001-engine-and-architecture.md), which already picks MetaHuman for characters.

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

## Open questions
- [ ] What does UE 5.8 actually offer for runtime MetaHuman customization?
- [ ] Which face LOD keeps enough expression for emotes and dialogue? Prototype one face at LOD 1, 2 and 3.
- [ ] Mutable spike: can it merge MetaHuman body and armour while RigLogic still drives the face?
- [ ] Stylization art test: one MetaHuman with a stylized skin and hair material in the Raza blockout, next to the environment art.
- [ ] Profile 50 players in Raza at the proposed LOD policy on the minimum-spec machine.
- [ ] First-person approach: separate arms mesh, or full body with the head hidden?
- [ ] Licensing: confirm the current MetaHuman licence terms for a non-commercial fan project.
