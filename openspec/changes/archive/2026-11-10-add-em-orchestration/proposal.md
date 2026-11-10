# Proposal

## Why

The Execution Manager is a solid lifecycle supervisor (start/stop/restart/timeouts), but the SDV orchestration contract (`initial_spec/exec/execution_manager.md`) is only partially met: applications start in array order with no dependency tree, the EM has no Function Group configuration of its own, applications cannot report `Running` over the iceoryx2 event channel, and a PHM `SafeState` command queues FIFO behind ordinary transitions. These gaps were identified in the 2026-11-10 EM audit (deferred items recorded in `docs/em-deferred.md`).

## What Changes

- Dependency-ordered startup: applications declare `dependencies` in the manifest; a dependent application does not start until every dependency reports `Running` (REQ_EM_001).
- `ReportApplicationState(Running)`: a new client API publishing the application's readiness through the iceoryx2 event pattern (REQ_EM_011); the EM gates startup on it.
- Function Groups in the EM: groups with their states become EM configuration, and a group state switch starts/stops exactly the processes bound to that group (REQ_EM_005/006).
- SafeState priority in `StateClient`: a PHM `SafeState` command is processed with the highest priority, ahead of queued ordinary transitions (REQ_EM_007).
- Startup-timeout recovery policy: a process SIGKILLed after `startup_timeout` is logged and handed to the configured recovery rule (REQ_EM_002).
- Crash-safe IPC cleanup: before restarting a crashed process, the EM runs the iceoryx2 cleanup of stale node markers/handles (REQ_EM_014 item 3).

## Capabilities

### New Capabilities

- `exec/application-state`: the client API applications use to report `Running` to the EM over the iceoryx2 event pattern, gating dependency-ordered startup.

### Modified Capabilities

- `exec/process-supervision`: ADDED — dependency-ordered startup, startup-timeout recovery policy, crash cleanup before restart.
- `exec/app-suite`: ADDED — Function Group configuration with group-bound process start/stop.
- `exec/state-client`: ADDED — SafeState commands processed with the highest priority.

## Impact

- **Modified modules**: `nexigen::exec` (dependency gating, Function Groups, recovery policy, cleanup), `nexigen::exec::StateClient` (SafeState priority).
- **New manifest fields**: `dependencies`, `function_groups` (with `states`) in the suite/process manifest; `startup_timeout` per application (REQ_EM_012 partial).
- **New client header** `include/nexigen/exec/application_state.hpp` (report API over `com::EventNotifier`).
- **Tests**: `tests/exec/` (dependency gating, groups, SafeState priority, readiness reporting, cleanup) plus the existing StateClient/phm suites.
- **Deferred** (unchanged, see `docs/em-deferred.md`): REQ_EM_009/010 security hardening, REQ_SM_008 OTA/UCM, YAML manifests.
