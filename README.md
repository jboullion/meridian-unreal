# Meridian Remastered

This is a free fan remaster of Meridian 59, built on the Server 104 ruleset, using Unreal Engine 5.8. The first playable demo covers the town of **Raza** and the zones around it.

- **Decisions:** [docs/adr/0001-engine-and-architecture.md](docs/adr/0001-engine-and-architecture.md)
- **Findings from the original data** (scale, zone layout, missing assets): [docs/findings.md](docs/findings.md)
- **Environment art pipeline** (terrain, buildings, materials, lighting): [docs/adr/0003-environment-art-pipeline.md](docs/adr/0003-environment-art-pipeline.md)
- **Characters** (appearance system, MetaHumans, first person): [docs/characters.md](docs/characters.md)

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

Environment art for the world build (see [ADR 0003](docs/adr/0003-environment-art-pipeline.md), "Raza look-dev"):

```bash
python tools/textures/make_placeholders.py                                   # textures, normals, heights, macro noise (incremental; --force)
blender -b --factory-startup -P tools/blender/build_zone_art.py -- --rid 300 # rebuilt buildings (the Hall) -> build/environment/
blender -b --factory-startup -P tools/blender/build_grass_kit.py              # grass tufts -> build/environment/kit/
blender -b --factory-startup -P tools/blender/build_prop_kit.py               # lamp post, brazier -> build/environment/kit/
```

## Game project (`game/MeridianRemastered`)

Requires UE 5.8 (`G:\Unreal Engine\UE_5.8`) and Visual Studio 2022 or 2026 with the C++ game workload.

```bash
# compile the editor target
"G:/Unreal Engine/UE_5.8/Engine/Build/BatchFiles/Build.bat" MeridianRemasteredEditor Win64 Development -Project="E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" -WaitMutex
```

Rebuild the world level (`/Game/Generated/Maps/L_World`) from the zone blockouts and environment art (run `roo2gltf` and the environment-art steps first):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/build_world.ps1
```

- **Incremental.** Each generated texture, material, mesh and level is stored with a hash of its inputs (`tools/ue/build_cache.py`, `Saved/MRBuild/world_cache.json`). Only what changed is rebuilt, in place. Re-exporting one building re-imports that one mesh; levels that place it are untouched.
- **In the open editor.** If the editor is open on the project, the build runs inside it through Python remote execution (`tools/ue/run_in_editor.py`, enabled in `DefaultEngine.ini`, this machine only). There's no startup cost and the viewport updates. Stop Play-In-Editor first. A level that must be rebuilt is opened and the map you had open comes back afterwards; unsaved edits to generated maps are discarded.
- **Otherwise headless.** With no editor open, it starts a headless editor on the engine's empty Entry map (opening `L_World` in a headless editor has crashed the GPU driver), prints a summary and quits.
- `-Clean` deletes everything generated and builds from scratch. `-Headless` never uses the open editor.

Run the network smoke test. It starts a dedicated server and a headless client, walks the player through the demo's exits, and exits 0 if every step passes. It takes about 2 minutes, including a 35 s wait that lets the client unload zones before a long-distance teleport:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_zone_test.ps1
```

Environment look-dev: render the camera bookmarks in `data/environment/lookdev_cameras.json` and compare against an earlier label (about 45 s). Each camera is saved once its image has settled, with clouds and wind frozen so runs are comparable; `-Settle 3` waits a fixed 3 s per camera instead. Change only the lighting with `build_world.ps1 -Script zone_mood.py`, which skips the full rebuild:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_lookdev.ps1 -Label mytest -Compare baseline
```

Add `-Profile -ResX 1920 -ResY 1080` to also measure each camera with everything on, Nanite tessellation off and grass hidden. Then summarise the GPU passes:

```bash
python tools/lookdev/profile_report.py mytest
```

**To play:** open the project in the editor and press Play with Net Mode set to "Play As Client" (2+ players).
- Move with WASD and the mouse.
- Shift sprints (drains Vigor), Caps Lock toggles walking, C crouches, Space jumps.
- V or the mouse wheel switches between first and third person.

| Source folder | What it holds |
|---|---|
| `Core/MRUnits.h` | Original grid and angle conversions to UE (must match `roo2gltf`) |
| `Abilities/MRAttributeSet` | GAS attributes: the six stats plus Health, Mana and Vigor |
| `Character/` | Player character (FP/TP camera, input built in code), predicted walk/run/sprint movement, and the data-driven appearance (MakeHuman body on the mannequin skeleton, head sliders, hair part); see [docs/characters.md](docs/characters.md) |
| `Player/` | Player state (owns the Ability System Component and the zone ID) and controller (drives client zone streaming) |
| `Zones/MRZoneSubsystem` | Zone data, tile and edge exits, seamless shared-geometry zones, teleports, zone level streaming |
| `Game/MRGameMode` | Spawns new characters at the Raza Inn |
| `Tests/MRZoneSmokeTest` | The `-MRZoneTest` server-side travel test |
| `Tests/MRScreenshotTour`, `Tests/MRProfileTour` | `-MRScreenshots` visual check and `-MRProfile` character cost measurement |
| `Tests/MRLookDevTour` | `-MRLookDev` environment captures from fixed cameras; console `MRBookmark <name>` logs a new camera |
| `Environment/MRScatterActor` | Instanced decoration (grass tufts) placed by the world build |

### Zone streaming

`L_World` is a persistent level that holds only lighting and sky. Every zone's geometry is its own streaming sublevel, `Generated/Maps/Zones/L_Zone_<rid>`; the town and Outskirts share `L_Zone_300`.

- **Server:** loads every zone at startup.
- **Client:** keeps its current zone and every zone one exit away loaded and visible, so taking an exit is a same-frame switch. Zones it has just left stay loaded for 30 s.
- **Spawning and teleports:** the server only spawns or teleports a player into a zone that player's client has reported as visible. Otherwise it asks the client to stream the zone (`ClientPrepareZone`) and waits, up to 8 s.
- **Relevancy:** Iris's spatial filter does it. Zones sit 2 km apart and characters cull at 300 m, so players in other zones are never replicated.

The client logs `MRStreaming: entered zone N, ready=1|0`. The smoke test fails on any `ready=0`.

The game reads `data/zones.json` and `data/zone_layout.json` directly from the repo during development. A packaged build reads them from `game/MeridianRemastered/Data/`.

## Tools

- **`tools/kod_extract/kodparse.py`**: a case-insensitive reader for Kod source. It handles constants, resources, classvars, properties and message bodies, and resolves inheritance.
- **`tools/kod_extract/extract.py`**: extracts the demo zones (exits, placed objects, spawns), monsters and NPCs (including shop lists), all spells, skills and items, and the constant tables.
- **`tools/roo2gltf/roo2gltf.py`**: turns `.roo` files into glTF blockouts. It builds floors and ceilings from the BSP leaves and Doom-style wall sections, and handles slopes. It also writes the world positions of exits, arrivals, objects and spawn generators.
- **`tools/bgf2png/bgf2png.py`**: decodes BGF v10 files into PNGs. Sprites become contact sheets (one group per row, view angles in columns); textures are written un-rotated, along with a size catalog.
- **`tools/blender/render_glb.py`**: renders a preview of any `.glb` in headless Blender.
- **`tools/blender/build_zone_art.py`**: rebuilds the buildings listed in `data/environment/zone_<rid>.json` as real geometry (recessed windows, proud trims, solid merlons) from the blockout and the painted-feature map in `data/environment/facades.json`. Optional per-building detail (`tools/blender/zone_detail.py`): timber beams extruded from the texture, tile-by-tile roofs, window boxes, or displacement baked into the mesh. A hand-edited `art_src/environment/zones/<rid>/<Building>.blend` overrides the generated building; `--seed-override <Building>` starts one from the generated mesh.
- **`tools/blender/build_grass_kit.py`**: grass tuft meshes for the ground scatter.
- **`tools/blender/build_prop_kit.py`**: meshes for Kod-placed objects listed in `data/environment/props.json` (lamp posts, braziers), spawned with their lights by the world build.
- **`tools/environment/`**: shared pure-Python helpers: blockout glb reading and the rebuilt-face selection (`blockout.py`), facade outlines with an overlay per texture and a one-image review sheet of every opening (`facades.py`, writes `build/environment/facade_check/`), and seeded ground scatter (`scatter.py`).
- **`tools/textures/make_placeholders.py`**: tier-0 textures from the originals: upscaled base colour, normal and height maps (masonry heights from a mortar/stone split), and the macro-variation noise.
- **`tools/ue/zone_mood.py`**, **`tools/ue/run_lookdev.ps1`**, **`tools/lookdev/compare.py`**: lighting moods from `data/environment/moods.json`, and the look-dev capture and comparison loop.
- **`tools/blender/install_mpfb_packs.py`**: installs MakeHuman asset packs (zips) into Blender's MPFB extension.
- **`tools/blender/mpfb_character.py`**: builds a MakeHuman character kit on the UE5 mannequin skeleton: one body mesh with head-slider morph targets, hairstyles, textures and a manifest. Settings live in `tools/blender/characters/*.json`.
- **`tools/blender/preview_character.py`**: renders a kit with random head sliders.
- **`tools/blender/retarget_ual.py`**: retargets the Quaternius Universal Animation Library (CC0) onto the UE5 mannequin skeleton.
- **`tools/ue/export_mannequin.py`**: exports the engine mannequins to FBX for Blender.
- **`tools/ue/import_character.ps1`**: imports a kit into UE, with material instances and an appearance asset.
- **`tools/ue/import_animations.ps1`**: imports the retargeted clips and makes montages.

Characters and animation are covered in [docs/characters.md](docs/characters.md).
