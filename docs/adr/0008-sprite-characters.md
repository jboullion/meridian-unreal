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

## Consequences
- Equipment, hair and faces are sprite overlays, as in the original. The character creator works on `FMRSpriteAppearance`.
- Props and buildings stay 3D (ADR 0003, ADR 0007). Only animated characters are sprites.
- Open items (atlas size, combat, the creator UI) are listed under "Open" in docs/sprites.md.
