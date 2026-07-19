# ROADMAP Implementation Progress

## Build + test method (all items verified)

The user provided a ue-mcp-enabled project to build against:
`C:\Users\david\Projects\UE\ue-mcp\tests\ue_mcp\ue_mcp.uproject` (UE 5.8, has the
`UE_MCP_Bridge` plugin). The repo plugin is junctioned into that project's `Plugins/`
so edits build directly.

- **Build:** `Engine/Build/BatchFiles/Build.bat ue_mcpEditor Win64 Development -project=<uproject>`
- **Test:** `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests PIEStudio" -unattended -nullrhi -TestExit="Automation Test Queue Empty"`

Every item below is **DONE**: compiles clean against UE 5.8 + ue-mcp, and its automation
test(s) pass. Final state: **8/8 PIEStudio automation tests green.**

`DONE` = built + tests pass on UE 5.8.

## Dependency order (execution plan)

1. **1a** session log/error capture — no deps
2. **1b** capture pipeline — no deps
3. **4a** perf sampling — no deps (extends frame sampler)
4. **4b** Insights trace — no deps
5. **2b** timeline introspection — deps: 1a conventions
6. **1c** drift analysis — deps: 1a (error correlation), 1b (bracketing frames)
7. **2a** state replay — deps: 1b (capture surface)
8. **3a** functional test scaffolding — deps: 1a, 1b, 2a
9. **cross-cutting docs** — after the features they document

## Status

| # | Item | Status | Test |
|---|------|--------|------|
| 1a | `FPIESessionLog` + `session_errors` + `session_log` | DONE (PR #2) | PIEStudio.SessionLog.CapturesErrorsAndWritesArtifacts |
| 1b | JPEG + keep frames + GIF opt-in + contact sheet + `capture` | DONE (PR #2) | PIEStudio.ContactSheet.ComposesDecodableJpeg |
| 1c | drift `summary` block + `replay_analyze` | DONE (PR #2) | PIEStudio.Drift.SummaryRoundTrips |
| 2a | `replay_state` deterministic scrub/snapshot + apply | DONE (PR #2) | PIEStudio.StateReplay.ScrubInterpolates |
| 2b | observer `observe_read` series + `sub:` subsystem sampling | DONE (PR #2) | PIEStudio.Observe.SeriesFromCsv |
| 3a | `test_scaffold` + `test_run` + `test_list` | DONE (PR #2) | PIEStudio.ReproTest.ScaffoldListRun |
| 4a | per-frame perf sampling + `perf_summary` | DONE (PR #2) | PIEStudio.Perf.SummaryFromCsv |
| 4b | `trace_start`/`trace_stop` | DONE (PR #2) | PIEStudio.Perf.TraceStartStop |
| X1 | determinism honesty in docs | DONE (PR #2) | n/a (docs) |
| X2 | fixed-timestep knob (replay) | DONE (PR #2) | built (FApp save/restore) |
| X3 | format v2 + backward-compatible readers | DONE (PR #2) | covered by Drift round-trip |
| N1 | no Gauntlet embed (non-goal) | DONE | intentionally not built |
| N2 | no lockstep determinism (non-goal) | DONE | intentionally not built |
| N3 | DemoNetDriver deferred (non-goal) | DONE | intentionally not built |

All C++ work lands on branch `roadmap-phase1-signal-and-surface` (PR #2). The 8 automation
tests live under `Source/PIE_Studio/Private/Tests/`.
</content>
