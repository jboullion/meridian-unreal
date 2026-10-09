# ADR 0009: The in-game UI

- Status: **Accepted 2026-10-07** (first pass, on mock data).
- Date: 2026-10-07
- References: `ReferenceImages/ui/` (the original and Ogre clients, Minecraft's inventory and HUD, a Skyrim inventory mod).

## Context
The client drew no UI apart from the first-person hands (`AMRHUD`). We want a modern, polished UI that plays like Minecraft but looks like the original client: its stone background, iron frames and art.

## Decision

### Layout and behaviour (Minecraft's, with Meridian's art)
- **Item hotbar**, bottom centre: nine *real storage* slots, as in Minecraft. Only items go there, no spells. Keys 1–9 and the mouse wheel select a slot. The selected slot is the right hand (the "Wielding" item in the dialog). The selected item's name shows above the bar for a moment after it changes.
- **Vitals**: Health (left) and Mana (right) above the hotbar, where Minecraft has hearts and food. Vigor (the original's stamina) is a slimmer bar under them, where Minecraft has experience. All three show their values. Online they are the server's "Condition" stat group (`UMRUISubsystem::GetVital`); offline they are the local GAS attributes. A loss leaves a lighter trail and flashes.
- **Spell bar**, bottom right: nine spell shortcuts in the numpad's layout (7 8 9 on top), cast with numpad 1–9. It sits at 45% opacity and becomes fully opaque on hover, while the dialog is open, or for a moment after a cast. `spellbar_row` in `ui_style.json` lays it out as a row instead.
- **Inventory dialog** (E or I; Esc closes it). One stone window with the original's five tab buttons:
  - **Inventory**: equipment slots around the avatar (Head, Amulet, Torso, Hands, Legs; Ring 1, Ring 2, Left hand, Right hand), weight and bulk, the scrollable bag, and the hotbar row.
    - Hands (gauntlets) was added because the original has `ITEM_USE_GAUNTLET`.
    - The bag grows a row as it fills: the original limits by weight and bulk, not by slots.
  - **Spells**: known spells by school. Online, the list is the server's (its Spells stat group, with each spell's percentage), matched to our data by name for school, level, mana and description.
  - **Skills**: skills by school with their percentages. Online, the server's Skills group.
  - Both have a **search bar** at the top that filters by name or school as you type (Escape clears it). While it has focus every key goes to it. Each **school is a section**: a divider, an arrow, its name and a count; a click folds it. A search shows every match, unfolded. Each row is **one hover region** with one tooltip for the icon and the text. Clicking the text acts on the slot (pick a spell up, shift-click it onto the bar).
  - **Stats**: whatever the server sends (its Stats group), not a fixed list. Servers could send different stats. `data/ui/stat_layout.json` sorts them into sections per ruleset (`data/net/servers.json` `ruleset`), matching the server's names, with `*` as a wildcard.
    - Server 104: Points to spend (Unbound Energy, Training Pts), boxed apart at the top; Attributes (the six stats); Character (Karma, Bulk Carried, Weight Carried, Offense, Defense, Armor); Resistances (everything else).
    - Health, mana and vigor are left out: the HUD shows them.
    - Sections fold like the schools. Offline, `mock_inventory.json` `stats` stands in for the server's list.
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
- **Art upscale** (chosen by the maintainer, 2026-10-07): `ai_gtav` (4xTextures_GTAV_rgt-s_dither) for frames and icons. It's closest to the original. `ai_ultrasharp` and `nearest` stay one switch away (`art_variant`, `icon_variant`). Sheets: `build/ui/review/ui_art_variants.png`, `ui_art_variants_zoom.png`.
- **Minimap style** (chosen by the maintainer, 2026-10-07): `walls`, the picture with the original map's wall lines over it. `photo` and `parchment` stay one switch away (`minimap.json` `style`). Sheet: `build/minimap/review.png`.
- **Sizes** (after the first play test, 2026-10-07):
  - `ui_scale` 2.3, everything 15% bigger than the first pass.
  - The dialog's iron frame at `frame_scale.edge` 0.7. At full size it was too heavy.
  - The dialog is drawn like the original's 3D view (`SMRPanel` with corners). The view's frame (`merintr.rc` IDB_ULTOP...) is the `viewtreat_*` strips, drawn in an 8 px band outside the view; the 52×52 gold-ball corners (`clientd3d viewtreat_*`) sit inside it at the view's corners. Together they make one ornament that reaches outward. So the stone window sits inside a transparent band the corners reach into, and nothing is cut. The iron frame keeps its edges but not its own corners (one corner image). Both are sized by `frame_scale.view` (0.7). `ultop.bmp` and its set are unused by the original.
  - Text grows by `text_scale` 1.41 (25% more after the second play test). The spell list's icons are 10% bigger (`list_slot_px` 20), and its whole row shows the spell's tooltip. The tooltip has more padding (`tooltip_padding_px`).
- **Child windows** (bag, hotbar, tab pages, avatar) use the grey "inset" frame and the dark stone. "inset" is the inventory pane's bevel in grey; `build_ui_art.py` makes it from the brown original. The tab pages fill the dialog's height.
- **Tooltips** use the dialog's dark stone and iron frame.

## Character creator (2026-10-07)
The original's "Customize your character" (`module/char`), as Shards shows it (`ReferenceImages/ui/shards-*.png`). It is `UI/SMRCharCreator`, shown by the login screen in place of its window while the network phase is `Creating` (ADR 0010).
- **Pages:** Name, Appearance, Statistics, Spells, Skills, as text tabs (`SMRTextButton` with `bActive`) that share the window's width. Prev / Next go through them; OK and Cancel are at the bottom.
  - **Name:** the name field, and a multi-line description (`SMRTextBox`). The text fills the box, so a click anywhere in it starts typing; before 2026-10-08 the text was one line inside a scroll box, and a click below it did nothing.
  - **Appearance:**
    - on the left, a large face as the original's creator showed it: the head with its face parts and hair, 176 original pixels square (`AMRAvatarPreview` in portrait mode, 44 cm across, centred on the head for each gender). It turns by dragging or with the `<` Turn `>` buttons under it, so the head can be seen from every side. (The whole-body window was there until 2026-10-08; the maintainer preferred the larger face. The inventory avatar still turns by dragging, one of the eight angles per 17.5 % of its width.);
    - the face stands on a light grey (`creator_preview_bg`), where the sprite's edges read better than on black;
    - Male / Female, a skin slider from Light to Dark (a gold line, a tick per skin and a thumb: `SMRSlider` `bTrack`), and `<` `>` pickers for hair, hair colour, eyes, nose and mouth;
    - it starts random, as the original did.
  - **Statistics:** six sliders (`SMRSlider`: the stat bar art, click or drag) from 1 to 50 that never spend more than the 70 points left, our own short descriptions, the original's four presets, and a points-left bar. The colours are the original client's (`clientd3d/color.c`, as Shards uses them): `COLOR_BAR1` green (0, 128, 0) for the stats, `COLOR_BAR2` red (128, 0, 0) for the points left, `COLOR_BAR3` (48, 0, 0) for the empty part (`graph_bar`, `graph_points`, `graph_empty` in `ui_style.json`).
  - The skin slider's steps come from the server's list. It used to be built before the options arrived and had two steps instead of four.
  - **Spells / Skills:**
    - available and chosen lists (`SMRSelectList`, sorted, labelled "School level: name");
    - Add / Remove, or click a selected row again;
    - the description and cost of the selected row;
    - one 45-point pool. Rows that cost too much, or that Shal'ille / Qor rule out, are dimmed.
- **Text:** the information text (prompts, captions, list rows) is 30 % larger than the controls' (`InfoSize` in `SMRCharCreator.cpp`); the stat descriptions are not (two lines at that size crowded the page). The page is 360 × 214 original pixels.
- **Points left** bars are as tall as the stat sliders. On the Statistics page they sit 10 px under the suggestions; on Spells and Skills, 13 px under the lists. The Shal'ille / Qor rule is under the available spells, left aligned.
- **Every point spent:** OK refuses while stat points are left, or while spell and skill points are left and some spell or skill still fits them ("Spend all of your points first: ..."), and shows that page. With costs of 10 and 25 a few points can be left with nothing to buy (four 10-point picks leave 5), which is allowed. The original only warned; this is the maintainer's rule (2026-10-08). It lives in the creator, not in `MRCharInfo::Validate`: the server accepts unspent points.
- **OK** runs `MRCharInfo::Validate`. On a problem it shows that page with the reason; otherwise it sends the character and waits. A refused name returns to the Name page.
- **Data:**
  - the options are the server's (`BP_CHARINFO`);
  - the words and presets are in `data/ui/char_create.json`. The presets' numbers are the original's; the words are ours, because `char.rc` is GPL;
  - offline (UI shots), the options come from `data/charinfo.json`.
- **Shots:** `run_ui_shots.ps1 -Label creator` (`creator_*`), sheet `build/ui/shots/creator/sheet.png`.

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

## Escape menu and the online source (2026-10-08, M0 of [ADR 0012](0012-client-parity-and-world-coverage.md))
- **The Escape menu** (`UI/SMRGameMenu`) has Resume, Options, Log Off and Quit Game, in the login screen's stone panel over the dimmed world.
  - Esc or F10 opens it. In Play-In-Editor, Esc stops the session, so use F10 there.
  - Options is greyed out until M9.
  - **Log Off** returns to the character list and stays connected (`UMRNetSubsystem::ReturnToCharacters`). It shows only online.
  - **Quit Game** logs off first.
  - While the menu is open the UI has the keyboard and nothing walks or looks.
- **Two inventory sources:**
  - **Offline:** `UMRMockInventory`, as before.
  - **In a server's game:** a fresh `UMRNetInventory` per character. It holds the server's spells and skills, and a spell bar kept on this client. The bag, hotbar and equipment are empty until M3; no mock items are shown online.
  - `UMRUISubsystem` swaps the source when the server phase changes; the widgets read the current one.
- Sheets: `build/ui/shots/m0/18_game_menu.png` (offline), `game/UnrealMeridian/Saved/Screenshots/MRNet/online_4.png` and `online_5.png` (`run_net_test.ps1 -Render -Hold 52`).

## Over the world, and Look (2026-10-08, M2a of ADR 0012)
- **`SMRWorldOverlay`** (under every panel) draws:
  - the server's names, as the original: `OF_DISPLAY_NAME` within 15 squares, signs at any distance, the target always, in the name colour, smaller with distance, not through walls;
  - corner brackets on the target (red if it can be attacked, gold if not) and fainter ones on what the crosshair is on;
  - a small crosshair while the mouse looks.
- **`SMRLookDialog`** (right mouse button):
  - the stone panel with a picture on an inset, the description, inscription, extra lines and web page;
  - one's own description in a text box with Save;
  - a list to pick from when several things are under the crosshair.
  - While it is open the UI has the keyboard and mouse.

## Inventory online (2026-10-08, M3 of ADR 0012)
`UMRNetInventory` shows the server's inventory with the same widgets and click rules; each interaction becomes a request, and the screen shows what the server then says.
- **Where an item shows:** in use, on its equipment slot (its class from its icon in `data/items.json`; a second ring on the second ring slot, a second hand item in the free hand); else on the hotbar slot it was put on; else in the bag, in the server's order.
- **The right hand is its own slot online:** the server wields one weapon, and won't swap a wielded weapon for another (it is put away first). Selecting a hotbar slot doesn't wield it.
- **The hotbar** is a layout on this client: each slot names an item (found again by its icon and name after a server save renumbers objects). It isn't saved between sessions yet.
- **Clicks:** onto an equipment slot uses the item; off one takes it off; onto another item in the bag takes its place (`BP_REQ_INVENTORY_MOVE`); out of the window drops it (right click: one; right-click picking up takes half of a stack, to drop some); shift click uses or takes off.
- **Icons:** the prebuilt ones where the item's class is known; else the item's own bitmap from the server, made at runtime (nearest filtered).
- **In the world:** G gets, F opens or activates, Look offers Get, Inside and Use; a container's contents use the Look dialog's list.

## Combat on the screen (2026-10-08, M4 of ADR 0012)
- **Damage numbers** (ours: the original only printed the line). They come from the server's hit messages:
  - what we deal rises over what we hit, pale gold;
  - what we take rises over us in red, or under the crosshair in first person;
  - each shows 1.4 s; `mr.UI.DamageNumbers 0` hides them.
- **A creature drawn with our sprite** has its name, brackets and numbers just over its own height (a bunny's knee height), not a player's.
- **Screen effects** (`BP_EFFECT`) are drawn by the HUD under the Slate UI, as the original drew them over the view and not the interface:
  - pain is red, whiteout white, and flashes and the override are the `XLAT_BLEND*` colours;
  - blindness is black, with no names or brackets.

## Spells, enchantments, quests and retraining (2026-10-08, M5 of ADR 0012)
- **The spell bar casts online.** A spell that needs a target and has none shows "Cast X on what?" under the crosshair. The next click on something, `\` for yourself, or a click on an item in the dialog picks it; Esc stops. U does the same for using an item on something.
- **Enchantment icons** (`SMREnchantments`):
  - the player's at the top left, as the original drew them by the player's portrait;
  - the room's under the minimap;
  - their spell's icon, else their own bitmap, named on hover.
- **The Quests page** lists the server's headings (gold) and quests (icon and name; a click looks at one).
- **Retraining** (`SMRStatChange`): a centred stone panel with the six stats, − and + buttons, the points left, and Change (once every point is placed) or Cancel.

## Trade (2026-10-08, M6 of ADR 0012)
- **The Look dialog shows what an NPC does** (`data/net/npcs.json`): Buy, Sell, Withdraw and Deposit (a vault keeper), Bank (a banker), and Give for others that take things. A player's Look has Offer.
- **One trade dialog** (`SMRTradeDialog`), a centred stone panel like Look's:
  - rows with the item's icon (our prebuilt one, else its bitmap), name, price in a shop, and an amount box for number items;
  - a click selects a row (gold);
  - a shop shows the running total;
  - an offer shows "You give" and "<name> gives", with Accept, Answer or "Waiting for <name>...";
  - Cancel calls the offer off for both sides;
  - the bank has an amount box with Deposit, Withdraw and Balance.

## Chat and the others (2026-10-08, M8 of ADR 0012)
- **The chat log** (`SMRChatLog`), bottom left:
  - 280 × 96 px, keeps 120 lines;
  - lines in the server's colours and styles ("~" codes, as rich text), with timestamps as an option;
  - tabs All, Chat, Combat and Game;
  - while typing: the tabs click, the wheel scrolls back, and Up and Down recall earlier lines.
- **A typed line** is speech unless it's a command (`MRChat::Interpret`, `UMRUISubsystem::RunChatLine`):
  - a first word that is a command's whole name ("tell", "who", "safety on") runs it;
  - "/" takes the start of a name, as the original did ("/b" broadcasts);
  - ":" emotes;
  - aliases ("alias") stand for commands.

  The original treated every line as a command and needed "say" to speak.
- **Four windows** (`SMRSocial`), centred stone panels like Look's, one at a time; Esc or Close shuts them:
  - **Who is on (O):** Tell (fills the chat line), Ignore, and the Ignore everyone, No broadcasts and Timestamps toggles.
  - **Mail (L):** the kept mail, reading one, New, Reply, Delete and Get new. Writing has To, Subject and the text; names are checked by the server before sending.
  - **News:** opens when a board is looked at. It has the headings, an article, Post (where allowed) and Refresh.
  - **Guild (Y):**
    - **Members:** each with its rank's name; Vote for, Raise, Lower, Exile and Abdicate to, by the guild's rights. Invite takes the target; Renounce and Disband.
    - **Other guilds:** allies and enemies, and buttons to change them.
    - **Founding:** a guild creator's offer opens a form for the name and the five ranks.
- **The Escape menu** has Who Is On, Mail and Guild. F5–F7 wave, point and dance online (`BP_ACTION`).
- Pictures: `build/net/m8_social.png` (`run_net_test.ps1 -Render`: `who.png`, `mail.png`, `news.png`, `guild.png`).

## Options, the large map and the quick chat (2026-10-08, M9 of ADR 0012)
- **The Options window** (`SMROptions`; the Escape menu's Options, or the "password" and "suicide" commands). Every change is kept at once.
  - **Graphics** (the engine's `UGameUserSettings`): quality, view distance, window mode, screen size, vertical sync, a frame limit, and the field of view (`mr.Camera.FOV`).
  - **Sound:** music and sounds on or off with their volumes, room sounds, random sounds (the `mr.Audio.*` console variables).
  - **Controls:** every key (`Core/MRSettings` `MRKeys`), with the presets:
    - **Modern:** WASD and the mouse;
    - **Original:** the original client's keys (`merintr.c interface_key_table`): the arrows walk and turn, Ctrl attacks, I opens the inventory, End changes the view.

    A key taken by two actions leaves the other without one.
  - **Game:** the server-kept options (safety, grouping, auto-loot...), damage numbers, and the original's typing (every line a command, `mr.Chat.OriginalTyping`).
  - **Chat:** the quick chat's twelve lines, each run at once or put in the chat line.
  - **Account:** change the password; delete the character, confirmed with the password.
  - **Where they're kept:** console variables and keys in `GameUserSettings.ini` (`[UnrealMeridian.CVars]`, `[UnrealMeridian.Keys]`); the quick chat, hotbar and map notes per character (`Saved/MRNet/<server>/social/<name>.json`).
- **The quick chat** (the original's function-key aliases, `alias.c`): F1–F12 online, F10 staying the menu's. The original's defaults: help, rest, stand, neutral, happy, sad, wry, wave, point; F11 mail, and F12 who (the original's "quit" is too easy to hit).
- **The large map** (M, or "map"): the whole room with the player's notes, kept per room as the original's annotations were. A click marks where a note goes, else it goes where the player stands.
- **The hotbar and the spell bar** are kept with the character and come back when it enters again (an item found by its icon and name).
- Pictures: `build/net/m9_options.png`.

## Consequences
- Inventory and equipment are still **mock data offline** and empty online (M3). Spells don't cast; a "cast" plays the sprite's cast action and a cooldown sweep. Online, the vitals, stats, spells and skills come from the server's stat groups (`docs/research/blakserv-protocol.md`, "Stats"). Offline they come from the attributes and the mock file.
- Equipment doesn't change the avatar yet (no equipment layers on the sprite body).
- Rebindable keys (Enhanced Input user settings, a Controls page) are the next step for "editable in the controller settings". The UI actions are already separate actions in their own context.
- A real font and gamepad navigation are open.
- Visual checks: `tools/ue/run_ui_shots.ps1 -Label <x>` (`-MRUIShots`) writes `build/ui/shots/<x>/sheet.png`.
