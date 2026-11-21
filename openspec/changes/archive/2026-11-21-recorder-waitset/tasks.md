# Tasks

## 1. Trace wake-up notification

- [x] 1.1 Create a companion `<service_name>Event` notifier in `Iceoryx2TraceSink::start()` and
      notify best-effort after each successful publish; release it in `stop()`.
- [x] 1.2 Verify the trace sink round-trip suite and the strict Release 400 ns latency gate.

## 2. Collector wait set

- [x] 2.1 Create a `com::WaitSet` in `nexigen-recd` with per-stream event listeners and one
      maintenance interval; skip listeners for disabled streams.
- [x] 2.2 Replace the 2 ms drain loop with `wait_and_process()`, keep `TimeSync` on the interval,
      and drain once more before closing on SIGTERM/SIGINT.
- [x] 2.3 Preserve the existing collector smoke coverage for frames, events, gaps, rotation, and
      retention.

- [x] 2.4 Drop the service-details attribute set in `RawSubscriber::create`; ASan reports it as
      a leak on every raw subscriber.

## 3. Validation

- [x] 3.1 Run Debug, COM, Release, TSan, ASan, complexity, and format checks.
- [x] 3.2 Validate the change and archive it after review.
