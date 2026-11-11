# Design

## Context

See `proposal.md` — Why. Two facts shape the fix:

- `com::WaitSet::create()` (`include/nexigen/com/waitset.hpp:31`) creates the iceoryx2 waitset with its default signal-handling mode; `create_without_signal_handling()` (line 43) passes `SignalHandlingMode::Disabled` so the application owns the handlers.
- `nexigen-metricsd` already uses the disabled mode and shuts down cleanly (exit 0, `nexigen-metricsd: stopped`), including its own `stop_http` and `history.flush()` steps.

`nexigen-logd` already has everything the graceful path needs: `std::atomic<bool> g_stop`, a minimal handler, a batch interval attachment that wakes the wait every `batch_flush_interval_ms` (500 ms default), `flush_and_close(Sink&)` and the `stopped` line.

## Goals / Non-Goals

**Goals:**

- `SIGTERM`/`SIGINT` drive the existing `g_stop` path: final flush, `fsync`, `close`, `stopped`, exit 0.
- Pin the behavior with a CTest lifecycle test so a future regression fails CI, not production.
- Keep exit latency bounded by the batch interval.

**Non-Goals:**

- No change to the wait/drain structure, the buffer policy, the hand-written JSONL format or rotation.
- No new CLI flags or configuration keys.
- No change to `nexigen-metricsd`, `nexigen-sm` or `nexigen-phmd`.

## Decisions

### D1 — `create_without_signal_handling()` plus the daemon's own handlers

This is the smallest change that restores the documented behavior and matches the already-verified metricsd pattern. Alternative considered: keep iceoryx2-managed signal handling and query its shutdown state instead of `g_stop` — rejected because it makes the daemon depend on a transport-specific signal contract and diverges from the other daemons.

### D2 — Keep the interval attachment as the exit-latency bound

The batch-flush timer already wakes `wait_and_process` every `batch_flush_interval_ms`, so the loop re-checks `g_stop` and exits without a dedicated wake-up mechanism. Removing or shortening the timer is unnecessary; a hostile `batch_flush_interval_ms = 0` is already clamped to 500 ms.

### D3 — Regression test as a shell lifecycle test, matching metricsd's smoke test

`nexigen_metricsd_smoke` is the existing precedent: start the binary, wait for startup, terminate, assert the process ended and the artifacts exist. The logd variant asserts the exit code is **0** and that both the `listening on` and `stopped` lines were printed, which is exactly the behavior that is broken today. A unit test cannot cover signal disposition; the shell test can, in 2 seconds.

## Risks / Trade-offs

- **A wrong signal-handling mode regressing again** → the new CTest entry fails on exit code 143 and on the missing `stopped` line.
- **Daemon exits before the first frame arrives** → the test only needs startup and shutdown, not traffic; the log directory is created by the daemon's own `open_log_file` at startup.
- **Flaky timing in CI** → the test polls for startup and for exit with bounded waits (1 s and 5 s), and treats a still-running process after the poll as a failure with a clear message.
- **Stream buffering hides the `listening on` line** → both lines are printed to stdout, which the test redirects to a file; a clean `exit()` flushes it. The test asserts on the `stopped` line, which can only appear after the flush path ran.
