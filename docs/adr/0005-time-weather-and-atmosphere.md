# ADR 0005: Time of day, weather and atmosphere (moods, interiors, fire, weather)

- Status: Proposed (open decisions settled by the user on 2026-10-05; see "Decisions taken")
- Date: 2026-10-05
- Related: [ADR 0001](0001-engine-and-architecture.md) (text-first data, one server), [ADR 0003](0003-environment-art-pipeline.md) (environment art; its pass 4 "moods" is superseded here)

## Context

Raza has four hand-tuned lighting moods: afternoon, morning, dusk and night (`data/environment/moods.json`, ADR 0003 "Moods and lit windows"). But:
- **One mood is baked into the level.** Only one mood at a time is baked into `L_World` by an editor script (`tools/ue/zone_mood.py`), so nothing changes while you play.
- **Interiors share the town's light.** Interiors (the Inn, shops, Hall, Mausoleum) are streamed into the same `L_World`, under the town's sun, fog and exposure.
- **Torches are still the original art.** They're flat animated textures, rebuilt as thin solids.
- **There's no weather.**

The original game already had most of this. Our server runs its rules (Server 104 Kod), so we know exactly how it behaved:

| Original system | What it does (Server-104 Kod / client) |
|---|---|
| **Game clock** | A game day lasts 2 real hours, so a game hour is 5 real minutes, all derived from UTC (`util/system.kod` `SystemInitGameHour`). Already ported: `UMRGameTimeSubsystem`, which drives the clock face. |
| **Day phases** | Night before 6 and after 20, dawn 6–8, day 9–17, dusk 18–20 (`SysRecalcLightAndDayPhase`). Each phase has its own skybox, with stormy variants for dawn, day and dusk (`SKYBOX_*`). |
| **Brightness** | Peaks around 14:00. It's clamped between 15 and 75, flat at 75 from about 11 to 17, and at 15 through the small hours. Room light = `base light + outside factor × (brightness − 50) / 4`, clamped 0–255 (`room.kod` `GetRoomLight`). |
| **Per zone** | `outside_factor` 0–10 sets how much daylight reaches a room: Raza 10, the Inn 10, shops and the Hall 5, the bank and vaults 3, the Mausoleum 0. `base_light` ranges from 124 (Mausoleum) to 255 (`data/zones.json`). |
| **Per sector** | Every sector of a `.roo` has a light level: the Inn sits at 50–65, the town at 128–160, the crypt at 30–153. `SF_FLICKER` makes a sector's light flicker by up to ±40 every 100 ms (`clientd3d/roomanim.c`). Flickering sectors: the Inn 4, Raza 5, the crypt 10. |
| **Lights** | `DynamicLight` objects (a `Flickerer`) carry an intensity (0–255) and a 15-bit colour. In the Raza interiors they're warm orange at intensity 30–45: 1–12 per interior. `Lamp` posts switch off from 11 to 17 outdoors, and never switch off indoors (`lamp.kod`). Also placed: braziers, a candle, a chandelier. Wall torches are 3-frame animated wall textures (`grd08886`, `grd08887`). |
| **Weather** | 15 weather zones; Raza is zone 13. Every game day (2 real hours) each zone rolls a 15% storm chance (`settings.kod` `piStormChance`, `RecalcWeatherConditions`). A storm becomes rain, snow or sandstorm through the room's weather mask for the current season. Raza's mask `WEATHER_MASK_DEFAULT_NS` gives rain in spring, summer and fall, snow in winter, and no rain sound. A room can switch weather off (`pbWeatherEffects`). |
| **Seasons** | `game year mod 4`. A game year is 240 game days, so each season lasts about 20 real days. |
| **Client effects** | The Server 104 client drew snow and rain particles (`clientd3d/d3dparticle.c`, `weather_snow.png`), and players could turn them off (`INIWeatherFX`). Weather is cosmetic, except for rooms that script around it. A Qormas holiday event can freeze the weather and set off fireworks (`ROOM_FIREWORKS`). |

Constraints carried over from ADRs 0001 and 0003:
- Data stays text in `data/`, and levels are generated.
- One authoritative server; clients render.
- Nothing paid or non-redistributable goes into the repo, so for example the Ultra Dynamic Sky marketplace pack is out.
- An RTX 3070 at 1080p must stay comfortable. Today the frame is 10.8–14.6 ms (ADR 0003 "Profile").
- The user wants a look that "works everywhere all the time", so every effect is judged on look-dev sheets across all cameras and moods.

## Decision

**Faithful systems, modern presentation.** We keep the original's clock, phases, per-zone daylight, lamps, flicker, weather zones, storm chance, weather masks and seasons, so the world behaves as players remember. We render them with today's tools: sky atmosphere, Lumen, volumetric clouds and fog, Niagara, and material parameters.

### 1. Time of day: moods become keyframes on the game clock
- **One clock.** `UMRGameTimeSubsystem` (already built) is the only source of the game hour.
  - The day phase and the original brightness curve are derived from it: `GetDayPhase()` and `GetBrightness()`, ported 1:1 so gameplay and visuals agree.
  - It also gains the game day, year and season. The server's Kod clock and ours both derive from UTC, so they never drift apart.
- **Sun and moon.**
  - The sun's path is driven by the hour: it rises in the east at dawn, peaks at 14:00 where the original brightness peaks, and sets in the west at dusk.
  - At night the same directional light becomes the moon: it follows the moon's path, with moonlight intensity and colour. One light keeps one shadow cache (built this way in phase 1).
  - A star layer gives the night its sky. The reference shots' purple night sky is the colour target, because the stock sky atmosphere goes black at night.
- **Moods become key moods.** A *cycle* in `moods.json` lists `[hour, mood]` keys, for example night at 5, morning at 7, afternoon at 11–17, dusk at 19 and night at 21. The director (§2) blends the two keys around the current hour:
  - numbers and colours interpolate
  - enums snap at the midpoint
  - rotations come from the sun path, not from the moods

  Existing moods stay valid as keys, and their `"Collection"` block (`WindowGlow`) blends the same way. So lit windows follow the hour instead of being set by hand.
- **Lamps follow the original rule.** Outdoor lamp lights and their glass emissive are off from 11 to 17, with a short fade. Indoor lamps never switch off.
- **Sun movement and shadows.** A game day of 2 real hours moves the sun 3° per real minute. A sun that moves every frame invalidates the Virtual Shadow Map cache every frame. The sun and moon therefore step every few seconds, for example 0.25° every 5 s, with exposure smoothing (decided by the user). The step is profiled (`run_lookdev.ps1 -Profile`) before it's tuned.

### 2. One client-side "environment director" for every zone
- **The director.** `UMREnvironmentSubsystem`, a world subsystem, runs on clients, in PIE and in the editor. It never runs on the dedicated server, which doesn't render. Each frame, or at about 10 Hz for the heavier parts, it computes the environment state from three inputs:
  - **the game hour:** the sun path, the cycle keys and lamps
  - **the weather:** §5
  - **the zone the local player is in:** `AMRPlayerState::FOnZoneChanged`, which already exists

  It then applies that state to the one set of lighting actors in `L_World` (sun, sky light, sky atmosphere, height fog, clouds, post process) and to `MPC_Environment`.
- **One semantics, no duplicate applier.** The director reads `moods.json` at runtime, from the repo in development and from `Data/` in a packaged build, as `zones.json` is read today. It implements the same rules `zone_mood.py` does today: properties by actor label, and the `"Collection"` block. Once it works, `zone_mood.py` shrinks to baking the level's default state for headless builds.
- **Zones switch, they don't overlap.** Zones sit 2 km apart and are entered through exits, so a player only ever sees one zone's interior at a time. The director blends to the new zone's profile over about 0.5 s on a zone change. Raza and the Outskirts share geometry, so they share a profile family and blend smoothly as you walk between them.
- **Zone profiles** (new `moods.json` `"zones"`, keyed by rid) name a zone's cycle, its kind, its weather zone and mask, and its overrides.
  - Defaults come from `data/zones.json`: `outside_factor`, `base_light` and terrain flags (`TERRAIN_CAVES` means cave or dungeon).
  - So a new zone works with no entry, and an entry exists only where it needs tuning.

### 3. Interiors, dungeons and caves
Each zone has a kind: **outdoor**, **interior** or **underground** (dungeon or cave). The profile sets the kind, and `outside_factor` and terrain suggest it.

- **The shared outdoor lights are dimmed.** Interiors are closed boxes in the same world, so the sun is already blocked. What leaks in is the global fog, exposure, sky-light leak and grading. In an interior or underground zone the director turns height fog and clouds down, and switches exposure, grading and the Lumen sky-light leak to the zone's own values.
- **Daylight through the windows, in reverse.** An interior's painted windows show daylight from inside: a cool daylight emissive in the same window masks (`T_<grd>_E`), scaled by `outside_factor × brightness`. They're bright by day and dark at night, the reverse of the exterior `WindowGlow`. A small fill light scaled the same way keeps rooms with `outside_factor` 5–10 from looking sealed at noon.
- **Placed lights come from the original data.**
  - `DynamicLight` objects become point lights: the 15-bit colour is decoded, and intensity 0–255 is mapped to candela with one tuned factor.
  - `Candle`, `Chandelier`, `Brazier` and `Lamp` become props with lights (`props.json`, as lamps and braziers already are).
  - Sector light levels set the zone's ambient floor, the lowest light a dark corner gets, so the Inn stays dim and the Hall bright.
  - Sectors with `SF_FLICKER` flicker the lights inside them.
- **Underground zones** (the Mausoleum, later crypts and caves) get cold grading, high contrast, a low fixed exposure range and no sky-light leak. Local Fog Volumes give them mist in low passages. They're lit mainly by fire (§4), which matches ADR 0003's "Crypt and Mausoleum: cold, high contrast, torch-lit".
- **Look-dev for interiors.**
  - `run_lookdev.ps1` gains `-StartZone`. Today it always starts in Raza, and only zones one exit away stream in.
  - Interior cameras are added to `lookdev_cameras.json`.
  - Every interior is judged at noon and midnight, and in a storm.

### 4. Fire as VFX, with flickering light
- **One Niagara fire family** (`NS_Fire`), with presets: wall torch, brazier, candle, chandelier, lamp flame, and later hearths. Each has flames, embers and thin smoke. Heat haze is used only on the large fires, and only close up.
- **The flames are the originals, at first.**
  - The 3 frames of the torch and brazier animations (wall-torch textures and object BGFs) are upscaled with the base-colour model and made into a flipbook. That keeps the fires recognisably Meridian, costs nothing and needs no new art.
  - If they read as low-res in look-dev, a flipbook baked from a Blender fire simulation (free, scripted, made by us, so no licensing issues) replaces them.
  - Volumetric fire (Heterogeneous Volumes) is too expensive for dozens of torches and stays out.
- **Flicker in code, deterministic and cheap.**
  - A small light component (`UMRFlickerLightComponent`) varies intensity, and slightly the colour and source position, with seeded noise.
  - Each light has its own seed, so neighbouring torches don't pulse in sync.
  - The original ±40-of-255 flicker sets the amplitude.
  - Lights beyond a distance stop flickering, and then stop casting light at all.
- **Placement from data, never by hand.**
  - Kod objects place fire through `props.json` classes.
  - Wall torches are found where the blockout uses a torch texture. `facades.json` gains a `"flame"` point in texture pixels, the way the clock's dial is described. The zone-art build then emits the bracket as a solid plus a spawn point for the flame. That point is the same everywhere the texture appears, mirrored walls included.
- **Shadows on a budget.** Fire lights are unshadowed by default. The few nearest the camera, say 4, cast shadows. If MegaLights is production-ready in 5.8, it replaces this hand-made budget, decided after profiling.

### 5. Weather: the server rolls it, clients show it
- **Faithful rules on the server.**
  - Weather zones, the 15% storm roll per zone each game day, weather masks by season, and per-zone opt-out are ported into the server's game state.
  - The weather is rolled from a seeded random generator, so a day's weather is reproducible for testing.
  - GM and console overrides allow test storms and future events such as Qormas snow and fireworks.
- **Replication.** `AMRGameState` replicates one small entry per weather zone: pattern (clear or storm), kind (rain, snow or sand), and the time it started. That's a few bytes every 2 hours, so the network cost is nothing. Clients blend from the start time, so a player who logs in mid-storm sees it at full strength.
- **What a storm looks like.** The director blends all of these over 1–2 real minutes:
  - Volumetric clouds thicken and darken. The sun and sky dim. Fog density rises. Wind (`MPC_Environment.Wind`) drives grass sway and gusts, and later tree cards and banners.
  - **Rain:**
    - Niagara GPU rain falls around the camera, with splashes on the ground and ripples on the pond (`M_Water`).
    - Surfaces get wet: `MPC_Environment.Wetness` darkens base colour, lowers roughness, and pools puddles on up-facing floors with the macro noise.
    - Rain doesn't fall under roofs, overhangs or arcades. A per-zone **shelter map**, the height of the highest surface over each 25 cm of ground, is baked offline from the blockout and zone art. Rain particles test against it. This is cheaper and more deterministic than a runtime depth capture, and interiors need nothing because they're separate zones.
  - **Snow:**
    - Niagara flakes fall, slower and drifting.
    - `MPC_Environment.SnowCover` builds up over minutes on up-facing surfaces: ground, roofs, ledges and merlon tops. A world-normal blend in every master material does this, and the grass shortens and whitens.
    - It melts after the storm.
  - **Lightning** (storms only, more striking at dusk and night):
    - The sky light and clouds flash in brief bursts, with the exposure guarded so it doesn't blow out.
    - Distant Niagara ribbon bolts, with thunder delayed by distance once audio exists.
  - **Mist and fog.** There is no fog weather (decided by the user). Outdoor zones get a **morning mist** in the morning key mood instead: enough for atmosphere, never enough to hide anything. It's a thin ground fog, dense at ground level and falling off fast with height, clear for the first 12 m, scattering forward toward the low sun. It's already tuned in `moods.json` `raza_morning` (look-dev `mood_raza_morning_fogA`, chosen over the fuller haze `fogB`). The cycle blends it out as the morning turns to day.
  - **Sandstorms** exist only in desert masks, outside the demo. The pattern is ported, and the visuals wait until a desert zone needs them.
- **Player choice.** As in the Server 104 client, a setting turns weather particles off. Wetness and snow cover stay, because they're cheap and they're part of how the world looks.

### 6. Other atmosphere
These are ordered by value for effort. All are data-driven, and all are judged on look-dev.

1. **Chimney smoke.** Rooftop chimneys are already identified by texture (the `chimney` relief rule). A thin smoke plume at each chimney top bends with the wind, and is thicker on cold mornings and in winter.
2. **Ambient particles by zone kind and time:**
   - dust motes in interiors and in sun shafts
   - falling leaves at the forest edge
   - fireflies over the grass and the pond at dusk and night
   - pollen by day
   - drips in caves
3. **Volumetric light** through the morning mist and in dusty interiors. Lumen and volumetric fog do this already; the moods just need to give the fog some scattering.
4. **Seasons.** A seasonal tint on foliage and grass: autumn colour in fall, snow-dusted and bare in winter. It's driven by the same season the weather uses.
5. **Life and sound.** Birds by day, crickets at night, crackling fires, rain and wind loops, thunder. These need an audio pass, which is ADR material of its own; the director already exposes the state they need.
6. **Lamp moths and lit glass.** A few moths circle lit lamps at night, and lamp glass glows only while the lamp is on.

### 7. Budget and testing
- **Frame budget** on the RTX 3070 at 1080p, all measured with `run_lookdev.ps1 -Profile`:
  - day/night director: under 0.1 ms
  - fire lights and VFX in a busy view: 1 ms or less
  - a storm with rain, fog and particles: 1.5 ms or less
- **The VSM cost of the moving sun** is measured before the step size is fixed.
- **Look-dev pins everything:**
  - `-MRGameHour=` (exists)
  - new `-MRWeather=<zone>:<pattern>:<kind>` and `-MRSeason=`
  - `-MRStartZone`
- A **time-lapse** capture renders one camera through a full game day as a contact sheet, like the original's `TestGameDay`. `tools/lookdev/mood_test.ps1` becomes `cycle_test.ps1`, which captures fixed hours (0, 6, 9, 14, 19, 22) in each weather.

## Phase 1 built (2026-10-05): day and night outdoors
- **Clock** (`UMRGameTimeSubsystem`):
  - `GetDayPhase`, `GetBrightness` and `GetSeason` are ported from Kod. An automation test, `Meridian.Environment.GameTime`, checks the phase and brightness for each hour against the Kod formulas.
  - **Deviation:** the original counts game days and years on the server. Here they follow from UTC like the hour, so every client agrees without asking (`-MRSeason` / `mr.Season` pin the season).
- **Director** (`UMREnvironmentSubsystem`):
  - It's a client-side world subsystem; it isn't created on a dedicated server. In editor worlds it only runs with `mr.Env.Editor 1`.
  - It reads `moods.json` at runtime: `"cycles"` with hour keys, `"zones"` with a cycle per zone (only `default` for now) and `"sky"` with the sun and moon paths.
  - It blends the keys around the hour and applies them through reflection, with the same property names `zone_mood.py` uses. Lighting actors are found by tag; `build_world.py` now tags them with their label.
  - Every 5 s (`mr.Env.UpdateSeconds`) it re-evaluates, and it applies only what changed. A pinned hour applies once, and the sun moves about 0.2° per step.
  - `-MRMood=<name>` / `mr.Env.Mood` pin one mood with its own sun: `mood_test.ps1` now uses that, with nothing baked. `MREnvReload` re-reads the file.
- **Sun and moon:**
  - One directional light, now movable. Sun: rises at 6, highest at 14 (50°), sets at 22. Moon: 18 to 10, highest at 2 (40°).
  - Both rise in the east-north-east and set in the west-north-west. At 17 the sun is where the afternoon mood was tuned (`Meridian.Environment.SkyPath` tests it), so `run_lookdev.ps1` now defaults to `-GameHour 17`.
  - The moon takes over as soon as the sun is down. **The user asked for a faint ambient fill at night:** the night mood's sky light rose to 0.7 and its skylight leaking to 0.12, so shade stays readable.
- **Cycle `raza_outdoor`:** night until 4:30, the misty morning by 6:30, day (the afternoon mood) from 9:30 to 17, dusk by 19:30, night from 21:30.
- **Lamps:** props marked `night_only` (lamp lights and lamp glass) follow the original rule: off from 11 to 17, with quarter-hour fades. The lights are tagged `NightLamp`; the glass reads `MPC_Environment.LampsOn` through `M_PropSurface` `LampSwitch`.
- **Night sky:**
  - A 100 km sky dome (`NightSky`, `M_NightSky`, an `is_sky` material). It draws the sky atmosphere and the sun or moon disc through itself, then adds `T_Stars`.
  - `T_Stars` is generated by `make_placeholders.py`: an equirectangular map with about 9,000 stars and a faint milky-way band. It turns once per game day.
  - `MPC_Environment.Stars` is 1 at night and 0.2 at dusk.
- **Look-dev:** `cycle_test.ps1` captures fixed hours side by side (`compare_cycle_*.png`).
- **Open:**
  - Stars can show through thin cloud: the dome isn't occluded by every cloud.
  - The night sky is deep blue rather than the reference shots' purple; that's tunable in the night mood.
  - Profiling the 5 s sun step against the shadow cache.

## Decisions taken (user, 2026-10-05)
1. **Sun and moon step every few seconds**, not every frame, to protect the shadow cache (§1).
2. **No fog weather.** Instead there's a reasonable outdoor morning mist that adds atmosphere without obscuring the view (§5). It's tuned and in `raza_morning`.
3. **Fire starts from the original flame frames** (§4). A simulated flipbook stays the upgrade path if close-ups need it.

## Options considered

### Time of day
| Option | For | Against |
|---|---|---|
| **Keyframed key moods blended by hour (chosen)** | Reuses the four moods already tuned. Art-directable and faithful to the original phases. Plain JSON. | A cycle needs several good keys, and blending them needs care: enums, exposure. |
| Purely physical sky (only the sun's angle drives sky atmosphere and exposure) | Little data | No control over grading or the purple night. The look drifts from the original and from what we've tuned. |
| Marketplace sky system (Ultra Dynamic Sky etc.) | Fast, polished | Paid and not redistributable (ADR 0003 constraint 5). It also brings its own data model. |

### Per-zone lighting
| Option | For | Against |
|---|---|---|
| **A client director switching profiles on zone change (chosen)** | One set of lighting actors. Covers fog, sky and exposure, not just post process. Uses the zone event we already have. | Needs code, and the zone event must arrive before the first interior frame. A short fade hides that. |
| A bounded post-process volume per zone | UE blends it by camera position, with no code | It covers only post-process settings: not height fog, the sun or the sky light. |
| A separate persistent level per interior | Isolated lighting | Breaks ADR 0001's single-world streaming and the neighbour preloading. |

### Weather authority
| Option | For | Against |
|---|---|---|
| **The server rolls it and replicates per zone (chosen)** | Faithful to the original, reproducible from a seed. GM events and future gameplay hooks fit. | A little server code |
| Every client derives it from the clock, with no replication | Zero network | No events or overrides, and gameplay can't depend on it |

### Fire
| Option | For | Against |
|---|---|---|
| **Niagara with original-frame flipbooks, plus a code flicker light (chosen)** | Recognisable and cheap. Scales to every torch. | 3 original frames may look coarse up close. The simulated flipbook is the upgrade path. |
| Heterogeneous Volumes fire | Best close up | Too expensive at dozens of instances |
| Keep the animated wall textures | No work | Flat, and what the user wants replaced |

### Rain under roofs
| Option | For | Against |
|---|---|---|
| **A shelter map baked offline from the zone geometry (chosen)** | Deterministic, cheap, regenerated with the zone | One more build output per zone |
| A runtime top-down depth capture | Follows dynamic objects | Costs every frame, with capture artefacts |
| Distance-field collision only | Built in | Particles still spawn under roofs and fall through overhangs |

## Consequences

- **Easier:**
  - one place, `moods.json`, decides how every zone looks at every hour and in every weather
  - new zones get sensible light from their original `zones.json` values with no tuning
  - the clock, lamps and weather match the original server's rules, so long-time players' expectations hold
- **Harder:**
  - Masters gain weather parameters (wetness, snow), so every master changes. They're generated by `environment_materials.py`, so that's one code change, not many assets.
  - The look must now hold across time × weather × zone. Look-dev runs get longer, so the cycle test pins a small set of hours.
  - A moving sun has a shadow-cache cost to manage.
- **Revisit:**
  - MegaLights once its 5.8 status is clear
  - the fire flipbook source after the first close-ups
  - sandstorms when a desert zone needs them
  - the audio ADR

## Phasing

| Phase | Result | Depends on |
|---|---|---|
| 1. Day/night outdoors | Director, sun and moon path, star layer, Raza cycle from the four key moods, lamps by the original rule, window glow by the hour, cycle and time-lapse look-dev | the game clock (built) |
| 2. Interiors and underground | Zone profiles and kinds, `DynamicLight`, candle and chandelier lights, interior daylight windows, sector ambient and flicker, Mausoleum mood, `-StartZone` look-dev | phase 1; interior zone art (ADR 0003) |
| 3. Fire | `NS_Fire` presets from the original frames, `UMRFlickerLightComponent`, torch flame points in `facades.json`, brazier, candle and chandelier props, shadow budget | phase 2 for interiors (outdoor braziers can go first) |
| 4. Weather | Server weather state (zones, rolls, masks, seasons), replication, storm blend, rain plus wetness plus shelter maps, lightning, snow plus snow cover, `-MRWeather` look-dev, player setting | phase 1 |
| 5. Atmosphere | Chimney smoke, ambient particles, seasonal tint, lamp moths; audio hooks for the audio ADR | phases 1–4 |

## Action items

1. [x] Port `GetDayPhase`, `GetBrightness`, game day, year and season into `UMRGameTimeSubsystem`; unit-test them against the Kod formulas.
2. [x] `moods.json`: `"cycles"` and `"zones"` schemas (documented in `_doc`), and a first `raza_outdoor` cycle from the existing moods.
3. [x] `UMREnvironmentSubsystem`: read `moods.json`, blend keys by hour, apply to the `L_World` actors and the MPC, switch on zone change. `zone_mood.py` stays the headless bake until then.
4. [x] Sun and moon path with stepped updates; star/night-sky layer. [ ] VSM profiling of the step size.
5. [x] Lamps on and off by the hour (lights plus glass emissive).
6. [ ] `-StartZone` for look-dev (phase 2). [x] `cycle_test.ps1` and time-lapse sheets.
7. [ ] Interior profiles; import `DynamicLight`, `Candle` and `Chandelier`; interior window daylight; sector ambient and `SF_FLICKER`.
8. [ ] `NS_Fire` from the original flame frames; `UMRFlickerLightComponent`; `"flame"` points in `facades.json`; props.
9. [ ] Server weather (zones, rolls, masks, seasons), replication in `AMRGameState`, overrides and `-MRWeather`.
10. [ ] Rain (Niagara plus shelter maps plus wetness), lightning, snow (plus snow cover in the masters), player setting.
11. [ ] Chimney smoke, ambient particles, seasonal tint.
