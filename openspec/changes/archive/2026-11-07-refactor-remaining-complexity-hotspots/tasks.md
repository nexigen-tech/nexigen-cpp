# Tasks

## 1. Baseline and guards

- [x] 1.1 Record the pre-refactor lizard CCN and location for each targeted function (`scan_suite`, `open_segment`, `cmd_start`, `cmd_stop`, `cmd_status`, hub `main`, `test_provider`, `test_exponential_histogram_mt`) so the before/after comparison is verifiable
- [x] 1.2 Confirm the working tree is clean of unrelated changes and note the test commands used for Debug, Release, ASan, and TSan verification

## 2. hub/discovery `scan_suite`

- [x] 2.1 Extract `load_suite_document` and `apply_suite_streams` in `src/hub/discovery.cpp`; verify `openspec`-unrelated hub discovery tests still pass and the containment logic is unchanged
- [x] 2.2 Extract `apply_suite_outcome`, `upsert_app`, and `build_suite_instance`; verify `suite-state.json`/`suite-exit.json` precedence and `scan_attempts` output are unchanged
- [x] 2.3 Extract `derive_app_states` and `derive_unknown_suite_state`; verify suite/app state aggregation is unchanged and lizard reports `scan_suite` ≤ CCN 15

## 3. logging/rotation `open_segment`

- [x] 3.1 Extract `validate_segment_request`, `ensure_directory`, and `build_directory`; verify existing rotation tests pass and the fixed scratch buffers are still used
- [x] 3.2 Extract `build_file_path` and `open_append`; verify `open_period`/`open_segment` keep their `core::Status` and `errno` semantics and `open_segment` is ≤ CCN 15 by lizard

## 4. nexigen-run CLI

- [x] 4.1 Add `Start_args`/`Stop_args`/`Status_args` and their parsers; verify every option, `--` command mode, `--help`, and unknown-option path returns the same exit code as before
- [x] 4.2 Add `resolve_suite_target` and rewrite `cmd_stop`/`cmd_status` to use it; verify `--config`/`--suite-dir` mutual-exclusivity and suite-vs-instance messages are byte-identical
- [x] 4.3 Rewrite `cmd_start` to parse → resolve → dispatch → report; verify suite and single-instance start output, log lines, and exit codes are unchanged and all three `cmd_*` are ≤ CCN 15 by lizard

## 5. nexigen-hub CLI

- [x] 5.1 Add `Hub_options` and `parse_hub_args`; verify `--help`, `--run-dir` requirement, numeric parsing, and unknown-option exit codes are unchanged
- [x] 5.2 Add `classify_listen_host` and `warn_if_non_loopback`; verify bracket IPv6, bare `::1`, `localhost`, and non-loopback warning text are unchanged
- [x] 5.3 Add `run_hub` and reduce `main` to parse → run; verify discovery/listen failure paths and the listening banner are unchanged and hub `main` is ≤ CCN 15 by lizard

## 6. Test decomposition

- [x] 6.1 Split `test_provider` into the focused `static` sub-functions listed in design.md and call them in the original order; verify the tracing suite passes with the same assertions and prints
- [x] 6.2 Split `test_exponential_histogram_mt` into the focused `static` sub-functions listed in design.md and call them in the original order; verify the metrics suite passes with the same assertions
- [x] 6.3 Verify both split test functions and every new sub-function are ≤ CCN 15 by lizard

## 7. Verification

- [x] 7.1 Run `clang-format` check on all touched C++ files and verify it is clean
- [x] 7.2 Run lizard over the touched files and verify every function introduced or modified is ≤ CCN 15 and no average complexity regressed
- [x] 7.3 Compile and pass 100% of unit tests in Debug and verify the full suite is green
- [x] 7.4 Compile and pass 100% of unit tests in portable Release (`-O3 -march=native -flto` with latency gates) and verify the full suite is green
- [x] 7.5 Compile and pass 100% of unit tests under AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) and verify the full suite is green
- [x] 7.6 Compile and pass 100% of unit tests under ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and verify the full suite is green
- [x] 7.7 Confirm coverage for the touched modules is unchanged or better than the project gates

## Workflow follow-up

- Run OpenSpec verification for this change.
- Archive the change after verification passes.
