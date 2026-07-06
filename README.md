# pie-studio

PIE record, replay, observe, and input injection for [ue-mcp](https://github.com/db-lyon/ue-mcp). Built for **Unreal Engine 5.8**.

## Install

```bash
ue-mcp plugin install pie-studio
```

This installs the npm package, deploys the native C++ module to your project's `Plugins/` directory, and adds the plugin to your `ue-mcp.yml`. Rebuild the UE project before launching the editor.

## Editor UI

PIE Studio adds a toolbar button group to the UE5 editor (next to the Play controls) and a dockable panel accessible from the three-dot dropdown or **Tools > PIE Studio**.

### Toolbar

| Button | Action |
|--------|--------|
| Record (arm) | Arms the input recorder — waits for PIE to start, then captures all input |
| Record + Play | Arms the recorder and immediately starts PIE |
| Three-dot menu | Contextual options: arm, disarm, stop, open panel |

### Panel Sections

**Recorder** — status display showing current state, recording ID, frame count, and elapsed time.

**Time Scale** — slider and quick-set buttons (1%, 10%, 25%, 50%, 100%, 200%) to control PIE playback speed. Persists across PIE restarts.

**Recordings** — lists all saved recordings with:
- Play button to replay (arms replayer + active observation profiles, starts PIE, captures viewport frames, generates GIF)
- Delete button
- Collapsible GIF list per recording with Open (launches in default viewer) and Delete

**Observation Profiles** — manages `UMCPObservationProfile` data assets:
- Checkbox toggles to mark profiles as active
- Active profiles automatically observe during replay
- Create, Edit (opens UE asset editor), Delete, Refresh
- Multiple profiles run simultaneously, each producing independent output

## Observation Profiles

Observation profiles are UDataAssets that control what gets sampled during replay. Create them from the panel or the Content Browser.

| Field | Description |
|-------|-------------|
| **Tracked Values** | Gameplay properties to observe (e.g. `CharacterMovement.Velocity.X`). Each can override the drift threshold. |
| **Tracked Actors** | Actors to track by ID — position, rotation, velocity sampled each frame. |
| **Capture Pawn State** | Sample pawn transform, velocity, movement state each frame. |
| **Capture Montage** | Sample active anim montage name and position. |
| **Drift Thresholds** | Minimum change to count as divergence. Filters physics/animation jitter. Position (cm), Rotation (deg), Velocity (cm/s), and a default for tracked values. |

## GIF Capture

Every replay automatically captures viewport frames and encodes an animated GIF (no external dependencies — built-in LZW encoder). GIFs are saved to `<recording>/captures/replay_<timestamp>.gif`, scaled to max 720px wide.

## Unattended replay

`replay_run` lets an AI agent drive a recording end-to-end with no human touching the editor. It arms the replayer **and** starts PIE in a single call, then ends PIE and writes `drift.json` when the run completes. So "hey Claude, solve this bug in `./some-recording`" becomes:

```
pie(action="replay_run", recording_dir="C:/proj/Saved/MCPRecordings/some-recording")
# poll until PIE has torn down:
pie(action="replay_status")            # -> pie_active: false, last_result: { drift_report_path, ... }
pie(action="record_read", id="some-recording", file="drift")
```

`replay_run` takes the same params as `replay_arm` (`recording_id`, inline `steps`, `time_scale`, `drift_thresholds`, `capture_frame_every`, …). It defaults `auto_stop_pie=true`; pass `false` to leave PIE up for inspection. `recording_dir` alone names the source folder directly. Contrast with `replay_arm`, which only arms and still needs a separate `editor(action="play_in_editor")` (and a human) to start PIE.

## MCP Actions

34 actions in the `pie` category (provisioned by the plugin; call as `pie(action="...")`):

- **Recording** — `record_arm`, `record_disarm`, `record_stop`, `record_status`, `record_list`, `record_read`, `record_delete`, `mark`
- **Replay** — `replay_arm`, `replay_run` (unattended), `replay_disarm`, `replay_stop`, `replay_status` with drift tracking and viewport capture
- **Observation** — `observe_arm`, `observe_disarm`, `observe_stop`, `observe_status`, `observe_list`, `observe_read` with profile-based sampling
- **Input injection** — `inject_input`, `inject_input_start`, `inject_input_update`, `inject_input_stop`, `inject_input_tape`
- **Profiles** — `profile_create`, `profile_read`, `profile_update`, `profile_delete`, `profile_list`
- **Diff / Snapshot** — `record_diff`, `snapshot`
- **PIE inspection** — `anim_state`, `anim_properties`, `subsystem_state`

## Data Layout

```
<Project>/Saved/MCPRecordings/
  <recording-id>/
    manifest.json        # recording metadata
    sequence.json        # input sequence
    recording.csv        # frame-by-frame state
    drift.json           # replay drift report
    captures/
      replay_20260527_171430.gif
      replay_20260527_183200.gif

<Project>/Saved/MCPObservations/
  obs_<profile>_<timestamp>/
    manifest.json
    observation.csv
    tracked.jsonl
```

## Develop

```bash
npm install
npm run build
```

See [ue-mcp plugin docs](https://ue-mcp.com/docs/plugins/) for the full author contract.
