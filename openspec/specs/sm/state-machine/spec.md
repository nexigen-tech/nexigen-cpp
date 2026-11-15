# sm/state-machine Specification

## Purpose

Defines the SM's central arbitration engine: one RootFG machine plus independent FunctionGroup machines with strict transition graphs, requester-priority arbitration of conflicting requests, and multi-step startup/shutdown sequencing.

## Requirements

### Requirement: State machines execution

`nexigen::sm` SHALL run one main `RootFG` machine — the root-foundation Function Group — plus an arbitrary number of independent application Function Group machines. The `RootFG` machine SHALL use the state vocabulary `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, and `Restart`, where `Off` is the initial state and `Run` is the target working state. Every group SHALL have a strict graph of allowed transitions, and a request for an invalid transition SHALL be rejected with `InvalidGraph`.

#### Scenario: Invalid transition is rejected

- **WHEN** a requester asks a Function Group to move from `Driving` directly to `Update` where the graph forbids it
- **THEN** the SM rejects the request with `InvalidGraph` and the group stays in `Driving`

#### Scenario: Valid transition is applied

- **WHEN** a requester asks for a transition the graph allows
- **THEN** the SM computes the target state and drives the group there

#### Scenario: RootFG has the mandatory state vocabulary

- **WHEN** the `RootFG` machine is configured
- **THEN** its states are exactly `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, and `Restart`, with `Off` as the initial state

### Requirement: Request arbitration

When several requesters ask for conflicting states, the SM SHALL resolve the conflict by the configured requester priorities, with PHM fault events ranked above hardware power signals, diagnostics above user applications.

#### Scenario: Higher priority wins

- **WHEN** a user application requests `Active` while a diagnostics requester asks for `Parking`
- **THEN** the SM applies the diagnostics request because its priority is higher

### Requirement: Startup and shutdown sequencing

The SM SHALL sequence the platform wake-up (`Off -> Startup -> Verify -> Run`) and the shutdown (`Run -> Shutdown -> Off`, moving every application group to `Off` first, then `RootFG` to `Shutdown` and finally `Off`) step by step. When an EM control path is available, the SM SHALL report its own readiness, query `GetInitialRootStateTransitionResult`, and request `RootFG:Verify` and `RootFG:Run` through `StateClient`, advancing only after the EM acknowledges `Success`; a terminal initial-transition failure SHALL abort startup visibly. When no EM control path is reachable, the SM SHALL keep the documented synchronous fallback. Shutdown SHALL request `RootFG:Shutdown` (never `Off`); the EM owns the final `Off`. `Restart` SHALL take the platform back to `Startup` without passing through `Off`. Application groups SHALL NOT be started until `RootFG` reaches `Run`.

#### Scenario: Startup advances through the states

- **WHEN** the SM takes control and an EM control path is reachable
- **THEN** it reports readiness, waits for the initial transition result, and advances `Verify -> Run` only after the EM confirms each state

#### Scenario: Startup failure aborts visibly

- **WHEN** the initial `RootFG:Startup` transition fails or `Verify`/`Run` exhausts its budget
- **THEN** the SM exits nonzero instead of advancing the platform state

#### Scenario: Standalone SM keeps the synchronous fallback

- **WHEN** the EM query and command channels are unavailable
- **THEN** the SM logs a warning and advances `Verify -> Run` synchronously as before

#### Scenario: Shutdown sequences the groups

- **WHEN** the shutdown signal arrives
- **THEN** the SM moves every application group to `Off`, requests `RootFG:Shutdown`, and exits while the EM performs the final `Off`

#### Scenario: Restart re-enters startup without power-off

- **WHEN** a restart is requested while in `Run`
- **THEN** the SM transitions `RootFG` to `Restart` and then back to `Startup`, without entering `Off`

#### Scenario: Application groups wait for RootFG Run

- **WHEN** `RootFG` has not yet reached `Run`
- **THEN** no application Function Group is started

### Requirement: Transition result vocabulary

`nexigen::sm` SHALL expose a `TransitionResult` scoped enum with the values `Success`, `RejectedByPriority`, `RejectedByUpdate`, `ActionFailed`, `InvalidGraph`, and `Timeout`, with the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json`, and `std::formatter`).

#### Scenario: TransitionResult round-trips

- **WHEN** each `TransitionResult` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name

### Requirement: Pending transitions carry a request id

The SM SHALL allocate a monotonically increasing request id for each accepted
TriggerIn transition, store it in the pending transition, and reuse it for every
`InProgress` retry. On `OperationCanceled`, the SM SHALL drop the pending entry
without forcing a `SafeState` fallback, because the request was superseded
rather than failed. The SM RootFG lifecycle client SHALL maintain its own
request-id sequence.

#### Scenario: InProgress retries reuse the id

- **WHEN** the EM acknowledges `InProgress` for a pending transition
- **THEN** the next retry carries the same request id

#### Scenario: Cancellation drops the pending transition

- **WHEN** the EM acknowledges `OperationCanceled` for a pending transition
- **THEN** the SM removes it and does not force `SafeState`

#### Scenario: RootFG requests use their own ids

- **WHEN** the SM commands RootFG during startup
- **THEN** those commands carry ids from the lifecycle client's sequence
