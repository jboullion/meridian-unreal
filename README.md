# Meridian Remastered

This is a free fan remaster of Meridian 59, built on the Server 104 ruleset, using Unreal Engine 5.8. The first playable demo covers the town of **Raza** and the zones around it.

- **Decisions:** [docs/adr/0001-engine-and-architecture.md](docs/adr/0001-engine-and-architecture.md)
- **Findings from the original data** (scale, zone layout, missing assets): [docs/findings.md](docs/findings.md)

> The Server 104 team has approved the "Meridian" name for this project. "104" must not be used in the product's name or branding. The original art and audio are not covered by the GPL. This repo contains no original art. Extracted assets go to `build/`, which is git-ignored, and are used only as reference.

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

## First-time setup (fresh clone)

Generated files are not committed. That covers extracted art, blockout meshes, compiled binaries, and everything under `game/MeridianRemastered/Content/Generated/`. One command rebuilds them all:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1
```

Pass `-SkipData` to keep the existing `data/` and `build/`, or `-SkipBuild` to skip compiling.

**Content rule:** anything a script writes into Unreal goes under `/Game/Generated/` and is git-ignored. Hand-made assets go anywhere else under `Content/` and are tracked with Git LFS.

## Regenerating everything

You need Python 3.11+ with Pillow, and Blender 5.x for previews. The art steps search the Server 104 client (`%LOCALAPPDATA%\Meridian-104\resource`) first, then the Steam classic client, then `Server-104/resource`.

```bash
python tools/kod_extract/extract.py                 # Kod -> data/{zones,monsters,spells,skills,items,constants}.json
python tools/bgf2png/bgf2png.py --textures-for-zones # textures used by the demo rooms -> build/textures/
python tools/roo2gltf/roo2gltf.py --preview          # .roo -> build/zones/*.glb (+ PNG maps), data/zone_layout.json
python tools/bgf2png/bgf2png.py cow rat mummy centip spdrby bunny2   # creature sprites -> build/bgf/<name>/
blender -b --factory-startup -P tools/blender/render_glb.py -- build/zones/300_Raza.glb out.png
```

Run `bgf2png` before `roo2gltf` so the blockouts pick up the real texture sizes for UV tiling and for walls that shouldn't tile vertically.

## Game project (`game/MeridianRemastered`)

Requires UE 5.8 (`G:\Unreal Engine\UE_5.8`) and Visual Studio 2022 or 2026 with the C++ game workload.

```bash
# compile the editor target
"G:/Unreal Engine/UE_5.8/Engine/Build/BatchFiles/Build.bat" MeridianRemasteredEditor Win64 Development -Project="E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" -WaitMutex
```

Rebuild the world level (`/Game/Generated/Maps/L_World`) from the zone blockouts (run `roo2gltf` first). It runs a headless editor session and quits:

```bash
"G:/Unreal Engine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" -ExecutePythonScript="E:/2026_Experiments/meridian-unreal/tools/ue/build_world.py" -unattended -nosplash -RenderOffscreen
```

Run the network smoke test. It starts a dedicated server and a headless client, walks the player through the demo's exits, and exits 0 if every step passes:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_zone_test.ps1
```

**To play:** open the project in the editor and press Play with Net Mode set to "Play As Client" (2+ players).
- Move with WASD and the mouse.
- Shift sprints (drains Vigor), Caps Lock toggles walking, C crouches, Space jumps.
- V or the mouse wheel switches between first and third person.

| Source folder | What it holds |
|---|---|
| `Core/MRUnits.h` | Original grid and angle conversions to UE (must match `roo2gltf`) |
| `Abilities/MRAttributeSet` | GAS attributes: the six stats plus Health, Mana and Vigor |
| `Character/` | Player character (FP/TP camera, input built in code) and predicted walk/run/sprint movement |
| `Player/` | Player state (owns the Ability System Component and the zone ID) and controller |
| `Zones/MRZoneSubsystem` | Zone data, tile and edge exits, seamless shared-geometry zones, teleports |
| `Game/MRGameMode` | Spawns new characters at the Raza Inn |
| `Tests/MRZoneSmokeTest` | The `-MRZoneTest` server-side travel test |

The game reads `data/zones.json` and `data/zone_layout.json` directly from the repo during development. A packaged build reads them from `game/MeridianRemastered/Data/`.

## Tools

- **`tools/kod_extract/kodparse.py`**: a case-insensitive reader for Kod source. It handles constants, resources, classvars, properties and message bodies, and resolves inheritance.
- **`tools/kod_extract/extract.py`**: extracts the demo zones (exits, placed objects, spawns), monsters and NPCs (including shop lists), all spells, skills and items, and the constant tables.
- **`tools/roo2gltf/roo2gltf.py`**: turns `.roo` files into glTF blockouts. It builds floors and ceilings from the BSP leaves and Doom-style wall sections, and handles slopes. It also writes the world positions of exits, arrivals, objects and spawn generators.
- **`tools/bgf2png/bgf2png.py`**: decodes BGF v10 files into PNGs. Sprites become contact sheets (one group per row, view angles in columns); textures are written un-rotated, along with a size catalog.
- **`tools/blender/render_glb.py`**: renders a preview of any `.glb` in headless Blender.
