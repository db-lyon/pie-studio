# pie-studio: evolution report

**From:** an autonomous coding agent that used pie-studio 0.5.0 end-to-end for a real locomotion
debugging + testing session (record, replay, observe, analyze, inject, perf, errors) on UE 5.8.
**Lens:** the tool's fastest-growing user is an AI agent driving the editor headlessly. This report
evaluates pie-studio as an **agent-driven end-to-end (E2E) automation harness** and proposes where to
take it. Grounded in what actually worked and what I hit walls on, not speculation.

---

## TL;DR

pie-studio is a **great instrumentation layer** and a **weak actuation + verdict layer**. Framed as
the classic test structure:

| Leg | What it means | pie-studio today |
|---|---|---|
| **Arrange** | set up the world/scenario/actors | ⚠ almost nothing — you get whatever the map spawns |
| **Act** | drive the scenario | ⚠ raw player-input injection only; no semantics, no non-pawn control |
| **Assert** | decide pass/fail | ◑ superb *observation*; primitive *verdict* (drift + error-count only) |

The observe/analyze/perf/errors surface is genuinely excellent and should be the model for the rest.
The highest-ROI work is filling **Arrange** and **Act**, and turning the rich observation data into
real **assertions**. None of it requires solving physics determinism.

---

## What works well today (keep, and use as the quality bar)

- **Observation profiles** — per-frame sampling of arbitrary reflected values *and* arbitrary actors
  (pos/rot/vel) *and* subsystem values. Verified: tracked 3 arbitrary scene actors + pawn state +
  a gameplay int across 2500+ frames in one run. This is the strongest primitive in the tool.
- **`replay_analyze` first-divergence lead** — surfacing the *first* frame/channel that diverged
  (not just the max) with the bracketing image is exactly the right abstraction. More of this.
- **`session_errors`** — deduped log errors/warnings with frame + count + callstack. Surfaced real
  latent warnings ("Unknown setting key: …") we'd never have scrolled the Output Log to find.
- **`perf_summary`** — frametime p50/p99, per-thread ms, hitches. Immediately told us the pawn is
  game-thread bound with GPU headroom. Zero ceremony.
- **Input injection now live** — `inject_input_*` authoring input on the fly (was dead in 0.3.5).
  Real unlock: the agent drove the pawn with no recording and no human.

---

## The strategic thrust: agent-authored, repeatable E2E — without chasing determinism

The valuable end state is: **an agent can arrange a scenario, act on it semantically, observe
everything, and get a pass/fail verdict — unattended and repeatably.** Repeatable ≠ deterministic.
Chaos physics will never reproduce and that's fine; the answer is **seeded + fixed-timestep +
tolerance-based assertions**, which the tool already leans toward. Do not spend effort trying to make
UE deterministic. Spend it on the three legs below.

---

## Roadmap, ordered by ROI (value × feasibility)

### P0 — Assertion layer on observed channels *(highest ROI; you already have the data)*
Today `test_scaffold` asserts only `max_drift` + `error_count`. That's a tiny slice of "did it work."
You already sample every channel per frame — expose assertions over them:
- value reached / stayed within tolerance ("pawn within 50cm of actor X by frame N")
- equality / threshold / crossed ("`Health` == 0", "`speed2d` never exceeded 900", "gait reached 2")
- montage played / section hit; no log matching a regex; actor never fell below Z
- temporal: "within T seconds", "held for T", "first_frame_over already exists — promote it to an assert"

This turns the excellent observation surface into actual tests. **Feasibility: high** — it's a
predicate layer over data you already write. This single feature is the difference between "a
profiler" and "a test framework."

### P0 — Actor puppeteering (Arrange + Act for non-pawn actors)
The single biggest capability gap. Today you can teleport **only the player pawn** to a **recorded**
pose (`replay_state --apply`). An agent authoring a scenario needs to, in a live PIE session:
- spawn / despawn arbitrary actors at transforms
- set arbitrary reflected state (health, faction, an AI blackboard key, a flag)
- drive an actor's transform over time (a moving platform, a scripted NPC path)
- trigger gameplay events (apply damage, fire an ability, force a montage)

`level.move_actor world:pie` exists in core ue-mcp but is editor-actor-centric and not part of the
test-authoring flow. Bring first-class **puppet** actions into pie-studio so Arrange/Act cover the
whole world, not just the player. **Feasibility: medium** — spawn/setprop/move are reflection +
existing engine calls; driving AI behavior is the only hard part and can be deferred.

### P1 — Semantic / nav-aware input (Act, but intent-level)
Raw `inject_input(IA_Move, y=1)` is too low-level: in testing this session the pawn ran straight off a
platform into the void, twice, because injection is dumb axis-holding. An agent wants intent:
- `goto(location | actor, tolerance)` — project to navmesh, path, drive the pawn's input to follow
- `face(target)`, `follow(actor, distance)`, `interact(actor)`, `stop`
- these compile down to the existing input primitives, but the harness owns the closed loop

This makes scenario authoring robust instead of fragile dead-reckoning. **Feasibility: medium** —
navmesh project + path + a follow controller feeding the existing inject layer. Huge usability win.

### P1 — Declarative scenario format (the reusable Arrange+Act+Assert unit)
`test.json` today is welded to a recording. Promote it to a standalone, version-controlled scenario:
```
setup:  spawn/place actors, set state, seed RNG, choose map
steps:  goto / inject_tape / puppet actor / wait-until / mark
assert: the P0 predicates above
```
A recording becomes *one possible* source of `steps`, not the only one. This is the artifact a team
checks into git and runs in CI. **Feasibility: medium** — schema + runner; the pieces (steps, asserts,
seed) already exist piecemeal.

### P1 — Live / event-driven observation (fail-fast + triggers)
Observation is currently pull-after-run: run, then read the series. Add live watchpoints:
- `wait_until(condition, timeout)` — the backbone of scenario steps
- fail-fast: abort + capture the moment an invariant breaks (pawn below kill-Z, error logged)
- capture-on-event, mark-on-event
This also cuts wasted wall-clock on runs that already failed. **Feasibility: medium.**

### P2 — Headless / cooked-build / Gauntlet path (CI-grade E2E)
Everything today is in-editor PIE. Real E2E in CI wants out-of-editor runs against cooked builds
(and eventually Gauntlet-style client/server orchestration). This is the biggest lift and the clearest
"product vs toy" line for a studio pipeline. Sequence it after the in-editor Arrange/Act/Assert legs
are solid, since those define what a headless run would even execute. **Feasibility: high effort.**

### P2 — Netplay flows (matters a lot for this project specifically)
`client_id` exists but multi-client scenarios aren't first-class: listen-server + N clients, per-client
input tapes, and **replication assertions** (does client B see what client A did, within tolerance).
For a coop/pvp game this is where the real bugs live. **Feasibility: high effort**, high value for
networked titles.

### P3 — Vision-loop polish
Contact sheets are great. Next: capture-on-event, region/bounding-box assertions, and
frame-to-frame visual diff so a vision model can be handed "what changed here" rather than a full grid.

---

## North-star example (what an ideal agent-authored E2E test reads like)

```yaml
scenario: boss_dies_to_three_heavies
seed: 1337
map: /Game/Maps/Test/L_Arena
arrange:
  - spawn: BP_Boss at (1200,0,100) as $boss
  - set: $boss.Health = 300
  - place: player at (0,0,90)
act:
  - goto: $boss within 150cm
  - repeat 3: inject_tape IA_HeavyAttack
assert:
  - $boss.Health == 0 within 8s
  - montage "AM_Death" played on $boss
  - no error logged
  - player.pos.z never < -100     # never fell off
```
Every verb there maps to a capability above. The tool is ~40% of the way to running this: it can
observe and (crudely) act, but it can't yet arrange the boss, act semantically, or assert the verdict.

---

## The one thing not to do

Don't chase physics determinism or frame-exact reproduction. The tool's own docs already say stock UE
isn't deterministic; input-replay drift measured 151 → 822 cm across identical runs this session. The
winning philosophy is **repeatable scenarios + tolerance-based assertions**, not bit-exact replay.
Lean into that, not against it.
