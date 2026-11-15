# Proposal

## Why

The platform has no UCM coordination: an update manager cannot ask the State
Manager to park a Function Group, and nothing prevents a normal TriggerIn from
restarting application processes while an update is in progress. AUTOSAR
Adaptive expects the SM to validate UCM conditions and move the affected group
into an `Update` state, blocking application startup until the update completes
or is canceled.

## What Changes

- Add `UpdateAction` (`Start`, `Complete`, `Cancel`), `UpdateResult`
  (`Accepted`, `Rejected`, `Busy`, `InvalidGroup`, `InvalidState`, `EmFailure`),
  and POD `UpdateRequest`/`UpdateResponse` payloads.
- Serve `Nexigen/UCM/UpdateRequest` in the SM process.
- Add `sm::UpdateCoordinator`: on `Start` it checks that `PlatformFG` is `Run`,
  the group is not `MachineFG`, and `Update` is a declared state; it commands
  the EM into `Update`, records the previous state, and locks the group.
  `Complete` moves to the request's target state; `Cancel` restores the
  previous state. Both clear the lock.
- Reject TriggerIn transitions for a locked group with a new
  `TransitionResult::RejectedByUpdate`.
- Treat `Update` as an off-like EM target: entering it stops the group's
  processes instead of starting Update-specific ones in this baseline.
- Keep single-update semantics: one active update at a time.

## Capabilities

### New Capabilities

- `sm/ucm-coordination`: UCM request vocabulary, coordinator state machine, and
  the `Nexigen/UCM/UpdateRequest` service.
- `exec/update-state`: `Update` as an off-like Function Group state in the EM.

### Modified Capabilities

- `sm/state-machine`: a locked group rejects TriggerIn transitions with
  `RejectedByUpdate`; the result vocabulary gains that value.
- `sm/ipc`: the SM arbitration loop also serves the UCM request channel.

## Impact

- **Performance:** UCM traffic is cold path and request-response only; the
  TriggerIn lock check is one integer/boolean comparison per request. The
  arbitration loop gains one non-blocking `serve_once` call per tick.
- **Compatibility:** manifests without an `Update` state reject UCM `Start`
  with `InvalidState`; normal TriggerIn traffic is unchanged when no update is
  active.
- **Dependencies:** reuses `nexigen::com` request-response and the existing SM
  EM command client; no new serialization.
