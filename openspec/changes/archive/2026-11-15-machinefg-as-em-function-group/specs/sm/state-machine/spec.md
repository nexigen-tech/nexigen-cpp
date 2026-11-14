# Spec Delta

## MODIFIED Requirements

### Requirement: Startup and shutdown sequencing

The SM SHALL sequence the platform wake-up (`Off -> Startup -> Verify -> Run`) and the shutdown (`Run -> Shutdown -> Off`, moving every application group to `Off` first, then `PlatformFG` to `Shutdown` and finally `Off`) step by step. When an EM control path is available, the SM SHALL report its own readiness, query `GetInitialMachineStateTransitionResult`, and request `MachineFG:Verify` and `MachineFG:Run` through `StateClient`, advancing only after the EM acknowledges `Success`; a terminal initial-transition failure SHALL abort startup visibly. When no EM control path is reachable, the SM SHALL keep the documented synchronous fallback. Shutdown SHALL request `MachineFG:Shutdown` (never `Off`); the EM owns the final `Off`. `Restart` SHALL take the platform back to `Startup` without passing through `Off`. Application groups SHALL NOT be started until `PlatformFG` reaches `Run`.

#### Scenario: Startup advances through the states

- **WHEN** the SM takes control and an EM control path is reachable
- **THEN** it reports readiness, waits for the initial transition result, and advances `Verify -> Run` only after the EM confirms each state

#### Scenario: Startup failure aborts visibly

- **WHEN** the initial `MachineFG:Startup` transition fails or `Verify`/`Run` exhausts its budget
- **THEN** the SM exits nonzero instead of advancing the platform state

#### Scenario: Standalone SM keeps the synchronous fallback

- **WHEN** the EM query and command channels are unavailable
- **THEN** the SM logs a warning and advances `Verify -> Run` synchronously as before

#### Scenario: Shutdown sequences the groups

- **WHEN** the shutdown signal arrives
- **THEN** the SM moves every application group to `Off`, requests `MachineFG:Shutdown`, and exits while the EM performs the final `Off`

#### Scenario: Restart re-enters startup without power-off

- **WHEN** a restart is requested while in `Run`
- **THEN** the SM transitions `PlatformFG` to `Restart` and then back to `Startup`, without entering `Off`

#### Scenario: Application groups wait for PlatformFG Run

- **WHEN** `PlatformFG` has not yet reached `Run`
- **THEN** no application Function Group is started
