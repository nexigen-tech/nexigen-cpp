# sm/lsm-runtime Specification

## Purpose
TBD - created by archiving change p3-lsm-runtime-integration. Update Purpose after archive.

## Requirements

### Requirement: LSM Function Group binding

`LightweightStateMachineConfig` SHALL carry a non-empty `function_group` naming
the Function Group it controls. A manifest SHALL bind at most one state machine
per group, the group SHALL be declared, and every state referenced by the LSM
SHALL be declared by that group.

#### Scenario: Valid binding is accepted

- **WHEN** a state machine names a declared Function Group and only uses that group's states
- **THEN** manifest validation accepts the binding

#### Scenario: Duplicate binding is rejected

- **WHEN** two state machines name the same Function Group
- **THEN** manifest validation rejects the suite

#### Scenario: Unknown state is rejected

- **WHEN** a state machine uses a state the group does not declare
- **THEN** manifest validation rejects the suite

### Requirement: EM action sink

`LsmRuntime` SHALL execute `RequestFunctionGroupState` by sending a
`StateCommand` to the EM with the originating requester id and a monotonically
increasing request id, retrying `InProgress` up to the group's transition
timeout. `PublishState` SHALL write the existing StatePublication field. A com
failure SHALL fail the action.

#### Scenario: Request action reaches the EM

- **WHEN** an LSM transition contains `RequestFunctionGroupState`
- **THEN** the EM receives the command with the group, target, and requester

#### Scenario: Publish action updates the field

- **WHEN** an LSM transition contains `PublishState`
- **THEN** readers of the group's State field observe the published state

### Requirement: TriggerIn routing through LSM

For a group bound to an LSM, TriggerIn SHALL call the machine's transition table
instead of `StateManager::request_transition`. Unbound groups SHALL keep the
existing arbitration. A UCM lock SHALL still reject the request before the LSM
runs. After an LSM attempt, the `StateManager` state SHALL match the machine's
current state, including a recovery fallback.

#### Scenario: Bound group uses the table

- **WHEN** TriggerIn requests a transition for an LSM-bound group
- **THEN** the LSM validates the table and executes its action list

#### Scenario: Unbound group keeps the old path

- **WHEN** a group has no LSM binding
- **THEN** the existing StateManager graph and priority arbitration run

#### Scenario: Recovery fallback is visible

- **WHEN** an LSM action fails and recovery moves the machine to `Fallback`
- **THEN** the StateManager reflects `Fallback` and the caller receives `ActionFailed`

### Requirement: LSM application-group gate

Before an LSM runs for an application group, the SM SHALL reject a non-off-like
target while `RootFG` is not `Run`, matching the generic graph gate.

#### Scenario: Platform not ready

- **WHEN** RootFG is not Run and an LSM-bound application group requests Active
- **THEN** the SM rejects the request with `InvalidGraph`
