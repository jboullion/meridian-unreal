# The .roo room format (version 15)

These are our own notes on the original room files, written from reading `ReferenceServers/Server-104/clientd3d/bspload.c` (`LoadRoomFile`, `LoadNodes`, `LoadWalls`, `LoadSidedefs`, `LoadSectors`, `LoadSlopeInfo`, `LoadThings`) and `clientd3d/bsp.h`. The C++ reader is `World/MRRooFile` ([ADR 0012](../adr/0012-client-parity-and-world-coverage.md)); `tools/roo2gltf` reads the same files in Python.

Never copy or translate code from Server-104 or Meridian Shards (both GPL). Write the facts here and implement from this page.

## Units
- Positions are **ROO units**: 1024 per grid square. X grows east, Y grows south.
- Heights and texture offsets are **Kod fine units**: 64 per square, so ×16 for ROO units.
- Angles in slope records are the client's 0..4095 (0 = east).

## Layout
Everything is little-endian. Offsets are from the start of the file.

**Header**
| Field | Type | Notes |
|---|---|---|
| magic | 4 bytes | `52 4F 4F B1` ("ROO" and 0xB1) |
| version | i32 | 15 here; the client refuses older than its `ROO_VERSION` |
| security | u32 | the room's id: `BP_PLAYER` sends it; compare the low 28 bits ([blakserv-protocol.md](blakserv-protocol.md)) |
| main info | i32 | offset of the main info |
| server info | i32 | offset of the server's own data (the client never reads it) |

**Main info** (at that offset)
| Field | Type |
|---|---|
| width, height | i32, i32: the Kod grid rectangle in fine units ×16, i.e. ROO units (`roo2gltf` `grid_size_roo`) |
| nodes, client walls, server walls, sidedefs, sectors, things | 6 × i32 offsets |

The server walls (third offset) are the server's movement data; the client skips them.

**Nodes** (at the nodes offset): `u16 count`, then each node:
- `u8 type`: 1 internal, 2 leaf.
- `f32 x0, y0, x1, y1`: the bounding box.
- Internal: `f32 a, b, c` (the separator line `a·x + b·y + c = 0`), `u16 positive child`, `u16 negative child`, `u16 first wall`. Children and walls are 1-based; 0 is none.
- Leaf: `u16 sector` (1-based), `u16 n`, n × (`f32 x`, `f32 y`): a convex polygon.

Node 1 is the root. Every leaf is one convex piece of floor and ceiling; walking the array gives the same leaves as walking the tree.

**Client walls** (at their offset): `u16 count`, then each wall:
- `u16 next` (the next wall in the node's list);
- `u16 positive sidedef`, `u16 negative sidedef` (1-based, 0 = none);
- `f32 x0, y0, x1, y1`, `f32 length`;
- `i16 positive x offset`, `i16 negative x offset`, `i16 positive y offset`, `i16 negative y offset` (fine units);
- `u16 positive sector`, `u16 negative sector` (1-based, 0 = none).

The positive side is the side the separator's normal points to. A wall can be listed more than once (split by the BSP); `roo2gltf` skips repeats with the same ends and sectors.

**Sidedefs:** `u16 count`, then each:
- `u16 server id`;
- `u16 normal texture`, `u16 above texture`, `u16 below texture` (the `grdNNNNN.bgf` number, 0 = none);
- `u32 flags` (`WF_*`, below);
- `u8 animation speed` (0 = none; else the texture's frames cycle).

**Sectors:** `u16 count`, then each:
- `u16 server id`;
- `u16 floor texture`, `u16 ceiling texture` (a ceiling of 0 is open sky);
- `u16 texture x`, `u16 texture y`: the floor and ceiling texture origin (fine units, signed);
- `i16 floor height`, `i16 ceiling height` (fine units);
- `u8 light` (0..255);
- `u32 flags` (`SF_*`, below);
- `u8 animation speed` (version 10 and later);
- with `SF_SLOPED_FLOOR`, a slope record; then with `SF_SLOPED_CEILING`, another.

**A slope record** (46 bytes): `f32 a, b, c, d` (the plane `a·x + b·y + c·z + d = 0` in ROO units, so `z = -(a·x + b·y + d) / c`), `i32 texture origin x, y`, `i32 texture angle`, 18 unused bytes.

**Things:** `u16 count` (2), then 2 × (`i32 x`, `i32 y`): two corners of the room's box in fine units, as the room editor shows them (Y up). The client uses them only to count rows and columns.

## Flags (`clientd3d/bsp.h`)
| Wall (`WF_*`) | | Sector (`SF_*`) | |
|---|---|---|---|
| 0x001 | backwards (mirror the texture) | 0x003 | wading depth 0..3 |
| 0x002 | the normal texture has transparency | 0x00C | scroll speed (0 none, 1 slow, 2 medium, 3 fast) |
| | | 0x070 | scroll direction (0 N, 1 NE ... 7 NW, clockwise) |
| 0x004 | passable (walk through it) | 0x080 | scroll the floor |
| 0x008, 0x010 | never / always on the map | 0x100 | scroll the ceiling |
| 0x020 | can't be seen through | 0x200 | flicker the light |
| 0x040 | above texture bottom-up | 0x400 | sloped floor |
| 0x080 | below texture top-down | 0x800 | sloped ceiling |
| 0x100 | normal texture top-down | | |
| 0x200 | don't tile the normal texture vertically | | |

Walls scroll too: sidedef flags `0xC00` are the speed and `0x7000` the direction (`WallScrollSpeed`, `WallScrollDirection`).

## Drawing it (what `roo2gltf` and `World/MRRoomMesh` do)
- **Floors and ceilings:** one polygon per leaf, at its sector's heights (or slope planes). Their textures tile **once per grid square**, whatever their size, shifted by the sector's texture origin.
- **Walls:** Doom-style. A one-sided wall is its sector's floor to ceiling. A two-sided wall has a lower section (the lower floor's side sees it, the "below" texture), an upper section (skipped between two open-sky sectors, the "above" texture) and, if a normal texture is set, a middle section between the higher floor and the lower ceiling.
- **Wall texture size** comes from the BGF: one texel is `1/shrink` fine units ([bgf-format.md](bgf-format.md)). The UV rules (offsets, top-down and bottom-up anchoring, no vertical tiling) are `roo2gltf.py wall_uvs`.
- **Light:** each face carries its sector's light, as vertex colour.
- **Wading:** in a sector with depth 1..3, things stand 1/5, 2/5 or 3/5 of a square below its floor (`DEPTH_SINK_ROO`).
