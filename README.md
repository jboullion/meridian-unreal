# Blakston: Meridian 59 remaster (codename)

This is a fan remaster of Meridian 59, built on the Server 104 ruleset, using Unreal Engine 5.8. The first playable demo covers the town of **Raza** and the zones around it.

- **Decisions:** [docs/adr/0001-engine-and-architecture.md](docs/adr/0001-engine-and-architecture.md)
- **Findings from the original data** (scale, zone layout, missing assets): [docs/findings.md](docs/findings.md)

> "Meridian" is a registered trademark. The original art and audio are not covered by the GPL. This repo contains no original art. Extracted assets go to `build/`, which is git-ignored, and are used only as reference.

## Layout

| Path | What it holds |
|---|---|
| `Server-104/` | Reference checkout of the original server. It's a separate git repo and is not part of this one. |
| `tools/` | Python tools that read the original data |
| `data/` | Generated JSON with the game data and zone layouts. It's committed. |
| `build/` | Generated meshes, previews and extracted reference art. Git-ignored; you can regenerate it any time. |
| `game/` | The UE 5.8 C++ project (not created yet) |
| `backend/supabase/` | Database migrations and policies (not created yet) |
| `docs/` | ADRs and notes |

## Regenerating everything

You need Python 3.11+ with Pillow, and Blender 5.x for previews. The art steps read from the Steam client at `H:\Steam\steamapps\common\Meridian 59`.

```bash
python tools/kod_extract/extract.py                 # Kod -> data/{zones,monsters,spells,skills,items,constants}.json
python tools/bgf2png/bgf2png.py --textures-for-zones # textures used by the demo rooms -> build/textures/
python tools/roo2gltf/roo2gltf.py --preview          # .roo -> build/zones/*.glb (+ PNG maps), data/zone_layout.json
python tools/bgf2png/bgf2png.py cow rat mummy centip spdrby bunny2   # creature sprites -> build/bgf/<name>/
blender -b --factory-startup -P tools/blender/render_glb.py -- build/zones/300_Raza.glb out.png
```

Run `bgf2png` before `roo2gltf` so the blockouts pick up the real texture sizes for UV tiling and for walls that shouldn't tile vertically.

## Tools

- **`tools/kod_extract/kodparse.py`**: a case-insensitive reader for Kod source. It handles constants, resources, classvars, properties and message bodies, and resolves inheritance.
- **`tools/kod_extract/extract.py`**: extracts the demo zones (exits, placed objects, spawns), monsters and NPCs (including shop lists), all spells, skills and items, and the constant tables.
- **`tools/roo2gltf/roo2gltf.py`**: turns `.roo` files into glTF blockouts. It builds floors and ceilings from the BSP leaves and Doom-style wall sections, and handles slopes. It also writes the world positions of exits, arrivals, objects and spawn generators.
- **`tools/bgf2png/bgf2png.py`**: decodes BGF v10 files into PNGs. Sprites become contact sheets (one group per row, view angles in columns); textures are written un-rotated, along with a size catalog.
- **`tools/blender/render_glb.py`**: renders a preview of any `.glb` in headless Blender.
