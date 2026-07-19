# PIE Record / Replay / Observe / Profile

pie-studio provisions the `pie` category: deterministic PIE recording, replay,
observation, diffing, snapshots, input injection, session-error capture, viewport
capture, profiling, and self-verifying reproduction tests. Actions are unprefixed -
the category is the namespace. Targets Unreal Engine 5.8.

## Determinism reality (read this first)

Stock UE is **not** deterministic: Chaos physics does not reproduce across runs,
animation and much gameplay is frame-rate dependent, and unseeded randomness is
everywhere. So:

- **Input replay** (`replay_arm` / `replay_run`) re-injects the recorded input and
  measures how far the world drifted. It only reproduces bugs that are a pure
  function of input + a seed. Heavy drift usually means the bug depends on state the
  recording never captured - do not keep re-running an input replay that was never
  going to reproduce.
- For **faithful** reproduction of what happened, use `replay_state` (deterministic:
  it replays recorded transforms, nothing is simulated) or Take Recorder baking.
- The durable reproduction unit is a **reproduction test** (`test_scaffold` /
  `test_run`), not a recording.
- `fixed_timestep=true` on replay makes runs *more* reproducible (fixed 1/pin_fps
  delta), never fully deterministic.

## Debugging loop (what to call, in order)

1. `replay_run(recording_dir=...)` - drive the recording unattended; poll
   `replay_status` until `pie_active=false`.
2. `replay_analyze(recording_dir=...)` - get the **lead**: first divergence (frame,
   channel, source vs replay value), top channels, errors during the run, and the
   images bracketing the divergence. Read this instead of the CSV.
3. `session_errors()` - the deduped errors/warnings (incl. Blueprint exceptions)
   logged during the session, with callstacks. Often the fastest path to the bug.
4. Look at the contact sheet (`contact_sheet_path`) and the bracketing frames.
5. `test_scaffold` then `test_run` - lock the repro in and verify a fix.

## Session errors (highest-leverage signal)

`session_errors` returns deduped errors + warnings captured from the Output Log for
the PIE session; `session_log` returns the paged raw log (filter by verbosity /
category / substring). Both default to the live/most-recent session; pass a
`session` id to read a finished one from `Saved/MCPSessions/`. Works for every bug,
not just input-deterministic ones.

## Recording

`record_arm` configures what to capture: input actions, pawn state, tracked
reflection paths, actor positions, and (new) subsystem values via `sub:<Class>.<path>`
tracked entries. Per-frame performance (game/render/gpu ms, memory) is always
written to `recording.csv`. Recording starts on BeginPIE and finalises on EndPIE.

## Replay

`replay_arm` loads a recording's `sequence.json` and replays input through Enhanced
Input; drift sampling compares pawn location/rotation/velocity frame-by-frame.
`capture_frame_every` grabs viewport JPEGs and builds a labeled contact sheet (GIF is
opt-in via `encode_gif`; a vision model cannot read GIF animation). `replay_run` is
the unattended variant that starts and stops PIE.

## Deterministic state replay

`replay_state(recording_dir, at_time=T | at_frame=N)` returns the exact recorded pawn
pose at that moment (interpolated), with zero drift. `apply=true` teleports the live
PIE pawn to the recorded pose. Use to scrub a session and inspect state.

## Observation

`observe_arm` samples actor/subsystem state per frame using a profile. `observe_read
file=series` returns per-channel time series (min/max/first/last, first_frame_over a
threshold) - a curve over time, not one value.

## Profiling

`perf_summary(recording_dir)` aggregates the per-frame perf columns into frametime
avg/p50/p99/max, avg game/render/gpu ms, peak memory, and the worst hitches with
timestamps. `trace_start` / `trace_stop` capture an Unreal Insights `.utrace`.

## Reproduction tests

`test_scaffold` writes a `test.json` (assertions: max drift + max errors) beside a
recording. `test_run` replays the recording (auto_run) then checks the finalised
drift + error count against the assertions, returning `passed` + `failures` + the
contact sheet. `test_list` enumerates them. This is the reproduce -> verify-a-fix loop.

## Input injection

`inject_input` / `inject_input_start` / `inject_input_tape` drive Enhanced Input
actions programmatically. `capture` grabs viewport frames on demand (decoupled from
replay) so inject/observe flows are visual too.
