# Proposal

## Why

`nexigen-logd`'s frame reception — the `logging/log-daemon` scenario «Frame arrives and is processed» — is untested end-to-end: every existing test drives a *manual* `Subscriber<LogFrame>` with busy-polling, never the actual daemon binary with its `WaitSet`/event-listener drain. In practice the daemon never receives frames from an `Iceoryx2Logger` (its log file stays empty) even though the metrics daemon, built on the same iceoryx2 sink→daemon pattern, does receive frames. This blocks the `add-suite-2-apps` integration example's log observability and leaves the daemon's core promise unverified.

## What Changes

- Add an end-to-end test that runs the real `nexigen-logd` against an `Iceoryx2Logger` (or a minimal `LogFrame` publisher) and asserts the daemon writes the published record, replacing the manual-subscriber gap.
- Fix whatever the new test exposes in the frame-delivery path (see design.md for the investigation so far).
- Keep the daemon's public contract unchanged; the fix is internal to the reception path and the test harness.

## Capabilities

### New Capabilities

<!-- none -->

### Modified Capabilities

- `logging/log-daemon`: the «Daemon subscribes and drains log frames» requirement gains a scenario asserting real-daemon frame reception end-to-end, and the reception path is made to satisfy it.

## Impact

- **Code**: `tools/nexigen-logd/` and/or `include/nexigen/logging/detail/iceoryx2_sink.hpp` (the fix), plus a new test in `tests/logging/`.
- **Dependencies**: none new.
- **Performance / latency / allocation**: the daemon drain loop is cold-path (no producer hot path involved); the fix adds only a time-based flush of the pending batch and preserves the WaitSet zero-polling contract.
- **Breaking changes**: none.
