# Meridian Remastered

A free, fan-made remaster of **Meridian 59**, the 1996 classic that was one of the first 3D MMORPGs, rebuilt in Unreal Engine 5.8.

The goal is simple: walk back into Meridian and have it feel like home, only better looking. The same towns, the same rules and the same music, with modern lighting, real 3D buildings, a day/night cycle, weather, and first- or third-person action combat.

> Meridian Remastered is a fan project. It is not affiliated with or endorsed by the owners of the Meridian 59 trademark. It's built on the open-source Server 104 ruleset, with the blessing of the Server 103 and 104 teams.

## Where it stands

We're early. Today the project is essentially a port: the original's data, maps, textures and sounds brought into Unreal, playing online on a real Meridian server (the same one as our browser client, Meridian Shards). The first playable demo covers the town of **Raza** and the zones around it.

What works now:

- Raza, its interiors and the Outskirts, built from the original maps, with near-instant zone changes
- Upscaled original textures and rebuilt roofs, parapets, fences and signs
- The original's day/night cycle, seasons and storms, with lit windows, torches and chimney smoke
- The original's music and ambient sound
- Online play: log in (a new name makes an account), create a character, walk Raza with other players (browser, desktop and original clients too) and chat, in first or third person

What's next: combat, inventory, spells and skills from the server, the full character creator, player looks, and the rest of the world.

Once the remaster is in a working state, we'll open-source as much of it as we're allowed.

## Ground rules

A few things matter a lot to the people who made this possible, so please respect them in anything you contribute:

- **The name is "Meridian Remastered".** Never use "104" in the name or branding.
- **No original game files in the repo.** The original art and sounds are extracted from your own installed copy of the game when you set up the project.
- **Stay close to the original.** This is a remaster, not a reimagining. Keep the colours, the motifs and the feel people remember.

## Contributing

We'd love help, especially from people who played Meridian and remember how it should feel.

Good ways to help:

- **Play the demo and tell us what feels wrong.** A missing sign, a wall that doesn't match, a sound that plays at the wrong time. Memories of the original are valuable.
- **Art:** 3D buildings and props; sprite work for characters, monsters, hair and clothing.
- **Code:** Unreal C++ (gameplay, combat, networking), Python tools, Supabase backend.
- **Research:** how the original actually behaved, from the Kod source or from playing Server 103/104.
- **Docs:** if something in setup confused you, fixing the docs helps the next person.

How to get involved:

1. Open an issue on [GitHub](https://github.com/jboullion/meridian-unreal) describing what you'd like to work on, so we can avoid duplicated effort.
2. Fork the repo, make your change on a branch, and open a pull request.
3. For anything visual, include before and after screenshots.

Most decisions are written down as short decision records in [docs/adr/](docs/adr/). If you want to change something that's been decided, open an issue first and explain why.

### Working with AI assistants

Much of this project is built with AI coding assistants. [AGENTS.md](AGENTS.md) holds everything an assistant (or a curious human) needs: the architecture, every build and test command, the tools, and the rules. Task-specific guides live in [.claude/skills/](.claude/skills/).

## Getting started

You'll need:

- Windows, with [Unreal Engine 5.8](https://www.unrealengine.com/) and Visual Studio 2022 or 2026 (C++ game workload)
- [Git LFS](https://git-lfs.com/)
- Python 3.11+ and [Blender](https://www.blender.org/) 5.x
- An installed Meridian 59 client (the Server 104 client or the classic Steam client) to extract the original art from
- A checkout of the original Server 104 source in `Server-104/` (reference only, for maps and game data)

Then, from a fresh clone:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1
```

This extracts the original data and art, builds the maps, compiles the game and generates the world. Pass `-EngineRoot "<path to UE_5.8>"` if Unreal is installed somewhere else.

To play, open `game/MeridianRemastered/MeridianRemastered.uproject` and press Play (Net Mode "Play Standalone"). The login screen connects to a Meridian server: "Shards (online)", or "Local (dev)" when the Shards dev stack runs on your machine (`npm run dev` in the meridian-browser repo). Any new name and password make an account. Move with WASD and the mouse (you run; hold Shift to walk), press Space on a door to go through it, V to switch between first and third person, and Enter to chat.

More detail on building, testing and the tools is in [AGENTS.md](AGENTS.md).

## Learn more

- [How the remaster is built](docs/adr/0001-engine-and-architecture.md): engine, server and data
- [Playing on Meridian servers](docs/adr/0010-meridian-servers.md): the login, the protocol and the servers
- [What we learned from the original data](docs/findings.md)
- [Environment art](docs/adr/0003-environment-art-pipeline.md): how the towns get their look
- [Time, weather and atmosphere](docs/adr/0005-time-weather-and-atmosphere.md)
- [Audio](docs/adr/0006-audio.md)
- [Characters](docs/adr/0008-sprite-characters.md): players and monsters drawn as sprites, like the original

## Thanks

To the original Meridian 59 creators at Archetype Interactive, to the Server 103 and Server 104 teams for keeping Meridian alive and for their permission to use its assets, and to everyone who still remembers the road out of Raza.
