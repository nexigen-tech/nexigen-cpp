# Tasks

## 1. Metrics examples

- [x] 1.1 Split `examples/metrics/ex_histograms.cpp` (16 CCN) into steps. Verify lizard reports ≤ 15, the example exits 0 and its output matches the baseline transcript.
- [x] 1.2 Split `examples/metrics/ex_descriptive_statistics.cpp` (21 CCN) into steps. Verify lizard ≤ 15 and identical output.
- [x] 1.3 Split `examples/metrics/ex_counters_and_gauges.cpp` (31 CCN) into steps. Verify lizard ≤ 15 and identical output.
- [x] 1.4 Split `examples/metrics/ex_metric_provider_and_sinks.cpp` (38 CCN) into steps, keeping the composite-logger lifetime boundary before the JSONL read-back. Verify lizard ≤ 15, identical output and the read-back assertions still pass.

## 2. Core and suite examples

- [x] 2.1 Split `examples/core/ex_spsc_ring_buffer.cpp` (21 CCN) into steps. Verify lizard ≤ 15 and identical output.
- [x] 2.2 Split `examples/suite/ex_suite_app.cpp` (36 CCN) into steps, preserving the env-driven setup order and the role/fixture lifetimes. Verify lizard ≤ 15 and identical output.

## 3. Application CLI example

- [x] 3.1 Split `examples/app/ex_app_cli_settings.cpp` (34 CCN, 235 NLOC `main`) into steps, keeping the `--help` early exit, the settings-file preparation and the failure demo in their current order. Verify lizard ≤ 15, identical output for the normal run, and that `ex_app_cli_settings_help` still exits 0.

## 4. Verification

- [x] 4.1 Run `lizard -l cpp -C 15 -w examples/` and verify no example function is reported.
- [x] 4.2 Run `ctest -L examples -j1 -V` and diff against the captured baseline; verify 21/21 pass and every differing line is a timing token, not a message or an ordering change.
- [x] 4.3 Build and run `ctest` in Debug (`build-com`), Release, ThreadSanitizer and AddressSanitizer; verify zero failures in all four.
- [x] 4.4 Run `check_naming.py`, `check_result_value.py`, `check_complexity.py` and `format-check`; verify all green.
- [x] 4.5 Run `openspec validate refactor-examples-step-functions --strict`.

## Workflow follow-up

- Commit the refactor, archive with `/opsx-archive`, and push.
