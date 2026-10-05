# ADR 0006: Audio

- Status: Accepted (phase 1a and 1b built; see "Phase 1 built")
- Date: 2026-10-05

## Context
Meridian's sound is a big part of how it's remembered: the login theme over Raza's square, birdsong and lapping water around the pond, the crypt's dripping cave and necropolis groans, the smithy's fire. We have the original client's 705 sound files (`.ogg`, in `%LOCALAPPDATA%\Meridian-104\resource`), and the Server 103/104 teams' permission to use all original game assets ([ADR 0001](0001-engine-and-architecture.md)).

The project's notes (`TODO.md`) set the direction: keep the same sound files for the most part, and add or improve sounds where it makes sense, such as spells.

**The user's plan (2026-10-05):** a first pass that uses the audio Meridian already has and sounds like the original. More sounds go on top after that.

Phase 4 of [ADR 0005](0005-time-weather-and-atmosphere.md) already plays the original's `rain.ogg`, `Rs_wind.ogg` and `thunderclap.ogg` from the environment director. That was a stopgap until this ADR.

### How the original plays sound
Both sources are in this repo: the client in `Server-104/clientd3d/audio.c`, `game.c`, `server.c` and `move.c`; the server in `Server-104/kod`.

| Part | The original |
|---|---|
| **Engine** | irrKlang. Up to 24 sounds at once (more are dropped). A sound plays either 2D (at the player) or 3D at a point in the room, with the listener at the player and facing their way. 3D volume rolls off gently (rolloff 0.00016 per fine unit) and is silent beyond 32 grid squares (about 70 m; a square is 2.2 m in the remaster). |
| **Who decides** | The server. Kod sends `BP_PLAY_WAVE` (sound, source object or row/col, flags), `BP_STOP_WAVE` and `BP_PLAY_MUSIC`. The client only plays what it's told, plus a few sounds of its own (wading, particles, UI). Flags: `SF_LOOP` loops until the player leaves the room, `SF_RANDOM_PLACE` means Kod picked the spot, and `SF_RANDOM_PITCH` asks for a random pitch, which the 104 client ignores. |
| **Music** | One looping track per room (`prMusic`), sent on entering. The same track carries on across rooms; a new one replaces the old at once. |
| **Room loops** | Each room has a list `plLooping_sounds` of [sound, row, col, radius, max volume]. Each plays as a looping 3D sound at its square. The terrain adds one ambience loop at square (1, 1): forest `ambcntry.ogg`, caves `ambcave.ogg` (volume 70), necropolis `necloop2.ogg`, sewers, jungle, beach, lava, mountains. The 104 client ignores the radius and max volume. |
| **Periodic sounds** | Every room with a terrain plays one random sound from its terrain's list every `piPeriodic_sounds` ms (20 s, halved for each terrain that adds sounds, kept within 2–20 s, ±20% each time), at a random square, with random pitch requested. Forests get birds (`rs_for*`); lakes and beaches get wind, beach, gulls and waves; cities by water add waterfront sounds; caves a drop; necropolises groans (`necro01–09`). |
| **Objects and combat** | Monsters: `vrSound_aware`, `vrSound_hit`, `vrSound_miss`, `vrSound_death` (in `data/monsters.json`). Players: weapon swings, hits on flesh, leather or metal, ouches (male or female), death, level-up (`imp.ogg`, `tougher.ogg`), learning a spell. Spells: `vrSucceed_wav` (in `data/spells.json`). Doors and levers, wading (per room: Raza's is `weatstp1.ogg`), weather. |
| **System cues** | Welcome, logging on and off, saving, safety on and off, "can't carry that", "can't go anywhere". |
| **Player settings** | Music on/off and volume, sound on/off and volume (0–100, default 100), and separate switches for looping sounds and random (periodic) sounds. |
| **Not in the original** | Footsteps (only wading), reverb, occlusion, a crossfade between music tracks, and anything that changes with the time of day. |

### What Raza and its zones play (from the Kod)
| Zone | Music | Loops | Periodic (random spot) |
|---|---|---|---|
| Raza (300; city, road, lake, forest) | `login.ogg` (the title theme) | `ambcntry.ogg` (country) | every ~5 s: 7 birds (`rs_for01–20`), wind, beach, gulls, 4 waves, 2 waterfront |
| Outskirts (330), Farol West (331) (forest) | `walk5.ogg` | `ambcntry.ogg` | every ~10 s: the 7 birds |
| Inn, Hall, Bar, Hut, Museum, Apothecary (city, shop) | `login.ogg` | none | none |
| Smithy (303) | `smithy.ogg` | `fireplac.ogg` at square (9, 6) | none |
| Vault (332) | `smithy.ogg` | none | none |
| Bank (333) | `bank.ogg` | none | none |
| Mausoleum (306; caves, necropolis) | `nec03.ogg` | `ambcave.ogg` (70) | `drop.ogg` and `necro01–09` |

- **Other sounds:**
  - Raza's wading `weatstp1.ogg`.
  - The crypt's doors and levers `down2.ogg`.
  - Monsters: rats, centipedes, bunnies and baby spiders in the forest, mummies in the crypt. Their `*_awr`, `*_atkh`, `*_atkm` and `*_dth` sounds.
  - Weather (built).

## Decision

**Faithful first, then richer.** Phase 1 reproduces the original's soundscape with its own files and rules, data-driven from the Kod as the weather and moods are. It sounds like the original, with a modern mixer under it. Phase 2 adds what the original never had. Each addition gets its own switch and is judged against phase 1.

### 1. Assets
- **Source and storage:**
  - The original client's `resource/*.ogg` are copied to `build/audio/original/` and imported to `/Game/Generated/Audio/Original/<name>` (git-ignored, as all original assets).
  - Only the sounds the data references are imported, plus anything the data names later.
  - Names stay the original file names, so a Kod reference maps straight to an asset.
- **Format:** the files are imported as they are, with no re-encoding by hand. UE imports `.ogg` directly (as phase 4 did).
  - Looping is set on import for sounds that are used as loops.
  - Music streams; short effects stay in memory.
- **Extraction:** a new `tools/audio/extract_audio.py` (in the style of `tools/kod_extract`) reads the Kod and writes `data/audio/`:
  - `rooms.json`: per room id, the music, the terrain loop, the `plLooping_sounds` (with squares turned into zone metres), the periodic list and interval, the wading sound, door sounds.
  - `sounds.json`: every sound by name, with its category (music, ambience loop, periodic, combat, spell, UI) and whether it loops.
  - Monster and spell sounds stay in `data/monsters.json` and `data/spells.json`.
  - Player and system sounds are read from `user.kod` and `player.kod` constants.
- **Import:** `build_world.py` (later its own `build_audio.py`) imports them, generalising phase 4's `weather_sounds()`.

### 2. Runtime
- **`UMRAudioSubsystem`** (client world subsystem, like the environment director) owns everything the client decides:
  - **Music:** the zone's track on entering, kept playing if the next zone has the same track. Whether a change cuts or crossfades is decision 1 below.
  - **Room loops:** looping 3D sounds at their positions. The terrain loop plays at the original's square (1, 1) only if that sounds right (decision 3); otherwise it's an ambient 2D bed.
  - **Periodic sounds:** a timer per zone, the original's interval and ±20% jitter, a random sound from the list, at a random point in the zone.
  - **Wading and weather:** phase 4's rain, wind and thunder move here from the environment director, which keeps only the visuals.
  - **Settings:** the original's settings, as console variables and later the options menu.
- **Gameplay sounds come from the server, as in the original:**
  - Monster aware/hit/miss/death, player hits and ouches, weapon swings, spells, level-up and system cues are triggered by server-side gameplay. Each plays on the clients that can hear it.
  - Combat and spells use GAS Gameplay Cues ([ADR 0001](0001-engine-and-architecture.md): gameplay is on GAS), carrying the original sound name and the source actor.
  - One C++ entry point (`UMRSound::Play(World, Name, Location or Actor, Flags)`) mirrors `WaveSendUser`, so ported Kod logic calls one thing.
  - These arrive with the combat and spell systems; this ADR fixes the API and the data, and the Raza zones' ambience comes first.
- **Mixing:**
  - **Sound classes:** Master → Music; Ambience (Loops, Periodic, Weather); Effects (Combat, Spells, World); UI. One submix each for Music, Ambience, Effects and UI.
  - **Settings:** they map onto these: music volume, sound volume (everything but music), loop sounds on/off, random sounds on/off.
  - **Levels:** the original plays everything at the sound volume. Phase 1 starts with every category at unity. The levels are then set by ear, with the user, against the original client.
- **Space:**
  - 3D sounds use one attenuation preset matching the original: a gentle rolloff to silence at 32 squares (70 m), with standard stereo panning (no HRTF).
  - 2D sounds play at the listener.
  - At most 24 voices per category group, as the original, with UE concurrency rules (the quietest or oldest stops).

### 3. Verification
- **Can't be heard here.** Look-dev runs with `-nosound`, and the AI can't listen. So:
  - **Logs:** every sound the subsystem starts or stops is logged (`MRAudio: play ambcntry.ogg loop at ...`), and an automation test checks the data (every referenced sound exists; Raza's music, loops and periodic list match the Kod).
  - **An audio look-dev, `run_audiotest.ps1`:** stands at fixed spots and hours, records the main submix to WAV (UE's submix recording), and writes the log of active sounds. `tools/audio/audio_report.py` turns the recordings into a sheet: loudness over time per category, plus a spectrogram. This catches silence, clipping and level jumps.
  - **Listening by the user** decides how it sounds, A/B against the original client on the same spots.

## Phasing

| Phase | Result | Depends on |
|---|---|---|
| 1a. Data and assets | `extract_audio.py` → `data/audio/`; import of the referenced originals; automation test of the data | – |
| 1b. Ambience | `UMRAudioSubsystem`: music per zone, room and terrain loops, periodic sounds, wading; weather moved over; settings; mixer; audio look-dev | 1a |
| 1c. Gameplay sounds | `UMRSound::Play` and the Gameplay Cue path; monster, combat, spell and system sounds as those systems are built | 1a; combat and spells |
| 2. Beyond the original | See below: one switch each, judged against phase 1 | 1b |

**Phase 2 candidates** (in rough order of value, each a decision with the user when it comes up):
- **Reverb per zone kind:** small rooms, the Hall, the crypt's stone; Audio Volumes or a submix effect set by the zone profile.
- **Fire crackle** at every torch, brazier and candle (the original's `fireplac.ogg` exists but only the smithy uses it), quiet and close-range.
- **Footsteps** by surface (stone, wood, grass, water): new sounds.
- **Day and night:** crickets and owls at night, fewer birds after dark; rain on roofs heard inside.
- **Occlusion** through walls and doors.
- **Random pitch** on periodic and combat sounds, as Kod asks.
- **Improved or new spell sounds** where the originals are thin (`TODO.md`).
- **Music:** day and night or combat variants, if the user wants them.

## Decisions taken (user, 2026-10-05)
All four recommendations below were accepted: a 1.5 s crossfade between tracks, no random pitch in phase 1, the terrain loop at the original's square (to be judged by ear), and Raza's rain stays audible.

## Phase 1 built (2026-10-05)
- **Data** (`tools/audio/extract_audio.py`, `data/audio/`):
  - It ports `room.kod`'s terrain rules (loops and periodic lists) and reads each demo room's own `plLooping_sounds`, music, wading and door sounds. Squares become zone metres, and the floor under each loop comes from the blockout (the nearest floor where none is under it).
  - **Results:**
    - Raza: `login.ogg`, `ambcntry.ogg`, and 16 periodic sounds every 5 s.
    - Outskirts and Farol West: `walk5.ogg`, `ambcntry.ogg`, and 7 birds every 10 s.
    - Smithy: `smithy.ogg` and `fireplac.ogg` at square (9, 6).
    - Crypt: `nec03.ogg`, `ambcave.ogg`, and 10 sounds every 10 s.
    - The rest of the interiors: music only.
  - `sounds.json` lists the 191 sounds the demo can play (rooms, the demo monsters, all spells, the player and system cues, weather), by category.
  - Four that the Kod names aren't shipped with the client and are marked `missing`: `flight.ogg`, `perc2.ogg`, `rperappr.wav`, `welcome.ogg`.
  - `data/audio/audio.json` is hand-written: attenuation, fades, loop height, jitter, random pitch, and levels per sound class.
- **Assets** (`tools/ue/build_audio.py`, run by `build_world.py`):
  - The 187 shipped sounds are copied to `build/audio/original` and imported to `/Game/Generated/Audio/Original/<name>`, looping set by use.
  - Each gets its sound class under `SC_Music` / `SC_Sound` (`/Game/Generated/Audio/Mix`, with `SM_Settings`).
  - Phase 4's `/Game/Generated/Audio/Weather` is gone; the weather sounds are among the originals.
- **`UMRAudioSubsystem`** (client, game and PIE worlds):
  - Music per zone with the 1.5 s crossfade, kept across zones with the same track.
  - Zone loops at their squares (fading in over 0.5 s, out on leaving).
  - Periodic sounds at random squares, every interval ±20%.
  - The original's attenuation as a custom curve: volume 1 / (1 + 0.0745 × metres), silent beyond 70.4 m.
  - Named 2D loops and one-shots; `Play()` for gameplay.
  - Settings cvars `mr.Audio.Music`, `MusicVolume`, `Sound`, `SoundVolume`, `Loops`, `Random`, applied through `SM_Settings`.
  - Everything the rooms name is preloaded asynchronously, so a random sound never stalls on a load.
  - It logs `MRAudio: music|loop|periodic ...`.
- **Weather moved:** the environment director still decides when it rains, winds and thunders, but plays them through `UMRAudioSubsystem` (`SetLoop2D`, `PlayOriginal`, muffled inside).
- **Tests:** `Meridian.Audio.Data` checks:
  - the attenuation and asset names;
  - Raza, the smithy, the crypt and the inn against the Kod;
  - that every shipped sound imported.
  It passes, as do the six environment tests.
- **Audio look-dev:**
  - `run_lookdev.ps1 -Audio <seconds>` keeps sound on, records the main mix at each camera (`<camera>.wav`, through `AudioMixer` submix recording) and writes `audio.log`.
  - `python tools/audio/audio_report.py <label>` draws `audio_sheet.png` (still, loudness L/R, spectrogram, the sounds started) and prints peak, RMS and L−R per camera.
  - **First run** (`aud2`, `aud2_storm`):
    - every spot has sound, with peaks between −3 and −8 dBFS and RMS between −20 and −26;
    - in the storm, peaks reach −1.4 dB in the town;
    - no clipping.
- **Found on the way:**
  - A stray F1 (the engine's wireframe key) reached the game window during one recording; the stills after it are wireframe (`aud1`). It isn't from the code.
  - Loading sounds on first use stalled the game thread (`FlushAsyncLoading`) for every new random sound; the preload fixed it. The zone change still has one stall, from the environment director's per-zone particle material (phase 5), which is unrelated to audio.
- **Not built yet:**
  - Wading (needs the character's water depth).
  - The crypt's door and lever sounds, and every gameplay sound (they come with the systems that trigger them, through `UMRAudioSubsystem::Play` and Gameplay Cues).
  - Separate submixes (sound classes only for now).
  - A voice limit like the original's 24.
- **Next:** the user listens (Raza, the pond, the forest, the smithy, the crypt) against the original client. The levels in `data/audio/audio.json` `volumes` are set from that.

## Decisions as proposed
1. **Music changes:** the original cuts straight to the new track. Recommendation: a short crossfade (about 1.5 s), since a hard cut is jarring once zone changes are instant.
2. **Random pitch:** Kod asks for it on periodic sounds, but the 104 client ignored it. Recommendation: follow the 104 client in phase 1 (no pitch change, as players heard it), and offer it in phase 2.
3. **The terrain loop's position:** the original plays it as a 3D sound at the room's first square (Raza's north-west corner), so its level and panning depend on where you stand. Recommendation: reproduce it, listen, and switch to a 2D bed if it sounds wrong.
4. **Rain in Raza:** the original's weather mask made Raza's rain silent. Phase 4 plays it (`moods.json` `weather.sounds.follow_mask` false). This decision stays as it is unless the user says otherwise.

## Consequences
- The original's soundscape comes back with its own files, so it should sound like Meridian from the first build. Everything beyond it is opt-in and comparable.
- Gameplay sound depends on the server (as before), so it can't be finished before combat and spells exist. The Raza ambience doesn't wait for them.
- The audio can't be judged by look-dev. It needs the recordings, the logs and the user's ears.
- Raw audio never enters git: it's copied from the installed client and rebuilt, like the art.

## Action items
1. [x] `tools/audio/extract_audio.py` → `data/audio/rooms.json`, `sounds.json`; automation test against the Kod for the demo zones.
2. [x] Import of the referenced originals (`build/audio/original` → `/Game/Generated/Audio/Original`), looping set by use.
3. [x] `UMRAudioSubsystem`: music, loops, periodic sounds, weather moved from the environment director; settings cvars; sound classes. [ ] Wading. [ ] Submixes and a voice limit.
4. [x] The audio look-dev (`run_lookdev.ps1 -Audio`, built into the look-dev rather than a separate `run_audiotest.ps1`) and `audio_report.py`. [ ] First listening session with the user on Raza, the forest, the smithy and the crypt.
5. [x] `UMRAudioSubsystem::Play` (the `UMRSound::Play` of the plan). [ ] The Gameplay Cue path, with combat and spells.
6. [ ] Phase 2, one item at a time, after the user signs off phase 1.
