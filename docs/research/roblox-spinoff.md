# Research: a Roblox spin-off inspired by Meridian 59

- Status: Direction decided (see "Decisions" below); vertical slice next
- Date: 2026-10-06
- Working title: **Fantasy Blocks** (placeholder)

## Decisions (2026-10-06)

1. **Our own IP.** Working title "Fantasy Blocks". It's inspired by Meridian's mechanics and feel, with no Meridian names, art, music, maps or code. The world is built around **towns**: you travel town to town, and towns are where people hang out. Each town has one or more **dungeons** where many players fight at once. Each dungeon has a **boss that takes teamwork**: puzzles, keys and mechanics, not just damage.
2. **Our own characters.** A custom character system with a medieval-fantasy look influenced by Meridian. Cosmetics (hair, dyes, outfits, emotes, décor) are sold as Developer Products and Game Passes. Armour and weapons are earned in game only.
3. **No PvP at launch.** Guilds, factions and arenas may add PvP later. **Death costs no equipment.** You wake at the nearest town's inn.
4. **A separate repo.** It's an entirely new game.
5. **A parallel experiment.** Both projects run until each has a one-town vertical slice that proves the concept. Then decide.

## The idea

A separate, simplified game on Roblox that takes heavy inspiration from Meridian 59: lots of small zones, towns built for hanging out, the original's skill-and-spell progression, and occasional content updates that grow the world the way the original did. It would have its own name and branding. It would make money from cosmetics only (clothing, hair, customization). Armour and weapons stay in-game items you earn.

The Unreal remaster continues as the faithful port. The spin-off is a different product.

## What Roblox solves for us

| Problem in the Unreal project | On Roblox |
|---|---|
| Hosting a dedicated server, scaling, costs ([ADR 0004](../adr/0004-hosting-and-operations.md)) | Roblox hosts and scales servers for free. |
| Accounts and auth (Supabase, JWTs) | Every player already has a Roblox account. |
| Persistence | DataStoreService, shared across every place in an experience. MemoryStoreService for cross-server state (parties, guild chat, world events). |
| Networking and replication | Built in. The server is authoritative by default if we write it that way. |
| Finding players (the low-population problem) | Discovery, friends joining friends, and a huge player base. It's still a competitive charts market, but people can actually find the game. |
| Distribution and payments | Instant on PC, phone, tablet, console and VR, and Robux handles all payments. |
| Moderation of chat | Roblox text and voice filtering and age checks (see the limits below). |

## What it costs us

- **We rebuild everything.** It's Luau, not C++, and Roblox's renderer, not UE's. Almost nothing in `game/` carries over.
- **The look has a ceiling.** Meshes, textures and lighting are limited compared with UE 5.8. Assume about 20k triangles per mesh and 1024 px textures (check the current limits). Lots of devices are phones. "Mid-poly, faithful remaster" turns into stylized and readable.
- **Platform rules we don't control:**
  - Content and age ratings (Content Maturity labels)
  - Mandatory age checks for chat since January 2026, which put players in age bands that limit who can chat with whom
  - Moderation of every uploaded asset
  - Changing payout rules
- **Server size.** Servers commonly cap at 50–100 players. Larger servers have been possible (devforum reports of 200 and tests of up to 700), but performance gets hard. That's fine for Meridian's scale: one town server with 50–100 people is lively.
- **Audience.** Roblox skews young. A PvP-heavy, death-penalty Meridian needs softening (see the design notes below).
- **Revenue share is low.** Developers keep roughly a quarter to a third of what players spend, after the platform cut and the DevEx rate (about $0.0035 per Robux in early 2026).

## Legal and IP: the most important part

This needs settling before any public build, especially because the game would make money.

- **"Meridian 59" is a trademark, and the art and audio were never open-sourced.** The 2012 release put the *code* under GPLv2 and left out the content. The Server 103 and 104 teams' permission covers our non-commercial remaster, under a different name, on our own server. **It doesn't obviously cover a monetized Roblox game.** Ask them, and ideally the copyright holders (Andrew and Chris Kirmse), before using anything original.
- **What's safe to take:** game *ideas* and mechanics. Small zones, towns as social hubs, skills and spells that improve with use, guilds, schools of magic, a newbie island, death that costs you something, safe and unsafe zones. Mechanics aren't copyrightable.
- **What's not safe without permission:**
  - The name, place names (Raza, Tos, Barloque, Marion, Jasper, Cor Noth), NPC names and the lore text
  - The original art, sprites, textures, music and sound
  - Close copies of the maps
- **The GPL trap.** Porting Kod source line by line into Luau makes the result a derivative work of GPLv2 code. The whole Roblox game would then have to ship under the GPL. Avoid that by writing the *design* down (what a formula does and why) and implementing from that write-up, not from the Kod. Rebalance the numbers instead of copying the tables.
- **Recommendation:** an original setting with its own names, art and music, built to *feel* like Meridian: "a small, dangerous world of towns and roads, where people hang out." Use the original's map layouts as private greybox references, then redesign them.

## Design (as decided)

### World: towns, roads and dungeons

- **One experience, many places.** Players move between places with `TeleportService`. Every place shares the same DataStores, so the character follows you.
- **A town place** holds the town, its interiors and its nearby countryside (a few small areas joined by gates and doors, Meridian-style). Towns are where people gather, so a town server should be big (50–100 players).
- **A dungeon place per dungeon,** with its own servers, so a dungeon's population doesn't crowd the town. "Many players fighting at once" means one dungeon server holds a full crowd (aim for 20–40). The boss room is the shared goal.
- **Travel town to town** along road places (short, dangerous, the Meridian feel) or, later, a paid carriage or portal for fast travel. Teleports take a few seconds behind a loading screen, so a trip is a handful of them, not dozens.
- **Small scale.** Each town is a compact hub, like Meridian's, not a Warcraft city. New towns (with their dungeons) are the content updates.

### Towns are the product

- Dense, social places: an inn (respawn point and fireplace), tavern, bank, shops, trainers, a notice board, benches and a stage.
- Things to do while hanging out: emotes and sitting, fishing, dice or cards, a training dummy, a town crier for events.
- Creator Rewards pay for qualified time played, so a good hangout town earns even from players who never buy anything.

### Dungeons and bosses

- **Shared dungeons:** everyone on the server fights the same monsters (no per-party instances in the slice), which makes them busy and social.
- **Bosses need teamwork, not just damage:**
  - Keys and switches that open the boss room or turn off its shield (found by different players in different wings).
  - Phase mechanics that need several people at once, like holding plates, carrying a relic, or splitting up to break crystals.
  - Readable attacks to dodge (telegraphs that work on phones).
- **Rewards:** loot goes to everyone who took part (personal drops, no stealing), plus a boss-specific cosmetic *earned*, not bought, for status.
- **Respawn timer** on the boss, so a server always has a reason to gather.

### Progression (inspired, simplified)

- **Stats:** a few, set at character creation, with a cheap respec.
- **Skills and spells:** learned from town trainers, improving with use (the best idea in the original). Start with about 3 schools: weapons, a "light" school and a "shadow" school.
- **Gear:** armour and weapons are earned only, never sold. They're dropped by monsters and bosses or crafted.
- **Death:** no equipment loss. You wake at the nearest town's inn. A small cost (a few seconds, or durability) keeps danger meaningful.
- **PvP:** none at launch. Guilds, factions and arenas come later and may add PvP then.
- **Guilds** (after the slice): guild halls and ranks, a social anchor carried over from Meridian.

### Characters and cosmetics

Our own character system, not Roblox avatars: a body, face, hair, skin and dye creator with a medieval-fantasy look. The armour you equip shows on the character, and cosmetics change the *look* only.

### Social and safety

- **Chat.** Chat is age-gated per player since January 2026, so plan for players who can't chat with each other. Use emotes, a quick-chat wheel, pings and markers (vital for boss teamwork), and readable non-verbal play.
- **Guides and moderators.** A moderator role with tools (teleport to player, mute, kick, start events).

## Selling on Roblox: a short primer

- **Robux** is the only currency. Players buy it with real money; we earn Robux and cash it out through **DevEx** (Developer Exchange), at about $0.0035 per Robux in early 2026. DevEx has eligibility rules (age 13+, a verified account, a minimum Robux balance, good standing); check the current terms when it matters.
- **Game Passes:** a one-time purchase that Roblox remembers for you (`MarketplaceService:UserOwnsGamePassAsync`). Good for permanent perks: an extra character slot, an extra bank tab, a cosmetic wardrobe slot.
- **Developer Products:** buy any number of times; *we* record what was bought in our DataStore. Every cosmetic item (a hairstyle, a dye, an outfit, an emote) is a developer product, and our shop UI calls `MarketplaceService:PromptProductPurchase`. The server handles `ProcessReceipt`. It must be idempotent and save before returning `PurchaseGranted`, or players can lose purchases or get duplicates. That's the one piece of money code to get exactly right and test.
- **Creator Rewards (engagement payouts):** Roblox pays for time that eligible players spend in the game. That's no work for us beyond making a place people stay in.
- **Our cut:** after Roblox's share and the DevEx rate, developers take home roughly a quarter to a third of what players spend.
- **Rules to respect:**
  - Never sell power. That's our design rule, and loot boxes with paid random rewards are restricted on Roblox anyway.
  - Show prices clearly.
  - Follow the age and content rules (an experience questionnaire sets the content-maturity label).
- **Trading:** if we add player trading later, it must stay inside the game and can't involve Robux or anything outside the game.

## Reusing work from the remaster

| Asset | Reuse |
|---|---|
| `tools/kod_extract`, `data/*.json` | **Design reference only.** Read how monsters, spells and skills worked, then rebalance. Don't ship the numbers or code (GPL, see above). |
| `tools/roo2gltf` blockouts, `docs/findings.md` | Scale references: how big a Meridian town and its zones really are. Not layouts to copy. |
| Blender tooling, AI prop pipeline (`sprite-to-3d` skill) | The workflow transfers (Blender to FBX to Roblox MeshParts). The assets are new: our own designs, not restyled originals. |
| Our habits (ADRs, skills, AGENTS.md, text-first data) | Directly. Rojo keeps code and data as files in git. |

## Tooling

- **Rojo:** syncs a git repo of Luau files and JSON/TOML data into Studio. It's text-first, works with git, and suits AI assistants. Manage tools with **Rokit** (or Aftman).
- **Luau with strict types**, plus Wally for packages, `selene` and `StyLua` for lint and format, and Jest-Lua or TestEZ for tests.
- **Roblox Studio** for building levels and meshes, with Studio's assistant or MCP server for AI-driven edits.
- **Open Cloud APIs** for publishing places and managing DataStores from scripts, which enables CI publishing.
- **Analytics:** Roblox's built-in analytics (retention, funnels, economy events).

## Vertical slice: one town

A slice that matches the Unreal remaster's slice (one town), so the two can be compared.

| Place | Contents |
|---|---|
| Tutorial | A short opening area: movement, the first skill, the first spell, the first fight, then walk to town. Could be the town's outskirts rather than its own place. |
| Starter town | The inn (respawn), tavern hangout, bank, a shop, two trainers, the character creator, the cosmetics shop, and the countryside around it with weak creatures. |
| Dungeon | 2–3 wings with monsters, keys hidden across them, and a boss room with one teamwork mechanic. |

### Milestones

1. **Repo and pipeline.**
   - A new repo with Rojo, Luau strict mode, lint and format, tests, and an `AGENTS.md`.
   - One empty experience with three places, publishing from the command line.
   - Teleports between the three places.
2. **Character and save.**
   - Our own character rig (R15-compatible, so Roblox animation tools work) and the creator: body, face, hair, dye.
   - A DataStore profile with session locking (e.g. ProfileStore) that follows you across places.
3. **Combat core.**
   - Melee, one spell, and monsters with simple AI (server-authoritative, readable on phones).
   - Death and respawn at the inn, plus skill improvement on use.
4. **Town.**
   - A greybox of the town, then art.
   - Interiors, NPCs (shop, trainers, bank) and hangout activities.
5. **Dungeon and boss.**
   - The keys, the boss's teamwork mechanic, shared loot and the respawn timer.
6. **Cosmetics shop.**
   - Developer products for 3–5 hairstyles and dyes, plus one game pass, with `ProcessReceipt` tested hard (duplicates, server shutdown mid-purchase, rejoin).
7. **Load and playtest.**
   - Bots or a crowd test to fill a town server to 50+ and a dungeon to 30.
   - Check the frame rate on a mid-range phone, then a friends-and-community test.

### What the slice must prove

- [ ] Teleports between town and dungeon feel acceptable, and the character persists.
- [ ] Combat feels good on PC and on phone.
- [ ] The boss needs real teamwork and is fun with strangers who may not be able to chat (pings and markers, not just text).
- [ ] Skill-improves-on-use works and feels rewarding.
- [ ] The creator and a cosmetic purchase work end to end.
- [ ] A town server with 50+ players and a dungeon with 30 run well on a mid-range phone.
- [ ] People stay and hang out (time in town, D1/D7 return).
- [ ] The art style is readable and fast enough to produce for regular town updates.

## Next steps

- Choose the folder and create the repo (e.g. `E:\2026_Experiments\fantasy-blocks`).
- Write its ADR 0001 from the decisions above, its `AGENTS.md`, and milestone 1's scaffold.

## Sources

- Roblox Creator Hub: [Monetize avatar items](https://create.roblox.com/docs/monetize-avatar), [Teleport between places](https://create.roblox.com/docs/en-us/projects/teleport.md), [Monetization overview](https://create.roblox.com/docs/en-us/get-started/monetization.md)
- [Roblox requires age checks for chat worldwide](https://secure.businesswire.com/news/home/20260107986568/en/Roblox-Requires-Users-Worldwide-to-Age-Check-to-Access-Chat) (Business Wire, January 2026)
- [Meridian 59 open-sourced under the GPL](https://gamefromscratch.com/meridian-59-open-sourced-and-released-under-gpl/) (content not included)
- DevEx rate and payouts (secondary sources; check against the official DevEx terms): [rolearn.dev](https://rolearn.dev/guidance/roblox-devex-requirements-2026), [rowatcher.com](https://rowatcher.com/news/devex-math-in-2026-what-you-actually-take-home-per-1-000-players)
- Server sizes: [DevForum: increase server size](https://devforum.roblox.com/t/increase-server-size/306199), [DevForum: how big can a game be](https://devforum.roblox.com/t/how-big-can-a-game-be/934343)
