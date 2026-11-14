# Tasks

## 1. PlatformFG state vocabulary

- [x] 1.1 Rename the machine state machine from `MachineState` to `PlatformFG` and replace its vocabulary with `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, `Restart` (`Run` replaces `NormalRunning`), updating `include/nexigen/sm/machine_state.hpp` and its JSON/`std::formatter` conversion set. Verify: the enum, `to_string`/`from_string`, `to_json`/`from_json`, and formatter cover all six states and round-trip.
- [x] 1.2 Update the SM sequencing to `Off -> Startup -> Verify -> Run` for wake-up, `Run -> Shutdown -> Off` for shutdown (application groups to `Off` first), and `Restart -> Startup`, in `include/nexigen/sm/state_manager.hpp` and `src/sm/`. Verify: the state-machine tests cover the new transitions, including `Restart` and `Off` as the initial state.
- [x] 1.3 Gate application-group startup on `PlatformFG::Run` so no application group starts before the platform foundation is ready. Verify: a test asserts an application group does not start while `PlatformFG` is `Startup`/`Verify`.

## 2. Process termination behavior

- [x] 2.1 Add `type` (`daemon`/`oneshot`) to the process manifest schema (the `AppSpec` and its JSON round-trip), mirroring AUTOSAR `terminationBehavior`. Verify: a manifest with each type parses and round-trips; an invalid type is rejected.
- [x] 2.2 Classify self-termination in the supervision loop: a `oneshot` exiting 0 is a normal termination, a `daemon` terminating without a stop request (or before readiness) is an unexpected self-termination. Verify: unit tests cover both the normal and the failure paths.

## 3. Readiness reporting

- [x] 3.1 Distinguish Reporting from Non-reporting processes: a `daemon` reports `Run` via `ReportExecutionState`, a `oneshot` does not report. Verify: a test asserts a daemon that never reports stays non-running, and one that reports reaches running.

## 4. Unrecoverable State and confirmation

- [x] 4.1 Add the `Unrecoverable State` to the Execution Manager: on an unresolvable failure it stops acting, terminates all managed processes, and invokes the project-specific hook. Verify: a test drives an unresolvable failure and asserts all processes are terminated and the hook runs.
- [x] 4.2 Implement the state-transition confirmation condition: a Function Group switch to `Run` is confirmed only when every bound `daemon` has reported readiness and every bound `oneshot` has exited 0. A retried switch must not relaunch a process that is already ready, failed, exited, or still awaiting oneshot completion. Verify: a test asserts an unready daemon blocks confirmation, a ready set confirms it, and `nexigen_em_smoke` asserts one launch across retries.

## 5. Tests, docs, and matrix

- [x] 5.1 Update `docs/architecture.md`'s module map and state-management Mermaid diagram to name `PlatformFG` and its six states. Verify: the diagram and map reflect the renamed group.
- [x] 5.2 Run the full build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer, AddressSanitizer — with the `sm` and `exec` suites. Verify: 100% pass, no sanitizer report.
- [x] 5.3 Validate that every `application_group` declares `Off` and starts `Off`/`SafeState`, reject invalid manifests in the SM loader, and make the validating `StateManager::create()` factory the only construction path (private constructor). Verify: `validate()`/factory unit tests and the SM hostile/required-spec tool tests.
- [x] 5.4 Host the `ExecutionStateTracker` in `nexigen-em`: `StateClient` opens the per-application `Nexigen/EM/State/<app>` listeners, drains them into the tracker on every `serve_once`, and polls `exit.json` for pending oneshots. Verify: the state-client suite reports readiness over the real channel and confirms the switch, and `nexigen_em_smoke` exercises the real host end to end.
