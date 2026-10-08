# ADR 0012: Client parity and world coverage

- Status: Accepted
- Date: 2026-10-08
- Builds on: [ADR 0010](0010-meridian-servers.md) (the game is a client of Meridian servers)
- Amends: [ADR 0001](0001-engine-and-architecture.md) decision 7 (action combat)

## Context

The Raza vertical slice plays online against blakserv. These parts work:
- login, character select and creation;
- entering rooms, walking, exits;
- creature sprites;
- chat;
- stats from the server.

Most of the rest of the original desktop client is missing or runs on mock data. There is no real inventory, no combat beyond an animation, no Look, shops, trade, mail, guilds, settings or Escape menu. Server sound, light, effect and room-change messages aren't handled either. The row-by-row state is in [docs/parity.md](../parity.md).

Only 13 of the roughly 395 rooms the server serves have a UE zone. In any other room the client logs "staying put" (`Net/MRNetWorldSubsystem.cpp`, the `Rid = 0` branch). The pawn then stops sending movement, out of step with the server, and the only way out is closing the game.

**The goal** is parity with the original desktop client, plus a way to walk the whole world before every zone has its remastered art.

**Findings from the 2026-10-08 survey:**
- Our `roo2gltf` parses and meshes all 362 rooms in `ReferenceServers/Server-104/resource/rooms`, with no failures:
  - 329 s for the render meshes, and roughly double that with collision;
  - most of the time goes into `wall_light`'s per-wall scan of every leaf.
- The Shards client parses all 362 rooms at runtime in the browser. A runtime path works.
- The server's assets URL already serves every `.roo` (395) and `.bgf` (3,517), each with a hash in `manifest.json`. We download only `rsc0000.rsb` from it today.
- Online, a zone needs only geometry, an origin, a grid size and wading areas, because exits are server-driven. `UMRZoneSubsystem` already treats a zone with no streaming level as ready.
- The game has no runtime mesh code, no C++ `.roo` reader and no C++ BGF decoder.
- The 33 rooms the server serves that aren't in `Server-104/resource/rooms` (e.g. jasper, east1–5, labyrinth) are in the client folder or the assets URL.

## Decision

### 1. Three tiers per room

| Tier | What it is | How it's made |
|---|---|---|
| **Authored** | The full remaster (Raza today) | The zone-environment skill, with hand data |
| **Baked** | A streaming level built automatically from the blockout: upscaled textures, default mood, no hand data | Batch `roo2gltf` → `build_world` |
| **Runtime** | The server's `.roo`, downloaded and meshed in C++ when the player arrives | Nothing to build |

**How the client picks a tier.** It uses `data/zones/coverage.json` and the levels in the build:
- An authored or baked level is used when it exists **and** the room checksum it was built from matches the security value in `BP_PLAYER`.
- Otherwise the room is built at runtime.

So when a server changes a room, players still get a correct room, just not the remastered one.

### 2. Runtime rooms

- **Clean-room readers.** The C++ `.roo` and `.bgf` readers (`World/MRRooReader`, `World/MRBgfReader`) are written from our own format notes:
  - `docs/research/roo-format.md` and `docs/research/bgf-format.md`;
  - the notes come from reading clientd3d and blakserv, and cite their sources.
  - **Never translate** `roomedit/roogen/roofile.py` (GPL) or Shards' `roo.ts` / `bgf.ts` (GPLv2).
- **The mesh builder** (`World/MRRoomMeshBuilder`) is a C++ port of **our own** `roo2gltf.py` functions `build_room_mesh`, `wall_uvs` and `SectorHeights`. It emits:
  - one section per texture;
  - sector light as vertex colour;
  - a collision version without `WF_PASSABLE` walls;
  - lowered wading floors and depth areas;
  - geometry grouped by sector, so server room changes (§5) can move it.
- **The room actor** (`AMRRuntimeRoom`):
  - two `UProceduralMeshComponent`s (the `ProceduralMeshComponent` plugin): one section per texture to draw, and a hidden collision mesh;
  - complex collision on `ECC_WorldStatic`, so `TraceFloor`, the capsule and step-up work as they do now;
  - its material (`/Game/Generated/Runtime/M_RuntimeRoom`) is unlit: the texture, made at runtime from the BGF through the palette with nearest filtering and index 254 cut out, times the sector light.
- **Zone hookup:**
  - `UMRZoneSubsystem::AddRuntimeZone` adds the room as a zone with an origin in a row of slots 16 km north of the built zones;
  - the "staying put" branch becomes: download, build, then place the pawn, with a loading overlay meanwhile;
  - the last 3 rooms stay built, and neighbouring rooms (from `data/net/rooms.json`) are prefetched.
- **Runtime sprites.** A creature or item with no pre-imported atlas gets its frames from the same BGF reader. It is drawn with the original look instead of not at all.

### 3. The asset cache

`Net/MRAssetCache` generalises the rsb download:
- it fetches any file listed in the server's `manifest.json`;
- files go to `Saved/MRNet/<server>/assets/`;
- each file is checked against its hash, and several download in parallel.

Runtime rooms, sprites and server sounds use it. The files always match the server, because they come from it.

### 4. Combat: aim picks the target

- blakserv decides every hit; the client only names a target in `BP_REQ_ATTACK`.
- **The target** is the aim target (crosshair or facing, in range), or failing that the nearest attackable within the original's reach. Click and Tab targeting work too.
- The sprite swing is presentation only.
- The "a hit needs physical contact" rule from ADR 0001 decision 7 no longer decides anything online.

### 5. The server drives the world

These server messages are handled on every tier:
- sound, music and light messages;
- room changes: `BP_SECTOR_MOVE`, `BP_WALL_ANIMATE`, `BP_CHANGE_TEXTURE`, the scrolls;
- the `.roo`'s own scrolling and animated textures.

On baked and authored zones this needs `roo2gltf` to emit sector-tagged movable pieces. Our local ambience and moods stay as a remaster layer on top.

### 6. Order of work

| Milestone | Scope |
|---|---|
| M0 | Foundations: one client world model (`Net/MRNetWorld`) that the UI and world read; `UMRNetInventory` behind the `UMRInventorySource` seam; wait and invalidate; room checksum; the asset cache; an Escape menu with Log off (done 2026-10-08, see Log) |
| M1 | Runtime rooms and sprites: whole-world travel |
| M2 | Draw every object; name plates; targeting; Look; equipment overlays |
| M3 | Inventory and items |
| M4 | Combat, death and effects |
| M5 | Spells, skills, enchantments, stats |
| M6 | NPCs, economy, player trade |
| M7 | Server sound, light and room changes; texture animation |
| M8 | Chat modes, who list, mail, news, guilds, emotes |
| M9 | Options and rebinding, account, polish, admin (optional), retiring the UE-server path |
| W2 | Alongside, after M1: the baked tier for every room |
| W3 | Authored zones promoted one at a time, in an order the maintainer sets |

**W2's steps:**
1. `extract.py --all` replaces the `DEMO_RIDS` gate.
2. Rooms come from the client folder or the assets URL.
3. `roo2gltf` gets:
   - a spatial index for `wall_light`;
   - a square world grid;
   - checksums in `zone_layout.json`;
   - sector groups and scroll metadata.
4. All ~1,355 grid textures go through `bgf2png` and `make_placeholders`.
5. `build_world` runs in region chunks, within a package-size budget recorded in [ADR 0011](0011-linux-and-mac-builds.md).

## Consequences

- **Players can go anywhere** once M1 lands. Rooms just look plainer until they are baked or authored.
- **Runtime rooms don't have** Nanite, the remastered textures or hand-placed props. They use the original light model under our global mood and sky.
- **Every message milestone adds** protocol notes and round-trip unit tests (`Meridian.Net`). A new `Meridian.World` test parses and meshes every cached room.
- **The demo's local gameplay** (`AMRMonster` placeholder hits, the GAS skeleton) becomes offline-only. Whether GAS stays as a client-side mirror is decided in M4.
- **Each milestone is its own change.** It ends with its `docs/parity.md` rows updated and the changed files listed for the maintainer to commit.

## Verification

Each milestone adds a `-Scenario` to `tools/ue/run_net_test.ps1`, all run against the local Shards stack:

| Scenario | What it checks |
|---|---|
| `travel` | Walk out of the demo into runtime rooms; the pawn stands on the floor and the server accepts movement |
| `inventory` | Get, use and drop an item |
| `combat` | Attack a rat until it dies |
| `cast` | Cast a self spell |
| `shop` | Buy and sell in Raza |
| `chat` | Tell, group and mail |

The existing movement, UI-shot and look-dev runs stay green. Look-dev compares runtime and baked versions of the same room. The Shards client, on the same server, is the reference for behaviour.

## Log

### M0, foundations (2026-10-08)
**What landed:**
- **`Net/MRNetWorld`:** the client's model of the session (player, room objects, logged-on players, stat groups), with readers that unit tests can call without a server.
  - Objects keep every field the server sends: amount, drawing effect, name colour, light, translation or effect, animations, and the motion record.
  - `BP_PLAYER` is read in full: lights, background, wading sound, flags, depths.
- **Session messages:**
  - `BP_WAIT` / `BP_UNWAIT` pause movement.
  - `BP_INVALIDATE_DATA` reloads the room, the players and the stats (`UMRNetSubsystem::ReloadData`).
  - `BP_PLAYERS` / `ADD` / `REMOVE` keep the players list.
  - `BP_CHANGE` reads its new motion record.
- **Room checksum:** `tools/roo2gltf` writes each zone's `.roo` security value to `data/zone_layout.json` (`roo_security`). `UMRNetWorldSubsystem` compares it with `BP_PLAYER`'s on every room (`DoesRoomMatchServer`).
- **`Net/MRAssetCache`:** fetches any file in the server's `manifest.json` into `Saved/MRNet/<server>/assets/`, checked by hash (the first 16 hex digits of SHA-1), 8 downloads at a time.
- **Log Off to the character list** (`ReturnToCharacters`): `BP_REQ_QUIT`, `BP_QUIT`, then the server's new `AP_GETCHOICE` and `AP_REQ_GAME`, staying connected.
- **The UI:**
  - the Escape menu (`UI/SMRGameMenu`, Esc or F10);
  - `UMRNetInventory` online: the server's spells and skills, a local spell bar, and no mock items ([ADR 0009](0009-user-interface.md)).

**Changed from the plan:** "BP_RESYNC instead of dropping the session" turned out to be impossible.
- blakserv's game-mode beacon match compares a signed `char` with byte 255, so it never completes.
- A test against the local server got no reply in 60 s.
- A broken stream, or a `BP_RESYNC` from the server, ends the session. Details: [blakserv-protocol.md](../research/blakserv-protocol.md), "Session".

**Verification (local Shards stack), M0:**
- All 18 `Meridian.*` automation tests pass, including the new `Meridian.Net.World`: round-trips of `BP_PLAYER`, room objects, `BP_CHANGE`, players, the room security value and the manifest hash.
- `run_net_test.ps1` reports **DONE 10/10**; with `-Create`, 11/11. The new steps:
  - our zone is built from the server's room (its security value);
  - `raza.roo` downloads through the asset cache and is read back from it;
  - the players list has us;
  - reloading the data brings the room back and chat still works;
  - Log Off returns to the character list and the character enters again.
- `run_move_test.ps1` reports 8/8.
- UI shots `build/ui/shots/m0/` (`18_game_menu.png`); online shots `Saved/Screenshots/MRNet/online_4.png` (the online inventory) and `online_5.png` (the menu with Log Off).

### M1, whole-world travel (2026-10-08)
**What landed:**
- **Format notes:** [roo-format.md](../research/roo-format.md) and [bgf-format.md](../research/bgf-format.md), from reading `clientd3d`.
- **`World/MRRooFile`, `World/MRBgf`:** clean readers of rooms and bitmaps. `MRBgf` also makes a transient texture of a bitmap through the palette.
- **The palette:** the original client compiles it in and no server serves one. `tools/bgf2png/bgf2png.py --palette` writes `data/runtime/palette.bin` (git-ignored; `tools/setup.ps1` runs it, and `package.ps1` copies `data/`).
- **`World/MRRoomMesh`:** the C++ port of our own `roo2gltf` meshing.
  - Same triangles, in the same order. The only difference is the winding: the switch from glTF's axes to UE's is a mirror, so faces are flipped to keep floors facing up, as UE's glTF import does for the built zones.
  - Its sector lookup uses a grid instead of scanning every leaf. All 362 reference rooms build in 1 s, against about 5½ minutes in Python.
- **`World/MRRuntimeRoom`, `World/MRRuntimeRooms`:**
  - the actor, and the subsystem that fetches the `.roo` and its `grdNNNNN.bgf` files through the asset cache, builds them, registers the zone, keeps the last three rooms and prefetches the neighbours.
  - A runtime zone's RID is 100000 + the room's Kod RID (`data/net/rooms.json`, a new output of `tools/kod_extract/extract.py`: all 429 Kod rooms with their exits' targets).
  - The material comes from `environment_materials.build_runtime_room_material`, built by `build_world.ps1`; `/Game/Generated/Runtime` is in `DirectoriesToAlwaysCook`.
- **`UMRNetWorldSubsystem`:**
  - a room we haven't built, or whose security value differs from the one ours was built from, becomes a runtime room;
  - the pawn is frozen and the HUD says "Loading <room>..." until it's ready.
- **`World/MRBgfSpriteComponent`:** a creature with no sprite of ours is drawn from the server's own bitmap.
  - It is an upright quad turned to the camera, showing the bitmap for its group (the server's animation records, standing and moving) at the view slot the original would pick, its feet where the original puts them.
  - Before, such creatures weren't drawn at all.

**Changed from the plan:**
- `ProceduralMeshComponent` instead of `UDynamicMeshComponent`: it has sections and cooked complex collision built in.
- The sky (`.bsf`, `BP_BACKGROUND`) moves to M7: runtime rooms show our sky meanwhile.
- `room_links.json` became `rooms.json`, which also names each room.

**Found on the way:** an edge exit needs a move into a sector past the room's box ([blakserv-protocol.md](../research/blakserv-protocol.md), "In the game").

**Verification (local Shards stack):**
- `Meridian.World.Rooms`:
  - the C++ meshes of all 13 built zones equal `roo2gltf`'s `.glb` files triangle for triangle, render and collision;
  - every room's security value and grid size equal `zone_layout.json`'s;
  - all 362 reference rooms parse and build.
- `Meridian.World.Bgf`: 150 textures decode to `bgf2png`'s sizes, shrink and transparency, and the palette loads.
- All 20 `Meridian.*` automation tests pass.
- `run_net_test.ps1` reports **DONE 15/15** (16/16 with `-Create`). The new travel step:
  - walks from wherever the character is through our zones to Farol West, then off its east edge;
  - the Forest of Farol (`c6.roo`, Kod RID 536) is built from the server's files in about 0.03 s as zone 100536;
  - the pawn stands on its floor, every creature there is drawn (spiders and living trees, from their bitmaps), chat works;
  - walking off its west edge comes back to Farol West.
- `run_move_test.ps1` reports 8/8 (one run of four gave 7/8: a flaky step not touched by M1). UI shots `build/ui/shots/m1/` are unchanged.
- Pictures (`run_net_test.ps1 -Render`): `Saved/Screenshots/MRNet/runtime_room_0.png` (the forest) and `runtime_room_1.png` (a spider drawn from `spider.bgf`).

**Not yet:**
- the minimap of a runtime room (it says "no map"): M2;
- scrolling and animated textures, server room changes, the sky: M7;
- geometry grouped by sector (for moving lifts and doors, §5): the mesh is grouped by texture for now; M7 splits moving sectors out;
- lighting beyond the sector light (sprites drawn from bitmaps are full bright): M7;
- packaging with the new plugin and cook directory is untested (a package build takes over an hour).

## Alternatives considered

- **Bake every room before allowing travel:** no runtime code, but hours of GPU texture work and hundreds of levels to import before anyone can leave Raza. A room changed on the server would also break until rebuilt.
- **Runtime rooms only, no baked tier:** less pipeline, but every room outside the authored ones keeps the original's flat look forever.
- **Port the Shards or roofile readers:** fastest, but both are GPL, and this repo is not.
- **Ship the original `.roo` and `.bgf` files in the package instead of downloading them:** works offline, but can drift from the server's build. The asset cache downloads only what is missing or changed, so it can still start from bundled files later.
