# Sprite players and monsters

**Question:** can players (later monsters) be drawn the way the original client drew them — 2D sprites composited from overlays, picked by viewing angle — and still sit well in the remastered 3D world? If so, new faces, hair and items become 2D image work instead of modelling, rigging and animation.

**Status (2026-10-06):**
- Phases 0–6 of the plan (`C:/Users/jboul/.claude/plans/i-am-working-with-dynamic-zebra.md`) are built and checked in game.
- Round 2, after the user's feedback, is also built:
  - quads drawn like the original's projection
  - third person tilts at most 20°
  - pure animation frames (in-betweens kept)
  - runtime colour
  - surface classes
  - lighting tuning
  - multiplayer sync
- Adopted for every animated character ([ADR 0008](adr/0008-sprite-characters.md)). The 3D MPFB player and its pipeline were removed on 2026-10-06.
- Monsters and NPCs use the same sprite body (see "Monsters"): every class in the demo zones, their Kod animations, corpses, spawning from the original spawn tables and a stand-in AI.

Every `AMRCharacter` is drawn with the sprite body. The default look is `DefaultSpriteLook` under `[/Script/MeridianRemastered.MRCharacter]` in `DefaultGame.ini`; a test client picks its own with `-MRSpriteLook=<name>`.

## How the original draws a player

Everything here was ported from the Server-104 source and checked image by image.

**Parts:**
- The torso is the object's own icon: `bta` male, `btb` female; armour `btc`–`btr`.
- Kod's `SendOverlays` (`player.kod:11509`) sends the overlays in this order, with their hotspots:

| Part | Bgf | Hotspot |
|---|---|---|
| Left arm | `bla` (female `blb`) | 31 |
| Right arm | `bra` (female `brb`) | 21 |
| Legs | `bfa` (female `bfb`) | 41 |
| Head | `phax` / `phkx` | 1 |
| Mouth | | 12 |
| Eyes | | 11 |
| Nose | | 14 |
| Hair | | 13 |
| Weapon | | 22, on the right arm |

- Face and hair names: `kod/util/system.kod:130`.

**Angles:**
- Every group has 8 slots, of which 6 views are unique: the three back slots share one bitmap.
- Hair groups run the other way round (`[0, 5, 4, 3, 3, 3, 2, 1]`), and face parts have no back view (`-1`).
- The angle is `(facing - direction from the object to the viewer) & 4095`, with 0 meaning the object faces you (`d3drender.c:5848`).
- The slot is `((angle + interval/2) % 4096) / interval`, where `interval = 4096/n + 1` (`draw.c:108`).
- UE yaw and the client's angle both turn clockwise seen from above (checked in game: the 8-angle orbit shows front, ¾, right profile, back in order).

**Placement** (`d3drender.c:6246`, same result as `object3d.c` `FindHotspot`):
- An overlay's top-left is the body's hotspot plus the overlay's own offset, both in torso pixels.
- It is scaled by `shrink_torso / shrink_overlay`.
- An overlay on an overlay (face parts and hair on the head) chains: head position + `(hotspot2 + offset) * shrink_torso / shrink_head`.
- A negative hotspot number means an underlay.

**Draw order:**
1. Under passes: under-under, under, under-over.
2. The torso.
3. Over passes: over-under-over-under (helmets), over-under, over, over-over.

Within a pass, parts go in Kod's order.

**Our one change to the draw order (2026-10-07):** seen from behind (view slots 3–5), the arms and what they hold always go under the torso. The original's attack and dance torsos from behind (`bta`/`btb` bitmap 9: groups 2 and 3, which the weapon and fist attacks use, and dance groups 14, 16 and 18–21) mark the right arm "over", so a punch or a backswing was drawn on the player's back. At the original's size it hardly showed; upscaled, the arm came out of the back. `mr.Sprite.BackArmsUnder 0` restores the original's layering, and so does `composite.py --original-layering`.

**Size:**
- A bitmap is `w/shrink*16` by `h/shrink*16` Kod fine units (1024 per grid square = 2.2 m).
- The torso's bottom sits `yoffset*4` fine units below the object position.
- With no fudge factor the male stands **1.84 m** and the female 1.70 m, so the original scale fits the 180 cm capsule as it is.

**Animation:**
- Kod's groups and timings are in `data/sprites/player_actions.json`.
- Walk: legs 2–5 at 100 ms, arms 2–3 at 200 ms.
- Weapon attack, fist, bow, cast, wave, point, and dance (17 poses at 150 ms).
- First-person hand and weapon overlays (`_first_person`).
- `clientd3d/animate.c` `AnimateSingle` runs them. Each group shows for its full period, then snaps to the next.
- Attacks: a swing lasts 900 ms (3 groups at 300 ms; the fist is 2 groups at 600 ms on the arms). The server allows one attack a second (`player.kod` `IsOkayAttackTime`, 1000 ms), so a swing is never cut short. The remaster does the same: `AMRCharacter::AttackIntervalSeconds` (1 s) on the owner, checked again on the server.
- One-shot actions hold each pose and play their in-betweens only in the last `mr.Sprite.Smooth.OnceWindow` (35 %) of it, closer to the original's snaps. Before 2026-10-07 the in-betweens ran across the whole pose, so an attack's strike pose showed for only ~75 ms before easing back. Cycles (walk, dance) keep in-betweens across the whole pose.

**Colour** (`xlat.c`):
- Faces are drawn in the dark-blue ramp and translated to 4 skins.
- Hair is drawn in grey and translated to the creator's 14 colours.
- Torso, arms and legs are red and blue: two-colour xlats `0x87 + shirt*11 + skin`.

**Projection** (`d3drender.c:780`):
- Every object quad is turned by the camera's heading only (`mPlayerHeadingTrans`), so all sprites are parallel to the screen. They don't turn toward the camera's position.
- A quad parallel to the image plane projects without perspective stretch, even at the screen edges.
- The player can only look about ±11° up or down: `height_offset` is limited to `HEIGHT_MAX_OFFSET/4` = 103.5, and pitch = `height_offset × 45/414` degrees (`move.c:1079`). So sprites are never seen from steep angles.

**Movement versus animation** (`move.c`):
- The legs always cycle at 100 ms per pose, whatever the speed.
- Players moved fast: walking was `MOVEUNITS` (256 fine units) per 85 ms, about 2.9 squares/s ≈ 6.5 m/s, and running was double that.
- The remaster's run is the original's 12.94 m/s (`mr.Move.SpeedScale`, docs/findings.md "Walking"); the cycle plays at the original rate at the run speed. `mr.Sprite.WalkRefSpeed 0` plays the original fixed rate.

## Results

| Test | Result | Evidence (`build/`, git-ignored) |
|---|---|---|
| Port fidelity | All 8 angles of every action match the original, as do weapons, hair, skin and clothes. The C++ port matches the Python one (`Meridian.Sprites.Placement`). | `sprites/preview/<look>/*.gif` |
| Upscale each part, or the composite? | Each part. Face parts (shrink 14) keep 3.5× the torso's detail, and the shrink-2 legs keep 2×. No seams. Upscaling the composite smears the face. | `sprites/upscale_test/` |
| In the world | Quads parallel to the screen, as the original drew them, following the camera's pitch about the feet (`mr.Sprite.Billboard 1`), so nothing is stretched or squashed from any angle. Feet on the ground, right size, lit by the world and its moods. An upright card from the sun casts the shadow; in the shadow pass it is pushed along the light, so it doesn't shadow the sprite itself. | `sprites/tour/light_day_v2`, `light_inn_v2` |
| Cost (50 extra characters, Raza, 1280×720, RTX 3070) | Sprites: game 3.5–3.9 ms, GPU 12.3–13.2 ms, 0.2 M primitives. The engine mannequin: game 6.6 ms, GPU 14.3 ms, 2.4 M primitives. (The MPFB bodies are Git LFS files not fetched in this worktree, so the comparison is with the mannequin.) | `sprites/tour/crowd_v2/crowd_50.png` |
| Camera views (Phase 3) | V cycles first person → chase → behind → front (the selfie view). The third-person views orbit freely and tilt at most `mr.Camera.ThirdPersonPitch` (20°) from eye level; first person looks freely. P takes a photo without the HUD. First person draws the original 2D hand or sword at the original scale, with the original attack frames. | `sprites/tour/views/` |
| Smoothing (Phase 4) | In-betweens per part transition (no AI model): 445 of 868 steps get 3 frames. Optical flow where the silhouettes overlap (legs); a rigid turn about the shoulder, then flow, for arm swings; the rest keep the original snap. The user kept the in-betweens. Procedural motion, crossfade and view fade are built but off ("pure animation frames"). | `sprites/clips/walk.gif`, `dance.gif` |
| Runtime colour (Phase 5) | Each part is stored once, untranslated; the GPU applies the look's palette translations. 26 atlases, 469 MB uncompressed (≈117 MB BC7), down from 1.1 GB for the same 5 looks with baked colours. The cost stays flat for any number of colour combinations: a 50-player crowd with random creator colours runs as fast as before. | `sprites/tour/crowd_v2/` |
| AI hair (Phase 5) | One prompt gave 6 consistent views of a new style. It takes every creator hair colour and sits on the head at all 8 angles. | `sprites/ai_hair/`, `sprites/preview/ai_hair_heads.png` |
| Heights (Phase 6) | 90–110 % (server-clamped) scales the drawing and the first-person eyes; the capsule doesn't change. | `sprites/tour/crowd_v2/` |
| Multiplayer | Look, creator colours, height and the current action replicate (`FMRSpriteAppearance`, `FMRSpriteActionState`). Dedicated server plus 2 clients: client B sees client A's `test_sword`, colours, 105 % height and dance (`tools/ue/run_sprite_net_test.ps1`: PASS). | `Saved/Logs/spritenet-*.log` |

## Colour at runtime

**What it replaced:**
- Atlases used to hold each part already translated, so every colour combination needed its own atlas set.
- Now each part is upscaled once in its untranslated colours.
- Each texel's source palette ramp, taken from the original's palette indices, is stored in the atlas alpha (`luts.ramp_alpha`: 0.6 none, 0.733 red, 0.867 dark blue, 1.0 grey).
- The runtime recolours the way the original client did.

**The two render passes:**
1. A colour pass draws the parts alpha-tested (`SE_BLEND_Masked`, crisp like the original), so the ramp codes land in the render target unblended.
2. A code pass writes each part's translation (R) and surface class (G) into a second target (`SE_BLEND_MaskedDistanceField`, exact values).

**The lookups** (`tools/sprites/luts.py`):
- `T_SprClass` gives a colour's position on each ramp.
- `T_SprXlat` gives the translated colour of every palette index under each of the 256 translations (`xlat.c`).

**Colour options:**
- `SetColours` / `FMRSpriteAppearance` take the creator's choices: skin 0–3, hair 0–13, shirt and pants 0–10 (`FMRSpriteColours`, `EncodeTwoColorXLAT`).
- Any other xlat id works too.
- An AI part is grey (ramp 3) and colours the same way.

**Surface classes** (`data/sprites/materials.json`):
- Each part has a class (cloth, skin, hair, leather, metal) with its own roughness, metallic and specular.
- Parts get their class by role (weapons are metal) or by bgf. List armour bgfs there to make them shiny.
- Per-texel masks (an armour with cloth parts) would add a third atlas channel later.

**Edge texels (fixed 2026-10-07):** the canvas samples the atlas with filtering, so a part's edge texels land in the render target with a blended alpha, not an exact ramp code. Just over the 0.5 clip, that rounds to "no ramp" and the texel showed untranslated: a thin red line round the arms, the torso and the belt (`ReferenceImages/sprites/lighting-examples/player-arm-seems.png`). `M_SpriteBody` now gives such a texel the colour and ramp of the nearest texel of the same part with an exact code (±6, to allow for BC7 noise inside parts). Where the colour filter's 2×2 footprint mixes two parts or two ramps, it uses the exact texel's colour. Sheet: `build/lookdev/seam_zoom.png`.

## Lighting

Lit sprites (the default) take the world's lights, sun, moods, torches and future spell colours. Tuning (console variables, also material parameters):
- **`mr.Sprite.Albedo` 0.64:** sunlit sprites read too bright at 1. It was 0.75 until 2026-10-07, then 15 % less at the user's request (indoors and out).
- **`mr.Sprite.SunFace` 0.6 and `mr.Sprite.NormalUp` 0.4:** the shading normal faces the sun's side and leans up, so brightness doesn't swing as the camera orbits. A sprite has no real 3D shape, and a camera-facing normal made it bright in some views and dull and blue against the sun.
- **`mr.Sprite.Ambient` 0.51:** indoors, the share of the environment's ambient floor (`MPC_Environment.SectorAmbient × AmbientTint`, 0 outdoors) added as emissive, as the zone materials do. Without it, sprites in the Inn were nearly black. It was 0.6 until 2026-10-07, then 15 % less with the albedo.
- Players, NPCs and monsters share these values, so an NPC is lit like a player standing in the same place.
- Look-dev: cameras `sprite_inn_bar`, `sprite_inn_table`, `sprite_square` and `sprite_close` in `lookdev_cameras.json` stand the player in view (`pawn_cm`, `pawn_yaw`). Run `run_lookdev.ps1 -Label x -GameHour 14 -Only sprite_inn_bar,sprite_square,sprite_close`.

## Pipeline

| Step | Command | Output |
|---|---|---|
| Preview (no AI) | `python tools/sprites/composite.py --look test_male [--action walk]` | `build/sprites/preview/<look>/` |
| Group catalogue | `python tools/sprites/composite.py --catalog bta bra bla bfa` | `build/sprites/catalog/` |
| New hair (AI, paid: 6 images) | `build/texai/.venv/Scripts/python tools/sprites/new_hair.py --name ai_hair_02 --describe "..."` | `build/sprites/custom/<name>/` (a part like any bgf) |
| Upscale, in-betweens, atlases, lookups | `build/texai/.venv/Scripts/python tools/sprites/build_player_sprites.py` | `build/sprites/atlas/`, `build/sprites/lut/`, `data/sprites/player_parts.json` |
| Import into UE | `powershell -File tools/ue/import_sprites.ps1` | `/Game/Generated/Sprites` (atlases, lookups, `M_SpriteBody`, `M_SpriteBodyUnlit`) |
| Visual tour / crowd / monsters | `powershell -File tools/sprites/run_sprite_tour.ps1 -Label x [-Crowd \| -Monsters] [-Look l] [-Zone 301] [-Hour h] [-Cvars "..."]` | `build/sprites/tour/<label>/` |
| Smoothing clips | `powershell -File tools/sprites/run_sprite_clips.ps1 -Clip walk\|dance\|wave\|weapon_attack [-Look l] [-Variants a,b] [-View 0]` (`-View`: 90 the side, 0 from behind; variants `old_attack`, `new_attack` compare the one-shot changes) | `build/sprites/clips/<clip>.gif` |
| Multiplayer check | `powershell -File tools/ue/run_sprite_net_test.ps1` | PASS / FAIL |

Notes:
- Test looks are in `data/sprites/looks.json`.
- `tools/setup.ps1` runs the atlas build and import when the AI venv exists.
- A look whose AI part isn't on this machine is skipped.
- `bgf2png.py`, the upscaler and `new_hair.py` find `Server-104/`, `build/texai/` and `tools/aigen/` in a parent folder, so they work from a git worktree.

## Runtime (C++)

**`FMRSpriteLibrary`** (`Character/MRSpriteData.*`):
- Loads the JSON.
- Ports `GetObjectPdib` (`ViewSlot`, `RelativeAngle`), the placement (`Place`, with bitmap overrides for in-betweens) and `AnimateSingle` (`FMRSpriteTrack`).
- `FMRSpriteColours` holds the creator's colours.

**`UMRSpriteBodyComponent`** (`Character/MRSpriteBodyComponent.*`), on `AMRCharacter`, client only. Each tick it:
1. Picks stand or walk.
2. Advances the tracks.
3. Computes the angle to this machine's camera.
4. Composes in-betweens into a list of bitmaps.
5. When that list (or a colour) changed, redraws the colour and code render targets.
6. Places the quad parallel to the screen.

**`AMRCharacter`** replicates `SpriteAppearance` and `SpriteAction`. The owner predicts both; the server validates them (known looks, colour ranges, height 90–110 %); the others apply them in `OnRep`. A late joiner skips one-shot actions that are long over.

**`AMRHUD`** draws the first-person hand or weapon.

**Tests:**
- Automation: `Meridian.Sprites.Angles|Tracks|Placement|Tweens|Colours|Monsters`.
- Network: `run_sprite_net_test.ps1`.
- In game: `-MRScreenshots` (8-angle orbit, `view_behind`, `view_front`), `-MRProfile` (a crowd with random looks, colours and heights), `-MRSpriteClip=<clip>`.

**Console variables:**

| Variable | Default | What it does |
|---|---|---|
| `mr.Camera.ThirdPersonPitch` | 20 | How far third-person cameras tilt from eye level |
| `mr.Sprite.Billboard` | 1 | Share of the camera's pitch the quad follows (1 = always parallel to the screen) |
| `mr.Sprite.Shadow` | 1 | The sun-facing shadow card |
| `mr.Sprite.Unlit` | 0 | 1 = the unlit material |
| `mr.Sprite.Albedo` | 0.64 | Lighting (see "Lighting") |
| `mr.Sprite.SunFace` | 0.6 | Lighting |
| `mr.Sprite.NormalUp` | 0.4 | Lighting |
| `mr.Sprite.Ambient` | 0.51 | Lighting |
| `mr.Sprite.BackArmsUnder` | 1 | Seen from behind, arms under the torso (0 = the original's layering) |
| `mr.Sprite.TexelsPerPixel` | 4 | Render target texels per torso pixel |
| `mr.Sprite.WalkRefSpeed` | -1 | Ground speed for the original walk rate; -1 = the player's run speed; 0 = always the original rate |
| `mr.Sprite.UV` | `1 0 1` | `SwapUV FlipU FlipV` for the engine plane |
| `mr.Sprite.HandScale` | 1 | First-person hand size |
| `mr.Sprite.HandBob` | 1 | First-person hand walk bob |
| `mr.Sprite.Smooth.Tweens` | 1 | Show the in-betweens |
| `mr.Sprite.Smooth.OnceWindow` | 0.35 | One-shot actions: in-betweens only in this last share of each pose |
| `mr.Sprite.Smooth.Crossfade` | 0 | Per-part crossfade (built, off) |
| `mr.Sprite.Smooth.AngleFade` | 0 | Fade between views (built, off) |
| `mr.Sprite.Smooth.Motion` | 0 | Procedural motion (built, off) |

**Test keys** (sprite mode):
- LMB attack
- F5 wave, F6 point, F7 dance (again to stop), F9 cast (1-9 select the hotbar: docs/adr/0009-user-interface.md)
- L next look
- V view, P photo

Start options for your own character (sent to the server): `-MRSpriteLook=<name>`, `-MRSpriteHeight=<scale>`, `-MRSpriteColours=skin,hair,shirt,pants`.

## Monsters

Monsters and NPCs are drawn by the same `UMRSpriteBodyComponent` as players. A monster look is one part (`body`) from one bgf, with no overlays and no colour translation.

**Data** (`tools/sprites/monsters.py`, run by `build_player_sprites.py`):
- Reads `data/monsters.json` and each class's Kod file.
- Actions come from the Kod handlers, the way the original server sent them:
  - `SendMoveAnimation` gives the walk cycle.
  - `SendAnimation` with `ANIM_ATTACK` gives the one-shot attack, ending on group 1.
  - Stand is group 1.
  - For example, the giant rat walks 2–6 at 75 ms and attacks 7–11 at 100 ms. The mummy walks 2–5 and attacks 6–9, both at 200 ms.
- Also read from Kod:
  - the corpse bgf (`m_<Class>_dead`)
  - speed (`viSpeed` × 450/55 cm/s in `player_parts.json`, scaled in game to the player's run: × run / 450, and by `mr.Monster.SpeedScale`)
  - vision (`viVisionDistance` squares)
  - `AI_FIGHT_AGGRESSIVE` (mummies), NPC / no-move flags
  - the aware, hit, miss and death sounds
- Results go to the `monsters` table and the `m_*` looks in `player_parts.json`.
- Monster atlases are upscaled 2× (not 4×) and capped at 4096², because big monsters (centipede) have many large frames. The upscaler pass is the same.
- A monster look with only its own actions doesn't borrow the player's: a cow has no attack, and nothing dances.

**Runtime:**
- **`AMRMonster`** (`Monsters/MRMonster.*`) is an `ACharacter`, server-authoritative.
  - It replicates `Look`, `MonsterClass`, `Action` (`FMRSpriteActionState`, as players do) and `bDead`.
  - The capsule comes from the living look's bounds.
  - Monsters walk at the original fixed frame rate, not scaled by speed.
  - A dedicated server creates no sprite body.
- **The AI is a stand-in until combat exists:**
  - NPCs and stationary classes stand still.
  - Monsters idle and wander within `mr.Monster.WanderRadius` of their spawn.
  - Aggressive ones (and any monster that was hit) chase a player they can see (vision distance plus line of sight), play "aware", and attack in reach with the attack animation and sound. There's no damage yet.
  - They give up past `mr.Monster.Leash`.
- **Placeholder hits:** a player's fist or weapon attack hits the nearest monster within 2.6 m in front. Three hits kill it. The corpse look stays for `mr.Monster.CorpseSeconds`, and its atlas is loaded at spawn so it doesn't pop in late.
- **`UMRMonsterSubsystem`** (`Monsters/MRMonsterSubsystem.*`), server:
  - **NPCs:** placed from `zone_layout.json` objects, at their Kod spots and facings.
  - **Monster rooms:** spawned from `zones.json` `spawning`: weighted classes, `init_count_min..max` at start, then one more every `gen_time_ms` while under the maximum.
  - **Placement:** NPCs stand exactly at their Kod spots. Until 2026-10-07 every NPC was moved 40–80 cm, because its own new capsule failed the spawn's overlap test; that put Marcus on top of the Inn's bar. The capsule's collision is now off until it is placed. Monsters spawn at the room's generator points, or at random floor points in the grid. Random points use the lowest floor with headroom (`UMRZoneSubsystem::TraceFloor(P, true)`): the original rooms are 2.5D, so a surface with a floor under it is a roof we added.
  - In the demo zones: 7 Raza NPCs and about 22 monsters across the Mausoleum (306), the Outskirts (330) and the Forest edge (331).
  - `mr.Monster.Spawn 0` turns spawning off; `MRMonsterReset` respawns everything.

**Checks:**
- `run_sprite_tour.ps1 -Label x -Monsters` (the `-MRMonsters` tour):
  - every class in a row, NPCs behind, AI off
  - stand, walk, attack, side, side attack and dead shots
  - then a chase: an aggressive mummy and a provoked bunny with AI on, logged with distance and attacks
- `run_sprite_tour.ps1 -Label x -Monsters -Zone 306|330|331`: moves next to four monsters the spawner placed and photographs them, logging the spawn heights.
- Automation: `Meridian.Sprites.Monsters` (every class has its look and corpse; the rat's Kod animations; the cow has no attack; mummies are aggressive).
- `run_sprite_net_test.ps1` also checks that client B receives the server's monsters with their looks.

**Fixed along the way:** a sprite whose atlas wasn't ready on its first draw stayed blank. In an editor build a just-loaded texture shows a placeholder while it compiles. Such a draw is no longer cached, so it is retried.

| Variable | Default | What it does |
|---|---|---|
| `mr.Monster.Spawn` | 1 | Populate zones with monsters and NPCs when the world starts |
| `mr.Monster.SpeedScale` | 1 | Monster speed relative to the original |
| `mr.Monster.WanderRadius` | 600 | How far monsters wander from their spawn (cm) |
| `mr.Monster.Leash` | 2500 | How far from its spawn a monster chases (cm) |
| `mr.Monster.CorpseSeconds` | 20 | How long a corpse stays |

## Open

1. **Atlas size:** the 4× upscale plus 3 in-betweens per step is generous. 2× for the torso and limbs would roughly halve it.
2. **Steps with no in-betweens:** big pose changes keep the original snap (user: fine for now). RIFE or ToonCrafter later, which needs a model download.
3. **Distance aliasing:** render-target mips aren't regenerated after canvas draws. Watch for it; sizing the target to the sprite's screen height would fix it.
4. **Memory per character:** two render targets of about 2.7 MB each at 4 texels per pixel. Lower `mr.Sprite.TexelsPerPixel` for distant characters if crowds grow.
5. **Lighting polish:** check the new defaults across moods and interiors; per-texel surface masks for mixed armour.
6. **Monsters:**
   - Real combat (damage, health, the original's to-hit) replaces the placeholder hits and the stand-in AI.
   - Monsters in other zones need their spawn tables, once those zones are built.
   - Monster colour variants (`xlat` on some Kod classes) aren't read yet.
   - A spawn could still land somewhere a player can't reach, such as a closed courtyard. The original used the room's movement grid, which we don't have yet.
7. **Next:** the creator UI on `FMRSpriteAppearance`.
