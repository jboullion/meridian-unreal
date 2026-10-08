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

## Consequences
- Equipment, hair and faces are sprite overlays, as in the original. The character creator works on `FMRSpriteAppearance`.
- Props and buildings stay 3D (ADR 0003, ADR 0007). Only animated characters are sprites.
- Open items (atlas size, combat, the creator UI) are listed under "Open" in docs/sprites.md.
