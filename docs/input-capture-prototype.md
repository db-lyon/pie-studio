# Physical input capture prototype

## Status

**DRAFT / experimental.** This is a bounded implementation proposal based on
PIE Studio 0.5.2 and Unreal Engine 5.8. It is not acceptance-ready: focused live
verification passed, but the Unreal automation suite has not been run for this
candidate and the production decisions below remain open.

## Problem and hypothesis

`FPIEFrameSampler` samples Enhanced Input action values after input processing.
Physical events handled by UMG/CommonUI need not produce a sampled action value.
A mouse press whose handler shows a UI widget and changes the active mapping
context or flushes pressed keys can disappear before the sampler reads it.
Recordings can therefore omit the press that activates a widget or a keyboard
shortcut consumed by that widget.

Capturing the physical event before its handler runs preserves those events.
The listener must return `false` so recording does not consume the player's input.

This proposal helps drive a UI flow unattended. It does not promise deterministic
physics, animation, camera movement, or reproduction of a complete game session.

## Implementation

- `PIEMousePrototype` registers a Slate input preprocessor during recording and
  unregisters it when recording stops.
- Absolute pointer movement and left clicks are captured for the visible cursor
  inside the selected local player's game viewport. Positions are normalized to
  the viewport geometry, not the desktop.
- A left click that starts with the cursor hidden is recorded as a gameplay key
  edge, including its matching release after a mode change. Hidden-cursor mouse
  movement remains in the Enhanced Input stream to avoid applying look twice.
- Keyboard press/release events are captured for the focused PIE viewport,
  including repeat, key/character codes, and modifier state. Gameplay keys use
  `PlayerController::InputKey`; UI keys use Slate's key event processing path.
- UI pointer replay uses Slate move/button processing. The move event is routed
  with `bIsSynthetic=false`, which is needed for the captured viewport's ordinary
  mouse-move handler to update world-space widget hit testing.
- Sampled action steps corresponding to recorded physical inputs are filtered to
  avoid injecting the same input twice. This policy needs refinement for mixed
  input sources.
- A recording-local `mouse-prototype.json` sidecar stores the events on a wall
  clock. Existing recordings without the sidecar retain their action replay path.
  No new MCP action or handler schema is introduced.

The prototype currently assumes the first local player and Slate user 0. Mouse
buttons other than the left button, wheel input, gamepad event capture, and text
entry/IME are outside its current scope.

## Generic reproduction recipe

Use a disposable UE 5.8 project with Enhanced Input and UMG/CommonUI:

1. Bind an Enhanced Input action to the left mouse button. In its handler, show
   a UMG widget, change the active mapping context, and flush pressed keys.
2. Add a clickable button whose pointer callback changes a visible widget
   property, such as a label or counter, without a gameplay input action.
3. Add a CommonUI keyboard binding that hides the widget and restores gameplay
   input. Any key can be used; the key itself is not significant.
4. Record the physical activation press, button click, and shortcut, then replay from
   the same initial world and camera state.
5. Compare the visible states and input artifacts, rather than relying only on
   the replayer's `completed` status.

This recipe has not yet been packaged as a standalone fixture. The expected
failure is a missing widget input event or a lost activation press during the
input-mode transition. With physical capture, the sidecar should contain the
activation down/up pair, button click pair, and shortcut down/up pair.

## Acceptance and observed evidence

| Acceptance | Observation on UE 5.8 |
|---|---|
| Recording leaves physical input responsive | Physical UI clicks and a keyboard shortcut responded during recording |
| A physical click on a button that resumes paused PIE replays | Verified in a focused live PIE check |
| A gameplay press survives showing a UI widget | Captured the activation down/up pair across the input-mode change |
| Replay activates a widget rendered by a Widget Component, executes button callbacks, and restores gameplay input | Verified from the same arranged starting view in a fresh PIE session by observing the callbacks' state changes |
| A UI-handled keyboard shortcut is captured and replays | Before: the relevant sampled action values were all zero. After: physical key down/up captured; replay of that captured pair switched the arranged UI state back to gameplay |
| Hidden-cursor absolute motion does not duplicate look input | Capture/replay guards were added after observing a camera jump; broader movement fidelity remains unverified |
| Native module builds | The module compiled and linked in a UE 5.8 Win64 Development Editor build |
| Full Unreal automation suite passes | **Not run for this candidate** |

The world-space UI check removed leading idle time and isolated the requested
interaction segment. The keyboard replay isolated the actual captured key pair
and arranged the same UI state before playback. These checks
do not establish fidelity of the complete walking/camera recording.

The plugin's existing deprecated `BlockUntilGPUIdle` warning was observed and was
not changed by this prototype.

## Remaining decisions before acceptance

- Choose an explicit recording option and supported input-routing policy instead
  of treating cursor visibility as the game/UI boundary.
- Integrate a documented, versioned event format and validate malformed input;
  decide whether physical events belong in `sequence.json` or a declared sidecar.
- Reconcile physical and sampled actions per source/time interval. The current
  keyboard filter can remove sampled steps from another source sharing an action.
- Specify pause, replay time scaling, focus loss, held-key cleanup, and ordering
  across transitions. Wall-clock playback at normal speed is the tested case.
- Resolve selected PIE client/Slate user identity and multiplayer behavior.
- Add a small portable UI reproduction and focused regressions for lost opening
  presses, consumed UI shortcuts, duplicate look, and recorder/replayer teardown.
- Run the existing `PIEStudio` automation suite before marking this work complete.

## Build and test reporting

Follow the repository README for the TypeScript build:

```sh
npm install
npm run build
npx tsc --noEmit
```

The native source is under `ue/Plugins/PIE_Studio`. Deploy that module to a
disposable UE 5.8 test project with UE-MCP, close its Editor, and build its Editor
target with UnrealBuildTool. As documented in `ROADMAP_PROGRESS.md`, run the
existing native suite with:

```text
UnrealEditor-Cmd.exe <test.uproject> -ExecCmds="Automation RunTests PIEStudio" -unattended -nullrhi -TestExit="Automation Test Queue Empty"
```

Headless checks do not verify physical Slate pointer routing. Report that live
verification separately, with the initial state and observed post-state.
