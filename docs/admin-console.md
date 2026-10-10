# The admin console

The admin console lets staff run the server's commands from inside the game: teleport anywhere, bring or rescue players, make items and monsters, set the time of day, message everyone. It's our version of the original client's admin window (`module/admin`), laid out like Meridian Shards' console, with buttons for the common jobs.

**Everything in it changes the live world at once.** There's no undo, and only Clear Inventory asks first. Try things on our local server before the hosted one, and never point the game at the live Server 104.

## Who gets it

The server decides. When an admin or DM character logs on, blakserv loads the original client's staff modules for it ("admin.dll", "dm.dll"; `admin.kod` and `dm.kod` `UserLogonHook`), and the game shows **Admin Console** in the Escape menu (Esc, or F10). Ordinary characters never see it, and the server refuses staff commands from anyone else.

| Account | What works |
|---|---|
| Admin (`create account admin`) with an `Admin` character | Everything: server commands and DM commands |
| DM (`create account dm`) with a `DM` character | The DM commands; the server commands are greyed out. Travel and Go to use `BP_REQ_DM`, which the server's `[Rights]` settings allow |

## Making an admin account

Accounts are made on blakserv's maintenance port (localhost:9998). With the Shards dev stack running (`npm run dev` in `meridian-browser`):

```bash
node tools/maint/maint.ts "create account admin <name> <password> none"
```

That prints the account's number. Give it an admin character:

```bash
node tools/maint/maint.ts "create admin <account number>"
```

Both commands run in `E:\2026_Experiments\meridian-browser`. The character is named at its first login: in the game's character creator, or with Shards' headless client (`node tools/headless/client.ts --user <name> --pass <password> --char <Name>`).

**Our local test admin** is `ueadmin`, password `ueadmin`, character Ueadmin (account 41 on the local server, made 2026-10-10). It exists only on the local dev server. On the hosted server, make one the same way through the container (Shards' `deploy/README.md`), with a strong password.

## The window

| Part | What it does |
|---|---|
| **Travel** | Every room (`data/net/rooms.json`), filtered by what you type: a name, a room file or a number. **Go there** (or a double click, or Enter in the search) teleports you. A number the list doesn't know still works. **Where am I?** prints the room's file and number, and your square. |
| **Players** | Who is on. Pick one, then **Show** (every property of their character), **Go to**, **Bring here**, **Rescue** (somewhere safe, for a player stuck in a wall) or **Tell**. |
| **Self** | Immortal or mortal, boost stats, get every spell or skill, hidden, invisible, plain (back to normal), stealth, anonymous, karma, appeals; get an item by name or a class of items; disguise as a monster; Reset data (ask the server for everything again). |
| **World** | The time of day (until Real time), make or call a monster, rumble, place scenery or a light (Lights? and Scenery? list the names), an event sign, a message to everyone or to this room, and the server's who, status, clock, save game and help. |
| **Answers** | What the server said back, newest last. Server commands are echoed as `> command`; DM commands as `> dm ...`, with the game messages that answered them. Select text to copy an object number. **Clear** empties it. |
| **Command line** | Type a server command, or `dm` and a DM command, and press Enter. **Up** and **Down** step through what you've sent, buttons included. `clear` empties the answers; `quit` empties them and closes the window. |

Esc or Close hides the window. The answers stay until you log off.

## Typing commands

**Server commands** are the maintenance port's. `help` lists the verbs, and a verb on its own (`show`, `send`, `create`) lists what it takes. Your own object number is in Players > Show, or in `show user <name>`.

| Command | What it does |
|---|---|
| `send object <you> teleportto rid int <room>` | Go to a room (Travel does this) |
| `show object <number>` | Every property of an object; `poOwner` is the room or holder it's in |
| `create object <class>` then `send object <you> NewHold what object <it>` | Make something and take it |
| `send object <thing> Delete` | Remove an object from the world |
| `send users <text>` | A system message to everyone |
| `save game` | Save now. **Object numbers change** at every save: show an object again before naming it |

**DM commands** start with `dm`; `dm help` lists them, `dm help lights`, `dm help scenery` and `dm help messages` list more. For example `dm item long sword`, `dm monster orc`, `dm night`, `dm get coords`, `dm place brazier`.

Some rooms by number: 1 the Underworld, 50 Tos, 102 Barloque, 200 Marion, 300 Raza, 301 the Inn of Raza, 302 the Adventurer's Hall, 303 the smithy, 306 the Raza crypt, 330 the Outskirts, 332 the vault, 333 the bank. Rooms we haven't built are built at runtime from the server's files.

## Not included

- The original's key for the window (Shift+4): use the Escape menu.
- The original's object box, which listed a shown object's properties to edit in place: use `set object <number> <property> <type> <value>`.
- Looking at something with the console open doesn't show its object, as in the original and Shards: use Players > Show, or `show object`.
- The DM module's BGF and quest editors and G-Channel.

## For developers

- Code: `UI/SMRAdminConsole` (the window), `UMRNetSubsystem::AdminCommand`, `DMCommand`, `DMSay`, `IsAdmin`, `IsStaff` (`Net/`). The protocol is in `docs/research/blakserv-protocol.md`, "Admins and DMs".
- Test: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/run_net_test.ps1 -Admin` logs in as `ueadmin`, checks the modules, runs `show clock` and `dm get roo`, and teleports between the Inn and the Adventurer's Hall (`DONE 9/9`). `-Render` also opens the console and photographs each page (`DONE 10/10`, `Saved/Screenshots/MRNet/admin*.png`).
