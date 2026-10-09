// A second player for the online test (tools/ue/run_net_test.ps1 -Pair): a scripted Meridian Shards
// client (its GameSession, imported from the Shards checkout, which is ours) that logs in to the same
// local server and does what the game's test character tells it to.
//
//   node tools/ue/second_player.ts [--user uenetpal] [--pass uenetpal-local] [--name Unrealpal]
//        [--shards E:/2026_Experiments/meridian-browser] [--url ws://localhost:8059] [--stay 900]
//
// Orders come as tells starting "pal:" (the test character tells it): "pal: tell <text>" tells the
// sender back, "pal: say <text>", "pal: broadcast <text>", "pal: wave", "pal: offer" (offers the
// sender one shilling and accepts their answer), "pal: quit". Prints "PAL READY <name>" once in the
// game, and a line for everything it hears and does, for the test's log.
//
// Its own account (not Shards' shardbot / shardpal: a second login would throw their sessions off).
// An unknown name and password make the account; a character is made the first time.

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { parseArgs } from "node:util";

const { values: opt } = parseArgs({
  options: {
    user: { type: "string", default: "uenetpal" },
    pass: { type: "string", default: "uenetpal-local" },
    name: { type: "string", default: "Unrealpal" },
    shards: { type: "string", default: process.env.SHARDS_DIR ?? "E:/2026_Experiments/meridian-browser" },
    url: { type: "string", default: "ws://localhost:8059" },
    stay: { type: "string", default: "900" },
  },
});

const shards = opt.shards!;
const load = (rel: string) => import(pathToFileURL(join(shards, rel)).href);
const { GameSession } = await load("packages/world/src/index.ts");
const { parseRsb } = await load("packages/formats/src/rsb.ts");
const { SAY, UA } = await load("packages/protocol/src/index.ts");

const t0 = Date.now();
const log = (...a: unknown[]) => console.log(`[pal ${((Date.now() - t0) / 1000).toFixed(1).padStart(6)}s]`, ...a);

const cfg = readFileSync(join(shards, "server/config/blakserv.cfg"), "utf8");
const secretKey = /^SecretKey\s+(\S+)/m.exec(cfg)?.[1];
if (!secretKey) throw new Error("no SecretKey in the Shards server's blakserv.cfg");
const rsb = parseRsb(readFileSync(join(shards, "server/src/run/server/rsc/rsc0000.rsb")));

let ready = false;
let offerTo = 0;

const session = new GameSession(
  { url: opt.url!, username: opt.user!, password: opt.pass!, secretKey, lookupResource: (id: number) => rsb.get(id) },
  {
    phase: (p: string) => {
      log(`phase ${p}`);
      if (p === "game" && !ready) {
        ready = true;
        setTimeout(() => {
          const room = session.world.player ? session.resource(session.world.player.roomNameRes) ?? "" : "";
          log(`PAL READY ${opt.name} ${room}`);
        }, 1500);
      }
      if (p === "closed") process.exit(0);
    },
    characters: (chars: { id: number; name: string; flags: number }[]) => {
      const mine = chars.find((c) => c.flags !== 1);
      if (mine) {
        log(`entering as ${mine.name}`);
        session.useCharacter(mine.id);
        return;
      }
      const slot = chars.find((c) => c.flags === 1);
      if (!slot) throw new Error("no character slot");
      log(`making ${opt.name}`);
      session.createDefaultCharacter(slot.id, opt.name!);
    },
    error: (m: string) => log(`error: ${m}`),
    chat: (line: { spans: { text: string }[]; sayType?: number; sender?: { id: number; name: string } }) => {
      const text = line.spans.map((s) => s.text).join("");
      log(`heard (${line.sayType ?? "-"}) ${text}`);
      const order = /pal:\s*([^"]*)/.exec(text)?.[1]?.trim();
      if (line.sayType !== SAY.GROUP || !order || !line.sender || line.sender.id === session.world.player?.id) return;
      obey(order, line.sender.id);
    },
    offer: (e: { type: string }) => {
      log(`offer: ${e.type}`);
      // they answered our offer (nothing, or something): take the deal
      if (e.type === "counteroffer" && offerTo) {
        session.acceptOffer();
        log("accepted");
        offerTo = 0;
      }
    },
  },
);

function obey(order: string, from: number): void {
  const [verb, ...rest] = order.split(/\s+/);
  const words = rest.join(" ");
  log(`order: ${verb} ${words}`);
  switch (verb) {
    case "tell": session.sayTo([from], words); break;
    case "say": session.say(words); break;
    case "broadcast": session.say(words, SAY.EVERYONE); break;
    case "wave": session.action(UA.WAVE); break;
    case "offer": {
      // a shilling, or else the first thing carried
      const items = [...session.world.inventory.values()] as { id: number; amount: number; nameRes: number }[];
      const coin = items.find((i) => (session.resource(i.nameRes) ?? "").toLowerCase().includes("shilling")) ?? items[0];
      if (!coin) { log("nothing to offer"); break; }
      offerTo = from;
      session.offerItems(from, [{ id: coin.id, amount: coin.amount > 0 ? 1 : undefined }]);
      break;
    }
    case "quit": session.close(); process.exit(0);
    default: log(`unknown order ${verb}`);
  }
}

setTimeout(() => { log("staying no longer"); session.close(); process.exit(0); }, Number(opt.stay) * 1000);
