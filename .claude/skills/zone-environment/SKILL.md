---
name: zone-environment
description: The current recipe for Unreal Meridian environment art - turning a zone's roo2gltf blockout into the finished look (upscaled original textures, selective normals, rebuilt roofs/parapets/cut-outs, grime, moods, lit windows, look-dev). Use when building or dressing a zone or level (Raza, its interiors, the Outskirts, the Crypt, new maps), or when touching data/environment/*.json, make_placeholders.py, build_zone_art.py, environment_materials.py, zone_mood.py or the look-dev captures.
---

# Zone environment art

How to take a zone from its generated blockout to the Raza look. This file is the **current recipe**.
The history, experiments and reasons live in [ADR 0003](../../../docs/adr/0003-environment-art-pipeline.md)
(the decision log): read the linked section when you need the *why*, not to find out what to do.

More detail, loaded when needed:
- [reference/data-files.md](reference/data-files.md): every `data/environment/*.json` key, units, coordinates
- [reference/tools.md](reference/tools.md): every tool's command, flags, outputs, caches
- [reference/pitfalls.md](reference/pitfalls.md): known traps (symptom → cause → fix); check it before debugging

## Current defaults (what holds up everywhere)

The user wants a look that "works everywhere all the time" rather than one that fights artefacts.
Every default below won an in-engine comparison; don't change one without a new look-dev test.

| Area | Default | Why (ADR 0003) |
|---|---|---|
| Layout and collision | The roo2gltf blockout is the source of truth. Never hand-edit generated output; art is layered on top, and the full blockout stays as hidden collision. | Constraints 2–3; "Hidden collision" |
| Base colour | The originals' own pixels, smoothly filtered (`materials.json` `"base_colour": "original"`, since 2026-10-10), as the original D3D client and Meridian Shards draw them; rule-based relief. `"upscaled"` brings back the 4x AI upscale (`4xTextures_GTAV_rgt-s_dither`). Fire flipbooks are always upscaled | "Original textures, filtered": nearly the same at normal distances, a quarter of the memory, seconds to build |
| Normals | On plain surfaces only (ground, plain and perimeter walls). Flat on anything with painted windows, doors, signs, pictures, clocks (`relief.flat_openings` + `relief.flat` regex) | "Textures-only test", "Selective rebuild and relief" |
| Roof and path normals | Rule-based maps instead of Marigold's (`relief.rules_for`): roofs `luma` at strength 2.2; path, stone-path and rocky-ground floors `stones` at `ground` `normal_strength` 1.0. With the original base colours every texture's relief is rule-based (Marigold only runs with `"upscaled"`) | Marigold gives roofs and ground nearly flat normals; "Ground normals" |
| Displacement | Off (`relief.displacement: false`) | Inferred depth made wobbly glass and melted stone |
| Rebuilt geometry | `"rebuild": ["plain_facades", "roofs", "parapets", "cutouts"]`: proud bands, piers and plinths on walls *without* painted openings, roof slabs with eaves and verges, solid merlons, solid fences/gates/signs. Walls with painted windows or doors stay original (no facade relief, no cut openings) | "Selective rebuild", "Plain facades back" |
| Grime | Geometry-derived strips at wall feet and under eaves (mesh decals) | Never reads the textures, so it can't misplace anything |
| Lighting | A day/night cycle: moods in `moods.json` are keys on the game clock, blended in game by `UMREnvironmentSubsystem`; one directional light is the sun by day and the moon by night; lamps off 11–17; stars at night; painted windows glow at night (window masks × `MPC_Environment.WindowGlow`). **Fixed exposure** everywhere (`auto_exposure_min_brightness` = `max`, per key mood): never auto exposure, which faded the light at walls and doors | "Moods and lit windows"; ADR 0005 "After the first playtest" |
| Surfaces | Matte: the masters' `Specular` 0.2 (0.5 wet, and on glass); the engine's 0.5 washed walls out at grazing angles. Foliage shades its painted undergrowth (`Undergrowth`) | ADR 0005 "After the first playtest" |
| Fire | The original flame frames as flipbook sprites (`props.json` `"fires"` → `T_Fire_<preset>` → `M_Fire` on `AMRFireActor`); wall torches found from their textures (`"walls"`), braziers and candles as props; every fire light flickers (`UMRFireSubsystem`), as do the original's lights in flickering sectors; 4 nearest cast shadows | ADR 0005 "Phase 3 built" |
| Weather | The original's storm rolls per weather zone (`AMRGameState`), rain or snow by season and the zone's mask (`moods.json` `"zones"` `weather_zone` / `weather_mask`); storm overlay moods; wet ground with puddles or snow cover in every master; GPU rain and snow (`AMRPrecipitationActor`) kept out from under roofs by baked shelter maps; lightning in rainstorms | ADR 0005 "Phase 4 built" |
| Atmosphere | Chimney smoke plumes found from the chimney texture (`props.json` `"smoke"`), moths at the lamps (`"moths"`), ambient particles around the camera by zone profile (`moods.json` `"zones"` `ambient`: motes inside, pollen and fireflies outdoors, leaves in the forest), the season's tint on foliage and grass (`materials.json` `"seasons"`), all driven through `MPC_Environment` by `moods.json` `"atmosphere"` | ADR 0005 "Phase 5 built" |
| Time-driven textures | Animated textures that show game time (the clock) become a frame atlas indexed by `MPC_Environment.GameHour` from `UMRGameTimeSubsystem` | "The Raza clock tells the time" |

**The guiding rule:** prefer depth and richness that come from the geometry or from lighting
(roof slabs, cut-out solids, grime, moods, glow) over depth inferred from the painted textures.
Inferred relief is fine on plain masonry, ground and roofs. It breaks on painted detail.

## Coordinates in one line

- Blockout and `zone_<rid>.json` `region_m`: glTF metres, x east, y up, z south, relative to the zone.
- UE and `lookdev_cameras.json`: world cm, X east, Y south, Z up. UE = `world_origin_cm` + (100x, 100z, 100y).
- Zones sit on a 2 km grid (`data/zone_layout.json` `world_origin_cm`: 301 is at X 200000). Raza (300) and the Outskirts (330) share geometry. A zone keeps its slot when zones are added (`roo2gltf.py` reads the old layout); new zones take free slots, a map bigger than a cell reserving the cells it covers, so cameras in world cm stay valid.
- `facades.json`: pixels of the original texture (`build/textures/<grd>.png`), x right, y down.

## New zone workflow

Work in this order; each step is cheap to re-run (everything is incremental).

1. **Blockout and textures exist.** The demo zones are `DEMO_RIDS` in `tools/kod_extract/extract.py`.
   If the zone is new there, run `extract.py`, `bgf2png.py --textures-for-zones`, then `roo2gltf.py --rid <rid> --preview`.
   Check `build/zones/<rid>_<Name>.glb` and its `.png` top-down map.
2. **Inventory the zone's textures.** `python tools/environment/zone_textures.py <rid>` lists the blockout's slots with catalog names, area and where they are (wall, floor, ceiling), sorted into wall torch (the fire pipeline's), cut-out solid, foliage cut-out, painted and kept flat, animated and plain surface, and writes `build/environment/zone_<rid>/textures.png`.
   Check the sort on the sheet: painted art filed as a surface needs `relief.flat` (step 4); roofs aren't told apart, so find them by eye.
   Show the sheet to the user.
3. **`facades.json`.** For every texture with painted windows, add `openings` with `"kind": "window"` (doors: `"kind": "door"`).
   With facade relief off, these still drive the night-glow mask (`T_<grd>_E`), the window flattening and `flat_openings`.
   Each window glows at night in its own painted glass colour × `moods.json` Collection `WindowGlowColor` × `WindowGlow`; `make_placeholders.py` brings dark glass up (up to 4×). A texture's `"glow_gain"` in facades.json multiplies that, and `"glow_tint"` below 1 swaps some of the glass colour for the shared one.
   Add sloped roof textures to `roofs`. Check whether cut-outs need a `cutouts.thickness_m` rule or `exclude`.
   A time-driven animated texture gets a `"clock"` block. Run `python tools/environment/facades.py` and check `build/environment/facade_check/openings_sheet.png`.
4. **`materials.json`.**
   - Extend `relief.flat` for painted doors, pictures and signs that `facades.json` doesn't describe. It matches the catalog *name*, so check it against a contact sheet of the matches.
   - Extend `grime.exclude` for surfaces that shouldn't get dirt.
   - Add tiling floors to `ground` (`tile_m` 2.2).
5. **Textures.** `python tools/textures/make_placeholders.py` (incremental; runs the upscaler and the Marigold `ai_maps.py --apply`).
   Look at the new `T_<grd>_D/_N/_E` for the zone's textures before going further.
6. **`zone_<rid>.json`.** Copy `zone_300.json`'s shape:
   - **Always set `"rebuild"` explicitly.** A missing key means *all*, facades included.
   - `buildings`: one entry per building with `region_m` [x0, z0, x1, z1], read off the blockout PNG or the `.blend`.
   - A face belongs to the first entry that takes it, so broad entries go last (the town wall by `materials`, then a `"kind": "cutouts"` catch-all with `"displacement": "none"`).
   - Water: `"kind": "water"`. Grass: `scatter`.
   - Without a `zone_<rid>.json`, a zone gets placeholder materials on the plain blockout and no cut-outs or grime.
7. **Zone art.** `blender -b --factory-startup -P tools/blender/build_zone_art.py -- --rid <rid> --preview --strict`.
   Read every `WARNING` (cut-out outlines that triangulate to the wrong area, openings not built) and `build/environment/zone_<rid>/openings.json`.
   Run `tools/blender/check_overlaps.py` on `zone_<rid>_art.blend` for z-fighting (see pitfalls for which pairs are harmless).
   Then `python tools/environment/shelter.py <rid>`, so rain and snow stay out from under the new roofs.
   Inspect `zone_<rid>_art.blend` if something looks off.
8. **Props, lights and fire.** Map the zone's Kod object classes (`data/zone_layout.json` objects) to kit meshes and lights in `props.json`.
   New meshes go into `tools/blender/build_prop_kit.py`.
   A new fire (a torch texture, a firepit object) is a `"fires"` preset: extract an object's frames with `python tools/bgf2png/bgf2png.py <name>`, set `crop` and `keep` from a contact sheet, and for a wall texture add it under `"walls"` (`at`, `out`, `erase`). Check the flames with `python tools/environment/fires.py <rid>`.
9. **World.** Run `powershell -File tools/ue/build_world.ps1`. It uses the open editor when there is one; stop PIE first.
   C++ changes need a compile and an editor restart.
10. **Mood and cycle.** In game the environment director blends the moods along the zone's cycle by game hour (`moods.json` `"zones"` → `"cycles"`; `default` = `raza_outdoor`). The level's baked mood (`levels`) is only the editor's view.
    New moods `inherit` an existing one and set only what differs, plus `"Collection"` (`WindowGlow`, `Stars`). A new key mood goes into a cycle's `keys` at its hour.
    Outdoor zones in another weather zone than Raza's need `weather_zone` / `weather_mask` in their profile (from the room's kod `viWeatherZone` / `viWeatherMask`); zones without weather get `"weather_zone": 0`.
    Give the profile an `ambient` (`motes` inside, `pollen` / `fireflies` outdoors, `leaves` in forests); without one a zone falls back to the default's outdoor kinds. Chimneys smoke where the blockout uses a `props.json` `"smoke"` texture; check `python tools/environment/chimneys.py <rid>`.
    Interiors and underground zones need a profile in `moods.json` `"zones"`: `cycle` (`raza_interior` / `raza_crypt` or a new one), `kind`, `sun: false`, `daylight` (outside factor / 10), and `base_light` / `outside` copied from `data/zones.json`. Their lights come from the original's `DynamicLight` / `Candle` objects (`props.json`), and their ambient floor from the sector light levels `roo2gltf` writes as vertex colour ([ADR 0005](../../../docs/adr/0005-time-weather-and-atmosphere.md) "Phase 2"). Inside, exposure is fixed per mood: tune it by bracketing (temporary moods with `auto_exposure_min/max_brightness` equal, captured with `mood_test.ps1`).
11. **Look-dev.**
    - Add cameras for the zone to `lookdev_cameras.json`. In game, `MRBookmark <name>` logs the current view as an entry.
    - Capture a baseline label, then compare every change: `run_lookdev.ps1 -Label <new> -Compare <old> [-Only cam1,cam2]`.
    - Use `cycle_test.ps1` for the day/night cycle, `mood_test.ps1` for single moods and `upscaler_test.ps1` for upscalers. Compare only labels captured the same way (see pitfalls).
    - Send the sheets to the user (SendUserFile) with a recommendation.
12. **Record.** Log any new experiment, comparison or decision in ADR 0003 (with the sheet paths).
    If a *default* changed, update this skill's table and the reference files too.

## Interiors and other zones

**Interiors** (Raza's ten, 2026-10-10; ADR 0003 "Raza's interiors"):
- **Zone config.** No `buildings` regions. Use `"rebuild": ["cutouts"]` and a single `"kind": "cutouts"` entry over the whole blockout (solid bars, cages, gates; wall torches stay with the fire pipeline). Copy `zone_301.json`.
- **Grime.** Set `"grime": {"base": "indoor_base", "ceiling": "ceiling"}` (`materials.json` kinds: darker contact dirt at the wall feet and a band under the ceiling). The town's base strips vanish on dark rooms.
- **Not used.** Skirting boards and cornices (`"trims"`, `tools/blender/zone_trims.py`) were tried and dropped by the user: they read as dark lines and cover painted beams. A brighter or deeper day mood was also turned down; interiors keep `raza_interior_day`.
- **Expect little change.** The town's recipe changes interiors little: on Raza's, only the solid cut-outs showed.
- **Painted art.** Tapestries, shelves, cabinets, weapon racks, barrel racks and cloths are in `relief.flat`. Confirm it on the zone's texture sheet.

**Other zones** (the forest zones 330 and 331, new maps). Raise these gaps with the user rather than guessing:
- **Lighting.** A zone without a profile in `moods.json` `"zones"` gets the town's outdoor cycle. Add a profile (see step 10) and cameras (`python tools/lookdev/suggest_cameras.py <rid>`; `--opposite` gives the reverse view).
- **Look-dev.** `run_lookdev.ps1` starts in zone 300; zones one exit away (Raza's interiors, the Outskirts) stream in. For farther zones pass `-StartZone <rid>`.

## The iteration loop

| Change | Run |
|---|---|
| Facade/relief/texture rules | `make_placeholders.py` → `build_world.ps1` → `run_lookdev.ps1 -Label x -Compare y` |
| Zone layout, rebuild, grime, roofs | `build_zone_art.py -- --rid <rid>` → `build_world.ps1` → look-dev |
| Lighting only | Edit `moods.json` (read at runtime: no build; `MREnvReload` in a running game) → `run_lookdev.ps1 -Mood <name>` / `mood_test.ps1` for a mood, `cycle_test.ps1` for the cycle. `build_world.ps1 -Script zone_mood.py` only refreshes the editor's baked view |
| Upscaler comparison | `upscaler_test.ps1 -Models default,<name>` (restores the default afterwards) |
| Materials code (`environment_materials.py`) | `build_world.ps1`; a new helper used by a master must be added to `_master`'s cache key |
| Clock / game hour | `run_lookdev.ps1 -GameHour <h>` (default 17, where the afternoon mood was tuned); `mr.GameHour <h>` in the console |

Typical costs on the RTX 3070: texture no-op 0 s, one building about 22 s in Blender plus 15 s in the open editor, a look-dev run about 45 s, an upscaler model over all 150 textures about 5 min (cached afterwards).

## Working with the user

- They iterate visually and decide from sheets: show before/after images, recommend one option, keep the others reversible behind a data switch.
- They handle git commits; don't commit unless asked (a PR request counts as asking).
- Ask before any download, stating the file, source and size. Models go in `build/texai/models/` (git-ignored).
- Raw extracted art stays in `build/` and never enters git. Never print `.env`. The product is "Unreal Meridian", never with "104".
- They may use all original M59 assets; stay close to the original look (colours, motifs, recognizable facades).
