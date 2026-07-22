# pie-studio Roadmap v2 — The Test-Authoring Product

## The bet (read first)

pie-studio is **not** a test runner. Runners are commodity: Gauntlet is free, and any studio that cares already has sunk cost in in-house automation. Competing there is a capital-intensive fight against Epic and against a studio's existing pipeline, to arrive at parity nobody pays for.

The uncontested, high-value capability is **authoring**. Studios have thin gameplay-test coverage because writing and maintaining these tests is expensive human labor, so it doesn't get done. An agent that observes a build and produces durable, trustworthy coverage collapses that cost. That is the moat and the sale.

So: **pie-studio authors and validates tests, then emits them into the runner the studio already trusts.** It never wins the runner war. It feeds the trusted runner.

Three properties turn "an agent wrote a test" into "a studio will buy this." They, not the runner, are the product:

1. **Born-validated** — the agent proves each test discriminates (red on the bug, green on the fix) before committing it. A test never observed to fail is noise.
2. **Flake-budgeted** — each authored test's stability is measured (run N times at authoring), tolerance bands are derived from observed variance, a false-red rate is attached, and nothing over budget is emitted. This is what earns a place in a merge gate.
3. **Emitted, not hosted** — the validated scenario compiles to a native `AFunctionalTest` or a Gauntlet node the studio's CI already runs headless. Additive; near-zero procurement risk. Headless CI is a consequence of emit, not a phase.

Everything below serves that bet. Determinism honesty from v1 is unchanged and load-bearing here: we do **not** chase physics/lockstep determinism; the flake budget is how we make nondeterminism a measured, bounded property instead of a hidden liability.

## Status

- **Phase A — Assertion layer: DONE (v0.5.x, this milestone).** Predicate language (channels, events, five temporal holds, windows, deadlines), `FPIEPredicateEvaluator`, `assert_eval`, predicate-aware `test_scaffold`/`test_run`, witness frames wired to contact sheets. Verified: `PIEStudio.Assertions.Predicates` + full suite green on UE 5.8. This was the necessary floor: without a real verdict there is nothing to validate, budget, or emit.

---

# The substrate (necessary, not sufficient)

These build the raw ability to construct and drive a scenario. They are table stakes. They do not, by themselves, clear the buy bar — F3–F5 do.

## F1 — Arrange + intent-Act

Give an agent write access to the world and semantic control of pawns. Without state construction there is nothing to author.

- **`FPIEActorPuppet`** (`Private/PIE/PIEActorPuppet.{h,cpp}`): reflection **write** by dotted path, mirroring the read walk at `PIEFrameSampler.cpp:46` (descend via `FindPropertyByName` + `ContainerPtrToValuePtr` through struct/object segments; at the leaf `ImportText_Direct` for uniform JSON→property coercion). Reuses `FindActorById`.
- **Verbs:** `actor_spawn` (class path + transform → stable id), `actor_destroy`, `actor_set` (path → value), `actor_call` (invoke a callable `UFUNCTION` with args), `actor_drive` (per-frame velocity / move-to / montage for N seconds).
- **Intent input:** `pawn_goto` / `pawn_face` / `pawn_follow` over a transient `AIController` + navmesh path-follow. Replaces blind axis-holds (the pawn ran off the platform twice because injection is dumb).

**Effort:** M (write half mirrors existing read half) + L (nav). **Depends on:** nothing hard; only useful once Phase A exists to assert on results.

## F2 — Declarative scenario

The artifact: a committed `arrange` / `act` / `assert` document. A recording becomes one possible source of `act` steps, not the unit. `scenario_run` executes it end-to-end into a Phase A verdict; `scenario_scaffold` emits a skeleton from a recording.

**Effort:** L. **Depends on:** F1, Phase A.

---

# The product (what clears the buy bar)

## F3 — Self-validating authoring loop  ★ first true differentiator

An authored test must be proven to discriminate before it is trusted.

- **`test_prove`** — given a scenario and a way to toggle the suspected defect (a fixed vs. broken build, a cvar, a mutation), run the scenario against **both** states and require the verdict to flip: FAIL on broken, PASS on fixed. A scenario whose verdict does not flip is rejected as non-discriminating and never committed.
- The agent's authoring loop becomes: observe → hypothesize → author scenario → **prove it flips** → commit. The "prove it flips" step is the thing in-house frameworks never automate.
- Output: a `proof.json` alongside the scenario recording the two verdicts and the discriminating assertion(s), so a human reviewer sees *why* this test is real.

**Effort:** M (orchestrates F2 + Phase A twice). **Depends on:** F2.

## F4 — Flake budget + variance-derived tolerances  ★ the trust layer

Make nondeterminism a measured, bounded property.

- **`test_stability`** — run a scenario N times (default ~20), record per-assertion pass rate and the observed distribution of each channel at each witness point.
- Derive tolerance bands from observed variance (e.g. band = observed range + k·σ) instead of guessed constants; rewrite the scenario's predicates with data-backed thresholds.
- Attach a **false-red rate** per assertion and a scenario-level flake score. Refuse to emit (F5) any test whose flake score exceeds a configurable budget; report exactly which assertion is unstable and why.

**Effort:** M. **Depends on:** F2, Phase A. Reuses the observer's per-frame series.

## F5 — Emit into the studio's runner  ★ the distribution / buy-enabler

Compile a proven, budgeted scenario down to an artifact the studio's CI already runs, headless, on packaged builds.

- **`emit_functional_test`** — generate an `AFunctionalTest` (C++ or Blueprint asset) that reproduces the scenario's arrange/act and evaluates its predicates, written into the project under source control. Runs headless via the standard Automation harness. No pie-studio dependency at run time.
- **`emit_gauntlet_node`** (optional, later) — emit a Gauntlet test node for studios standardized on Gauntlet CI.
- The emitted test carries its `proof.json` and flake score as metadata, so a failed CI run links back to the discriminating evidence.

**Effort:** L. **Depends on:** F2, F3, F4.

---

# Explicit non-goals (reaffirmed and sharpened)

- **Do not build a runner or a CI dashboard.** Emit into theirs. The moment we host execution we are competing with Gauntlet on Epic's turf and asking a studio to trust our runtime in their merge gate. Both are losing asks.
- **Do not chase physics/lockstep determinism.** F4 makes nondeterminism a measured budget instead. This was the right hill in v1.
- **Do not ship a test the agent never watched fail (F3) or never measured for flake (F4).** Those two gates are the entire reason this is buyable rather than a toy that emits plausible-looking assertions.

# Sequencing and milestones

| Milestone | Contents | Clears |
|-----------|----------|--------|
| **A (done)** | Phase A assertion layer | Credibility: a real verdict exists |
| **B** | F1 + F2 | An agent can construct and run a scenario |
| **C** | **F3** | Tests are born validated — the first thing a buyer can't get elsewhere |
| **D** | **F4** | Tests carry a measured false-red rate — trustworthy in a merge gate |
| **E** | **F5** | Proven, budgeted tests run in the studio's own CI |

Milestones C, D, E are the product. B is the substrate they stand on. A studio's buy decision is made at C→E, not at B.

# North-star (end state)

```
# Agent is told: "players sometimes clip through the floor after a dodge."
1. observe a repro, form the hypothesis (Phase A: witness frame 512, pos_z = -140)
2. author scenario: arrange the dodge setup, act the dodge, assert pos_z >= -50 always
3. test_prove  -> FAIL on today's build, PASS on the candidate fix   (proof.json: verdict flipped)
4. test_stability -> 20 runs, floor assertion false-red rate 0.0%, band pos_z in [-8, +∞]
5. emit_functional_test -> FT_DodgeFloorClip.cpp committed to the project
6. studio CI runs FT_DodgeFloorClip headless on every build, red if the clip returns
```

The sale is step 5's artifact and steps 3–4's evidence that it is real and stable. Not the tool that produced it.
