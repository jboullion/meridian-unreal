# ADR 0013: Ambient life from Meridian Shards

- **Status:** accepted, 2026-10-14, and built the same day (see "As built").
- **Builds on:** ADR 0009 (the Shards layout), ADR 0010 (playing on Shards' server), ADR 0012 (parity). Meridian Shards' own ADR 0005 (friends and the relay) and ADR 0007 (the arena).

## Context

Meridian Shards, our browser client, fills its world with life the server never sees: townsfolk walking door to door, inn patrons, castle courts and shop customers; critters underfoot; butterflies, fish, birds and bats; and the Arena of Kraanan's fights, which one sim on its relay runs for everyone. We had fireflies, falling leaves, dust motes, pollen and lamp moths (ADR 0005), and empty streets.

Shards' ambient code is ours (GPLv2, AGENTS.md: "Meridian Shards is ours"), and none of it ports the original's (it isn't `movement.ts`), so it is translated to C++ here. Nothing of it reaches the server; offline play gets it too.

## Decisions (the user, 2026-10-14)

1. **Build now:** the crowds, the critters, and butterflies, fish, birds and bats. **Later:** the crowd and arena recordings (Shards' CC0 `amb_*.ogg`, which our server already serves) and pets.
2. **The arena: watch Shards' relay**, so we see the same fights its browser players do. With no relay, the floor is empty (its foyer keeps its crowd).
3. **Online and offline:** online in every room Shards gives life to, offline in our built zones (all of them since the all-rooms build).

## How it works

| Part | Translated from Shards | Ours |
|---|---|---|
| Which rooms get a crowd, how busy by the hour and weather, what people say, their names | `ambient/ambientRooms.ts`, `chatter.ts`, `names.ts` | `data/ambient/ambient.json`, read from Shards' sources by `tools/world/ambient_data.py`; `Ambient/MRAmbientData` |
| Looks: outfits by role, livery and army, torches after dark | `ambient/townsfolk.ts` | `MRAmbient::*Look` builds the server's overlay list (`FMRNetObject`); `MRNetLook::AppearanceFromObject` makes the sprite appearance |
| Nav grids | `navGrid.ts` (the format, areas, A*, the pulled path) | `Ambient/MRNavGrid`: reads Shards' `.nav` (format 2). A straight line is clear when every cell on it is open and linked to the last (Shards tests the room's walls; we have the grid's links instead, so we need no `.roo` at runtime) |
| The crowd | `ambient/ambientLife.ts` | `Ambient/MRAmbientLife`: the same rules, in ROO units |
| Critters | `ambient/critters.ts` | `Ambient/MRCritters`: floors, headroom, walls and water from traces of our collision and the zone's depth areas (Shards reads the room's sectors) |
| Butterflies, fish, birds, bats | `render/nature.ts`, `flyers.ts` | `Ambient/AMRNatureActor`; the shapes drawn into `T_NatureAtlas` by `tools/textures/make_nature_atlas.py`; `M_Nature` |
| The arena | `arena/arenaView.ts`, `arenaWire.ts`, `social/socialClient.ts` | `Ambient/MRArenaPlayback`, `Net/MRSocialClient` |

- **Data:** `data/ambient/nav/*.nav` and `data/ambient/exits.json` are Shards' asset build's (`npm run assets`), copied for the rooms we built; online a room built at runtime (another version of ours) uses the server's (`nav/<room>.nav`, `roomexits.json` through the asset cache). Rerun `ambient_data.py` after Shards changes its tables or rebuilds its assets.
- **Drawing:** a person, critter or arena figure is an `AMRAmbientFigure`: a plain actor (not a character) with a sprite body or a bitmap, moved along its path kinematically, its feet on the floor (a trace), the sprite body walking by the velocity it's given. No collision; clicks, the crosshair and attacks go to real objects first.
- **Who:** `UMRAmbientSubsystem` (a world subsystem) follows the player's room (online the server's, offline the zone the player is in), loads its crowd, critters, nature and arena, and feeds them the player's position and facing, the game hour, the weather, fine armour and the objects in the way (online the server's `MOVEON_NO` objects, offline the NPCs).
- **Reactions:** our wave (online `UA_WAVE`, offline the wave emote) gets one waved back; a spell (each projectile's ends) scatters those near; they step aside, look twice at chain, scale, plate or nerudite, and greet who comes close. Speech goes over their heads (`SMRWorldOverlay`), never in the chat.
- **Examine:** the Look key with nothing real there opens the Look dialog for someone of the crowd or the arena: a made-up name, role, what they're doing, where from and to, mood; an arena fighter's archetype, favourites, season record and championships.
- **Critters:** by the room's terrain (`zones.json` "terrain"), half Shards' habitats' counts, appearing 4-12 squares away out of view. An attack with nothing real to hit (online the server's attack found nothing) squishes the one under the cursor, else the nearest ahead within 1.6 squares: its death sound quietly, a corpse that fades. Our monster sprites draw rats, centipedes, baby spiders and bunnies; ants, scorpions and grey bunnies are their own bitmaps online, and the nearest sprite we have offline (a baby spider, a centipede, a bunny).
- **Nature:** butterflies over green country (FOREST, FIELDS, HILLS, JUNGLE) by day, not in rain; fish leaping where there are about six squares of open water: a floor with a water texture (`zone_layout.json` "water_areas" from `roo2gltf`, by Shards' rule for its `watertextures.json`: a texture under a depth more often than not; Raza's pond is water painted on a dry floor). Shards takes every sector with a depth for water, and so did we at first: fish leapt from the wheat fields and the beach at Barloque's main gate (i3), its wet sand. A room built at runtime knows only its depth areas, still taken for water; flocks crossing 9-15 squares up by day under open sky; bats in caves, dungeons, crypts and prisons. Our fireflies and leaves stay ADR 0005's.
- **The arena:** `FMRSocialClient` connects to the server entry's `social` (`data/net/servers.json`), signs the relay's challenge with an ECDSA P-256 key made by the engine's OpenSSL (one per character and server, `Saved/MRNet/social/`), and names the game server as Shards' clients do (`social_server`: their game socket URL, the browser's page on `localhost:5173` locally) so both watch the same arena. `FMRArenaPlayback` plays the relay's ticks 220 ms behind, glides the figures, draws the walls' elements and spells in flight from their bitmaps, and plays the game's sounds for swings, hits, cries, spells, frying and coughs.

## Settings

Options > Game: **Ambient life** Off / Few / Normal / Many (`mr.Ambient.Level`, Shards' scale 0, 0.5, 1, 2), **Critters**, **Butterflies and fish**, **Birds and bats** (`mr.Ambient.Critters`, `.Nature`, `.Flyers`), saved in `GameUserSettings.ini`. Tests: `-MRAmbientSeed=N` (the same crowd every run), `mr.Ambient.Showcase 1` (critters in view, fish leaping often in view; 2 holds each fish at the top of its leap), `mr.Ambient.Debug 1` (counts in the log).

## Consequences

- Up to about 20 more sprite bodies in a busy room. The hitch test (uncooked, 1080p, the maintainer's RTX 3070; `Saved/MRHitch/amb_on.csv` against `amb_off.csv`, the same build with `mr.Ambient.Level 0`): p50 8.2 -> 8.7 ms, p95 11.6 -> 13.6 ms, p99 15.6 -> 19.0 ms, frames over 33 ms 28 -> 35. Worth trimming later (the figures' floor traces, sprite bodies far off at a lower density).
- Not yet: the crowd and arena recordings, pets, the web and the blinding ring on arena fighters (no sprites of ours for them), the arena's spectators' cheer held as one dance frame (ours dance), sharing a crowd's seed with friends (Shards' ADR 0005 6).

## As built (2026-10-14)

- Source: `Ambient/` (`MRAmbientData`, `MRNavGrid`, `MRAmbientLife`, `MRCritters`, `MRNatureActor`, `MRArenaPlayback`, `MRAmbientFigure`, `MRAmbientSubsystem`), `Net/MRSocialClient`; hooks in `UMRNetSubsystem::DoAction` (waves), `UMRNetWorldSubsystem::OnProjectile` (spells), `AMRCharacter::OnAttack` (squishing), `AMRPlayerController::LookAtAim` (Examine), `SMRWorldOverlay` (speech), `UMRUISubsystem::ShowAmbientPerson`.
- Tools and data: `tools/world/ambient_data.py`, `tools/textures/make_nature_atlas.py`, `data/ambient/`, `M_Nature` (`environment_materials.py build_nature_material`, with the runtime materials: `build_world.ps1 -Script build_runtime_materials.py`).
- Tests: `Meridian.Ambient` (nav grid, looks, chatter, crowds in the Inn and Raza, habitats, the relay's signature); the online test's "people only we see" step; `run_net_test.ps1 -Arena` (`-Admin`, then the arena: `DONE 11/11`, `arena.png`).
- Sheets: `build/lookdev/amb_a` (the Inn's patrons, a walker by the Inn), `amb_b` (critters in the Outskirts, `mr.Ambient.Showcase`), `amb_c`/`amb_d` (butterflies), `Saved/Screenshots/MRNet/arena.png` (the relay's fight, speech over heads).
