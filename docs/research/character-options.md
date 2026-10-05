# Research: character base options (2026-10-05)

- Status: **Investigation only.** These are notes, not a decision.
- Context:
  - The MPFB characters look weak, and fitting them to the mannequin warps the mesh.
  - Face sliders are **not required**. Preset heads and body parts plus colours are fine, as in the original game.
  - [ADR 0002](../adr/0002-metahuman-characters.md) is background brainstorming, not a binding decision.
- Related: [mayjen-stylized-characters.md](mayjen-stylized-characters.md).

> Fab, the Synty store, quaternius.com and the Epic forums were blocked from the research environment. The facts below come from search-engine extracts. Verify them before buying anything.

## Two questions decide most of this
1. **Must the characters live in the public repo?** If yes, only CC0 content or our own work qualifies: Quaternius, MPFB, Meshy output we own, or hand-made. Fab, Synty and MetaHuman are out, or at best "bring your own copy".
2. **What look?** Low-poly stylized (Synty, Quaternius), painted stylized (Mayjen) or realistic (MetaHuman, MPFB). It has to sit next to the environment art in Raza.

## Note: the MPFB warping is a pipeline choice, not inherent
`mpfb_character.py` snaps every MPFB bone onto the **Manny joint positions** (fit error 0 mm). That forces the body into Manny's proportions, which is what deforms it.

The usual UE approach keeps **each mesh's own joint positions**:
- The mesh's skeleton uses the mannequin bone names and hierarchy, but its own reference pose.
- Bone translation retargeting is set to *Skeleton* (or *Animation Scaled* for the pelvis), so animations apply rotations only.
- Alternatively, use an IK Retargeter from Manny.

The same point applies to **any** non-native mesh below (Quaternius, Meshy). Re-skinning onto Manny's exact joints will warp them too.

## Options

| Option | Look | Licence / public repo | Skeleton | Modular parts | Cost |
|---|---|---|---|---|---|
| **Quaternius Universal Base Characters + Modular Character Outfits – Fantasy** | Simple low-poly stylized | **CC0, committable** | Quaternius humanoid rig, the **same rig as the UAL animations we already use** | 6 bases (superhero, regular and teen proportions, male and female), 20 hairstyles; 12 outfits from 62 parts, 3 colourways each | Free; source .blend for $19.99+ |
| **Synty Sidekick** | Synty low-poly stylized | Synty licence (5 seats per copy); not committable | **UE Mannequin compatible** | Huge: 50–300+ parts per pack, with fantasy villagers, sorcerers, elven warriors, skeletons, goblins and pirates; editor character-creator plugin (UE 5.3–5.8) | Free starter pack; paid packs |
| **Mayjen Stylized characters** | Painted stylized, nicer faces | Fab; not committable | UE5 skeleton + extra bones | 2 bodies, 5-part split; add-on head, hair and outfit packs | ~$35 + add-ons |
| **MetaHuman (low LODs)** | Photoreal; hard to stylize | Engine-agnostic now, but not committable | Native | Presets only (editor-only creator); outfits via MetaHuman tooling | Free; heavy runtime |
| **Meshy (AI)** | Whatever you prompt | Output we own (paid plan): committable | Auto-rig (Mixamo-like); needs remapping | Not modular: every generation has different topology, proportions and baked lighting | Subscription + cleanup time |
| **MPFB, refit properly** | Realistic-ish | CC0, committable | Mannequin names, own proportions | One body; CC0 clothes | Pipeline rework |

### Quaternius
- **Strongest fit for a public repo.**
  - CC0, about 13k triangles average, FBX and glTF.
  - Already set up in UE, Unity and Godot.
  - Same author and rig as our UAL animations, so `retarget_ual.py`'s bone mapping is reusable.
- **The look is the risk.** It's simple and chunky, which may read as "prototype" next to the environment.
- Gear and outfits from one author keep the style consistent.
- To check:
  - Are the 3 colourways separate textures or a palette/atlas? (Draw calls.)
  - Do the outfit parts replace body parts, or sit over them?

### Synty Sidekick
- **The most complete modular system, and fantasy-heavy, which suits M59.**
  - Mannequin compatible.
  - Body blendshapes (masculine/feminine, skinny/heavy, muscular) and ARKit face blendshapes for expressions and lip-sync.
  - An editor creator tool.
- The runtime API is Unity-only ("UE coming soon"). We'd build our own runtime from the parts anyway: replicated part ids plus colours.
- The free starter pack lets us test it at no cost.
- Not committable. Synty is also a very recognisable style.

### Mayjen
- See [mayjen-stylized-characters.md](mayjen-stylized-characters.md).
- Better-looking stylized faces than Synty or Quaternius, but a smaller catalogue and no morphs.
- Sidekick covers the same need with more content, unless Mayjen's look is the one we want.

### MetaHuman with low LODs
- Our measurement (UE Optimized, medium): **0.18 ms game thread and about 15 draws per character**, against about 0.09 ms and about 12 for MPFB.
- Forcing low LODs, LODSync, and turning the face rig off past about 5 m helps the GPU and some of the CPU. It doesn't remove the second skinned mesh or the face AnimBP.
- Search results report about 800 MB of VRAM for grooms alone across 50 card-hair MetaHumans. Epic has said there is no crowd-specific path, apart from the Mass-based MetaHuman Crowd plugin.
- Presets-only removes the creator problem, but the look fights a stylized world and nothing can be committed.
- **Only worth it if we want a realistic look and accept "bring your own assets".**

### Meshy
- Good for **gear, props, monsters and unique NPCs**, as the ADR already says.
- For player characters, use it to make **preset heads, hairstyles and helmets** on top of a fixed base body. Even then, each head needs Blender work:
  - weld the neck seam to the body
  - retopologise or decimate
  - skin to `head` / `neck_01`
  - match the texture style

  Expect hours per head, not minutes.
- Generating whole modular characters doesn't work: the parts won't share seams or proportions.
- **Update:** whole-outfit generation *can* work if every piece is fitted back onto one base body that owns the proportions, seams and weights. See [ai-character-pipeline.md](ai-character-pipeline.md).

## Recommendation (for discussion)
- **If the public repo matters:** use **Quaternius Universal Base Characters + Fantasy outfits** as the committed default.
  - Use its own proportions on mannequin bone names (see the note above).
  - Grow it with Meshy-made gear, heads and helmets that we own.
  - This drops MPFB and keeps everything CC0 or ours.
- **If "bring your own assets" is acceptable:** **Synty Sidekick** has the most content and the best system for a fantasy MMO creator, and the starter pack is free to try. Pick Mayjen instead only if its look clearly wins.
- **MetaHuman last.** It costs the most, the look is hardest to match, and nothing is committable.

### A cheap bake-off before deciding
1. Free assets only: the Quaternius base + outfits, and the Sidekick starter pack. Add Mayjen if we want to spend $35.
2. One character of each in the Raza square with the screenshot tour, next to MPFB.
3. Play `ABP_Unarmed` and a few UAL clips (sword attack, interact, sit). Check hand contact and deformation.
4. Run `-MRProfile` with 50 characters: game thread, draws and triangles per character.
5. Count parts and materials per assembled character, to see how much merging (Mutable or the skeletal mesh merge) we'd need.

## Sources
- [Quaternius: Universal Base Characters](https://quaternius.com/packs/universalbasecharacters.html), [itch.io](https://quaternius.itch.io/universal-base-characters), [Patreon post](https://www.patreon.com/quaternius/posts/universal-base-137047994)
- [Quaternius: Modular Character Outfits – Fantasy](https://quaternius.com/packs/modularcharacteroutfitsfantasy.html), [itch.io](https://quaternius.itch.io/modular-character-outfits-fantasy)
- [Synty Sidekick starter pack (Fab)](https://www.fab.com/listings/8d8e9639-d93f-4f1d-8332-32ae0ef14bca), [Synty store](https://syntystore.com/products/sidekick-modular-characters-starter-pack), [Sidekick packs](https://syntystore.com/collections/sidekick-character-packs)
- [MetaHuman assembly docs](https://dev.epicgames.com/documentation/en-us/metahuman/assembly), [LODSync](https://dev.epicgames.com/documentation/en-us/metahuman/lodsync-component-for-unreal-engine), [MetaHuman 5.6/5.7 pipeline reference](https://medium.com/@Jamesroha/metahuman-5-6-5-7-pipeline-reference-170d302b078e)
- [Meshy auto-rigging](https://www.meshy.ai/features/ai-auto-rigging), [Meshy rigging docs](https://docs.meshy.ai/en/webapp/guides/3d-model/rigging)
