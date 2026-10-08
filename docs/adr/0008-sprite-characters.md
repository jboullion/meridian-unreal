# ADR 0008: Sprite characters

- Status: **Accepted 2026-10-06.** Supersedes [ADR 0002](0002-metahuman-characters.md) (MakeHuman bodies on the mannequin skeleton).
- Date: 2026-10-06
- How it works and how to build it: [docs/sprites.md](../sprites.md). This ADR is the decision.

## Context
ADR 0002 picked MakeHuman (MPFB) bodies on the UE mannequin skeleton, with head morph sliders and retargeted Quaternius animations. It worked, but every new look, item and monster meant modelling, rigging and animation, and the bodies never looked like Meridian.

The sprite experiment (docs/sprites.md) drew players the way the original client did: directional sprites composited from overlays, picked by viewing angle, placed in the 3D world. It ports the original's rules exactly, keeps the original art, and runs cheaper (50 extra characters: GPU 12.3–13.2 ms against 14.3 ms for the engine mannequin, 0.2 M primitives against 2.4 M). Monsters and NPCs were then built on the same component.

## Decision
Every animated character (players, NPCs, monsters) is drawn with sprite sheets through `UMRSpriteBodyComponent`. The 3D character pipeline is removed.

- **Players:** `AMRCharacter` always draws a sprite body. Its look, creator colours and height replicate in `FMRSpriteAppearance`. First person shows the original's 2D hands and weapons (`AMRHUD`).
- **Monsters and NPCs:** `AMRMonster`, same component, looks from Kod.
- **New looks** are 2D work: upscaled original bgfs, recoloured at runtime, with in-between frames.
- `ACharacter`'s skeletal mesh is unused and hidden. Collision and movement stay on the capsule.

## What was removed (2026-10-06)
- C++: `UMRCharacterAppearance` (driver mesh, parts, head sliders), the mannequin and placeholder fallbacks, the `bSpriteBody` / `mr.Character.SpriteBody` / `-MRSpriteBody` switch, `-MRAppearance`, `-MRRandomFaces`, and the montage shots in the screenshot tour.
- Content: `Content/Characters/` (MPFB_Male, MPFB_Female, the character materials, the Quaternius animations and montages).
- Sources: `art_src/characters/`, `art_src/animations/`.
- Tools: `tools/blender/mpfb_character.py`, `install_mpfb_packs.py`, `preview_character.py`, `retarget_ual.py`, `tools/blender/characters/`, `tools/ue/export_mannequin.py`, `import_character.*`, `import_animations.*`, and the mannequin copy step in `tools/setup.ps1`.
- Docs: `docs/characters.md`. It and everything above are in git history before this ADR if they're ever needed again.

## Update (2026-10-07): faces for the creator, no AI, no specks
- **Every face part and hair the original creator offers is converted** (`data/charinfo.json` from Kod). Looks with an AI-made part are left out of the build unless asked for (`--with-ai`). No default part was ever AI-made; only the test hair `ai_hair_01` was.
- **The upscaler is chosen per part role** in `data/sprites/upscale.json`; every method is local:
  - `nearest`, `scale4x` (Scale2x twice on the palette indices), `gtav_dither` (the default until now), `gtav`, `gtav_pinned` and `animesharp`;
  - the comparison sheets come from `tools/sprites/face_sheet.py` (`build/sprites/faces/compare.png`, `zooms.png`);
  - the model methods upscale each face patch on its own, so its shading drifts from the head's and the patch outlines show;
  - **chosen (2026-10-08):** `scale4x` for the head, eyes, nose, mouth and hair (crisp, the original's colours, no grain);
  - **chosen (2026-10-08):** `scale4x` for the torso, arms and legs too (`build/sprites/faces/review_body.png`). Weapons and the first-person hand stay on `gtav_dither`;
  - any method is one line away.
  - (The first switch to the body did nothing: the role check wanted four-letter names, and the body bgfs have three. Fixed in `upscale_parts.role`.)
- **The face specks are fixed** by keeping each original pixel's palette ramp in its own unfiltered atlas and render target, not in the colour atlas's alpha (docs/sprites.md, "Ramps per original pixel").
- **Close up,** the render target grows with the sprite's size on screen (`mr.Sprite.ScreenTexels`).
- **Players wear the server's face parts and colours** (`FMRSpriteAppearance` part fields, `Net/MRNetLook`). The creator's previews use the same sprite body.

## Update (2026-10-08): equipment
- Every bitmap an item puts on a player is converted (165, `data/sprites/equipment.json`), and players wear what the server sends: torso, arms, legs, weapons, shields, bows and helmets (ADR 0012, M2b; docs/sprites.md "Players online").
- Torsos, arms and legs use `scale4x` like the base parts. Weapons, shields and hats stay on `gtav_dither` (the "other" role) for now; `build/sprites/equipment/items.png` compares the methods.

## Update (2026-10-08): back to the original pixels
**Decision:** players and monsters are drawn from the original game's pixels again: no upscale, no in-betweens. The atlases hold one texel per original pixel, filtered nearest, so every pixel is a crisp square as the original drew it. The colour-ramp atlases (the runtime recolouring) are built at 1× too. Weapons, shields, bows and hats drawn finer than the torso (shrink 12–100) are stored at the torso's detail, below their own pixels. It's a data switch, `store` in `data/sprites/upscale.json`, so the upscale can come back.

**Why:** the upscale only showed close up, and it cost about ten times the texture memory.
- At a normal viewing distance the original pixels, `scale4x` and `gtav_dither` look practically the same. Close up, the original is crisp square pixels, `scale4x` rounds the outlines but leaves flat blotches, and `gtav_dither` adds a fine grain. Sheets: `build/sprites/equipment/methods.png` (`tools/sprites/equipment_sheet.py --methods-only`), with `methods_small.jpg` and `methods_zoom.jpg`.
- In the game: `build/sprites/tour/orig_vs_4x_player.png` (left the 4× upscale, right the original pixels), `build/sprites/tour/orig_plate/`, `build/sprites/tour/orig_monsters/`.

**What we measured** (220 atlases: the base parts, the 165 equipment bitmaps of M2b, 19 monster and NPC bodies):

| Storage | Atlas texels | Cooked (BC7 colour with mips, RGBA8 ramp) |
|---|---|---|
| 4× upscale + 3 in-betweens per step (until 2026-10-08) | 1,061 M | ~1.5 GB (1.32 GB colour + 0.18 GB ramp) |
| 4×, no in-betweens (estimate) | ~620 M | ~0.85 GB |
| **Original pixels, no in-betweens, worn pieces at the torso's detail (now)** | **46 M colour + 26 M ramp** | **~156 MB (58 MB colour + 98 MB ramp)** |

- Where the 4× bytes went: the in-betweens were 60–83 % of the used texels in torsos, arms and legs; the upscale stored 16 texels per original pixel; about half of every atlas was empty (power-of-two sizes and shelf packing); and high-resolution masks and weapons (up to 400 pixels, shrink 30) were stored at their own size.
- Not shipped either way: the upscale caches (`build/sprites/up/`, 1.4 GB), in-betweens (`build/sprites/tweens/`, 0.44 GB) and atlas PNGs live in `build/`.
- The uncompressed RGBA8 ramp atlases are now the larger share (98 MB). One channel would do (the ramp, and coverage), if size matters again.

**Given up:**
- The in-betweens (smoother walk and attack cycles). The original snapped between poses; so do we now. Crossfade and view fade were already off.
- The close-up detail of the upscale. Faces, hair and the creator portrait are the original pixels too.
- Worn pieces stored below their own pixels (`worn_detail_shrink` 4, the torso's) are a little softer than the face under a helmet when seen close; 7 (the head's) would match the face.

**Follow-ups the same day:**
- **Worn pieces at their own pixels.** At the torso's detail a helmet looked blurry (`helm`, 84 × 125 at shrink 14, was stored at 24 × 36). Weapons, shields, bows, helmets, hats and masks are now stored at their own pixels, as the original drew them (`worn_detail_shrink` 0). Before and after: `build/sprites/helm_before_after.png`. This is the expensive part: about 50 masks and long weapons are high-resolution originals (up to 400 pixels, shrink 20–100), and each gets a ramp atlas too.
- **Ramp atlases 16-bit.** Block compression (BC/DXT) would break them: they carry exact values (the part's translation id, the ramp code, coverage). They're stored `TC_LQ` (A1RGB555 on Windows and Linux), which keeps those values exactly, at half the size of RGBA8. On Mac the engine falls back to DXT5; the atlas keeps red at 255 on every texel so that stays exact within the decoder's rounding. A recoloured player rendered with the 16-bit and the RGBA8 ramp matches (mean difference under 2 levels on the character; `build/sprites/ramp_check.png`). `TC_LQ` is hidden from Python, so `tools/ue/import_sprites.py` sets it through `UMREditorScripting::SetLowQualityCompression` (C++).
- **Cost now:** colour 209 M texels (~265 MB) + ramp 195 M (~371 MB) = **~636 MB cooked**. Capping worn pieces at what the render target can show (`worn_detail_shrink` 16: nothing finer than shrink 16 is visible at 4 texels per torso pixel, except in close-ups) would make it ~241 MB, with `helm` and most helmets unchanged. At the torso's detail with the 16-bit ramp it would be ~107 MB. **The maintainer kept the own pixels** (~636 MB).
- **Faces stay as stored.** They were already the original pixels; the original draws the head at shrink 7 and the eyes, mouth and hair at 14, so their pixels are 1.75–3.5× finer than the torso's (shrink 4), which makes them stand out next to it. Snapping them to the torso's pixel grid was tried, sharp (features break up) and averaged (soft); the maintainer kept them as stored. Sheet: `build/sprites/equipment/faces.png` (`tools/sprites/equipment_sheet.py --faces`; rows: as stored, snapped sharp, snapped averaged), with `faces_heads.jpg` and `faces_bodies.jpg`.

**To go back:** in `data/sprites/upscale.json`, set `store` to `{"scale": 4, "tweens": 3, "monster_max_scale": 2}` (the method per part is still in `default` and `roles`). Then run `build/texai/.venv/Scripts/python tools/sprites/build_player_sprites.py` and `tools/ue/import_sprites.ps1`. That takes about an hour, mostly in-betweens and the AI upscale of the weapons.

## Consequences
- Equipment, hair and faces are sprite overlays, as in the original. The character creator works on `FMRSpriteAppearance`.
- Props and buildings stay 3D (ADR 0003, ADR 0007). Only animated characters are sprites.
- Open items (atlas size, combat, the creator UI) are listed under "Open" in docs/sprites.md.
