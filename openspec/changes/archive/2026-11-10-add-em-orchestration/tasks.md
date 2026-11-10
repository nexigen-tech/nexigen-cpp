# Tasks

## 1. Readiness reporting

- [x] 1.1 Create `include/nexigen/exec/application_state.hpp`: `ApplicationStateReporter` that publishes `ReportApplicationState(ApplicationState::Running)` over `com::EventNotifier` on the per-application channel `Nexigen/EM/State/<app>`; `to_string` + formatter. Verify: an emitted Running notification reaches a `com::EventListener` without polling.

## 2. Dependency-ordered startup

- [x] 2.1 Add `dependencies` (list of application names) to `AppSpec` + manifest JSON round-trip. Verify: the field survives a serialize/parse round-trip.
- [x] 2.2 Gate suite startup on dependencies: a dependent application starts only after every dependency reported `Running` (event, file state as fallback). Verify: a dependent waits until its dependency reports, and fails visibly when the dependency times out.

## 3. Function Groups

- [x] 3.1 Add `function_groups` (with `states`) to the suite manifest and the per-application group list to `AppSpec`; JSON round-trip. Verify: fields survive a round-trip.
- [x] 3.2 Create the EM group registry (group -> bound processes, current state per group) with `to_string` + formatter. Verify: diagnostics name each group and its state.
- [x] 3.3 Wire `StateClient::execute` to the group registry: a group state switch stops (Off-like) or starts (active) exactly the bound processes with the graceful shutdown sequence. Verify: a group transition stops only its own processes.

## 4. SafeState priority

- [x] 4.1 `StateClient::serve_once`: a pending `SafeState` command is executed and acknowledged before ordinary queued commands; ordinary commands keep FIFO among themselves. Verify: a SafeState overtakes queued ordinary transitions, and the remainder resume in order.

## 5. Recovery policy and crash cleanup

- [x] 5.1 Startup-timeout recovery: the SIGKILL-on-`startup_timeout` path logs the failure via `nexigen::log` and feeds the existing `restart_attempts` machinery. Verify: a timed-out process is logged and restarted per policy.
- [x] 5.2 Crash cleanup before restart: the restart path runs the iceoryx2 cleanup of the crashed process's stale node markers/handles before relaunch. Verify: a relaunched process re-registers its services with the same identifiers without `AlreadyExists`.

## 6. Tests

- [x] 6.1 `tests/exec/` — readiness reporting, dependency gating, function groups, SafeState priority, timeout recovery, crash cleanup. Verify: suites pass.

## 7. Gates

- [x] 7.1 Debug build with the gate OFF stays byte-identical; gate ON builds and all tests pass under `ctest`.
- [x] 7.2 Release (`-O3 -march=native -flto`), TSAN, and ASAN gate-ON builds pass all tests.
  - Release gate-ON: 45/45. ASAN gate-ON: 45/45. TSAN gate-ON: 45/45 (плюс 40 повторов `ex_app_cli_settings` подряд).
  - Наблюдение: в двух ранних TSAN-прогонах кратко мелькал сбой `ex_app_cli_settings` (`too few distinct trace ids`). Пример линкуется только с `nexigen::app` (tracing), к exec отношения не имеет; сбой не воспроизведён в ~45 последующих прогонах — предсуществующая трассировочная гонка, вне рамок change.
- [x] 7.3 `openspec validate "add-em-orchestration" --strict` passes.
