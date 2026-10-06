# ADR 0007: AI prop pipeline (sprite → 3D)

- Status: Accepted for props. Every prop in Raza and its interiors is generated and placed (2026-10-06).
- Date: 2026-10-06
- Current recipe: skill [sprite-to-3d](../../.claude/skills/sprite-to-3d/SKILL.md). This ADR is the decision log.
- Background: [docs/research/ai-monster-prop-pipeline.md](../research/ai-monster-prop-pipeline.md) (the full plan, including monsters).

## Context
Most of Raza's Kod-placed objects (ornamental objects, furniture, lamps, braziers) have no mesh. The few that do (lamp post, brazier, candle) are procedural stand-ins from `build_prop_kit.py`. We want real 3D props that are recognisably the original sprites. The plan is: upscale the sprite, optionally restyle it with an image model, generate a model in Tripo, normalise it and place it.

We work **one object at a time** until the recipe settles, then add batching.

## Decisions (2026-10-06, with the user)

### Tripo access
The Max plan's credits only work in Tripo Studio (web). The API bills a separate prepaid wallet. So:
- **Now:** the scripts prepare a handoff folder (`03_tripo_in/` + `SETTINGS.md`). The user generates in Studio and drops GLBs into `04_tripo_out/`, and the scripts take over again.
- **Later (phase 3):** a V3 API backend behind the same folders, for unattended and parallel runs, once the user tops up the wallet.
- API V2 and the Python SDK stop working 2026-10-31, so only V3 is built.

### Restyle
The user has both Gemini (Nano Banana Pro) and OpenAI (GPT Image 2) API keys. The first props are restyled with both and compared, and the winner becomes the default. Restyling is an image edit: the upscaled sprite is the identity reference, and the style bible (`tools/aigen/style/style.md`) and the manifest's `describe` are the text.

### Layout
- **Manifest per asset:** `data/aigen/<kind>/<name>.json` (committed). It holds the sprite, frame, description, mesh name, face target and variants. The pipeline records heights, restyle runs, Tripo runs and the choice.
- **Working files:** `build/aigen/<kind>/<name>/`, git-ignored. The numbered step folders are `01_upscale`, `02_restyle`, `03_tripo_in`, `04_tripo_out` and `05_review`.
- **The chosen raw Tripo GLB** goes in `art_src/aigen/<kind>/<name>/` (LFS). Unlike our other generated output it costs credits and can't be regenerated exactly, so it's kept like a hand-made source. The original-art permission covers reworked assets (ADR 0001).
- **Every step is cached** by a hash of its inputs, model and prompt, as the textures are. A paid restyle is never repeated for the same inputs.

### Scale
The model is scaled to the sprite's in-world height, `h / shrink / 64 * 2.2` m (`bgf2png.world_height_m`). Tripo's own scale is ignored. Brazier: 143 px at shrink 5 = 0.983 m. That matches `props.json`'s `fire.base_m` 0.98, so the flame flipbook still sits on the rim.

### Upscale
The same model as the environment (`4xTextures_GTAV_rgt-s_dither`), with the cut-out kept:
- transparent pixels are filled from their neighbours before upscaling;
- alpha is rebuilt from the 1-bit original (bilinear, then a slight blur and levels).

The brazier came out clean: no cyan halo, and the detail is kept. Sheet: `build/aigen/props/brazier/05_review/sheet1_restyle.png`.

### Normalise and place
- `tools/blender/prop_glb.py normalize` joins the meshes, scales to the height, puts the origin at the base centre (+Z up, metres) and exports with embedded textures to `build/environment/kit/SM_AI_<Name>.glb`.
- `props.json` classes get `"mesh_ai"`. The top-level `"ai_meshes"` switch picks them over the procedural `"mesh"` when the GLB exists (`build_world.py` `zone_props`).
- The imported glTF material is kept for now. A shared master-material pass comes later.

## Brazier (first object)
- Sprite `brazier.bgf` frame 0 is the unlit brazier (frames 1–6 add flames). It shows a copper bowl held by iron straps with square lugs, on a single slender iron stem and a round foot. **The procedural `SM_Brazier` is a tripod**, so the original's shape was never matched.
- Variants: A (raw upscale), B (Gemini restyle), C (OpenAI restyle), each a single image to Studio.
- Look-dev bookmark `brazier_close` (the brazier by the crypt entrance in Raza).
- **Restyle (2026-10-06):** four image-edit models on the same input and prompt. Sheet: `build/aigen/props/brazier/05_review/sheet1_restyle.png`.

  | Variant | Model | Time | Read |
  |---|---|---|---|
  | C | OpenAI `gpt-image-2.5-sunburst` | 29 s | **Closest to the sprite**: proportions, hammered bowl, straps and lugs, the collar, the plain domed foot |
  | B | Vertex `gemini-3-pro-image` (Nano Banana Pro) | 25 s | Very close. It invents a scalloped edge on the foot |
  | D | fal `flux-pro/kontext` | 7 s | Close, but smoother and less detailed. Cheapest and fastest |
  | E | fal `qwen-image-edit` | 152 s | Glossier copper and a cast shadow, despite the prompt. Weakest |

  All four keep the identity. Whether the extra detail of C and B survives into Tripo is the next test.
- **Tripo (2026-10-06):** Studio HD Model, 2K PBR, Remove Lighting, 8000 tris, single image. Sheet: `build/aigen/props/brazier/05_review/sheet2_tripo.png`.

  | Variant | Input | Model | Read |
  |---|---|---|---|
  | **C** | OpenAI restyle | H3.1 (45 cr) | **Chosen.** Crisp straps, lugs and collar, broad round foot, clean bowl |
  | C_h30 | OpenAI restyle | H3.0 (30 cr) | Almost the same, slightly simpler. Good enough for clutter |
  | B | Vertex restyle | H3.1 | Good, but keeps the restyle's invented scalloped foot |
  | A | Raw upscale | H3.1 | Soft and lumpy. **The restyle step is worth it** |

  All four came out at 7.3–7.5k tris and about 1 m tall. C was normalised to 0.983 m: `build/environment/kit/SM_AI_Brazier.glb`, kept as `art_src/aigen/props/brazier/brazier_tripo.glb`.

- **In game (2026-10-06):** `ai_meshes` on vs off, by day (13:00) and at night (21:00). Sheets: `build/lookdev/compare_brazier_ai_h13_brazier_proc_h13.png` and `…_h21….png`.
  - The AI brazier reads as the original: a single stem on a round foot, a copper bowl with iron straps, the right size. The flame flipbook sits on the rim.
  - The procedural tripod never matched the sprite. **Recommended: keep `ai_meshes` on** (it's on now).
  - Open: at night the bowl's inside glows almost white, because the fire light (`offset_m` 1.2) sits about 22 cm above the rim. Raise it or dim it if it bothers.

### Automating Studio (2026-10-06)
The user wants to stay out of the loop. Claude drives Tripo Studio in the user's Chrome (signed in; web credits) and collects each model over the **Tripo DCC Bridge** into the user's open Blender. `tools/aigen/blender_link.py` talks to Blender through the community "MCP for Blender" add-on's socket (port 9876), exports the arrival and checks its Tripo task id in the bridge's log. The recipe is in the skill's reference/studio-automation.md.

The bridge connection drops when the Studio page reloads, so it's re-toggled before each send. Studio runs jobs in parallel and has a batch mode (30 images), which covers parallel runs until the API backend exists.

### Restyle providers
- **Vertex AI** (`VERTEX_API_KEY`, postpaid Google Cloud) runs the Gemini image models. The user's Google Developer Program monthly credits pay for it; AI Studio's prepaid billing can't use them. The `gemini` provider (AI Studio) stays for completeness.
- **fal.ai** (`FAL_AI_API_KEY`) gives one key for the open-weight edit models (the later style-LoRA route) and other 3D generators to compare with Tripo.

## Raza batch (2026-10-06)
**The user's decisions after the brazier:**
- Drop the procedural props altogether; AI meshes only.
- Keep the OpenAI restyle (variant C) and stop calling the other image models.
- Make every prop in Raza.
- Dim all added lights (ADR 0005, `light_scale` 0.3).

**Inventory:** `tools/aigen/inventory.py` → `data/aigen/inventory.json`.
- 61 props: Raza outdoors, the interiors, and 30 museum miniatures in 308.
- Also 7 NPCs, the cows (monster), and logic objects (room lights, food dispensers, news links).
- `modteleport` (a magic swirl) is an effect, not a mesh, so it was skipped.
- Overview sheets of every frame: `build/aigen/overview_*.png`.

**Manifests:** one per prop, with `describe` written from each upscale. Frame choices:
- weapons: their upright frame, sized by the lying frame's width (`size_frame`, `size_by: length`);
- wand: its diagonal frame (the lying one is 11 px tall);
- chandelier: hangs 0.6 m up (`lift_m`, from the empty rows under its sprite).

**Restyle through the OpenAI Batch API:** 60 edits in one job (`aigen.py all batch-submit` / `batch-collect`) at half price. It finished in about 30 minutes.
- One miss: the egg basket's eggs. It was redone directly with a firmer `describe`.
- Overview: `build/aigen/overview_openai_front.png`.

**Tripo:** 60 single-image jobs, all H3.1 with the brazier's settings (plus AI Complete), 2,700 credits; 22,280 left of the month's 25,145.
- Claude uploaded each and recorded its task id from the URL. Jobs ran in parallel; all 60 finished within about 15 minutes.

**Collect:** `aigen.py bridge-collect all` matched every arrival in Blender by task id.
- Sends: Studio in Chrome, switching models through the Assets grid's router links (no reload, so the bridge stays up), then Export → Send To → Blender.
- Lessons:
  - the window has to be visible, or the popovers don't open;
  - a page reload drops the bridge;
  - sends made before the model finished loading were silently lost (10 of 60), so we resent them.
- All 60 arrived.

**Placement:**
- `props.json` now has `"classes"` and `"types"` (OrnamentalObjects by OO number), all `SM_AI_*`.
- `build_world.py` `zone_props()` skips meshes not generated yet, and turns props by their Kod angle (`(angle − 1024) / 4096` of a turn, since glTF +Z lands on UE +Y). Entries with `random_yaw` get a stable random turn.
- `build_prop_kit.py` now makes only the effect meshes (rain, smoke).

**Weapons lie flat:** the sword, scimitar, hammer and wand were generated from side views and stood on their edge. The manifest's `lay_flat` makes `normalize` turn them face-up on the ground.

**Material: `M_PropTextured`.** Imported as-is (Interchange's glTF material), the props went near-black inside while the walls around them were lit. Our surface masters add an ambient floor in interiors (sector light × `MPC_Environment.SectorAmbient` × `AmbientTint`), and the imported material had none.
- `environment_materials.py` `build_textured_prop_master` takes the glTF textures (base colour, normal, metallic-roughness) and adds:
  - that ambient floor (`SectorLight`, 1 by default);
  - matte specular;
  - the weather response (wet, snow).
- `build_world.py` gives every `SM_AI_*` mesh an `MI_AIProp_<mesh>` of it.

**Look-dev:**
- Before and after by day: `build/lookdev/compare_props_before_props_final_h13.png`.
- Summary sheets: `build/lookdev/sheet_props_final_h13.png` and `_h1.png` (night).
- 3D overview of every model: `build/aigen/overview_05_review_normalized_front.png`.
- The smoke test passes (13 zones, all ready).
- One look-dev bookmark (`inn_front`) now stands half a metre behind Raza's welcome sign. The sign is the original's; move the camera if that view matters.

## Trees (2026-10-06)
**Problem.** Tripo makes one closed surface, so its trees (`SM_AI_Midtree2`, 30 in Raza) came out as a solid green lump with leaves painted on. Normals follow every bump, light doesn't come through, nothing moves in the wind. Good game trees are a trunk mesh plus many small alpha-cut leaf cards. So trees get their own pipeline, and the user picks one from look-dev. Two candidates, compared against the AI tree in game:

**A. Procedural Blender kit** (fully scripted, ours to commit):
- `python tools/textures/make_tree_textures.py`: `T_TreeLeaves_Mid` (a 2x2 atlas of leaf clusters; every leaf a pointed oval filled with a patch of `midtree2`'s own painted canopy) and `T_TreeBark_Mid` (tileable streaks in the sprite trunk's colours), into `build/textures_placeholder/`.
- `blender -b --factory-startup -P tools/blender/build_tree_kit.py`: `SM_Tree_Mid_A/B/C` (seeded, about 3,700 triangles). Sized off the sprite: 4.92 m tall, clear trunk to 1.41 m, a 1.72 x 1.78 m ellipsoid crown with a few bulges. Tapered bark tubes for the trunk, limbs and twigs. About 360 folded leaf cards face outwards with scatter.
- What makes it read as a tree:
  - leaf normals come from the crown ellipsoid, so the crown shades as one soft volume like the painted original;
  - cards are doubled in geometry rather than in the material, so their backs keep those normals;
  - vertex colour R is the wind weight, G per-card variation, B crown occlusion.
- `environment_materials.py` `build_tree_master`: `M_TreeLeaves` (masked, Two Sided Foliage shading, `Transmission` tint, season tint, weather) and `M_TreeBark`. Both share `TREE_WIND_HLSL`: a downwind sway in slow gusts plus leaf flutter, scaled by `MPC_Environment.Wind`. `build_world.py` gives `SM_Tree_<Kind>_*` meshes `MI_Tree_<Kind>_Bark/Leaves`.

**B. Unreal's Procedural Vegetation Editor (PVE)** (`Engine/Plugins/Experimental/ProceduralVegetationEditor`, enabled for the comparison and disabled again after it):
- A node graph (a PCG graph) that grows a tree from a sample species and exports a skeletal mesh (Dynamic Wind bones) or a static mesh, with the leaves as Nanite assembly parts.
- **Not scriptable.** The graph is protected from Python, and the export button (`FPVExporter`, editor-private) has no API. Making our own PVE tree means editing and exporting by hand in the PVE editor. `/Game/Generated/Trees/PVE_Mid` is a copy of `PVE_Sample_Deciduous_Tree_01`, ready for that.
- For this comparison the `pve` option places the plugin's own exported `PVE_Deciduous_Tree_01` (a skeletal mesh 11.4 m tall), scaled to 4.92 m (`fit_height_m`).
- **Leaves need Nanite Foliage.** Without `r.Nanite.Foliage=1` (set in `DefaultEngine.ini` for the comparison, read-only at startup, needs a shader recompile; removed again) the tree rendered as bare branches: `build/lookdev/trees_v3/trees_grove.png`.
- Wind: the Dynamic Wind plugin is driven through `UDynamicWindSubsystem::UpdateWindParameters`. Nothing in the game calls it yet.
- Licensing: its meshes and textures are Epic sample content. They stay in the plugin or git-ignored `/Game/Generated/`, never in git.

**Switch.** `props.json` `"192"`:
- `"mesh_options"` lists `ai`, `blender` and `pve` meshes. A kit GLB name or a UE asset path; a stable random variant per tree.
- `"mesh_use": "compare"` cycles the options over the zone's trees, so they stand side by side. `build_world.py` `prop_mesh_name()` does this.
- Set `"mesh_use"` to one option to choose. `prop_mesh_name()` also takes UE asset paths, scaled by `"fit_height_m"`, and `build_zone_level()` places skeletal meshes; both stay for later use.

**Look-dev cameras:** `trees_grove`, `trees_close`.
- Blender kit vs AI: `build/lookdev/trees_v2/`.
- All three, PVE without leaves (Nanite Foliage off): `build/lookdev/trees_v3/`.
- **All three, the sheet to judge:** `build/lookdev/trees_compare_v9.png` (`trees_v9/`, captured with `-Settle 6`). By then the Blender kit had 460 smaller cards (0.68–0.98 m), `Brightness` 0.85 and a lower `Transmission` (0.22, 0.32, 0.12).
- Capture traps met on the way:
  - right after the shader recompile the PVE leaves came out near-black (`trees_v4`: textures still streaming);
  - auto-settled captures right after a rebuild showed low mips everywhere (`trees_v7`); `-Settle 6` fixed that;
  - the Blender leaves stayed on a low mip even then: streaming misjudged the small cards' texel density, and the alpha cut turned the leaves into blobs (`trees_v8`). `tree_materials()` now sets the leaf atlas `never_stream` (1024 px, cheap).

**Decision (2026-10-06, the user): the Blender kit.**
- `"192"` `"mesh_use": "blender"`. The `pve` option, the PVE plugin and `r.Nanite.Foliage` are gone again.
- The AI tree stays as the `ai` option.
- The user's notes on the kit, and the fixes:
  - **The trunk's foot slid with the sway.** Cause: the GLB had two colour sets. A bmesh byte-colour layer isn't the mesh's active one, so the exporter (`export_vertex_color="ACTIVE"`) wrote a white `COLOR_0` and our data as `COLOR_1`. Unreal reads `COLOR_0`, so every vertex had wind weight 1, no crown occlusion, and the bark fluttered.
  - `build_tree_kit.py` now writes a float colour layer and exports it by name as the only `COLOR_0` (`export_vertex_color="NAME"`, `export_all_vertex_colors=False`).
  - `TREE_WIND_HLSL` also bends with the square of the vertex's height in the mesh (`LocalPosition.z / HeightCm`, 490), so the foot stays planted whatever the colours say.
  - **The leaves looked shiny in direct sun.** `M_TreeLeaves` now has `Specular` 0.05 and `Roughness` 0.95: still lit, but diffuse. Wet leaves still gloss.
  - **With real vertex colours the crowns went about 40% darker.** The white set had given every card the top colour variation and switched the crown occlusion off. Retuned: `Brightness` 1.3, `AoMin` 0.5 (crown average about 58 in green against v9's 81, with more depth). Sheet: `build/lookdev/compare_trees_v9_trees_v13.png`.
  - **`M_PropTextured` failed to compile** after the shader recompile, so every AI prop drew the grey default (the shrub in `trees_v10`). Cause: its `MetallicRoughness` default was the engine's sRGB `WhiteSquareTexture` on a Linear Color sampler. `default_orm()` now makes a linear `T_DefaultORM` (roughness 1, metal 0) as the default.
- `build_grass_kit.py` had the same white `COLOR_0`. Fixed the same way at the user's request (ADR 0003 "Grass vertex colours").

**Shrub and dead tree (2026-10-06, the user).** Both now come from the kit too (`props.json` `"198"`, `"91"`: `mesh_use` `blender`, the AI meshes kept as `ai`).
- `SM_Tree_Shrub_A/B/C` (`shrubee1`, 2.26 m): the leafy generator with the crown ellipsoid dipped below the ground, so the dome meets it wide, as the sprite's does. A hidden stem. A finer leaf atlas (230 small leaves per cluster) cut from the shrub's own canopy; bark from `midtree2` (`trunk_bgf`; the shrub shows none).
- `SM_Tree_Dead_A/B` (`nectree1`, 3.76 m): a new bare generator (`bare_tree`).
  - a flared, wandering trunk forks at about a third of the height into 4-6 thick limbs;
  - the limbs branch four times (2-4 children, 22-55 degrees, crooked by `gnarl`), and every branch tapers almost to a point, so no cut ends show;
  - scaled to the sprite's height. Bark only (`T_TreeBark_Dead` from the sprite's trunk); `tree_materials()` handles a tree without leaves.
- `build_tree_kit.py -- Shrub Dead` builds only the named kinds.
- Sheets: `build/lookdev/plants_v2/dead_tree.png`, `trees_grove.png` (the shrub). Blender previews: three of each.

**Dead tree back to AI (2026-10-06, the user).** The user prefers the Tripo `SM_AI_Nectree1`: `"91"` `mesh_use` `ai`. Rule for now: the kit makes leafy trees and shrubs; static leafless trees stay AI props. `SM_Tree_Dead_*` and `bare_tree` stay as the `blender` option.

**Slower wind (2026-10-06, the user: "constantly moving a little too fast").** Every frequency is halved:
- trees: `TREE_WIND_HLSL` `Rate` 0.5 (sway, gusts and flutter);
- grass: `materials.json` `grass` `wind_speed` from 0.3 to 0.15.

The amplitudes are unchanged.

## Creatures: 4 views and Smart Mesh (2026-10-06)
**Problem (the user).** The museum miniatures of monsters (the mutant ant `modant`, the centipede `modcen`, the fungus beast `modfung`) "don't look quite right". The user asked for a test of Studio's multi-view input and its **Smart Mesh** mode, and of higher and lower polygon counts.

**What was run** (task ids and settings in each manifest's `tripo.runs`; log `build/aigen/creature_test/tasks.tsv`):
- **Side views:** left, back and right from OpenAI (`restyle.views`), 3 per creature.
  - The first centipede left and back views were near-copies of its front. `restyle.VIEWS` now asks for "a true side profile", says which way the front points, and says it must not copy the front. The centipede's second set turned properly.
- **MV:** HD Model H3.1 with the 4 views, our usual settings (8000 triangles, 2K PBR). 45 credits, the same as a single image.
- **Smart Mesh P2.0:** one run makes up to 4 generations, each with its own polygon count. We used 2k, 5k, 10k and 20k triangles.
  - The ant was run from its single image; the centipede and fungus from the 4 views.
  - 100 credits a run (the first two runs were free trials). Each generation takes under a minute.
  - Smart Mesh models come **untextured**. Studio's Texture tool adds one: 2K for 10 credits, 4K, or 8K for 30 (the ant's 10k used the one free 8K trial).
- Spent: 265 Studio credits (22,280 → 22,015), plus 12 OpenAI images.

**Results** (sheets: `build/aigen/creature_test/modant_compare.png`, `modcen_compare.png`, `modfung_compare.png`, `polycount.png`; every variant in each asset's `05_review/sheet2_tripo.png`, now with mesh renders):
- **A single image was the problem.** From the front, C matches the sprite. From the side, the ant is a thin flat cut-out and the fungus beast leans forward like a blob. The centipede's coil was already plausible.
- **4 views fix it.** The MV ant has a real thorax and abdomen behind the head; the MV fungus beast stands upright on its root legs. HD MV keeps the most painted detail (the fungus's teeth and roots).
- **Smart Mesh** comes close to HD MV in form, at fewer triangles and with cleaner, even topology (the mesh renders show it). Even from a single image its ant had real depth. Its textures are a little softer.
  - Each polygon count is its own generation, so the shape varies between them (the centipede's 5k grew a long tail spike).
- **Polygon count**, for figures under 1 m:
  - 2k loses features: the ant's mandibles thin out, and the fungus loses its teeth and most of its roots;
  - 5k holds the overall shape;
  - 10k brings back the fine parts (teeth, roots, leg joints);
  - 20k adds little you would see at these sizes.
- **Cost per asset:** HD MV is 45 credits. Smart Mesh is 100 per run plus 10 per texture, unless all 4 generations go to use.

**The original side views were not used.** The user remembered that the originals have side views, and they do: every museum figure's sprite is drawn from 8 directions (6 bitmaps in the group `[0, 1, 2, 3, 3, 3, 4, 5]`: front, front three-quarter, side, back three-quarter, back). So do `box1` and `helm`; `modportal` and `modthron` have 8 and 7 bitmaps.
- The test's left, back and right views were instead invented by OpenAI from the front. The original angles show what it got wrong: the centipede's real side is a long S-curve ending in a tail spike, and the fungus beast's back is a plain cap.
- Next: restyle each side view from its own original frame, not from the front alone.

**Decision (proposed, awaiting the user):**
- Creatures, figures and anything with legs or an unseen back get 4 views (`restyle.views` all four, `tripo.variants` `MV`). Plain symmetric props keep one image.
- HD H3.1 with 4 views stays the default for props.
- Smart Mesh (10k, 2K texture) is the candidate for monsters that will be rigged and animated, where clean topology matters.
- The three test assets keep C in game until the user picks; `aigen.py <name> choose MV` then `normalize` swaps one.

**Pipeline fixes from the test:**
- `blender_link.collect` found each arrival in Blender by Tripo's model name. Tripo reuses names ("alien creature 3d model" for every fungus), and Blender gives a second copy a `.001` suffix. Two things went wrong:
  - the collector skipped any name it had already filed, so the first sends of two models were ignored;
  - a later arrival was filed under the wrong task (the 5k fungus file was really the 10k).

  It now takes the exact name each import ended up with from the bridge log ("Renamed imported root: ... -> 'x.001'"). A resend of a model already collected is deleted from the scene.
- `prop_glb.py preview --wire` renders the geometry (grey, edges drawn). `tripo-ingest` adds "mesh above" and "mesh front" columns to `sheet2_tripo.png`, so untextured Smart Mesh models can be judged too.

## Smart Mesh props (2026-10-06)
**Question (the user).** Would Smart Mesh make better static props in game than the HD models we placed?

**What was run** (task ids and Studio face counts in each manifest's `tripo.runs.SM*`; log `build/aigen/sm_props_test/runs.tsv`):
- 10 props the look-dev cameras show: `brazier`, `lamp`, `table`, `barstool`, `stool`, `chandelr`, `box1`, `tallurn`, `sign`, `rockc`.
- One Smart Mesh P2.0 run each, from the same single image as their HD model (`03_tripo_in/C_front.png`). 4 generations at 2k, 4k, 8k and 16k triangles (`SM2k` … `SM16k`).
- The 4k and 8k were textured (2K, Remove Lighting). The 2k and 16k stay untextured, for judging the mesh.
- Spent: 1,200 Studio credits (10 runs × 100 + 20 textures × 10; 22,015 → 20,815).
- In game: `aigen.py <name> normalize SM8k` builds `SM_AI_<Name>_SM8k` beside the chosen mesh. `props.json` gives each of the 10 entries `"mesh_options"` `hd` / `sm8k` / `sm4k`. One world build and capture per option at 13:00, on 10 cameras: the props' own, plus 3 new bookmarks `prop_box`, `prop_rock`, `prop_sign`.

**Results:**
- Sheets: `build/aigen/sm_props_test/compare_1.png` and `compare_2.png` (renders), `polycount_1.png` and `polycount_2.png`; in game `ingame_outdoor.png` and `ingame_interior.png` (HD | SM 8k | SM 4k); `rock_back.png`.
- **In game the difference is small** at normal distances. Brazier, lamp, bar stool, stool, chandelier and urn look all but the same.
- Smart Mesh's textures are a little crisper: the table's grain, the sign's carved runes.
- Its meshes are cleaner: an even radial table top against HD's scattered triangles.
- **4k looks as good as 8k** for every one of these props, at half the triangles of the HD models (about 7,500).
- **Smart Mesh reinterprets more:**
  - the crate (`box1`) became a chest with a curved lid, where HD kept the sprite's flat crate;
  - the sign became separate planks, closer to the sprite than HD's solid block.
- **Each generation invents its own back.** The rock's 8k has a round plate on its back, which a random turn shows to the camera. Its 4k is a proper rock. So the review sheets now render the mesh from behind (`tripo-ingest`: "mesh above", "mesh back").
- Cost: HD 45 credits per prop for one try. Smart Mesh 100 per run for 4 tries at different counts, plus 10 per texture.

**Recommendation (awaiting the user):**
- Keep HD H3.1 as the prop default. Smart Mesh doesn't look better enough to redo Raza: about 6,600 credits for 60 props.
- Smart Mesh 4k is the choice where triangles matter, and for monsters.
- For the 10 tested props the meshes exist, so switching costs nothing: `sm4k` for all but the crate (keep `hd`).

**Pipeline additions:**
- `aigen.py <name> normalize <variant>`: a variant's raw GLB → `SM_AI_<Name>_<variant>.glb`, recorded in `normalized_variants`.
- Studio: wait about 5 s after an upload before Generate; a click while the image is still uploading does nothing (and costs nothing).

## Custom models, polycount 1000, original angles (2026-10-06)
**Decisions (the user):**
- **HD H3.1 stays the default for every asset.**
- **Custom models:** an optional per-asset override shown in game, with the HD model kept as the fallback.
  - `props.json` `"mesh_custom"` (a kit GLB name or UE asset path), or a manifest's `"custom"` model: `aigen.py <name> custom <variant>` keeps that Tripo output as `art_src/aigen/<kind>/<name>/<name>_custom.glb`, and `normalize` builds it as `SM_AI_<Name>_Custom`.
  - `build_world.py` `prop_mesh_name()` places the custom mesh when its file exists, else the HD `"mesh"`. `custom none` removes it.
- **Polycount default 1000 triangles** (`tripo.DEFAULT_POLYCOUNT`); a manifest's `"polycount"` overrides it. The old `face_target` field is gone. Already generated props keep their 8000-triangle models until regenerated.
- **Original angle frames are used whenever the sprite has them.**
  - `sprite.angle_frames()` reads the 8-direction group (`[0, 1, 2, 3, 3, 3, 4, 5]`: front, side, back, other side) and the sprite step upscales each view on one shared canvas scale (`01_upscale/<view>_grey.png`; the manifest records `"angles"`).
  - `restyle` then redraws each view from its own original drawing, with the approved front restyle as the colour reference (`restyle.prompt(own_view=True)`: keep that viewpoint). Side views wait one round for a fresh front, so a batch takes two rounds.
  - Without a manifest `tripo.variants`, `tripo-prepare` makes a 4-view variant `MV` when angles exist, else `C` (`tripo.default_variants`).
  - In Raza: the ten museum figures, `box1` and `helm` (12; the throne has sides but no back).

**Test** (sheets in `build/aigen/sm_props_test/`):
- `hd1k_compare.png`: 9 props at HD 1000 (`HD1k`) against their 8000 models. In game: `ingame_outdoor_new.png`, `ingame_interior_new.png` (8000 left, 1000 right).
  - At game distances 1000 holds up for lathe-turned and simple shapes: brazier, lamp, table, bar stool, stool, chandelier, urn, sign. The stool's legs and the chandelier's ring stay whole.
  - The rock loses its craggy relief and reads as a smooth lump; rocks want a higher `polycount`.
- `views_orig_1.png`, `views_orig_2.png`: the 12 assets' restyled views next to the original drawings. The side and back views now follow the original (the centipede's S-curve and tail, the fungus beast's plain cap, the throne's profile).
- `mv1k_compare_1.png`, `_2.png`: HD with the 4 original-angle views at 1000 (`MV1k`) against the single-image models in game. In game: `ingame_museum_new.png` (new cameras `museum_bugs`, `museum_figures_l`, `museum_figures_r`, `museum_throne`, `museum_portal`).
  - Better: the ant, centipede, fungus beast, lupogg, orc, throne and portal have real sides and backs at under 1000 triangles.
  - Failed: the baby spider collapsed into a lump at 1000 (thin legs need triangles); with `"polycount": 4000` it came out right (`MV4k`, 3639). The helm came out as a flat sheet at both 1000 and 4000: the original's side and back drawings look nearly the same as the front with the mail drawn as a flat curtain. Keep its single-image model.
  - The crate became a chest with a domed lid again; keep its HD model.
  - Not run: `modavar` and `modfey`. Studio asks for an age confirmation ("content that may be sensitive") on these figures' images, which Claude doesn't answer; the user can generate them by hand.
- Spent: about 1,080 Studio credits (20,815 → 19,735: 11 HD1k jobs, 12 multi-view jobs, 2 at 4k) and 45 OpenAI images over two batches.
- Two HD jobs (brazier, sign) hung on "Generating" for 25 minutes; they were left running and resubmitted. Studio had not charged a failed or blocked submission.

**State:** the game shows HD everywhere; the test models are built as `SM_AI_<Name>_HD1k`, `_MV1k`, `_MV4k` and can be switched on per prop with `"mesh_custom"`.

## Next run: settings and where we stopped (2026-10-06)
Implementation is paused here; the next session picks up from this list.

**Manifest properties for the next automated run** (`data/aigen/<kind>/<name>.json`; code in `tools/aigen/tripo.py`):
- **`polycount`**: triangles to ask Tripo for. **Default 4000** (`tripo.DEFAULT_POLYCOUNT`), half the 8000 the Raza batch used. Set it per asset to go up or down: 1000 held up for lathe-turned props, rocks and thin-legged figures want 4000 or more.
- **`model`**: how the asset's mesh is made, one of:
  - `"hd"` (the default): HD Model H3.1, 45 credits. Variants `C` (one image) or `MV` (4 views).
  - `"smart_mesh"`: Smart Mesh P2.0, then a 2K texture in Studio's Texture tool. 100 credits a run plus 10 per texture. Variants `SM` / `SMMV`.
  - `"custom"`: no Tripo run; the manifest's `"custom"` model is the mesh.
  - Whatever the model, a `"custom"` model, once kept, is what the game shows (`SM_AI_<Name>_Custom`, with `SM_AI_<Name>` as the fallback). `props.json` `"mesh_custom"` can also name any mesh directly.
- **`retopology`**: an optional extra step after generation, using Studio's Retopo tool (left bar). `true` uses the asset's polycount and triangles; `{"polycount": n, "topology": "quad"|"triangle"}` sets them. The settings card (`03_tripo_in/SETTINGS.md`) then lists the step. **Set on no asset yet.** Untested: whether Retopo costs credits, keeps the texture or needs a re-texture, and how its output reaches the bridge. Test it on a few static props first.
- `tripo-prepare` picks the variants from these when a manifest names none (`tripo.default_variants`): 4 views when the sprite has its own angles, else one image.

**Where we are:**
- In game: the 61 Raza props at HD 8000 (the brazier, the 60 of the Raza batch). No `mesh_custom` set.
- Built and ready to switch on (kit meshes `SM_AI_<Name>_<variant>`, raw GLBs in each asset's `04_tripo_out/`), from the tests above:
  - HD 1000 (`HD1k`): brazier, lamp, table, barstool, stool, chandelr, tallurn, sign, rockc;
  - original angles, 4 views, 1000 (`MV1k`): modant, modcen, modfung, modlupog, modorc, modportal, modthron, box1;
  - `MV4k`: modspdr. Smart Mesh `SM4k`/`SM8k`: the 10 static props.
- Recommended, awaiting the user: the `MV1k` creatures, throne and portal, `MV4k` for the spider, `HD1k` for the brazier, lamp, table, stools, chandelier, urn and sign; keep HD for the rock, crate and helm. Keep each with `aigen.py <name> custom <variant>` then `normalize`.
- Not generated: `modavar`, `modfey` (Studio's age check on upload); their restyled 4 views are ready in `02_restyle/`.
- Studio credits: 19,735 left.

**Next steps:**
1. The user's picks above; keep them as custom models.
2. Try `retopology` on 2-3 static props (a table, the crate, a rock) and compare topology and textures.
3. Regenerate the rest of Raza with the new defaults (4000, original angles where they exist): about 45 credits a prop.
4. The API backend (V3) when the wallet is topped up, so runs need no browser.

## Open
- Trees: the Outskirts tree lines (ADR 0003 2g).
- Emissive parts (lamp glass, embers) on AI meshes: a second material slot or a mask.
- The API backend (phase 3), once the user tops up the API wallet; Studio plus the bridge covers batches until then.
- Collision on props (they're NoCollision today).
- Side views from the original angle frames (`bgf2png` groups of 8; the 12 Raza props that have them are the museum figures, `box1`, `helm`, `modportal` and `modthron`) instead of OpenAI's guesses.
- Custom models: the user's pick per prop from the 1000-triangle and original-angle tests (`mesh_custom`, or `aigen.py <name> custom <variant>` to keep it in `art_src/`).
- Static props: the user's pick per prop between `hd`, `sm8k` and `sm4k` (`props.json` `mesh_use`, the 10 tested props). After that, drop the losing options.
- Creatures: the user's pick between C, MV and Smart Mesh for the museum figures; the other museum figures (`modavar`, `modorc`, `modfey`, `modlupog`, `modspdr`) with 4 views.
- `SectorLight` per prop from the original sector light where it stands (1 now: props read slightly brighter than the walls indoors).
- The night glow of lamp glass and other emissive parts on AI meshes.
