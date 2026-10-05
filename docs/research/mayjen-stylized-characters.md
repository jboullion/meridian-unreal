# Research: Mayjen "Stylized characters" as a replacement character base

- Status: **Investigation only. Nothing has been bought, imported or decided.**
- Date: 2026-10-05
- Why: the MakeHuman (MPFB) bodies don't look good, and fitting them onto the UE5 mannequin skeleton (see [characters.md](../characters.md#building-the-character-kits)) visibly warps the mesh. We want a base that is rigged natively for the UE5 skeleton.
- Related: [ADR 0002](../adr/0002-metahuman-characters.md), which covers the MetaHuman vs modular discussion and the MPFB decision.

> **How reliable these notes are.** Fab, the Epic forums and Sketchfab were all blocked from the research environment. Everything below comes from search-engine extracts of those pages, not from reading them directly. Treat it as a lead and check it against the [open questions](#open-questions-check-on-the-fab-page-or-after-buying) before committing.

## The packs

| | [Stylized characters](https://www.fab.com/listings/f4b79ab2-213e-4d51-8fed-bdbcf9325a34) (base) | [Customization pack 01](https://www.fab.com/listings/96755080-42c6-4c5e-842c-8fdc0e156a7d) (add-on) |
|---|---|---|
| Seller | Mayjen ([Fab seller page](https://www.fab.com/sellers/Mayjen)) | Mayjen |
| Price | about $34.99 (search results also showed $49.99, maybe a different licence tier or a sale) | not found |
| Rating | 5.0 (21 ratings) | not found |
| Contents | Two base characters: **Body A (male)** and **Body B (female)**. Fully modular, low poly, easy recolouring of any part. **10 static poses**. | **5 heads A, 5 heads B**, 10 hairstyles (A and B), 5 + 5 eyebrows, 6 beards and 5 moustaches (A), makeup textures (14 for A, 28 for B), 19 body tattoos, 10 head tattoos, 4 eye textures + 4 catchlights |
| Size | not found | about 105k triangles / 53.7k vertices for the whole pack |
| Rig | **Epic Skeleton (UE5) with extra bones** | same rig |
| Morph targets | **None.** The seller says on the forum that the models have no morphs or blendshapes, and suggests scaling the mesh for size. | none mentioned |
| Character generator | **None.** The seller says the characters are not procedurally generated. Customisation means swapping parts and recolouring. | |

Body parts are split into **head, torso, hands, legs and feet**.

### The rest of the line (all add-ons on the same rig)
Casual set 01, Casual set 03 Blacksmith, Casual set 04 Elves, Monk set 01 (clothes, hair, weapons, tattoos), Warrior set 01, Adventurers kit. This is the strongest point of the line: a growing catalogue of clothes and armour fitted to the same two bodies, which suits the equipment plan.

## Can we use it as our modular character base?

**Short answer: technically yes. It fits the skeleton and the equipment plan well, but we lose face sliders and it can't be committed to the public repo.**

### What fits
- **The skeleton is native.** It is rigged on the UE5 skeleton, so there is no MPFB-style refitting and none of the warping that comes with it. The mannequin animation stack should carry over: Quaternius UAL retargets, `ABP_Unarmed` and later GASP / Motion Matching. Proportions are the caveat, see [risks](#risks).
- **The parts match our equipment design.** Head, torso, hands, legs and feet as separate meshes is the "each item hides the body regions it covers" model from ADR 0002 ([Equipment](../adr/0002-metahuman-characters.md#equipment)). It is cleaner than our current single MPFB body mesh. The parts plug into `UMRCharacterAppearance`'s leader-pose part list, and later into Mutable or the skeletal mesh merge for draw calls.
- **Recolouring matches what we planned.** Skin, hair and eye colour as material parameters is how we planned to replicate colours already (a few bytes).
- **The stylized, low-poly look matches** ADR 0002's "slightly stylized, performance first" decision, and arguably suits Meridian 59 better than realistic MPFB.
- **The add-on catalogue** gives us clothes, armour, hair and weapons on the same rig without making our own.

### What doesn't fit
1. **No face sliders.** Our current design replicates **49 head sliders** driving MPFB morph targets ([characters.md](../characters.md#head-sliders)). Mayjen has no morphs, so the creator falls back to **ADR 0002 Option A: presets plus parameters**:
   - Head: 1 base + 5 pack heads per gender, so about 6
   - Hair: about 10 styles
   - Brows: 5 + 1
   - Facial hair: beard and moustache
   - Makeup and tattoo textures
   - Colours

   That is closer to the original M59 creator (pick face parts, hair, colours) than our slider design is. The save format already allows presets.
   - **Adding sliders back is possible.** We could sculpt our own shape keys in Blender, but only if the heads share topology and the licence allows modifying the source files. Each hair, brow and beard piece would also need a matching corrective morph, the way the MPFB build refits them. That is real art work.
2. **It can't go in the public repo.** Fab content is git-ignored (`game/*/Content/Fab/`), the same reason MetaHuman counted against itself in ADR 0002.
   - Every contributor would have to buy the pack, plus every add-on we use.
   - CI and fresh clones fall back to the plain mannequin (the existing appearance fallback already handles this).
   - Build scripts must reference it without containing it.
   - The derived assets (MI_ instances, data assets, merged meshes) need a decision: can we commit our own `DA_*` / `MI_*` files that point at Fab assets? Probably yes, if they hold no Fab data, but they break without the pack.
3. **No body variation.** No build or height morphs, only scaling. That is fine for M59, which had none either.

### Risks
- **"Extra bones" means its own Skeleton asset.** It is probably not `SK_Mannequin` itself.
  - If the shared bones match names and the reference pose, we can mark it a compatible skeleton and play mannequin animations directly.
  - If the proportions differ a lot (stylized: bigger head and hands, shorter limbs), direct playback gives hand-to-body offsets and foot sliding. We would then need an **IK Retargeter** pass from Manny, at runtime or baked.
  - Check this first.
- **First person.** Our current setup hides the `head` bone for the owner and puts the camera at the eyes. A separate head mesh makes that easier: hide the head part instead.
- **Draw calls.** Five body parts plus hair, brows and facial hair, plus equipment, is easily 8–15 sections per character before merging. The MPFB body is about 12 draws today, so it's no worse, but Mutable or mesh merging becomes more important.
- **LODs unknown.** We don't know if the meshes ship with LODs. If not, UE's skeletal mesh reduction works on low-poly meshes, but gains less.
- **Art style lock-in.** All future human gear has to match Mayjen's style, or come from Mayjen.

## Open questions (check on the Fab page or after buying)
1. **Licence:** Fab Standard licence? Personal or Professional tier, and which one is the $34.99 price? Can we modify the meshes (sculpt morphs) and ship a game with them? (The Standard licence normally allows both. Raw redistribution is what's banned.)
2. **Formats:** UE project only, or also **FBX / .blend source**? We need FBX to add morphs or do Blender work.
3. **Skeleton:**
   - Does it ship its own Skeleton asset?
   - Which extra bones does it have (face, jaw, eyes, cloth)?
   - Does the reference pose match Manny's (A-pose vs T-pose)?
   - Do Manny animations play on it directly?
4. **Supported UE versions:** we are on 5.8.
5. **Per-part triangle counts, LODs, texture resolution and material count per part.**
6. **Do the 5 + 5 heads share topology** with each other and with the base head? (This decides whether head-blend sliders are possible.)
7. **How the materials work:** colour masks on one texture set, or several materials per part? Is there an atlas?
8. **Do the add-on clothes cover the body parts cleanly** when swapped (hide-the-part model), or are they designed over specific bodies?
9. Is the "Version 2.0 body shapes controlled by morph targets" claim in search results about Mayjen, or a different seller? It probably belongs to [Stylized Modular Characters (Male/Female, 3 Ethnicities, Body Morphs)](https://www.fab.com/listings/466219a1-dd20-461c-9457-c76deb7794a0), which is worth a look as an alternative because it has body morphs.

## Suggested next step (when we're ready to act)
A small spike, kept out of the other session's way:
1. Buy the base pack only. Add it to a scratch copy of the project.
2. Answer open questions 2–7.
3. Play `ABP_Unarmed` and a few Quaternius clips (sword attack, interact, sit) on Body A. Compare hand contact with Manny.
4. Assemble a character with the leader pose: body parts + head + hair. Count draw calls with `-MRProfile` against the MPFB numbers.
5. Screenshot it in Raza next to the MPFB male for a look comparison.
6. Then decide between: replace MPFB, run both (Mayjen for players, MPFB faces for NPC variety), or keep looking.

## Other options worth comparing in the same spike
Found while searching. Not investigated.
- [Stylized Modular Characters (Male/Female, 3 Ethnicities, Body Morphs)](https://www.fab.com/listings/466219a1-dd20-461c-9457-c76deb7794a0): has body morphs, and possibly blink morphs.
- [Baba Akar: Stylized Modular Base Character, UE5 Skeleton Compatible](https://forums.unrealengine.com/t/baba-akar-stylized-modular-base-character-ue5-skeleton-compatible/2747363)
- [Stylized Modular Character](https://www.fab.com/listings/98a26af6-c8f4-4916-bb12-aa76dd7b4d8a)
- [Modular Male Mannequin on Standard Skeleton](https://www.fab.com/listings/ed801c11-5d10-4857-b4c1-6e07b0e30483)
- [MMORPG Advanced Warrior: Medieval Fantasy Modular Character](https://www.fab.com/listings/9051d8a4-f190-4630-a250-cd181bac828a)

## Sources
- [Fab: Stylized characters](https://www.fab.com/listings/f4b79ab2-213e-4d51-8fed-bdbcf9325a34)
- [Fab: Customization pack 01](https://www.fab.com/listings/96755080-42c6-4c5e-842c-8fdc0e156a7d)
- [Fab: Mayjen seller page](https://www.fab.com/sellers/Mayjen)
- [Epic forums: Mayjen – Stylized characters](https://forums.unrealengine.com/t/mayjen-stylized-characters/2458429)
- [Epic forums: Mayjen – Customization pack 01](https://forums.unrealengine.com/t/mayjen-customization-pack-01/2458511)
- [Sketchfab: Body A](https://sketchfab.com/3d-models/stylized-character-base-body-a-9eba918f82084577bd0ec339655aa78d), [Body B](https://sketchfab.com/3d-models/stylized-character-base-body-b-2ae3177259494ab38ef5b80e136ba2a6), [Customization pack 01](https://sketchfab.com/3d-models/stylized-characters-customization-pack-01-41eed261b383421fb6d6cc5223912f7c)
- Add-ons: [Casual set 01](https://www.fab.com/listings/9960f65b-f101-4131-9827-64977f8b7825), [Casual set 03 Blacksmith](https://www.fab.com/listings/4edccd2e-fb2a-46a2-b48b-7aa312d52bf9), [Casual set 04 Elves](https://www.fab.com/listings/b6a722af-da03-4650-87f1-d2b33a2dabee), [Warrior set 01](https://www.fab.com/listings/08e8fe7a-37de-4379-8020-b022ff5dbef0), [Adventurers kit](https://www.fab.com/listings/68af7ef1-57a9-473e-9b6e-4a132cacea88), [Monk set 01 (forum)](https://forums.unrealengine.com/t/mayjen-monk-set-01/2737245)
