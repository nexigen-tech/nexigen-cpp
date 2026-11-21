# Proposal

## Why

`nexigen-recd` polls every transport stream every 2 ms. An idle collector therefore wakes the CPU
500 times per second, and the trace stream has no wake-up notification at all, so it cannot be
driven by a blocking wait set like the log and metric streams.

## What Changes

- `nexigen-recd` blocks in a `com::WaitSet` on per-stream wake-up listeners plus one maintenance
  interval; the 2 ms polling loop is removed.
- `Iceoryx2TraceSink` creates a companion `<service>Event` notifier and emits a best-effort wake-up
  after each published frame, matching the existing log and metric sinks.
- `TimeSync` and shutdown latency are driven by the maintenance interval instead of the poll cycle.
- ASan now runs for COM builds; the raw-subscriber service-details attribute set is dropped instead
  of leaked.

**BREAKING**: none.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `recorder/telemetry-capture`: the collector blocks on wake-up notifications instead of polling.
- `tracing/iceoryx2-trace-sink`: the sink notifies a companion event service after publication.

## Impact

- `tools/nexigen-recd` and `include/nexigen/tracing/iceoryx2_trace_sink.hpp`.
- Existing producers and daemons keep their defaults.
