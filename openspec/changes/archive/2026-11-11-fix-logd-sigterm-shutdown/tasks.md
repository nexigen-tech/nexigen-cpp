# Tasks

## 1. Fix the shutdown path

- [x] 1.1 Replace `com::WaitSet::create()` with `com::WaitSet::create_without_signal_handling()` in `tools/nexigen-logd/main.cpp` and keep the existing `SIGTERM`/`SIGINT` handler installation and interval attachment. Verify the file builds and `check_complexity.py` stays green.
- [x] 1.2 Run the daemon manually against a temporary config, send `SIGTERM`, and verify exit code 0, the `listening on` and `stopped` lines, and a created log directory instead of exit code 143.

## 2. Regression test

- [x] 2.1 Add `tools/nexigen-logd/smoke_test.sh` (start → poll startup → `SIGTERM` → require exit 0, `stopped` line, log directory) and register it in `tools/CMakeLists.txt` as `nexigen_logd_smoke` with label `tools` and a 60 s timeout. Verify `ctest -R nexigen_logd_smoke` passes and that it fails when the daemon is started with `WaitSet::create()` restored temporarily.

## 3. Verification

- [x] 3.1 Build and run `ctest` in Debug (`build-com`) and confirm 60/60 plus the new test pass; verify the other three daemon smoke/hostile tests are unaffected.
- [x] 3.2 Build and run `ctest` in Release, ThreadSanitizer and AddressSanitizer configurations; verify zero failures.
- [x] 3.3 Run `check_naming.py`, `check_result_value.py`, `check_complexity.py`, `format-check` and `ctest -R complexity`; verify all green.
- [x] 3.4 Run `openspec validate fix-logd-sigterm-shutdown --strict` and verify it reports valid with `skip_specs`.

## Workflow follow-up

- Commit the fix, then archive with `/opsx-archive` and push.
