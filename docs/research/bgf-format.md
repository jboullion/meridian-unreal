# The .bgf bitmap format (version 10)

These are our own notes on the original bitmap files (sprites, textures, sky): what `tools/bgf2png/bgf2png.py` decodes (written from `makebgf/writebgf.c` and `clientd3d/dibutil.c`). The C++ reader is `World/MRBgf` ([ADR 0012](../adr/0012-client-parity-and-world-coverage.md)).

## Layout
Everything is little-endian.

| Field | Type |
|---|---|
| magic | `BGF` and 0x11 |
| version | i32 (10) |
| name | 32 bytes, NUL-padded |
| bitmaps, groups, max indices, shrink | 4 × i32 |

Then each bitmap:
- `i32 width`, `i32 height`, `i32 x offset`, `i32 y offset`;
- `u8 n` hotspots, n × (`i8 number`, `i32 x`, `i32 y`): where overlays attach. A negative number marks an underlay;
- `u8 compressed`, `i32 length`, then the pixels: `length` zlib bytes if compressed, else `width × height` raw bytes. One byte per pixel, row by row.

Then each group: `i32 n`, n × `i32` bitmap index.

## Pixels
- A pixel is an index into the game's 256-colour palette. **Index 254 is transparent.**
- The palette isn't in any file the server serves: the original client compiles it in (`clientd3d/palette.c`, `base_palette`). `tools/bgf2png/bgf2png.py --palette` writes ours, from `ReferenceServers/Server-104/blakston.pal`, to `data/runtime/palette.bin` (768 bytes, RGB; git-ignored, `tools/setup.ps1` makes it).
- Palette translations (skin, hair, clothes colours) remap indices before the palette; `docs/sprites.md` covers them.

## Size in the world
- One texel is `1/shrink` Kod fine units (64 per grid square). A 64-pixel texture with shrink 1 is one square wide.
- **Wall and floor textures (`grdNNNNN.bgf`) are stored transposed** (`makebgf -r`): the image to use is the transpose of the stored one, so its width is the stored height.
- Floors and ceilings ignore the size and tile once per square ([roo-format.md](roo-format.md)).

## Groups
- **Textures:** a texture with several bitmaps animates (an animated sidedef or sector cycles them; Kod's `AnimateWall` picks one, e.g. the Raza clock face shows the hour).
- **Sprites:** a group is one pose; its indices are the views around the object (usually 8 slots, 6 unique bitmaps). `docs/sprites.md` "Angles" has how the slot is picked from the viewing angle.
