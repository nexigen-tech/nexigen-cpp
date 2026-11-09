# Tasks: Extract ChildProcess Class

## 1. ChildProcess class (thin wrapper over Runner)

- [x] 1.1 Create `include/nexigen/runner/child_process.hpp` with the `ChildProcess` class declaration
- [x] 1.2 Create `src/runner/child_process.cpp` with the implementation
- [x] 1.3 Add `ChildProcess` to the `app_suite.hpp` umbrella header
- [x] 1.4 Implement `launch()` — delegate to `Runner::start`, then read `resource.json` for PIDs
- [x] 1.5 Implement `stop(grace_s)` — delegate to `Runner::stop`
- [x] 1.6 Implement `is_running()` query
- [x] 1.7 Implement `to_string()` member and ADL `to_string(const ChildProcess&)`
- [x] 1.8 Add `std::formatter<ChildProcess>` specialization
- [x] 1.9 Add `to_json`/`from_json` (nlohmann::json, non-throwing)

## 2. Suite supervisor integration

- [x] 2.1 Replace the local `RuntimeApp` struct with `using RuntimeApp = ChildProcess;`
- [x] 2.2 Route `launch_app` through `ChildProcess::launch`
- [x] 2.3 Route `stop_all` through `ChildProcess::stop`
- [x] 2.4 Build `ChildProcess` in `initialize_runtime_apps` via its constructor
- [x] 2.5 Confirm `AppStateMachine` works unchanged against `ChildProcess` (type alias)

## 3. Tests

- [x] 3.1 Add `tests/runner/child_process_test.cpp` covering `launch`, `stop`,
      `is_running`, `to_string`, `std::format`, and JSON round-trip
- [x] 3.2 Build and run `nexigen_runner_tests` and `nexigen_app_suite_tests` under Debug
- [x] 3.3 Build and run tests with ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`)
- [x] 3.4 Build and run tests with AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`)
- [x] 3.5 Build with `-O3 -march=native -flto` and confirm latency gates pass

## 4. Verification

- [x] 4.1 Run `openspec validate --changes extract-child-process-class`
- [x] 4.2 Confirm 100% of existing tests pass (no regression)
- [x] 4.3 Build and run the `examples/` targets to confirm no breakage
