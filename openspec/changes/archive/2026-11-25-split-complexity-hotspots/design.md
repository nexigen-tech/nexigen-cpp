# Design

## Context

The gate (`cmake/check_complexity.py`, `lizard -C 15 include src tests tools`) exits 1 with 18 violations, all under `tools/`. `benchmarks/` is outside the gate but `bench::parse_options` (30) is the same kind of hotspot and is in scope.

The shape of each hotspot is known from the sources:

- `run_ui` (`ui/logview_window.cpp`, line 2111, 318 NLOC) interleaves six phases: UI-state initialization, capture startup, splash loading/run, the actions-test short-circuit, the frame loop, and result reporting. Inside the loop it also inlines the focus-test input script, the focus-test state machine, close-request collection, and the test-completion decision.
- `draw_local_controls` (line 447, 197 NLOC) is a single horizontal strip: pause badge, navigation/bookmark/clone/save actions, level checkboxes, text filter, source popup, find controls, timestamp/delta/anomaly combos, rebuild progress, counters.
- `run_ui_view_test` (38) and `drive_view_test` (20) encode scripted UI phases as nested conditionals over `state.view_test_phase`.
- `LogView::copy_state_from` (27) copies filter criteria, time settings (with T0 containment validation), rebuild state, selection and bookmarks in one body.
- `suite_views` (30) and `suite_paused_views` (35) each build a model, seed frames, create views and assert; the setup is duplicated by hand.
- `import_line` (20) dispatches JSON-lines and text lines; `parse_iso8601_ns` (19) validates shape and performs civil-date maths; `from_json` (19) probes config fields; `json_escape` (16) escapes by character class.
- `run_session` in `nexigen-logd` (16) sets up transport and then runs the wait loop.
- `bench::parse_options` (30) is a flat `if`-chain over flags and value options.

No concurrent or atomic component is touched: the log-viewer capture thread, the log daemon's subscriber loop and the recorder benchmark keep their existing threading, `std::memory_order` choices and `alignas(64)` layouts untouched. No shared-memory or per-thread shard sizing changes.

## Goals / Non-Goals

**Goals:**

- Bring every function in `include/`, `src/`, `tests/`, `tools/` to CCN ≤ 15, so `nexigen_complexity_gate` and `complexity-check` pass, plus `bench::parse_options`.
- Preserve observable behavior exactly: CLI flags, stdout/stderr lines, exit codes, JSON fields, view rendering order, test names, assertion counts.
- Make each decomposed unit individually testable where a seam exists (`importer`, `copy_state_from`, UI test drivers), without adding test-only code to production paths.

**Non-Goals:**

- Changing the threshold, the gate's scope, or adding directories to it (recommendation 9).
- Changing any spec-level behavior; the change sets `skip_specs: true`.
- Implementing or modifying viewer features from the in-flight `add-logview-latest-and-splash` work.
- Cleaning `TODO` markers in the vendor CAN driver (recommendation 10).
- Refactoring `include/`/`src/` functions that are already within the ceiling.

## Decisions

### D1 — `run_ui` becomes phase functions plus a frame-loop orchestrator

Extract, in call order:

1. `initialize_ui_state(model, options)` — the current field assignments, ini path creation, and per-view `ui_for` priming.
2. `start_capture(options, state, capture)` — capture thread creation and its failure status.
3. `load_splash(options)` — returns `std::optional<SplashImage>` and applies the test-failure injection.
4. `run_splash_phase(...)` — the splash-test and short-splash branches, including the fall-through rule.
5. `run_actions_test(...)` — the `options.actions_test` short-circuit.
6. `run_frame_loop(...)` — the `while` loop, whose body calls `update_focus_script_input`, `draw_workspace_frame`, `collect_closed_views`, `update_focus_test_state`, `draw_and_present_frame`, `frame_test_done`.
7. `report_ui_result(options, model, state, focus_ok, view_ok)` — the existing `printf` blocks and the final return value.

`run_ui` then reads as: init → view/focus test → GUI init → capture → splash → actions → frame loop → report. Each phase is ≤ 14 branches.

*Alternative:* keep `run_ui` as a single body and only extract the loop. Rejected: the loop alone is ~60 decisions, so it would still fail the gate.

### D2 — The pane dispatch stays one explicit sequence

The viewer has no conditional tab strip: every view is its own dock window drawn by a single loop over `model.view_at(i)`. That loop, together with the toolbar, sources, workspace and scripted-driver calls, moves into `draw_workspace_frame`, which the frame loop calls exactly once per frame. A `TabSpec` table would have no variants to dispatch over, so none is introduced; the win comes from making the frame loop read as one sequence and from the per-view draw being a single call. Ordering, `SameLine()` calls and `PushID` scopes are preserved so the rendered frame is identical.

### D3 — `draw_local_controls` becomes per-group builders

One function per group, called in the current order: `draw_view_state_badge`, `draw_view_actions`, `draw_level_filters`, `draw_text_filter`, `draw_source_filter`, `draw_find_controls`, `draw_time_controls`, `draw_rebuild_progress`, `draw_view_counters`. Each builder owns its `SameLine()` placement, so removing or reordering a call cannot silently change layout.

*Alternative:* group by "left/right half". Rejected: it would still yield 20+ decisions per half and split related state (`time_` controls) across functions.

### D4 — Scripted UI tests become step tables

Define `struct UiStep { void (*send)(UiState&, ImGuiIO*); bool (*expect)(const LogViewModel&, const UiState&); }` (file-local) and drive both `run_ui_view_test` and `drive_view_test` with a loop over a static array. `state.view_test_phase` remains the cursor so the existing debug output and timing stay identical.

### D5 — `LogView::copy_state_from` is three copies plus replay

`copy_filter_criteria(other, options)`, `copy_time_settings(other, options, adjustments)` and `replay_selection_and_bookmarks(other, options, counters)`; the main body calls them in the existing order, keeping the early `begin_rebuild()/rebuild_now()` boundary exactly where it is.

### D6 — Test suites share a scenario builder

`struct ViewScenario { std::unique_ptr<LogViewModel> model; ViewId engine, viz; };` built by `make_source_scenario()` (create model, ingest the same 20 frames, create two source views, drain rebuilds). `suite_views` and `suite_paused_views` keep their own assertions and check counts; only setup moves.

### D7 — Parsers get a dispatcher and per-shape helpers

- `importer.cpp`: `import_json_line(...)` and `import_text_line(...)` behind `import_line`, with `parse_iso8601_ns` split into `match_iso8601_shape`, `extract_iso8601_fields` and `civil_to_epoch_ns`.
- `logview_config.cpp`: `from_json` delegates to `read_paths`, `read_capture`, `read_limits` (each ≤ 10).
- `json_escape.cpp`: `escape_character(...)` per class plus the driver that appends it; the destination buffer contract is unchanged.

### D8 — `run_session` splits setup from the loop

`LogdSession` (node, subscriber, sink, stream state) built by `open_session(cfg)`; `run_session` becomes `open_session` + `run_dispatch_loop(session)` + `close_session`. Signal handling stays where it is.

### D9 — `bench::parse_options` uses an option table

`struct FlagOption { std::string_view name; bool Options::*field; }` for booleans and `struct ValueOption { std::string_view name; bool (*apply)(Options&, std::string_view); }` for values, with one `parse_workload_option`, `parse_mode_option`, `parse_size_option` per group. Unknown-flag diagnostics and `--help` output stay byte-identical.

### D10 — Behavior-preservation protocol

Every decomposition is verified in this order:

1. `python3 cmake/check_complexity.py` reports 0 violations (>15) and no touched function above 15.
2. The existing test binaries pass **unchanged**: `nexigen_logview_tests`, `nexigen_logview_transport_tests`, `nexigen_logd_smoke`, and the recorder benchmark build. Test names and `NX_CHECK` counts are compared before/after.
3. The UI test modes print the same lines: `UI_VIEWS_CHECKS`, `UI_FOCUS`, `UI_SPLASH`, `UI_SPLASH_WORKSPACE`, `UI_ACTIONS`.
4. CLI contracts: `--help`, `--config`, no arguments and an unknown flag produce the same stdout/stderr/exit code for `nexigen-logd`; the benchmark keeps every flag.
5. `clang-format --dry-run --Werror` on touched files; `scripts/build.sh build` and the logview targets are green.

## Risks / Trade-offs

- **ImGui call-order sensitivity**: `SameLine()`, `PushID/PopID`, popup scopes and `SetNextItemWidth` are stateful. Splitting is safe only if every builder preserves its position; the design keeps one builder per visual group and forbids reordering. Verification includes the view/splash/focus test modes, which compare rendered-state facts.
- **Test-count drift**: moving setup into a builder can accidentally drop `NX_CHECK`s. The tasks require comparing check counts per suite, not just pass/fail.
- **Scope creep into the in-flight feature work**: `add-logview-latest-and-splash` is being developed in the same files. The refactor must rebase onto it rather than revert it; if conflicts make a split ambiguous, stop and ask instead of guessing intent.
- **Table indirection**: function-pointer tables cost one indirect call per tab/option. They run once per frame or once per CLI argument, so the cost is irrelevant; the alternative (macros) would hurt readability.
- **Gate blind spot remains**: this change does not add `benchmarks/` to the gate or change thresholds. `bench::parse_options` is fixed by choice, but a future benchmark hotspot can regress unnoticed until someone runs the analyzer.
