# Design

## Context

The log and metric sinks already notify a companion `<service>Event` service once per record or
batch. The event channel notifies on its own service name. The trace sink publishes spans but has
no notifier, so a consumer can only poll it.

## Decisions

### D1. One WaitSet with per-stream listeners

`nexigen-recd` creates a `com::WaitSet` with `create_without_signal_handling()` because the
collector owns SIGINT/SIGTERM. It attaches:

- one `EventListener` on `<service_log>Event` when log capture is enabled;
- one on `<service_metrics>Event` when metric capture is enabled;
- one on `<service_traces>Event` when trace capture is enabled;
- one on each configured event service, because the event producer notifies on the channel name.

Any activation drains every enabled stream. Draining an empty stream is cheap, and this avoids
tracking which attachment fired.

### D2. Maintenance interval instead of a poll cycle

A single `attach_interval(100 ms)` drives the once-per-second `TimeSync` check and bounds
shutdown latency. The interval is the only periodic wake-up; it is not a short transport poll.

### D3. Trace wake-up notification

`Iceoryx2TraceSink::start()` creates an `EventNotifier` on `<service>Event` and `on_event()` emits a
best-effort notification after a successful `send_copy()`. A failed notification does not change
the return status or the drop accounting. Latency stays within the existing 400 ns median gate.

### D4. Failure handling

Listener creation or attachment failure aborts startup with a diagnostic and creates no further
session state, matching the existing service-open failures. Queue-drain failures keep the current
best-effort recording behaviour.

## Risks / Trade-offs

- **One notification per span** adds transport work to the tracing worker; the strict Release
  latency test guards the budget.
- **One-second interval** bounds stop latency; an idle collector still wakes once per second for
  `TimeSync`, which the session contract requires anyway.
