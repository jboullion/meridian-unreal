---
name: sprite-to-3d
description: The current recipe for turning original Meridian 59 sprites (.bgf) into 3D assets in Unreal Meridian - inventory the zone's objects, extract and upscale sprites, restyle with OpenAI image edit (batch API), image-to-3D in Tripo Studio (driven in Chrome, collected over the DCC Bridge into Blender), normalise, place through props.json and judge in look-dev. Use when making or replacing props (lamps, braziers, furniture, ornamental objects, plants, rocks, museum pieces), later monsters, or when touching tools/aigen/, data/aigen/, tools/blender/prop_glb.py, props.json "classes"/"types"/"light_scale", or anything Tripo.
---

# Sprite to 3D

How to take a zone's objects from their original sprites to placed 3D meshes. This file is the **current recipe**. The experiments and reasons live in [ADR 0007](../../../docs/adr/0007-ai-prop-pipeline.md) (the decision log). The wider plan (monsters, rigs, style LoRA) is in [docs/research/ai-monster-prop-pipeline.md](../../../docs/research/ai-monster-prop-pipeline.md).

More detail, loaded when needed:
- [reference/studio-automation.md](reference/studio-automation.md): Claude driving Tripo Studio in Chrome and collecting models over the DCC Bridge
- [reference/tripo.md](reference/tripo.md): Studio vs API, V3 endpoints, model versions, credits
- [reference/prompts.md](reference/prompts.md): how the restyle prompt is built, models, writing `describe`
- [reference/pitfalls.md](reference/pitfalls.md): known traps (symptom → cause → fix)

**Status (2026-10-06):** every prop in Raza and its interiors (61: the brazier plus 60) has been generated. The procedural kit is retired. Creatures and Smart Mesh were tested (ADR 0007). Since 2026-10-06: polycount 4000 by default, original angle frames as side views, model type hd / smart_mesh / custom, optional retopology. **Paused:** where we stopped and the next steps are in ADR 0007 "Next run". Update this file when a default changes, and record results in ADR 0007.

**World batch (2026-10-08):** every other static prop placed in any room: 67 (62 props plus the 5 leafless trees, which the user wants from Tripo), front image only (`C`), polycount 2000 (1000 for simple shapes, 4000 for the bare trees). Found by `inventory.py --world`; manifests drafted by `--manifests`. Leafy trees and shrubs went to the Blender kit instead (ADR 0007 "World batch").

## Current defaults

| Step | Default | Why (ADR 0007) |
|---|---|---|
| Pick | `tools/aigen/inventory.py` → `data/aigen/inventory.json`: every placed object → sprite, name, kind (prop / npc / monster / logic), count per zone. `--world` reads every room's Kod instead (`data/aigen/inventory_world.json`: kinds prop / tree / npc / monster / item / effect / logic, frame counts, static, done) and `--manifests` drafts a manifest per static prop not done (front only, polycount by shape) | "Raza batch" |
| Unit of work | One manifest per asset, `data/aigen/<kind>/<name>.json`. Working files go in `build/aigen/<kind>/<name>/`. Steps take a name, `a,b,c` or `all` (every manifest not `"status": "done"`) | "Layout" |
| Sprite frame | The frame that shows the object plainly: unlit, upright, front. Weapons generate from their upright frame (2) but are sized by the lying frame (`size_frame` 0). The wand uses frame 0 (diagonal); its frame 1 is an 11 px sliver | "Raza batch" |
| Size | From the frame's **visible** pixels: `height_m`, `width_m` and `lift_m` (empty rows below, e.g. the hanging chandelier). Items lying down use `"size_by": "length"` | "Scale" |
| Upscale | 4x `4xTextures_GTAV_rgt-s_dither`, alpha rebuilt from the 1-bit original | "Upscale" |
| Restyle | **OpenAI `gpt-image-2.5-sunburst` only**, via the **Batch API** (50% off, ~30 min for 60): `aigen.py all batch-submit`, then `aigen.py batch-collect`. Single fixes run directly with `aigen.py <name> restyle --force` | "Restyle", "Raza batch" |
| Tripo | Studio (Max-plan credits), driven by Claude in Chrome. HD Model **H3.1**, Ultra Mesh + AI Complete, 2K PBR, Remove Lighting, triangles, **Private**, 45 credits each. Jobs run in parallel. Polycount **2000** (`tripo.DEFAULT_POLYCOUNT`, since 2026-10-08); a manifest's `"polycount"` overrides it (1000 for simple turned props: jugs, vases, barrels, skulls; rocks, thin legs and bare branches want 4000). The Raza batch used 8000, the tests of 2026-10-06 4000 |
| Model type | Manifest `"model"`: `"hd"` (default), `"smart_mesh"` (Smart Mesh P2.0 + 2K texture, variants `SM`/`SMMV`) or `"custom"` (no Tripo run; the kept custom model is the mesh). The settings card in `03_tripo_in/SETTINGS.md` follows it | "Next run" |
| Retopology | Optional manifest `"retopology"`: `true` or `{"polycount": n, "topology": "quad"}` adds Studio's Retopo step to the settings card. On no asset yet; untested | "Next run" | "Brazier", "Raza batch" |
| Side and back views | The world batch pins every asset to its front (`restyle.views: ["front"]`, `tripo.variants` `C` only), to judge before paying for side views. **From the original sprite whenever it draws them** (8-direction groups; `sprite.angle_frames`, manifest `"angles"`): the sprite step upscales each, `restyle` redraws each from its own drawing (two batch rounds: fronts, then sides), `tripo-prepare` makes the 4-view variant `MV`. Without original angles, 4 views guessed from the front only for creatures (`restyle.views`); otherwise one image. Check the views on a sheet first | "Custom models, polycount 1000, original angles" | "Creatures" |
| Smart Mesh | Studio's low-poly mode (P2.0): 4 generations per run at your polygon counts, 100 credits a run, untextured; the Texture tool adds 2K for 10. Cleaner topology at fewer triangles: the candidate for monsters to rig. Polycount: 2k loses features, 5k holds the shape, 10k keeps teeth and joints. For static props 4k looks like HD's 8000 in game but reinterprets more (crate → chest) and each generation invents its own back: check the mesh-back column | "Creatures", "Smart Mesh props" |
| Collect | `aigen.py bridge-collect all` waits in Blender and files each arriving model by its **Tripo task id** and the exact name it got in Blender (both from the bridge log). Send them from Studio in any order. `tripo-ingest` renders each variant textured and as a mesh (`05_review/sheet2_tripo.png`) | "Automating Studio" |
| Kept source | The chosen raw GLB, `art_src/aigen/<kind>/<name>/<name>_tripo.glb` (LFS) | "Layout" |
| Comparing in game | `aigen.py <name> normalize <variant>` → `SM_AI_<Name>_<variant>`; list it in the `props.json` entry's `"mesh_options"` and switch `"mesh_use"`, one world build and look-dev capture per option | "Smart Mesh props" |
| Custom model | HD stays the default. `aigen.py <name> custom <variant>` keeps another model as `<name>_custom.glb` in `art_src/`; `normalize` builds `SM_AI_<Name>_Custom`, which the world build shows instead, the HD mesh as fallback. Or `props.json` `"mesh_custom"` names any kit mesh or UE asset | "Custom models, polycount 1000, original angles" |
| Kit mesh | `aigen.py <name> normalize` → `build/environment/kit/SM_AI_<Name>.glb`: base-centre origin, +Z up, metres, scaled to the sprite. `build/` is git-ignored: on a fresh clone, `aigen.py every normalize` rebuilds them all from `art_src/` | "Normalise" |
| Symmetrical props | Manifest `"symmetrical": true`: the front restyle in Studio's Front slot and, mirrored left to right, in the Back slot (`tripo.SYMMETRICAL_VIEWS`, variant `SYMM`). Nothing new is painted; Tripo builds a real back. For skulls, jars, crates, mounds. A view source ending in `:mirror` flips its image | "Gallery review" |
| Sprites stay sprites | What moves or can be used stays a sprite (the server's bitmap online); static scenery is 3D. A manifest with `"status": "sprite"` keeps its object out of `props.json` (necglobe, node) | "Gallery review" |
| Placement | `props.json` `"classes"` (by Kod class) and `"types"` (OrnamentalObjects by OO number) give `"mesh"`. Meshes are AI only and skipped until their GLB exists. Yaw comes from the Kod angle, or a stable random turn with `"random_yaw"` (plants, rocks, clutter). `"mesh_options"` + `"mesh_use"` switch between alternative meshes (`"compare"` alternates them) | "Placement", "Trees" |
| Material | Every `SM_AI_*` mesh gets `MI_AIProp_<mesh>` on `M_PropTextured` (its glTF textures, plus the interiors' ambient floor, matte specular and weather). Without it, props go black indoors. The ambient floor is the original sector light under each prop (set per placed prop as custom primitive data 0) × props.json `ambient_scale` (2.5). A class's `albedo` scales a texture lighter than its sprite (the Inn's `Table`: 0.55) | "Raza batch", "Interior fill and albedo" |
| Lying items | `"lay_flat": true` turns a model generated from a side view (swords, wand) face-up on the ground | "Raza batch" |
| Lights | `props.json` `"light_scale"` 0.3 dims every added light; the environment carries the scene | ADR 0005 |
| Lamp glass | A class's `"glow"` lights pale, unsaturated texels inside a height band and radius of the mesh at night (`M_PropTextured` `PROP_GLOW_HLSL`); a light inside a closed lantern wants `"shadows": false` | "Props polish" |
| Blocking | Props are walked through, as in the original. A class's `"blocks"` (`true` = 26-sided hull, or `"box"` / `"capsule"` / `"sphere"`) makes it solid: `Lamp`, `Brazier`, `Table`, `WoodenBox`. `{"radius_m", "height_m"}` blocks with a hidden cylinder instead: sign posts, tree trunks | "Props polish", "Props round 2" |
| Hanging | A class's `"hanging"` (Kod `OF_HANGING`: the chandelier) hangs the mesh's top from the ceiling above it (`zone_layout.json` `ceiling_y`); `lift_m` alone only lifts it off the floor | "Hanging props" |
| Facing | Kod angle when the original gives one; else the class's `"facing"` (signs: east). `random_yaw` wins over the Kod angle (outdoors it's always 0). One object: `props.json` `"placed"` by actor label | "Props round 2" |
| Side views without original angles | Tripo source `"openai@front"` reuses the front restyle for left, back and right (variant `MVF`). Done for the rocks and dung (kept as custom models): single-image models of round things come out as slabs | "Props round 2" |

## Recipe for a zone (what ran for Raza)

All commands from the repo root. `aigen.py` re-runs itself in `build/texai/.venv`.

1. **Inventory:** `python tools/aigen/inventory.py`. Look at the frames: `python tools/bgf2png/bgf2png.py <bgf...>`, then make an overview sheet of all frames, as in ADR 0007.
2. **Manifests:** one per prop (copy one from `data/aigen/props/`). Leave `tripo.variants` out to get the defaults (`MV` when the sprite has angles, else `C`). Set:
   - `class` (+ `oo_type`), `bgf`, `frame`, `size_frame`, `size_by`, `mesh` `SM_AI_<Name>`;
   - `describe`: what the **sprite** shows, written after looking at its upscale;
   - `restyle.extra` (e.g. "lying flat");
   - `polycount` to move off the 4000 default; `model` for Smart Mesh or a custom model; `retopology` to add the Retopo step.
   Add each to `props.json` `classes` / `types`, with `random_yaw` for natural clutter.
3. **Sprites:** `aigen.py all sprite`. Check `height_m` for oddities.
4. **Restyles:** `aigen.py all batch-submit`, then `aigen.py batch-collect` (poll it until it reports completed). With original angles, run `batch-submit` and `batch-collect` a second time for the side and back views.
   - Check them all on one sheet: `aigen.py overview`.
   - Fix single misses by sharpening `describe` and running `aigen.py <name> restyle --force` (the egg basket lost its eggs).
5. **Tripo:** `aigen.py all tripo-prepare`.
   - Generate each `03_tripo_in/C_front.png` in Studio and write its task id into `tripo.runs.C.tripo_task`; see reference/studio-automation.md.
   - Then start `aigen.py bridge-collect all` (in the background) and Send To Blender every model. Resend any that didn't arrive.
6. **Keep and normalise:** `aigen.py all choose C`, then `aigen.py all normalize`. Review on `aigen.py overview all 05_review/normalized/front.png`.
7. **Build and judge:** run `tools/ue/build_world.ps1`, then `tools/ue/run_lookdev.ps1` by day and at night on cameras that show the props. Send the sheets to the user.
8. **Write it down:** results and sheet paths go in ADR 0007; changed defaults and new traps go here.

## Working with the user
- Visual choices are the user's, made from sheets. Recommend one option and keep the others reversible: raw GLBs stay in `04_tripo_out/` and `art_src/`, and the manifests record everything.
- Generating in Studio spends the user's web credits. State a batch's cost before running it (60 × 45 = 2,700 for Raza).
- Studio's menus only work while the Chrome window is visible on screen. Ask the user to keep it uncovered during collection.
- Keys: `OPENAI_API_KEY`, `VERTEX_API_KEY`, `FAL_AI_API_KEY`, `GEMINI_API_KEY` (and `TRIPO_API_KEY` later) live in the repo-root `.env`. Never print them.
- Ask before installing packages or downloading models. Don't commit; list the changed files at the end.
