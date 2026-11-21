# Design

## Context

`WaitSet` only reports that an attachment is ready; it does not consume the notification. For a
`Listener`, pending activations must be drained with `try_wait()`. Without that drain, the listener
stays ready and `wait_and_process()` invokes the callback again immediately.

## Decisions

### D1. Drain the listener at the start of every callback

Both daemons call `try_wait()` with a no-op callback before draining their subscriber. This clears
the activation that woke the loop and keeps the zero-CPU idle guarantee.

### D2. No counter or callback expansion

The daemons do not need the activation payload. A no-op callback avoids adding state to the hot
wake-up path and matches the recorder's `WakeupSources::drain_notifications()`.

## Risks / Trade-offs

- **One extra non-blocking transport call per wake-up**; negligible compared with the subscriber
  drain that follows.
