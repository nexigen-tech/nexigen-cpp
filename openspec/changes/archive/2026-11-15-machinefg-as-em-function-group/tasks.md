# Tasks

## 1. MachineFG vocabulary and manifest

- [x] 1.1 Add `include/nexigen/exec/machine_function_group.hpp` with the reserved name, six state constants, and helpers.
- [x] 1.2 Validate a declared `MachineFG` in `validate_suite_manifest`: all six states present, initial state `Off`/`Startup`, other groups unchanged.
- [x] 1.3 Add `FunctionGroupRegistry::is_machine_group(index)` and make `initial_state_for_group` return `Startup` for `MachineFG`.
- [x] 1.4 Add manifest tests for the valid group, missing state, invalid initial state, bound apps, and absent group.

## 2. EM-owned lifecycle and query

- [x] 2.1 Add `include/nexigen/exec/machine_state_query.hpp` with `std::monostate` request and `InitialMachineStateTransitionResultResponse` (conversion set).
- [x] 2.2 Reject `SetState(MachineFG, Off)` with `InvalidTransition` in `StateClient::execute`.
- [x] 2.3 Add `StateClient::shutdown_machine()` and `ControlHost::shutdown_machine()`; call it from operator stop and terminal failure.
- [x] 2.4 Record the first terminal `MachineFG` result in `ComControlHost`; never overwrite it with `Verify`/`Run`.
- [x] 2.5 Serve `Nexigen/EM/InitialMachineStateTransitionResult` from `ComControlHost::poll()`.
- [x] 2.6 Add tests for Off rejection, recorded initial success/failure, query response, and internal shutdown.

## 3. State Manager gating

- [x] 3.1 Add `include/nexigen/sm/machine_lifecycle.hpp` with readiness reporting, query, and `MachineFG` SetState helpers.
- [x] 3.2 Parse `--app-name`/`NEXIGEN_APP_NAME` in `nexigen-sm`; export `NEXIGEN_APP_NAME` from `ExecutionManager::start`.
- [x] 3.3 Gate `run_sm` startup on the query and `Verify`/`Run` confirmations; abort on terminal failure; keep the synchronous fallback when no EM is reachable.
- [x] 3.4 Request `MachineFG:Shutdown` (never `Off`) on shutdown.
- [x] 3.5 Add `nexigen_sm` tests for the lifecycle client with a mock query/command server and the fallback path.

## 4. Verification and docs

- [x] 4.1 Declare `MachineFG` in the example suite manifests; binding the real platform daemons is deferred to the platform manifest work.
- [x] 4.2 Update `docs/architecture.md` for the MachineFG/EM/SM lifecycle.
- [x] 4.3 Run Debug, Release, TSAN, ASAN and the gate-OFF build; run complexity/naming/result gates and `graphify update .`.
