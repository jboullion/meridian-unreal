# AGENTS.md

Guidance for AI coding agents (and humans who like the detail) working in this repo. The [README](README.md) is the human introduction. This file covers how the project works, how to build and test it, and the rules to follow.

## What this is

**Meridian Remastered**: a free fan remaster of Meridian 59, built on the Server 104 ruleset, in Unreal Engine 5.8. Right now it's a faithful port: original rules, zones, textures and sounds, rebuilt in a modern engine. It's a client for Meridian servers (blakserv): it plays on the same server as our browser port, Meridian Shards ([ADR 0010](docs/adr/0010-meridian-servers.md)). The first playable demo covers the town of **Raza** and the zones around it (RIDs 300–308, 330–333). We plan to open-source as much of it as we're allowed once it's in a working state.

## Hard rules

- **Name.** The product is "Meridian Remastered" (UE module `MeridianRemastered`). The Server 104 team approved "Meridian" but **"104" must never appear in the product name or branding**. Referring to the `ReferenceServers/Server-104/` reference checkout or the Server 104 ruleset in code and docs is fine.
- **Original art and audio stay out of git.** We have permission from the Server 103 and 104 teams to use all original assets (different name, own server; both met), so upscaled or reworked originals may ship. Raw extracted files still go to `build/` (git-ignored) and are regenerated from the installed client by `tools/setup.ps1`.
- **No redistributable-restricted content in git.** Engine template content, anything from Fab (`Content/Fab/`), MetaHumans, and downloads in `UnrealAssets/` are git-ignored. This repo will be public.
- **`ReferenceServers/Server-104/` is reference only.** It's a separate GPLv2 git repo. Never link, copy or ship its code; read it, extract data with `tools/`, and re-implement.
- **Meridian Shards (`E:\2026_Experiments\meridian-browser`) is GPLv2 too.** Never copy or translate its code into this repo. The protocol in `Source/.../Net/` is written from reading blakserv and from our notes in [docs/research/blakserv-protocol.md](docs/research/blakserv-protocol.md); add new facts there, with their blakserv source.
- **Own servers only.** Never point the game or a test at the live Server 104 (or 103) server; use ours (`data/net/servers.json`).
- **Secrets.** Supabase credentials live in the repo-root `.env`. Never print, log or commit its values.
- **Commits.** The maintainer makes the commits. Don't `git commit` or push unless explicitly asked (asking for a PR counts); finish by listing the changed files.
- **Downloads.** Ask before downloading anything (models, packs, tools), stating file, source and size. AI models go in `build/texai/models/`.
- **Generated content.** Anything a script writes into Unreal goes under `/Game/Generated/` (git-ignored). Hand-made assets go elsewhere under `Content/` and are tracked with Git LFS (`.uasset`, `.umap`, `.blend`, `.fbx`, `.glb`, `.png`, ...; see `.gitattributes`).
- **The blockout is the source of truth.** `roo2gltf` output defines layout and collision. Never hand-edit generated output; fix the generator or layer art on top.
- **Stay close to the original look.** Colours, motifs and recognizable facades. Prefer depth from geometry and lighting over depth inferred from painted textures.

## Skills and decision records

Load these before working in their area. Skills are the current *how-to*; ADRs are the *why* and the decision log.

| Area | Read first |
|---|---|
| Environment art: zones, textures, facades, roofs, grime, moods, look-dev, `data/environment/*.json` | Skill [.claude/skills/zone-environment/](.claude/skills/zone-environment/SKILL.md) (with [data-files](.claude/skills/zone-environment/reference/data-files.md), [tools](.claude/skills/zone-environment/reference/tools.md), [pitfalls](.claude/skills/zone-environment/reference/pitfalls.md)); log in [ADR 0003](docs/adr/0003-environment-art-pipeline.md) |
| Props and monsters from sprites: upscale, AI restyle, Tripo image-to-3D, `tools/aigen/`, `data/aigen/`, `props.json` `mesh_ai` | Skill [.claude/skills/sprite-to-3d/](.claude/skills/sprite-to-3d/SKILL.md) (with [tripo](.claude/skills/sprite-to-3d/reference/tripo.md), [prompts](.claude/skills/sprite-to-3d/reference/prompts.md), [pitfalls](.claude/skills/sprite-to-3d/reference/pitfalls.md)); log in [ADR 0007](docs/adr/0007-ai-prop-pipeline.md) |
| Engine, server architecture, data pipeline | [ADR 0001](docs/adr/0001-engine-and-architecture.md) |
| Characters, monsters, appearance, animation (sprites) | [docs/sprites.md](docs/sprites.md), [ADR 0008](docs/adr/0008-sprite-characters.md) |
| Hosting, Supabase, costs | [ADR 0004](docs/adr/0004-hosting-and-operations.md) |
| Time of day, weather, fire, atmosphere | [ADR 0005](docs/adr/0005-time-weather-and-atmosphere.md) |
| Audio | [ADR 0006](docs/adr/0006-audio.md) |
| In-game UI: HUD, hotbars, inventory dialog, minimap, `Source/.../UI/`, `tools/ui/`, `data/ui/*.json` | [ADR 0009](docs/adr/0009-user-interface.md) |
| Playing on servers: login screen, chat, the protocol, `Source/.../Net/`, `data/net/servers.json` | [ADR 0010](docs/adr/0010-meridian-servers.md), [docs/research/blakserv-protocol.md](docs/research/blakserv-protocol.md) |
| The original data (scale, coordinates, zone layout, missing assets) | [docs/findings.md](docs/findings.md) |
| Monster, prop and character pipeline research | [docs/research/](docs/research/) |

When a decision changes a default, update both the skill and its ADR. New pipelines (creatures, props, spells) should get their own skill in `.claude/skills/` once they settle.

## Architecture in brief

- **Engine:** UE 5.8, gameplay in C++ on the Gameplay Ability System. Blueprints only for thin presentation.
- **Server:** a Meridian server, the unmodified Server 104 `blakserv` that Meridian Shards runs (locally and on its VM), reached through its WebSocket gateway. It is the authority: accounts, characters, rules, positions and saves. The game is a client (`Source/.../Net/`, [ADR 0010](docs/adr/0010-meridian-servers.md)). Each original room is a streaming level with `ZoneId` = Kod room ID (RID); a server room maps to its zone by `.roo` file.
- **Legacy:** the old UE dedicated-server path (multiplayer PIE, `-MRZoneTest`, `-MRSpriteNetTest`) still works but is being retired. Supabase isn't used (ADR 0004 is superseded).
- **Data is text-first:** Python tools write JSON into `data/` (committed); UE imports it. Levels are generated by editor scripts.
- **Combat:** full action. A hit needs physical contact; the original Offense/Defense roll then decides a full or glancing hit. Skills improve with the original `ImproveAbility` formulas.
- **Characters:** players, NPCs and monsters are drawn like the original client: directional sprites composited from upscaled original bgfs, recoloured at runtime, placed in the 3D world (`UMRSpriteBodyComponent`). No skeletal meshes or 3D animation.

## Repo layout

| Path | What it holds |
|---|---|
| `ReferenceServers/Server-104/` | Reference checkout of the original server and client. Separate git repo, git-ignored. Tools find it through `tools/server104.py`. |
| `tools/` | Python and PowerShell tools: read the original data, build art, drive Unreal |
| `data/` | Generated JSON (game data, zone layouts, environment/audio config). Committed. |
| `build/` | Generated meshes, previews, extracted reference art, AI model caches. Git-ignored; regenerate any time. |
| `art_src/` | Hand-made art sources (AI prop models, building overrides). LFS. |
| `game/MeridianRemastered/` | The UE 5.8 C++ project |
| `backend/supabase/` | Database migrations and policies (not created yet) |
| `docs/` | ADRs, findings and research notes |
| `UnrealAssets/`, `ReferenceImages/` | Local-only downloads and references. Git-ignored. |

## Machine setup

Defaults are the maintainer's machine; pass overrides where the scripts accept them (e.g. `tools/setup.ps1 -EngineRoot`).

- UE 5.8 at `G:\Unreal Engine\UE_5.8`; Visual Studio 2022 or 2026 with the C++ game workload
- Python 3.11+ with Pillow; Blender 5.x (`H:\Steam\steamapps\common\Blender`)
- Original art is found in this order: the Server 104 client (`%LOCALAPPDATA%\Meridian-104\resource`), the Steam classic client, then `ReferenceServers/Server-104/resource`. Room geometry comes from `ReferenceServers/Server-104/resource/rooms`; the original interface bitmaps from its `module/merintr/bitmap` and `clientd3d/bitmap`.
- Shells: commands below are PowerShell or bash-compatible as written; the `.ps1` scripts need `powershell -NoProfile -ExecutionPolicy Bypass -File`.

## Build and run

Fresh clone; rebuilds everything generated (data, extracted art, blockouts, binaries, sprite atlases, `Content/Generated/`):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1     # -SkipData keeps data/ and build/, -SkipBuild skips compiling
```

Data and blockouts, step by step (run `bgf2png` before `roo2gltf` so blockouts pick up real texture sizes):

```bash
python tools/kod_extract/extract.py                  # Kod -> data/{zones,monsters,spells,skills,items,constants}.json
python tools/bgf2png/bgf2png.py --textures-for-zones # demo-room textures -> build/textures/
python tools/roo2gltf/roo2gltf.py --preview          # .roo -> build/zones/*.glb (+ PNG maps), data/zone_layout.json
python tools/bgf2png/bgf2png.py cow rat mummy        # creature sprites -> build/bgf/<name>/
blender -b --factory-startup -P tools/blender/render_glb.py -- build/zones/300_Raza.glb out.png
```

Environment art (details and the full workflow in the zone-environment skill):

```bash
powershell -File tools/textures/setup_ai.ps1                                  # once: upscaler + Marigold env (build/texai/, ~5 GB; ask first)
python tools/textures/make_placeholders.py                                   # textures, height/normal maps, macro noise (incremental; --force)
blender -b --factory-startup -P tools/blender/build_zone_art.py -- --rid 300 # rebuilt buildings -> build/environment/
blender -b --factory-startup -P tools/blender/build_grass_kit.py
python tools/textures/make_tree_textures.py                                   # leaf atlas + bark for the procedural trees
blender -b --factory-startup -P tools/blender/build_tree_kit.py
blender -b --factory-startup -P tools/blender/build_prop_kit.py
```

Compile the editor target:

```bash
"G:/Unreal Engine/UE_5.8/Engine/Build/BatchFiles/Build.bat" MeridianRemasteredEditor Win64 Development -Project="E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" -WaitMutex
```

C++ changes need a compile and an editor restart.

Build the world level (`/Game/Generated/Maps/L_World`) from the blockouts and environment art:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/build_world.ps1   # -Clean rebuilds all; -Headless ignores the open editor; -Script zone_mood.py refreshes lighting only
```

- **Incremental:** every generated texture, material, mesh and level is keyed by a hash of its inputs (`tools/ue/build_cache.py`, `Saved/MRBuild/world_cache.json`). Only what changed is rebuilt.
- **In the open editor** when one is running (Python remote execution, `tools/ue/run_in_editor.py`). Stop Play-In-Editor first. Unsaved edits to generated maps are discarded.
- **Otherwise headless**, starting on the engine's empty Entry map. Never open `L_World` in a headless editor: it has crashed the GPU driver.

To play: open the project and press Play with Net Mode "Play Standalone". The login screen offers the servers in `data/net/servers.json`:
- "Local (dev)" needs the Shards dev stack (`npm run dev` in `E:\2026_Experiments\meridian-browser`: blakserv, the gateway on `ws://localhost:8059`, the game files on `http://localhost:5173/assets`).
- "Shards (online)" needs `app://meridian-remastered` in the VM's `GATEWAY_ORIGINS`.
- A new name and password make an account. `-MROffline` (and every visual test tour) plays locally without a server, as before.

WASD + mouse (running at the original's 12.9 m/s; `mr.Move.SpeedScale` scales every speed), hold Shift to walk, Space goes through the door you stand on (the original's "go"; edge exits are walked through), V cycles the view, Ctrl + wheel zooms. 1–9 or the wheel select the hotbar slot, numpad 1–9 cast from the spell bar, E or I opens the inventory dialog, - and = zoom the minimap, Enter chats. Test emotes: F5, F6, F7, F9.

## Testing and verification

- **Online smoke test** (~1 min; needs the Shards dev stack, `npm run dev` in meridian-browser): a headless game logs in to the local server, enters a zone, chats, takes an exit and logs off (`MRNetTest: DONE 5/5`). Run it after touching `Source/.../Net/`, the login screen, zones or spawning. `-Render -Hold 40` stays in a game window with screenshots (`Saved/Screenshots/MRNet/`: the character page, the world, the dialog), for a look from the Shards client. Protocol unit tests: `-ExecCmds="Automation RunTests Meridian.Net;Quit"`.

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_net_test.ps1
  ```

- **Legacy network smoke test** (~2 min; starts a UE dedicated server and a headless client, walks the demo's exits, exits 0 on success). Run it after touching zones, streaming, spawning or replication while the UE-server path still exists:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_zone_test.ps1
  ```

  The client logs `MRStreaming: entered zone N, ready=1|0`; any `ready=0` fails the test.
- **Movement** (~1 min, an offline game window): the original's speeds, steps and ledge jumps, checked in play (`MRMoveTest: DONE 8/8`). Run it after touching `Character/` movement or the collision blockout:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_move_test.ps1
  ```

- **Environment look-dev** (~45 s): renders the camera bookmarks in `data/environment/lookdev_cameras.json` and compares against an earlier label. Judge every visual change this way and send the sheets to the user with a recommendation:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_lookdev.ps1 -Label mytest -Compare baseline
  ```

  Add `-Profile -ResX 1920 -ResY 1080` for GPU timings, then `python tools/lookdev/profile_report.py mytest`. Other switches: `-GameHour`, `-Mood`, `-Weather storm`, `-Season`, `-Audio 12`, `-StartZone`. Compare only labels captured the same way.
- **UI** (~1 min): screenshots of the HUD and every dialog tab *with* the UI, into `build/ui/shots/<label>/sheet.png`. Run it after touching `Source/.../UI/` or `data/ui/`:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_ui_shots.ps1 -Label mytest     # -StartZone 301: indoors
  ```

- **Minimap pictures** (~2 min, a game window): `tools/ue/run_map_capture.ps1` captures every zone top-down and styles them (`build/minimap/review.png`); then `tools/ue/import_ui.ps1`. Recapture after changing a zone's art.
- **In-game test modes** (command-line flags on the game): `-MRNetTest`, `-MRZoneTest`, `-MRMoveTest`, `-MRScreenshots`, `-MRProfile`, `-MRLookDev`, `-MRUIShots`, `-MRMapCapture`, and `-MROffline` to play without a server. Console: `MRBookmark <name>`, `MREnvReload`, `MRUIReload`, `mr.GameHour <h>`.
- **Visual work is decided by the user from images.** Show before/after sheets, recommend one option, keep the others reversible behind a data switch.

## C++ source map (`game/MeridianRemastered/Source/MeridianRemastered/`)

| Path | What it does |
|---|---|
| `Core/MRUnits.h` | Original grid and angle conversions to UE (must match `roo2gltf`) |
| `Abilities/MRAttributeSet` | GAS attributes: the six stats plus Health, Mana, Vigor |
| `Character/` | Player character (FP/TP camera, input in code), predicted run/walk (no sprint, crouch or jump), Space's "go"; the sprite body (`MRSpriteBodyComponent`, `MRSpriteData`) shared with monsters |
| `Player/` | Player state (owns the ASC and zone ID), controller (client zone streaming, the UI keys' `IMC_UI`), `AMRHUD` (shows the UI or, online, the login screen first; draws the first-person hands) |
| `UI/` | The Slate UI (ADR 0009): `UMRUISubsystem` (per local player), `UMRUIStyle` (`ui_style.json`, brushes, `MRPaint` frames), `UMRGameDataSubsystem` (items, spells, skills JSON), `UMRInventorySource` / `UMRMockInventory` (the seam; Minecraft's click rules), widgets `SMRHUDRoot`, `SMRSlot`, `SMRInventoryScreen`, `SMRMinimap`, `SMRLoginScreen`, `SMRChatLog`, the shared `MRUI::Label`, `SMRPanel`, `SMRTextButton`, `SMRTextField`, `AMRAvatarPreview` |
| `Net/` | Playing on Meridian servers (ADR 0010): `MRProtocol` (frames, CRC, security word, redbook token), `MRResources` (the server's rsb, message formatting), `FMRConnection` (WebSocket, login, pings), `UMRNetSubsystem` (servers, game data download, characters, the room's objects, chat), `UMRNetWorldSubsystem` (zone, pawn, movement up, exits, `AMRNetObject` sprites) |
| `Zones/MRZoneSubsystem` | Zone data, tile and edge exits, shared-geometry zones, teleports, level streaming |
| `Game/MRGameMode`, `Game/MRGameState` | Spawns at the Raza Inn (offline) or where the server puts the character (`SpawnOnlinePlayer`); replicated weather state |
| `Environment/MRGameTimeSubsystem` | Meridian time from UTC (2-hour days), day phases, seasons |
| `Environment/MREnvironmentSubsystem` | Client day/night director: blends `moods.json` by hour, sun/moon, lamps, stars |
| `Environment/MRWeather`, `MRPrecipitationActor` | The original's storm rolls; GPU rain and snow |
| `Environment/MRFireActor`, `MRFireSubsystem` | Flame sprites and flickering, distance-culled fire lights |
| `Environment/MRScatterActor` | Instanced grass placed by the world build |
| `Audio/MRAudioSubsystem` | The original's music, room loops and ambient sounds per zone |
| `Monsters/` | `AMRMonster` (sprite body, stand-in AI) and `UMRMonsterSubsystem` (spawns from the zone data) |
| `Tests/` | `MRNetTest` (online), `MRNetTests` (protocol unit tests), `MRZoneSmokeTest`, `MRMoveTest`, `MRScreenshotTour`, `MRProfileTour`, `MRLookDevTour`, `MRSpriteNetTest`, `MRSpriteClipTour`, `MRMonsterTour`, `MRUIShots`, `MRMapCapture` |

## Zones and streaming

- `L_World` holds only lighting and sky. Each zone is a streaming sublevel `Generated/Maps/Zones/L_Zone_<rid>_<KodClass>` (e.g. `L_Zone_307_RazaBar`). Raza town and the Outskirts share `L_Zone_300`. The name is built in two places that must agree: `tools/ue/build_world.py` `zone_level_path()` and `UMRZoneSubsystem`.
- Zones sit on a 2 km grid (`data/zone_layout.json` `world_origin_cm`); characters cull at 300 m, so Iris never replicates players in other zones.
- The server loads every zone. A client keeps its zone and every zone one exit away loaded; zones just left stay loaded 30 s.
- The server only spawns or teleports a player into a zone the client reports visible; otherwise it calls `ClientPrepareZone` and waits up to 8 s.
- In development the game reads `data/zones.json` and `data/zone_layout.json` from the repo; a packaged build reads `game/MeridianRemastered/Data/`.
- Coordinates: blockout glTF is metres, x east, y up, z south. UE is cm, X east, Y south, Z up: UE = `world_origin_cm` + (100x, 100z, 100y).

## Tools reference

- `tools/kod_extract/` — `kodparse.py` (case-insensitive Kod reader with inheritance) and `extract.py` (zones, monsters, NPCs and shops, spells, skills, items, constants). Demo zones are `DEMO_RIDS`.
- `tools/roo2gltf/roo2gltf.py` — `.roo` to glTF blockouts (BSP floors/ceilings, Doom-style walls, slopes, the original client's UV rules) plus world positions of exits, objects, spawns and wading areas, and a collision blockout without the walls the original lets you walk through (`<rid>_<class>_collision.glb`). `--walls-only` writes just the minimap's wall lines (`build/zones/<rid>_<class>_walls.json`).
- `tools/bgf2png/bgf2png.py` — BGF v10 decoder: sprite contact sheets, un-rotated textures, size catalog.
- `tools/blender/` — `render_glb.py` (previews), `prop_glb.py` (AI prop previews and normalising), `build_zone_art.py` + `zone_detail.py` + `zone_grime.py` (rebuilt buildings; `art_src/environment/zones/<rid>/<Building>.blend` overrides, `--seed-override`), `build_grass_kit.py`, `build_tree_kit.py` (procedural trees, ADR 0007 "Trees"), `build_prop_kit.py` (rain and smoke quads), `check_overlaps.py`.
- `tools/environment/` — pure-Python helpers: `blockout.py`, `facades.py` (opening review sheet), `scatter.py`, `shelter.py`, `fires.py`, `chimneys.py`.
- `tools/textures/` — `make_placeholders.py` (upscale with `4xTextures_GTAV_rgt-s_dither`, Real-ESRGAN fallback; Marigold or rule-based normals; incremental), `make_tree_textures.py` (leaf atlas and bark from a tree sprite), `ai_maps.py` (runs in `build/texai/.venv`), `upscale.py`, `setup_ai.ps1`.
- `tools/ue/` — `build_world.ps1/.py`, `build_cache.py`, `run_in_editor.py`, `environment_materials.py`, `zone_mood.py`, `build_audio.py`, `run_lookdev.ps1`, `run_net_test.ps1`, `run_move_test.ps1`, `run_zone_test.ps1`, `import_sprites.ps1`, `run_sprite_net_test.ps1`, `import_ui.ps1`, `run_ui_shots.ps1`, `run_map_capture.ps1`.
- `tools/aigen/` — sprite → 3D assets, one manifest per asset (`data/aigen/<kind>/<name>.json`). `inventory.py` maps a zone's placed objects to sprites. `aigen.py` runs the steps over one asset, `a,b,c` or `all`: sprite, restyle, `batch-submit` / `batch-collect` (OpenAI Batch API), tripo-prepare, `bridge-collect`, choose, normalize, overview. It re-runs itself in `build/texai/.venv`. Also `sprite.py`, `restyle.py` (OpenAI, Vertex, Gemini, fal), `tripo.py`, `blender_link.py` (the open Blender via its MCP add-on socket, plus the Tripo DCC Bridge log), `review.py`, `style/style.md`. Blender side: `tools/blender/prop_glb.py` (preview, normalize).
- `tools/sprites/` — player and monster sprites (docs/sprites.md): `build_player_sprites.py` (upscaled part atlases, in-betweens, palette lookups), `monsters.py`, `run_sprite_tour.ps1`, `run_sprite_clips.ps1`; imported by `tools/ue/import_sprites.ps1`.
- `tools/lookdev/` — `compare.py`, `profile_report.py`, `suggest_cameras.py`, `cycle_test.ps1`, `mood_test.ps1`, `upscaler_test.ps1`, `ai_maps_test.ps1`.
- `tools/audio/` — `extract_audio.py` (sound data from Kod), `audio_report.py`.
- `tools/ui/` — the in-game UI's art (ADR 0009): `build_ui_art.py` (the original interface bitmaps as frame pieces, per upscale variant), `build_icons.py` (item, spell and skill icons), `review_ui_art.py` (variant sheet), `minimap.py` (minimap styles from the captures), `shots_sheet.py`. Unreal side: `tools/ue/import_ui.ps1/.py`, `run_ui_shots.ps1`, `run_map_capture.ps1`.
- `tools/server104.py` — where the Server-104 checkout is (`ReferenceServers/Server-104`, or an older `Server-104/`).

Full commands, flags and caches for the environment tools: [zone-environment/reference/tools.md](.claude/skills/zone-environment/reference/tools.md).

## Known traps

- Before debugging anything visual, check [pitfalls.md](.claude/skills/zone-environment/reference/pitfalls.md).
- A GPU crash (`DXGI_ERROR_DEVICE_HUNG`) in Nanite on the first frame of a look-dev run is a known engine issue, not your change; run it again.
- A material helper used by a master must be in `_master`'s cache key in `environment_materials.py`, or the master won't rebuild.
- An audio component must be held by a `UPROPERTY`, or it gets garbage-collected and the sound stops.
- Packaging a standalone dedicated server needs a source-built engine; until then use PIE's dedicated-server mode.
- **Online, the rsb:** the client needs *the server's* `rsc0000.rsb` (downloaded from its `assets` URL into `Saved/MRNet/<server>/`): the security redbook and every name come from it. A wrong one shows as "the first message after each echo ping decodes, the next is garbage".
- **Online, the security word:** every game message sent steps the security streams; never drop or reorder one after `FMRConnection::Send`. One wrong security word and blakserv hangs up.
- **Online, missing content:** a server room with no zone of ours (outside the demo RIDs) leaves the player where they were, with a warning; a creature without a converted sprite isn't drawn.
- **Online, local Vite:** the local Vite server listens on `[::1]:5173` only; use `localhost`, not `127.0.0.1`, in `data/net/servers.json`.

## Writing docs

Keep docs plain and concrete: short sentences, real paths and commands, the reason in a clause. Record decisions and experiments in the matching ADR (with sheet paths); keep how-to recipes in skills; keep the README for people.
