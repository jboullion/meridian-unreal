# ADR 0004: Hosting and operations

- Status: Proposed
- Date: 2026-10-05

## Context
[ADR 0001](0001-engine-and-architecture.md) sets the architecture:
- one authoritative UE 5.8 dedicated server process that holds the whole world
- Supabase for accounts and persistence, with only the server writing gameplay state

This ADR decides where those pieces run and how we operate them. Expected load is 10–15 players online most of the time, with a peak of about 200.

Facts that drive the decision:
- The game server is a long-running Linux process (the packaged UE dedicated server) listening on a UDP port. It doesn't need an MMO-specific host.
- 200 players in one UE process fit on a single machine. The limit is CPU, mainly one fast core for the game thread, plus roughly 4–8 GB of RAM. We don't need sharding or orchestration.
- Unreal doesn't provide hosting. It only produces the Linux server build, which needs a source-built engine (see ADR 0001).

## Decision

1. **Game server: one always-on Linux VPS or dedicated server.**
   - Pick hosts with fast single-core clocks. Hetzner CCX13/CCX23 (dedicated vCPU, about €15–30/mo) is the first choice. OVH game servers also work and include DDoS protection. A DigitalOcean CPU-Optimized droplet works too but costs more for the same CPU.
   - Use one region near most players (US-East or EU-central).
   - Start with 2–4 dedicated vCPUs and 8 GB of RAM. Load-test with bots, and move to a bigger machine if the server tick rate drops.
   - Run the server under **systemd** with auto-restart on crash. Open only the game UDP port and SSH, and allow SSH keys only.
   - Deploy with GitHub Actions: build the server, rsync it to the box, then restart the service.
   - Restart nightly in a fixed window. On shutdown, flush the world to the database.
2. **Database and auth: Supabase.**
   - Use the free tier during development. Move to **Pro ($25/mo)** for launch. Pro adds daily backups and stops the project from being paused for inactivity, which would take a live game down.
   - Put the Supabase project in the same region as the game server.
   - Write to the database in batches, never every tick: an autosave every 1–5 minutes, plus a save on logout and on zone change. This keeps database load small and keeps database latency out of the game loop.
3. **Supporting services**
   - Login flow: the client signs in with Supabase Auth, gets a JWT, then connects to the game server through a Cloudflare DNS record. The record must be DNS-only, because Cloudflare's proxy doesn't carry game UDP traffic.
   - Client distribution: itch.io or Cloudflare R2 for now. Steam can come later.
   - Monitoring: an uptime check, log shipping (Grafana Cloud or Better Stack free tiers), and a Discord webhook that alerts on crashes.
   - Staging: a second small box, or a server run locally, backed by a Supabase branch.

## Estimated monthly cost
| Item | Development | Launch |
|---|---|---|
| Game server | $0 (local PIE) | $20–45 |
| Supabase | $0 | $25 |
| DNS, CDN and monitoring | $0 | $0–10 |
| **Total** | **$0** | **~$50–80** |

## Alternatives considered
- **Managed game-server fleets (AWS GameLift, Edgegap, Hathora, PlayFab):** they're built to start many short match sessions. A persistent world gets nothing from that, and they cost more and tie us to their SDKs.
- **Self-hosted Postgres on the game box:** cheaper, but we'd have to run auth, backups and upgrades ourselves. It's not worth it at this scale. Revisit only if cost becomes a problem.
- **Several server processes (sharding by zone):** not needed below about 200 players, and it would make cross-zone state much more complex.

## Follow-up work
- Source-built engine and a Linux server target
- systemd unit and deploy workflow
- Autosave service in C++
- Load test with 200 bot clients on the chosen box. It must hold at least 30 Hz server ticks, with database writes well under Supabase's limits.
