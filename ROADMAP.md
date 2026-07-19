# pie-studio Roadmap

## Thesis

pie-studio's job is to give an AI agent a tight **observe → hypothesize → reproduce → verify** loop inside Play-In-Editor. Today it is a data recorder: it captures a thin, pawn-centric slice of state and returns raw CSV, JSONL, and GIF. It captures the wrong signals and hands back dumps instead of conclusions. This roadmap turns it into a diagnostician.

Every feature below is judged against one contract:

1. **Capture the signals that actually carry bug information.** Order of value: logs and errors, then reproduction fidelity, then performance, then visual state. Today only the last exists, and thinly.
2. **Return conclusions, not dumps.** A lead ("first divergence at frame 340, channel = velocity, and an error was logged one frame earlier") beats a 5000-row CSV.
3. **Images, not GIFs.** A vision model reads stills, not animation. Return JPEG keyframes and a labeled contact sheet.
4. **Deterministic observation over fragile re-simulation.** Replay recorded *state*, not input, wherever faithful reproduction is the goal.

## The determinism reality (read first)

Stock UE is nondeterministic: Chaos physics does not reproduce across runs, animation and much gameplay is frame-rate dependent, and unseeded randomness is everywhere. Input replay (what the plugin does today) only reproduces bugs that are a pure function of input plus a seed. That is a small fraction of real bugs and is the root cause of "I told my agent to use pie-studio and nothing improved."

Consequence for this roadmap: input replay is demoted from "reproduction mechanism" to "a way to drive the game unattended to reach a state." Faithful reproduction comes from **state replay** (Take Recorder / Sequencer) and the durable reproduction unit is an **authored Functional Test**, not a recording.

## Architecture recap (what we build on)

- All actions are C++ members of `FGameplayHandlers`, registered via `UEMCP::RegisterExternalHandler`, and provisioned as the `pie` category in `ue-mcp.plugin.yml`. Adding an action = declare it in `GameplayHandlers.h`, implement it in a `GameplayHandlers_*.cpp`, and add a `handlers:` entry with schema to the yml. The TypeScript side stays a no-op.
- Core runtime classes live in `Source/PIE_Studio/Private/PIE/`: `FPIEInputRecorder`, `FPIEInputReplayer`, `FPIEObserver`, `FPIEFrameSampler`, `FPIEInputInjector`, `FPIEViewportCapture`, `FPIEGifEncoder`, `FPIETakeRecorderBridge`, `FPIESequenceFormat`, `UMCPObservationProfile`.
- Data lands under `Saved/MCPRecordings/<id>/` and `Saved/MCPObservations/<run>/`.
- Build constraint: this workstation cannot compile the plugin against the user's UE install (do not touch the Vale project). Every phase ends with the user running the build. Keep changes modular so a broken phase does not block the others.

---

# Phase 1 — Signal and Surface (foundational, do first)

Cheap, universal, mostly independent of the fragile replay core. This phase alone makes the plugin useful. Target: the agent gets errors, viewable images, and a lead instead of a spreadsheet.

## 1a. Session log and error capture

The single highest-leverage feature. Works for every bug regardless of whether replay reproduces it.

**New class** `FPIESessionLog` (`Private/PIE/PIESessionLog.{h,cpp}`):
- Register an `FOutputDevice` on `GLog` for the PIE session lifetime. Capture `{ timestamp, frame, category, verbosity, message }` for every line at Warning and above (configurable floor), plus a bounded ring buffer of all-verbosity lines for context around each error.
- Hook `FBlueprintCoreDelegates::OnScriptException` to catch Blueprint runtime errors and access/nullptr exceptions with their script callstack.
- Hook ensure/check failures (via the output device stream, which carries `Error`-verbosity ensure text and the C++ callstack).
- Auto-attach whenever a record, replay, or observe session is armed. Write `session_log.jsonl` (all captured lines) and `session_errors.json` (deduped errors and warnings with counts, first/last frame, and the surrounding context window) into the session's output dir.

**New actions:**
- `session_errors` — return the deduped error/warning summary for the active or most recent PIE session. This is what the agent calls first after a run.
- `session_log` — paged raw log with filters (`min_verbosity`, `category`, `contains`, `frame_range`, `limit`, `offset`).

**Acceptance:** a `UE_LOG(..., Error, ...)`, an `ensure`, and a Blueprint nullptr access during PIE each appear in `session_errors` with category, frame, and callstack where available.

**Effort:** M. **Depends on:** nothing.

## 1b. Fix the capture pipeline

Directly addresses the "GIFs are useless to Claude, and the plugin deletes the good frames" problem. Note `PIEInputReplayer.cpp:843-845` currently encodes the GIF then **deletes every PNG frame and the capture directory**.

**Changes:**
- `FPIEViewportCapture`: add JPEG output via `IImageWrapper` (`EImageFormat::JPEG`) with a `quality` param (default ~80). JPEG is the right default for gameplay stills; keep PNG as an option for pixel-exact work.
- `FPIEInputReplayer`: stop deleting frames. Keep them under `captures/<run>/frames/frame_%05d.jpg`. GIF stays opt-in (already is) and is demoted in docs.
- **New** `FPIEContactSheet` (`Private/PIE/PIEContactSheet.{h,cpp}`): compose a grid montage of evenly sampled (or divergence-bracketing) keyframes into one image, with frame index and timestamp baked into each cell. One image the agent views at a glance. Output `captures/<run>/contact_<ts>.jpg`.
- Result surface: `replay_status.last_result` and `replay_stop` return `frame_dir`, `frame_count`, `frame_glob`, and `contact_sheet_path`. The agent reads the contact sheet first, then drills into specific frames by path.

**New action:**
- `capture` — standalone "screenshot the PIE viewport now, N frames at H hz" decoupled from replay, so observe and inject flows are visual too. Returns image paths and a contact sheet.

**Acceptance:** after a replay, frames persist as JPEG, a labeled contact sheet exists, and all paths are returned in the result. `capture` works during a plain PIE session with no replay armed.

**Effort:** M. **Depends on:** nothing (contact sheet reuses the readback path).

## 1c. Analysis and synthesis

Turn "here is the data" into "here is the lead."

**New** `FPIEDriftAnalyzer` (fold into the replayer's finish path):
- Compute **first significant divergence**: earliest frame where any tracked channel exceeds its threshold, with `{ frame, time, channel, source_value, replay_value, delta }`.
- Rank the **top-N divergent channels** across the run.
- Correlate divergence with `session_errors` (1a), montage mismatches, and markers, so the lead reads: "first divergence at frame 340 (velocity, 620 vs 180 cm/s); an Error was logged at frame 339; montage mismatch began at 341."
- Write this as a `summary` block at the top of `drift.json`.

**New action / change:**
- `replay_analyze` (or extend `replay_status.last_result`) returns the `summary` block plus the contact sheet path and the frame paths bracketing the divergence.

**Acceptance:** the analyzer names a frame and a channel and links any correlated error, without the agent reading the CSV.

**Effort:** M. **Depends on:** 1a (for error correlation), 1b (for bracketing frames).

**Phase 1 exit state:** an agent runs `replay_run`, polls `replay_status`, and gets back a lead, a viewable contact sheet, and a list of logged errors. That is a usable debugging loop.

---

# Phase 2 — Faithful Observation (deterministic reproduction of *what happened*)

Input replay cannot reproduce most bugs. State replay reproduces the timeline exactly because nothing is re-simulated.

## 2a. State replay via the Take Recorder bridge

The `FPIETakeRecorderBridge` and the `take_record` flag already exist; this makes them the primary reproduction path.

**Changes:**
- During record, bake every tracked actor's transform and selected properties into a Level Sequence each frame (Take Recorder already does transform baking; extend property tracks from the observation profile's `tracked_values`).
- **New action** `replay_state` — play the baked sequence deterministically (in PIE or editor), driving puppet actors, with viewport capture on. Returns frame paths and a contact sheet. Zero drift by construction.
- Scrub support: `replay_state(at_time=...)` to jump to a moment and snapshot it.

**Acceptance:** a recorded session replays visually identical from Sequencer, and the agent can capture any frame or scrub to any timestamp with no divergence.

**Effort:** L. **Depends on:** 1b.

## 2b. Timeline introspection (not one-shot)

`subsystem_state`, `anim_state`, and `anim_properties` are one-shot polls today. Debugging needs the curve over time.

**Changes:**
- Extend `FPIEObserver` to sample arbitrary actors (not just the pawn) and named subsystems each frame into `tracked.jsonl`, driven by the observation profile.
- `observe_read` gains a `series` file type that returns a per-channel time series ready to reason over, plus min/max/first-cross-threshold per channel.

**Acceptance:** the agent can ask "how did `MyGameSubsystem.Phase` change over the run" and get a timeline, not a single value.

**Effort:** M. **Depends on:** Phase 1.

---

# Phase 3 — The Reproduction Unit (durable value)

The lasting payoff. A recording is disposable; a test is not. This is the "proper" answer input recording only gestures at.

## 3a. Functional Test scaffolding from a recording

**New actions:**
- `test_scaffold` — generate an `AFunctionalTest`-based scenario (C++ or Blueprint asset) from a recording: known setup, an input tape or state-replay drive, and assertions derived from the recording's tracked values, drift thresholds, and expected montage sequence. Write it into the project so it lives in source control.
- `test_run` — run the automation test in PIE (headless-capable), returning pass/fail plus artifacts (reuses 1a log capture and 1b image capture).
- `test_list` — enumerate generated tests.

**Acceptance:** the agent converts a repro recording into a runnable Functional Test that goes red on the bug and green after a fix. This is the observe → reproduce → verify loop closed.

**Effort:** L. **Depends on:** Phases 1 and 2.

---

# Phase 4 — Profiling (specialized)

Currently zero performance profiling exists. Build the pragmatic version first; it reuses the frame sampler.

## 4a. Per-frame performance sampling

**Changes to** `FPIEFrameSampler`:
- Sample per frame into the CSV: game-thread time (`FApp` delta / thread timings), GPU time (`RHIGetGPUFrameTime`), render-thread time, draw calls (`GNumDrawCallsRHI`), primitives, and memory (`FPlatformMemory::GetStats`).

**New action:**
- `perf_summary` — avg / p50 / p99 frametime, the worst hitches with timestamps and frame indices, correlated with markers and `session_errors`. "Hitch of 84 ms at frame 512, 0.4 s after the montage started."

**Acceptance:** a replay or observe run yields a perf summary that flags hitch frames with timestamps.

**Effort:** M. **Depends on:** Phase 1.

## 4b. Unreal Insights trace capture

**New actions:**
- `trace_start` / `trace_stop` wrapping `FTraceAuxiliary::Start/Stop` with chosen channels (cpu, gpu, frame, memory). Return the `.utrace` path for the human to open in Insights, and a text summary the agent can read.

**Acceptance:** the agent can capture a `.utrace` around a suspect window and hand the path back.

**Effort:** M. **Depends on:** nothing, but sequence after 4a.

---

# Cross-cutting work (every phase)

- **Determinism honesty.** Update `knowledge/pie.md` and every replay tool description to state plainly: input replay reproduces input-deterministic bugs only; heavy drift usually means the bug depends on uncaptured state; use `replay_state` (Phase 2) or a Functional Test (Phase 3) for faithful reproduction. Stop agents from spinning on replays that were never going to reproduce.
- **Tighter reproducibility knob.** Expose fixed timestep plus seeded RNG (`FApp::SetUseFixedTimeStep`, `FApp::SetFixedDeltaTime`, plus the existing `rng_seed`) as an opt-in for the subset of systems that can support it. Label it "more reproducible," never "deterministic."
- **Data-format versioning.** Bump `kFormatVersion` when adding the `summary` block, perf columns, and series output. Keep readers backward-compatible.
- **Build verification.** Cannot compile here. Each phase ships behind a clean boundary; the user runs the UE build and reports errors. Keep new classes self-contained so a failed build in one area does not wedge the others.

# Explicit non-goals

- **Do not embed Gauntlet.** It is host-side, packaged-build, CI machinery written in C#; it fights the grain of a live in-editor MCP tool. Borrow its disciplines (in-game controller pattern, artifact rigor), not its runner. A far-future "emit a Gauntlet node from a recording" export is optional and out of scope here.
- **Do not chase true lockstep determinism.** It must be built into a game from day one; it cannot be retrofitted onto an arbitrary project.
- **Networked replay (DemoNetDriver)** is deferred. It is the right tool for replicated-gameplay bugs, but it is heavier than Take Recorder state replay and only sees replicated properties. Revisit as an optional Phase 5 if networked bugs become a focus.

# Sequencing and milestones

| Milestone | Contents | Gets you |
|-----------|----------|----------|
| **M1** | 1a + 1b | Errors captured, images viewable, frames stop getting deleted. Usable next week. |
| **M2** | 1c | The agent gets a lead, not a CSV. Phase 1 complete. |
| **M3** | 2a + 2b | Deterministic visual reproduction and state timelines. |
| **M4** | 3a | Recordings become durable, self-verifying Functional Tests. |
| **M5** | 4a + 4b | Real profiling and Insights traces. |

Build M1 first and in order. 1a (log/error capture) is the highest-leverage single change in the entire plan and depends on nothing.

# North-star workflow (end state)

```
# Agent is told: "the player clips through the floor sometimes after a dodge"
pie(action="replay_run", recording_dir="./dodge-clip-repro")
pie(action="replay_status")        # poll to pie_active=false
# -> last_result: {
#      summary: { first_divergence_frame: 512, channel: "PawnLocation.Z",
#                 source: 92.0, replay: -140.0, correlated_error:
#                 "Ensure failed: capsule half-height 0 at frame 511" },
#      contact_sheet_path: ".../contact_..jpg",
#      frame_dir: ".../frames" }
pie(action="session_errors")       # the ensure + its callstack
# agent reads contact sheet, sees the clip, reads the callstack, forms a fix
pie(action="test_scaffold", recording_id="dodge-clip-repro")   # lock in a repro
pie(action="test_run", test="FT_DodgeClip")                    # red now, green after fix
```

That loop is what "useful" looks like: a signal, a lead, an image, and a test that proves the fix.
