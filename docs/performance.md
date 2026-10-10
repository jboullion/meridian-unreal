# Performance

How the game was made to play smoothly (2026-10-10), what was measured and the knobs that are left.
This is a decision record, like the ADRs. The sky's part is also in [ADR 0005](adr/0005-time-weather-and-atmosphere.md) ("Sky: the original skyboxes").

## How to measure

- **Hitch test** (`npm run test:hitch`, `tools/ue/run_hitch_test.ps1`, ~2 min, an offline game window). `-MRHitchTour` plays at normal speed with the game clock running: the look-dev profile's frozen time and still cameras hid every stutter.
  - It records every frame from launch.
  - It waits for the warm-up screen like a player.
  - It teleports through Raza, the Outskirts and three interiors, turning a full circle and walking a little at each stop.
  - It prints p50/p95/p99, frames over 33 and 100 ms per stop, and the warm-up time (`MRHitch: DONE ...`), and writes `game/UnrealMeridian/Saved/MRHitch/<label>.csv`.
  - Options:
    - `-Weather rain`: the storm moods.
    - `-Exec "<cvar> <value>, ..."`: try a console variable.
    - `-Extra -MRHitchCsv`: a CSV profile with per-pass GPU timings. Then `python tools/lookdev/hitch_report.py` lists what grows in the slow frames.
    - `-Extra -MRHitchCmdAt=pond:ProfileGPU`: a GPU profile in the log at one stop.
- **Our own code** shows in the CSV profile as `Exclusive/GameThread/MR*`: `MRSpriteTick`, `MRSpriteDraw`, `MRSpriteTargets`, `MREnvApply`, `MREnvTick`, `MRFireTick`, `MRNetAim`, `MRNetWorldTick`, `MRBgfSpriteTick` and `MRWorldOverlayPaint`.
- **Look-dev profile** (`run_lookdev.ps1 -Profile`): the static GPU cost of a view (ADR 0003 "Raza look-dev").

## What was slow, and what changed

All runs below are on an RTX 3070 at 1920×1080, Epic quality, uncooked `-game` (`npm run play`).

| Run | Ready after | p50 | p95 | Frames over 33 ms |
|---|---:|---:|---:|---:|
| before, first launch after a build | (no warm-up) | 16.1 | 82.8 | 581 |
| before, warm | (no warm-up) | 14.0–14.9 | 20.0–51.4 | 20–276 |
| volumetric clouds capped, clear | 4.1 s | 12.3 | 29.2 | 122 |
| **skybox, clear** (`sky_clear`) | 4.1 s | 12.7 | 17.4 | 33 |
| **skybox, rain** (`sky_rain`) | 4.1 s | 13.1 | 17.6 | 29 |
| (capped clouds, rain, for comparison) | 4.6 s | 12.3 | 31.6 | 146 |

1. **The first minute: things still being made.** The uncooked game builds or fetches from the DDC whatever it lacks while you play:
   - shaders;
   - meshes, distance fields and textures (546 static meshes "waiting on ... being ready");
   - the pipeline states the engine precaches for every component;
   - the sprite atlases, loaded synchronously the first time a look is drawn (`FlushAsyncLoading` stalls).

   On the first launch after a change the GPU ran at 60–75 ms a frame for half a minute.

   **Fix: the warm-up screen.**
   - `UMRWarmup` (`Core/MRWarmup`) holds the game behind `SMRLoadingScreen` ("Compiling shaders (99 left)", a bar) until the shader jobs, asset builds, pipeline precaches, sprite atlases and streaming textures have all been at zero for a moment. It preloads the sprite atlases (~40 MB, kept).
   - Offline, the player can't walk or look meanwhile. Online, it covers the login screen.
   - Settings:
     - `mr.Warmup 0` skips it.
     - `mr.Warmup.MaxSeconds` gives up waiting (default 120 s uncooked, 30 s packaged).
     - `mr.Warmup.TextureSeconds` (8) caps the wait for streaming textures.
   - The test modes skip it; the hitch test waits for it.
2. **The volumetric clouds, up to 55 ms a frame.** The engine's `m_SimpleVolumetricCloud` cost about 1 ms under a clear sky, but 25–55 ms whenever clouds drifted overhead.
   - The breakdown: `CloudView (CS) 350x197` alone took 29 ms.
   - The sky light's real-time capture renders them again: 8–30 ms on the 2 s of whole captures after each zone change.
   - Capping the samples (`Config/DefaultScalability.ini`), a 4 km layer, 20 km rays and early ray stop (moods.json `"Clouds"`) still left 11–19 ms.

   **Fix: the original client's skyboxes** (user's call). They replace the volumetric clouds on the existing sky dome, so the clouds' cost is gone. See [ADR 0005](adr/0005-time-weather-and-atmosphere.md) and `tools/textures/make_skyboxes.py`. `moods.json` `"sky"` `"volumetric_clouds": true` brings the clouds back, with their caps.
3. **Zone changes:** the director captured the sky light whole every frame for 2 s after each zone change. It now does so for 0.25 s (`mr.Env.SkyBurstSeconds`).
4. **Interiors:** the volumetric clouds are hidden where the sun is off (a zone profile's `"clouds"`). This matters only if they are turned back on.
5. **Smaller fixes:**
   - **Environment director:**
     - Light and sky light intensity and temperature go through their setters.
     - Other properties only rebuild a component's render state when they really changed. Before, every apply rebuilt the sun's light proxy, which throws away its shadow cache.
     - Actors are found once, not by scanning every actor on each apply.
   - **Fire lights:** shadow-casting fire lights no longer wander (`mr.Fire.JitterCm`), because a moving shadowed light re-renders its shadow every frame.
   - **Aim:** distant server objects are skipped before the sight trace (`UMRNetWorldSubsystem::IsInSight`).
   - **Sprites:** a sprite whose atlas failed to import no longer redraws its three render targets every frame.
   - **Nanite tessellation:** off (`r.Nanite.Tessellation=0`, still allowed). Nothing displaces since `relief.displacement` went false, it cost ~0.5 ms, and it is linked to the first-frame `PatchSplit` GPU crash.
   - **TSR history:** Epic anti-aliasing keeps TSR's history at 100%, not 200%, which saves video memory (the render-target pool was 1.7 GB, and the GPU's budget is sometimes below what the game uses).

**Measured, and not the problem:** our game-thread code. Sprite composition averages 0.3 ms a frame, and the environment apply, fire flicker and overlay are each under 2.5 ms at worst. The planned reworks of sprite render targets, server-object actors, room-entry spawning and HUD brush caching were not done, because the numbers didn't call for them.

## Lighting and grass options (2026-10-10)

These were compared with `run_lookdev.ps1 -Profile` at 13:00 on 5 outdoor and 3 indoor cameras (1920×1080, GPU ms, the "full" variant), then while playing with the hitch test.

Sheets:
- `build/lookdev/lighting_options.png`;
- `build/lookdev/grass_options.png`;
- `build/lookdev/candidate.png` (now, half the grass, and Medium lighting with half the grass).

| Variant (console variables) | Outdoor | Indoor | Look |
|---|---:|---:|---|
| Epic, now (Lumen, the moods' quality 1.5) | 9.7 | 7.7 | |
| Lumen 1.0 (moods `lumen_*_quality` 1.0) | 8.4 | 5.7 | the same |
| High (`sg.GlobalIlluminationQuality 2`, `sg.ReflectionQuality 2`) | 7.5 | 4.8 | the same |
| Medium (`sg.GlobalIlluminationQuality 1`, `sg.ReflectionQuality 1`: Lumen's cheaper gather, screen-space reflections) | 6.7 | 3.9 | nearly the same; the pond reflects a little less |
| Low (`sg.GlobalIlluminationQuality 0`: no bounce light, no distance-field AO) | 6.4 | 3.4 | interiors darker and warmer, without the sky's blue fill |
| Cheapest (Low, `r.Shadow.Virtual.Enable 0`, `r.VolumetricFog 0`) | 6.4 | 3.1 | as Low; the haze is gone |
| Grass without shadows (`mr.Grass.Shadows 0`) | 9.5 | | the grass goes flat and lime green |
| Half the grass, no shadows | 8.9 | | |
| Half the grass, shadows kept, drawn 70% as far (`mr.Grass.Density 0.5`, `mr.Grass.Distance 0.7`) | 8.6 | 7.3 | a little sparser |
| **Medium + half the grass with shadows** | **5.6** | **4.0** | close to now |

**While playing** (hitch test, median GPU ms outdoors / indoors):
- Epic: 12.0 / 8.2.
- Medium: 9.7 / 5.8.
- Half the grass at 70% distance: 10.8 outdoors.
- Grass without shadows: 11.7. Its shadows are cheap even while the grass sways in the wind, because the virtual shadow map caches them.

The profile runs freeze time, so they read lower than play.

**Chosen (user, 2026-10-10): Medium lighting and half the grass, shadows kept, drawn 70% as far.**
- **Lighting:** `Config/DefaultGameUserSettings.ini` sets `sg.GlobalIlluminationQuality=1` and `sg.ReflectionQuality=1` for new installs. A player's saved settings win, and the Options "Quality" preset still sets every group, lighting included.
- **Grass:** in the world build's data (`zone_300.json`: 6/m², 28–42 m; 68,612 instances), so the tufts are spaced evenly rather than thinned at runtime.
- The moods' `lumen_*_quality` stay at 1.5: Medium was measured with them.
- **Checked while playing, with the new grass (hitch test):**
  - **Uncapped:** Medium has the faster typical frame (p50 9.3 ms against Epic's 12.2, p95 13.7 against 16.2). But it has about twice the short stalls, 46 frames over 33 ms against 22, and High is in between at 39. They are the render thread waiting on worker threads (`EventWait/Visibility`), not the GPU: uncapped, the faster settings simply drive this 6-core machine's CPU harder.
  - **Capped at 60 fps** (`-Extra -MRHitchMaxFPS=60`, as players run with vsync or a limit), Medium is better on every count. It has 4 frames over 33 ms against Epic's 8, a worst frame of 62 ms against 115, and a median GPU time of 5.8 ms against 9.8.
  - **A default frame cap** (vsync or 60 in `DefaultGameUserSettings.ini`) is worth considering. Options already has the setting, but new installs start uncapped.

**Also chosen (user, 2026-10-10):**
- **No bounce lighting indoors.** The interior moods set `dynamic_global_illumination_method` `None` (the outdoor `raza_afternoon` sets `Lumen` again), whatever the Lighting option. That gives the original's warm, dark rooms without the blue haze the sky put into Lumen's fill. It costs ~3.6 ms indoors against 3.9 ms at Medium (`build/lookdev/interior_lighting.png`). The Mausoleum inherits it.
- **60 fps unless the player picks otherwise.** `Config/DefaultGameUserSettings.ini` `FrameRateLimit=60`. Options has 30, 60, 120, 144, Uncapped and vertical sync.

**Traps:**
- An `sg.*` set with `-ExecCmds` is saved to `Saved/Config/WindowsEditor/GameUserSettings.ini` and carries into later runs. Set it in every run, then put it back.
- `mr.Grass.*` are new in this pass (`AMRScatterActor::ApplySettings`). They thin and shorten the world build's grass at runtime, so they can become the Options foliage setting.

## Follow-ups

- **The sky light now takes the skybox's colour.** The day is bluer, the dusk violet. The user likes it for now (2026-10-10); revisit if it should go: tint the dome only outside the sky light's capture, or lower `SkyboxBrightness` and lift the moods' ambient.
- **Options > Graphics:** done 2026-10-10. It has lighting, shadows, effects, textures, grass, resolution scale and the frame limit (ADR 0009).
- **Defaults tuned (2026-10-10):** Medium lighting and half the grass. See "Chosen" below. Still open: volumetric fog distance (0.3–0.7 ms).
- **Packaged builds:** record a PSO cache (`-logPSO`, `ShaderPipelineCacheTools expand`) for the shaders precaching misses. Then time the packaged game with the hitch test, which is all uncooked so far.
- **Online:** spread a room's object spawns over a few frames when there are many, if the hitch test ever runs online.
