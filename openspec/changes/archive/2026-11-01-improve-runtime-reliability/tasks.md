# Tasks

## 1. Coverage across fork boundaries

- [x] 1.1 Add `src/runner/coverage_flush.hpp` with a no-op `coverage_flush()` and a `__gcov_dump()` implementation gated by `NEXIGEN_ENABLE_COVERAGE`. Verify: a normal Debug build compiles and the symbol is absent from `nm`.
- [x] 1.2 Define `NEXIGEN_ENABLE_COVERAGE=1` for the coverage build and replace every runner/supervisor child `_exit()` plus the application `execvp()` path with the flush-aware exit helper. Verify: the coverage build links without missing `__gcov_dump`.
- [x] 1.3 Run the coverage suite and confirm supervisor and pre-exec lines are reported as covered. Verify: `app_suite.cpp` and `runner.cpp` line coverage rises materially and the supervisor loop has non-zero hits.

## 2. Per-module coverage gates

- [x] 2.1 Add repeatable `--module-fail-under <module>=<percent>` handling to `cmake/coverage_report.py` and return non-zero with a clear message on violation. Verify: a forced high threshold fails and the real thresholds pass.
- [x] 2.2 Pass `runner=90` and `hub=90` from the `coverage` target and raise the CI overall gate to the achieved floor. Verify: `cmake --build build/coverage --target coverage` succeeds and actionlint accepts the workflow.

## 3. Hub control-plane diagnostics

- [x] 3.1 Add a `Diagnostic_sink` function-pointer type and defaulted sink/context parameters to `Http_server` and `Stream_manager`, with setters. Verify: existing hub tests compile unchanged and the default sink is silent.
- [x] 3.2 Report discovery rescan failures, stream open/stat/read failures, and `sync()` failure through the sink; make `sync()` return `core::Status`. Verify: unit tests with a captured sink see a message for a missing stream and for a failed rescan.
- [x] 3.3 Forward hub diagnostics to stderr in `tools/nexigen-hub/main.cpp`. Verify: the tool prints `nexigen-hub:` diagnostics when a watched stream disappears.
- [x] 3.4 Document the diagnostic behavior in `docs/architecture.md` (hub section) and add the scenarios to the hub tests. Verify: `hub_test` reports the new checks as passed.

## 4. HTTP edge hardening

- [x] 4.1 Replace substring header searches with a case-insensitive `header_value` helper and use it for `Last-Event-ID` and `Connection`. Verify: lower-case, upper-case, mixed-case, whitespace-padded, invalid, and similarly named headers all behave as specified.
- [x] 4.2 Extend `tests/hub/hub_test.cpp` with malformed request lines, unsupported versions, oversized request lines, non-GET methods, invalid `limit`/`since`, invalid percent escapes, and unknown SSE streams. Verify: every new scenario asserts a bounded status and no state mutation.
- [x] 4.3 Update the HTTP API documentation and `docs/project-analysis.md` P2 notes. Verify: documented status codes match the tests.

## 5. Runner state-write warnings

- [x] 5.1 Call `warn_runner` from `write_json_file` on failure with the target path and error class, keeping the returned `core::Status`. Verify: `runner_logging_test` sees a warning record in `runner.jsonl` when the target path is unwritable.
- [x] 5.2 Add suite tests that make `suite-state.json` and `suite-exit.json` unwritable and assert a warning record. Verify: the new checks pass under Debug and Release.

## 6. Reproducible and portable builds

- [x] 6.1 Add the `SHA256` hash to the `nlohmann/json` FetchContent declaration and verify the expected digest from the upstream archive.
- [x] 6.2 Add the `NEXIGEN_NATIVE_ARCH` option (default `ON`) and make `cmake/Optimizations.cmake` emit `-march=native`/`-mtune=native` only when enabled. Verify: `compile_commands.json` for a portable Release build contains neither flag and still contains `-O3`.
- [x] 6.3 Add a portable Release build to the CI matrix and document the option in `README.md` and `docs/architecture.md`. Verify: the job configures, builds, and runs `ctest` with 100% passing.

## 7. Coverage growth for runner and hub

- [x] 7.1 Add hub tests for discovery error branches, model/from_json defaults, streaming gaps, truncation, replacement, overflow, and malformed descriptors. Verify: hub module coverage is at least 90.0%.
- [x] 7.2 Add runner tests for start-option validation, instance lifecycle error paths, JSON read/write failures, suite validation/serialization branches, restart policies, and forced stop. Verify: runner module coverage is at least 90.0%.
- [x] 7.3 Refresh the coverage table and P2 completion notes in `docs/project-analysis.md`. Verify: the documented numbers match the latest report.

## 8. Verification

- [x] 8.1 Build and run 100% of CTest under Debug, Release, TSan, and ASan.
- [x] 8.2 Build and run the Clang Debug suite under `-Werror`.
- [x] 8.3 Run the coverage target with the module gates and confirm runner/hub ≥90%.
- [x] 8.4 Run `openspec validate improve-runtime-reliability --strict` and `openspec validate --specs`.

## Workflow follow-up

- Archive the change after all verification tasks pass.
- Re-run `openspec validate --specs` against the archived result.
