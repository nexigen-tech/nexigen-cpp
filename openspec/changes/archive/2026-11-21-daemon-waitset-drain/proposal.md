# Proposal

## Why

`nexigen-logd` and `nexigen-metricsd` attach event listeners to their `WaitSet` but never consume
the listener activations. iceoryx2 reports the attachment as ready until `try_wait()` drains it, so
a notification can keep the callback loop hot even when the subscriber queue is empty.
`nexigen-traced`, `nexigen-sm`, `nexigen-phmd`, and `nexigen-recd` already drain their activations.

## What Changes

- Drain the event listener with `try_wait()` in the log and metric daemon callback before processing
  frames.
- Keep the existing subscriber drain and flush behaviour unchanged.

**BREAKING**: none.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logging/log-daemon`: the idle wait loop consumes notifications.
- `metrics/metrics-daemon`: the idle wait loop consumes notifications.

## Impact

- `tools/nexigen-logd/main.cpp`, `tools/nexigen-metricsd/main.cpp`.
- Existing daemon tests cover the drain path.
