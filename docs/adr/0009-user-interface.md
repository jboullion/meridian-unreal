# ADR 0009: The in-game UI

- Status: **Accepted 2026-10-07** (first pass, on mock data).
- Date: 2026-10-07
- References: `ReferenceImages/ui/` (the original and Ogre clients, Minecraft's inventory and HUD, a Skyrim inventory mod).

## Context
The client drew no UI apart from the first-person hands (`AMRHUD`). We want a modern, polished UI that plays like Minecraft but looks like the original client: its stone background, iron frames and art.

## Decision

### Layout and behaviour (Minecraft's, with Meridian's art)
- **Item hotbar**, bottom centre: nine *real storage* slots, as in Minecraft. Only items go there, no spells. Keys 1–9 and the mouse wheel select a slot. The selected slot is the right hand (the "Wielding" item in the dialog). The selected item's name shows above the bar for a moment after it changes.
- **Vitals**: Health (left) and Mana (right) above the hotbar, where Minecraft has hearts and food. Vigor (the original's stamina) is a thin bar under them, where Minecraft has experience. These are the live GAS attributes from `UMRAttributeSet`, not mock data. A loss leaves a lighter trail and flashes.
- **Spell bar**, bottom right: nine spell shortcuts in the numpad's layout (7 8 9 on top), cast with numpad 1–9. It sits at 45% opacity and becomes fully opaque on hover, while the dialog is open, or for a moment after a cast. `spellbar_row` in `ui_style.json` lays it out as a row instead.
- **Inventory dialog** (E or I; Esc closes it). One stone window with the original's five tab buttons:
  - **Inventory**: equipment slots around the avatar (Head, Amulet, Torso, Hands, Legs; Ring 1, Ring 2, Left hand, Right hand), weight and bulk, the scrollable bag, and the hotbar row.
    - Hands (gauntlets) was added because the original has `ITEM_USE_GAUNTLET`.
    - The bag grows a row as it fills: the original limits by weight and bulk, not by slots.
  - **Spells**: known spells by school.
  - **Skills**: skills with their percentages.
  - **Stats**: the six stats and the three pools.
  - **Quests**: an empty state for now.
- **Interactions** are Minecraft's:
  - left click picks up, places, merges or swaps a stack
  - right click picks up half, or places one
  - shift-click moves an item between the bag and the hotbar, equips it, or unequips it
  - a number key over a slot swaps it with that hotbar slot
  - clicking outside the window drops the carried stack
  - plain drag and drop also works: release over another slot
  - tooltips show name, damage or defence, weight, bulk, value and description
  - spells are copied from the spell book onto the spell bar
- **Minimap**, top right: a real top-down picture of the zone, north up, with the player as an arrow turned to the view's heading. It sits in the original map's metal rim on its parchment, with the zone name and Meridian time below. `-` and `=` zoom.
- **Input**: the UI keys live in `IMC_UI`, built in code in `AMRPlayerController` (like the character's `IMC_Default`).
  - Camera zoom moved to Ctrl + wheel; the wheel alone selects the hotbar slot.
  - The test emotes moved from 1–4 to F5, F6, F7 and F9. F8 is PIE's eject key.

### How it's built
- **Slate in C++** (`Source/.../UI/`), no widget blueprints. The plan said "C++ UMG"; UMG is a wrapper over Slate, and laying out widgets in code is plainer in Slate. Everything stays text an agent can edit, like the input setup and the levels.
- **The seam: `UMRInventorySource`.**
  - Minecraft's rules are implemented on top of `GetRaw`/`SetRaw`.
  - `UMRMockInventory` stores arrays, seeded from `data/ui/mock_inventory.json`.
  - A server-owned inventory will override the interactions (`Click`, `QuickMove`, `SwapWithHotbar`, `DropCursor`) to send them to the server; the widgets won't change.
- **Art**: the original interface bitmaps (`module/merintr/bitmap`, `clientd3d/bitmap` in the Server-104 checkout) and the item, spell and skill icons (`vrIcon` bgfs). Built by `tools/ui/build_ui_art.py` and `build_icons.py` into `build/ui/` (git-ignored), and imported by `tools/ue/import_ui.ps1` into `/Game/Generated/UI`.
  - A frame is drawn the way the original draws it (`drawint.c`): eight corner strips plus four tiled repeaters (`MRPaint::Frame`).
  - The slot is the original inventory cursor turned half round and darkened, so it looks sunk. The selection frame uses the stat bars' gold.
- **Look data**: `data/ui/ui_style.json` holds sizes in original pixels, `ui_scale`, colours, opacities and the art variant. `MRUIReload` re-reads it.
- **Avatar**: `AMRAvatarPreview` is a client-only actor far below the world. It has its own sprite body (the player's look and colours, unlit) and an orthographic scene capture of only that body. Dragging turns it through the original's eight angles.
- **Minimap pictures** (see "Minimap captures" below).

### Choices made from images
- **Art upscale**: `ai_ultrasharp` (4x-UltraSharpV2) for frames and icons. It keeps the stone grain without the nearest-neighbour blockiness. `nearest` and `ai_gtav` stay one switch away (`art_variant`, `icon_variant`). Sheets: `build/ui/review/ui_art_variants.png`, `ui_art_variants_zoom.png`.
- **Minimap style**: `walls`, the picture with the original map's wall lines over it. `photo` and `parchment` stay one switch away (`minimap.json` `style`). Sheet: `build/minimap/review.png`.

## Minimap captures
An orthographic scene capture straight down, run in the game (`-MRMapCapture`, `tools/ue/run_map_capture.ps1`). A headless editor can't open `L_World`.
- One picture per geometry zone covers every zone drawn from that geometry; Raza and the Outskirts share one. `MRMinimap::CaptureRect` (the `bounds_m` footprint, squared, plus a margin) is shared by the capture and the widget.
- **Interiors** (`minimap.json`, per zone):
  - `cut_height_cm` puts the camera inside the room, below the ceiling. An ortho capture's near plane is at the camera, so the ceiling isn't drawn. `bUpdateOrthoPlanes` must be off, or the view moves back and the ceiling returns.
  - `source: basecolor` captures the albedo. Lumen and the zone ambient don't light an ortho view, so the lit floors came out black.
  - `hide_props` leaves out props, fires and smoke (the `ZoneProp`, `ZoneFire` and `ZoneEffect` tags), so the room reads as a floor plan.
- **Outdoors**: the lit picture without shadows (`shadows: false`), with atmosphere and clouds off. The sky's "does not cover that part of the screen" warning was drawn over the empty areas.
- `tools/ui/minimap.py` makes the black empty space outside a zone transparent (only where it touches the picture's edge, so dark water stays), so the parchment shows there. It also draws the walls from `roo2gltf.py --walls-only`.
- Alternatives not taken: the original's hand-drawn wall map only (it's still there in the `walls` and `parchment` styles); a Blender render of the blockout (it lacks the finished art); a global clip plane (it needs `r.AllowGlobalClipPlane`, which costs every frame, and Nanite ignores it).

## Consequences
- Inventory, equipment, the spell bar and skills are **mock data**: local, not saved, not replicated. Spells don't cast; a "cast" plays the sprite's cast action and a cooldown sweep. The vitals and stats are real.
- Equipment doesn't change the avatar yet (no equipment layers on the sprite body).
- Rebindable keys (Enhanced Input user settings, a Controls page) are the next step for "editable in the controller settings". The UI actions are already separate actions in their own context.
- A real font and gamepad navigation are open.
- Visual checks: `tools/ue/run_ui_shots.ps1 -Label <x>` (`-MRUIShots`) writes `build/ui/shots/<x>/sheet.png`.
