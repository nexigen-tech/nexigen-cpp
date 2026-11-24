# Proposal

## Why

The complexity gate is red. `python3 cmake/check_complexity.py` exits 1 with **18 functions above CCN 15** across `tools/`, and the worst is a single function: `nexigen::logview::run_ui` at **118 CCN / 318 NLOC**, committed in `b4f4993` when the multi-view tabs landed. `spec quality/code-structure` requires every function in `include/`, `src/`, `tests/` and `tools/` to stay at or below 15, so this blocks CI and makes the new log viewer effectively unreviewable: two functions (`run_ui` 118, `draw_local_controls` 47) hold 165 of the 348 decision points added by the viewer.

This change executes analysis recommendations 1–8 and closes the remaining gate violations so the tree is back inside its own contract.

## What Changes

- **`run_ui` (118 → ≤15)**: split into an initialization step, a capture step, a splash phase, an actions-test phase, a frame loop and a result-reporting step. The frame loop keeps a small body and delegates input scripting, focus-script state, view close handling, per-view drawing and test completion to named helpers. The tab strip becomes an explicit descriptor table (name, draw handler, input handler) instead of nested branching.
- **`draw_local_controls` (47 → ≤15)**: split into per-group builders — live/pause state, navigation and bookmark actions, level filters, text filter, source filter popup, find controls, time/timestamp controls, rebuild progress and counters.
- **`run_ui_view_test` (38) and `drive_view_test` (20) → ≤15**: move the scripted phases into a data-driven step list (input event, expected state predicate) evaluated by one small driver.
- **`LogView::copy_state_from` (27 → ≤15)**: split into filter-criteria copy, time-settings copy (including T0 validation), and selection/bookmark replay; the main body becomes an ordered sequence of those calls.
- **`suite_views` (30) and `suite_paused_views` (35) → ≤15**: extract a shared scenario builder (model + seeded frames + N source views + drained rebuilds) and keep each test function to its own assertions.
- **Parsers**: `import_line` (20) → per-format importers behind a dispatcher; `parse_iso8601_ns` (19) → shape validation + field extraction + civil-date conversion; `from_json` in `logview_config.cpp` (19) and `json_escape` (16) → per-field/per-character-class helpers.
- **`run_session` in `tools/nexigen-logd` (16 → ≤15)**: separate transport/consumer setup from the wait-and-dispatch loop.
- **`bench::parse_options` (30 → ≤15)**: replace the flat `if`-chain with an option table whose entries carry an argument kind and a small per-group apply function.
- **Remaining gate violations not named by recommendations 1–8** are included because the gate must go green: `draw_global_toolbar` (23), `main` in `logview/main.cpp` (21), `run_headless` (17), `main` in `logview_transport_test.cpp` (18), `draw_log_view` (16), `draw_clone_modal` (16).
- **No behavior change**: identical CLI flags, stdout/stderr text, exit codes, JSON fields, test names, assertion counts and rendering. No threshold or gate change (recommendation 9 is explicitly out of scope), no feature work from the in-flight `add-logview-latest-and-splash` change.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is a behavior-preserving refactor that conforms to the existing `quality/code-structure` requirements ("Complexity ceiling is enforced across the maintained source tree", "Complexity refactors preserve observable behavior and public surface", "Test suites stay within the ceiling without losing coverage"). The change therefore declares `skip_specs: true` rather than inventing a delta.

## Impact

- **Code**: `tools/nexigen-logview/ui/logview_window.cpp`, `tools/nexigen-logview/model/{log_view,importer,logview_config,json_escape}.cpp`, `tools/nexigen-logview/{main,headless}.cpp`, `tools/nexigen-logview/tests/{logview_model_test,logview_transport_test}.cpp`, `tools/nexigen-logd/main.cpp`, `benchmarks/recorder/bench_types.cpp`. New file-local helpers stay in their existing translation units; no new public headers.
- **APIs**: none. No public header of `include/nexigen` is touched. The log-viewer model library keeps its declarations; only the bodies are decomposed.
- **Performance / latency (ns)**: no hot-path code is touched. Every function in scope is cold path — UI rendering, log import, JSON configuration parsing, session/CLI setup — and none is called from a producer, worker or sink path. No extra virtual calls or allocations are introduced. The existing latency gates (`nexigen_logging_tests` latency suite, `build-verify-lat`) must report the same numbers as before within measurement noise; any measurable delta is a failure of this change.
- **Memory**: no allocation is added or removed. The tab descriptor table is a `constexpr`/static array of function pointers; test scenario tables are static arrays. The UI state struct is unchanged.
- **Behavior**: identical output and exit codes; `nexigen-logview` smoke/view/focus/splash/actions test modes print the same lines, and the CLIs of `nexigen-logd` and the recorder benchmark keep every flag and error path.
- **CI**: `nexigen_complexity_gate` returns to green (18 violations → 0), and `complexity-check` reports the same verdict.
- **Out of scope**: extending the gate to new directories or changing the 15 threshold (recommendation 9); cleaning the seven `TODO` markers in the vendor CAN driver (recommendation 10); any new viewer feature.
