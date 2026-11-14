# Tasks

## 1. Control host boundary

- [x] 1.1 Add `include/nexigen/exec/control_host.hpp` with the abstract `ControlHost`, `ControlHostFactory`, start/stop handler types, and the global factory slot.
- [x] 1.2 Add the COM-free factory implementation in `src/exec/control_host.cpp` and compile it into `nexigen_exec`; add unit coverage for installing/clearing a factory.
- [x] 1.3 Add `nexigen_exec_control` (COM-gated) with `ComControlHost`: owns node, registry, tracker, `ExecutionManager`, `StateClient`, listeners, and the `poll`/`request_state`/`ready`/`process_state`/handlers surface.
- [x] 1.4 Install the factory from `nexigen-exec` and `nexigen-em` only when COM is enabled; keep `nexigen_exec` free of COM symbols.

## 2. StateClient handler path

- [x] 2.1 Add optional `StartHandler`/`StopHandler` to `StateClient`; use them in `start_app`/`stop_app` when installed, preserving tracker transitions.
- [x] 2.2 Add `FunctionGroupRegistry::instance_of(app)` and make `StateClient::instance_dir_for` use the manifest instance id.
- [x] 2.3 Add public `StateClient::request(group, state)` for the control host, without exposing the private command executor.
- [x] 2.4 Add tests for handler start/stop, direct fallback, and per-app instance directories.

## 3. Suite supervisor integration

- [x] 3.1 Add `ControlHost* control` to `RuntimeSuite`; create it after runtime-app initialization via the inherited factory.
- [x] 3.2 Install suite start/stop handlers that call `launch_app`/`ChildProcess::stop`, preserving attempt artifacts and restart policy.
- [x] 3.3 Replace direct initial-state process selection with `control->request_state(group, initial_state)`; keep `launch_all` as the gate-OFF fallback.
- [x] 3.4 Use `control->ready(dep)` for dependency release and `control->on_launched/on_exited` from supervisor restarts/exits; keep `AppState`/`SuiteStatus` mapping.
- [x] 3.5 Poll the control host from `supervisor_tick`; surface `InProgress`/`Undefined`/terminal results in suite status and logs.
- [x] 3.6 Verify an end-to-end suite: reporting daemon readiness gates a dependent, a state command switches groups, and initial state enters through the transition engine.

## 4. Fold nexigen-em

- [x] 4.1 Change `nexigen-em --config` to start a suite through `ExecutionManager::start_suite`; remove the direct per-app start loop and `--service` dependence.
- [x] 4.2 Update `nexigen_em_smoke` to drive the suite supervisor's `StateClient` and assert readiness/oneshot completion.
- [x] 4.3 Keep the gate-OFF `nexigen-em` behavior documented (no reporting/StateClient).

## 5. Verification and documentation

- [x] 5.1 Update Doxygen comments in touched `include/` headers.
- [x] 5.2 Update `docs/architecture.md` Mermaid diagrams for the suite control host, readiness inbox, and `nexigen-em` front-end.
- [x] 5.3 Add suite readiness, initial-state transition, handler, and gate-OFF tests.
- [x] 5.4 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`).
- [x] 5.5 Run `clang-format`, naming/complexity/result-value gates, and `graphify update .`; confirm no hot-path allocation or latency regression.
