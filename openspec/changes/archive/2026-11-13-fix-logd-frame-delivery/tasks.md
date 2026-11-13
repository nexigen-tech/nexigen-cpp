# Tasks

## 1. Reproduce

- [x] 1.1 Add an end-to-end test in `tests/logging/` that runs the real `nexigen-logd` binary against an `Iceoryx2Logger` (or a minimal `LogFrame` publisher) on a unique service name and asserts a published record is written to the daemon's log directory. Verify: the test fails on the current tree (empty log file).
- [x] 1.2 Confirm the reproduction is independent of stale iceoryx2 shared memory: the test uses a per-PID unique service name and `/dev/shm/iox2_*` is cleaned before the run. Verify: the failure reproduces with no stale service state.

## 2. Isolate the root cause

- [x] 2.1 Bisect the trigger between the logger's worker-thread publisher creation and the daemon's `WaitSet`/`EventListener` drain: run the daemon against a main-thread `Publisher<LogFrame>` (mimicking the metrics sink's construction) and record whether delivery succeeds. Verify: the result distinguishes a worker-thread-creation defect from a daemon-drain defect. *(Result: delivery fails with both the worker-thread and the main-thread publisher, and with an explicit subscriber buffer, so the publisher thread and buffer depth are ruled out.)*
- [x] 2.2 The `RefCell already mutably borrowed` / `corrupted service` observations were ruled out as stale-shared-memory noise from `SIGKILL`-based debugging (not reproducible on a fresh domain). The task's condition (delivery fails *only* with the worker-thread publisher) does not hold, so the panic backtrace was not needed. *(Deliberately skipped.)*

## 3. Fix

- [x] 3.1 Fix the daemon's write path: `append_frame` flushed to disk only at half the buffer (32 KiB) or on shutdown, so the `batch_flush_interval_ms` timer never flushed a low-rate producer's pending batch. Add a `last_flush` timestamp to `Sink` and flush the pending bytes at the end of `drain_subscriber` when `now - last_flush >= batch_flush_interval_ms` (default 500 ms), mirroring `metricsd`. Verify: the task 1.1 e2e test passes.
- [x] 3.2 Re-run the existing `logging` test suites (`iceoryx2_logger_test`, `log_daemon_test`, plus `add-suite-2-apps`'s daemon-ingestion check) to confirm no regression. Verify: all suites green.

## 4. Verify

- [x] 4.1 Run the full build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer, AddressSanitizer — with the e2e test in each. Verify: 100% pass, no sanitizer report.

## Workflow follow-up

- After the fix lands, finish `add-suite-2-apps` tasks 5.2/5.3/6.1 (log daemon ingestion, shared `trace_id` in logs, full matrix).
- Archive both changes after review.
