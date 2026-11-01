# Tasks

## 1. Fix the runner log stream path

- [x] 1.1 Change `read_suite` in `src/hub/discovery.cpp` to resolve `runner_log.path` as `<artifacts_path>/<suite_id>/runner.jsonl`; verify the hub discovers a real runner suite's `runner.jsonl` without a `stream_open_failed` diagnostic
- [x] 1.2 Update the `Fixture` in `tests/hub/hub_test.cpp` to write `runner.jsonl` under `<artifacts>/<suite_id>/` and add a `test_discovery` assertion that `suite.runner_log.path` equals `<artifacts>/<suite_id>/runner.jsonl`; verify the updated `hub_test` passes

## 2. Verification

- [x] 2.1 Build `nexigen_hub` and run the full hub test suite (`nexigen_hub_tests` + `hub_cli_test.sh`); verify 100% pass
- [x] 2.2 Build and run the build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) — running the hub tests; verify no sanitizer report
- [x] 2.3 Run an end-to-end check: start the suite demo, run `nexigen-hub --run-dir <run_dir>`, and confirm the browser/SSE receives the runner stream instead of hanging on "connecting"; verify `runner.jsonl` lines are delivered

## Workflow follow-up

- Archive the change after review requirements are satisfied.
- Verify the archived `hub/discovery` spec carries the new requirement.
