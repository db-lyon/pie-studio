# pie-studio

PIE record, replay, observe, and input injection for [ue-mcp](https://github.com/db-lyon/ue-mcp). Built for **Unreal Engine 5.8**.

## Install

```bash
ue-mcp plugin install pie-studio
```

Requires ue-mcp 1.4.8-beta.1 or later. This installs the npm package, deploys the native C++ module to your project's `Plugins/` directory, and adds the plugin to your `ue-mcp.yml`. Rebuild the UE project before launching the editor.

## Editor UI

PIE Studio adds a toolbar button group to the UE5 editor (next to the Play controls) and a dockable panel accessible from the three-dot dropdown or **Tools > PIE Studio**.

### Toolbar

| Button | Action |
|--------|--------|
| Record (arm) | Arms the input recorder — waits for PIE to start, then captures all input |
| Record + Play | Arms the recorder and immediately starts PIE |
| Three-dot menu | Contextual options: arm, disarm, stop, open panel |

### Panel Sections

**Recorder**: status display showing current state, recording ID, frame count, and elapsed time.

**Time Scale**: slider and quick-set buttons (1%, 10%, 25%, 50%, 100%, 200%) to control PIE playback speed. Persists across PIE restarts. From an agent, use core's `editor(set_pie_time_scale)`.

**Recordings**: lists all saved recordings with:
- Play button to replay (arms replayer + active observation profiles, starts PIE, captures viewport frames, generates GIF)
- Delete button
- Collapsible GIF list per recording with Open (launches in default viewer) and Delete

**Observation Profiles**: manages `UMCPObservationProfile` data assets:
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

## Multiplayer PIE targeting

Input, recording, replay, and observation actions accept optional `pie_instance`.
It selects exact `FWorldContext::PIEInstance`; `client_id` remains local-player
index inside selected world. Explicit selectors fail when instance is missing,
dedicated-server-only, or missing requested local player.
Supplied `pie_instance` values must be finite exact non-negative integers within the int32 range; invalid values are rejected rather than treated as automatic selection.

When `pie_instance` is omitted, PIE Studio first prefers eligible
`GEditor->PlayWorld`, then first eligible PIE/Game world containing requested
local player. This avoids dedicated-server-first mismatch in multiplayer PIE.
Selected world and player stay fixed for each recording, replay, observation
session, hold, and tape.
Unresolved recorder, replay, and observation targets are retried for up to 10
seconds from `BeginPIE`; they then finalize as failures and retain `last_error`
in their status responses, including after natural `EndPIE`.

### Remote players

With **Run Under One Process** off, every client is a separate `-game` process that the editor cannot reach directly. PIE Studio attaches a replicated control component to each remote player's controller on the server and relays commands over the game connection, so the same actions drive any player:

- `clients` lists the session: net mode, the host player, and each remote player as `client` 1..N with a `ready` flag.
- `inject_input`, `inject_input_start` and `inject_input_tape` take `client`; 1 or more relays to that remote player. `inject_input_update` and `inject_input_stop` route by the returned id.
- `console` runs a command on a remote player (`client` 1..N). `client` 0 is the editor's local player console, the same path as core's `editor(execute_command, world="pie")`; use that core action when no remote players are involved.
- `remote_result` reports whether a relayed request was applied and, for tapes, finished.

The runtime half lives in the `PIE_StudioRuntime` module (`DeveloperTool`), which loads in the client processes.

## Multiplayer acceptance runs

`uat_run` runs a scripted multiplayer acceptance test with no human at the keyboard. It opens the scenario's map, starts PIE with its players, waits for every remote player to join, then runs each step: drive input or console on any player, and judge the step from the logs. The evidence is the editor's own log plus each separate-process client's own log file, whose path the client reports when it joins (`clients` shows it as `log_file`). `log: "client:0"` is the listen server's player, which logs to the host log. A dedicated server needs `one_process: true` (the default for `dedicated`): a separate-process server runs outside the editor, where the run cannot drive or read it. When the run ends it stops PIE, restores the play settings and writes `Saved/PIEStudio/UAT/<name>_<stamp>/report.md` and `report.json`. Poll `uat_status`; `uat_abort` ends a run early.

```json
{
  "name": "coop-attack",
  "map": "/Game/Maps/Test/L_CombatTest",
  "pie": { "players": 2, "net_mode": "listen", "one_process": false },
  "steps": [
    {
      "name": "P2 attacks",
      "do": [ { "client": 1, "press": { "action": "/Game/Input/IA_Primary.IA_Primary" } } ],
      "expect": [
        { "log": "client:1", "pattern": "\\[Attack\\]" },
        { "log": "host", "pattern": "plays Light01" }
      ],
      "forbid": [ { "log": "host", "pattern": "Error" } ],
      "window_ms": 4000
    }
  ]
}
```

Step actions: `tape {action, values}`, `press {action, frames}`, `hold {action, value, ms}`, `inject {action, value}`, `console "cmd"`, `place {at, offset, face}` or `place {location, yaw}` (each with `client`), and `wait_ms`. `place` teleports that player's pawn on the server, to the nearest free spot within 360 units when the exact one is taken, and turns it on its own machine (a remote player owns its facing, so the step fails if that client never confirms the turn): next to the actor named or labelled `at` (`offset` in its local frame, default 300 units in front, facing it), or to an absolute `location`. `capture` (a label, or `true`) saves that player's view as a JPEG under the report's `captures/` folder; the step fails if the image is not written, and `report.md` embeds every capture next to its step, so a vision pass can judge what the logs cannot show.

### Windowless clients

`pie.windowless: true` launches each separate-process client with `-RenderOffscreen`: the client renders every frame exactly as it would in a window (animation, hit traces and captures behave the same), but opens no window and never takes focus. Its frame rate is pinned to `pie.client_fps` (default 60) because no vsync paces an offscreen client. The run restores the play settings' launch parameters afterwards. `expect` rules take `min`/`max` match counts, and `since: "run"` to judge every line logged since PIE started (state set up while players joined) instead of only the step's own lines. A step passes when every expectation is met and no `forbid` pattern matched inside its window. It ends early once its expectations are met, unless it forbids something, in which case it watches the whole window.


## Frame Capture

When `capture_frame_every` is set, replay grabs viewport frames as **JPEGs** (kept on disk under `<recording>/frames/`) and composes a single labeled **contact sheet** at `<recording>/captures/contact_<timestamp>.jpg` — a grid montage of keyframes with the frame index drawn on each cell. A vision model reads stills, so the frames and the contact sheet are the useful artifacts; the paths come back in `replay_status.last_result` (`frame_dir`, `frame_count`, `contact_sheet_path`).

Animated GIF is now **opt-in** (`encode_gif=true`). A vision model cannot parse GIF animation, so it is off by default and exists only for human eyeballing.

For a frame of the live PIE viewport on demand, decoupled from replay, use core's `editor(capture_screenshot, target="pie")` or `editor(capture_scene_png, world="pie")`.

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

58 actions in the `pie` category (provisioned by the plugin; call as `pie(action="...")`), 9 of them deprecated (below):

- **Recording**: `record_arm`, `record_disarm`, `record_stop`, `record_status`, `record_list`, `record_read`, `record_delete`, `mark`
- **Replay**: `replay_arm`, `replay_run` (unattended), `replay_disarm`, `replay_stop`, `replay_status` with drift tracking and viewport capture
- **Analysis**: `replay_analyze` (first-divergence lead + errors + images), `replay_state` (deterministic scrub/snapshot)
- **Profiling**: `perf_summary` (frametime p50/p99, GPU, hitches). Unreal Insights traces are core's `editor(start_trace)` / `editor(stop_trace)`
- **Reproduction tests**: `test_scaffold`, `test_run`, `test_list`
- **Observation**: `observe_arm`, `observe_disarm`, `observe_stop`, `observe_status`, `observe_list`, `observe_read` with profile-based sampling
- **Input injection**: `inject_input`, `inject_input_start`, `inject_input_update`, `inject_input_stop`, `inject_input_tape`
- **Remote players**: `clients`, `console`, `remote_result`
- **Live actors** live in core: `level(spawn_transient_actor|destroy_transient_actor, world="pie")`, `editor(set_object_property|invoke_object_function, world="pie")`. Scenario `arrange` steps keep `spawn`, `set` and `call`.
- **Acceptance runs**: `uat_run`, `uat_status`, `uat_abort`
- **Profiles**: `profile_create`, `profile_read`, `profile_update`, `profile_delete`, `profile_list`
- **Diff**: `record_diff`
- **PIE inspection**: `anim_state`, `anim_properties`. Actor and subsystem properties are core's `editor(get_object_properties, world="pie")` (`target="subsystem"` with `subsystemClass`) and `editor(get_runtime_values)`
- **Session log**: `session_errors`, `session_log`

### Deprecated actions

These duplicate core actions. They keep working for one release so installed setups and saved calls do not break, each result carries a `deprecated` field naming its replacement, and the next release removes them. Use the core action instead:

| pie action | core action |
|---|---|
| `trace_start`, `trace_stop` | `editor(start_trace)`, `editor(stop_trace)` |
| `capture` | `editor(capture_screenshot, target="pie")`, `editor(capture_scene_png, world="pie")` |
| `actor_spawn`, `actor_destroy` | `level(spawn_transient_actor, world="pie")`, `level(destroy_transient_actor)` |
| `actor_set`, `actor_call` | `editor(set_object_property)`, `editor(invoke_object_function)` |
| `snapshot`, `subsystem_state` | `editor(get_object_properties)` (`target="subsystem"` with `subsystemClass` for a subsystem), `editor(get_runtime_values)` |

Scenario `arrange` steps (`spawn`, `set`, `call`) are not deprecated.

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
