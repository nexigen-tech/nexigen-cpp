# Design

## Context

`apply_runner_thread_spec` (`src/runner/runner_logging.cpp`) already computes a `core::ThreadApplyReport` by delegating to `core::apply_thread_spec_self`, but returns only a `core::Status` and discards the report. Its `context` parameter is unused. The callers (`run_suite_supervisor` in `src/runner/app_suite.cpp`, `apply_control_affinity` in `tools/nexigen-run/main.cpp`) therefore print fixed messages that cannot reflect the per-attribute outcome.

`core::ThreadApplyReport` (`include/nexigen/core/thread_management.hpp`) is a 64-byte `alignas(64)` value with private `requested_`/`applied_`/`failed_`/`rollback_failed_` bitmasks (name=1, affinity=2, scheduler=4) and `first_error_`. It is filled only by `apply_thread_spec_self`; its `to_string()` renders numeric masks.

## Goals / Non-Goals

**Goals:**
- Surface the report to runner callers so warnings name the real attribute and reflect partial success.
- Keep the existing `apply_runner_thread_spec(spec, context)` signature as a non-breaking wrapper.

**Non-Goals:**
- Do not change `core::ThreadApplyReport` itself (it belongs to `core/thread-management` and is already serializable). Do not alter apply/rollback semantics or the `online_cpu_set` pre-check.

## Decisions

1. **Add a report-out overload** in `nexigen::runner`:
   ```cpp
   [[nodiscard]] core::Status apply_runner_thread_spec(
       const core::ThreadSpec& spec,
       core::ThreadApplyReport& report,
       std::string_view context = {}) noexcept;
   ```
   The existing two-argument form delegates to it and discards the report. The report-out form removes the early `spec.validate()` return so `apply_thread_spec_self` records validation failures into `report.first_error_`; it keeps the `online_cpu_set` pre-check unchanged (that is a structural, pre-OS check, distinct from the OS-level application the report describes).

2. **Add a cold-path per-attribute renderer** `describe_thread_apply_report(const core::ThreadApplyReport&)` (free function in `nexigen::runner`, implementation in `runner_logging.cpp`) that maps the name/affinity/scheduler bits to human labels and includes the first error. It returns a `std::string` built with `std::format`; this is control-plane only.

3. **Update call sites** to print the report-derived message instead of the fixed strings:
   - `run_suite_supervisor` (`src/runner/app_suite.cpp`): replace the `fprintf` with `warn_stderr` carrying the rendered report (the supervisor forks before logger init, so `warn_runner` is not usable there).
   - `apply_control_affinity` (`tools/nexigen-run/main.cpp`): pass the report and emit a warning naming the failed attributes.

4. **No concurrent/atomic component is introduced.** The only shared value is the existing `ThreadApplyReport`, which is a 64-byte `alignas(64)` stack value written by `apply_thread_spec_self` on the calling thread and read immediately by the same thread; no `std::memory_order` is required beyond the already-documented startup publication inside each worker.

## Risks / Trade-offs

- The new diagnostic string allocates on the control path. It runs once per control-thread/logger-thread startup and never on a hot path, so the zero-allocation hot-path mandate is preserved.
- The report covers the OS-level application stage; the `online_cpu_set` structural rejection is reported separately and still uses its own message. Both messages must not contradict each other when both fire.
