# The blakserv protocol

These are our own notes on the Meridian 59 client/server protocol, written from reading `ReferenceServers/Server-104` and checked against a running server ([ADR 0010](../adr/0010-meridian-servers.md)).

The implementation is `game/MeridianRemastered/Source/MeridianRemastered/Net/`. Paths below are relative to `ReferenceServers/Server-104/`.

Never copy or translate code from Server-104 or from Meridian Shards (both GPLv2). Read them, write the facts down here, and implement from this page.

## Transport
- **TCP to blakserv:** port 5959.
- **Over a WebSocket:** the Shards gateway passes the byte stream through unchanged. It does not re-frame: one WebSocket message can hold part of a frame or several frames.
- **The server speaks first.** On a fresh connection it sends `AP_GETLOGIN`. The beacon handshake (`BP_RESYNC`, `blakserv/game.c GameSyncInit`) is only for recovering a broken stream. We don't implement it; a broken stream ends the session.
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
- **Creating one:** `BP_SYSTEM` (6) followed by `BP_NEW_CHARINFO` (48) (`sprocket.c system_def_table`; `module/char/char.c` shows the client side). The fields:
  - `u32 slot`;
  - `string name`, `string description`;
  - `u8 gender` (1 male, 2 female);
  - `u16 n` + n × `i32` face-part resources (none: the defaults);
  - `u8 hair colour`, `u8 skin colour`;
  - `u16 6` + six `i32` stats (we send 35 each);
  - `u16 n` + `i32` spells, `u16 n` + `i32` skills.
- **The answer:** `BP_CHARINFO_OK` (56) `u32 id`, after which the client sends `BP_USE_CHARACTER`. Or `BP_CHARINFO_NOT_OK` (57), for example when the name is taken.

## In the game (`clientd3d/server.c` shows what the client expects)
- **`BP_PLAYER` (130)** fields:
  - `u32 id`, `icon`, `name`, `room object`, `room file resource` (e.g. `razainn.roo`), `room name resource`;
  - `u32 room security` (the room checksum);
  - `u8 ambient`, `u8 light`;
  - `u32 background`, `wading sound`, `room flags`, depth 1–3.

  A room change is a new `BP_PLAYER` followed by `BP_ROOM_CONTENTS`.
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
- **A room object** (`ExtractNewRoomObject`) is an object followed by:
  - `u16 row`, `u16 col` (Kod position: `square * 64 + fine`, so the room's top-left edge is 64);
  - `u16 angle` (0–4095, 0 = east, increasing toward south);
  - its motion state: palette prefix, animation, overlays.
- **`BP_ROOM_CONTENTS` (134):** `u32 room object`, `u16 n`, n room objects.
- **Objects coming and going:** `BP_CREATE` (217) is a room object; `BP_REMOVE` (218) is `u32 id`; `BP_CHANGE` (219) is an object, a palette prefix, an animation and overlays.
- **Movement from the server:**
  - `BP_MOVE` (200): `u32 id`, `u16 row`, `u16 col`, `u8 speed`. Bit 7 of the speed means "turn to face the move".
  - `BP_TURN` (201): `u32 id`, `u16 angle`.
- **Moving the player** (the client's `move.c`):
  - `BP_REQ_MOVE` (100): `u16 row`, `u16 col`, `u8 speed` (walk 25, run 55), `u32 room object`. The original sends one about every 250 ms.
  - Moving off the room's edge is a request to leave; send it at most once a second, at walking speed.
  - The server rejects only destinations outside the room's sectors, by sending `BP_MOVE` back.
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
- **Names:** `BP_CHANGE_RESOURCE` (30) is `u32 id`, `string` and adds a name at runtime. `BP_PLAYERS` (136) and `BP_PLAYER_ADD` (137) carry player names the same way.
- **Leaving:** `BP_REQ_QUIT` (54) leaves the game; the server answers `BP_QUIT` (149).

## The resource file (`util/rscload.c`)
`rsc0000.rsb` starts with `"RSC\x01"`, `i32 version` (5) and `i32 count`. Each entry is `i32 id`, `i32 language` and a NUL-terminated string. Language 0 is the default text.

On Shards it has 34,811 entries, 15,467 of them in language 0.
