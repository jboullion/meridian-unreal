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
  - **Never translate** `roomedit/roogen/roofile.py` (from the Meridian 59 source) or Shards' `roo.ts` / `bgf.ts` (ports of `bspload.c` and `dibutil.c`). They're the original authors' GPL code, which our Unreal Engine linking exception can't cover (AGENTS.md, "Hard rules").
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
- **The demo's local gameplay** (`AMRMonster` placeholder hits, the GAS skeleton) becomes offline-only. Decided in M4: GAS stays offline-only; online, the vitals, stats and every hit are the server's, and nothing mirrors them into GAS.
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

### M2a, see and select (2026-10-08)
**What landed:**
- **Every object the server sends has an actor** (`AMRNetObject`), not just creatures. Each is shown one of three ways:
  - a creature we have a sprite for wears it;
  - in a built zone, an object whose square holds a prop of the world build (the server's lamps, signs, tables, placed as meshes by `build_world.py`) is shown by that prop: a stand-in with no sprite, matched within 0.3 of a square of a `ZoneProp` actor;
  - anything else is drawn from its bitmap.
  - Non-creatures stand still (no movement ticks). Drawing effects: invisible hides, black darkens, translucent dithers (`M_RuntimeRoom` gained `Opacity`).
- **Name plates, target brackets and a crosshair** (`UI/SMRWorldOverlay`), with the original's rules.
- **Targets** (`UMRNetWorldSubsystem`):
  - what the crosshair is on (in reach and in sight) is the aim;
  - T takes it as the target; Tab or `]` / Shift+Tab or `[` cycle the attackable objects in view, left to right; `\` targets yourself;
  - Esc clears the target before opening the menu.
- **Look** (`UI/SMRLookDialog`, the right mouse button):
  - `BP_REQ_LOOK` → `BP_LOOK` (description, inscription) or `UC_LOOK_PLAYER` (description, extra lines, web page);
  - the picture is the object's own bitmap, or a player's face from the creator's portrait;
  - one's own description can be edited and saved (`BP_CHANGE_DESCRIPTION`);
  - a pile under the crosshair is listed first to pick from.
- **Minimap:** dots by the server's minimap flags; a runtime room draws its one-sided walls.
- **`BP_PLAYER_OVERLAY`** is read into the world model, for M2b.

**Split from the plan:** equipment on players (armour, weapons, hats) and the first-person weapon became **M2b**. They need the sprite pipeline to convert the original's equipment bitmaps (only 65 player and monster bitmaps are converted), and their look is the maintainer's call from sheets.

**Verification (local Shards stack):**
- `Meridian.Net.World` also round-trips `BP_LOOK`, `UC_LOOK_PLAYER` and `BP_PLAYER_OVERLAY`; all 20 automation tests pass.
- `run_net_test.ps1` reports **DONE 19/19** (20/20 with `-Create`). The new Look step:
  - walks to the Inn of Raza;
  - checks that every object has an actor (the Inn: 1 with our sprite, 7 by props, 5 from bitmaps);
  - looks at Marcus ("A veteran of the Orc Wars...");
  - looks at ourselves (editable), saves a new description and reads it back.
- `run_move_test.ps1` reports 8/8; UI shots `build/ui/shots/m2/` are unchanged offline.
- Pictures (`-Render`), in `Saved/Screenshots/MRNet/`:
  - `names.png`: Marcus's name and target brackets, the NPC dot on the minimap;
  - `look_object.png`: his Look dialog with his sprite;
  - `look_self.png`: one's own, editable, with the face portrait;
  - `runtime_room_1.png`: a target in a runtime room, its walls on the minimap.

### M2b, equipment on players (2026-10-08)
**What landed:**
- **Every bitmap an item puts on a player is converted:** 165 of them, listed from Kod by `tools/kod_extract/extract.py` into `data/sprites/equipment.json`. They are torsos (shirts, robes, leather, scale, chain, plate, nerudite), arms (shirts, robes, gauntlets), legs (pants, robes, skirts), weapons, shields, bows, helmets, hats and masks (one per gender), and the first-person pictures.
- **Players wear what the server sends** (`MRNetLook`, docs/research/blakserv-protocol.md "Equipment on players"):
  - the torso is the object's icon, the arms and legs its overlays, each in the server's palette translation;
  - the items' overlays are drawn on their hotspots: a weapon bends the right arm, a shield or bow the left, as `SendOverlays` does;
  - a helmet that takes the hair off leaves none.
- **First person online** draws the server's `BP_PLAYER_OVERLAY` slots: the weapon or shield in its screen corner, animated. The local swing shows first, until M4 sends attacks.
- **Render boxes grow only for what is worn.** Each piece stores the box a player needs while wearing it; an unarmed player keeps the original 182 × 232 base pixels. (Taking the union of everything made every player's box 363 × 316.)

**Pipeline choices:**
- Worn pieces finer than the torso (weapons, shields, hats at shrink 12–100) are stored at the torso's density, at most 4×. A 400-pixel hat isn't upscaled 4×. Before this, a few took a 4096² atlas each.
- A torso, arm or leg atlas too big for 4096² with its in-betweens is kept without them (robe and gauntlet arms), so it keeps the ramp atlas the runtime recolours with.
- The red-nose masks (200 pixels at shrink 1, mostly empty) would make a player's box 1000 pixels wide; they're left out of the bounds and clipped.
- **Cost:** 220 atlases. At the 4× upscale with in-betweens that was about 1.5 GB cooked (1,061 M texels), most of it the armour's in-betweens. The maintainer then chose the original pixels without in-betweens, worn pieces at their own pixels and 16-bit ramp atlases (ADR 0008, "Back to the original pixels"): about 636 MB.

**Not yet:**
- Items' own palette translations (a red-tinted shield) aren't applied to worn pieces stored below their own pixels: they have no ramp atlas.
- Other players' attack animations come with M4.
- The overlays on first-person pictures (the fist's glow) aren't drawn.

**Verification (local Shards stack):**
- `Meridian.Sprites.Equipment` (new): a plate-armoured player with a sword, shield and helmet maps from the server's overlays. The items start after the hair, or after the nose when a helmet took the hair. Weapon, shield and helmet are placed from all eight sides, and a long sword grows the box. All 21 automation tests pass.
- `run_net_test.ps1` reports **DONE 20/20** (21/21 with `-Create`). The new check: our torso, arms and legs come from the server.
- Offline, `-MRSpriteEquip=bte,swordov@22:4,metlshld@32:2,helm@13,nohair` renders plate, sword, shield and helmet lit from every side (`build/sprites/tour/m2b_plate/`, `build/sprites/tour/m2b_plate_sheet.png`).
- Review sheets: `build/sprites/equipment/outfits.png` (every torso with a weapon, shield or helmet; original against upscaled) and `items.png` (every weapon, shield, bow and helmet under nearest, `scale4x` and `gtav_dither`).
- `run_move_test.ps1` is 8/8, but the 3.9 m ledge jump failed 2 of 6 runs today. It sits near the original's ~4.1 m limit and was flaky before; no movement code changed.

### M3, inventory and items (2026-10-08)
**What landed:**
- **The protocol** (docs/research/blakserv-protocol.md, "Items"): the inventory, the use list, use and unuse, get, drop, containers, apply, activate and inventory moves, with number items sent as a tagged id and an amount. The world model keeps the inventory, what is in use and the last container's contents; the inventory is asked for on entering and after a server save.
- **The inventory screen online** (`UMRNetInventory`, ADR 0009 "Inventory online"):
  - items in use show on their equipment slot (by their class's use type, matched from the icon in `data/items.json`), the wielded weapon in the right hand, which is its own slot online;
  - the hotbar is a layout on this client, the rest of the bag is in the server's order;
  - the click rules become requests; icons we haven't built come from the server's bitmaps at runtime.
- **In the world:** G picks up what the crosshair is on (a list for a pile), F opens a container or works a lever, and the Look dialog offers Get, Inside and Use. A container's contents are a list; picking one takes it.

**Not yet:** putting things into a container and applying an item to a target have no UI (the requests exist; apply comes with spell targets in M5); giving is an offer (M6); the hotbar layout isn't saved between sessions; the inventory's weight and bulk come from our item data, not the server's.

**Verification (local Shards stack):**
- `Meridian.Net.World` also reads an inventory with a number item, and a use list; all 21 automation tests pass.
- `run_net_test.ps1` reports **DONE 28/28**. The new Items step: the inventory arrives (a mace, 980 shillings) and the inventory screen shows it; the mace is wielded (BP_USE; on the right-hand slot) and put away; it is dropped (it lies in the room) and picked up again; 10 shillings are dropped and picked up (the amount kept through `BP_CHANGE`).
- `-Render` writes `inventory.png`: the mace in the right hand, on the avatar and in first person (the server's window overlay), the shillings with an icon from their bitmap.
- UI shots offline (`build/ui/shots/m3/`) are unchanged.

### M4, combat, death and effects (2026-10-08)
**What landed:**
- **Attacks** (docs/research/blakserv-protocol.md, "Combat"):
  - Online, left mouse sends `BP_REQ_ATTACK`, at most every 250 ms. It takes the target if it is in view (else "You can't see your selected target."), else what the crosshair is on if it can be attacked, else the nearest attackable thing within 5 squares.
  - Our position goes up first, so the server checks range from where we stand. The server decides the rest: range, line of sight, its one attack a second, the hit.
  - The placeholder hit and local monsters stay offline only.
- **The swing is the server's:**
  - In first person, `BP_PLAYER_OVERLAY`'s one-off animation. The local swing no longer plays online.
  - In third person, the one-off animation a `BP_CHANGE` carries plays the matching action on the sprite body (`UMRNetWorldSubsystem::PlayServerAction`): a player's weapon, fist or bow swing, or an arm's cast, point or wave; a monster's attack. This covers us, other players and monsters.
- **Damage numbers** (ours) come from the hit messages: what we deal rises over what we hit, what we take over us (ADR 0009).
- **Screen effects** (`BP_EFFECT`, `FMRNetEffects`, counted down as the original's `AnimateEffects`):
  - the HUD draws pain, whiteout, colour flashes, the override and blindness;
  - the camera shakes, sways (waver) and blurs (depth of field);
  - paralysis stops walking;
  - the room's rain, snow and sand replace the zone's storm roll (`UMREnvironmentSubsystem::SetServerWeather`).
  - Invert is approximated: UE grades colour in linear light before the tonemapper, so the colour is mirrored about mid grey (0.36 − colour), since 1 − colour comes out nearly white.
  - `MREffect <n> [ms] [xlat]` plays one, as if the server had sent it.
- **Projectiles** (`AMRNetProjectile`): `BP_SHOOT` and `BP_RADIUS_SHOOT` fly the server's bitmap from source to target at its speed, with its light. They haven't been seen yet against a real caster (spells come in M5).
- **Death is the server's room change** to the Underworld, built at runtime like any room we haven't made.
- **Fixed on the way:**
  - a creature's capsule stopped a floor trace, so a corpse made where its monster still stood lay on the monster's head (`TraceFloor` now ignores pawns);
  - the name and damage number of a creature drawn with our sprite went at a player's height whatever its size (now the look's own height).

**Decided:** GAS stays offline-only (see Consequences). ADR 0001's "a hit needs physical contact" is superseded online: the swing is presentation.

**Not yet:**
- other players' and monsters' swings are only checked on our own character;
- projectiles haven't been seen from a real spell (M5);
- fireworks aren't drawn;
- invert is an approximation;
- bow attacks and their ammunition are untested.

**Verification (local Shards stack):**
- `Meridian.Net.Combat` (new) covers:
  - the hit messages: monster and player, dealt and taken, found in the rsb by their text;
  - `BP_EFFECT`: durations, limits, the blur adding up, a flash's translation, paralysis, the weather;
  - `BP_SHOOT` and `BP_RADIUS_SHOOT`.
  - All 22 automation tests pass.
- `run_net_test.ps1` reports **DONE 32/32** (33/33 with `-Create`; three runs in a row). The new Combat step:
  - in the Outskirts of Raza, it wields the mace and fights a bunny or a baby spider until it dies;
  - the server answers ("Your mace crushes the baby spider for 8 damage."), swings `povmace` in first person, and our sprite plays `weapon_attack` from the server's `BP_CHANGE`;
  - a damage number rises over the creature.
- `-Create -Death` (35/35): a fresh character dies bare-handed to the Forest of Farol's spiders (in 30–60 s). The server takes it to the Underworld, which is built at runtime (zone 100001), and its archway leads back to the Inn of Raza.
- `-Render` pictures:
  - `combat.png`: the spider, its damage number and ours under the crosshair, the mace;
  - `combat_corpse.png`: the corpse on the ground;
  - `effect_pain/flash/whiteout/invert/blur.png`.
- `run_move_test.ps1` 8/8. The UI shots (`build/ui/shots/m4/`) are unchanged from M3.

### M5, spells, skills, enchantments and stats (2026-10-08)
**What landed** (docs/research/blakserv-protocol.md, "Spells, skills and enchantments"):
- **The server's spell and skill lists** (`BP_SPELLS`, `BP_SKILLS` and their ADD / REMOVE) feed the Spells and Skills pages. They are matched to our data by name; the percentages come from stat groups 3 and 4 as before.
- **Casting:**
  - The spell bar's numpad keys cast online (`BP_REQ_CAST`).
  - A spell with no target goes at once. Otherwise it goes at the target if it is in view, else at what the crosshair is on, else the next choice picks it: the attack click, `\` for yourself, or a click on an item in the dialog. Esc stops.
  - "Cast appraise on what?" shows under the crosshair meanwhile.
  - The cast animation is the server's (M4). The mock cast stays offline.
- **Using an item on something:** U picks the selected hotbar item (or the one under the mouse in the dialog), then the same choice (`BP_REQ_APPLY`). The original always waits for that click.
- **Enchantments** (`BP_ADD_ENCHANTMENT`, `_REMOVE`; `SMREnchantments`): icons for the player's at the top left and the room's under the minimap, named on hover. The room's are dropped on each new room.
- **Rest:** R rests or stands (`UC_REST`, `UC_STAND`). While resting the pawn doesn't walk, attack, go or cast, as the original.
- **The Quests page** lists the server's quest group (5): headings and quests. A click looks at a quest.
- **Retraining** (`BP_STAT_CHANGE`, `SMRStatChange`): an elder's offer opens a dialog that moves points between the six stats (1–50, the same total), and Change sends `BP_CHANGED_STATS`.

**Not yet:**
- retraining doesn't lower school levels (the original's module trades them for points), and hasn't been tried against an elder (Jasper, Marion and Ko'catan are far from the demo);
- the spell bar layout isn't saved between sessions;
- a player enchantment hasn't been seen yet in the test (the utility spells put none on the player);
- the chat commands ("rest", "cast") come with M8.

**Verification (local Shards stack):**
- `Meridian.Net.Spells` (new): `BP_SPELLS` with targets and schools, a room enchantment, `BP_STAT_CHANGE`. All 23 automation tests pass.
- `run_net_test.ps1` reports **DONE 38/38** (39/39 with `-Create`, 41/41 with `-Create -Death`). The new Spells step, in the Inn of Raza:
  - the server's 9 spells (10 with relay for a fresh mage) are all on the Spells page;
  - the room's "Safe Room" enchantment came;
  - the Quests page lists "Passive Quests: Find Priestess Xiana, Find Raza's Elder; No Active Quests; No Completed Quests";
  - appraise is cast on the mace, picked by the target choice ("You have improved in the art of appraise.");
  - meditate (no target) is cast;
  - resting stops walking and casting until we stand.
- `-Render` adds `quests.png` and `choose_target.png`; the UI shots add `stat_change` (`build/ui/shots/m5/`). The other UI shots are unchanged.
- `run_move_test.ps1` was 8/8 in one of four runs and 7/8 in the others, a different ledge check each time. Those checks read the height a fixed 1.6 s after the run starts, so a slow frame catches the character mid-fall. Nothing in M5 touches offline movement.

## Alternatives considered

- **Bake every room before allowing travel:** no runtime code, but hours of GPU texture work and hundreds of levels to import before anyone can leave Raza. A room changed on the server would also break until rebuilt.
- **Runtime rooms only, no baked tier:** less pipeline, but every room outside the authored ones keeps the original's flat look forever.
- **Port the Shards or roofile readers:** fastest, but both are the Meridian 59 authors' GPL code (roofile directly, Shards' readers as ports of the client), which this repo's Unreal Engine exception can't cover.
- **Ship the original `.roo` and `.bgf` files in the package instead of downloading them:** works offline, but can drift from the server's build. The asset cache downloads only what is missing or changed, so it can still start from bundled files later.
