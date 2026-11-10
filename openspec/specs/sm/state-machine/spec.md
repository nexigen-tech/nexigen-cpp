# sm/state-machine Specification

## Purpose

Defines the SM's central arbitration engine: one MachineState machine plus independent FunctionGroup machines with strict transition graphs, requester-priority arbitration of conflicting requests, and multi-step startup/shutdown sequencing.

## Requirements

### Requirement: State machines execution

`nexigen::sm` SHALL run one main `MachineState` machine and an arbitrary number of independent `FunctionGroup` machines; every group SHALL have a strict graph of allowed transitions, and a request for an invalid transition SHALL be rejected with `InvalidTransition`.

#### Scenario: Invalid transition is rejected

- **WHEN** a requester asks a Function Group to move from `Driving` directly to `Update` where the graph forbids it
- **THEN** the SM rejects the request with `InvalidTransition` and the group stays in `Driving`

#### Scenario: Valid transition is applied

- **WHEN** a requester asks for a transition the graph allows
- **THEN** the SM computes the target state and drives the group there

### Requirement: Request arbitration

When several requesters ask for conflicting states, the SM SHALL resolve the conflict by the configured requester priorities, with PHM fault events ranked above hardware power signals, diagnostics above user applications.

#### Scenario: Higher priority wins

- **WHEN** a user application requests `Active` while a diagnostics requester asks for `Parking`
- **THEN** the SM applies the diagnostics request because its priority is higher

### Requirement: Startup and shutdown sequencing

The SM SHALL sequence the platform wake-up (`Startup -> Verify -> NormalRunning`) and the shutdown (`Off` for every application group, then `MachineState::Shutdown`) step by step, waiting for the EM's report after each step.

#### Scenario: Startup advances through the states

- **WHEN** the SM takes control in `MachineState::Startup`
- **THEN** it initiates `Verify` and, after the EM reports success, advances to `NormalRunning`

#### Scenario: Shutdown sequences the groups

- **WHEN** the ignition-off signal arrives
- **THEN** the SM moves every application group to `Off`, waits for the EM's stop report, and finally commands `MachineState::Shutdown`

### Requirement: Transition result vocabulary

`nexigen::sm` SHALL expose a `TransitionResult` scoped enum with the values `Success`, `RejectedByPriority`, `InvalidGraph`, and `Timeout`, with the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json`, and `std::formatter`).

#### Scenario: TransitionResult round-trips

- **WHEN** each `TransitionResult` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name
