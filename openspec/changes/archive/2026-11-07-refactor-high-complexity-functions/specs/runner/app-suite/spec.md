# runner/app-suite — internal structure (delta)

## Purpose

This delta specifies how the `runner/app-suite` capability is internally reorganized to reduce cyclomatic complexity in suite lifecycle management. The suite manifest format, `suite.json`/`suite-state.json`/`suite-exit.json` contracts, restart/impact policy, and CLI-visible behavior remain unchanged.

## Requirements

### Requirement: Suite supervisor loop uses an explicit application state machine

The per-application lifecycle logic currently embedded in `advance_app` and `run_suite_supervisor` SHALL be expressed through a small `AppStateMachine` helper that knows how to transition a `RuntimeApp` between `Running`, `Restarting`, `Exited`, `Failed`, and `Stopped` states. State transitions SHALL be explicit functions (`on_exit_consumed`, `on_restart_timer`, `on_launch_failed`) rather than inline nested branches.

#### Scenario: Normal app exit transitions to Exited

- **WHEN** a running app's `exit.json` shows exit code 0
- **THEN** `on_exit_consumed` transitions the app to `AppState::Exited` and marks the suite dirty

#### Scenario: Abnormal Failure-impact app triggers suite restart

- **WHEN** a running app with `impact == Failure` exits abnormally
- **THEN** `on_exit_consumed` routes to the suite-wide restart path and stops scanning the current tick

#### Scenario: App restart timer expires

- **WHEN** a `Restarting` app's `next_restart_ns` is reached
- **THEN** `on_restart_timer` attempts launch and transitions to `Running` or `Failed` via `schedule_retry_or_fail`

### Requirement: `start_suite` is split into validation, conflict check, and supervisor launch

`start_suite` SHALL decompose into: manifest validation, thread-spec preflight, suite directory conflict check, creation of the suite directory, fork of the detached supervisor, and reading of the `StartReport`. The supervisor process image setup (setsid, redirect to null) SHALL remain in the child path.

#### Scenario: Invalid manifest returns before any fork

- **WHEN** `validate_suite_manifest` fails
- **THEN** `start_suite` returns the validation error before creating directories or forking

#### Scenario: Already-running suite is detected

- **WHEN** `suite.json` exists and the recorded PID is alive
- **THEN** the conflict check returns `Conflict` without forking

### Requirement: `stop_suite` is split into request, wait, and finalize phases

`stop_suite` SHALL decompose into: writing the stop request file, computing the effective grace period, waiting for `suite-exit.json` to appear, and a fallback finalize path that signals the supervisor and individual instances only if the exit file does not appear. The identity gate SHALL remain before any fallback signal.

#### Scenario: Graceful stop succeeds quickly

- **WHEN** the supervisor observes the stop request and writes `suite-exit.json`
- **THEN** `stop_suite` returns success without entering the fallback finalize path

#### Scenario: Forced stop writes terminal files

- **WHEN** the supervisor does not write `suite-exit.json` within the deadline
- **THEN** the finalize path writes a terminal `suite-state.json` and `suite-exit.json` with state `Stopped`

### Requirement: `suite_status` is split into metadata/state loading and per-app merging

`suite_status` SHALL use helpers to load suite metadata, load the recorded state (if present), derive the suite-level state from `suite-exit.json`, and merge per-application statuses from metadata with live `Runner::status` results.

#### Scenario: Sticky Failed/Restarting states are preserved

- **WHEN** a recorded app state is `Failed` or `Restarting`
- **THEN** the per-app merge helper keeps that state regardless of the live instance status

#### Scenario: Live running instance overrides unknown recorded state

- **WHEN** no recorded state exists and `Runner::status` reports `running`
- **THEN** the per-app merge helper sets the app state to `Running`

### Requirement: Refactored internals preserve all observable behavior

After decomposition, suite start, stop, status, restart, and impact policy SHALL produce identical files, logs, and return codes. No public header SHALL gain or lose a symbol.

#### Scenario: Existing app-suite tests pass unchanged

- **WHEN** the existing tests for suite start, stop, status, restart exhaustion, and impact policy run
- **THEN** they pass without modification

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `src/runner/app_suite.cpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `src/runner/app_suite.cpp`
- **THEN** no function in the file exceeds complexity 15
