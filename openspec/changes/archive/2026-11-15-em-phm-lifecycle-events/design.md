# Design

## Context

PHM already runs a WaitSet-driven supervision loop (Alive/Logical/Deadline)
with per-process event listeners. It has no notion of EM process lifecycle:
an intentional `SIGTERM`/`SIGKILL` from the EM is indistinguishable from a
missed checkpoint. The COM-gated `ComControlHost` introduced by
`unify-exec-suite-control-path` already owns the readiness tracker and observes
every lifecycle transition, so it is the natural publisher.

## Goals / Non-Goals

**Goals:**

- Publish `Running`, `Terminating`, and `Terminated` process lifecycle events
  from the EM control host.
- Let PHM arm supervision on `Running` and suspend it during intentional
  termination, without false recovery.
- Preserve existing PHM behavior when no EM lifecycle event is received.

**Non-Goals:**

- Changing ALIVE/LOGICAL/DEADLINE semantics for running processes.
- A new PHM manifest section per process (the standard channel is derived from
  the application name).
- EM→PHM supervision of platform processes that are not bound to the suite.
- Hardware watchdog, UCM, or diagnostics integration.

## Decisions

### D1 — Vocabulary and payload live in `nexigen_exec`

`include/nexigen/exec/process_lifecycle.hpp` defines:

```cpp
enum class ProcessLifecycleState : std::uint8_t {
    Running, Terminating, Terminated,
};

struct ProcessLifecycleEvent {
    char app[k_process_lifecycle_name_length]; // 32 bytes, NUL-terminated
    ProcessLifecycleState state;
};
```

The struct is trivially copyable and fixed-size, so it satisfies
`com::concepts::Message` and never allocates on publish. It gets the mandatory
conversion set (`to_string`, `from_string`, JSON, formatter) behind the
COM-gated control library; the header itself stays COM-free.

### D2 — One standard channel

The control host publishes `ProcessLifecycleEvent` samples on
`Nexigen/PHM/ExecutionState` and rings a wake event on
`Nexigen/PHM/ExecutionStateEvent`. A single publisher owns both handles; PHM
creates one `Subscriber<ProcessLifecycleEvent>` and one `EventListener` for the
wake event, then drains all pending samples from its existing WaitSet callback.
No per-process channel is added, so the manifest schema does not change.

### D3 — Publication points match the process table

`ComControlHost` keeps `last_published_lifecycle_` per application (a small
fixed vector built from the manifest at construction). In `poll()` it compares
the tracker's `process_state(app)` after `serve_once()` and publishes:

- `Running` when the tracker reaches `ProcessState::Running` (readiness report
  for a reporting daemon, immediate for a non-reporting process);
- `Terminating` when the stop handler starts;
- `Terminated` when `on_exited` records the reap.

`on_launched` resets the published state to `Starting` (no event), so a restart
cannot inherit a stale `Terminated` marker.

### D4 — PHM arms and suspends supervision

`phm::Supervisor` gains per-process `EmLifecycleState`:

```cpp
enum class EmLifecycleState : std::uint8_t {
    Unknown, Running, Terminating, Terminated,
};
```

- `on_execution_state(index, state)` stores the state; `Running` resets the
  Alive/Deadline counters and marks the process supervised; `Terminating` and
  `Terminated` mark it suspended.
- `on_alive_tick` and `on_checkpoint` ignore suspended processes.
- `status(index)` stays `Ok` while suspended (never `Expired`), and
  `apply_recovery` skips suspended processes.
- `Unknown` preserves the pre-change behavior (supervision armed after the
  first checkpoint / alive tick), so a PHM without an EM keeps working.

`nexigen-phmd` attaches the lifecycle subscriber and wake listener next to the
per-process checkpoint listeners. The existing `handle_attachment` drains the
lifecycle samples and calls `supervisor.on_execution_state`.

### D5 — Event ordering

`Running` may be published more than once (readiness re-report or restart
cycle); duplicates are idempotent. `Terminating` precedes `Terminated` because
the stop handler publishes before it calls the suite stop path, and
`on_exited` publishes only after the reap. A missing `Terminating` (crash) still
produces `Terminated`, which suspends supervision; the crash remains visible in
the EM process table and suite status.

## Risks / Trade-offs

- **Event coalescing** → the wake event only signals “new samples”; the
  payload channel preserves every sample, so an event id is never the sole
  source of truth.
- **Service without a reader** → publishing succeeds with no PHM subscriber;
  the control host does not require PHM to be present.
- **Late PHM start** → PHM arms on the next `Running` report or keeps the
  legacy behavior if it starts after the process; no deadlock.
- **Extra wakeups** → only lifecycle transitions produce samples, so the PHM
  idle loop stays asleep in steady state.

## Migration Plan

1. Add the vocabulary and payload.
2. Add the control-host publisher and the per-app published-state vector.
3. Add the PHM lifecycle state to `Supervisor` and the daemon listener.
4. Update the PHM supervision spec and tests; run the four-config matrix.

## Open Questions

None blocking. Whether to make lifecycle reporting mandatory for every
supervised process is deferred until the platform manifest work.
