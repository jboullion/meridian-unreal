# Client parity checklist

This file tracks what the Unreal client does compared with the original desktop client. The aim is parity with the original client playing the Server 104 ruleset on a Meridian server (blakserv). The plan behind it is [ADR 0012](adr/0012-client-parity-and-world-coverage.md). Update a row whenever a feature lands.

**Columns:**
- **UE:** this client today.
  - Done: works online.
  - Partial: some of it works; the note says what.
  - Mock: the UI exists but runs on local data.
  - Missing: nothing yet.
- **Shards:** the Meridian Shards client (`E:\2026_Experiments\meridian-browser`), which is our behaviour oracle. Its own code may be reused here, but not the parts it ported from the Meridian 59 source (AGENTS.md, "Hard rules").
- **M:** the milestone that delivers the feature (see "Milestones" below).

Protocol facts go in [research/blakserv-protocol.md](research/blakserv-protocol.md), each with the blakserv or clientd3d source it came from.

Last survey: 2026-10-08. M0 to M9 done the same day; what they left over is in "Left over from finished milestones" at the end.

## Session and account

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Log in, create an account by logging in | `AP_LOGIN`, `AP_LOGINOK`, `AP_GETCHOICE` | Done | Done | — |
| Login error messages | `AP_LOGINFAILED`, `AP_ACCOUNTUSED`, ... | Done | Done | — |
| Character list, message of the day | `BP_CHARACTERS` | Done | Done | — |
| Character creator | `BP_CHARINFO`, `BP_NEW_CHARINFO` | Done | Done | — |
| Delete a character | `UC_SUICIDE` (the original's "suicide"; `BP_DELETE_CHARACTER` isn't handled by this server) | Done: Options > Account, confirmed with the password | Missing | M9 |
| Change password | `BP_CHANGE_PASSWORD`, `BP_PASSWORD_OK/NOT_OK` | Done: Options > Account | Done | M9 |
| Keep-alive and redbook | `BP_PING`, `BP_ECHO_PING` | Done | Done | — |
| Server saves: wait and invalidate | `BP_WAIT`, `BP_UNWAIT`, `BP_INVALIDATE_DATA` | Done: no moves while waiting; the room, players and stats are asked for again | Done | M0 |
| Resync after a bad frame | `BP_RESYNC` | Not possible: blakserv's game-mode handshake can't complete, so a broken stream ends the session (protocol notes, "Session") | Missing | — |
| Escape menu: log off to the character list, quit | `BP_REQ_QUIT`, `BP_QUIT`, `AP_GETCHOICE` | Done: Esc or F10, with Who, Mail, Guild and Options | Done | M0, M9 |
| Room checksum | `BP_PLAYER` security | Done: checked on every room; a mismatch is logged (M1 builds the room at runtime instead) | Done | M0 |
| The server's game files | `manifest.json`, `<assets>/<name>` | Done: the asset cache downloads and hash-checks any listed file | Done | M0 |

## World and movement

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Enter a room, walk, turn | `BP_PLAYER`, `BP_REQ_MOVE`, `BP_REQ_TURN` | Done | Done | — |
| Doors ("go") and edge exits | `BP_REQ_GO` | Done | Done | — |
| Every room in the world | `.roo` | Done: 13 built rooms; any other room is built at runtime from the server's files | Done: 362 rooms parsed at runtime | M1, W2 |
| Server room changes: lifts, doors, texture swaps | `BP_SECTOR_MOVE`, `BP_SECTOR_CHANGE`, `BP_CHANGE_TEXTURE`, `BP_WALL_ANIMATE` | Partial: lifts, depth, scroll and texture changes on runtime rooms; authored zones don't move (W2.3); wall frames (`BP_WALL_ANIMATE`) not yet | Missing | M7 |
| Scrolling and animated textures | from the `.roo` | Partial: scrolling floors, ceilings and walls on runtime rooms; frame animation not yet | Done | M7 |
| Sky and background overlays (sun, moon) | `BP_BACKGROUND`, `BP_*_BG_OVERLAY` | Decided: our own sky and sun (ADR 0005); the server's background is kept, not drawn | Done | M7 |
| Server lighting | `BP_LIGHT_AMBIENT`, `BP_LIGHT_PLAYER`, `BP_LIGHT_SHADING` | Done on runtime rooms: the original's light model (sector, ambient, the player's light); authored zones keep their moods | Done | M7 |
| Sector light changes | `BP_SECTOR_LIGHT` | Not drawn: flicker, which the original's Direct3D client doesn't draw either | Missing | M7 |
| Set view, translation override | `BP_SET_VIEW`, `BP_RESET_VIEW`, `BP_XLAT_OVERRIDE` | Not needed now: Kod never sends `BP_XLAT_OVERRIDE`; only the view globe sends `BP_SET_VIEW` (left over) | Missing | M7 |

## Objects

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Creatures as sprites (players, monsters, NPCs) | `BP_ROOM_CONTENTS`, `BP_CREATE`, `BP_MOVE`, ... | Done, for converted sprites | Done | — |
| Items, containers, signs and ornaments | the same | Done: drawn from their bitmaps, or by the world build's props in built zones | Done | M2 |
| Sprites we haven't converted | `.bgf` | Done: drawn from the server's bitmap with its overlays (since 2026-10-10: the flagpole's flag, hung on its 3D pole) but no colours yet; since 2026-10-10 anything sent with a player's body overlays (soldiers and guards, `human.kod`; a logged-off player's ghost) is drawn as a player figure instead of its bare torso (`MRNetLook::IsPlayerFigure`) | Done | M1 |
| Draw effects, name colours, object lights | object fields | Partial: invisible, black and translucent (dithered) on bitmap sprites and, since 2026-10-10, on our sprite bodies (a logged-off ghost); name colours; object lights not yet (left over) | Done | M2, M7 |
| Name plates | — | Done: the original's rules (15 squares, signs, the target), not through walls | Done | M2 |
| Armour, weapons and hats on players | overlays | Done: torsos, arms, legs, weapons, shields, bows and helmets from the server's overlays, all 165 bitmaps converted; arms bend as the original's | Done | M2b |
| First-person weapon and shield | `BP_PLAYER_OVERLAY` | Done: the server's slots drawn in their screen corners and animated; the local swing shows first | Done | M2b |
| Look / examine dialog | `BP_REQ_LOOK`, `BP_LOOK` | Done: right mouse button; picture, description, inscription; a picker for a pile; Get, Inside and Use buttons for things in the room (M3) | Done | M2, M3 |
| Player descriptions | `UC_LOOK_PLAYER`, `BP_CHANGE_DESCRIPTION` | Done: face portrait, extra lines, web page; edit and save one's own | Done | M2 |

## Inventory and items

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Inventory list | `BP_INVENTORY`, `BP_INVENTORY_ADD/REMOVE` | Done online (the server's items; a number item's amount kept by `BP_CHANGE`); mock offline | Done | M3 |
| Use and unuse, equipment slots | `BP_REQ_USE`, `BP_USE_LIST`, `BP_USE`, `BP_UNUSE` | Done: onto an equipment slot or shift click; in-use items show on their slot (the wielded weapon in the right hand) | Done | M3 |
| Apply to a target, activate | `BP_REQ_APPLY`, `BP_REQ_ACTIVATE` | Done: F activates what the crosshair is on; U uses the selected (or hovered) item on what you choose next | Done | M3, M5 |
| Get and drop (stack amounts) | `BP_REQ_GET`, `BP_REQ_DROP` | Done: G gets (a list for a pile); dropping out of the window drops it (right click: one of a stack; half picked up with right click) | Done | M3 |
| Containers | `BP_SEND_OBJECT_CONTENTS`, `BP_REQ_PUT`, `BP_REQ_GET_FROM_CONTAINER` | Partial: F or Look's Inside lists the contents, picking one takes it; putting in is in the protocol, no UI yet | Done | M3 |
| Give | an offer, `BP_REQ_OFFER` (`BP_REQ_GIVE` is unused) | Done: Look's Give for NPCs that take things, Offer for players | Missing | M6 |
| Reorder | `BP_REQ_INVENTORY_MOVE` | Done: an item put on another in the bag takes its place | Missing | M3 |
| Hotbar | — | Done online as a layout on this client (not saved between sessions yet) | Missing | M3 |
| Equipment on the avatar preview | — | Done: the avatar wears the server's equipment (M2b) | — | M3 |

## Combat and effects

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Attack (aim picks the target) | `BP_REQ_ATTACK` | Done: left mouse attacks the target (if in view), else what the crosshair is on, else the nearest attackable within 5 squares; the server's swing shows in first and third person | Done (click target) | M4 |
| Targeting: aim, Tab, self, clear, halo | — | Done: the crosshair (since 2026-10-10 the free cursor, ADR 0009) aims; objects drawn by a world-build prop can be aimed at and looked at since 2026-10-10 (the sight line ignores their own prop), T takes it, Tab / [ ] cycle, \ self, Esc clears; brackets | Done | M2 |
| Projectiles | `BP_SHOOT`, `BP_RADIUS_SHOOT` | Done: the server's bitmap flies at its speed, with its light (not yet seen against a real caster) | Done | M4 |
| Screen effects: blind, paralyze, shake, invert, pain, whiteout, flash | `BP_EFFECT` | Done: tints and blindness over the view, paralysis stops walking, shake moves the eye; invert approximated (the grading is before the tonemapper) | Done | M4 |
| Blur, waver, rain, snow, sand | `BP_EFFECT` | Done: blur as depth of field, waver as a sway; online the room's rain, snow and sand are the server's (fireworks not drawn) | Missing | M4 |
| Death and the Underworld | `BP_PLAYER` | Done: the Underworld is built at runtime; its archway leads back to Raza (`run_net_test.ps1 -Create -Death`) | Done | M4 |
| Damage numbers (our addition) | hit messages | Done: what we deal rises over what we hit, what we take over us (under the crosshair in first person); `mr.UI.DamageNumbers` | Done | M4 |

## Spells, skills and stats

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Vitals bars | `BP_STAT` group 1 | Done | Done | — |
| Stats tab | group 2 | Done | Done | — |
| Spell and skill lists | `BP_SPELLS`, `BP_SKILLS` (+ADD/REMOVE) | Done: the server's lists, percentages from stat groups 3 and 4 | Done | M5 |
| Cast with targets | `BP_REQ_CAST` | Done: the target if in view, else what the crosshair is on, else choose (a click in the room or the inventory, \ for yourself, Esc stops) | Done | M5 |
| Spell bar | — | Done: numpad 1–9 casts; the layout is kept on this client (not saved between sessions yet) | Missing | M5 |
| Enchantments on you and on the room | `BP_ADD_ENCHANTMENT`, `BP_REMOVE_ENCHANTMENT` | Done: icons top left (on you) and under the minimap (the room), named on hover | Done | M5 |
| Quests tab | group 5 | Done: headings and quests; a click looks at a quest | Done | M5 |
| Stat changes, retraining | `BP_STAT_CHANGE` (156), `BP_CHANGED_STATS*` | Partial: the six stats move (school levels kept as offered); not tried against an elder yet | Missing | M5 |
| Rest and stand | `UC_REST`, `UC_STAND` | Done: R; no walking, attacking or casting while resting | Done | M5 |

## NPCs and trade

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Buy from an NPC | `BP_REQ_BUY`, `BP_BUY_LIST`, `BP_REQ_BUY_ITEMS` | Done: Look's Buy; a list with prices, amounts for number items, a total | Done | M6 |
| Sell (offer) | `BP_REQ_OFFER`, `BP_OFFERED`, `BP_COUNTEROFFER`, ... | Done: Look's Sell; the NPC's price, Accept or Cancel | Done | M6 |
| Trade with players | `BP_OFFER`, `BP_REQ_COUNTEROFFER`, `BP_COUNTEROFFERED` | Done: Look's Offer; an offer to us shows with what to give back; tried with a second player (`run_net_test.ps1 -Pair`) | Partial | M6 |
| Vault | `BP_REQ_WITHDRAWAL`, `BP_WITHDRAWAL_LIST`, `BP_REQ_DEPOSIT` | Done: Look's Withdraw and Deposit on a vault keeper | Done | M6 |
| Bank money | `UC_DEPOSIT`, `UC_WITHDRAW`, `UC_BALANCE` | Done: Look's Bank on a banker: an amount in or out, the balance | Done | M6 |

## Communication

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Say, and the chat log | `BP_SAY_TO`, `BP_SAID` | Done | Done | — |
| Yell, broadcast, emote, tell | `BP_SAY_TO` | Done (broadcast untried online) | Done | M8 |
| Group chat (tells to a tell group) | `BP_SAY_GROUP` | Done: tell groups kept per character | Done | M8 |
| Guild chat | `BP_SAY_TO` (guild) | Done (untried: the test character has no guild) | Missing | M8 |
| Typed commands and aliases | — | Done: the original's list, "/" for a name's start, aliases | Done | M8 |
| Chat tabs, timestamps, `~` colours | — | Done: All, Chat, Combat, Game; the server's colours and styles (no italic) | Done | M8 |
| Who list and ignore | `BP_PLAYERS`, `BP_PLAYER_ADD/REMOVE`, `BP_SAY_BLOCKED` | Done: the who window, tell, ignore, ignore everyone, no broadcasts | Done | M8 |
| Server-kept options | `UC_REQ_PREFERENCES`, `UC_SEND_PREFERENCES` | Done through the commands; the Options dialog is M9 | Done | M8, M9 |
| Mail | `BP_MAIL`, `BP_SEND_MAIL`, `BP_LOOKUP_NAMES`, ... | Done: kept on this computer, read, reply, write | Done | M8 |
| News boards | `BP_LOOK_NEWSGROUP`, `BP_ARTICLES`, `BP_ARTICLE`, ... | Done: read and post (posting untried) | Done | M8 |
| Guilds | `UC_*GUILD*` | Partial: members, ranks, every member action, alliances, founding; no halls or shields | Done | M8 |
| Emotes and moods | `BP_ACTION` | Done: wave, point, dance, moods (commands, F5–F7) | Missing | M8 |

## Sound

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Sounds from the server | `BP_PLAY_WAVE`, `BP_STOP_WAVE` | Done: at their object or square, or 2D; loops until the next room; files we haven't imported are downloaded and decoded | Done | M7 |
| Music from the server | `BP_PLAY_MUSIC`, `BP_PLAY_MIDI` | Done for `.ogg`; the server's 6 `.mp3` aren't decoded | Done | M7 |

## Interface and settings

| Feature | UE | Shards | M |
|---|---|---|---|
| HUD, hotbars, inventory dialog | Done (as UI; data per rows above) | Done | — |
| Minimap: walls and the player | Done | Done | — |
| Minimap: players, monsters, NPCs | Done: dots by the server's minimap flags; runtime rooms draw their walls | Done | M2 |
| Map annotations | Done: the large map (M), notes per room and character | Missing | M9 |
| Options: graphics, audio, controls | Done: the Options window | Done | M9 |
| Key rebinding, Modern and Original presets | Done (keyboard and mouse; the gamepad's stay) | Done | M9 |
| Server-kept game options (`CF_*`) | Done: Options > Game, and the commands | Done | M8, M9 |
| Tooltips | Done for items, spells and skills; not for buttons | Missing | M3, M9 |
| Admin and guide commands | Done: the Admin Console (Escape menu, admin and DM characters; ADR 0009) | Done | After M9 |

## World coverage

| Tier | Rooms | Notes |
|---|---|---|
| Authored | 13 (Raza and around, RIDs 300–308, 330–333) | the zone-environment skill |
| Baked | 0 | W2: every room, built automatically from the blockout |
| Runtime | every other room (about 380) | M1: built when entered, from the server's `.roo` and textures |

The server serves about 395 rooms. `ReferenceServers/Server-104/resource/rooms` has 362 of them; the Server 104 client folder has 378.

## Milestones

| M | Name |
|---|---|
| M0 | Foundations: client world model, session robustness, asset cache, Escape menu (done 2026-10-08) |
| M1 | Whole-world travel: runtime rooms and runtime sprites (done 2026-10-08) |
| M2 | See and select everything: all objects, name plates, targeting, Look (M2a, done 2026-10-08); equipment overlays (M2b, done 2026-10-08) |
| M3 | Inventory and items (done 2026-10-08) |
| M4 | Combat, death and effects (done 2026-10-08) |
| M5 | Spells, skills, enchantments, stats (done 2026-10-08) |
| M6 | NPCs, economy, player trade (done 2026-10-08) |
| M7 | Server-driven world: sound, light, room changes, texture animation (done 2026-10-08) |
| M8 | Communication and social (done 2026-10-08) |
| M9 | Settings, account, polish (done 2026-10-08) |
| W2 | Baked tier for every room (runs alongside, after M1) |
| W3 | Authored zones, one at a time |

## Left over from finished milestones

Things a finished milestone didn't do, or did without proof. Each lands in the milestone named in "Goes to", or "Any" when it fits in whenever it's convenient. Tick it and update its row above when it's done. Every milestone adds its own "Not yet" items here (ADR 0012, "Log").

| ☐ | From | What | Why it waited | Goes to |
|---|---|---|---|---|
| ☑ | M1 | Package a build with the runtime-room code (the `GeometryFramework` plugin, the cook directory) and play it | Done after M9: Win64 Development, the online test 58/58 (ADR 0011) | M9 |
| ☑ | M1 | Room geometry grouped by sector, so lifts and doors can move | M7 rebuilds the room instead (see M7's cost below) | M7 |
| ☐ | M1, M7 | Light on objects: sprites drawn from bitmaps are full bright; our converted sprites don't take the room's light model | M7 lit the room's surfaces only | Any |
| ☐ | M2b | Items' own colour translations on worn pieces stored below their own pixels (a red-tinted shield) | Those pieces have no colour-ramp atlas | Any |
| ☐ | M2b | The overlays on first-person pictures (the fist's glow) | Not drawn yet | Any |
| ☐ | M3 | Putting things into a container | The request exists (`BP_REQ_PUT`); no UI to choose the container | Any (likely with M9 polish) |
| ☑ | M3 | The hotbar layout and the spell bar layout saved between sessions | Done in M9: kept with the character | M9 |
| ☐ | M3 | Weight and bulk from the server instead of our item data | The client sums our `items.json` | Any |
| ☐ | M4 | Check other players' and monsters' swings on screen | Only our own character's swing is tested | Any (needs a second client or a monster attacking in a rendered run) |
| ☐ | M4 | See a projectile from a real spell, and test bow attacks with their ammunition | The test character has no attack spell or bow | Any (a test character with them) |
| ☐ | M4 | Fireworks (`EFFECT_FIREWORKS`) | Not drawn; M7 didn't get to it | Any |
| ☐ | M4 | A true colour inversion for `EFFECT_INVERT` | UE grades colour before the tonemapper; ours mirrors about mid grey | Any (a post-process material) |
| ☐ | M5 | Retraining that lowers school levels for points | The original module's points rules aren't ported | Any |
| ☐ | M5 | Try retraining with a real elder (Jasper, Marion, Ko'catan) | Far from the demo | Any |
| ☐ | M5 | See an enchantment on the player (icon top left) | The test's utility spells put none on the player | Any (a test character with a buff) |
| ☑ | M5, M6 | Chat commands: "rest", "stand", "cast", "buy", "deposit 100", "withdraw", "balance" | Done in M8 | M8 |
| ☑ | M6 | Try a trade between two players | Done: `run_net_test.ps1 -Pair` (a second player, `tools/ue/second_player.ts`, offers a shilling) | Any |
| ☐ | M6 | List a vault's items without opening Withdraw; check weight and bulk before buying | The server refuses instead | Any |
| ☐ | M7 | Bitmap animation: walls and sectors with an animation speed cycling their frames, and `BP_WALL_ANIMATE` (a wall's frame, passable or solid) | We draw a bitmap's first frame | Any |
| ☐ | M7 | Room changes on authored zones (lifts, doors, textures) | Needs sector-tagged pieces from `roo2gltf` | W2.3 |
| ☐ | M7 | Depth areas follow `BP_SECTOR_CHANGE` (sinking into newly deep water) | Collision follows; the zone's depth areas don't | Any |
| ☐ | M7 | A moving lift costs about 10 ms per redraw on a big room: mesh only the moving sector's walls and floors | The whole room is meshed again | Any |
| ☐ | M7 | See a lift, a door or a texture change sent by the server | No room on the test's path changes; only `Meridian.World.Changes` tries them | Any (a test hop through a room with a lift) |
| ☐ | M7 | The server's `.mp3` files (cave and temple music, ogre sounds) | We decode `.ogg` only | Any |
| ☐ | M7 | The player's light measured as the original (distance per wall), and `BP_LIGHT_SHADING`'s sun on runtime rooms | The pixel's depth stands in; our own sun shows | Any |
| ☐ | M7 | `BP_SET_VIEW` / `BP_RESET_VIEW` (looking through a view globe) | Only the view globe sends them | Any |
| ☐ | M8 | Guild halls and shields (`UC_GUILD_HALLS`, `UC_GUILD_RENT`, `UC_GUILD_SHIELD(S)`, `UC_CLAIM_SHIELD`) | Not read or shown | Any |
| ☐ | M8 | Try a guild online: founding (Barloque's guild creator), inviting, ranks, alliances | The test character has no guild; the window is pictured with sample data | Any (a test character in a guild) |
| ☑ | M8 | Try ignoring, a blocked tell, broadcasts and tells between two players | Done: `run_net_test.ps1 -Pair` (and the second player's wave) | Any |
| ☐ | M8 | Post to a news board, and delete an article | Raza's board is read only for players | Any |
| ☐ | M8 | Check a mood's changed face (happy, sad, wry) on our sprite and others' | Sent, not checked | Any |
| ☑ | M8 | The original's way of typing as an option (every line a command) | Done in M9 (Options > Game) | M9 |
| ☐ | M8 | The original's profanity filter | Not ported | Any |
| ☑ | M8 | Quick-chat keys (function keys running lines) | Done in M9 (F1–F12, Options > Chat) | M9 |
| ☐ | M8 | Moving and resizing the chat log | Not built | Any |
| ☐ | M8 | Name colours, guilds and flags in the who list | Names only | Any |
| ☐ | M8 | Italic text in the chat | The UI's font has no italic face | Any (a font with one) |
| ☑ | M8 | "suicide" and "password" | Done in M9 (Options > Account) | M9 |
| ☐ | M9 | Gamepad navigation of the windows, and a real font | ADR 0009's follow-ups, not started | Any |
| ☑ | M9 | Admin and guide commands (`BP_REQ_ADMIN`, `BP_REQ_DM`) | Done after M9: the Admin Console (ADR 0009, `run_net_test.ps1 -Admin`); its Shift+4 key and the object box are left out | Any |
| ☐ | M9 | Options: a separate Lumen switch; a hand-set quality shown as such; binding the left mouse button outside the presets | The dialog's limits | Any |
| ☐ | M9 | The original's keys typing letters straight into the chat line | The Original preset maps the moves only | Any |
| ☑ | Tests | `run_move_test.ps1`'s 3.9 m ledge jump failing in about half the runs | Done after M9: it was the movement, not the test (the round capsule bottom rolled off the ledge and lost 20 cm; ADR 0012 log); 8/8 in ten runs since | Any |
| ☑ | Movement | Stuck in deep water: our collision sinks a pool's floor by its depth (0.88 m, 1.32 m), so a bank level with the pool was a step too high; the original lets you out (a wall without a lower texture never blocks a step, clientd3d move.c) | Done: wading raises the step to the pool's sink (`UMRCharacterMovementComponent::WadingStepHeight`); frees all 172 such exits in 13 rooms (ke1, the desert shores, i3, ...); `run_move_test.ps1` 12/12 | Any |
| ☑ | Movement | Pools whose level banks *have* a lower texture: the original keeps you in | Done: steps follow the original's rule at each wall (`step_walls`, `StepWallAt`) | Any |
| ☑ | Movement | The server's override of the three wading depths (`BP_PLAYER`; only the Temple of Riija's invisible bridge) | Done for runtime rooms: an absolute floor height in their collision and wading areas (`FMRWadingOverride`); speed keeps the sector's depth, as the original. A built zone with one only logs a warning (none has) | Any |
| ☑ | Movement | Stuck on steps: rooms built at runtime climbed no step (backwards winding, double-sided collision); a 1.8 m capsule where the original's player is 1.65 m; the full step rise under low ceilings; untextured walls and sloped walls the original lets you over | Done (2026-10-09, ADR 0012): the step survey (`run_step_survey.ps1`) passes 637/637 crossings in our zones and 48,922 of 48,958 in every room; `run_move_test.ps1` 23/23 | Any |
| ☐ | Movement | Steps the survey still can't climb: a one-way wall standing on a step (nest1, 9: the step-down sweep starts inside its back face), ojas's drops into a sector whose ceiling is below the floor you come from (10), 17 singles | `Saved/MRStepSurvey/results.csv` lists them | Any |
| ☐ | Movement | Steps over `mr.Move.StepCapCm` (3 m): the original lets you up the desert's cliffs (to 34 m) where they have no lower texture | A choice (2026-10-09): capped; 504 crossings | Any |
| ☐ | Movement | Floors steeper than 45 degrees in our built zones (only roofs today) aren't walkable; runtime rooms allow 89 | Set the collision mesh's walkable slope in `build_world.py` if a reachable one appears | Any |
| ☐ | Audio | The server's wading sound (`BP_PLAYER`) isn't played (the original splashes as you wade, less often the deeper) | Read into `MRNetWorld`, unused | Any |
