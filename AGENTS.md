# AGENTS.md

Guidance for AI coding agents (and humans who like the detail) working in this repo. The [README](README.md) is the human introduction. This file covers how the project works, how to build and test it, and the rules to follow.

## What this is

**Unreal Meridian**: a free fan remaster of Meridian 59, built on the Server 104 ruleset, in Unreal Engine 5.8. Right now it's a faithful port: original rules, zones, textures and sounds, rebuilt in a modern engine. It's a client for Meridian servers (blakserv): it plays on the same server as our browser port, Meridian Shards ([ADR 0010](docs/adr/0010-meridian-servers.md)). The first playable demo covers the town of **Raza** and the zones around it (RIDs 300–308, 330–333). We plan to open-source as much of it as we're allowed once it's in a working state.

## Hard rules

- **Name.** The product is "Unreal Meridian" (UE module `UnrealMeridian`). The Server 104 team approved "Meridian" but **"104" must never appear in the product name or branding**. Referring to the `ReferenceServers/Server-104/` reference checkout or the Server 104 ruleset in code and docs is fine.
- **Original art and audio stay out of git.** We have permission from the Server 103 and 104 teams to use all original assets (different name, own server; both met), so upscaled or reworked originals may ship. Raw extracted files still go to `build/` (git-ignored) and are regenerated from the installed client by `tools/setup.ps1`.
- **No redistributable-restricted content in git.** Engine template content, anything from Fab (`Content/Fab/`), MetaHumans, and downloads in `UnrealAssets/` are git-ignored. This repo is public.
- **License: GPLv2 ([LICENSE](LICENSE)) with an Unreal Engine linking exception ([LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md)).** The exception lets the game ship with the engine, but it only covers code we own. GPL code from anyone else can't come in, or the game couldn't be distributed at all.
- **`ReferenceServers/Server-104/` is reference only.** It's a separate GPLv2 git repo, the Meridian 59 authors' code. Never link, copy or ship its code; read it, extract data with `tools/`, and re-implement.
- **Meridian Shards (`E:\2026_Experiments\meridian-browser`) is ours and GPLv2 too, so its own code may be copied or translated here.** That includes its protocol code (`packages/protocol`: framing, CRC, the security word, messages, the connection), the gateway, the asset cache and its UI logic.
  - **Except code Shards ported from the Meridian 59 source**: code that translates the original's own logic, such as `roo.ts` and `roomGeometry.ts` (`bspload.c`, `d3drender.c`), `bgf.ts` (`dibutil.c`), `movement.ts` (`move.c`) and `chess.ts` (`cmove.c`). That's the original authors' code, which our exception can't cover. Read the original, write the facts down with their source, and re-implement, as with Server-104.
  - A comment that only says where a wire layout, constant or file format comes from doesn't make code a port: those are facts needed to talk to the server. Code that follows the original's algorithms step by step is a port. When unsure, treat it as one.
  - Protocol facts go in [docs/research/blakserv-protocol.md](docs/research/blakserv-protocol.md), with their blakserv source.
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
| In-game UI: HUD (laid out as Meridian Shards' Modern interface), hotbars, inventory dialog, minimap, `Source/.../UI/`, `tools/ui/`, `data/ui/*.json` | [ADR 0009](docs/adr/0009-user-interface.md) ("The Shards layout") |
| Playing on servers: login screen, chat, the protocol, `Source/.../Net/`, `data/net/servers.json` | [ADR 0010](docs/adr/0010-meridian-servers.md), [docs/research/blakserv-protocol.md](docs/research/blakserv-protocol.md) |
| The admin console, admin and DM accounts, the test admin `ueadmin` | [docs/admin-console.md](docs/admin-console.md) |
| New content on our own server: rooms, spell schools, items, monsters; the server fork | [ADR 0011](docs/adr/0011-custom-server-content.md) (proposed) |
| Linux and Mac builds, packaging, platform testing: `tools/ue/package.ps1`, `package_mac.sh`, `Config/DefaultDeviceProfiles.ini` | [ADR 0011](docs/adr/0011-linux-and-mac-builds.md) |
| Client parity roadmap, milestones, runtime rooms, world coverage tiers, `docs/parity.md` (its last section lists what finished milestones left over: add to it, tick it off) | [ADR 0012](docs/adr/0012-client-parity-and-world-coverage.md), [docs/parity.md](docs/parity.md) |
| Performance: stutter, frame time, the warm-up (loading) screen, the hitch test, the sky's cost | [docs/performance.md](docs/performance.md) |
| The original data (scale, coordinates, zone layout, missing assets) | [docs/findings.md](docs/findings.md) |
| Monster, prop and character pipeline research | [docs/research/](docs/research/) |

When a decision changes a default, update both the skill and its ADR. New pipelines (creatures, props, spells) should get their own skill in `.claude/skills/` once they settle.

## Architecture in brief

- **Engine:** UE 5.8, gameplay in C++ on the Gameplay Ability System. Blueprints only for thin presentation.
- **Server:** a Meridian server, the unmodified Server 104 `blakserv` that Meridian Shards runs (locally and on its VM), reached through its WebSocket gateway. It is the authority: accounts, characters, rules, positions and saves. The game is a client (`Source/.../Net/`, [ADR 0010](docs/adr/0010-meridian-servers.md)). Each original room is a streaming level with `ZoneId` = Kod room ID (RID); a server room maps to its zone by `.roo` file.
- **One world, no UE server:** the game runs standalone (online or `-MROffline`), every zone loaded. The UE dedicated-server path was retired on 2026-10-08 (M9 of ADR 0012): no client-side zone streaming, no `-MRZoneTest` or `-MRSpriteNetTest`. Replicated properties stay because offline play runs through them. Supabase isn't used (ADR 0004 is superseded).
- **Data is text-first:** Python tools write JSON into `data/` (committed); UE imports it. Levels are generated by editor scripts.
- **Combat:** the server decides every hit. Aiming picks the target and the client sends `BP_REQ_ATTACK`; the swing is presentation ([ADR 0012](docs/adr/0012-client-parity-and-world-coverage.md)).
- **Parity and world coverage:** the road to the original client's full feature set is tracked in [docs/parity.md](docs/parity.md). A room is drawn authored, baked or (planned) built at runtime from the server's `.roo` ([ADR 0012](docs/adr/0012-client-parity-and-world-coverage.md)).
- **Characters:** players, NPCs and monsters are drawn like the original client: directional sprites composited from the original bgfs' pixels, recoloured at runtime, placed in the 3D world (`UMRSpriteBodyComponent`). No skeletal meshes or 3D animation.

## Repo layout

| Path | What it holds |
|---|---|
| `ReferenceServers/Server-104/` | Reference checkout of the original server and client. Separate git repo, git-ignored. Tools find it through `tools/server104.py`. |
| `tools/` | Python and PowerShell tools: read the original data, build art, drive Unreal |
| `data/` | Generated JSON (game data, zone layouts, environment/audio config). Committed. |
| `build/` | Generated meshes, previews, extracted reference art, AI model caches. Git-ignored; regenerate any time. |
| `art_src/` | Hand-made art sources (AI prop models, building overrides). LFS. |
| `game/UnrealMeridian/` | The UE 5.8 C++ project |
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

**Shortcuts:** the repo-root `package.json` wraps the common commands; `npm run` lists them. There's nothing to install. Pass options after `--`, e.g. `npm run lookdev -- -Label mytest -Compare baseline`.
- `npm run play` (offline, Raza Inn), `play:online` (the login screen), `editor`
- `npm run prop-gallery` (also `:night`, `:rebuild`)
- `npm test` (C++ unit tests, the movement test, then the step survey of our zones; no server needed), `test:all` (plus the online smoke test), `test:unit`, `test:move`, `test:steps` (`:all`), `test:net` (`:create`, `:pair`, `:death`), `test:ui`, `test:hitch` (frame times while playing)
- `npm run compile`, `world` (`world:clean`), `lookdev`, `lookdev:gallery`, `package`, `setup`
- `npm run release -- 0.2.0` (package, bump `ProjectVersion`, commit, tag, push and publish a GitHub release; `-Linux`, `-Draft`), `release:local` (zip the working tree to `build/release/`, no git or GitHub). See ADR 0011 "Releases".
- `npm run data` (Kod extract), `props:inventory`, `props:gallery-layout`, `props:overview`

The commands those run, step by step:

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
"G:/Unreal Engine/UE_5.8/Engine/Build/BatchFiles/Build.bat" UnrealMeridianEditor Win64 Development -Project="E:/2026_Experiments/meridian-unreal/game/UnrealMeridian/UnrealMeridian.uproject" -WaitMutex
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
- "Shards (online)" needs `app://unreal-meridian` in the VM's `GATEWAY_ORIGINS`.
- A new name and password make an account. `-MROffline` (and every visual test tour) plays locally without a server, as before.

WASD + mouse (running at the original's 12.9 m/s; `mr.Move.SpeedScale` scales every speed); the cursor stays free and aims, and holding the right button turns the view (`mr.Input.FreeCursor`, Options > Controls; the test tours aim with the middle of the view), hold Shift to walk, Space goes through the door you stand on (the original's "go"; edge exits are walked through), V cycles the view, Ctrl + wheel zooms, H hides the interface (and brings it back). 1–9 or the wheel select the hotbar slot (a click on a hotbar item wields or wears it; selecting doesn't), numpad 1–9 cast from the spell bar (online: at the target, else the crosshair's, else the next thing you click; \ for yourself, Esc stops), R rests or stands, U uses the selected item on the next thing you click, E or I opens the inventory dialog, - and = zoom the minimap, M the large map (notes), Enter chats (a line is said unless it's a command: `tell <name> ...`, `/yell`, `:emote`, `/who`, `/mail`, `/guild`, `/help`; Up and Down recall lines), O who is on, L mail, Y the guild, F1–F12 online run the quick chat (the original's defaults: help, rest, stand, the moods, wave, point, mail, who), Esc or F10 opens the menu (Who Is On, Mail, Guild, Options, Admin Console for admin and DM characters, Log Off to the character list, Quit; in PIE Esc stops play, so use F10); online the cursor (or, with the free cursor off, the crosshair) aims, left mouse attacks (the target, else what the crosshair is on, else the nearest within 5 squares), right mouse looks at what it's on, G picks it up, F opens or works it, T targets it, Tab or ] / Shift+Tab or [ cycle targets, \ targets yourself, Esc clears the target first. Offline, F5, F6, F7 and F9 play the wave, point, dance and cast clips. The keys can be changed in Options > Controls (Modern, or the original's: arrows walk and turn, Ctrl attacks); they're kept in `GameUserSettings.ini` (`Core/MRSettings`). Offline, `-MRSpriteEquip=bte,swordov@22:4,metlshld@32:2,helm@13,nohair` dresses the player (docs/sprites.md).

## Testing and verification

- **Online smoke test** (~1 min; needs the Shards dev stack, `npm run dev` in meridian-browser): a headless game logs in to the local server, enters a zone (checking it's built from the server's room), chats, takes an exit, fetches the room through the asset cache, finds itself in the players list, reloads its data, walks to the Inn and looks at Marcus and at itself (and rewrites its description), reads the Inn's news board, types a tell to itself and an emote, mails itself, asks the time, flips an option, asks for its guild and waves, puts a note on the large map, waves with F8, changes its password and back, keeps an item on its hotbar, wields, drops and picks up its mace and drops and picks up 10 shillings, checks its spells, the room's enchantments and its quests, casts appraise on its mace and meditates, rests and stands, buys a torch from Tomas and sells it back, puts its mace in Bentu's vault and takes it out, banks 10 shillings with Gamos, fights a bunny or a baby spider in the Outskirts until it dies (the server's hit lines, its swing, a damage number), travels through our zones and off Farol West's east edge into a room built at runtime (`c6.roo`, lit by the server's light) and back, checks the server's sounds and music were found, logs off to the character list and back in (finding that hotbar item again), and logs off (`MRNetTest: DONE 58/58`; with `-Render` it also turns to the Inn's sign, drawn by a prop, and checks the aim takes it, and photographs Farol West's flagpole: `DONE 60/60`; it also checks we wear the torso, arms and legs the server sends). `-Render` also writes `names.png`, `look_object.png`, `look_self.png`, `inventory.png`, `quests.png`, `choose_target.png`, `shop.png`, `offer.png`, `combat.png`, `combat_corpse.png`, `effect_*.png` (the screen effects), `runtime_room_0/1.png`, `flagpole.png` (Farol West's pole; its flag shows only when claimed), `news.png`, `mail.png`, `guild.png`, `who.png`, `options_graphics/controls/account.png` and `map.png`. Run it after touching `Source/.../Net/`, the login screen, zones or spawning. `-Render -Hold 40` stays in a game window with screenshots (`Saved/Screenshots/MRNet/`: the character page, the world, the dialog), for a look from the Shards client. Protocol unit tests: `-ExecCmds="Automation RunTests Meridian.Net;Quit"` (with `Meridian.Net.Chat`: typed lines, tells, groups, style codes); runtime rooms: `Meridian.World` (the C++ meshes against `roo2gltf`'s, every reference room, BGF decoding, scrolling, a room taking lifts and texture changes, `.ogg` decoding).

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_net_test.ps1
  ```

  `-Admin` (`DONE 9/9`; `10/10` with `-Render`, which photographs each page of the console: `admin*.png`) logs in as the local admin account `ueadmin` (password `ueadmin`; docs/admin-console.md), checks the server loaded the admin and DM modules, runs a server command and a DM command through the Admin Console and teleports with its Travel page. Run it after touching the console or `UMRNetSubsystem`'s staff commands. `-Create` uses a fresh account and makes its character through the creator's path (`DONE 60/60`, including "the server shows the new character's chosen face"). Run it after touching `Net/MRCharInfo`, the creator or `Net/MRNetLook`. `-Create` also deletes the new character at the end ("suicide"). `-Pair` (`DONE 65/65`) adds a second player: `tools/ue/second_player.ts`, a scripted Shards client (Node 24, imported from the meridian-browser checkout) on its own account `uenetpal`, character Unrealpal. It obeys tells that start "pal:": it tells back, broadcasts, waves and offers a shilling, so the test checks tells both ways, ignoring (a blocked tell), broadcasts and turning them off, another player's wave and a trade between players. Run it after touching chat, ignore or trades. `-Create -Death` (~4 min, `DONE 62/62`) also dies bare-handed to the Forest of Farol's spiders and comes back from the Underworld through its archway to Raza. Run it after touching death, room changes or runtime rooms; it isn't the default because dying costs the character.

- **Movement** (~1 min, an offline game window): the original's speeds, steps and ledge jumps, wading (half speed at depth 2, walking out of a pool onto a bank level with its floor, not onto a higher one), terrain steps (up 0.825 m running and walking, not 0.85 m, under a beam 1.66 m up) and, in a room built at runtime, its steps, the original's rule (up 1.51 m where there's no lower texture, not 3.99 m: the cap) and a one-way wall, checked in play (`MRMoveTest: DONE 23/23`). Run it after touching `Character/` movement, the collision blockout or `World/` collision:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_move_test.ps1
  ```

- **Step survey** (~15 s for our zones, ~30 min for every room; an offline game window): every crossing between two sectors that the original allows (its step rule, the player's 1.65 m height, the server's room check) and that climbs 10 cm or more or passes under less than 2 m, walked by probes with the player's capsule and movement. Our built zones use their real collision and props; `-Rooms all` builds every other reference room at runtime, as online. Failures are logged (`MRStepSurvey: FAIL ...`, with what blocked) and every crossing goes into `game/UnrealMeridian/Saved/MRStepSurvey/results.csv` (with `no_room` where something occupies the start). Our zones pass every crossing (`DONE 637/637`); every room passes 48,922 of 48,958 (2026-10-09; the rest in [docs/parity.md](docs/parity.md)). `-ExecCmds="mr.Move.DebugSteps 1"` logs each step-up decision and, when one is refused, what its sweeps met. Run it after touching movement, collision, `roo2gltf` or `MRRoomMesh`, or props with collision:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_step_survey.ps1              # -Rooms all, or -Rooms ke1.roo,i3.roo
  ```

- **Environment look-dev** (~45 s): renders the camera bookmarks in `data/environment/lookdev_cameras.json` and compares against an earlier label. Judge every visual change this way and send the sheets to the user with a recommendation:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_lookdev.ps1 -Label mytest -Compare baseline
  ```

  Add `-Profile -ResX 1920 -ResY 1080` for GPU timings, then `python tools/lookdev/profile_report.py mytest`. Other switches: `-GameHour`, `-Mood`, `-Weather storm`, `-Season`, `-Audio 12`, `-StartZone`. Compare only labels captured the same way.
- **Hitches** (~2 min, an offline game window, docs/performance.md):
  - `-MRHitchTour` plays at normal speed and records every frame from launch. It waits for the warm-up screen, then walks Raza, the Outskirts and three interiors, turning a full circle at each stop.
  - It prints p50/p95/p99 and the frames over 33 and 100 ms (`MRHitch: DONE ...`, `Saved/MRHitch/<label>.csv`). At 1080p on the maintainer's RTX 3070, p95 was about 17.5 ms with at most ~35 frames over 33 ms (2026-10-10).
  - Run it after touching rendering, the environment director, sprites, the warm-up or anything that ticks.
  - Options: `-Weather rain`, `-Exec "<cvars>"`, `-Extra -MRHitchCsv` (a per-pass GPU profile; read it with `python tools/lookdev/hitch_report.py`), `-Extra -MRHitchMaxFPS=60` (capped, as players run; uncapped runs on a 6-core machine show CPU stalls the cap hides).

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_hitch_test.ps1 -Label mytest -Compare before
  ```

- **UI** (~1 min): screenshots of the HUD and every dialog tab *with* the UI, into `build/ui/shots/<label>/sheet.png`. Run it after touching `Source/.../UI/` or `data/ui/`:

  ```bash
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_ui_shots.ps1 -Label mytest     # -StartZone 301: indoors
  ```

- **Prop gallery** (docs/adr/0007 "Prop gallery"): every prop and tree mesh beside its original sprite, in its own zone under the real sky and moods. `python tools/environment/prop_gallery.py`, then `build_world.ps1`; play it with `tools/ue/play_gallery.bat` (or `play_gallery.ps1 [-GameHour 23] [-Weather storm] [-Rebuild]`; the game's `-MRGallery`, offline), or capture with `run_lookdev.ps1 -Label <name> -Gallery`. Rerun the generator after adding or changing props.
- **Minimap pictures** (~2 min, a game window): `tools/ue/run_map_capture.ps1` captures every zone top-down and styles them (`build/minimap/review.png`); then `tools/ue/import_ui.ps1`. Recapture after changing a zone's art.
- **In-game test modes** (command-line flags on the game): `-MRNetTest`, `-MRMoveTest`, `-MRStepSurvey[=all|<rooms>]`, `-MRScreenshots`, `-MRProfile`, `-MRLookDev`, `-MRUIShots`, `-MRMapCapture`, `-MRHitchTour`, and `-MROffline` to play without a server. The test modes skip the warm-up (loading) screen; `mr.Warmup 0` skips it in play. Console: `MRBookmark <name>`, `MREnvReload`, `MRUIReload`, `mr.GameHour <h>`, and online `MREffect <n> [ms] [xlat]` (a server screen effect, as if sent; `mr.UI.DamageNumbers 0` hides damage numbers).
- **Visual work is decided by the user from images.** Show before/after sheets, recommend one option, keep the others reversible behind a data switch.

## C++ source map (`game/UnrealMeridian/Source/UnrealMeridian/`)

| Path | What it does |
|---|---|
| `Core/MRUnits.h` | Original grid and angle conversions to UE (must match `roo2gltf`) |
| `Core/MRWarmup` | The warm-up behind the loading screen (`SMRLoadingScreen`): waits for shader jobs, asset builds, pipeline precaching, the sprite atlases (preloaded here) and streaming textures (docs/performance.md) |
| `Core/MRSettings` | The player's settings in `GameUserSettings.ini`: the console variables the Options dialog sets (`MRSettings`), the key bindings with the Modern and Original presets (`MRKeys`) |
| `Core/MREditorScripting` | Helpers for the `tools/ue/` editor scripts where Python can't reach (`SetLowQualityCompression`: the sprite ramp atlases' 16-bit format) |
| `Abilities/MRAttributeSet` | GAS attributes: the six stats plus Health, Mana, Vigor |
| `Character/` | Player character (FP/TP camera, input in code), predicted run/walk (no sprint, crouch or jump), Space's "go"; the sprite body (`MRSpriteBodyComponent`, `MRSpriteData`) shared with monsters |
| `Player/` | Player state (owns the ASC and zone ID), controller (the UI keys' `IMC_UI`, from `MRKeys`; the quick chat's function keys), `AMRHUD` (shows the UI or, online, the login screen first; draws the first-person hands) |
| `UI/` | The Slate UI (ADR 0009): `UMRUISubsystem` (per local player), `UMRUIStyle` (`ui_style.json`, brushes, `MRPaint` frames and the HUD's rounded panels, the Heidelberg title font), the HUD after Meridian Shards' Modern interface (`SMRHUDFrames`: `SMRUnitFrame`, `SMRTargetFrame`, `SMRMapCluster` with the round `SMRMinimap`, `SMRActionBar`; `ui_style.json` `"hud"`, HUD size `mr.UI.HudSize`), `UMRGameDataSubsystem` (items, spells, skills JSON), `UMRInventorySource` / `UMRMockInventory` (the seam; Minecraft's click rules), widgets `SMRHUDRoot`, `SMRSlot`, `SMRInventoryScreen`, `SMRMinimap`, `SMRLoginScreen`, `SMRLoadingScreen` (the warm-up's "Compiling shaders (n left)"), `SMRCharCreator` (the character creator, `data/ui/char_create.json`), `SMRChatLog` (tabs, the server's colours, the typed line; `MRUIChat.cpp` runs its commands), `SMRSocial` (the who, mail, news, guild and large map windows), `SMROptions` (graphics, with lighting, shadows, effects, textures, grass, resolution scale and the frame limit; sound, keys, game, quick chat, account), `SMRGameMenu` (the Escape menu), `SMRAdminConsole` (the Admin Console: travel, players, DM and server commands; docs/admin-console.md), `SMRWorldOverlay` (names, target brackets, crosshair), `SMRLookDialog` (Look), `SMREnchantments` (enchantment icons), `SMRStatChange` (retraining), `SMRTradeDialog` (shops, offers, the vault, the bank), `UMRNetInventory` (the source online: the server's inventory, equipment and hotbar layout, spells and skills), the shared `MRUI::Label`, `SMRPanel`, `SMRTextButton`, `SMRTextField`, `SMRControls` (`SMRSlider`, `SMRTextBox`, `SMRSelectList`), `AMRAvatarPreview` (the inventory avatar; the creator's body and face portrait) |
| `Net/` | Playing on Meridian servers (ADR 0010): `MRProtocol` (frames, CRC, security word, redbook token), `MRResources` (the server's rsb, message formatting), `MRCharInfo` (character creation: `BP_CHARINFO`, `BP_NEW_CHARINFO`, the rules), `MRChatCommands` (typed lines: the original's commands, tells, tell groups, aliases, the say filter), `MRNetLook` (a player's sprite appearance from its overlays), `MRNetWorld` (the client's world model: player, room objects, players list, stats, and the message readers), `FMRAssetCache` (the server's game files from its manifest, hash-checked, cached in `Saved/MRNet/<server>/assets/`), `FMRConnection` (WebSocket, login, pings, back to the menu on `BP_QUIT`), `UMRNetSubsystem` (servers, game data download, characters and creation, the session's messages, chat and its channels, ignoring, mail kept per character, news, guilds, the server-kept options, Log Off, the staff modules and commands: `BP_REQ_ADMIN`, `BP_REQ_DM`, `SAY_DM`), `UMRNetWorldSubsystem` (zone, pawn, movement up, exits, an `AMRNetObject` for every object (our sprite, its bitmap, or a world-build prop on its square), targets and the crosshair's aim, the attack key's choice, the server's swings, projectiles (`AMRNetProjectile`) and its effects on the pawn and the weather) |
| `Zones/MRZoneSubsystem` | Zone data, tile and edge exits, shared-geometry zones, teleports, level streaming |
| `Game/MRGameMode`, `Game/MRGameState` | Spawns at the Raza Inn (offline) or where the server puts the character (`SpawnOnlinePlayer`); replicated weather state |
| `Environment/MRGameTimeSubsystem` | Meridian time from UTC (2-hour days), day phases, seasons |
| `Environment/MREnvironmentSubsystem` | Client day/night director: blends `moods.json` by hour, sun/moon, lamps, stars |
| `Environment/MRWeather`, `MRPrecipitationActor` | The original's storm rolls; GPU rain and snow |
| `Environment/MRFireActor`, `MRFireSubsystem` | Flame sprites and flickering, distance-culled fire lights |
| `Environment/MRScatterActor` | Instanced grass placed by the world build |
| `Audio/MRAudioSubsystem` | The original's music, room loops and ambient sounds per zone; online, the server's music and sounds instead (`SetServerDriven`, `ServerMusic`, `ServerSound`) |
| `Audio/MRServerSound` | The server's `.ogg` sounds decoded at runtime (`MRServerSound::Decode`) and played by `UMRServerSoundWave` |
| `Monsters/` | `AMRMonster` (sprite body, stand-in AI) and `UMRMonsterSubsystem` (spawns from the zone data) |
| `World/` | Rooms built at runtime from the server's files (ADR 0012): `MRRooFile` (.roo reader), `MRBgf` (.bgf reader, palette, transient textures), `MRRoomMesh` (the port of `roo2gltf`'s meshing), `AMRRuntimeRoom` (the room: draw and collision meshes, scrolling as UV1; keeps its room file and rebuilds for the server's lifts, sector and texture changes), `UMRRuntimeRooms` (fetch, build, keep, prefetch; zone RIDs 100000 + Kod RID), `UMRBgfSpriteComponent` (a creature drawn from its own bitmap) |
| `Tests/` | `MRNetTest` (online), `MRNetTests` and `MRCharInfoTests` (protocol and creation unit tests), `MRWorldTests` (runtime rooms, the server's wading overrides), `MRMoveTest`, `MRStepSurvey` (every step, with `AMRStepProbe`), `MRScreenshotTour`, `MRProfileTour`, `MRLookDevTour`, `MRSpriteClipTour`, `MRMonsterTour`, `MRUIShots`, `MRMapCapture`, `MRHitchTour` (frame times while playing) |

## Zones and streaming

- `L_World` holds only lighting and sky. Each zone is a streaming sublevel `Generated/Maps/Zones/L_Zone_<rid>_<KodClass>` (e.g. `L_Zone_307_RazaBar`). Raza town and the Outskirts share `L_Zone_300`. The name is built in two places that must agree: `tools/ue/build_world.py` `zone_level_path()` and `UMRZoneSubsystem`.
- Zones sit on a 2 km grid (`data/zone_layout.json` `world_origin_cm`); characters cull at 300 m, so Iris never replicates players in other zones.
- The game loads every zone (`LoadAllZoneLevels`). A spawn or a teleport waits up to 8 s for its zone's level to show, so nobody drops through a floor still loading.
- In development the game reads `data/zones.json` and `data/zone_layout.json` from the repo; a packaged build reads `game/UnrealMeridian/Data/`.
- Coordinates: blockout glTF is metres, x east, y up, z south. UE is cm, X east, Y south, Z up: UE = `world_origin_cm` + (100x, 100z, 100y).

## Tools reference

- `tools/kod_extract/` — `kodparse.py` (case-insensitive Kod reader with inheritance) and `extract.py` (zones, monsters, NPCs and shops, spells, skills, items, constants, and `charinfo.json`: what the character creator offers; `net/rooms.json`: every Kod room, its `.roo` and exit targets, for runtime rooms; `sprites/equipment.json`: every bitmap an item puts on a player; `net/npcs.json`: who buys, sells, banks or keeps a vault). Demo zones are `DEMO_RIDS`.
- `tools/roo2gltf/roo2gltf.py` — `.roo` to glTF blockouts (BSP floors/ceilings, Doom-style walls, slopes, the original client's UV rules) plus world positions of exits, objects, spawns and wading areas, and a collision blockout without the walls the original lets you walk through (`<rid>_<class>_collision.glb`). `--walls-only` writes just the minimap's wall lines (`build/zones/<rid>_<class>_walls.json`).
- `tools/bgf2png/bgf2png.py` — BGF v10 decoder: sprite contact sheets, un-rotated textures, size catalog; `--palette` writes `data/runtime/palette.bin` (git-ignored) for runtime rooms.
- `tools/blender/` — `render_glb.py` (previews), `prop_glb.py` (AI prop previews and normalising), `build_zone_art.py` + `zone_detail.py` + `zone_grime.py` (rebuilt buildings; `art_src/environment/zones/<rid>/<Building>.blend` overrides, `--seed-override`), `build_grass_kit.py`, `build_tree_kit.py` (procedural trees, ADR 0007 "Trees"), `build_prop_kit.py` (rain and smoke quads), `check_overlaps.py`.
- `tools/environment/` — pure-Python helpers: `blockout.py`, `facades.py` (opening review sheet), `scatter.py`, `shelter.py`, `fires.py`, `chimneys.py`.
- `tools/textures/` — `make_placeholders.py` (upscale with `4xTextures_GTAV_rgt-s_dither`, Real-ESRGAN fallback; Marigold or rule-based normals; incremental), `make_tree_textures.py` (leaf atlas and bark from a tree sprite), `make_skyboxes.py` (the original client's skyboxes, `resource/sky*.bsf`, as atlases for the sky dome; ADR 0005 "Sky"), `ai_maps.py` (runs in `build/texai/.venv`), `upscale.py`, `setup_ai.ps1`.
- `tools/ue/` — `build_world.ps1/.py`, `build_cache.py`, `run_in_editor.py`, `environment_materials.py`, `build_runtime_materials.py` (just `M_RuntimeRoom` and the MPC), `zone_mood.py`, `build_audio.py`, `run_lookdev.ps1`, `package.ps1`, `release.ps1`, `run_net_test.ps1`, `run_move_test.ps1`, `run_step_survey.ps1`, `import_sprites.ps1`, `import_ui.ps1`, `run_ui_shots.ps1`, `run_map_capture.ps1`.
- `tools/aigen/` — sprite → 3D assets, one manifest per asset (`data/aigen/<kind>/<name>.json`). `inventory.py` maps a zone's placed objects to sprites. `aigen.py` runs the steps over one asset, `a,b,c` or `all`: sprite, restyle, `batch-submit` / `batch-collect` (OpenAI Batch API), tripo-prepare, `bridge-collect`, choose, normalize, overview. It re-runs itself in `build/texai/.venv`. Also `sprite.py`, `restyle.py` (OpenAI, Vertex, Gemini, fal), `tripo.py`, `blender_link.py` (the open Blender via its MCP add-on socket, plus the Tripo DCC Bridge log), `review.py`, `style/style.md`. Blender side: `tools/blender/prop_glb.py` (preview, normalize).
- `tools/sprites/` — player and monster sprites (docs/sprites.md): `build_player_sprites.py` (part atlases and ramp atlases, palette lookups, the equipment; the original pixels since 2026-10-08, or the upscale and in-betweens by `store` in `data/sprites/upscale.json`; `--with-ai` adds the AI test looks), `upscale_parts.py` (the upscale methods, chosen per part in `data/sprites/upscale.json`), `face_sheet.py` (creator faces under each method), `equipment_sheet.py` (armour, weapons, shields and helmets: `build/sprites/equipment/`), `monsters.py`, `run_sprite_tour.ps1`, `run_sprite_clips.ps1`; imported by `tools/ue/import_sprites.ps1`.
- `tools/lookdev/` — `compare.py`, `profile_report.py`, `hitch_report.py` (the hitch test's CSV profile), `suggest_cameras.py`, `cycle_test.ps1`, `mood_test.ps1`, `upscaler_test.ps1`, `ai_maps_test.ps1`.
- `tools/audio/` — `extract_audio.py` (sound data from Kod), `audio_report.py`.
- `tools/ui/` — the in-game UI's art (ADR 0009): `build_ui_art.py` (the original interface bitmaps as frame pieces, per upscale variant; also copies the installed client's `Heidelb1.ttf` to `data/runtime/fonts/` for the HUD's names), `build_icons.py` (item, spell and skill icons), `review_ui_art.py` (variant sheet), `minimap.py` (minimap styles from the captures), `shots_sheet.py`. Unreal side: `tools/ue/import_ui.ps1/.py`, `run_ui_shots.ps1`, `run_map_capture.ps1`.
- `tools/server104.py` — where the Server-104 checkout is (`ReferenceServers/Server-104`, or an older `Server-104/`).

Full commands, flags and caches for the environment tools: [zone-environment/reference/tools.md](.claude/skills/zone-environment/reference/tools.md).

## Known traps

- Before debugging anything visual, check [pitfalls.md](.claude/skills/zone-environment/reference/pitfalls.md).
- **The old name:** until 2026-10-09 the project was "Meridian Remastered" (module `MeridianRemastered`, `game/MeridianRemastered/`, origin `app://meridian-remastered`). `Config/DefaultEngine.ini`'s `[CoreRedirects]` keeps assets saved before the rename loading; drop it once `build_world.ps1 -Clean` has re-saved everything.
- **Game data folder:** `tools/ue/package.ps1` copies `data/` into `game/UnrealMeridian/Data/` and leaves it there. Until 2026-10-09 development runs preferred that copy, so they read stale JSON after any packaging. `UMRZoneSubsystem::GetDataDir` now uses the repo's `data/` whenever the game isn't cooked; only a packaged build reads the copy.
- **Lighting defaults to Medium, 60 fps** (`Config/DefaultGameUserSettings.ini`: Lumen's cheaper gather, screen-space reflections; docs/performance.md). Interiors never use bounce lighting: their moods set `dynamic_global_illumination_method` `None`, and `raza_afternoon` sets `Lumen` back for outdoors. A new outdoor base mood must do the same. A `-ExecCmds="sg.…"` in a test run is saved into `Saved/Config/WindowsEditor/GameUserSettings.ini` and carries into later runs: put it back. The grass's runtime switches are `mr.Grass.Density`, `mr.Grass.Shadows` and `mr.Grass.Distance`.
- **The sky is the original skyboxes, not volumetric clouds:** the clouds cost up to 55 ms a frame (docs/performance.md). `moods.json` `"sky"` `"volumetric_clouds": true` brings them back. The skybox textures need colour compression: the importer takes a blue sky for a normal map (BC5, drawn green), so `environment_materials.py` `_skyboxes` sets it.
- A GPU crash (`DXGI_ERROR_DEVICE_HUNG`) in Nanite on the first frame of a look-dev run is a known engine issue, not your change; run it again.
- A material helper used by a master must be in `_master`'s cache key in `environment_materials.py`, or the master won't rebuild.
- An audio component must be held by a `UPROPERTY`, or it gets garbage-collected and the sound stops.
- **Online, the rsb:** the client needs *the server's* `rsc0000.rsb` (downloaded from its `assets` URL into `Saved/MRNet/<server>/`): the security redbook and every name come from it. A wrong one shows as "the first message after each echo ping decodes, the next is garbage".
- **Online, the security word:** every game message sent steps the security streams; never drop or reorder one after `FMRConnection::Send`. One wrong security word and blakserv hangs up.
- **Online, rooms we haven't built:** built at runtime from the server's files (`UMRRuntimeRooms`). They need `data/runtime/palette.bin` (`bgf2png.py --palette`) and `/Game/Generated/Runtime/M_RuntimeRoom` (`build_world.ps1`). A creature without a converted sprite is drawn from its own bitmap.
- **Online, edge exits:** the server takes one only for a move into a sector past the room's box; a step into the void is snapped back (`docs/research/blakserv-protocol.md`).
- **The player is 1.65 m tall:** the original's `player.height` (0.75 square, also its eye; the server's `OBJECTHEIGHTROO`) is what a passage or a step under a ceiling must leave. The capsule is that tall less the 2 cm the movement floats over the floor (`UMRCharacterMovementComponent::CapsuleHeightCm`, 162), with the eye 1.65 m up. The old 1.8 m capsule stopped in doorways and under beams the original walks through.
- **Runtime room collision is one-sided, wound like the built zones':** `MRRoomMesh` used to wind every face backwards (hidden by the two-sided material) and the collision was cooked double-sided to make up for it. That stopped every step-up in a room built at runtime: online, no step at all could be climbed outside our zones (found by the step survey, 2026-10-09).
- **Steps follow the original's rule at each wall** (`UMRCharacterMovementComponent::StepUp`, `UMRZoneSubsystem::StepWallAt`; `step_walls` in `zone_layout.json` from `roo2gltf`, `MRRoomMesh::StepWalls` for runtime rooms): a side without a lower texture never blocks a step, otherwise the higher floor at the wall's *first end*, less the far side's wading sink, may be 24 Kod units above where you stand. A step it allows may be taller than ours (untextured walls, sloped walls measured at their low end): climbed up to `mr.Move.StepCapCm` (300; the desert's cliffs are 34 m). The pawn rises only to the floor ahead (a low ceiling past a step stopped the full rise). Props and test boxes keep UE's own step.
- **Wading:** collision floors in a wading sector are lowered by its sink (0.44, 0.88, 1.32 m), so the movement may step up as far as the pool sinks you (`WadingStepHeight`). The server's depth overrides (`BP_PLAYER`; only the Temple of Riija's bridge) are absolute heights, applied to runtime rooms.
- **Collision walls like the original's checks:** upper walls only where the side you come from has an upper texture; a middle wall only toward a side whose sidedef isn't passable (a wall can be passable one way: Kocatan, the nests). Runtime rooms' floors are walkable up to 89 degrees (the original has no slope limit); props keep UE's 45.
- **Online, no resync:** blakserv's game-mode resync can't complete (a signed-char compare), so a broken stream ends the session. Never send `BP_RESYNC`.
- **Online, runtime room light and scroll:** `M_RuntimeRoom` needs `MPC_Environment`'s `RoomAmbient` and `PlayerLight` and reads UV1 as scroll speed. After changing it, rebuild it with `build_world.ps1 -Script build_runtime_materials.py`. Vertex alpha 0 (bitmap sprites) skips the room light model.
- **Online, two Origin headers:** the engine's WebSocket client (libwebsockets, `LwsWebSocket.cpp`) always sends `Origin: <host>` before the `app://unreal-meridian` we add, and the engine can't be changed. The local dev gateway allows every origin, so only a server with `GATEWAY_ORIGINS` set notices: before 2026-10-09 the Shards gateway compared the joined header and refused us (403, "Couldn't reach the server."). Its `tools/gateway/gateway.ts` now lets a request through if any of its origins is allowed.
- **Online, local Vite:** the local Vite server listens on `[::1]:5173` only; use `localhost`, not `127.0.0.1`, in `data/net/servers.json`.

## Writing docs

Keep docs plain and concrete: short sentences, real paths and commands, the reason in a clause. Record decisions and experiments in the matching ADR (with sheet paths); keep how-to recipes in skills; keep the README for people.
