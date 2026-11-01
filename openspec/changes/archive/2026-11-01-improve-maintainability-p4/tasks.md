# Tasks

## 1. Coverage above 95%

- [x] 1.1 Add streaming tests for path replacement, directory-read failures, stat diagnostics, and replay/live gaps. Verify: `nexigen_hub_tests` passes and the new diagnostic paths are exercised.
- [x] 1.2 Add HTTP tests for percent-decoded stream IDs, SSE replay/live gaps, and a failed initial sync. Verify: the hub suite passes and the coverage report includes the new branches.
- [x] 1.3 Add discovery tests for attempt-name edges, standalone running/stopped/unknown states, and combined app→suite state. Verify: `discovery` and `discovery_states` suites pass.
- [x] 1.4 Add runner/suite tests for settings and stream preparation failures, live process-group conflicts, hostile metadata/status JSON, invalid logging settings, metadata write failure, and a live supervisor stop. Verify: runner and app-suite suites pass.
- [x] 1.5 Run the coverage target and confirm overall ≥95%, runner/hub ≥90%. Verify: `cmake --build build/coverage --target coverage` passes.

## 2. Coverage gate

- [x] 2.1 Raise `NEXIGEN_COVERAGE_FAIL_UNDER` to 95 in the CI coverage job. Verify: the configured gate matches the measured report and the target passes.
- [x] 2.2 Update the coverage documentation with the new totals and gate. Verify: the documented command and percentages match the latest report.

## 3. clang-tidy

- [x] 3.1 Add `.clang-tidy` with the focused check set, warnings-as-errors, header filter, and documented exclusions. Verify: `clang-tidy -p build/clang` reports no findings on `src/hub/http_server.cpp`.
- [x] 3.2 Fix the reported findings: enum sizes, cast ordering, pointer conversion, vector reserve, optional access, and drop-summary casts. Verify: the full `src/*.cpp` clang-tidy run exits 0.
- [x] 3.3 Add clang-tidy installation and the `src/*.cpp` run to the Clang CI job. Verify: the YAML parses and the local equivalent command exits 0.
- [x] 3.4 Document the profile and its exclusions in `docs/architecture.md`. Verify: the documented check set matches `.clang-tidy`.

## 4. Documentation

- [x] 4.1 Add P4 completion notes to `docs/project-analysis.md` and refresh the coverage/verification text in `README.md` and `docs/architecture.md`. Verify: all documented numbers match the final report.

## 5. Verification

- [x] 5.1 Build and run 100% of CTest under Debug, Release, portable Release, TSan, and ASan.
- [x] 5.2 Build and run the Clang Debug suite under `-Werror`.
- [x] 5.3 Run the clang-tidy command from CI and confirm exit 0.
- [x] 5.4 Run `openspec validate improve-maintainability-p4 --strict` and `openspec validate --specs`.

## Workflow follow-up

- Archive the change after all verification tasks pass.
- Re-run `openspec validate --specs` against the archived result.
