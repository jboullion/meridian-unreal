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
- **60 fps unless the player picks otherwise.** `Config/DefaultGameUserSettings.ini` `FrameRateLimit=60`. Options has 30, 60, 120, 144, Uncapped and vertical sync. (Replaced by vertical sync on 2026-10-13: "Packaged game" below.)

**Traps:**
- An `sg.*` set with `-ExecCmds` is saved to `Saved/Config/WindowsEditor/GameUserSettings.ini` and carries into later runs. Set it in every run, then put it back.
- `mr.Grass.*` are new in this pass (`AMRScatterActor::ApplySettings`). They thin and shorten the world build's grass at runtime, so they can become the Options foliage setting.

## Zone streaming (2026-10-11)

The game used to load every zone at start (`UMRZoneSubsystem::LoadAllZoneLevels`): 64 zones took 3.3 s and the process about 560 MB, and every zone's fires, grass, monsters and sounds stayed in the world. The world remaster takes the built zones to about 330 (`tools/world/overnight.py`), so the game now keeps only the zones near the player loaded, as the client of the retired UE-server path did (ADR 0001 decision 6):
- **What's loaded:** the zone the player is in, the zone the view is in and a zone pinned by `EnsureZoneLoaded` (a spawn, a server room, a tour's stop), each with every zone one exit away (offline the zone's exits, online also the server's room links, `UMRRuntimeRooms::BuiltNeighbours`) and the zone it shares geometry with. All of them visible: zones sit 2 km apart, so a neighbour costs only culling and taking an exit is a same-frame switch.
- **What goes:** a zone no longer wanted stays `mr.Zones.KeepSeconds` (60), at most `mr.Zones.MaxLoaded` (24) stay, the least recently wanted unloading first, one level per check (4 a second).
- **Arriving further away** (a teleporter, recall, death, the Underworld): offline the teleport waits for the level (up to 8 s); online a built room freezes the pawn with "Loading <room>..." until its level shows, as a room built at runtime does.
- **Offline monsters and NPCs** appear with their zone's level and go when it unloads (`UMRMonsterSubsystem::FollowZones`).
- **Tours** that jump between zones (look-dev, the step survey, map capture, the hitch tour) call `EnsureZoneLoaded(rid, true)` per stop; others follow the view.
- `mr.Zones.LoadAll 1` (at start) loads every zone as before.

## Packaged game (2026-10-13)

The packaged game still felt choppy and stuttered, though the uncooked hitch test looked fine. Everything above was measured uncooked, and before zone streaming. So this pass measured what players run: a packaged Development build (`tools/ue/package.ps1 -Config Development -OutName Win64-dev`) under the hitch test (`run_hitch_test.ps1 -Packaged build/package/Win64-dev`), which now:
- lingers 75 s at the end, so zone unloads and garbage collection show;
- marks each stop in the log ("MRHitch: at <stop>");
- counts PSO misses, loading flushes, collections and hitches per stop;
- with `-Extra -MRHitchDump`, logs every frame over 40 ms's stat stack.

RTX 3070, 1920x1080, uncapped, the project's defaults (`-CleanConfig`):

| Run | p50 | p95 | p99 | Frames over 33 ms | over 100 ms |
|---|---:|---:|---:|---:|---:|
| before (`pkg_default`) | 8.5 | 12.5 | 19.1 | 72 | 43 |
| before, again (`pkg_default2`: the driver's shader cache warm) | 8.4 | 13.6 | 28.3 | 100 | 41 |
| before, with `mr.Sprite.ScreenTexels 0` (`pkg_nodensity`) | 8.0 | 11.1 | 14.1 | 14 | 1 |
| after, without the minimap's background load (`pkg_after`) | 8.1 | 11.5 | 14.8 | 11 | 3 |
| **after** (`pkg_final`) | 7.8 | 11.2 | 14.3 | 9 | 1 |

**The cause: our own sprite, in first person.**
- The hitches were the render thread waiting 50–700 ms in View Visibility for the GPU's occlusion results.
- An Unreal Insights trace showed where the GPU spent those frames: `GPUResourceUpdate` and `DrawCanvasToTarget`. That is sprite render targets being cleared and drawn.
  - The trace: `-tracefile=... -trace=default,task -statnamedevents`.
  - Exported with no window: `UnrealInsights.exe -OpenTraceFile=<trace> -NoUI -AutoQuit -ExecOnAnalysisCompleteCmd="@=<file of TimingInsights.ExportTimingEvents commands>"`.
- The player's body is hidden from its own camera in first person (`SetOwnerNoSee`), but its shadow card still draws it. So it kept choosing its density from the camera: about 12 cm away, inside its head. That asked for the largest targets there are, 3 × 2048 × 2048, drawn again at every pose and every turn of the view.
- `UMRSpriteBodyComponent::UpdateDensity` now keeps the base density for a body its viewer can't see. Its shadow doesn't need more.

**Also changed:**
- **Sprite render targets are pooled** (`UMRSpriteTargetPool`, `mr.Sprite.PoolMB` 96). A sprite borrows a set of its size and gives it back, rather than making three new targets on every look change and density step. A change that keeps the size keeps the targets. At most `mr.Sprite.ResizesPerFrame` (2) sprites resize a frame; the others keep their old targets meanwhile.
- **No synchronous loads during play:**
  - **Sounds:** the sound cache holds what it found. It was weak, so a sound collected after it played loaded again in the middle of a frame. The monsters' sounds and the weather's are preloaded (`UMRAudioSubsystem::PreloadSounds`).
  - **The weather's materials** are preloaded at the first tick, and the zone materials are kept. A zone without its own instance is no longer looked up on disk each time.
  - **The minimap's zone picture** loads in the background (`UMRUISubsystem::GetMapTexture`). Its synchronous load cost 55–82 ms at every zone change (`pkg_after`'s interiors).
- **Zone stream-in is spread over frames:** a zone's monsters and NPCs spawn from a queue, at most `mr.Monster.SpawnBudgetMs` (2) a frame.
- **Online, rooms built at runtime:** entering one applies every stored change and then rebuilds once (`AMRRuntimeRoom::BeginChanges`/`EndChanges`). It used to rebuild, collision cooking included, once per change.
- **No forced collection after each zone unload** (`s.ForceGCAfterLevelStreamedOut=False`). The regular 61 s collection takes the leftovers. The incremental collector (`gc.AllowIncrementalReachability`) is still experimental in 5.8, so it is left off.
- **Smaller fixes:**
  - **Fire shadows:** a fire's shadow stays until another fire is clearly nearer (20%), so two fires no longer swap as you walk.
  - **Mood actors:** a mood's actor that isn't in the world is remembered as missing until a level comes or goes, instead of scanning every actor on each apply.
  - **Lamps:** they are found per streamed level. They used to be found once at the start, so lamps in later zones never lit.

**PSO cache: not needed now.** With the warm-up, the packaged game misses 73–82 PSOs while the loading screen is up and 0–1 after it. A bundled PSO cache would only shorten the warm-up.

**Frame pacing: vertical sync by default** (`Config/DefaultGameUserSettings.ini` `bUseVSync=True`, `FrameRateLimit=0`).
- The 60 fps limit sleeps the game thread without lining frames up with the display. On a 120–165 Hz screen without variable refresh, frames stayed up for an uneven 2 or 3 refreshes: the "choppy at a good frame rate".
- Settings saved before this change get it once, with lighting and reflections no higher than Medium (`MRSettings` `ApplyNewDefaults`, `[UnrealMeridian.Defaults] Version=1`). The maintainer's packaged settings had been uncapped with vsync off, every group at Epic. They predated the 60 fps and Medium defaults, and a saved file always wins.

**Motion blur: off** (user's call). It was on at the engine's Epic post-process level (`r.MotionBlurQuality 4`). Now `r.DefaultFeature.MotionBlur=False` in `DefaultEngine.ini`, and `r.MotionBlurQuality=0` at every `PostProcessQuality` level in `DefaultScalability.ini`. It saves a little GPU time (the motion blur passes, a few tenths of a ms at 1080p), and the sprites and moths no longer smear. Motion blur hid judder at uneven frame rates; even pacing replaces that.

**Smooth motion:**
- **The view over steps:** a step up or down, or out of a pool, moved the capsule, and the eye with it, in one frame (up to 82 cm, 3 m at a room's wall). The view now follows over `mr.Camera.StepSmoothSeconds` (0.12). Only what rises or falls faster than 45° counts as a step, so slopes pass straight through (`AMRCharacter::SmoothCameraStep`, after each movement update).
- **The third-person camera:** after a wall pulls it in, it eases back out (`UMRSpringArmComponent`, `mr.Camera.ArmReturnSpeed` 6). It still pulls in at once.
- **Other players and monsters online:**
  - **Speed:** they reach speed at once (their acceleration was the engine's 2048 cm/s², 0.6 s to a run), and hurry when more than 0.35 s behind the server, as the original client does.
  - **Walk and stand:** they keep walking 0.3 s after reaching a position, as the next one is due. That holds both their sprite's walk (`UMRSpriteBodyComponent`) and a server bitmap's (`AMRNetObject`). A turn (`BP_TURN`) no longer starts a walk.
- **Server bitmaps** tick after the camera's update, like our sprites, so they face this frame's view.

**Left:**
- **The remaining slow frames:**
  - the warm-up's last frames;
  - one GPU wait of the same kind outdoors, perhaps a nearby NPC's density step;
  - the first frame at a teleport's stop.
- **Online:**
  - **Room entry:** a room's objects all spawn in one frame. Each is an `ACharacter` with an AI controller, even a static prop.
  - **Server sounds:** a server sound's `.ogg` is decoded on the game thread (`MRServerSound::Decode`).
  - Both only show online, so they wait for an online hitch test.
- **The sprites' per-pose redraw** (three canvas passes per sprite, each a render graph of its own) is the GPU's steady cost with many sprites in view.

**Fewer props (later the same day, ADR 0007 "Fewer props"):**
- The world places only the lights', flames' and trees' meshes (`props.json` `"mesh_keep"`).
- The props' material instances moved out of the always-cooked materials folder.
- The package went from 5.52 to 3.46 GB of game data.
- Video memory peaked at 3.9 GB instead of 5.4 GB (budget 5.9).
- Frame times stayed the same (`pkg_props`: p95 11.5 ms, 7 frames over 33 ms, 1 over 100).

## Online (2026-10-13)

**The online test now times frames** (`run_net_test.ps1`; `-Packaged build/package/Win64-dev` for a packaged game):
- **What it logs:** `MRNetTest: frames <step> ...` per step, and `long frame <ms> (step <n>)` for every frame over 100 ms, so the log around it says what took it.
- **Screenshots apart:** frames taken by the test's own screenshots (`-Render`: 25 of them, 240–300 ms each) are counted separately.

**What it measured, and the fixes:**

| Cost (uncooked, `-Create -Render`) | Before | Fix |
|---|---|---|
| Entering Raza: its 81 objects spawned in one frame | 41–57 ms | Spawned nearest first, `mr.Net.SpawnBudgetMs` (4) a frame (`UMRNetWorldSubsystem::SpawnPending`) |
| A cached sprite or room file read and hashed on the game thread | 25–44 ms, now and then | Read and hashed on a worker; the answer comes on the game thread a frame later (`FMRAssetCache::Fetch`). Prefetching downloads only what's missing (`Prefetch`) |
| The server's music and sounds mapped to imported originals, loaded at once | 100–160 ms on a zone change (a music track) | Loaded in the background (`UMRAudioSubsystem::FindImportedAsync`) |
| The interface's art and icons loaded the first time a window painted (inventory, quests, shop) | ~100 ms each | Preloaded with the sprite atlases behind the loading screen (`UMRWarmup`). Not in the test modes, which skip the warm-up: a load in play behind 900 queued packages waited 2.5 s for all of them |
| Each server bitmap made its own textures, created twice (wrapping, then clamped) | a texture per bitmap per object | One texture per bgf bitmap for the world, made once (`UMRBgfTextures`, `FMRBgf::MakeTexture` `bClamp`) |

**Results:**
- **Packaged online** (`-Create -Render -Packaged`, two runs, screenshots apart): 23–39 frames over 33 ms and 1–4 over 100 ms in the whole session (login, chat, items, spells, trades, combat, travel). The over-100 ms ones are mostly at login.
- **Packaged offline, capped at 60** (`-Extra -MRHitchMaxFPS=60`, as players run): p50 16.7 ms, p99 17.5–18.8 ms, 5–15 frames over 33 ms, 0–1 over 100 ms.
- **Uncapped runs vary much more** (7 to 54 frames over 33 ms across runs of the same build). In their slow frames neither the GPU nor the game or render thread accounts for the time: the waiting is outside the engine. Judge by capped runs and repeat them.

- **The offline sprites cost nothing measurable.** Same build, capped at 60, twice each, on and off (`-MRNoOfflineSprites`; `mr.World.OfflineSprites 0` hides them as zones load): on, p99 17.6–17.7 ms and 7–10 frames over 33 ms; off, 17.5–17.7 ms and 7.

**Not worth doing:**
- **Server sounds' Ogg decode:** 1.3 ms.
- **The sprites' redraw:** our sprite ticking is 0.27 ms a frame and the drawing 0.03 ms.
- **Draw calls:** 282 a frame; the render thread mostly waits.

## Follow-ups

- **The sky light now takes the skybox's colour.** The day is bluer, the dusk violet. The user likes it for now (2026-10-10); revisit if it should go: tint the dome only outside the sky light's capture, or lower `SkyboxBrightness` and lift the moods' ambient.
- **Options > Graphics:** done 2026-10-10. It has lighting, shadows, effects, textures, grass, resolution scale and the frame limit (ADR 0009).
- **Defaults tuned (2026-10-10):** Medium lighting and half the grass. See "Chosen" below. Still open: volumetric fog distance (0.3–0.7 ms).
- **Packaged builds:** timed on 2026-10-13 ("Packaged game" above). A recorded PSO cache (`-logPSO`, `ShaderPipelineCacheTools expand`) would only shorten the warm-up: there are no PSO misses after it.
- **Online:** spread a room's object spawns over a few frames when there are many, if the hitch test ever runs online (also in "Packaged game", "Left").
