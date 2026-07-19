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

## Frame Capture

When `capture_frame_every` is set, replay grabs viewport frames as **JPEGs** (kept on disk under `<recording>/frames/`) and composes a single labeled **contact sheet** at `<recording>/captures/contact_<timestamp>.jpg` — a grid montage of keyframes with the frame index drawn on each cell. A vision model reads stills, so the frames and the contact sheet are the useful artifacts; the paths come back in `replay_status.last_result` (`frame_dir`, `frame_count`, `contact_sheet_path`).

Animated GIF is now **opt-in** (`encode_gif=true`). A vision model cannot parse GIF animation, so it is off by default and exists only for human eyeballing.

`capture` grabs frames from the live PIE viewport on demand, decoupled from replay, so observe and inject flows can be visual too.

## Session Errors

`session_errors` returns the deduped errors and warnings captured from the Output Log during a PIE session (including Blueprint script exceptions), so an agent can ask "what errored" first. `session_log` returns the paged raw log with verbosity/category/substring filters. Both default to the live or most-recent session; pass a `session` id to read a finished one from `Saved/MCPSessions/`.

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

45 actions in the `pie` category (provisioned by the plugin; call as `pie(action="...")`):

- **Recording** — `record_arm`, `record_disarm`, `record_stop`, `record_status`, `record_list`, `record_read`, `record_delete`, `mark`
- **Replay** — `replay_arm`, `replay_run` (unattended), `replay_disarm`, `replay_stop`, `replay_status` with drift tracking and viewport capture
- **Analysis** — `replay_analyze` (first-divergence lead + errors + images), `replay_state` (deterministic scrub/snapshot)
- **Profiling** — `perf_summary` (frametime p50/p99, GPU, hitches), `trace_start`, `trace_stop` (Unreal Insights `.utrace`)
- **Reproduction tests** — `test_scaffold`, `test_run`, `test_list`
- **Observation** — `observe_arm`, `observe_disarm`, `observe_stop`, `observe_status`, `observe_list`, `observe_read` with profile-based sampling
- **Input injection** — `inject_input`, `inject_input_start`, `inject_input_update`, `inject_input_stop`, `inject_input_tape`
- **Profiles** — `profile_create`, `profile_read`, `profile_update`, `profile_delete`, `profile_list`
- **Diff / Snapshot** — `record_diff`, `snapshot`
- **PIE inspection** — `anim_state`, `anim_properties`, `subsystem_state`
- **Session log** — `session_errors`, `session_log`
- **Capture** — `capture` (standalone viewport frames + contact sheet)

## Data Layout

```
<Project>/Saved/MCPRecordings/
  <recording-id>/
    manifest.json        # recording metadata
    sequence.json        # input sequence
    recording.csv        # frame-by-frame state
    drift.json           # replay drift report
    frames/              # per-frame viewport JPEGs (kept)
      frame_00000.jpg
      frame_00001.jpg
    captures/
      contact_20260527_171430.jpg   # labeled grid montage
      replay_20260527_183200.gif    # only when encode_gif=true

<Project>/Saved/MCPSessions/
  <timestamp>/
    session_log.jsonl    # captured Output Log
    session_errors.json  # deduped errors + warnings

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
