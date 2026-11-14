# Spec Delta

## MODIFIED Requirements

### Requirement: State machines execution

`nexigen::sm` SHALL run one main `PlatformFG` machine — the platform-foundation Function Group — plus an arbitrary number of independent application Function Group machines. The `PlatformFG` machine SHALL use the state vocabulary `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, and `Restart`, where `Off` is the initial state and `Run` is the target working state. Every group SHALL have a strict graph of allowed transitions, and a request for an invalid transition SHALL be rejected with `InvalidGraph`.

#### Scenario: Invalid transition is rejected

- **WHEN** a requester asks a Function Group to move from `Driving` directly to `Update` where the graph forbids it
- **THEN** the SM rejects the request with `InvalidGraph` and the group stays in `Driving`

#### Scenario: Valid transition is applied

- **WHEN** a requester asks for a transition the graph allows
- **THEN** the SM computes the target state and drives the group there

#### Scenario: PlatformFG has the mandatory state vocabulary

- **WHEN** the `PlatformFG` machine is configured
- **THEN** its states are exactly `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, and `Restart`, with `Off` as the initial state

### Requirement: Startup and shutdown sequencing

The SM SHALL sequence the platform wake-up (`Off -> Startup -> Verify -> Run`) and the shutdown (`Run -> Shutdown -> Off`, moving every application group to `Off` first, then `PlatformFG` to `Shutdown` and finally `Off`) step by step. The host advances each platform step synchronously; gating a step on an explicit EM lifecycle report is deferred until the EM exposes a PlatformFG report (the EM currently reports application readiness, not platform phases). `Restart` SHALL take the platform back to `Startup` without passing through `Off`. Application groups SHALL NOT be started until `PlatformFG` reaches `Run`.

#### Scenario: Startup advances through the states

- **WHEN** the SM takes control in `PlatformFG::Startup`
- **THEN** the host advances `Verify` -> `Run` synchronously (an explicit EM lifecycle report gate is future integration)

#### Scenario: Shutdown sequences the groups

- **WHEN** the shutdown signal arrives
- **THEN** the SM moves every application group to `Off`, commands `PlatformFG::Shutdown` and finally `Off`

#### Scenario: Restart re-enters startup without power-off

- **WHEN** a restart is requested while in `Run`
- **THEN** the SM transitions `PlatformFG` to `Restart` and then back to `Startup`, without entering `Off`

#### Scenario: Application groups wait for PlatformFG Run

- **WHEN** `PlatformFG` has not yet reached `Run`
- **THEN** no application Function Group is started
