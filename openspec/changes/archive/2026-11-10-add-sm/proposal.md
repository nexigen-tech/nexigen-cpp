# Proposal

## Why

The platform's control loop is incomplete: the Execution Manager runs processes, PHM watches their health, but nothing arbitrates the system's global modes. The SDV architecture assigns that role to the State Manager (`nexigen::sm`): the central state machine that accepts triggers (from PHM, applications, and the platform), resolves conflicts by priority, computes target states for Function Groups, and orders the EM to switch them — including emergency `SafeState` transitions on PHM fault events. The `com` transport (event, wait-set, request-response, field) and the `phm` supervision layer are now in place, so the final infrastructure module can be built on top of them.

## What Changes

- Add the `nexigen::sm` module: the central state machine (one `MachineState` plus independent `FunctionGroup` machines with strict transition graphs), request arbitration by requester priority, startup/shutdown sequencing, a recovery handler for PHM fault events, and a transition watchdog.
- Add the SM IPC surface over `com`: `TriggerIn` request service, current-state publication through the Field pattern (`history_size=1`), and the prepare-for-transition handshake.
- Implement `nexigen::exec::StateClient` in the EM: the receiver of `SetState(FunctionGroup, TargetState)` request-response commands, mapping Function Groups onto the EM's managed processes, executing start/stop, and acknowledging completion.
- Re-route PHM's `SafeState` escalation through SM (per HLR-SM-030): PHM emits a recovery-notification event to SM; SM arbitrates and commands the EM via StateClient. Local `RestartProcess` stays a direct PHM→EM request.
- Add the SM manifest (function groups, states, allowed transitions, timeouts, requester priorities), audit logging into a lock-free ring buffer, atomic performance metrics, memory locking, thread affinity, and SCHED_FIFO priorities.

## Capabilities

### New Capabilities

- `sm/state-machine`: the central arbitration engine — MachineState and FunctionGroup machines, transition-graph validation, requester-priority arbitration, startup/shutdown sequencing.
- `sm/ipc`: the SM's external interfaces — `TriggerIn` request service, current-state publication via the Field pattern, prepare-for-transition handshake.
- `sm/recovery-handler`: the high-priority PHM fault-event interface that interrupts non-critical transitions and drives a failed Function Group into `SafeState`/`DegradedMode` within 1 ms.
- `sm/lifecycle`: managed startup under the EM (foreground, CLI/env config, readiness report), self-health checkpoints to PHM, memory locking, WaitSet event loop, transition watchdog, thread affinity and priorities.
- `sm/telemetry`: non-blocking transition audit log into a lock-free ring buffer plus atomic performance metrics (transition duration, arbitration latency, rejection/emergency/timeout counters).
- `sm/manifest`: the declarative state-management manifest (function groups, states, allowed transitions, timeouts, requester priorities, metrics flush interval).
- `exec/state-client`: the EM-side receiver of `SetState` commands — request-response server, Function-Group-to-process mapping, start/stop execution, and transition acknowledgements.

### Modified Capabilities

- `phm/recovery`: the `SafeState` escalation path changes — instead of a direct PHM→EM request, PHM emits a recovery-notification event to the SM (per HLR-SM-030/REQ_SM_007); `RestartProcess` remains a direct PHM→EM request.

## Impact

- **New module** `include/nexigen/sm/` + `src/sm/`, `nexigen_sm` static target; the SM process runs as `nexigen-sm` under `tools/`.
- **Modified modules**: `nexigen::exec` (StateClient server + FunctionGroup mapping), `nexigen::phm` (SafeState re-route to SM).
- **New tests** `tests/sm/` (state machine, arbitration, ipc, recovery, telemetry, manifest) and exec/phm test updates.
- **Dependencies**: `nexigen_com`, `nexigen_phm`, `nexigen_exec`, `nexigen_core` (SPSC ring buffer, affinity/thread management). No new third-party dependencies.
- **Deferred**: `REQ_SM_008` (OTA/UCM coordination) until the update manager exists — recorded in design.
