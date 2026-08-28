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
reflection paths, actor positions, subsystem values via `sub:<Class>.<path>`, and
skeletal bone/socket channels via `bone:<BoneOrSocket>.<channel>` (world) or
`bonecs:<BoneOrSocket>.<channel>` (component space). Channels are `x|y|z`,
`pitch|yaw|roll`, `scalex|scaley|scalez`, sampled off the pawn's skeletal mesh.
Component space is what an animation assertion usually wants: it does not move
when the actor does. Per-frame performance (game/render/gpu ms, memory) is always
written to `recording.csv`. Recording starts on BeginPIE and finalises on EndPIE.

A bone entry that names a bone or socket the mesh does not have is simply absent
from the row rather than sampling zero, so a typo shows up as a missing column
instead of a flat line that reads like real data.

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

## Per-tick telemetry

`telemetry_start` records an actor's state every tick for a bounded window and hands
back a time series. Reach for it whenever one point-in-time read cannot answer the
question: did the jump leave the ground, how high did it rise, how long was it
airborne, did the impulse settle, did the blend finish, how long was the volume dwelt
in. Built-in fields are location, rotation, velocity, speed, grounded, falling and
distance_to_ground; `properties` adds dotted UPROPERTY paths (struct leaves come back
as objects) and `getters` adds no-argument UFUNCTIONs invoked per tick.

It returns immediately with an id, because a bridge handler runs on the game thread
and a handler that blocked would stop the ticks it is sampling. Poll
`telemetry_status` until `state=completed`, or end it early with `telemetry_stop`.
`call_on_start` fires the thing under test right after the baseline sample, so
"record, then jump" is one call rather than a race between two.

The recorder is bounded by `duration_seconds` and `max_samples` together, both
clamped, and is torn down on completion, on stop, on EndPIE, on module shutdown, and
by a wall-clock watchdog if the world stops advancing. A sampler cannot outlive the
call that started it.

Read the answer off `summary`, not the rows: numeric channels carry
first/last/min/max with the time of each extreme, vector channels are summarised per
component (so rise height is `location.z.max - location.z.first`), and boolean
channels carry their transitions plus seconds spent true and false (so air time is
`grounded.false_seconds`).

## Sequences and loops

`run_sequence` runs an ordered step list inside ONE game-thread dispatch. Use it when
a verification only means anything as a whole - create subsystem state, bind it to a
runtime-spawned actor's component, kick a flow off, step it until a flag clears, then
read two objects - and especially when another agent may be driving the same editor,
since a call-per-step sequence loses to whoever ends PIE between calls.

Steps are `spawn`, `set`, `call`, `read` and `loop`. Every step names its object
through the same address: a bare `target` string is an actor token, or an object
`{actor|actorLabel|subsystem|objectPath|ref, component?, viaProperty?}`. `component`
reaches a named component subobject on an actor that was spawned at run time;
`viaProperty` follows a UPROPERTY pointer to whatever it was made to point at during
the flow; `ref` names a value an earlier step captured. `capture` on a step records
its result under a name, and the names come back in `captured`.

`loop` takes `while` or `until` (a property predicate: eq, ne, lt, lte, gt, gte,
is_true, is_false) and a mandatory, capped `max_iterations`. The cap is not optional:
the same single dispatch that makes the run uninterruptible means an unbounded
predicate would hang the editor.

Nothing waits for a tick inside a run. The world does not advance, so a step drives
state through calls and reads; anything that needs elapsed time belongs in
`telemetry_start`.

`sample_loop` is the same guarantee for a different question: invoke a function N
times (capped) and snapshot a set of addressed properties between every turn.
Struct-valued properties come back as JSON objects rather than stringified blobs, and
each invocation's own return value is recorded beside the snapshot. `stop_when` ends
the loop early on a predicate. Use it to drive an ability activation while watching
struct fields on two gameplay records turn by turn.

## Input injection

`inject_input` / `inject_input_start` / `inject_input_tape` drive Enhanced Input
actions programmatically. `capture` grabs viewport frames on demand (decoupled from
replay) so inject/observe flows are visual too.
