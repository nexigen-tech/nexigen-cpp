# Tasks

## 1. Log viewer UI (`tools/nexigen-logview/ui/logview_window.cpp`)

- [x] 1.1 Extract `initialize_ui_state(model, options)` from `run_ui`: the current state assignments, ini path creation and per-view `ui_for` priming
- [x] 1.2 Extract `start_capture(options, state, capture)` and `load_splash(options)` (including the splash test-failure injection) from `run_ui`
- [x] 1.3 Extract `run_splash_phase(...)` covering the splash-test branch and the short-splash branch, preserving the fall-through into the workspace session
- [x] 1.4 Extract `run_actions_test(...)` for the `options.actions_test` short-circuit
- [x] 1.5 Split the frame loop into `update_focus_script_input`, `draw_workspace_frame` (toolbar, sources, workspace, per-view loop), `collect_closed_views`, `update_focus_test_state`, `draw_and_present_frame` and `frame_test_done`
- [x] 1.6 Extract `report_ui_result(...)` with the existing `UI_VIEWS_CHECKS`, `UI_FOCUS`, `UI_SPLASH_WORKSPACE` printing and the final `(focus_ok && view_ok)` return
- [x] 1.7 Keep one pane dispatch in the frame loop (`draw_workspace_frame`): toolbar, sources, workspace, the per-view `draw_log_view` loop with close handling, and the scripted view driver, in the original `SameLine`/`PushID` order (the viewer renders each view as its own dock window, so there is no conditional tab strip to table-drive)
- [x] 1.8 Split `draw_local_controls` into `draw_view_state_badge`, `draw_view_actions`, `draw_level_filters`, `draw_text_filter`, `draw_source_filter`, `draw_find_controls`, `draw_time_controls`, `draw_rebuild_progress` and `draw_view_counters`
- [x] 1.9 Split `draw_global_toolbar` (23), `draw_log_view` (16) and `draw_clone_modal` (16) into per-group/per-section helpers
- [x] 1.10 Split `main` in `tools/nexigen-logview/main.cpp` (21) and `run_headless` in `headless.cpp` (17) into option/phase helpers
- [x] 1.11 Replace the scripted phases of `run_ui_view_test` (38) and `drive_view_test` (20) with a static `UiStep` table of send/expect callbacks driven by one loop; keep `state.view_test_phase` as the cursor

## 2. Log viewer model

- [x] 2.1 Split `LogView::copy_state_from` into `copy_filter_criteria`, `copy_time_settings` (incl. T0 containment) and `replay_selection_and_bookmarks`, preserving the `begin_rebuild`/`rebuild_now` boundaries
- [x] 2.2 Split `importer.cpp`: `import_json_line`/`import_text_line` behind `import_line`; `parse_iso8601_ns` into `match_iso8601_shape`, `extract_iso8601_fields` and `civil_to_epoch_ns`
- [x] 2.3 Split `from_json` in `model/logview_config.cpp` into per-section readers (`read_paths`, `read_capture`, `read_limits`)
- [x] 2.4 Split `json_escape` into a per-character-class helper plus the appending driver

## 3. Log viewer tests

- [x] 3.1 Add a `make_source_scenario()` builder (model, 20 seeded frames, two source views, drained rebuilds) and reduce `suite_views` (30) and `suite_paused_views` (35) to scenario + assertions without dropping `NX_CHECK`s
- [x] 3.2 Split `main` in `tests/logview_transport_test.cpp` (18) into named transport scenarios registered under the existing test names

## 4. Log daemon

- [x] 4.1 Split `run_session` in `tools/nexigen-logd/main.cpp` (16) into `open_session` (node/subscriber/sink), `run_dispatch_loop` and `close_session`, leaving signal handling in place

## 5. Recorder benchmark

- [x] 5.1 Replace the flat `if`-chain in `bench::parse_options` (30) with a flag table plus per-group `apply` functions for workload, mode and size options, keeping `--help`, flag names and error paths identical

## 6. Verification

- [x] 6.1 Run `python3 cmake/check_complexity.py` and confirm 0 violations and no touched function above 15; run `complexity-check` for the same verdict
- [x] 6.2 Run the existing test binaries unchanged and confirm the same test names and `NX_CHECK` counts per suite: `nexigen_logview_tests`, `nexigen_logview_transport_tests`
- [x] 6.3 Run `nexigen-logview` in smoke, view, focus, splash and actions test modes and diff the printed lines (`UI_VIEWS_CHECKS`, `UI_FOCUS`, `UI_SPLASH`, `UI_SPLASH_WORKSPACE`, `UI_ACTIONS`) against the pre-change output
- [x] 6.4 Diff `nexigen-logd` and the recorder benchmark CLI behavior for `--help`, `--config <path>`, no arguments and an unknown flag (stdout, stderr, exit code)
- [x] 6.5 Run `clang-format --dry-run --Werror` on every touched file and `scripts/build.sh build` (logview targets included)
- [x] 6.6 Build and run 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`); confirm the latency gates report the same numbers as the pre-change baseline
- [x] 6.7 Confirm no header under `include/` changed; document any new tool-side declaration (`tools/nexigen-logview/model/*.hpp`) with `/// @brief`/`@param`/`@return`; the `include/` Doxygen mandate is not triggered because no public header is touched
- [x] 6.8 Verify the `docs/architecture.md` logview Mermaid diagram and the `nexigen-logview` table row still describe the code (UI → LogView → FilterIndex/FindState/Minimap); update them only if the decomposition changes the described structure

## Verification

- `python3 cmake/check_complexity.py`: 0 functions above CCN 15 (previous run: 18); `complexity-check` reports the same verdict.
- Log-viewer tests: `nexigen_logview_tests` 60,636 checks / 0 failed (unchanged), `nexigen_logview_transport_tests` 13 checks / 0 failed; all 15 `logview` CTest entries pass (model, transport, headless, smoke, offline smoke, actions, scale, input, views, focus, splash).
- Log-daemon end-to-end: `nexigen_logging_com_daemon_tests` passes against the rebuilt `nexigen-logd`.
- Recorder benchmark: `nexigen_storage_bench_tests` 257 checks / 0 failed; `--help` exits 0 and a real workload run completes.
- Matrix: Debug 56/56, Release (LTO) 124/124, AddressSanitizer 83/83, ThreadSanitizer 83/84 — the single failure was `nexigen_synchro_tests` (a `pump_until` epoch timeout under TSan load) and it passes on rerun; `synchro` is untouched by this change.
- `clang-format --dry-run --Werror` is clean on every touched file (the pre-existing violations in `benchmarks/recorder/bench_types.cpp` outside the rewritten region are unchanged).
- No header under `include/` changed; new tool-side declarations carry `/// @brief`/`@param`/`@return`.
- `docs/architecture.md` logview Mermaid diagram and table row still describe the component structure unchanged (the refactor is function-level only).
