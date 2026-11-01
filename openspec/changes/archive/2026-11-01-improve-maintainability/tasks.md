# Tasks

## 1. HTTP request handling

- [x] 1.1 Split `Http_server::handle_request` into request-line validation plus `handle_health`, `handle_tree`, `handle_streams`, `handle_recent`, `handle_events`, and `handle_assets`, preserving status codes and keep-alive behavior. Verify: `nexigen_hub_tests` reports 100% passed with the same check count or more.
- [x] 1.2 Add `test_http_fuzz_corpus` with a fixed-seed malformed-request generator, bounded payload sizes, socket timeouts, and a final health probe. Verify: the suite passes repeatedly and the whole hub test binary stays under ~10 seconds.
- [x] 1.3 Re-run the hub coverage report and confirm `hub >= 90%`. Verify: `cmake --build build/coverage --target coverage` passes.

## 2. Runner start phases

- [x] 2.1 Extract validation/path resolution, instance preparation, and supervisor-child execution from `Runner::start` into internal helpers with explicit context structs, keeping the same fork/exit order and gcov flush. Verify: all runner and app-suite tests pass in Debug and Release.
- [x] 2.2 Add or adjust tests for the extracted phases, including a successful options launch and an early validation failure. Verify: runner coverage stays at or above 90%.

## 3. Suite supervisor phases

- [x] 3.1 Extract stop-request handling, pending suite restart, and per-app exit/retry handling from `run_suite_supervisor` into helpers while preserving the 100 ms poll and restart policy. Verify: restart, not-impact, failure-impact, clean-exit, and forced-stop suites pass.
- [x] 3.2 Confirm suite state and exit files are unchanged after the refactor. Verify: existing app-suite assertions on `suite-state.json`/`suite-exit.json` pass, and coverage stays at or above 90%.

## 4. Convention guard

- [x] 4.1 Add `cmake/check_result_value.py` with comment/string stripping, receiver extraction, the documented allowlist, and `path:line` diagnostics. Verify: the script passes the current tree and fails on a temporary `Result` `.value()` sample.
- [x] 4.2 Run the guard in `.github/workflows/ci.yml`. Verify: the YAML parses and the local command exits 0.

## 5. Static-analysis nits and docs

- [x] 5.1 Cast the constant products in `streaming.hpp`/`http_server.cpp` to `std::size_t` before multiplication and reserve `standalone_instances` in `discovery.cpp`. Verify: the build stays clean under `-Wconversion`/`-Wsign-conversion` and the hub tests pass.
- [x] 5.2 Add P3 completion notes to `docs/project-analysis.md` and update `README.md`/`docs/architecture.md` verification text. Verify: documented commands and numbers match the final report.

## 6. Verification

- [x] 6.1 Build and run 100% of CTest under Debug, Release, portable Release, TSan, and ASan.
- [x] 6.2 Build and run the Clang Debug suite under `-Werror`.
- [x] 6.3 Run the coverage target and confirm overall, runner, and hub gates.
- [x] 6.4 Run `openspec validate improve-maintainability --strict` and `openspec validate --specs`.

## Workflow follow-up

- Archive the change after all verification tasks pass.
- Re-run `openspec validate --specs` against the archived result.
