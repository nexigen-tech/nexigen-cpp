# Proposal

## Why

The platform has no central health supervision: nothing watches child processes for liveness, logical ordering, or deadline violations, and nothing coordinates recovery. The `nexigen::com` transport (publish-subscribe, event, request-response, field, wait-set) and the `nexigen::exec` execution manager are now in place, so the next infrastructure layer — the Platform Health Management (PHM) daemon and its client library — can be built on top of them, exactly as the original SDV specification `initial_spec/phm/phm.md` (REQ_PHM_001–012) prescribes.

## What Changes

- Add the client library `nexigen::phm`: applications emit checkpoints and health-channel state transitions over iceoryx2 `Event` notifications (`Notifier`), with no sockets and no serialization.
- Add the `nexigen-phmd` daemon: a supervision engine that consumes all monitoring channels through a single `WaitSet` loop (0% CPU idle), and runs Alive / Logical / Deadline supervision plus status aggregation into `Ok`/`Failed`/`Expired`/`Stopped`.
- Add recovery actions: local `RestartProcess` (a new PHM→EM request-response channel over `com`), global `SafeState` escalation (a `StateClient` command interface whose EM-side implementation is deferred to `nexigen::sm`), and non-blocking `Fault Storage`.
- Add a declarative YAML/JSON manifest describing processes, checkpoint graphs, deadline pairs, and recovery rules.
- Add the infrastructure guarantees: CPU-core isolation + `SCHED_FIFO` for the daemon, and babbling-idiot protection via event aggregation and time margins.

## Capabilities

### New Capabilities

- `phm/health-channel`: the inter-process health channel — `nexigen::phm` client API for checkpoint and health-status notifications over the iceoryx2 event pattern, with health-channel states `Ok`/`Warning`/`Error`/`Critical`.
- `phm/supervision`: the daemon's supervision engine — Alive, Logical, and Deadline supervision over a single WaitSet loop, status aggregation with tolerance counters, core isolation, and babbling-idiot protection.
- `phm/recovery`: recovery actions — local process restart via a PHM→EM request-response channel, global SafeState escalation through the StateClient interface, and non-blocking fault storage.
- `phm/manifest`: the declarative health-logic manifest (YAML/JSON) that configures processes, checkpoint graphs, deadline pairs, and recovery rules.

### Modified Capabilities

None.

## Impact

- **New module** `include/nexigen/phm/` + `src/phm/` (client library), `nexigen_phm` static target.
- **New tool** `nexigen-phmd` under `tools/` (the daemon binary).
- **New tests** `tests/phm/` (health-channel, supervision, recovery, manifest).
- **Dependencies**: `nexigen_com` (event, wait-set, request-response) and `nexigen_exec` (process restart). No new third-party dependencies — iceoryx2 pin is unchanged.
- **Deferred**: the EM-side `StateClient` implementation (the receiver of the `SafeState` command) is designed here as an interface but implemented by the later `nexigen::sm` change.
