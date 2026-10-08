# The blakserv protocol

These are our own notes on the Meridian 59 client/server protocol, written from reading `ReferenceServers/Server-104` and checked against a running server ([ADR 0010](../adr/0010-meridian-servers.md)).

The implementation is `game/MeridianRemastered/Source/MeridianRemastered/Net/`. Paths below are relative to `ReferenceServers/Server-104/`.

Never copy or translate code from Server-104: it's the Meridian 59 authors' GPL code, which our Unreal Engine linking exception can't cover. Read it, write the facts down here, and implement from this page. Meridian Shards' own protocol code (`packages/protocol`) may be reused (AGENTS.md, "Hard rules").

## Transport
- **TCP to blakserv:** port 5959.
- **Over a WebSocket:** the Shards gateway passes the byte stream through unchanged. It does not re-frame: one WebSocket message can hold part of a frame or several frames.
- **The server speaks first.** On a fresh connection it sends `AP_GETLOGIN`.
- **A broken stream ends the session.** The beacon handshake (`BP_RESYNC`, `blakserv/game.c GameSyncInit`) was meant to recover one, but it can't work in game mode (tested 2026-10-08, "Session" below).
- **Keep-alive:** blakserv hangs up an idle game session (`[Inactive] Game`, 30 s on Shards). The client sends `BP_PING` every 5 s.

## Frames (`blakserv/session.c SendGameClient`, `session.h HEADERBYTES`)
```
u16 length | u16 check | u16 length again | u8 epoch | body (length bytes)
```
- **Byte order:** little-endian.
- **Body:** the first byte is the message type.
- **Strings:** a `u16` length and that many bytes, with no NUL. They are Latin-1 (one byte, one character), never UTF-8.
- **The check word:**
  - From the server: the low 16 bits of the CRC-32 of the body.
  - From the client in login mode: the CRC (`blakserv/synched.c` doesn't verify it).
  - From the client in game mode: the security word (below).
- **The CRC** is the standard reflected CRC-32: poly `0xEDB88320`, init and final XOR `0xFFFFFFFF` (`util/crc.c`).
- **The epoch:**
  - The server stamps each game frame with its current epoch (`session.c`).
  - The client sends 0 in login mode, and in game mode the latest epoch it has received.
  - A game message with a stale epoch is dropped silently, but it still steps the security streams (`game.c GameProcessSessionBuffer`).

## Login (`blakserv/synched.c`)
1. Server: `AP_GETLOGIN` (21).
2. Client: `AP_LOGIN` (2). The fields, in order:
   - `u8` major and `u8` minor version: 50, 55. Below `[Login] InvalidVersion` is refused (`VerifyLogin`).
   - `i32` OS type, OS major, OS minor, RAM and CPU.
   - `u16` screen width and height.
   - `i32` displays, bandwidth, and "reserved" (its low byte is the colour depth).
   - `string` user.
   - `string` password: the 16-byte MD5 of the password, with every 0x00 byte replaced by 0x01 (`util/md5.c MDString`; the server keeps it as a C string). It is unsalted, so send it only over TLS.
   - `string` secret key. It must equal `[Login] SecretKey`; otherwise the server sends `AP_GETCLIENT` and hangs up. The server reads only the length's low byte.
   - The body has to be at least 39 bytes before the strings.
3. **An unknown user name is created as a new account** with that password and `[Account] NumSlots` character slots (`SynchedAcceptLogin`).
4. Server on success:
   - `AP_LOGINOK` (23) `u8 account type`;
   - `AP_GETCHOICE` (22) with five `u32` seeds for the security streams;
   - `AP_CREDITS` (30).
5. Server on failure:
   - `AP_LOGINFAILED` (24): a wrong password;
   - `AP_TOOMANYLOGINS` (28): after `[Login] MaxAttempts` failures, then a hang-up;
   - `AP_ACCOUNTUSED` (27);
   - `AP_MESSAGE` (34) `string`, `u8 action`;
   - `AP_NOCHARACTERS` (37).
6. Client: `AP_REQ_GAME` (4) `i32 last download time (0)`, `i32 catch (0)`, `string host ("")`.
7. Server: `AP_GAME` (25). Game mode starts.

## Game-mode security
- **Client to server** (`blakserv/game.c GameRandomStreamsStep`).
  - Before each message, every one of the five seeds steps as `s = (s * 9301 + 49297) % 233280`. This is 32-bit unsigned arithmetic, wrapping.
  - `r = seeds[seeds[4] % 4]`
  - `word = (r & 0xFFFF) ^ length ^ (type << 4) ^ crc16(body)`, kept to 16 bits.
  - `type` is the body's first byte as a **signed** char. Types of 128 and up sign-extend before the shift, so `BP_USERCOMMAND` (155) gives `0xF9B0`.
  - One wrong word and the server hangs up (`[Security] HangupSpoofs`). Every encoded message must be sent, in order.
- **Server to client** (`blakserv/commcli.c SecurePacketBufferList`). Each packet's type byte is XORed with the low byte of a token. After every packet, the token grows by `redbook[pos] & 0x7F`, and `pos` advances, wrapping at the end.
  - Until the first `BP_ECHO_PING` the token is 0 and nothing slides, so decoding every packet the same way is always right.
  - **Re-keying** (`game.c GameEchoPing`): the server answers `BP_PING` with `BP_ECHO_PING` (1): `u8 token ^ 0xED`, `u32 redbook resource id`. The echo itself is still scrambled with the old token. After it:
    - the token is that byte XOR `0xED`;
    - the redbook is the text of that resource, or `BLAKSTON: Greenwich Q Zjiria` if there is none (`GetSecurityRedbook`);
    - `pos` starts again at 0.
  - The resource is `[Security] RedbookRsc`; on Shards it is `system_success_rsc`, "Success.". The client needs the server's own `rsc0000.rsb` to know the text.

## Character select (`blakserv/game.c`, `sprocket.c`)
- **The character module:** the server sends `BP_LOAD_MODULE` (58) `u32 resource` naming `char.dll`. The client answers `BP_SEND_CHARACTERS` (45).
- **The list:** `BP_CHARACTERS` (139) contains:
  - `u16 n`;
  - n × (`u32 object id`, `string name`, `u8 flag`) — a flag of 1 means the character has never been in the game and must be created first (`GameSendEachUserChoice`);
  - `string motd`;
  - `u8 2`, then four ad strings.
- **Playing a character:** `BP_USE_CHARACTER` (46) `u32 id`.
  - Never for a slot with flag 1 (not created yet). blakserv logs it as illegal, blocks the address for a while and hangs up (`game.c`).

## Character creation (`module/char/char.c`, `charmake.c`, `charface.c`; Kod `system.kod` `SendCharInfo`, `player.kod` `PlayerNewCharInfo`)
The client is `Net/MRCharInfo` and the dialog is `UI/SMRCharCreator` (ADR 0010).

**Asking for the options:** `BP_SYSTEM` (6) followed by `BP_SEND_CHARINFO` (49), with no fields. It changes nothing on the server, so the client can simply go back to the list.

**`BP_CHARINFO` (140)**, built by `system.kod` `SendCharInfo` (`:2181-2263`) and read by `char.c` (`:148-267`). The list counts here are **u32**, except the colours:
1. Hair colours: `u8 n` (14), then n × `u8` palette translation. Server 104 sends 0, `0x0A`, `0x12`, `0x1A`-`0x1E`, `0x30`, `0x22`, `0x2A`-`0x2C`, `0x2F` (`PT_GRAY_TO_*`).
2. Skins: `u8 n` (4), then n × `u8` (`PT_BLUE_TO_SKIN1-4` = 1-4, light to dark).
3. Two face blocks, male then female (`AddFaceIconsToPacket`). Each is:
   - `u32 n` + n × `u32` hair resources (`blank.bgf` is bald);
   - **one** `u32` head resource;
   - `u32 n` + eyes, `u32 n` + noses, `u32 n` + mouths.
4. Spells: `u32 n`, then n × (`u32 number`, `u32 name`, `u32 description`, `u32 cost`, `u8 school`). The cost is 25 for a level-2 spell, else 10. The list is the server's `plNewCharSpells`: spells whose `OfferToNewCharacters` says yes, schools 1-6.
5. Skills: the same shape. Every skill of level 2 or lower.
6. Nothing may be left over: the original client refuses the message if it is.

Server 104's face options (`system.kod:130-177`, `GetAllowed*Icons :2101-2153`):

| Part | Male | Female |
|---|---|---|
| Head | phax | phkx |
| Hair | ptcd, ptac, ptba, ptad, ptbb, blank, ptxa | ptcd, ptbc, ptca, ptdb, ptbd, ptcb, ptdc, ptdr, ptxb, blank |
| Eyes | peax, pebx, pecx, pedx | pekx, pelx, pemx |
| Nose | pnax, pnbx, pncx | pnkx, pnlx, pnmx |
| Mouth | pmax, pmbx, pmcx | pmkx, pmlx, pmmx |

`tools/kod_extract/extract.py` writes the same lists to `data/charinfo.json`. The sprite build uses it, and the creator uses it as its offline stand-in. On 2026-10-07 the local server offered 33 spells and 11 skills.

**Creating the character:** `BP_SYSTEM` (6) followed by `BP_NEW_CHARINFO` (48) (`charmake.c:174`, read by `sprocket.c:92`):
- `u32 slot`;
- `string name`, `string description`;
- `u8 gender` (1 male, 2 female);
- `u16 5` + 5 × `u32` face resources, in the order **head, hair, eyes, nose, mouth**;
- `u8 hair translation`, `u8 skin translation`: the values themselves, not their indexes;
- `u16 6` + six `i32` stats: Might, Intellect, Stamina, Agility, Mysticism, Aim;
- `u16 n` + `u32` spell numbers, `u16 n` + `u32` skill numbers.

**The rules** (client: `charname.c`, `charstat.c`, `charspel.c`; server: `system.kod` `ValidateUserName`, `player.kod` `PlayerNewCharInfo :2585-2881`):
- Name: 3-30 characters, from letters, digits, `_ '!@$^&*()+=:[]{};/?|<>` and the Latin-1 letters `0xC0`-`0xFF` (not `×` or `÷`). It must not belong to another player, a monster, an NPC or a guild.
- Description: up to 999 characters (the server allows 1000).
- Stats: 1-50 each, starting at 25, 220 in all (70 points to spend).
- Spells and skills: one 45-point pool.
- Shal'ille and Qor spells can't be chosen together. **Only the client enforces this**: the server's check is commented out.

**What the server does with bad values.** It never says why; it quietly replaces them:
- Not exactly five face parts: the gender becomes male and the default male face is used. (Our first client sent none, so every new character was male.)
- A part not in the gender's list: that list's first entry.
- A hair translation not in the list: blond (`0x2F`). A skin outside 1-4: skin 3.
- Stats out of range or over 220: the junk stats 3/1/4/1/5/9.
- Spells and skills over 45 points: none at all.
- The clothes are random (`SetDefaultClothes`).

**The answer:**
- `BP_CHARINFO_OK` (56) `u32 id`. The server may have given the character a new object id (`RecycleUser`); send `BP_USE_CHARACTER` with **that** id.
- Or `BP_CHARINFO_NOT_OK` (57) with no fields: the name is taken or not allowed (the original client always says so).
- A server bug: `system.kod:4593` overwrites the "is this slot still new" check with the name check, so it isn't enforced.

## In the game (`clientd3d/server.c` shows what the client expects)
- **`BP_PLAYER` (130)** fields (`HandlePlayer`; the message must end exactly there):
  - `u32 id`, `icon`, `name`, `room object`, `room file resource` (e.g. `razainn.roo`), `room name resource`;
  - `u32 room security` (the room checksum, below);
  - `u8 ambient light`, `u8 player light`;
  - `u32 background` (a bitmap resource, e.g. `2skyd.bgf`), `u32 wading sound`, `u32 room flags`;
  - `u32 depth 1`, `depth 2`, `depth 3`: the server's override of the room's three wading depths (`SetOverrideRoomDepth`, shifted left 4).

  A room change is a new `BP_PLAYER` followed by `BP_ROOM_CONTENTS`.
- **The room security value:**
  - It is the `u32` at byte 8 of the `.roo` file, after the 4-byte magic and the `u32` version (`bspload.c LoadRoomFile`).
  - The client compares only the low 28 bits with `BP_PLAYER`'s (`game.c` after `LoadRoomFile`): the server sets the top 4 bits as an object tag. Raza is `f9ea1ac4` from the server and `89ea1ac4` in `raza.roo`.
  - `tools/roo2gltf` writes it to `data/zone_layout.json` (`roo_security`); `UMRNetWorldSubsystem` checks it on every room.
- **An object** (`ExtractObject`), field by field:
  - `u32 id`. Its top 4 bits are a tag; tag 1 is a number item and is followed by `u32 amount`.
  - `u32 icon`, `u32 name`, `u32 flags` (`OF_*`).
  - `u8 draw effect`.
  - `u32 minimap flags`, `u32 name colour`.
  - `u8 object type`, `u8 move-on type`.
  - Light: `u16 flags`; if non-zero, `u8 intensity` and `u16 colour`.
  - An optional palette prefix: when the next byte is 9 or 10, it is followed by one byte.
  - Animation: `u8 type`.
    - `NONE` (1): `u16 group`.
    - `CYCLE` (2): `u32 period`, `u16 low`, `u16 high`.
    - `ONCE` (3): the same as CYCLE, plus `u16 final`.
  - Overlays: `u8 n`, then n × (`u32 icon`, `u8 hotspot`, palette prefix, animation).
  - A player (`player.kod SendOverlays :11509`): the object's icon is the torso, its palette prefix the shirt's two-colour translation. The overlays are the arms (hotspots 31, 21), the legs (41, the pants' translation), the head (1), mouth (12), eyes (11) and nose (14), all three with the skin's translation (`ANIMATE_TRANSLATION` = 9, then the translation), and the hair (13, its colour). There's no hair overlay while a hat takes it off; a bald head sends `blank.bgf`. `Net/MRNetLook` turns this into a sprite appearance.
- **A room object** (`ExtractNewRoomObject`) is an object followed by:
  - `u16 row`, `u16 col` (Kod position: `square * 64 + fine`, so the room's top-left edge is 64);
  - `u16 angle` (0–4095, 0 = east, increasing toward south);
  - its motion state: palette prefix, animation, overlays.
- **`BP_ROOM_CONTENTS` (134):** `u32 room object`, `u16 n`, n room objects.
- **Objects coming and going:** `BP_CREATE` (217) is a room object; `BP_REMOVE` (218) is `u32 id`; `BP_CHANGE` (219) is an object followed by its new motion state (palette prefix, animation, overlays), with no position (`HandleChange`).
- **Movement from the server:**
  - `BP_MOVE` (200): `u32 id`, `u16 row`, `u16 col`, `u8 speed`. Bit 7 of the speed means "turn to face the move".
  - `BP_TURN` (201): `u32 id`, `u16 angle`.
- **Moving the player** (the client's `move.c`):
  - `BP_REQ_MOVE` (100): `u16 row`, `u16 col`, `u8 speed` (walk 25, run 55), `u32 room object`. The original sends one about every 250 ms.
  - Moving off the room's edge is a request to leave; send it at most once a second, at walking speed.
  - The server rejects only destinations outside the room's sectors, by sending `BP_MOVE` back (`user.kod UserMove`: `LIR_SECTOR_INSIDE`).
  - **An edge exit** is a move that lands in a sector but outside the room's box: the `.roo`'s things box, the same rectangle as its width and height ([roo-format.md](roo-format.md)). `room.kod SomethingMoved` then takes the `LEAVE_*` exit on that side. Rooms with edge exits draw floor past the box there (a road, a forest's edge), and that's where to walk out. A point past the box with no floor under it is refused and the player snapped back (checked 2026-10-08: the Outskirts' north edge).
- **Exits and turning:** stand on the exit square and send `BP_REQ_GO` (102). Turning is `BP_REQ_TURN` (101): `u32 own id`, `u16 angle`.
- **Chat:**
  - Send `BP_SAY_TO` (110): `u8 type` (`SAY_*`), `string text`.
  - Receive `BP_SAID` (206): `u32 sender`, `u32 sender name`, `u8 type`, `u32 format resource`, then that format's parameters.
  - `BP_MESSAGE` (32) and `BP_SYS_MESSAGE` (31) are `u32 format` plus parameters.
- **Message formats** (`clientd3d/srvrstr.c`):
  - `%d` or `%i`: a 32-bit integer.
  - `%s`: a resource id whose text is inserted and scanned again (it can hold more formatters).
  - `%q`: a literal string parameter.
  - `%r`: a resource id formatted with the parameters that follow it.
  - A `$0` right after a formatter consumes the parameter but hides it.
  - Style codes are `~` plus a letter (`~B` bold, `~I` italic, `~n` normal, colour letters).
- **Names:** `BP_CHANGE_RESOURCE` (30) is `u32 id`, `string` and adds a name at runtime.
- **Who is logged on** (`HandlePlayers`, `HandleAddPlayer`, `HandleRemovePlayer`):
  - `BP_PLAYERS` (136): `u16 n`, then n players.
  - `BP_PLAYER_ADD` (137): one player.
  - A player: `u32 id`, `u32 name resource`, `string name`, `u32 flags`, `u8 draw type`, `u32 minimap flags`, `u32 name colour`, `u8 object type`, `u8 move-on type`. The name becomes that resource's text.
  - `BP_PLAYER_REMOVE` (138): `u32 id`.
  - The server sends `BP_PLAYERS` by itself when a character enters (seen 2026-10-08); after that, ask with `BP_SEND_PLAYERS` (44).
- **Stats** (`module/merintr/merintr.c HandleStat*`, `ExtractStatistic`; the server side is `kod/.../player/user.kod ToCliStats`, `ToCliStatGroups`). The server decides which stats exist and names them; the client lists what it sends.
  - **Asking:** `BP_SEND_STAT_GROUPS` (52) with no fields; then `BP_SEND_STATS` (41) `u8 group` for each group. The original asks for group 1 and the shown group; we ask for every group after the first `BP_ROOM_CONTENTS`.
  - **`BP_STAT_GROUPS` (133):** `u8 n`, n × `u32 name resource`. Groups are numbered from 1. Server 104 sends five: Condition, Stats, Spells, Skills, Quests.
  - **`BP_STAT_GROUP` (132):** `u8 group`, `u8 n`, n stats, in display order.
  - **`BP_STAT` (131):** `u8 group`, one stat. It replaces the stat with the same number. The server sends these unasked when a value changes (health, a skill's percentage...).
  - **A stat:** `u8 num`, `u32 name resource`, `u8 type`, then:
    - type 1, numeric: `u8 tag`, `u32 value`, and when the tag is 1 (an integer) `i32 min`, `i32 max`, `i32 current max`. With tag 2 the value is a resource (text).
    - type 2, list: `u32 object`, `i32 value`, `u32 icon resource`.
  - **`num` is not the order.** Server 104's Unbound Energy is number 27 but comes first; Training Pts is 8 and comes second. Keep the message's order.
  - **The bar fills to the current max.** Health is sent as value, 0, 100, max health, and the original draws 27 of 27 as a full bar. Intellect 20 with a current max of 50 is 40%.
  - **Server 104's groups:** 1 Condition (health, mana, vigor, experience); 2 Stats (27: Unbound Energy, Training Pts, the six stats, Karma, Bulk Carried, Weight Carried, Offense, Defense, Armor, then 13 resistances); 3 Spells and 4 Skills (list stats: name, the spell or skill object, the ability percentage, its icon); 5 Quests (list stats with headers).

## Seeing and choosing things (`clientd3d/d3drender.c`, `gameuser.c`, `server.c`; `merintr.c HandleLookPlayer`)
- **Names** over objects (`d3drender.c` around `:3141`):
  - drawn for objects with `OF_DISPLAY_NAME`, not `DRAWFX_INVISIBLE`, within 15 squares (`object3d.h MAX_NAME_DISTANCE`);
  - signs (`OF_SIGN`) at any distance, and the current target always;
  - the colour is the object's `u32 name colour` (`0x00RRGGBB`), black with `DRAWFX_BLACK` (`color.c`).
- **Drawing effects** (`proto.h DRAWFX_*`): 1–3 translucent 25/50/75 %, 4 black, 5 invisible, 7 haze, 11 grey haze.
- **Minimap dots** (`proto.h MM_*`): player `0x1`, enemy `0x2`, friend `0x4`, guildmate `0x8`, monster `0x20`, NPC `0x40`, own minion `0x100`, boss `0x800`.
- **Targets** (client only; the server never hears of them):
  - `[` and `]` cycle through the attackable (`OF_ATTACKABLE`), visible objects in view (`gameuser.c UserTargetNextOrPrevious`);
  - `\` targets yourself; Escape clears the target before anything else (`intrface.c A_TARGETCLEAR`);
  - the target's name always shows.
- **Look:**
  - The client sends `BP_REQ_LOOK` (116) `u32 id`. The right mouse button was the original's look (`merintr.c`, `A_LOOKMOUSE`).
  - At an object the server answers `BP_LOOK` (207): an object (as in a room's contents, with its light), `u8 flags` (`DF_EDITABLE` 1, `DF_INSCRIBED` 2), `u32 format` + its parameters, and with either flag `u32 format` + parameters again (the inscription).
  - At a player (`user.kod SendLookPlayer`) it answers `BP_USERCOMMAND` (155) `u8 UC_LOOK_PLAYER` (2): an object, `u8 editable` (1 for yourself or an admin), `u32 format` + parameters (the description), `u32 format` + parameters (extra lines), `string` web page.
  - **Writing a description:** `BP_CHANGE_DESCRIPTION` (126) `u32 object`, `string` (up to 1000 characters, `object.h MAX_DESCRIPTION`).
  - The format strings can number their parameters (`%s$2`, `srvrstr.c CheckMessageOrder`), but no Server 104 Kod resource does, so we don't reorder.
- **First-person overlays:** `BP_PLAYER_OVERLAY` (151) is `u8 screen hotspot`, then an object without its light (`ExtractObjectNoLight`).
  - The object's id is the slot: `PWO_LEFT_HAND` 1, `PWO_RIGHT_HAND` 2 (`blakston.khd`). Each message replaces its slot (`clientd3d/overlay.c SetPlayerOverlay`).
  - The hotspot is a screen place: `HS_NW` 1 … `HS_SE` 5, `HS_S` 6, `HS_SW` 7, `HS_W` 8, `HS_CENTER` 9. 0 hides the slot (`weapon.kod GetWindowOverlayHotspot` when the weapon is put away). Group 0 draws nothing either (the fist after its swing).
  - Placement (`ComputePlayerOverlayArea`): the bitmap's size × viewport width / 452 × 0.5, against the corner or edge, plus its offsets.
  - Weapons send their window overlay when wielded (`SetWindowOverlay`): `ANIMATE_NONE` on `vrWeapon_window_hold` (5), and on attack `ANIMATE_ONCE` 150 ms from `vrWeapon_window_attack_start` to `_end` (1–4), back to the hold. Shields go to `HS_SW` in slot 1 (`shield.kod`). The bare fist is `ANIMATE_ONCE` 175 ms, groups 1–3, ending on 0 (`player.kod DoWindowOverlayFistAttack`).

## Equipment on players (`player.kod SendOverlays` / `SendMoveOverlays`, the items' `SendOverlayInformation`)
- **Order of a player's overlays:** left arm (`HS_LEFT_HAND` 31), right arm (21), legs (41), head (1), mouth (12), eyes (11), nose (14), the hair (13) unless a helmet took it off (`poHair_remove`, set by `RemoveHair` in `helm.kod`, `dhelm.kod`, `knighthelm.kod`, masks...), then each item in `plOverlays`.
- **Worn pieces swap the base parts:**
  - the torso is the object's own icon (`SetPlayerIcon`: a shirt's `GetShirtIcon`, armour's; `bt?`), its translation the object's (`piBody_translations`);
  - the arms and legs are the overlays at 31, 21 and 41 (`SetPlayerArms` from shirts, robes and gauntlets' `GetOverrideLeftArm`; `SetPlayerLegs`), each with its translation (`GetArmsTranslation`, `GetLegsTranslation`; none sent when 0).
  - Names: `bt?` torso, `bl?` / `br?` arms, `bf?` legs; the letter a, c, e... is male, b, d, f... female.
- **Items add overlays:** a bgf, a hotspot and an animation each (`AddPacket(4,overlay, 1,hotspot)` + the animation, with `ANIMATE_TRANSLATION` from `ITEM_PALETTE_MASK` first).
  - Weapons: `vrWeapon_overlay` at `HS_RIGHT_WEAPON` 22, resting on group 4, attacking `ANIMATE_ONCE` 300 ms groups 1–3 (`weapon.kod`).
  - Shields: `vrShield_overlay` at `HS_LEFT_WEAPON` 32, group 2 (`shield.kod`).
  - Bows: the bottom at `HS_BOTTOM_BOW` 33; while shooting also the top at `HS_TOP_BOW` (= 32) (`bow.kod`).
  - Helmets and hats: their own icon at `HS_TOUPEE` 13, group 1 (`helmet.kod`).
- **Holding bends the arms:** a weapon at 22 puts the right arm on group 17; a shield, bow or token puts the left on group 7. That arm then doesn't swing while walking or dance, and one-shots (attack, wave, cast) end on it (`iRight_group`, `iLeft_group`).

## Items (Kod `user.kod` ReceiveClientMsg / ToCliInventory; `clientd3d/protocol.c`, `server.c`; `blakserv/parsecli.c`)
- **The inventory:**
  - `BP_REQ_INVENTORY` (117, no body) is answered with `BP_INVENTORY` (208) then `BP_USE_LIST` (205). The original asks once on entering the game (`game.c:122`).
  - `BP_INVENTORY`: `u16 count`, then that many objects in the room-contents object format without position (`ExtractObject`: id, amount for a number item, icon, name, flags, drawing effect, minimap flags, name colour, two type bytes, light, translation, animation, overlays). The items in use come first, then the rest from the newest (`ToCliInventory` walks `plPassive` backwards).
  - `BP_USE_LIST`: `u16 count`, then `u32` ids. `BP_USE` (203) and `BP_UNUSE` (204): one `u32` id.
  - `BP_INVENTORY_ADD` (209): one object. `BP_INVENTORY_REMOVE` (210): one `u32` id.
  - **A number item's new amount** (after dropping some) comes as `BP_CHANGE` (`SomethingChanged`), the same message that changes a room object: an object plus its motion record.
- **Number items** (shillings, reagents): the id's top 4 bits are `CLIENT_TAG_NUMBER` (1) and an `u32` amount follows the id, in both directions. Sending one, the client puts the tagged id and how many (`protocol.c PARAM_OBJECT`); blakserv reads the amount whenever an id carries the tag (`parsecli.c`) and Kod gets it as `number`.
- **Requests** (each id `u32`; *item* = an id, or a tagged id and an amount):
  - `BP_REQ_USE` (106) id, `BP_REQ_UNUSE` (107) id: wear, wield or use; take off. Kod refuses a second weapon while the hands are full (`player.kod CheckPosition`: armour and gauntlets swap themselves, weapons don't).
  - `BP_REQ_GET` (113) id: pick up from the room. `BP_REQ_DROP` (118) *item*: a number item is split and the part dropped (`UserDrop`, `Split`).
  - `BP_SEND_OBJECT_CONTENTS` (43) id: look into a container; answered with `BP_OBJECT_CONTENTS` (135): `u32` container, `u16 count`, objects. `BP_REQ_GET_FROM_CONTAINER` (240) *item*; `BP_REQ_PUT` (112) *item*, container id.
  - `BP_REQ_APPLY` (108) item id, target id: use an item on something. `BP_REQ_ACTIVATE` (109) id: work something in the room. The original's double click opens a container, else activates (`gameuser.c`).
  - `BP_REQ_INVENTORY_MOVE` (127) id, id: the first takes the second's place in `plPassive` (`UserMoveInventoryItem`); the server says nothing back.
  - Giving to someone isn't `BP_REQ_GIVE` (unused): it is an offer (`BP_REQ_OFFER`), with trade (M6).

## Combat (Kod `user.kod` UserAttack, `player.kod` TryAttack, `battler.kod` AssessHit; `clientd3d/gameuser.c`, `effect.c`, `project.c`)
- **`BP_REQ_ATTACK` (103):** `u8 kind` (`ATTACK_NORMAL` 1), `u32 target`.
  - The server decides everything. `player.kod TryAttack` checks, in order: the attack timer, the same room, line of sight, then the range from the weapon or the stroke. One attack a second (`IsOkayAttackTime`, 1000 ms); one sent sooner is dropped without a word.
  - The original client sends its exact position first (`gameuser.c` `MoveUpdatePosition`), so the range is checked from where the player stands.
  - The attack key (`UserAttackClosest`) takes the selected target if it is visible (else "You can't see your selected target.", `IDS_TARGETNOTVISIBLEFORATTACK`), or the nearest attackable object within `CLOSE_DISTANCE` (5 squares). It allows one attack every 250 ms (`ATTACK_DELAY`).
  - Kod's own refusals come as messages: "You can't reach the rat with your punch.", "You can't see your selected target." (`player_attack_not_in_view`, line of sight).
- **The swing comes back twice:**
  - **In first person,** `BP_PLAYER_OVERLAY`: the weapon's window overlay with an `ANIMATE_ONCE` (`weapon.kod WeaponAttack` → `ChangeWindowOverlay`; fists `DoWindowOverlayFistAttack`, which hides the fist again with group 0).
  - **In third person,** a `BP_CHANGE` of the attacker whose animation is `ANIMATE_ONCE` for that one message. Kod sets `piAnimation` and calls `SomethingChanged`, then sets it back.
  - **Players** (`player.kod SendAnimation`): weapon 300 ms, groups 2–4, final 1; fist 600 ms, 3–4; bow 1200 ms, 5–5. A cast, point or wave animates an arm overlay instead.
  - **Monsters:** their own attack groups.
- **Hit messages** (`battler.kod AssessHit`). The damage is only in the text. The formats are Kod resources; find their ids in the rsb by their text:
  - `battler_attacker_hit` `%sYour %s %s %s%q for ~k~B%i~B%s damage.`: colour, weapon, damage word, article, a player's name (string), damage, colour;
  - `battler_attacker_hit_mob`: the same with `%s`, a monster's name resource;
  - `battler_defender_hit` `%s%s%q's %s %s you for ~r~B%i~B%s damage.`: colour, article, name, weapon, damage word, damage, colour; `_mob` the same with a resource.
  - Misses, kills and hits too weak to hurt use other formats.
- **`BP_EFFECT` (70):** `u16 effect`, then its parameters (`effect.c PerformEffect`):
  - `i32 ms` for invert (1), shake (2), pain (7, at most 10 s), blur (8, adds up to 200 s), waver (14, adds up) and whiteout (16, at most 10 s);
  - `i32 ms, i32 xlat` for a colour flash (15). The xlat is an `XLAT_BLEND*` id: red 0x41–0x4A, white 0x70–0x79, 25/50/75% red 0x51/0x45/0x57, blue 0x52/0x55/0x58, green 0x53/0x56/0x59;
  - `i32 xlat` for an override over the whole view until 0 (17: the phase spell's white 116+, the temple's red 0x4A);
  - nothing for paralyze (3) and release (4), blind (5) and see (6), rain (9), snow (10), clear weather (11), sand (12), clear sand (13) and fireworks (18).
  - **The weather is per room:** on entering a room (`user.kod WeatherChanged`) the server sends clear weather, clear sand (unless a sandstorm spell is on the room), then the room's weather (`room.kod GetRoomWeather`: snow before rain before sand before fireworks).
  - **How the original draws it** (`d3drender.c`): pain is red fading from 80% (`min(ms, 2000) * 204 / 2000`); whiteout is white at 200/255 or more for its last half second; blind draws no world; paralysis stops walking (`move.c`).
- **`BP_SHOOT` (202):** `u32 icon`, optional translation-or-effect, animation, `u32 source`, `u32 dest`, `u8 speed`, `u16 flags`, light (`u16 flags`, then `u8 intensity`, `u16 colour` when not 0).
  - `BP_RADIUS_SHOOT` (229) has no `dest` and adds `u8 range`, `u8 number` after the flags: `number` projectiles out to `range * 1000` fine units, evenly round.
  - **Flight** (`project.c`): from the source's position to the dest's, at `speed` squares a second, gone on arrival. `PROJ_FLAG_FOLLOWGROUND` (1) keeps it on the floor. Both ends must be in the room.
- **Death** (`player.kod Killed`): the server takes the player to the Underworld, `uworld.roo` (RID 1). It is an ordinary room change (`BP_PLAYER`, room contents), after "You are dead, poor soul." and a pain effect.
  - Its archway to Raza is the portal at row 11, col 3 (`uworld.kod`). The rip in space at (10, 6) leads to a random inn.
  - **A portal (`portal.kod`) takes whoever moves within a square of it, but only on a move that starts there.** The room tells the portal before the mover's own position is updated (`room.kod SomethingMoved`), so a single jump onto it does nothing; the next step does. Walking does that anyway.

## Spells, skills and enchantments (`module/merintr`: `merintr.c`, `spells.c`, `enchant.c`, `command.c`; Kod `user.kod`)
- **Asked for once the interface loads** (`mermain.c`), and again after a data reset (`InterfaceResetData`): `BP_SEND_SPELLS` (50), `BP_SEND_SKILLS` (51), and `BP_SEND_ENCHANTMENTS` (53) with `u8 ENCHANT_PLAYER` (1).
- **`BP_SPELLS` (141):** `u16 count`, then each spell: an object (as `ExtractObject`), `u8 targets` (0 or 1), `u8 school` (1-based).
  - `BP_SPELL_ADD` (142) is one spell; `BP_SPELL_REMOVE` (143) a `u32` id.
  - `BP_SKILLS` (144) is `u16 count` and plain objects; `BP_SKILL_ADD` (145) and `BP_SKILL_REMOVE` (146) work the same way.
  - The ability percentages aren't in these; they are in stat groups 3 and 4.
- **Every character has the utility spells:** appraise (1 target), set, loadout, vault load out, meditate, phase, transference (1), conveyance (1) and blink.
- **`BP_REQ_CAST` (105):** `u32 spell`, then `u16 count` and that many `u32` targets (`protocol.c PARAM_OBJECT_LIST`; a number item would add its amount).
  - **Choosing the target** (`spells.c SpellCast`): a spell with no target goes straight away. Otherwise it goes at the selected target if it is visible or is the player (else "You can't see your selected target."). With no target, the client waits for a click on something in the room or the inventory (`GAME_SELECT`).
  - **A cast made in a trance only ends the trance** (`user.kod UserCast`, `BreakTrance`). Appraise leaves you in one, waiting for a value to be said ("Say the value you wish to assign the item."), so the next cast just breaks it: "Your concentration is broken and the appraise spell fizzles."
  - **`BP_REQ_APPLY` (108) works like a target choice:** the original always waits for a click (`gameuser.c StartApply`).
- **`BP_ADD_ENCHANTMENT` (147):** `u8 type` (1 player, 2 room), then the enchantment as an object: its spell's name and icon. `BP_REMOVE_ENCHANTMENT` (148) is `u8 type, u32 id`.
  - The room's enchantments are dropped on every new room (`enchant.c EnchantmentsNewRoom`), and the server sends the new room's. The Inn of Raza has "Safe Room" (`rmnocombat.bgf`); others have "PvP Combat Allowed".
- **Rest:** `BP_USERCOMMAND` with `UC_REST` (5) or `UC_STAND` (6), and nothing comes back.
  - Resting sets `PFLAG_NO_MOVE`, `NO_FIGHT` and `NO_MAGIC` on the server (`player.kod ResetPlayerFlagList`), so a move is snapped back.
  - The original keeps the state itself (`command.c`): "You rest." / "You stop resting.". It refuses moves, attacks and the go key while resting or paralyzed (`mermain.c InterfaceAction`), and spells with "You can't cast spells while you're resting." / "You can't lift your hands to cast the spell!".
- **Quests** are stat group 5 (`user.kod ToCliStats`): list stats, each a heading or a quest.
  - Headings have object 0: "Passive Quests: ", "No Active Quests", "Completed Quests: "...
  - Quests carry their quest object, template number and icon.
- **Retraining:** a town elder (Jasper, Marion, Ko'catan) sends `BP_STAT_CHANGE` (156; our `BP_REQ_STAT_CHANGE`) with 14 bytes: might, intellect, stamina, agility, mysticism and aim, then the levels of Shal'ille, Qor, Kraanan, Faren, Riija, Jala, weaponcraft and crafting (`user.kod SendStatChange`).
  - The client answers `BP_CHANGED_STATS` (157) with the same 14 bytes (`module/stats`).
  - The server checks each stat is 1..50, each level 0..6, the stats total at most 220, and the levels against the points spent (`UserChangedStats`). It answers `BP_CHANGED_STATS_OK` (158) or `_NOT_OK` (159).

## Trade (`clientd3d/buy.c`, `offer.c`, `gameuser.c`, `server.c`; Kod `user.kod` UserBuy, UserOffer, Offer, UserCounterOffer, UserAcceptOffer; `monster.kod`)
- **Who trades** is Kod's `viAttributes` on each NPC: `MOB_BUYER` (0x40), `MOB_SELLER` (0x80), `MOB_BANKER` (0x100), `MOB_VAULTMAN` (0x80000). `tools/kod_extract` lists them by name in `data/net/npcs.json`.
  - The client doesn't get these flags; the original just offered every command.
  - In Raza, Tomas (blacksmith) and Ravi (apothecary) buy and sell, Marcus and Eric sell, Bentu keeps the vault and Gamos banks.
- **Buying:** `BP_REQ_BUY` (124) with `u32 seller` is answered by `BP_BUY_LIST` (216).
  - The list is the seller (an object), `u16 count`, then each item (an object) and its `u32` price.
  - The original doesn't check the list's length.
  - `BP_REQ_BUY_ITEMS` (125) sends `u32 seller` and an object list: `u16 count`, the ids, a number item tagged with how many.
  - Tomas sells a helm (120), a torch (36), a mace (60), a short sword (300), a small round shield (192), chain armour (1,200) and more.
- **An offer** (`BP_REQ_OFFER` 120: `u32 to`, object list):
  - We see what we offered (`BP_OFFERED` 213: an object list).
  - The other side sees `BP_OFFER` (211): who (an object), then an object list.
  - **Their answer:** what they give back, `BP_REQ_COUNTEROFFER` (123: an object list). They get `BP_COUNTEROFFERED` (215); the offerer gets `BP_COUNTEROFFER` (214).
  - **Then the offerer** accepts (`BP_ACCEPT_OFFER` 121, nothing comes back for it) or cancels (`BP_CANCEL_OFFER` 122). `BP_OFFER_CANCELED` (212) ends it for the other side, after an accept too.
  - **Selling to an NPC buyer** is such an offer: Tomas answers a torch with 27 shillings (`BP_COUNTEROFFER`), and accepting swaps them.
- **The vault:** `BP_REQ_DEPOSIT` (230: `u32 keeper`, object list) is an offer the keeper takes at once ("That will cost 60 shillings.").
  - `BP_REQ_WITHDRAWAL` (232: `u32 keeper`) is answered by `BP_WITHDRAWAL_LIST` (231, laid out as a buy list, its prices the fees).
  - `BP_REQ_WITHDRAWAL_ITEMS` (233) takes items out.
  - A banker takes shillings by `BP_REQ_DEPOSIT` too (`user.kod UserDeposit`).
- **The bank:** `BP_USERCOMMAND` with `UC_DEPOSIT` (35) or `UC_WITHDRAW` (36) and an `i32` amount, or `UC_BALANCE` (37). The room passes it to a banker there (`SomeoneTryUserCommand`), who answers aloud ("You have 10 shilling in your account."); else "can't deposit".
- **Moving onto a door square:** a door square's middle is often in the door frame, outside every sector. The server snaps such a move back (`UserMove`, `LIR_SECTOR_INSIDE`), and "go" then finds no door. Stand on the room side of the square.

## Session (`blakserv/game.c`, Kod `user.kod`; the client side is `clientd3d/game.c`, `com.c`)
- **Leaving the game but not the server:**
  - The client sends `BP_REQ_QUIT` (54). The server logs the character off and answers `BP_QUIT` (149) (`GameProtocolParse`, `GameClientExit`).
  - The session goes back to the server's menu (`SynchedInit`). Since the account is already known, the server sends a fresh `AP_GETCHOICE` with new seeds, then `AP_CREDITS` (`SynchedDoMenu`).
  - Answer `AP_REQ_GAME` as at login: `AP_GAME` follows, then `BP_LOAD_MODULE char.dll`, and the character list.
  - The type-byte token keeps sliding through all of this; don't reset it.
  - The original client went to its main menu on `BP_QUIT` (`GameQuit`). We ask for the game straight away, so Log Off lands on the character list.
- **Server saves** (`user.kod GarbageCollecting`, `GarbageCollectingDone`, `InvalidateData`):
  - `BP_WAIT` (21), no fields: the server is saving. Object ids are renumbered, so the original clears its target (`HandleWait`).
  - Afterwards, `BP_INVALIDATE_DATA` (228) then `BP_UNWAIT` (22), both with no fields.
  - On `BP_INVALIDATE_DATA` the client forgets the room and its inventory and asks for everything again (`ResetUserData`): `BP_SEND_PLAYER` (40), `BP_SEND_ROOM_CONTENTS` (42), `BP_SEND_PLAYERS` (44), `BP_REQ_INVENTORY` (117). We also ask for the stat groups: the spells' and skills' object ids change too.
  - `BP_REQ_MOVE` carries the room object's id, and Kod ignores a move whose room isn't the player's (`user.kod`), so moves sent between the save and the new `BP_PLAYER` are lost anyway. We send none while waiting.
- **Resync doesn't work in game mode:**
  - A client that sends `BP_RESYNC` (2) puts the session in `GAME_BEACON` (`GameSyncInit`). The server then reads raw bytes until the 9-byte beacon `1 255 66 76 65 75 10 13 2` arrives (`resync.c beacon_str`).
  - `GameSyncInputChar` compares a plain `char` (signed in both the MSVC and Linux builds; neither makefile passes `/J` or `-funsigned-char`) with the `unsigned char` beacon. Byte 255 reads as -1, so the match restarts at the second byte every time.
  - The session stays in beacon mode until it times out. We tried it against the local Shards server on 2026-10-08: no reply in 60 s.
  - So the client never sends `BP_RESYNC`. A `BP_RESYNC` from the server (it couldn't read us: `GameSendResync` sends ten) ends the session too.

## The server's game files (the Shards site, `<site>/assets/`)
- **`manifest.json`:** `{generated, rsbHash, files: {name: {size, hash, mtime}}}`. Names are lower case with no folders. The local stack lists 4,766 files (`.roo`, `.bgf`, `.ogg`, `.wav`, `.bsf`, ...).
- **`hash`** is the first 16 hex digits of the file's SHA-1, lower case; `rsbHash` is the same for `rsc0000.rsb`.
- **Fetching:** `<assets>/<name>?v=<hash>`. The query string only defeats HTTP caches.
- `Net/MRAssetCache` downloads on demand into `Saved/MRNet/<server>/assets/` and checks every file against its hash.

## The resource file (`util/rscload.c`)
`rsc0000.rsb` starts with `"RSC\x01"`, `i32 version` (5) and `i32 count`. Each entry is `i32 id`, `i32 language` and a NUL-terminated string. Language 0 is the default text.

On Shards it has 34,811 entries, 15,467 of them in language 0.
