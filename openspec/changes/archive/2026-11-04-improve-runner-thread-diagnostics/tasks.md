# Tasks

## 1. Surface the apply report

- [x] 1.1 Add a report-out overload `apply_runner_thread_spec(spec, ThreadApplyReport&, context)` in `include/nexigen/runner/runner_logging.hpp` and `src/runner/runner_logging.cpp`, keeping the existing two-argument form as a wrapper that discards the report; remove the early `spec.validate()` return so `apply_thread_spec_self` records validation failures into `report.first_error_`.
- [x] 1.2 Add a cold-path per-attribute renderer `describe_thread_apply_report(const core::ThreadApplyReport&)` that maps the name/affinity/scheduler bits to labels and includes `first_error`.

## 2. Report-based diagnostics at call sites

- [x] 2.1 Update `run_suite_supervisor` (`src/runner/app_suite.cpp`) to print the rendered report via `warn_stderr` instead of the fixed `runner_control_cpus is invalid; ... unpinned` message.
- [x] 2.2 Update `apply_control_affinity` (`tools/nexigen-run/main.cpp`) to pass the report and emit a warning naming the failed attribute(s).

## 3. Tests and validation

- [x] 3.1 Extend `tests/runner/runner_logging_test.cpp` to verify the report overload populates requested/applied/failed correctly and that the diagnostic string names the failed attribute (using `NEXIGEN_ENABLE_FAULT_INJECTION`).
- [x] 3.2 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`); run `openspec validate` on the change.
