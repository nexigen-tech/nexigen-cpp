# Tasks

## 1. Baseline

- [x] 1.1 Record pre-refactor lizard CCN/locations for the 14 functions and the per-suite `NX_CHECK` counts printed by a Debug run
- [x] 1.2 Confirm the working tree has no unrelated changes

## 2. tests/tracing/tracing_test.cpp

- [x] 2.1 Split `test_sinks` (38) into console, JSONL basic/overflow/escape/rotation/rate-limit/open-failure, config diagnostics, Chrome and composite helpers with shared UTC-window helpers; verify all sink assertions and order are unchanged and ≤ CCN 15
- [x] 2.2 Split `test_scoped_span` (19) into the 16 numbered scenario helpers plus the concurrency-flip helper; verify end-order, slot and drop-counter assertions are unchanged and ≤ CCN 15
- [x] 2.3 Split `test_latency` (tracing, 22) into setup/calibration/measured-pass/report helpers including the non-x86 path; verify the measurement loops, prints and gates are byte-identical and ≤ CCN 15
- [x] 2.4 Split `test_ids_and_context` (16) into sentinel/uniqueness/alloc-free/concurrent/JSON/handoff/diagnostics helpers; verify order and ≤ CCN 15

## 3. tests/app/app_test.cpp

- [x] 3.1 Split `test_command_line_parser` (37) into option-form, level, argv[0], help, terminator, error, directory-validation and usage helpers; verify every diagnostic assertion is unchanged and ≤ CCN 15

## 4. tests/hub/hub_test.cpp

- [x] 4.1 Split `test_http_server` (25) into socket helpers plus route/malformed/method/query/keep-alive/SSE/tree/all-stream/cursor/header/failure/shutdown helpers; verify request order, SSE boundaries and ≤ CCN 15

## 5. tests/metrics/metrics_test.cpp

- [x] 5.1 Split `test_explicit_histogram_mt` (24) and `test_exponential_histogram` (23); verify bucket math, thread-slot and ordering assertions unchanged and ≤ CCN 15
- [x] 5.2 Split `test_latency` (metrics, 22) and `test_serialization` (19); verify measurement loops, prints and serialization assertions unchanged and ≤ CCN 15
- [x] 5.3 Split `test_sinks_json` (19) and `test_provider_harvester` (17); verify output text, harvest-order assertions unchanged and ≤ CCN 15

## 6. tests/chrono/chrono_test.cpp and tests/core/core_test.cpp

- [x] 6.1 Split `test_provider` (chrono, 17) at its scenario blocks; verify order and ≤ CCN 15
- [x] 6.2 Split `test_sink_io` (core, 17) at its scenario blocks; verify order and ≤ CCN 15

## 7. Verification

- [x] 7.1 Run `clang-format` on all touched files and verify `format-check` is clean
- [x] 7.2 Run lizard over the touched files and verify every touched function is ≤ CCN 15 and the per-suite `NX_CHECK` counts match the baseline
- [x] 7.3 Compile and pass 100% of tests in Debug
- [x] 7.4 Compile and pass 100% of tests in Release
- [x] 7.5 Compile and pass 100% of tests under AddressSanitizer
- [x] 7.6 Compile and pass 100% of tests under ThreadSanitizer
- [x] 7.7 Confirm coverage module gates (`runner >= 90`, `hub >= 90`) still pass

## Workflow follow-up

- Run OpenSpec verification for this change.
- Archive the change after verification passes.
