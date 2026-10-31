# Tasks

## 1. Metrics and tracing sink settings

- [x] 1.1 Add `sink` members to `app::Metrics_settings` and `app::Tracing_settings` in `include/nexigen/app/app_settings.hpp`, including the `json_file_metric_sink.hpp` and `json_file_trace_sink.hpp` headers and keeping the existing defaults. Verify: Debug build succeeds and the existing `tests/app` suite still passes unchanged.
- [x] 1.2 Parse the `sink` object in `src/app/app_settings.cpp` for metrics (`path`, `append`) and tracing (`path`, `rotation`, `max_spans_per_window`, `window_ns`) with field-level merge and diagnostics for non-object/wrong-type/unknown-rotation input. Verify: new `tests/app` cases cover local-over-global path, absent-field inheritance, empty-path-is-unconfigured, invalid rotation keeping the previous policy, and provider environment overrides leaving the sink unchanged.
- [x] 1.3 Extend `Metrics_settings`/`Tracing_settings` `to_string` and `to_json` to include the sink configuration. Verify: tests assert the sink fields in both text and JSON, that `std::format("{}", settings)` equals `to_string()`, and that no `from_json` overload exists for either settings type.

## 2. Chrono sink settings

- [x] 2.1 Add the `sink` member (`chrono::JsonFileChronoSink_config`) to `chrono::Chrono_settings` in `include/nexigen/chrono/chrono_settings.hpp` with an empty default path. Verify: Debug build succeeds and the existing `tests/chrono` suite still passes unchanged.
- [x] 2.2 Parse the chrono `sink` object (`path`, `append`) in `src/chrono/chrono_settings.cpp` with field-level merge and diagnostics for non-object or wrong-type values, without touching the path on disk. Verify: new `tests/chrono` cases cover local-over-global path, absent-field inheritance, empty-path-is-unconfigured, wrong path type keeping the previous value, and `NEXIGEN_CHRONO_*` overrides leaving the sink unchanged.
- [x] 2.3 Extend `Chrono_settings` `to_string` and `to_json` with the sink configuration and confirm `apply_to` still maps only provider fields. Verify: tests assert sink fields in text/JSON, formatter equality, and that `apply_to` output is unaffected by a configured sink.

## 3. Run identity and telemetry layout

- [x] 3.1 Create `include/nexigen/app/telemetry_layout.hpp` and `src/app/telemetry_layout.cpp` with `Run_identity`, `load_run_identity()`, and the `<run_dir>/<app>/<instance>` derivation. Verify: new `tests/app` cases set each `NEXIGEN_*` variable, load, and assert every field plus derived `instance_dir`, explicit `NEXIGEN_INSTANCE_DIR` precedence, and empty fields when variables are unset.
- [x] 3.2 Implement `validate_run_identity` with the 1–128 byte id rule, `[A-Za-z0-9._-]` character set, `.`/`..` rejection, `..` substring rejection, and POSIX-absolute directory checks, while accepting a completely empty identity. Verify: tests cover valid identity, `/` in an app name, `..` in an instance id, relative directories, and the empty identity succeeding.
- [x] 3.3 Implement `resolve_telemetry_paths` (pure explicit-path overload plus the settings convenience overload) with per-signal precedence: explicit non-empty path, then `<instance_dir>/<metrics|traces|chrono>.jsonl`, then empty. Verify: tests cover explicit-wins, instance fallback, mixed per-signal resolution, empty identity with no explicit paths, invalid identity returning `InvalidArgument` with output untouched, and a non-existent instance directory producing path strings without creating anything.
- [x] 3.4 Add `to_string`, free `to_string`, `std::formatter`, and non-throwing ADL `to_json`/`from_json` for `Run_identity` and `Telemetry_paths`. Verify: tests round-trip both values, assert hostile JSON (non-object, wrong field types, extra keys) leaves the target unchanged under `JSON_NOEXCEPTION=1`, and check formatter output equals `to_string()`.

## 4. Example and documentation

- [x] 4.1 Update `examples/app/ex_app_cli_settings.cpp`: write `sink` blocks into the four local settings files and construct metric/tracing/chrono sinks from `settings.*.sink` instead of the hard-coded path constants. Verify: `ctest -R ex_app_cli_settings --output-on-failure` passes and the example still finds non-empty `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, and `chrono.jsonl` with the existing count and trace-shape checks.
- [x] 4.2 Add an app integration test that sets the five identity variables, loads settings without sink paths, resolves the layout, and initializes an infrastructure whose policy writes to the resolved paths. Verify: the test finds all four streams under the instance directory and sees no file outside it.
- [x] 4.3 Document the new keys and contract in `README.md`: `sink.path`/`sink.append` for metrics and chrono, the four-field tracing sink, the five `NEXIGEN_*` identity variables, and the explicit-path → instance-directory → empty precedence. Verify: the documented JSON snippets match the fixtures used by the tests.

## 5. Full verification matrix

- [x] 5.1 Configure and build Debug (`cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`), then run `ctest --test-dir build/debug --output-on-failure` and `ctest --test-dir build/debug -L examples --output-on-failure`. Verify: 100% of tests pass with no failures.
- [x] 5.2 Configure and build Release with the project production flags (`-O3 -march=native -flto`, `-Werror`) and the latency gates, then run the full unit-test suite and `ctest -L examples`. Verify: 100% pass and no latency gate regresses.
- [x] 5.3 Configure and build with `-DNEXIGEN_ENABLE_TSAN=ON`, then run the full unit-test suite and examples. Verify: 100% pass with no ThreadSanitizer report.
- [x] 5.4 Configure and build with `-DNEXIGEN_ENABLE_ASAN=ON`, then run the full unit-test suite and examples. Verify: 100% pass with no AddressSanitizer or UBSan report.
- [x] 5.5 Run `openspec validate add-telemetry-output-contract --strict`. Verify: the change validates with no errors.

## Follow-up (not part of this change)

- After this change lands, create a separate `nexigen-observatory` OpenSpec change for `nexigen-hub` (observe/ingest/UI) and `nexigen-run` (instance layout, `resource.json`, generated settings, stdio capture, `exit.json`, process-group control).
- Keep this change's four-stream filenames, `sink` keys, and `NEXIGEN_*` identity variables unchanged when implementing the hub/runner; the follow-up may add `runs/<run>/<app>/<instance>/` structure and heartbeat semantics but must not change record formats.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result and confirm the `nexigen-observatory` follow-up is scheduled before implementation starts on the runner.
