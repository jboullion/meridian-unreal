# Findings from the original data (Phase 1)

## Scale and coordinates
- **Units.** ROO geometry uses 1024 units per grid square. Kod positions and sector heights use 64 per square, which is ×16 to ROO units.
- **Grid to ROO.** `X = (col-1)*1024 + fine_col*16` and `Y = (row-1)*1024 + fine_row*16`. Rows grow southward (`blakserv/roofile.h` `GRIDCOORDTOROO`). The server adds no offset.
- **Angles.** Kod angles run from 0 to 4096. 0 is east, and the value increases toward south, the same as `atan2(y, x)` in ROO space.
- **Real-world scale: 1 square = 2.2 m.** The original eye height is 0.75 of a square (`clientd3d/game.c`), so a 1.65 m eye gives 2.2 m per square.
  - The original max step height is 24 Kod units ≈ 0.8 m. The remaster will use a normal ~45 cm step and ramps instead.
  - Resulting sizes: Raza town's walled area is roughly 145 × 76 m; the Inn is 23 × 18 m.
- **Texture size.** One texel = 1/shrink Kod units (`d3drender.c` wall UVs), so a texture covers `pixels / shrink / 64` squares. Wall textures are stored transposed (`makebgf -r`).

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
