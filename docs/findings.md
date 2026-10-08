# Findings from the original data (Phase 1)

## Scale and coordinates
- **Units.** ROO geometry uses 1024 units per grid square. Kod positions and sector heights use 64 per square, which is ×16 to ROO units.
- **Grid to ROO.** `X = (col-1)*1024 + fine_col*16` and `Y = (row-1)*1024 + fine_row*16`. Rows grow southward (`blakserv/roofile.h` `GRIDCOORDTOROO`). The server adds no offset.
- **Angles.** Kod angles run from 0 to 4096. 0 is east, and the value increases toward south, the same as `atan2(y, x)` in ROO space.
- **Real-world scale: 1 square = 2.2 m.** The original eye height is 0.75 of a square (`clientd3d/game.c`), so a 1.65 m eye gives 2.2 m per square.
  - The original max step height is 24 Kod units ≈ 0.8 m. The remaster will use a normal ~45 cm step and ramps instead.
  - Resulting sizes: Raza town's walled area is roughly 145 × 76 m; the Inn is 23 × 18 m.
- **Texture size.** One texel = 1/shrink Kod units (`d3drender.c` wall UVs), so a texture covers `pixels / shrink / 64` squares. Wall textures are stored transposed (`makebgf -r`).
- **Wall texture placement** (`D3DRenderWallExtract`, mirrored in `roo2gltf.wall_uvs`):
  - Each side measures horizontally from its own start vertex (x0 for the pos side, x1 for the neg side) plus that side's x offset. `WF_BACKWARDS` mirrors it. BSP-split wall pieces carry offsets that continue the texture across the split.
  - Vertically, normal and below sections are anchored bottom-up (the texture's bottom row on the section's bottom edge) unless `WF_NORMAL_TOPDOWN` / `WF_BELOW_TOPDOWN`. Above sections are top-down unless `WF_ABOVE_BOTTOMUP`. The y offset shifts the texture from there, and sloped edges anchor to the enclosing whole grid square.
  - Wall x/y offsets and sector offsets are signed 16-bit Kod fine units (16 ROO units).
  - `WF_NO_VTILE` textures are drawn once and clipped where the texture runs out. Raza's iron fences use a y offset of 60 so only the spiked top shows, as railings about 0.7 m tall.
- **Floor and ceiling textures tile once per grid square** (2.2 m) whatever their pixel size, shifted by the sector's texture offset (`D3DRenderFloorExtract`). The 512 px Raza path textures are therefore denser than their shrink value suggests.

## Walking: passable walls, wading and objects
- **Passable walls.** A wall side blocks unless it has `WF_PASSABLE` (`blakserv/roofile.c` `BSPCanMoveInRoomTreeInternal`, `clientd3d/move.c`). Raza's field borders ("Field 1/2/3 Tops", `grd08897`, `grd08902`, `grd08905`), its hanging shop signs and the interiors' wall torches are passable middle textures. Six "Field 1 Tops" sides in the north of the town are not.
  - Farol West (331) has 132 passable "Forest Deep 3" (`grd01502`) tree walls.
  - `roo2gltf` writes `<rid>_<class>_collision.glb` without the passable middle sections, and the world build uses it as the collision (2026-10-07). Before that every middle texture blocked, so the wheat fields were walls.
- **Wading.** A sector's `SF_MASK_DEPTH` (0-3) slows movement to 1, 3/4, 1/2 and 1/4 speed (`clientd3d/move.c` `UserMovePlayer`). Players and objects stand at the floor minus 0, 0.2, 0.4 or 0.6 of a square (0, 0.44, 0.88, 1.32 m; `draw3d.c` `sector_depths`, `object.c`, `blakserv/roofile.c` `DEPTHMODIFY`).
  - Raza's fields are depth 1 and 2 ("Field 1/2/3 Floor"); the Crypt's pool is depth 1.
  - **The fields are raised blocks**: their floor is 1.20 m against the grass's 0.34 m, edged with "Field Bottom" walls. Sinking 0.88 m puts you back at ground level with the wheat at your waist. A field without depth (the one north of the town, `grd08898`, depth 0) stays a block you can't enter.
  - `roo2gltf`: the collision blockout's floors are at the wading height (`SectorHeights(wading=True)`), and so are the positions in `data/zone_layout.json`. The render keeps the raised wheat. `depth_areas` in the layout drive `UMRCharacterMovementComponent`'s slowdown.
  - The smoke test's step "into the wheat field (11,62)" checks the depth, the feet at the wading floor and the halved speed.
- **Doors and gaits.** Tile exits (doors) are taken with "go": the space bar sends `BP_REQ_GO` and `room.kod` `SomethingTryGo` takes the exit on the player's square, or says it's locked (Raza's tutorial: "approach doors and press space bar"). Edge exits are walked through (`room.kod` edge handling). The original has two speeds, walk and run (`move.c` `A_FORWARDFAST`: twice the distance), and no jumping or crouching. The remaster runs by default and walks with Shift held (2026-10-07).
- **Speed, falling and steps** (`clientd3d/move.c`, `moveobj.c`, `move.h`, `game.c`; at 2.2 m a square):

  | | Original | Metres |
  |---|---|---|
  | Walk | `MOVEUNITS` (1/4 square) per `MOVE_DELAY` (85 ms) | 6.47 m/s |
  | Run | twice that ("wolfpack" enchantment: 65 ms) | 12.94 m/s |
  | Gravity | `GRAVITY_ACCELERATION` 5 squares/s/s | 11.0 m/s/s |
  | Fall start | `FALL_VELOCITY_0` 2/3 square/s down | 1.47 m/s |
  | Step | `MAX_STEP_HEIGHT` 24 Kod units, measured from max(floor, your current height), so also while falling | 0.825 m |
  | Wall distance | `min_distance` = `player.width` / 2 (31 x 64 / 4 / 2 ROO) | 0.53 m |

  - Horizontal movement ignores falling: you keep full speed and steering in the air. So the original's "jumps" are running off a ledge and stepping onto the far side before you've dropped 0.825 m: up to about 4.1 m of gap running (3.58 m of flight plus the wall distance), half that walking.
  - The server's speed is "big squares per 10 s" (`user.kod`: walk 25, run 55); its speed-hack bucket is commented out. Running (speed over 25) costs vigor on the server.
  - The remaster (2026-10-07) moves the same way: `UMRCharacterMovementComponent` (the speeds times `mr.Move.SpeedScale`, 1 = the original; gravity; the step, also while falling; full air control). Other players, monsters, the walk cycle and the hand bob follow `mr.Move.SpeedScale`. Before, it ran at 4.5 m/s with a 0.45 m step and couldn't cross a gap; the Mausoleum has 73 walls with 0.45-0.825 m steps.
  - `tools/ue/run_move_test.ps1` (`-MRMoveTest`) checks it in play: the speeds, a 0.72 m Mausoleum step, and ledge jumps across 2.5, 3.9, 4.5 and 5 m (running lands up to 3.9 m; walking 2.5 m and running 4.5 m fall in).
- **Hanging objects.** Objects with `OF_HANGING` (the `Chandelier`; OrnamentalObjects with `pbHanging`: flowers, garlic, herbs, stalactites, none in the demo) hang from the ceiling: z = ceiling - the sprite's height (`clientd3d/object.c` `RoomObjectSetHeight`). `roo2gltf` writes each indoor object's `ceiling_y` into `data/zone_layout.json`; props.json `"hanging"` hangs the mesh's top there.
- **Objects never block** in the original. props.json `"blocks"` makes a class solid in the remaster (lamps, braziers, tables, chests; sign posts and tree trunks as hidden cylinders; ADR 0007).

## Raza town and the Outskirts are one map
- `raza.roo` (RID 300) and `razaforest.roo` (RID 330) hold the **same geometry**. The forest file's coordinates are the town's plus (2208, 42592) ROO units, which is about (4.7 m, 91.5 m).
- The Kod object placements in `razaforest.kod` are the town's objects shifted by the same amount.
- In the original, both zones render the town and the forest. Only the grid origin and the spawn table change.
- **Remaster consequence:** build one outdoor level for the town plus the Outskirts. Treat the north gate as a *logical* zone boundary, where the no-combat flag, music and spawns change, instead of a teleport. This gives the seamless transition we want for free.
- `roo2gltf` detects this automatically and records it as `shares_geometry_with` in `data/zone_layout.json`.

## Raza on Server 104
- Raza is an open mainland town, not the old sealed newbie island.
- The town (300) is `ROOM_NO_COMBAT`.
- The Inn (301) is the hometown, a sanctuary and a safe-logoff room.
- The Outskirts (330) spawn Bunny (weight 60) and SpiderBaby (weight 40), with a maximum of 15 monsters.
- The Mausoleum (306) spawns Mummy and MummyNoTreasure at random generator points, with a maximum of 35.
- Western Farol (331) also spawns GiantRat and Centipede. It leads east to RID 536 and west to RID 526, both outside the demo.
- The two quick-start signs carry the original movement and map tutorial text, which is in `data/zones.json`.

## Asset coverage
- **Sources, searched in order:** the Server 104 client, then the Steam classic client, then the repo's `resource/`.
- **Textures:** all 150 textures the demo rooms use were extracted.
- **Sprites:** all 6 creatures and all 7 NPCs. Raza uses Server 104's high-resolution `bunny2.bgf`, not the classic bunny.
- **Sprite layout:** a sprite group is one animation frame. Its indices are the view angles: 8 for most creatures, 6 for the mummy. That gives front, side and back reference images for Meshy.

## Kod data quality notes
- 248 spells extracted. `SID_TRANCE` and `SID_DEMENT` have constants but no implementation.
- 26 skills extracted. The strokes (Slash, Thrust, Fire) are `Stroke` subclasses.
- Kod is case-insensitive (`RazaInnkeeper` vs `RazaInnKeeper`, `viSkill_Num` vs `viSkill_num`), so the parser resolves every name case-insensitively.
- Monster HP comes from `viLevel` with a random fuzz plus `piSurvivalLevel * viDifficulty` (`monster.kod` `Constructor`).
