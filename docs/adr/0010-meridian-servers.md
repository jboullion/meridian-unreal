# ADR 0010: Playing on Meridian servers

- Status: Accepted
- Date: 2026-10-07
- Supersedes: the server, accounts, zone-change and combat parts of [ADR 0001](0001-engine-and-architecture.md) (decisions 2, 4, 6 and 7) and the game-server and Supabase parts of [ADR 0004](0004-hosting-and-operations.md)

## Context
Until now the remaster only ran locally: a UE dedicated server (or a standalone game) with its own spawning, no accounts and nothing saved.

Our browser and desktop port, **Meridian Shards** (`E:\2026_Experiments\meridian-browser`, GPLv2), already has a working backend, locally and on a VM:
- **The game server** is the unmodified Server 104 `blakserv`. Its game is the original Kod, and it has accounts and saves.
- **A WebSocket gateway** forwards the TCP byte stream unchanged.
  - Locally: `ws://localhost:8059`.
  - On the VM: `wss://35-206-75-121.sslip.io/ws`, behind Caddy. The VM checks the `Origin` header against `GATEWAY_ORIGINS`.
- **The server's game files** (`manifest.json`, `rsc0000.rsb`, rooms, bitmaps) are served over HTTP at `<site>/assets/`.
- **Accounts:** none of its own. blakserv's `AP_LOGIN` creates the account the first time an unknown name logs in.

TODO.md already set the goal: the Unreal game should be "a full client build for any Meridian server". The user chose this direction on 2026-10-07.

## Decision

1. **The remaster is a client of Meridian servers.** blakserv is the authority; we don't run our own UE game server.
   - The UE dedicated-server path stays only until online play has replaced it (see Follow-up).
   - A standalone game started with `-MROffline` keeps local play for the visual tours (look-dev, UI shots, minimap captures, sprite and monster tours). The tours imply it automatically.
2. **We implement the protocol ourselves, in C++.** It is written from reading `ReferenceServers/Server-104/blakserv` (allowed: read and re-implement) and from our own notes in [docs/research/blakserv-protocol.md](../research/blakserv-protocol.md).
   - **Shards' TypeScript is GPLv2. Never copy or translate it into this repo.**
   - The protocol facts (message ids, layouts, the security arithmetic) aren't code, and our notes cite blakserv for each.
3. **Transport: WebSocket through the gateway**, using UE's `WebSockets` module with binary frames.
   - We send the Origin `app://meridian-remastered`. The VM's `GATEWAY_ORIGINS` has to list it; the local gateway allows any origin.
   - TLS comes from the site's `wss://`. The login sends an unsalted MD5, so it must not go over plain TCP across the internet.
4. **The server's own resources.** Before connecting, the client downloads `rsc0000.rsb` from the server's `assets` URL.
   - It is cached in `Saved/MRNet/<server>/` and fetched again when `manifest.json`'s `rsbHash` changes.
   - The security redbook and every name (rooms, bitmaps, messages) come from it, so it must be that server's build.
   - Our rooms are the same `.roo` files as the server's (checked by hash for `raza.roo` and `razainn.roo`).
5. **The server decides where you are; UE draws it.**
   - **Rooms:** a room from the server (`BP_PLAYER` + `BP_ROOM_CONTENTS`) is matched to our zone by its `.roo` file (`data/zones.json` `roo`). The pawn is spawned or moved to the server's position.
   - **Movement:** the pawn walks locally, as the original client does. Its Kod position goes up as `BP_REQ_MOVE` every 250 ms while it changes, and its facing as `BP_REQ_TURN`.
   - **Exits:** standing on an exit square sends `BP_REQ_GO`. Walking off a room's edge keeps asking to move off it (once a second, at walking speed). The server answers with the next room. If the server snaps us back, the pawn follows.
   - **Others:** other players, monsters and NPCs are `AMRNetObject` sprites.
     - Their look is matched by name, then by body bitmap, against `data/sprites` (`m_<KodClass>`).
     - Players get the default look for now.
     - They walk to each `BP_MOVE` at the server's speed and never block the player.
   - **Off while online:** local monster and NPC spawning (`UMRMonsterSubsystem`) and local exits (`UMRZoneSubsystem::UpdatePawnZone`).
6. **A login screen in the game's own UI** (`SMRLoginScreen`, styled like the inventory dialog of [ADR 0009](0009-user-interface.md)).
   - **Login:** the server, login name and password. An unknown name makes the account.
   - **Characters:** characters and empty slots, Play and Log Off. The server's message of the day sits in a "News" column to the right of the list, and the window widens for it, so a long message never squeezes the list.
   - **Create:** name and gender only. The face, stats, spells and skills are the server's defaults. The full creator comes later.
   - **Behind it:** the camera looks over Raza (the look-dev bookmark `square_overview`) and there is no pawn until the server puts the character in a room.
   - **Stored:** the last server and login name, in `GameUserSettings.ini` `[MR.Net]`. The password is never kept.
7. **Chat:** a log in the HUD (`SMRChatLog`) shows the server's speech and messages, formatted from the rsb. Enter types a line, which is sent as `BP_SAY_TO`.
8. **Servers are data:** `data/net/servers.json` holds the name, `ws`, `assets` and `secret_key` (blakserv's `[Login] SecretKey`, which isn't a secret: every client ships it).

## Consequences
- **Gameplay is the original's:** Kod's rules, combat, spells, shops and saves. Action combat (ADR 0001 decision 7) is on hold, and so are seamless UE-side zone changes and Supabase accounts.
  - The server runs on a grid. Our pawn can still move freely and look modern, but the server is the judge: it rejects positions outside a room's sectors and has the final say on fights.
- **Art and rooms must match the server's build.** A room we haven't built (any RID outside the demo) leaves the player standing where they were, with a warning. A creature without a converted sprite isn't drawn yet.
- **The UE dedicated server, Iris replication of players and the multiplayer PIE tests** become legacy. They are removed in a separate change once nothing depends on them.
- **Hosting is Shards':** the VM runs blakserv, the gateway and the site (ADR 0004's VPS and Supabase plan is dropped for now). One server serves the browser client, the desktop app, original Windows clients and this game.

## Verification (2026-10-07, local Shards stack)
- `Meridian.Net.Protocol` and `Meridian.Net.Resources` automation tests pass. They cover framing, CRC, the security word, the redbook token, the rsb and message formatting.
- `tools/ue/run_net_test.ps1`: DONE 5/5, headless and rendered (`-Render`). The steps:
  1. log in, making the account and a character;
  2. enter `razainn.roo` as zone 301 at the server's spawn point;
  3. say a line and hear it back through `BP_SAID`;
  4. step onto the Inn's door square and arrive in `raza.roo` (zone 300, 81 objects);
  5. log off.
- **Cross-client:** a Shards browser character in the Inn saw the Unreal player appear, speak and walk. The Unreal client drew the browser player and Marcus the innkeeper from the server's objects (`build/net/shards_sees_unreal.jpg`, `build/net/unreal_in_inn.png`).
- **Login screen pages:** `build/ui/shots/login/` (`run_ui_shots.ps1 -Label login`).

## Alternatives considered
- **Keep our own UE server and Supabase (ADR 0001):** action combat and full freedom, but every rule, quest, shop and save of the original to rewrite first, and no players to share a world with.
- **A UE server as a bridge to blakserv:** keeps UE replication for clients, but adds a second authority and a whole server to run. It gains nothing until UE-side gameplay exists.
- **Raw TCP to blakserv (port 5959):** no gateway change, but the login's unsalted MD5 crosses the internet in the clear, and the VM's firewall would have to open the port.
- **Port Shards' TypeScript protocol code:** fastest, but it is GPLv2 and this repo is not.

## Follow-up work
- **The VM:** add `app://meridian-remastered` to `GATEWAY_ORIGINS` in the VM's `deploy/.env` and restart the gateway. Until then "Shards (online)" gets 403.
- **Retire the UE-server path:** remove the dedicated-server spawn wait, `ClientPrepareZone` and `IsZoneReadyFor`, `MRZoneSmokeTest` / `run_zone_test.ps1`, `MRSpriteNetTest` / `run_sprite_net_test.ps1`, and the "Play As Client" instructions.
- **Next features:**
  - player looks from the server's overlays (body, head, hair, colours);
  - the full character creator;
  - stats, inventory and spells from the server (the inventory dialog's `UMRInventorySource` seam);
  - combat;
  - sounds and lighting messages;
  - room checksums.
