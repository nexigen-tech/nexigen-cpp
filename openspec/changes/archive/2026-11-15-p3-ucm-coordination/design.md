# Design

## Context

`nexigen-sm` already owns the arbitration loop, the TriggerIn server, the EM
command client, and the Function Group transition engine. UCM coordination is a
second control surface over the same state, so it belongs in the SM rather than
in the EM: the EM only needs to accept `Update` as an off-like state.

## Goals / Non-Goals

**Goals:**

- A request-response UCM channel with Start/Complete/Cancel.
- SM-side safety checks, single-update lock, and previous-state restore.
- TriggerIn rejection while a group is updating.
- EM `Update` state that stops processes.

**Non-Goals:**

- Package download, signature verification, activation, or reboot.
- Multiple concurrent updates or nested updates.
- Persistent update state across SM/EM restarts.
- A new UCM daemon binary.

## Decisions

### D1 — POD request/response vocabulary

```cpp
enum class UpdateAction : std::uint8_t { Start, Complete, Cancel };
enum class UpdateResult : std::uint8_t {
    Accepted, Rejected, Busy, InvalidGroup, InvalidState, EmFailure,
};
struct UpdateRequest {
    char function_group[32];
    char update_id[32];
    UpdateAction action;
    char target_state[32]; // Complete only; empty = previous state
};
struct UpdateResponse {
    UpdateResult result;
    char function_group[32];
    char active_update_id[32];
};
```

Both structs are trivially copyable and get the full conversion set. The action
and result enums are append-only vocabularies.

### D2 — Coordinator state machine

`sm::UpdateCoordinator` stores:

```cpp
bool active;
std::size_t group_index;
char update_id[32];
char previous_state[32];
```

`Start` rules:

1. no active update (`Busy` otherwise);
2. known group (`InvalidGroup`);
3. group is not `MachineFG` (`Rejected`);
4. the group declares `Update` (`InvalidState`);
5. `PlatformFG == Run` (`Rejected`).

Then it commands the EM `Update` through the existing `StateCommand` client,
retrying `InProgress` up to the group transition timeout. `Success` sets the
SM state to `Update`, stores the previous state, and marks the lock. An EM
failure returns `EmFailure` and leaves the lock clear.

`Complete` requires an active update and a matching `update_id`; the target is
the request's `target_state` (empty = previous state). `Cancel` restores the
previous state. Both wait for EM `Success` before updating the SM state and
clearing the lock; an `EmFailure` keeps the update active so the operator can
retry.

### D3 — TriggerIn lock

`UpdateCoordinator::locked(group_index)` is checked before
`StateManager::request_transition`. A locked group returns the new
`TransitionResult::RejectedByUpdate` and publishes no state change. Other
groups keep working; `MachineFG` is never locked by UCM.

### D4 — EM `Update` is off-like

`FunctionGroupRegistry::is_off_target` gains `Update`. Entering `Update` stops
the group's processes and sets the state without starting Update-specific
processes. This baseline assumes UCM itself is not a supervised group member;
starting Update helper processes is deferred.

### D5 — Serving loop

`run_sm` opens `Nexigen/UCM/UpdateRequest` and creates the coordinator with the
existing EM command client and the SM requester identity (`sm` or the configured
app name). The arbitration tick calls `serve_once()` for the UCM server after
the TriggerIn drain. No new thread, WaitSet attachment, or synchronization is
introduced; the arbitration thread stays the single writer.

## Risks / Trade-offs

- **Blocking EM wait inside the UCM handler** → bounded by the group's
  transition timeout; UCM is a cold control path, not a cyclic hot path.
- **Single update** → a second `Start` returns `Busy`; this is intentional for
  the first baseline.
- **No persistent lock** → an SM restart loses the update marker; the EM's
  Function Group state still shows `Update`, so a restart can recover by
  treating that state as locked on startup (future work).
- **No UCM helper processes** → `Update` only stops applications; UCM runs
  outside the suite in this baseline.

## Migration Plan

1. Add the vocabulary and serialization.
2. Add `Update` off-like handling and `RejectedByUpdate`.
3. Add `UpdateCoordinator` and its tests.
4. Wire the UCM server into `run_sm`.
5. Update docs and run the matrix.

## Open Questions

None blocking. Recovering a persisted `Update` state after an SM restart is a
follow-up.
