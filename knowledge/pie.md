# PIE Record / Replay / Observe

pie-studio provisions the `pie` category: 34 actions for deterministic PIE recording, replay, observation, diffing, snapshots, and input injection. Actions are unprefixed - the category is the namespace. Targets Unreal Engine 5.8.

## Quick flow

1. `pie(action="record_arm")` - arm the recorder
2. `editor(action="play_in_editor")` - start PIE (recording begins automatically)
3. Play the game, reproduce a bug
4. Stop PIE or `pie(action="record_stop")` - recording saved
5. `pie(action="replay_arm", recording_id="<id>", record_drift=true)` - arm replay
6. `editor(action="play_in_editor")` - replay with drift tracking
7. `pie(action="replay_status")` - check drift metrics

## Unattended replay (no human)

`replay_run` collapses "arm + start PIE" into one call so an agent can reproduce a bug without anyone clicking Play. It defaults `auto_stop_pie=true`, so PIE ends itself when the run completes and `drift.json` is written.

1. `pie(action="replay_run", recording_id="<id>")` - arms and starts PIE; returns immediately
2. `pie(action="replay_status")` - poll until `pie_active` is false; `last_result` then carries the drift report path + peak drift
3. `pie(action="record_read", id="<id>", file="drift")` - read the finalized drift report

`recording_dir="<path>"` on its own names the source recording folder ("solve this bug in ./some-recording"). Same params as `replay_arm`. Requires the editor to be idle (no PIE session already running).

## Recording

`record_arm` configures what to capture: input actions, pawn state, tracked reflection paths, actor positions. Recording starts on BeginPIE and finalizes on EndPIE (or `record_stop`). Output lands in `Saved/MCPRecordings/<id>/`.

## Replay

`replay_arm` loads a recording's `sequence.json` and replays input through Enhanced Input injection. Drift sampling compares pawn location/rotation/velocity against the source recording frame-by-frame. `capture_frame_every` captures viewport frames as PNGs for visual comparison. `replay_run` is the unattended variant that also starts and stops PIE (see above).

## Observation

`observe_arm` attaches to a PIE session and samples actor state per frame using an observation profile (UDataAsset). Profiles define which actors and properties to track. Runs output to `Saved/MCPObservations/`.

## Input injection

`inject_input` / `inject_input_start` / `inject_input_tape` drive Enhanced Input actions programmatically during PIE. Useful for automated testing without replay.
