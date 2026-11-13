# Design

## Context

See `proposal.md` — Why. The `nexigen-logd` daemon subscribes to `LogFrame` on a configured iceoryx2 service and drains through a `WaitSet` (event listener + batch-flush timer). The `Iceoryx2Logger` publishes `LogFrame` from a background worker and notifies a companion `<service>Event`. The metrics daemon uses the same `WaitSet`/event pattern over `MetricFrame` and demonstrably receives; the log daemon does not.

## Goals / Non-Goals

**Goals:**
- Make `nexigen-logd` receive `LogFrame`s published by an `Iceoryx2Logger`, verified by an end-to-end test that runs the real daemon binary.
- Identify and fix the root cause rather than papering over it.

**Non-Goals:**
- No change to the `LogFrame` wire layout, the daemon config schema, or the `Iceoryx2Logger` public surface.
- No real-time-scheduling change.

## Decisions

### D1 — Add a real-daemon e2e test before fixing

The existing `tests/logging/log_daemon_test.cpp::test_e2e` and `tests/logging/iceoryx2_logger_test.cpp::test_roundtrip` drive a *manual* subscriber with busy-polling; neither runs `nexigen-logd` itself. The new test launches the daemon binary (or, minimally, reproduces its exact `WaitSet` + `EventListener` + `attach_interval` drain) against an `Iceoryx2Logger`, then asserts a published record is written. This makes the failure reproducible and the fix verifiable.

### D2 — Root cause: the daemon never flushes the pending batch on its timer

The e2e test (D1) exposed it. A temporary drain diagnostic showed the daemon **does receive** the frame (`logd-drain: got frame level=2 size=12`), yet its log file stays empty. The cause is in the write path, not the transport:

- `append_frame` appends the rendered line to a 64 KiB `Sink` buffer and writes to the file only when `used >= kBufSz/2` (32 KiB), or on shutdown.
- The `batch_flush_interval_ms` timer is attached to the `WaitSet`, but the drain callback only drains the subscriber and never flushes the pending buffer.
- So a low-rate producer leaves the log file empty until ~32 KiB accumulate — the observed symptom.

The metrics daemon does it correctly: `drain_frames` checks `now - last_render >= flush_interval` and calls `history.flush()` on that cadence. The log daemon was missing the equivalent time-based flush.

The earlier "corrupted service" / `RefCell already mutably borrowed` noise was stale iceoryx2 shared-memory state from `SIGKILL`-based debugging, not the cause: the main-thread-publisher and explicit-buffer bisections both failed before the flush fix and pass after it.

### D3 — Fix: time-based flush in the drain path

Add a `last_flush` timestamp to the `Sink` and, at the end of `drain_subscriber`, flush the pending bytes when `now - last_flush >= batch_flush_interval_ms` (default 500 ms when the field is zero), mirroring `metricsd`. No transport, wire, or config change.

## Risks / Trade-offs

- [Flush cadence vs write amplification] → the interval defaults to 500 ms and the size-based half-buffer flush is retained, so a burst still flushes early and an idle daemon flushes nothing extra.
- [Stale shared memory confounds reproduction] → the e2e test uses a unique per-PID service name and the harness cleans `/dev/shm/iox2_*`; the fix is verified against a fresh domain.

## Migration Plan

Rollback = revert the drain change and the test. No data, wire, or config format changes.

## Open Questions

None.
