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

Last survey: 2026-10-08. M0, M1, M2a and M2b done the same day.

## Session and account

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Log in, create an account by logging in | `AP_LOGIN`, `AP_LOGINOK`, `AP_GETCHOICE` | Done | Done | — |
| Login error messages | `AP_LOGINFAILED`, `AP_ACCOUNTUSED`, ... | Done | Done | — |
| Character list, message of the day | `BP_CHARACTERS` | Done | Done | — |
| Character creator | `BP_CHARINFO`, `BP_NEW_CHARINFO` | Done | Done | — |
| Delete a character | `BP_DELETE_CHARACTER` | Missing | Missing | M9 |
| Change password | `BP_CHANGE_PASSWORD`, `BP_PASSWORD_OK/NOT_OK` | Missing | Missing | M9 |
| Keep-alive and redbook | `BP_PING`, `BP_ECHO_PING` | Done | Done | — |
| Server saves: wait and invalidate | `BP_WAIT`, `BP_UNWAIT`, `BP_INVALIDATE_DATA` | Done: no moves while waiting; the room, players and stats are asked for again | Done | M0 |
| Resync after a bad frame | `BP_RESYNC` | Not possible: blakserv's game-mode handshake can't complete, so a broken stream ends the session (protocol notes, "Session") | Missing | — |
| Escape menu: log off to the character list, quit | `BP_REQ_QUIT`, `BP_QUIT`, `AP_GETCHOICE` | Done: Esc or F10; Options waits for M9 | Done | M0 |
| Room checksum | `BP_PLAYER` security | Done: checked on every room; a mismatch is logged (M1 builds the room at runtime instead) | Done | M0 |
| The server's game files | `manifest.json`, `<assets>/<name>` | Done: the asset cache downloads and hash-checks any listed file | Done | M0 |

## World and movement

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Enter a room, walk, turn | `BP_PLAYER`, `BP_REQ_MOVE`, `BP_REQ_TURN` | Done | Done | — |
| Doors ("go") and edge exits | `BP_REQ_GO` | Done | Done | — |
| Every room in the world | `.roo` | Done: 13 built rooms; any other room is built at runtime from the server's files | Done: 362 rooms parsed at runtime | M1, W2 |
| Server room changes: lifts, doors, texture swaps | `BP_SECTOR_MOVE`, `BP_WALL_ANIMATE`, `BP_CHANGE_TEXTURE`, ... | Missing | Missing | M7 |
| Scrolling and animated textures | from the `.roo` | Missing | Done | M7 |
| Sky and background overlays (sun, moon) | `BP_BACKGROUND`, `BP_*_BG_OVERLAY` | Missing: our own sky | Done | M1, M7 |
| Server lighting | `BP_LIGHT_AMBIENT`, `BP_LIGHT_PLAYER`, `BP_LIGHT_SHADING` | Missing: moods only | Done | M7 |
| Sector light changes | `BP_SECTOR_LIGHT` | Missing | Missing | M7 |
| Set view, translation override | `BP_SET_VIEW`, `BP_RESET_VIEW`, `BP_XLAT_OVERRIDE` | Missing | Missing | M7 |

## Objects

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Creatures as sprites (players, monsters, NPCs) | `BP_ROOM_CONTENTS`, `BP_CREATE`, `BP_MOVE`, ... | Done, for converted sprites | Done | — |
| Items, containers, signs and ornaments | the same | Done: drawn from their bitmaps, or by the world build's props in built zones | Done | M2 |
| Sprites we haven't converted | `.bgf` | Done: drawn from the server's bitmap (no overlays or colours yet) | Done | M1 |
| Draw effects, name colours, object lights | object fields | Partial: invisible, black and translucent (dithered) on bitmap sprites; name colours; object lights are M7 | Done | M2, M7 |
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
| Apply to a target, activate | `BP_REQ_APPLY`, `BP_REQ_ACTIVATE` | Partial: F activates what the crosshair is on; apply is in the protocol, no UI yet (with spell targets, M5) | Done | M3, M5 |
| Get and drop (stack amounts) | `BP_REQ_GET`, `BP_REQ_DROP` | Done: G gets (a list for a pile); dropping out of the window drops it (right click: one of a stack; half picked up with right click) | Done | M3 |
| Containers | `BP_SEND_OBJECT_CONTENTS`, `BP_REQ_PUT`, `BP_REQ_GET_FROM_CONTAINER` | Partial: F or Look's Inside lists the contents, picking one takes it; putting in is in the protocol, no UI yet | Done | M3 |
| Give | an offer, `BP_REQ_OFFER` (`BP_REQ_GIVE` is unused) | Missing | Missing | M6 |
| Reorder | `BP_REQ_INVENTORY_MOVE` | Done: an item put on another in the bag takes its place | Missing | M3 |
| Hotbar | — | Done online as a layout on this client (not saved between sessions yet) | Missing | M3 |
| Equipment on the avatar preview | — | Done: the avatar wears the server's equipment (M2b) | — | M3 |

## Combat and effects

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Attack (aim picks the target) | `BP_REQ_ATTACK` | Done: left mouse attacks the target (if in view), else what the crosshair is on, else the nearest attackable within 5 squares; the server's swing shows in first and third person | Done (click target) | M4 |
| Targeting: aim, Tab, self, clear, halo | — | Done: the crosshair aims, T takes it, Tab / [ ] cycle, \ self, Esc clears; brackets | Done | M2 |
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
| Spell and skill lists | `BP_SPELLS`, `BP_SKILLS` (+ADD/REMOVE) | Partial: from stat groups 3 and 4 | Done | M5 |
| Cast with targets | `BP_REQ_CAST` | Missing: animation only | Done | M5 |
| Spell bar | — | Partial: online it holds the server's spells (kept on this client); casting is M5 | Missing | M5 |
| Enchantments on you and on the room | `BP_ADD_ENCHANTMENT`, `BP_REMOVE_ENCHANTMENT` | Missing | Done | M5 |
| Quests tab | group 5 | Missing: an empty tab | Done | M5 |
| Stat changes, retraining | `BP_REQ_STAT_CHANGE`, `BP_CHANGED_STATS_*` | Missing | Missing | M5 |
| Rest and stand | `UC_REST`, `UC_STAND` | Missing | Done | M5 |

## NPCs and trade

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Buy from an NPC | `BP_REQ_BUY`, `BP_BUY_LIST`, `BP_REQ_BUY_ITEMS` | Missing | Done | M6 |
| Sell (offer) | `BP_REQ_OFFER`, `BP_OFFERED`, `BP_COUNTEROFFER`, ... | Missing | Done | M6 |
| Trade with players | `BP_OFFER`, `BP_REQ_COUNTEROFFER`, `BP_COUNTEROFFERED` | Missing | Partial | M6 |
| Vault | `BP_REQ_WITHDRAWAL`, `BP_WITHDRAWAL_LIST`, `BP_REQ_DEPOSIT` | Missing | Done | M6 |
| Bank money | `UC_DEPOSIT`, `UC_WITHDRAW`, `UC_BALANCE` | Missing | Done | M6 |

## Communication

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Say, and the chat log | `BP_SAY_TO`, `BP_SAID` | Done | Done | — |
| Yell, broadcast, emote, tell | `BP_SAY_TO` | Missing | Done | M8 |
| Group chat | `BP_SAY_GROUP` | Missing | Done | M8 |
| Guild chat | `BP_SAY_TO` (guild) | Missing | Missing | M8 |
| Chat tabs, timestamps, `~` colours | — | Partial: colours from the rsb | Done | M8 |
| Who list and ignore | `BP_PLAYERS`, `BP_PLAYER_ADD/REMOVE` | Partial: the list is kept (M0); no window yet | Done | M8 |
| Mail | `BP_MAIL`, `BP_SEND_MAIL`, `BP_LOOKUP_NAMES`, ... | Missing | Missing | M8 |
| News boards | `BP_LOOK_NEWSGROUP`, `BP_ARTICLES`, `BP_ARTICLE`, ... | Missing | Missing | M8 |
| Guilds | `UC_*GUILD*` | Missing | Missing | M8 |
| Emotes and moods | `BP_ACTION` | Missing: local test keys only | Missing | M8 |

## Sound

| Feature | Messages | UE | Shards | M |
|---|---|---|---|---|
| Sounds from the server | `BP_PLAY_WAVE`, `BP_STOP_WAVE` | Missing: local zone audio | Done | M7 |
| Music from the server | `BP_PLAY_MUSIC`, `BP_PLAY_MIDI` | Missing: local zone music | Done | M7 |

## Interface and settings

| Feature | UE | Shards | M |
|---|---|---|---|
| HUD, hotbars, inventory dialog | Done (as UI; data per rows above) | Done | — |
| Minimap: walls and the player | Done | Done | — |
| Minimap: players, monsters, NPCs | Done: dots by the server's minimap flags; runtime rooms draw their walls | Done | M2 |
| Map annotations | Missing | Missing | M9 |
| Options: graphics, audio, controls | Missing: console variables only | Done | M9 |
| Key rebinding, Modern and Original presets | Missing: hard-coded | Done | M9 |
| Server-kept game options (`CF_*`) | Missing | Done | M9 |
| Tooltips | Missing | Missing | M9 |
| Admin and guide commands | Missing | Missing | M9 (optional) |

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
| M5 | Spells, skills, enchantments, stats |
| M6 | NPCs, economy, player trade |
| M7 | Server-driven world: sound, light, room changes, texture animation |
| M8 | Communication and social |
| M9 | Settings, account, polish |
| W2 | Baked tier for every room (runs alongside, after M1) |
| W3 | Authored zones, one at a time |
