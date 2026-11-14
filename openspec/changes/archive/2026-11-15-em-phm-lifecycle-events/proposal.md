# Proposal

## Why

PHM supervises application checkpoints but receives no notification from the
Execution Manager about process lifecycle. An intentional stop or a normal
oneshot completion therefore looks exactly like a missed checkpoint, so an
`Alive` supervision window can expire and trigger a recovery action while the
EM is deliberately shutting the process down. AUTOSAR Adaptive expects the EM
and PHM to exchange execution-state reports (`Running`, `Terminating`,
`Terminated`) so health supervision can be armed and disarmed with the actual
process lifecycle.

## What Changes

- Add a small `ProcessLifecycleState` vocabulary (`Running`, `Terminating`,
  `Terminated`) and a POD lifecycle notification carrying the application name
  and state.
- Publish execution-state notifications from the suite control host when the
  process table confirms readiness, when a stop starts, and when the process is
  reaped. The publisher exists only in the COM-gated control library.
- Add a PHM lifecycle listener on the standard
  `Nexigen/PHM/ExecutionState` channel plus a wake event. The PHM supervisor
  arms supervision on `Running` and suspends Alive/Deadline/Logical expiry on
  `Terminating`/`Terminated`, so a deliberate stop cannot trigger recovery.
- Keep every existing PHM behavior unchanged when no EM lifecycle event
  arrives: supervision remains armed by default for backwards compatibility.
- Cover the event ordering (Running before Terminating before Terminated) and
  the “no false Expired during shutdown” scenario.

## Capabilities

### New Capabilities

- `exec/process-lifecycle-events`: the execution-state vocabulary and the
  EM-side publication of process lifecycle transitions.

### Modified Capabilities

- `phm/supervision`: lifecycle-aware arming, suspension during intentional
  termination, and no recovery for suspended processes.
- `phm/manifest`: the lifecycle service follows the standard
  `Nexigen/PHM/ExecutionState` channel and needs no new per-process field.

## Impact

- **Performance:** publication happens only on lifecycle transitions (cold
  path), never in a cyclic hot path. The PHM listener is drained from the
  existing WaitSet loop; no new polling, allocation, or locking on the
  supervision hot path.
- **Compatibility:** a PHM daemon without lifecycle events keeps the current
  supervision behavior. A control host without a PHM subscriber publishes to a
  local service that simply has no reader.
- **Dependencies:** reuses `nexigen::com` from the COM-gated control library;
  `nexigen_exec` stays COM-free.
