# ADR 0005: Time of day, weather and atmosphere (moods, interiors, fire, weather)

- Status: Proposed (open decisions settled by the user on 2026-10-05; see "Decisions taken")
- Date: 2026-10-05
- Related: [ADR 0001](0001-engine-and-architecture.md) (text-first data, one server), [ADR 0003](0003-environment-art-pipeline.md) (environment art; its pass 4 "moods" is superseded here)

## Context

Raza has four hand-tuned lighting moods: afternoon, morning, dusk and night (`data/environment/moods.json`, ADR 0003 "Moods and lit windows"). But:
- **One mood is baked into the level.** Only one mood at a time is baked into `L_World` by an editor script (`tools/ue/zone_mood.py`), so nothing changes while you play.
- **Interiors share the town's light.** Interiors (the Inn, shops, Hall, Mausoleum) are streamed into the same `L_World`, under the town's sun, fog and exposure.
- **Torches are still the original art.** They're flat animated textures, rebuilt as thin solids. (Since 2026-10-10 they're 3D props with the original's flame: "3D wall torches and the signs' glow".)
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
*As built (phase 3): sprites of the original frames instead of Niagara, `UMRFireSubsystem` instead of a light component, and the flame points in `props.json` "fires" instead of `facades.json`; see "Phase 3 built".*

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

## Phase 5 built (2026-10-05): atmosphere
- **The pattern:** no Niagara. Like the rain, everything is quads moved on the GPU by their material (world position offset), driven by `MPC_Environment` values the director sets.
  - The director (`UMREnvironmentSubsystem::AtmosphereFor`, pure and tested by `Meridian.Environment.Atmosphere`) computes them from `moods.json` `"atmosphere"`, the hour, the season, the storm and the zone profile.
  - `MPC_Environment` gains `Night`, `Spring`, `Autumn`, `Winter`, `Smoke`, `Motes`, `Pollen`, `Fireflies` and `Leaves`.
  - It logs `MREnvironment: atmosphere night .., smoke .., ...` whenever they change.
- **Chimney smoke:**
  - `tools/environment/chimneys.py` finds every chimney the blockout draws with the chimney texture (`props.json` `"smoke"`): 5 in Raza.
  - `build_world.py` stands `SM_Puffs` (48 quads, `build_prop_kit.py`) with `M_Smoke` on each top.
  - Each puff rises 6.5 m over 14 s, slowing, growing from 0.7 to 4.2 m, bent downwind by `Wind`. It's soft value-noise lumps, lit translucent (it takes the sun's, the sky's and the moon's light), depth-faded where it meets the chimney.
  - How thick: a base of 0.45, more on cold mornings (4–11), at night and in winter, less in a storm (`"smoke"`). Wisps on a summer afternoon, full plumes on a winter morning.
- **Ambient particles:** `M_Ambient` on a third copy of `SM_Precip`'s quads in `AMRPrecipitationActor`. A quarter of the quads each, every kind wrapping in its own box around the camera.
  - Kinds by zone profile (`moods.json` `"zones"` `ambient`):
    - **dust motes** in the buildings (and fewer in the Mausoleum): tiny lit specks, catching the lamps and torches;
    - **pollen** by day outdoors, carried on the wind;
    - **fireflies** at dusk and night over open ground below eye level (the shelter map), blinking green;
    - **falling leaves** in the forest (the Outskirts, half as many at Farol West), tumbling.
  - Each by season (`"atmosphere"` `"ambient"`: no fireflies or pollen in winter, leaves mostly in fall), time of day, and storm (a storm takes the pollen and fireflies, and brings more leaves).
  - `mr.Env.Ambient 0` hides them (a player setting).
- **Lamp moths:** `SM_Puffs` with `M_Moth` at every lamp (`props.json` class `"moths"`). A few moths on erratic orbits around the lantern, wings flapping, while the lamps are lit at night (`LampsOn x Night`), not in winter.
  - **Lit glass** was already built in phase 1 (`lamp_glass` follows `LampsOn`).
- **Seasons:**
  - Foliage textures (`materials.json` `"seasons"` `"foliage"`: Raza's tree walls) get `M_Placeholder*Seasonal` masters; `M_Ground` and `M_Grass` always get the tint.
  - `SEASON_HLSL` changes only plant colours (greens and yellow-greens; stone, wood, earth, roofs and the wheat fields keep theirs):
    - autumn: trees gold, orange and rust in patches, grass a muted gold-olive;
    - winter: dormant straw on both, and the grass tufts die back to stubble (`WinterDropCm`);
    - spring: a fresher green;
    - summer: the original colours.
  - The painted trees can't go bare: winter makes them brown and dull.
- **Look-dev:**
  - `run_lookdev.ps1` now pins the season to summer by default, the original colours, so captures compare; `-Season 2` / `3` for fall and winter, `-1` follows the clock.
  - New cameras `smoke_tavern` and `moths_lamp`.
  - Sheets: `build/lookdev/p5c_seasons.png` (summer, fall, winter), `p5d_atmo.png` (smoke, moths, fireflies at 17, 7 in winter, 23).
- **Cost** (RTX 3070, `-Profile`, 23:00): ambient particles on vs off within noise (±0.7 ms either way over five cameras).
- **Tried and changed:**
  - **Winter snow lying all season** (`seasons.winter_snow_cover` 0.12): an even dusting read as pale sand over the whole town (`p5b_winter_17`). Off (0); the original has none.
  - **Autumn grass in the leaves' orange** looked burnt; grass has its own muted palette.
  - **Fireflies at first size** (2.5 cm, 5% of the quads over a 32 m box) didn't show at all. A few 1–2 px dots that the temporal upscaler dissolves. Now 6 cm with a halo, in a 16 m box, blinking more often.
  - **Smoke rising from 5 m above the chimneys, moths circling above the lanterns:** `ObjectPositionWS` is the centre of the mesh's bounds, which moves with the actor's scale. The puff materials use `ActorPositionWS`.
  - **Thin vertical lines across the view** (pollen, motes; rain had the same latent bug): wrapping each corner's own position split quads lying on the box's wrap into slivers as tall as the box. Every camera-box material now places a quad from its seed, the same on all four corners.
- **Open:**
  - Drips in caves: the crypt has no ceiling-and-floor map to drip between.
  - Moths smear into short dashes in stills (motion blur of a fast, flapping quad); fine in motion.
  - Birds, crickets and crackling fires wait for the audio ADR. The original client has `birdchirping.ogg`, `fireplac.ogg`, `Drips.ogg` and `AMBCave.ogg`, though Raza's rooms play only music.
  - Seasons switch at the season's boundary; there's no blend between them.

### After the first playtest (2026-10-06)
The user played the build and reported four things, with screenshots in `ReferenceImages/issues/`.
- **Lights too bright at night**, above all the braziers at the crypt door.
  - Braziers and lamps are 25% dimmer (`props.json`: Brazier `candela_scale` 0.75, Lamp `candela` 9), and so is the lamp glass's glow (`lamp_glass` emissive).
- **Exposure fading.**
  - The light changed as the camera adapted walking in and out of buildings, and went dark walking up to a wall (histogram auto exposure over EV −3..3 by day, −9..−4 at night).
  - Outdoors now has **fixed exposure**, as the interiors already had (min = max). It was bracketed against what auto exposure had settled on in wide views (`mood_zz_ev_*`, `build/lookdev/ev_*.png`).
  - EVs: afternoon −0.7, morning −1.2, dusk −2.5, night −5.7. Each is about 0.2 EV brighter than the match, to soften close shade. The cycle blends them by hour.
  - The trade: shade seen up close stays as dark as it is in the wide view, rather than the camera brightening it (`fix_17`). At night the moon's shadows are close to black.
  - Lifting shadows with local exposure (shadow contrast 0.5 and 0.4, `shade_*.png`) barely changed anything, so it was dropped. More sky fill at night would lift those shadows if wanted.
- **Light leaking under the trees** outside the gate.
  - It's the painted tree walls' lowest band: saturated red undergrowth and blue sky gaps between the trunks, which in the lit scene read as light glowing under the trees.
  - Foliage materials now shade that band toward the ground and take its red and blue out (`UNDERGROWTH_HLSL`; `Undergrowth`, `UndergrowthStart` 0.55, `UndergrowthShade` 0.4). The canopy above is unchanged (`leak2.png`).
- **Walls washed out by light at grazing angles.**
  - The masters used the engine's Specular 0.5, which reflects enough sky and lamp light at a grazing angle to wash the colours out.
  - The placeholder and zone-art masters now take a `Specular` parameter at 0.2 (matte), rising back to 0.5 when wet. Panes and stained glass keep 0.5 (`materials.json` variants). Before and after: `fix_matte.png`.

- **The second round** (the same day):
  - **Shops going "very dark" up close at night:**
    - It wasn't exposure. Captures with local exposure on and off are the same (`local_exp.png`), and no camera overrides the post process.
    - Those facades are simply unlit: no lamp reaches them and the moon is behind them. Up close, a near-black wall fills the screen.
    - Night's sky-light fill is doubled (SkyLight intensity 0.7 → 1.4, skylight leaking 0.12 → 0.25), chosen from 1.5×, 2× and 3× (`night_fill.png`). Shade is readable, and night still reads as night.
  - **Window glow and town lights 25% dimmer again:**
    - Window glow: dusk 0.12 → 0.09, night 0.35 → 0.26.
    - Lamps: 6.75 cd.
    - Braziers: `candela_scale` 0.56.
    - Lamp glass: emissive [2.8, 1.8, 0.74].
  - **Half the fireflies and moths:** `FireflyShare` 0.04, moths `Share` 0.125.
  - Sheet: `fix3_night.png`.
- **The third round: the real cause of walls going black up close.**
  - The user still saw it after the fill: from afar the shops' facade looked fine, closer a dark band ran down its middle, and close up it went black.
  - It reproduces in look-dev with no character at all (`shops_wall_17m/7m/2m`, `wall_23.png`), so it's a screen-space effect.
  - **Tests** (`wall_ab.png`, `wall_ef.png`):
    - With Lumen's screen traces off, the whole facade goes black at every distance; screen-space and distance-field AO make no difference.
    - Its only sky light came from screen traces catching the sky on screen. Up close the sky leaves the screen, and so does the light.
    - Lumen's world-space traces gave the facade nothing, even with only the global distance field.
  - **Cause:** the zone walls are single-sided planes. Their one-sided distance fields put the wall's own surface "inside", so every world-space ray from it starts occluded.
  - **Fix:**
    - `build_world.py` `two_sided_distance_field()` sets "Generate distance field as if two-sided" on every zone mesh, once (the first run re-imported all 37, about 3.5 min).
    - The facade is now evenly lit at every distance, even with screen traces off (`wall2.png`).
    - Shaded walls by day get their sky light too: the dark close shade noted above under fixed exposure is gone (`day_df.png`).
    - Night's fill comes back down to sky light 1.0, leaking 0.18 (`night_fill2.png`); doubling it had only been compensating.
  - **Also:** exposure adaptation speed is 100 in every mood. A fixed exposure still eases from the old value to the new one, which was the fade when stepping outside (interior EV −1.3, night −5.7). It's instant now.
  - **Tooling:** `run_lookdev.ps1 -WithPawn` puts the player's character at each camera and views through its own camera; the look-dev otherwise hides it.

- **The fourth round: the lighting lag at every door.**
  - `run_lookdev.ps1 -Burst "0.05,0.15,..."` saves frames at set times after a cut, at game speed. It showed every zone change lit by the *previous* place's sky for about 2 s, then snapping over: the inn was grey-blue (daylight) before going warm, and the town was dim before brightening (`burst_cmp.png`).
  - **Not the cause:** the director applies the new mood on the very frame of the change (logged), exposure is instant, and neither volumetric-fog reprojection nor faster Lumen updates changed the lag.
  - **Cause:** the sky light's real-time capture is time-sliced over several frames by default. After the sun turns off (going in) or on (going out), the old sky kept lighting the new place until the capture caught up.
  - **Fix:** on a zone change the director turns the time-slicing off for 2 s (`r.SkyLight.RealTimeReflectionCapture.TimeSlice 0`), then restores it, and cuts the camera's history. Day and night, both directions are right from the first frame (`burst_fixed.png`). No loading delay is needed.

- **The starting inn's windows didn't glow at night.**
  - Their window mask was right, but a painted window glows as its glass colour × lamplight, and the inn's glass is painted a dark teal. It came out about 3× dimmer than a typical pane, and muddy brown under warm light.
  - **Fix:** `make_placeholders.py` now measures every window's painted glass and writes `glow_gain` into `placeholders.json`. The gain is 1 up to 4×, bringing dark glass to the typical pane's brightness and never dimming a bright one. The materials multiply their glow by it (`GlowGain`). The barns' darker windows get 1.2–1.45×.
  - **The inn:** even at equal measured brightness its teal glass looked dull, so `facades.json` can override the gain per texture. The inn uses `"glow_gain": 7`, chosen from 3.14, 5, 7 and 9 (`inn_gain.png`).
  - Then all window glow halved at the user's request (too bright): `WindowGlow` dusk 0.09 → 0.045, night 0.26 → 0.13.
  - The tavern's and the temple's windows still disappeared next to the lamps: `glow_gain` 3 on `grd09593`, `grd09599`, `grd09600` (bracketed 2, 3, 4: `win_gain.png`).
- **One window glow for every building: tried and reverted.** Glowing every window in one shared colour (patterned by its glass) evened the town out but lost what the user loved: the temple's stained glass and the shops' self-coloured panes (`oneglow.png`).
  - Back to each window glowing in its own glass colour (`glow_tint` 1, the default), times `WindowGlowColor` (MPC vector, [1.0, 0.62, 0.3], what the warm lamplight constant was) × `WindowGlow`.
  - Dark glass is still brought up towards the typical pane (`GLOW_TARGET_LUM` 0.04, gain 1–4), and pale glass is never dimmed.
  - `facades.json` `glow_gain` is now a multiplier on that automatic gain. The Inn has 2.2 (= its old 7) and the tavern and temple 2, 2 and 2.48 (= their old 2, 2, 3), so the look is as before (`ownglow.png`).
  - `glow_tint` below 1 remains available per texture for a shared colour.
- **Torches lit twice.** The original lights each wall torch with a room light (`DynamicLight`, strength 30) about half a metre below the flame, and we had added the torch's own light on top. `build_world.py` `merge_torch_lights()` drops a room light within 1 m (in plan) of a flame and puts its strength and colour on the flame's light: 27 merged across the interiors.
- **All added lights dimmed (later the same day).** The user's rule: the environment light carries the scene, and the flames only add subtle warmth and character.
  - `props.json` `"light_scale"` is one factor on the intensity of every light the world build adds: props, wall torches and the original's room lights. Their reach is unchanged.
  - We compared 1.0, 0.5 and 0.3 at 01:00 (`build/lookdev/compare_lights10_h1_lights05_h1_lights03_h1.png`) and chose **0.3**.
  - Walls within a metre of a fire stay bright at any setting; that's the inverse-square falloff, not the scale.
- **Moths read as sticks.** They were masked quads, which motion blur smeared into streaks. `M_Moth` is now translucent (no velocity, no smear), lit by the lamp with an emissive of 1.5, 3 cm, and at half the speed (orbit, bob and jitter halved; wing beat 38 → 24).

### Hazy interiors (2026-10-05)
- **Goal:** the dusty air of the buildings and the Mausoleum shows around their lights.
- **How:** it's the volumetric fog that was already on, given density. The interior moods set it to 0.3, with full extinction, and a near-black warm fog colour so nothing glows without a light. The sky light is mostly taken out of the fog (`volumetric_scattering_intensity` 0.15). The crypt keeps its cold mood at the same density.
  - The torches, candles, lamps and braziers then hang in a warm glow; the painted windows (emissive) light nothing.
- **Bracketed** (`mood_test.ps1`, `mood_zz_haze_*`):
  - Densities 0.05, 0.2 and 0.6 with the outdoor fog colour and full sky light filled the rooms with a blue-grey veil.
  - With the dark warm fog colour and the sky light out of it, 0.15 and 0.3 gave clean glows; 0.3 chosen.
- **Restoring outdoors:** the outdoor base mood now names the sky light's `volumetric_scattering_intensity` (1.0). The director only sets the fields a mood names, so it restores the value on the way out.
- **Sheets:** `build/lookdev/haze2.png` (bracket), `haze3.png` (crypt), `haze_final.png` (by day and by night in the cycle).

## Online weather (2026-10-08, M4 of ADR 0012)
On a Meridian server the room's weather is the server's (`BP_EFFECT`: rain, snow, sand, sent on every room change). `UMREnvironmentSubsystem::SetServerWeather` replaces the zone's storm roll with it:
- weather already falling when a room is entered shows at once;
- weather that starts or stops while we're there builds up or clears with the usual curves.

Fireworks aren't drawn yet.

## The server's light (2026-10-08, M7 of ADR 0012)
Rooms built at runtime from the server's files (ADR 0012) are drawn with the original's light model, not the moods:
- **`M_RuntimeRoom` uses the original client's palette level** (`draw3d.c GetLightPaletteIndex`):
  - each sector's light;
  - the room's ambient light (`MPC_Environment.RoomAmbient`, from `BP_PLAYER` and `BP_LIGHT_AMBIENT`);
  - the player's own light, falling off with distance (`PlayerLight`, `BP_LIGHT_PLAYER`).
- **Our sky and sun stay** over runtime rooms. The server's sky bitmap (`BP_BACKGROUND`), its sun and moon overlays (`BP_*_BG_OVERLAY`) and the sun's angle (`BP_LIGHT_SHADING`) are kept but not drawn.
- **Authored zones keep their moods**: the server's light values don't change them.
- **Flicker** (`BP_SECTOR_LIGHT`) isn't drawn, as in the original's Direct3D client.

## Sky: the original skyboxes (2026-10-10)
- **Why:** under a cloudy sky the volumetric clouds cost 25–55 ms a frame on an RTX 3070. Capped, they still cost 11–19 ms. See `docs/performance.md`.
- **Decision (user):** draw the original client's skyboxes instead. No "fancy clouds" are needed.
- **The skyboxes:** the Direct3D client drew one of five skyboxes, `resource/sky{a,b,c,d}.bsf` and `redsky.bsf` (six 512 px PNG faces each).
  - `room.kod` `RecalcBackgroundSkyGraphic` picks the box by day phase: dawn `skyc`, day `skya`, dusk `skyb`, night `skyd`. Storms keep the same box; `redsky` is for Chaos nights.
  - `tools/textures/make_skyboxes.py` packs each into a 3×2 atlas (`T_Skybox_<name>`).
  - `M_NightSky` (the dome, `SKYBOX_HLSL`) finds the face for a view direction. It blends dawn, day, dusk and night by `SkyDawn` / `SkyDay` / `SkyDusk` / `SkyNight`, then adds the sun or moon disc on top.
- **Data and director:**
  - `moods.json` `"sky"` has `"skybox": true`, `"skybox_keys"` (hours, blended between keys) and `"volumetric_clouds": false`.
  - Each mood's `"SkyDome"` sets `SkyboxBrightness` for its fixed exposure: afternoon 0.6, morning 0.4, dusk 0.17, night 0.03. The storms multiply it (rain ×0.5).
  - The director (`ApplySkybox`) sets these on the dome's material instance.
- **Results:**
  - The hitch test's p95 went from 29–38 ms to 17.4 ms, with slow frames 122–460 → 33; in rain 31.6 → 17.6 ms, 146 → 29.
  - Sheets: `build/lookdev/compare_clouds_before_sky_day2.png` (13:00), `build/lookdev/sky_times.png` (7, 13, 19 and 23 h).
- **The sky light** captures the dome, so the skybox's blue (violet at dusk) tints the ambient. The user likes it for now. Revisit later (`docs/performance.md` "Follow-ups").
- **Interiors have no bounce lighting** (user, 2026-10-10). `raza_interior_day` sets `dynamic_global_illumination_method` `None` and the night and crypt moods inherit it; `raza_afternoon` sets `Lumen` for outdoors. Lumen had filled the rooms with the skybox's blue. Without it they are the original's warm, dark rooms, lit by their own lights (`build/lookdev/interior_lighting.png`, `docs/performance.md`).
- **Turning the clouds back on:** `"volumetric_clouds": true` restores them, with the caps in `Config/DefaultScalability.ini` and the thinner, shorter layer in `raza_afternoon`'s `"Clouds"`.

## 3D wall torches and the signs' glow (2026-10-10)
- **Wall torches are 3D props now, the flame still the original's flipbook,** as the braziers are (the maintainer's choice).
  - The mesh `SM_AI_WallTorch` comes from the side-view texture (`grd08886`) with its flame erased, through the props pipeline (ADR 0007 "Wall torch").
  - `props.json` fires `"torch"` `"mesh"`: once the kit mesh exists, `make_placeholders.py` blanks both torch textures (`grd08886`, `grd08887`), so the blockout's crossed planes draw nothing. The blockout itself is untouched.
  - `fires.py` also reports the wall point behind each flame (`"wall"`, the texture's edge in its `"out"` direction). `build_world.py` `zone_wall_fires` stands the mesh there: its bracket end (the mesh's -X) on the wall, turned to `"out"`. Its origin goes at the texture's bottom row (`"mesh_bottom_px"` 152; the aigen mesh keeps the rows under the torch as lift).
  - The flame is set on the mesh's head, read from the GLB (the top 4 cm of vertices), sunk `"flame_sink_m"` 0.03 into it, as the painted flame overlaps the head.
  - **Side view:** the old flame stood 12 cm further from the wall than the stick (`fires.py` `OUT_M`, there so the sprite wouldn't cut the wall). Now the flame stands on the head from every side. Cameras `torch_side_inn`, `torch_side_crypt`.
  - All 28 torches in Raza's interiors (the vault's 3 included). Their lights are unchanged (the room light under each still moves onto its flame).
- **Signs glow like the original's.** `sign.kod` sends a white light at 255 flagged `LIGHT_FLAG_HIGHLIGHT`. The original client draws it at a tenth of a normal light's size, with no falloff on the floor (`d3dlighting.c`): a lit disk about 3.9 m across at the sign's foot.
  - `props.json` `Sign` `"light"` `"highlight": true`: `build_world.py` sizes the disk from the original's formula and draws it as a spot light 2.2 m above the foot, pointing down, its cone just covering the disk.
  - First tried as a point light 0.5 m up: it burned a hot spot into the post. Then as the spot: the sign's top caught it, and it showed as a beam in the interiors' haze. So the light is on lighting channel 1, which only the zone's floors and walls take (`"geometry"`, `"render"` and `"art"` parts), and it doesn't scatter in fog.
  - Bright enough to read in the dark interiors (candela scale 3); outdoors by day it hardly shows, as in the original.
- **Sheets:** `build/lookdev/compare_torch_before_13_torch_v4_13.png` (midday) and `compare_torch_before_23_torch_v4_23.png` (night); the sign's three tries in `compare_torch_before_13_torch_v2_13_torch_v3_13.png`.

## Phase 4 built (2026-10-05): weather
- **The original's rules** (`MRWeather`, unit-tested in `Meridian.Environment.Weather`):
  - Every game day each of the 15 weather zones rolls a storm at 15% (kod `RecalcWeatherConditions`, `piStormChance`).
  - The roll is a hash of the game day, the zone and a seed (`mr.Weather.Seed`, default 59). So a day's weather is reproducible, and a server started mid-storm knows when the run of stormy days began.
  - The room's weather mask picks what a storm brings by season (kod `StartStorm`: sand, else snow, else rain). Raza and its forest are zone 13 with `DEFAULT_NS`: rain in spring, summer and fall, snow in winter. Farol West is Marion (4). The Mausoleum has no weather.
  - Every Raza room, interiors included, shares the town's weather, as in the original.
- **Server (`AMRGameState`):**
  - It replicates one entry per weather zone: storm or clear, and since when (Unix seconds). It re-rolls when the game day changes.
  - Overrides: `-MRWeather=storm|clear` holds full strength from the start (look-dev). `mr.Weather storm|clear` builds up from now.
- **Client (`UMREnvironmentSubsystem`):**
  - The zone profile names its `weather_zone` and `weather_mask`, falling back to `default`.
  - A storm builds over 90 s and clears over 2 minutes; the director re-applies every second while it's changing.
  - The zone's state blends toward an overlay mood (`moods.json` `"weather"` → `"storm"`): `storm_rain`, `storm_snow` and `storm_interior`. Overlays multiply or add to the hour's values, so one overlay works at noon and at midnight:
    - the sun is dimmed and its disc hidden (the moon too), the sky light dimmed, the fog thickened and lowered;
    - the clouds' material is pushed to more coverage and density (the engine cloud's `Cloud_GlobalCoverage` and `Cloud_GlobalDensity`, through a dynamic instance; their original values are restored when the storm clears);
    - colours wash out and exposure drops (−0.6 EV in rain);
    - inside, window daylight falls to 45%.
- **Ground (`_weather_surface` in every surface master; `MPC_Environment.Wetness` and `SnowCover`, outdoors only):**
  - **Wet:** base colour ×0.6; roughness to 0.12 on floors and 0.45 on walls. Wetness builds over 3 minutes and dries over 10.
  - **Puddles** on `M_Ground`: flat ground where a 7 m noise is high, growing with the wetness; dark, still and mirror-glossy.
  - **Snow** lies where the surface faces up. The texture's own relief breaks it up: the flat tops and joints take snow while the sloped stone edges show through; a full cover buries all but the outlines. It builds over 5 minutes and melts over 15. Grass tips whiten.
  - **Wind** (`MPC Wind`) goes from 1 to 2.5 and scales the grass sway.
- **Rain and snow without Niagara (`AMRPrecipitationActor` + `M_Precip`):**
  - Niagara emitters can't be authored from our scripts, so the falling rain and snow are GPU-only. `SM_Precip` holds 10,000 one-centimetre quads (`build_prop_kit.py`), and `M_Precip`'s world position offset turns each into a streak or flake.
  - The streaks fall and drift with the wind inside a 32 × 32 × 16 m box that wraps around the camera in world space. The actor snaps to a grid of whole boxes, so nothing slides with the camera and the mesh never leaves the view.
  - Streaks face the camera around their fall direction. A share of the quads shows by storm strength (`MPC Precip`).
  - The material is lit translucent (the translucency volume), so streaks pick up lamps at night.
- **Shelter maps** (`tools/environment/shelter.py`):
  - For each zone, the highest surface over every 25 cm of ground, baked from the blockout plus the rebuilt art (roof overhangs included), into an 8-bit map (10 cm steps).
  - `M_Precip` hides a streak below it, so nothing falls under roofs, arcades or eaves, or through floors.
  - Each zone gets `MI_Precip_<rid>`; the Outskirts share Raza's map.
- **Lightning:** in rainstorms, a stroke every 8–30 s: a flash, a flicker, a second flash, a fade.
  - Outdoors it's an unshadowed directional light, `Lightning` in `L_World`, in the original's bluish lightning colour.
  - Inside, the windows flash.
  - `mr.Weather.Lightning 0` turns it off. Look-dev stills run with `-MRNoLightning` unless `-Lightning` is passed.
- **Player setting:** `mr.Weather.Particles 0` hides the falling rain and snow (as the original's weather option did). Wet and snowy ground stay.
- **Look-dev:**
  - `run_lookdev.ps1 -Weather storm|clear|roll -Season <0..3>`; captures are clear by default, so labels still compare.
  - Sheets: `compare_wx_clear_17_wx_rain_17_wx_snow2_17.png`, `compare_wx_clear_23_wx_rain2_23.png`, `compare_wx_int_clear_12_wx_int_rain_12.png`.
  - Cost: no measurable difference (150-frame GPU averages −0.7 to +0.4 ms between storm and clear on three town views; `wx_perf_clear`, `wx_perf_storm`).
- **Follow-ups built (2026-10-05, the same day):**
  - **Splashes:** `M_Splash` on a second copy of `SM_Precip`'s quads. Each quad is a splash for 0.45 s at a random spot within 13 m of the camera, on the shelter map's top surface (ground, roofs, merlon tops, the pond), then jumps to a new spot. Rain only; `mr.Weather.Splashes 0` turns them off.
  - **Ripples:** rings of drops (`RIPPLE_HLSL`: random rings in 45 cm cells, two layers) on the pond (`M_Water`) and in `M_Ground`'s puddles while it rains. The shader is skipped when nothing falls.
  - **Ice:** the original turns water to ice when it snows. Each water mesh gets a copy 1.5 cm above it with `M_Ice`, which freezes in patches that spread with `SnowCover` (macro noise against the cover) and carries snow drifts.
  - **Distant bolts:** each stroke shows one of four generated bolts (`T_Bolts`, `make_placeholders.py bolts()`) 2.2 km out in a random direction, about 900 m tall. It's additive and unfogged, held for the stroke's envelope. The flash outdoors is now 1.5 lux: 6 lit the night like day.
  - **Sandstorms** (desert masks; testable anywhere with `-MRWeatherKind=sand` / `mr.Weather.Kind sand`): low, tan streaks blown sideways within 6 m of the ground, and `storm_sand` (ochre haze, the sun hidden).
  - **Sounds:** the original client's `rain.ogg`, `Rs_wind.ogg` and `thunderclap.ogg` (Meridian-104 resources). Since [ADR 0006](0006-audio.md) phase 1 they're imported with the other originals and played by `UMRAudioSubsystem`; the director still decides when.
    - Rain loops with the storm and wind with the wind; both are muffled by a low-pass filter inside. Thunder follows each stroke 0.4–3.5 s later, at random volume and pitch.
    - The original played rain only where the room's weather mask had the sound bit, and Raza's `DEFAULT_NS` has none, so its rain was silent. `weather.sounds.follow_mask` restores that rule; it's off.
  - **Inside:** window daylight drops to 35% in a storm. Rain and wind are heard muffled and the windows flash.
- **Look-dev additions:**
  - `run_lookdev.ps1 -WeatherKind rain|snow|sand`, `-LightningHold` (one stroke held on screen) and `-Exec "<console commands>"`.
  - Sheets: `compare_wx_clear_17_wx5_rain_17_wx5_snow_17_wx5_sand_17.png`, `compare_wx_clear_23_wx_rain2_23_wx5_bolt_23.png`.
- **Found on the way:**
  - In a look-dev still, a rain streak frozen within a metre or two of the camera smeared a grey veil across the view: a ribbon over the Hall in one run, a blurred wall in another. Streaks now hide within 1.5 m and fade in by 4.5 m.
  - Python has no temporal-dither expression (`DitherTemporalAA` is a material function), hence the ice's patches.
- **Open:**
  - The sounds weren't heard here: look-dev runs with `-nosound`. They imported; listen in the editor.
  - Snow on the pond's ice is drift-shaped, not piled.
  - Indoors the storm is still mostly sound.

## Phase 3 built (2026-10-05): fire
- **What the original has in Raza:**
  - Wall torches are wall textures with the flame painted on: `Torch Attaches to wall` (`grd08886`, side view with the bracket) crossed with `Torch Cross` (`grd08887`, front view), 3 frames each. Raza's interiors have 28: the Inn 4, the Smith 2, the Apothecary 1, the Elder's hut 1, the Mausoleum 2, the Bar 3, the Hall of Heroes (308) 12, the Vault 3. The original's wall torches give no light of their own: their sectors are lit.
  - `Brazier` (kod `flikerer/brazier.kod`): 7 frames, frame 0 unlit, intensity 40, `LIGHT_FIRE`, 100–120 ms a frame. There are two in town, two in the Mausoleum, one in the bank.
  - `Candle`: a candlestick with a small flame, intensity 5. There is one, in the Bar.
  - The chandelier is unlit (`chandelr.bgf` draws white candles with no flames) and isn't fire.
  - Flicker: the D3D client flickers flickering objects' own brightness (`OF_FLICKERING`, `animate.c`). Sector flicker (`SF_FLICKER`, ±40 of 255 every 100 ms) runs only in the software renderer.
- **Flames are sprites of the original frames, not Niagara.**
  - Niagara emitters can't be authored from our scripted pipeline (Python can't build emitter stacks). A camera-facing sprite with a flipbook material is scriptable end to end, and draws the same thing.
  - `props.json` `"fires"` defines three presets (torch, brazier, candle). Each crops the original frames to the flame, keeping only flame-coloured pixels where the flame overlaps a torch head or bowl.
  - `make_placeholders.py` upscales the frames with the base-colour model into `T_Fire_<preset>` (one power-of-two cell per frame, soft alpha).
  - `M_Fire` (unlit, translucent) cycles the cells at the preset's frame rate. Each frame fades into the next, and each fire starts from its own phase (a hash of where it stands). `AMRFireActor` draws it as a `UMaterialBillboardComponent` at the original's size: a torch flame is 0.45 × 0.65 m, a brazier's 0.52 × 0.31 m.
  - Embers and thin smoke are left for later: a hand-made Niagara system can be added to `AMRFireActor` when wanted.
- **Wall torches** (replaced on 2026-10-10 by 3D torches: "3D wall torches and the signs' glow"):
  - The torch textures lose their painted flame: flame-coloured pixels in the preset's `"erase"` rect become transparent, so the bracket and the glowing torch head stay.
  - `tools/environment/fires.py` finds a flame wherever the blockout uses those textures. It extends each face's UV mapping to the flame's texel (`"at"`), merges the faces of one torch (crossed planes, both sides), and sets the flame 12 cm off the wall (`"out"`: the texture direction away from the wall).
  - Each torch gets a light of Kod intensity 20 (`"light"`). The original torches had none of their own; this replaces the brighter sectors around them.
- **Props:**
  - Brazier: `SM_Brazier` plus the brazier flame in its bowl, and Kod intensity 40 (it was 8 cd at 2000 K).
  - Candle: a new `SM_Candle` candlestick (bronze and wax slots) plus the candle flame.
- **Flicker (`UMRFireSubsystem`, client only):**
  - Every fire light flickers by seeded noise: three slow sines and a smoothed 10 Hz random step (the original's 100 ms). The amplitude is ±16% (the original's ±40 of 255), and the flame reddens as it dips. The source wanders 1.5 cm.
  - The noise is unit-tested (`Meridian.Environment.Flicker`): bounded, deterministic, lively, and uncorrelated between seeds.
  - The original's `DynamicLight`s flicker only in its flickering sectors: `roo2gltf` marks objects in `SF_FLICKER` sectors (`"flicker": true` in `zone_layout.json`), and `props.json` `"flicker": "sector"`. That covers the Inn, Smith, Apothecary, Hut, Mausoleum, Bar, Hall of Heroes and Vault lights.
  - Beyond 30 m a light holds still; beyond 60 m it's off. The 4 nearest eligible fires cast shadows, re-chosen 4 times a second (`mr.Fire.*` console variables).
  - There are 58 fire lights in Raza and its interiors.
- **Cost** (`run_lookdev.ps1 -Profile`, new `no_fire` variant with the fire lights off, `fire_perf`): no measurable cost. The 150-frame GPU averages differ by −0.7 to +0.6 ms between fires on and off, within run-to-run noise.
- **Look-dev:**
  - Cameras `fire_crypt_torch`, `fire_crypt_brazier`, `fire_candle_bar` and `fire_inn_torch`, compared against `fire_before_23` (`compare_fire_before_23_fire_v5_23.png`).
  - Outdoors is unchanged (`fire_out_17` vs `out_check_17`).
- **Pitfalls found:**
  - A new component-mask expression has R on by default. An "A only" mask built with just `a=True` read the opacity from the red channel, which drew the flame's whole box in red.
  - `roo2gltf` coloured each blockout material by Python's salted `hash()`, so every run wrote different files and forced a re-import of every zone. It now uses `crc32`.
- **Open:**
  - The town's two braziers stand about 0.5 m inside the Mausoleum's facade (`zone_layout.json` positions), hidden from the street. That was already so before phase 3. They need moving out to the doorway.
  - The Mausoleum is still lit mostly by its ambient floor: it has only 2 wall torches and 2 braziers.
  - Embers and smoke; heat haze is not planned.

## Phase 2 built (2026-10-05): interiors and underground
- **What the baseline showed:**
  - Sun came through every interior. The blockout's floors and ceilings are single-sided, and a downward-facing ceiling doesn't block a sun shining from above.
  - At night the interiors were black: none of the original's interior lights were spawned.
- **Zone profiles** (`moods.json` `"zones"`, one per Raza interior):
  - `cycle` and `kind` (interior / underground).
  - `sun: false`: the sun and moon are switched off while the view is in the zone; their rotation is left alone, so the shadow cache isn't touched.
  - `daylight`: the share of daylight through painted windows, from the original's outside factor / 10.
  - `base_light` and `outside` from `data/zones.json`.
  - `lamps: always` for the Mausoleum.
- **Which profile applies:** the director picks it by the zone the camera is in (`UMRZoneSubsystem::ZoneAtLocation`), falling back to the player's zone. Look-dev cameras in other zones get their own profile.
- **The original's lights:** `props.json` gains `DynamicLight` and `Candle`.
  - The Kod defaults are `kod_intensity` / `kod_color`; the object's own `iIntensity` / `iColor` win.
  - `build_world.py` converts them: intensity × 0.24 cd, reach 4 m + 0.15 m per unit, the 15-bit colour blended 40% towards white (pure Kod fire colour read as yellow).
  - They're unshadowed, like the original's pools of light. The chandelier is decorative in the original and stays unlit.
- **Sector light as the ambient floor:**
  - `roo2gltf` writes each sector's original light level (0–255) as vertex colour (`COLOR_0`): floors and ceilings their sector's, each side of a wall the sector it faces.
  - Every placeholder master adds base colour × that level × `MPC_Environment.SectorAmbient` × `AmbientTint` as emissive. It's 0 outdoors, so it only shows inside.
  - The director scales `SectorAmbient` by the original's room light, `base + outside × (brightness − 50) / 4`, so rooms with windows dim at night: the Inn to about 65%, the shops to about 60%, the Mausoleum constant.
  - This replaced a Lumen skylight-leak ambient, which barely reached into closed rooms.
- **Window daylight from inside:** the window-glow term gained cool daylight × `WindowDaylight` on the same window masks. It's set only while the view is in an interior, so it never shows on the exterior.
- **Moods:**
  - `raza_interior_day` and `raza_interior_night` (cycle `raza_interior`: night until 5:30, day 8–18, night from 20:30).
  - `raza_crypt`: cold, desaturated, high contrast.
  - **Fixed exposure,** chosen by bracketing (`mood_raza_interior_day_ev*`, `mood_raza_crypt_ev*`): EV −1 by day and −1.3 at night in buildings, 0 in the Mausoleum. The original renders at a fixed brightness, so the room light itself dims rather than the camera adapting it away.
- **Look-dev:**
  - Eleven interior cameras (`int_*`), suggested by `tools/lookdev/suggest_cameras.py`.
  - `run_lookdev.ps1 -StartZone` captures zones further away; all of Raza's interiors stream in from Raza.
  - Outdoor captures are unchanged (`out_check_17` vs `cycle_17`).
- **Open:**
  - The Mausoleum is lit mostly by its ambient floor until the torches become fire with light (phase 3).
  - Local Fog Volumes for its low passages.
  - Day and night inside differ subtly; it's tunable (exposure, `SectorAmbient`).

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
| 3. Fire | Flame presets from the original frames (built as sprites), `UMRFireSubsystem` flicker, wall-torch flame points (`props.json` "fires"), brazier and candle props, shadow budget | phase 2 for interiors (outdoor braziers can go first) |
| 4. Weather | Server weather state (zones, rolls, masks, seasons), replication, storm blend, rain plus wetness plus shelter maps, lightning, snow plus snow cover, `-MRWeather` look-dev, player setting (built: see "Phase 4 built") | phase 1 |
| 5. Atmosphere | Chimney smoke, ambient particles, seasonal tint, lamp moths; audio hooks for the audio ADR (built: see "Phase 5 built"; audio waits for its ADR) | phases 1–4 |

## Action items

1. [x] Port `GetDayPhase`, `GetBrightness`, game day, year and season into `UMRGameTimeSubsystem`; unit-test them against the Kod formulas.
2. [x] `moods.json`: `"cycles"` and `"zones"` schemas (documented in `_doc`), and a first `raza_outdoor` cycle from the existing moods.
3. [x] `UMREnvironmentSubsystem`: read `moods.json`, blend keys by hour, apply to the `L_World` actors and the MPC, switch on zone change. `zone_mood.py` stays the headless bake until then.
4. [x] Sun and moon path with stepped updates; star/night-sky layer. [ ] VSM profiling of the step size.
5. [x] Lamps on and off by the hour (lights plus glass emissive).
6. [x] `-StartZone` for look-dev. [x] `cycle_test.ps1` and time-lapse sheets.
7. [x] Interior profiles; import `DynamicLight` and `Candle` (the chandelier has no light in the original); interior window daylight; sector ambient. `SF_FLICKER` moves to phase 3 with the flicker component. [ ] Local Fog Volumes for the Mausoleum.
8. [x] Fire from the original flame frames (sprites, not Niagara: see "Phase 3 built"); `UMRFireSubsystem` flicker and shadow budget; wall-torch flame points (`props.json` "fires" "walls"); brazier and candle props; `SF_FLICKER` lights. [ ] Embers and thin smoke. [ ] The town's two braziers stand inside the Mausoleum facade.
9. [x] Server weather (zones, rolls, masks, seasons), replication in `AMRGameState`, overrides and `-MRWeather`.
10. [x] Rain (GPU streaks, not Niagara; shelter maps; wetness and puddles), lightning, snow (plus snow cover in the masters), player setting (`mr.Weather.Particles`). [x] Splashes, ripples, ice on the pond, distant bolts, sandstorm visuals, the original's storm sounds.
11. [x] Chimney smoke, ambient particles (motes, pollen, fireflies, leaves), seasonal tint, lamp moths. [ ] Cave drips. [x] Volumetric light in dusty interiors ("Hazy interiors"). [ ] Birds, crickets and fire sounds (audio ADR).
