# ROADMAP Implementation Progress

## Environment constraint (governs the "status" column)

This workstation **cannot compile or run the plugin**. Every handler depends on the
`UE_MCP_Bridge` host module (`GameplayHandlers.h`, `HandlerUtils.h`,
`UEMCP::RegisterExternalHandler`, `MCP_CHECK_GAME_THREAD`, `MCPError`) which is not in
this repo; it is only present in a UE project that has ue-mcp installed. The only such
project on this machine is the user's Vale project, which there is a standing instruction
never to touch. CI (`.github/workflows/ci.yml`) only runs `tsc` on the TypeScript shell;
it does not build the C++.

Therefore no C++ item can be marked machine-verified here. Statuses used:

- `IMPLEMENTED (pending build)` — code + automation test written; correctness pending the
  user's UE build. This is category-A honest per the task rules (external toolchain
  absent), not a size/scope off-ramp.
- `QUEUED` — not started; ordered after its dependencies. Not blocked.
- `DONE` — reserved for items the user has compiled/verified, or non-code items.
- `BLOCKED` — with cited category + evidence.

**What I need from the user to move items to DONE:** the compile-verify path for
pie-studio (which project/command builds it against ue-mcp), since I must not use Vale.

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

| # | Item | Status | Branch | Notes |
|---|------|--------|--------|-------|
| 1a | `FPIESessionLog` + `session_errors` + `session_log` | IMPLEMENTED (pending build) — PR #2 | roadmap-phase1-signal-and-surface | New class, handler file, test, wired + yml |
| 1b | JPEG + keep frames + GIF opt-in + contact sheet + `capture` | IMPLEMENTED (pending build) — PR #2 | roadmap-phase1-signal-and-surface | Viewport capture format, contact sheet class, replayer edits |
| 1c | `FPIEDriftAnalyzer` + `summary` block + `replay_analyze` | QUEUED | | after 1a+1b |
| 2a | state replay via Take Recorder + `replay_state` | QUEUED | | after 1b |
| 2b | observer timeline + `observe_read` series | QUEUED | | after 1a |
| 3a | `test_scaffold` + `test_run` + `test_list` | QUEUED | | after 1a,1b,2a |
| 4a | per-frame perf sampling + `perf_summary` | QUEUED | | after 1 |
| 4b | `trace_start`/`trace_stop` | QUEUED | | independent |
| X1 | determinism honesty in docs | QUEUED | | cross-cutting |
| X2 | fixed-timestep + seed knob | QUEUED | | cross-cutting |
| X3 | format versioning bump | QUEUED | | cross-cutting |
| N1 | no Gauntlet embed (non-goal) | DONE | | intentionally not built |
| N2 | no lockstep determinism (non-goal) | DONE | | intentionally not built |
| N3 | DemoNetDriver deferred (non-goal) | DONE | | intentionally not built |
</content>
