# Research: AI pipeline for monsters and static props

- Status: **Investigation and test plan. Nothing has been generated or built yet.**
- Date: 2026-10-05
- Question: can we turn the original monster and object sprites into consistent, rigged and animated 3D monsters and static props, with one shared style, through upscale → 2D restyle → image-to-3D?
- Related: [ai-character-pipeline.md](ai-character-pipeline.md) (players, outfits and human NPCs; this doc reuses its front end), [ADR 0003](../adr/0003-environment-art-pipeline.md) (environment), `data/environment/props.json`.

> Tool facts come from vendor docs and search extracts (October 2026). Verify them on our own assets; that's what the spikes are for.

## Short answer
- **Static props: yes, and it's the easiest place to start.** No rig, no seams. The sprite even gives us the real-world size.
- **Monsters: yes, through body-type rigs, not the player's skeleton.** Each monster is rigged onto one of a few **archetype skeletons** (biped, quadruped, multi-leg, serpent, floater/rigid). Each archetype has its own animation set. The original walk and attack frames are used as **pose and timing references** for those animations, not converted directly.
- **A common style is enforced in the 2D step,** not the 3D one. Image-to-3D generators copy their input image; they don't take a style prompt or LoRA for the shape. So every sprite is restyled with a fixed "style bible" (text block plus approved reference images) before it reaches the 3D generator. Later, a LoRA trained on our approved restyles takes over.

## What the original sprites give us
Each monster `.bgf` has more than one picture:

- **6 view angles × 9 poses.** `mummy.bgf`: 54 bitmaps in 9 groups of 6.
  - Group 1 is standing, groups 2–5 are the walk cycle, groups 6–9 the attack.
  - Each group holds the same pose from front, three-quarter, side, back and the other side.
- **Timing from Kod.** `mummy.kod`:
  - `SendMoveAnimation`: `ANIMATE_CYCLE` over groups 2–5 at 200 ms per frame, so the walk loops every 0.8 s.
  - `SendAnimation`: `ANIMATE_ONCE` over groups 6–9 for an attack, then back to group 1.
  
  These `AddPacket` lines can be parsed for all 184 monster `.kod` files, so a per-monster animation manifest is generated, not typed.
- **Corpse.** `vrDead_icon` (for example `bunny2X.bgf`) is the death end pose.
- **Size.** Bitmap height and `shrink` give the in-world height (see `tools/bgf2png/bgf2png.py`: one texel = 1/shrink Kod fineness units). The 3D model is scaled to the original's height automatically.
- **Sounds** (`vrSound_hit`, `_aware`, `_death`) keep working as anim notifies.

Static objects (lamps, braziers, torches, furniture) are mostly a single view, sometimes with flame frames.

## The shared front end (monsters, props, NPCs)

```
data/aigen/<kind>/<name>.json      manifest: sprite, groups, prompts, seeds, model versions, scale, status
        │
 [1] extract    bgf2png.py → frames + meta (views, groups, shrink)
 [2] upscale    4xTextures_GTAV (same model as the environment), cached per model
 [3] restyle    image edit: original = identity, style bible = look   → front / side / back (+3/4)
 [4] review     contact sheet: original vs restyle; approve / regenerate
 [5] to 3D      Tripo P2.0 multi-view (or TRELLIS.2 on RunPod), target face count per kind
        │
        ├─ props     → normalise, collision, material pass → SM_*
        ├─ monsters  → archetype rig, animation set, material pass → SKM_* + anims
        └─ humans    → see ai-character-pipeline.md
```

Every step caches its output by input hash and model version (as the texture upscales do), so a prompt change only reruns what it affects.

### Step 3: one style for art made by many people over many years
The **style bible** is committed under `tools/aigen/style/`:
- a fixed **style text block**: for example "hand-painted dark fantasy, 1990s pre-rendered sprite heritage, muted earthy palette, soft diffuse light, no rim light, matte materials, readable silhouette", tuned on the first batch.
- **3–6 approved reference images**: the "golden" restyles everyone else is matched to.
- **palette notes per family**, so undead, beasts and elementals each keep a consistent colour range while staying close to the original's colours.

Each restyle request then sends the **original sprite** as the identity reference, the **style references**, and the style block plus a short per-asset line ("mummy: bandaged undead, arms forward").

**Hosted first, LoRA later:**
1. **Bootstrap:** Nano Banana Pro or GPT Image 2 with reference images. They're strong at keeping a reference's identity while changing its rendering.
2. **Scale:** once 30–50 restyles are approved, train a **style LoRA** for an open image-edit model (Qwen-Image-Edit or Flux Kontext) on RunPod. That gives consistent results at batch volume, at a fixed cost, and is reproducible because the weights and seeds are recorded.
3. **Check:** every restyle sits next to its original in the review sheet. Reject if it stops looking like the same monster. There's an "identity strength" trade-off here, and the review is where it gets tuned.

A final **material pass in UE** pulls things together further:
- shared master materials with the environment,
- clamped roughness and saturation ranges,
- the same texel density.

Generators like Tripo and Meshy can also **re-texture** an existing mesh from a reference image. Use that to bring an off-style model into line without regenerating its shape.

## Monsters

### Should humanoid monsters use the player skeleton?
**No: give them their own Skeleton asset, but keep the same bone layout for bipeds.**
- What makes a monster read as human is mostly the **animations and proportions**, not the bone hierarchy. A skeleton playing `ABP_Unarmed` walks like a person whatever its bones are called.
- A separate Skeleton asset makes "plays the player's clips" impossible by accident. Monster clips are made for monsters.
- We can still **borrow on purpose**: an IK Retargeter from the mannequin to the monster biped turns a chosen human clip (a sword swing, a stagger) into a monster clip at editor time. After that it is edited like any other monster clip, for example hunched or slowed.
- Each monster keeps **its own proportions** (long arms, hunched spine), like the NPCs.

### Archetype skeletons
| Archetype | Examples | Rig source | Animation sources |
|---|---|---|---|
| **Biped** | mummy, skeleton, orcs, trolls, the hunched beast | Tripo `biped` auto-rig or our own biped template; optional tail, jaw, extra spine | UAL zombie and sword clips via retarget, HY-Motion text-to-motion ("hunched shambling walk"), Tripo presets, edits from sprite poses |
| **Quadruped** | rat, bunny, cow, wolves | Tripo `quadruped` | Quaternius *Ultimate Animated Animals* (CC0), Tripo quadruped presets |
| **Multi-leg** | spiders, centipede, scorpions | Tripo `hexapod` / `octopod` | Procedural legs (Control Rig foot IK stepping), Tripo presets |
| **Serpent** | snakes, worms | Tripo `serpentine` | Procedural spline wave |
| **Floater / rigid parts** | the stone golem (floating rock chunks), ghosts, eyes, elementals | Hand template: one bone per rigid chunk, plus a hover root | Hover bob plus spring lag (AnimDynamics); a few authored attacks |

- **Dynamic rigging means template fitting.** Every monster of an archetype gets the **same bone names**, from Tripo's rig or by fitting our template's joints onto the mesh. All of them then share one UE Skeleton per archetype (compatible skeletons) and one base animation set. Per-monster clips are added on top.
- **Auto-rig candidates:**
  - **Tripo Rig v2.5** (via API): 7 body types (biped, quadruped, hexapod, octopod, avian, serpentine, aquatic), FBX/GLB, with idle/walk/run/slash/hurt/fall presets.
  - **UniRig** (VAST/Tsinghua, open source, runs on 8 GB): rigs any shape. Its skeleton names aren't consistent from one model to the next, so it's the fallback for odd shapes, where a per-monster skeleton is fine.
- **Rigid parts** (the golem's rocks, armour plates, a skeleton's ribcage) are bound 100 % to a single bone, so they never stretch.
- **Weapons in hand** (the skeleton's sword) are removed in the restyle step and become separate static meshes on a hand socket, so they can be swapped and dropped.

### Minimum animation set per monster
`idle`, `walk` (looping), `attack` (1–2), `hit`, `death` (ending in the `vrDead_icon` pose). Optional: `run`, `aware` (plays the aware sound), `cast` for spellcasters (`viSpellChance`).

### Using the sprite frames for motion
The 4 walk and 4 attack frames from 6 angles are good **key poses with timing**. They're too few to *be* the animation: 4 frames looped at 200 ms is the original's stop-motion look.
1. **Baseline (manual, reliable):** a Blender script sets up 6 cameras at the sprite's angles with the frames as camera backgrounds. An animator poses the rig to match each key frame at the Kod timing, starting from the nearest frame of the archetype's base clip. About an hour per monster.
2. **Assisted:** the same 6 cameras, then optimise bone rotations so the rendered silhouette matches the sprite's alpha (IoU), starting from the base clip. It works for chunky shapes; thin limbs and overlapping arms will need hand fixes. Worth trying once the manual baseline exists to compare against.
3. **Use the result** as keys inside a smoother base clip (walk) or directly as an attack (stretched timing, plus anticipation and follow-through frames added in Blender).

**Check:** play the original frames and the 3D monster rendered from the same 6 angles side by side, at the Kod timing. It should read as the same motion.

### Monster risks
- **Thin and open shapes.** Skeletons' ribcages and spider legs: generators fill gaps or fuse limbs. Consider generating the skeleton as an armature of bone pieces (part segmentation), or use alpha cut-outs on the ribcage.
- **Floating parts** (the golem's rocks) may fuse to the body or vanish. Tripo's part segmentation, or generating the core and the chunks separately, are the fixes.
- **Back views are invented** where sprites are front-only. The monsters' 6 angles make this much less of a problem than for NPCs.
- **Many legs.** Generators often get the leg count wrong. Count them in review.
- **HY-Motion's licence** is a Tencent community licence. Check its territory and use terms (the Hunyuan3D ones exclude the EU, UK and South Korea) before committing clips made with it.

## Static props
| Step | Detail |
|---|---|
| Restyle + views | Usually one sprite view, so ask for front / side / back / top from it. Symmetric props can make do with the front and side. |
| 3D | Tripo P2.0 smart low-poly at a per-class face target (500–5k for clutter, 10–20k for hero props). |
| Normalise (Blender) | Scale to the sprite's world height, pivot at the base, +Z up, apply transforms, metres. |
| Collision | Simple convex hulls (UE auto) or a box/capsule per class. |
| Materials | Slots by role, as `props.json` already does (`iron`, `lamp_glass`, `embers`). Emissive parts get their own slot. |
| Effects | **Flames, glows and smoke are not meshes.** Upscale the original flame frames into a flipbook for Niagara, so torches and braziers still flicker like the original. The `props.json` lights stay as they are. |
| Output | `build/environment/kit/SM_<Class>.glb`, listed in `props.json`, so `build_world.py` places them like the current lamp post and brazier. |

- **Pick which objects get 3D.** Tiny clutter seen from far away can stay as a camera-facing upscaled sprite at first, if it reads fine.
- **First test against what exists:** the procedural `SM_Brazier` / `SM_LampPost` from `build_prop_kit.py` are a ready baseline for an AI-versus-procedural comparison in the Raza look-dev cameras.

## Test plan

### Spike P: three props
Brazier and lamp post (compared against the procedural kit), plus one wooden prop (a table or crate) for a non-metal material. Run them through the look-dev cameras (`run_lookdev.ps1`) against the `baseline` label.

### Spike M: four monsters, four rig problems
| Monster | Why |
|---|---|
| **Mummy** (`mummy.bgf`, demo scope) | Biped with the full 6×9 frame set; UAL already has zombie clips |
| **Giant rat** (`rat.bgf`, demo scope) | Quadruped |
| **Spider baby** (`spdrby.bgf`, demo scope) | Octopod; leg count and procedural stepping |
| **Stone golem** (your example) | Floating rigid parts |

Add the skeleton (thin shapes, weapon in hand) when the first four work.

Steps per monster: manifest → extract → upscale → restyle (all four in **one** style session, to test consistency) → Tripo P2.0 multi-view → Tripo auto-rig with the archetype type → UE import onto the archetype Skeleton → idle / walk / attack / hit / death → spawn in Raza or the Outskirts.

### Checks
| Check | How | Pass |
|---|---|---|
| Same monster | 3D rendered at the sprite's 6 angles next to the frames | Recognisably the original |
| One style | All four monsters (and props) in one lineup screenshot in Raza | Look like one game, not four artists |
| Motion | Original frames vs 3D clip from the same angles, at the Kod timing | Reads as the same walk and attack |
| Deformation | Walk, attack, death | No tearing at the hips and shoulders; rigid parts stay rigid |
| Scale | Height vs the sprite-derived height, next to a player | Within ~5 % |
| Cost | Tris, draws, `stat anim` with 20 monsters | Monster ≤ player cost |
| Effort | Time from sprite to in-game | Props ≤ 30 min, monsters ≤ 3 h after scripting |
| Licence | Tripo plan, image model, LoRA base model | Output may sit in a public repo |

### Decision rule
- **Props:** adopt if the AI brazier matches or beats the procedural one in look-dev. Then batch the remaining Kod-placed classes.
- **Monsters:** adopt per archetype. Biped and quadruped are likely to pass first. Multi-leg and floater may need more hand work, which is fine for a handful of monsters.
- **Style:** if hosted restyles drift across a batch of ~20, move to the LoRA before continuing.

## Sources
- [Tripo auto-rig API](https://developers.tripo3d.ai/en/docs/animations-rig), [Tripo animation retarget](https://developers.tripo3d.ai/en/docs/animations-retarget), [Tripo non-humanoid rigging](https://www.tripo3d.ai/blog/mixamo-alternative-rigging-non-humanoid-quadruped-characters), [Tripo ComfyUI nodes](https://developers.tripo3d.ai/en/docs/comfyui-nodes)
- [UniRig (GitHub)](https://github.com/VAST-AI-Research/UniRig), [UniRig paper](https://arxiv.org/html/2504.12451)
- [HY-Motion 1.0 (Pandaily)](https://pandaily.com/tencent-s-hunyuan-team-open-sources-hy-motion-1-0-an-ai-motion-model-that-understands-vague-prompts-and-generates-high-quality-3-d-character-animations)
- [Quaternius Ultimate Monsters](https://sketchfab.com/3d-models/ultimate-monsters-pack-fd72e114d119488da71fe3a16f216c4f), [Ultimate Animated Animals](https://quaternius.com/packs/ultimateanimatedanimals.html), [Animated Monster Pack](https://quaternius.com/packs/animatedmonster.html)
- [Best AI auto-rigging tools 2026 (Ludo)](https://ludo.ai/compare/best-ai-auto-rigging-tools)
- Tripo P2.0, licensing and image-model sources: see [ai-character-pipeline.md](ai-character-pipeline.md#sources)
