# ADR 0011: New content on our own server

- Status: Proposed (planning only; nothing implemented)
- Date: 2026-10-08
- Builds on: [ADR 0010](0010-meridian-servers.md)

## Context
The game plays on an unmodified Server 104 `blakserv` (ADR 0010). Next we want a server of our own, where we can add and remove features: new rooms, a new spell school, new items and new monsters. This record lists what each costs, on the server and in this client, so the work can be planned before any of it starts.

The client facts below were read from this repo, with paths. The server-side facts come from `data/*.json` (its `file` fields show the Kod layout) and from the open-source Meridian 59 tree in general. They were written without the `ReferenceServers/Server-104/` checkout to hand. Mark each one as checked, with its Kod file, in [blakserv-protocol.md](../research/blakserv-protocol.md) or a new research note before relying on it.

### The server sends names, never content
- **What the server sends:** the room's `.roo` file name, an object's name and bitmap name (rsb strings), spell and skill names (stat groups 3 and 4), and text.
- **What the client downloads:** only `rsc0000.rsb` (`Net/MRNetSubsystem.cpp:343-425`).
- **Everything else is local:** geometry, sprites, icons, schools and rules. Our tools build it from a Kod tree (`tools/kod_extract/extract.py` into `data/*.json`) and from art made offline.
- **The client matches the server's content to it by name:**
  - **Rooms:** by `.roo` file (`Zones/MRZoneSubsystem.cpp:715`, `RidForRoom`).
  - **Creatures:** by `vrName`, then by bitmap name (`Net/MRNetWorldSubsystem.cpp:293-340`, `LookFor`).
  - **Spells and skills:** by `vrName` (`UI/MRUISubsystem.cpp:580-630`).
- **Gaps fail quietly:**
  - A room we haven't built leaves the player where they were, with a message.
  - A creature without a sprite isn't drawn.
  - A spell we don't know is dropped, with only a log warning.

So every new feature has two halves:
- **On the server:** Kod and resources in our own fork.
- **In the client:** our pipeline run over that fork, plus a few places that are hardcoded today.

C++ paths are under `game/UnrealMeridian/Source/UnrealMeridian/`.

## Decision (proposed)

1. **Our own server tree, kept out of this repo.**
   - **Fork:** a GPLv2 fork of the Server 104 tree, in its own repo, treated like `ReferenceServers/Server-104/`. We publish our Kod changes there under GPLv2, and never copy its code into this repo.
   - **Build:** `bc` (blakcomp) compiles Kod into `.bof` files and `.rsc` strings, and `rscmerge` merges the strings into `rsc0000.rsb`.
   - **Makefiles:** each Kod directory's makefile lists its classes, so a new class is added there.
   - **`kodbase.txt`:** it maps names to IDs and saved games depend on it, so it is kept in the fork and never rebuilt from scratch.
   - **Serving:** the fork's `rsc0000.rsb` and `manifest.json` (`rsbHash`) are served from the server's `assets` URL. The client already fetches the rsb again when the hash changes (ADR 0010 decision 4).
   - **Server list:** a "Custom (dev)" entry in `data/net/servers.json`, with its own `ruleset` (today that field only picks `data/ui/stat_layout.json`).
2. **One setting points every tool at the fork.**
   - Today the tools find the Kod through `tools/server104.py`. Some accept `--kod` or `--rooms`; `bgf2png.py --textures-for-zones` doesn't (:191).
   - Add an env var or `--server-root` that `tools/server104.py` and every tool honour.
3. **The client reads rules from data, not switches.** Before new content, move these into data:
   - **School names:** `UMRGameDataSubsystem::SchoolName` is a switch (`UI/MRGameData.cpp:196-211`), so a new school shows as "Other". Read them from `constants.json` (`SS_*`, `SKS_*`) or a ruleset file.
   - **Opposing schools:** the Shal'ille/Qor exclusion is hardcoded (`Net/MRCharInfo.h:86-87`, `Net/MRCharInfo.cpp:292-303`, `UI/SMRCharCreator.cpp:606-628`).
   - **Stat groups:** the spell and skill stat-group numbers 3 and 4 are fixed in `UI/MRUISubsystem.cpp:587`.
   - **Extraction scope:**
     - Rooms come from `DEMO_RIDS` (`tools/kod_extract/extract.py:29`).
     - Items are filtered by family (`extract.py:450-462`: weapons, armour, rings, necklaces, and items the demo references).
     - Monsters come only from the demo rooms (`extract.py:484-493`).
     - Replace all three with a content list in data, or "everything in the fork".
4. **Missing content shows in play, not just the log.**
   - **Unknown creatures** get a placeholder sprite.
   - **Unknown spells** use the icon the server already sends (`FMRNetStat::Icon`, parsed at `Net/MRNetSubsystem.cpp:619` but unused).
   - **Rooms** check the room checksum from `BP_PLAYER` (an ADR 0010 follow-up). Our `.roo` must be byte-identical to the server's, and today that is checked by hand.
5. **Order of work.** Each step reuses original art until the end:
   1. Fork, build and run the server unchanged. It must pass `tools/ue/run_net_test.ps1`.
   2. A monster that reuses an existing sprite, spawned in the Outskirts.
   3. An item sold by an existing shopkeeper.
   4. A sandbox room: a copy of an existing `.roo` under a new name and room ID, with an exit from the Inn.
   5. A spell in an existing school, then the new school.
   6. New art: textures, creature sprites, item bitmaps.

## What each feature takes

### A new room (the most work)
**Server**
- **Geometry:** the `.roo`, made in the original Room Editor (`roomedit`, Windows). New wall and floor textures are `grd*.bgf` files in the original 256-colour palette, made with `makebgf`.
- **Kod room class** under `kod/object/active/holder/room/`. It sets:
  - `prRoom` (the `.roo`) and `piRoom_num` (a new `RID_*` constant);
  - music, `piBaseLight` and `ROOM_*` flags;
  - exits (row, col, destination RID, destination row and col);
  - monster spawns (`plMonsters`), generators and placed objects.
- **Make it reachable:** register the room where `system.kod` creates rooms, and add an exit into it from an existing room. That edits the existing room's exits.
- **Ship the files:** the `.roo` goes to the server's rooms folder (blakserv uses it for movement and line of sight) and to every client. Its file name ends up in the rsb.

**Client**
- **Room ID:** add it to the extraction scope (`DEMO_RIDS` today). Use IDs above 333: `roo2gltf.py:672-689` lays zones out on a 2 km grid in room-ID order, so a lower ID moves every later zone's `world_origin_cm` and breaks the look-dev bookmarks and build caches.
- **Pipeline:** the zone-environment skill's steps, in order:
  1. `extract.py`
  2. `bgf2png.py --textures-for-zones`
  3. `roo2gltf.py --preview`, without `--rid`, so `zone_layout.json` is written
  4. optional art: `zone_<rid>.json`, `build_zone_art.py`, props, moods, `extract_audio.py`
  5. `build_world.ps1`
  6. the minimap capture
- **Sandbox shortcut:** a copied `.roo` needs no new art. `roo2gltf.py:653-670` may treat it as shared geometry with the original (equal extents). That is fine for a sandbox.

### A new spell school
Schools are only an integer, `viSchool`:
- **Spells (`SS_*`):** 1 Shal'ille, 2 Qor, 3 Kraanan, 4 Faren, 5 Riija, 6 Jala, 7 Crafting, 8 DM commands.
- **Skills (`SKS_*`):** 10 Fencing (shown as Weaponcraft), 11 Brawling, 12 Thievery.
- **Free numbers:** 9 and 13 up.

**Server.** The work is the rules, not the data:
- **The constant:** add `SS_<NEW>` to the constants header.
- **Find every place schools are listed:** grep the fork for `SS_JALA` and `SS_RIIJA`; every hit is a checklist item. Expect:
  - school levels and spell power;
  - who may learn what, and opposing schools (karma, as with Shal'ille and Qor);
  - what character creation offers;
  - trainers;
  - any formula that depends on the school.
- **Spells:** classes under `kod/object/passive/spell/<school>/`. Each has `viSpell_num` (a new `SID_*`), `viSchool`, `viSpell_level`, `viMana`, reagents, and cast and target handlers.
- **Learning:** register the spells in the system's spell list, and teach them from an NPC trainer (DM commands while testing).

**Client**
- **The school's name:** decision 3.
- **The opposing-school rule:** decision 3, needed only if the new school has an opposite.
- **Spell data:** re-run `extract.py` on the fork, then `tools/ui/build_icons.py` and `tools/ue/import_ui.ps1`. A spell's school, level, icon and description all come from local `spells.json`, matched by name.
- **Spell page:** the inventory dialog groups spells by school number already (`UI/SMRInventoryScreen.cpp:640-680`), so it needs only the name.
- **Blocked:** casting online is still a mock (`UI/MRUISubsystem.cpp:274-283`; no cast message in `Net/MRProtocol.h`). Until it lands, test new spells in the Shards browser client.

### New items (the smallest server change)
**Server**
- **Item class** under `kod/object/item/`. It has:
  - `vrName`, `vrIcon`, `viWeight`, `viBulk`, `viValue_average`;
  - bitmap groups for the ground, the inventory and the broken item;
  - weapon or defence stats;
  - its use and effect handlers.
- **Makefile:** add it to its directory's makefile.
- **Making it obtainable:** a shopkeeper's for-sale list, a treasure table (a monster's `viTreasure_type`), a room's placed objects, or DM create.
- **Art:** a bgf with inventory and ground frames. A weapon also needs its first-person overlay (`vrWeapon_window_overlay`) and its on-body overlay (`vrWeapon_overlay`).

**Client**
- **Extraction:** widen the scope (decision 3), ideally to every `Item` subclass in the fork.
- **Icon:** `build_icons.py` picks up the new `vrIcon`. `IconFor` (`UI/MRUISubsystem.cpp:451-465`) then maps class to `vrIcon` to `T_Icon_<stem>`.
- **Blocked:**
  - There is no online inventory yet: the dialog runs on `UMRMockInventory`.
  - Items on the ground aren't spawned online (`Net/MRNetWorldSubsystem.cpp:332`).
  - Until then, test items in the Shards client. To check an icon and tooltip offline, add the item to `data/ui/mock_inventory.json`.

### New monsters
**Server**
- **Monster class** under `kod/object/active/holder/nomoveon/battler/monster/`. It has:
  - `vrName`, `vrIcon`, `vrDead_icon`;
  - `viLevel`, `viDifficulty`, `viKarma`, `viSpeed`, `viAttack_type`;
  - `viDefault_behavior` (`AI_*` flags) and `viTreasure_type`;
  - sounds;
  - the animation handlers (`SendMoveAnimation`, `SendAnimation`) that pick bitmap groups.
- **Spawning:** add it to a room's `plMonsters` and generators.
- **Art:** a directional creature bgf in the original group layout, a dead bgf, and `.ogg` sounds.

**Client**
- **Extraction:** widen the scope (decision 3).
- **Sprite pipeline:**
  1. `tools/sprites/monsters.py` reads the animation handlers out of the Kod.
  2. `tools/sprites/build_player_sprites.py`.
  3. `tools/ue/import_sprites.ps1`.
- **Reusing a sprite:** a monster that uses an existing bitmap (a tougher rat) already draws online, through the bitmap fallback.
  - **Recolours:** a recoloured variant looks like its base creature, because palette changes apply only to players (`Net/MRNetLook.cpp:50-53`). Lift that limit if variants matter.
- **Blocked:** combat isn't online yet. A new monster can be seen but not fought in this client.

## Consequences
- **Shared tooling:** the same tools build data for Server 104 and for our fork, and `ruleset` in `servers.json` picks which data the client uses.
- **Client changes come first:** the server root, rules in data, and visible gaps (decisions 2–4) are small. Without them, each new feature is a silent gap in play.
- **The ADR 0010 follow-ups gate the testing:** items, spells and monsters can't be fully exercised in this client until online inventory, casting and combat land. The Shards browser client covers that meanwhile.
- **Art permission:** the Server 103 and 104 teams allowed the original art on condition that we run our own server (ADR 0001). A server of our own meets that more plainly than sharing Shards' server. New art we make is ours. "104" still never appears in the product name.

## Open questions
- **The Shards stack:** how does it build and run `blakserv` and its Kod? Can it load our `.bof` files and rsb, or do we need our own stack and VM?
- **Upstream:** do we keep taking Server 104's changes into the fork?
- **Other clients:** must the Shards browser client and the original Windows client also play on our server? If so, new rooms and art ship in their formats too.
- **Saves:** is our server a fresh world? If so, saves and `kodbase.txt` don't need to stay compatible with Server 104's.

## Verification (when work starts)
- **Check the server-side facts** against `ReferenceServers/Server-104/kod`, recording each one with its Kod file:
  - the makefiles and `kodbase.txt`;
  - where `system.kod` registers rooms and spells;
  - every use of `SS_*`.
- **Step 1:** the unchanged fork passes `tools/ue/run_net_test.ps1` against "Custom (dev)" (`DONE 5/5`).
- **Each later step:**
  - online, the new thing appears in this client, and in the Shards client for items and spells;
  - nothing new shows up in the "not in our data" or "no sprite" warnings.

## Alternatives considered
- **Change content only on the client:** impossible. The server is the authority for every room, object and rule (ADR 0010).
- **Have the server send content (geometry, sprites, rules) to the client:** it would remove the local pipeline, but means a protocol extension only our client understands. It also gives up the remastered art, which is built offline.
- **Modify Shards' server in place:** quickest, but it mixes our experiments into the world the browser players share.
