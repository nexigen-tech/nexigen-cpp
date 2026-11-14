# Spec Delta

## ADDED Requirements

### Requirement: MachineFG is a reserved EM Function Group

The EM SHALL reserve the Function Group name `MachineFG` and the fixed state
vocabulary `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, and `Restart`. A suite
manifest MAY declare the group; when declared, all six states SHALL be present,
applications MAY bind to it like any other Function Group, and the suite initial
state SHALL be `Startup` (an explicit `initial_state` of `Off` or `Startup` is
accepted; any other value is rejected). A manifest without `MachineFG` SHALL
remain valid.

#### Scenario: MachineFG declares the mandatory vocabulary

- **WHEN** a suite manifest declares `MachineFG` with `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, and `Restart`
- **THEN** manifest validation accepts the group and applications can bind to those states

#### Scenario: Missing MachineFG state is rejected

- **WHEN** a declared `MachineFG` omits `Verify`
- **THEN** manifest validation rejects the suite

#### Scenario: Invalid MachineFG initial state is rejected

- **WHEN** `MachineFG` declares `initial_state: "Run"`
- **THEN** manifest validation rejects the suite

#### Scenario: Suite without MachineFG remains valid

- **WHEN** a manifest declares only application Function Groups
- **THEN** validation succeeds and the EM exposes no MachineFG

### Requirement: The EM owns the initial MachineFG transition

At suite start the EM SHALL request `MachineFG:Startup` through the shared
transition engine, independently of any State Manager request, and SHALL record
the first terminal result of that initial transition. `InProgress` keeps the
result unsettled; a later `Verify` or `Run` transition SHALL NOT overwrite it.

#### Scenario: Startup is entered by the EM

- **WHEN** a suite declares `MachineFG` and starts
- **THEN** the supervisor requests `MachineFG:Startup` before any State Manager command and starts the configured foundation processes

#### Scenario: Initial result is recorded

- **WHEN** the initial `MachineFG:Startup` transition confirms or fails
- **THEN** the EM records that terminal state result for the query service

### Requirement: MachineFG Off is EM-owned

An external `SetState(MachineFG, Off)` SHALL be rejected with
`InvalidTransition`. The EM SHALL reach `Off` only through its own shutdown path,
which sets the group state after stopping the suite; the State Manager SHALL
request `Shutdown`, never `Off`.

#### Scenario: External Off is rejected

- **WHEN** a State Manager sends `SetState(MachineFG, Off)`
- **THEN** the EM acknowledges `InvalidTransition` and the group state does not change

#### Scenario: Operator stop reaches Off internally

- **WHEN** the suite is stopped by the operator and declares `MachineFG`
- **THEN** the supervisor stops the suite and records `MachineFG` as `Off` without a `SetState` command

### Requirement: Initial machine transition result query

The EM SHALL serve a request-response query on
`Nexigen/EM/InitialMachineStateTransitionResult`. The response SHALL carry the
recorded initial transition result, or `InvalidArgument` when the manifest
declares no `MachineFG`. The request SHALL be an empty POD and the response a
trivially copyable POD, so neither allocates or serializes on the transport.

#### Scenario: State Manager reads a successful startup

- **WHEN** the State Manager queries the service after the initial `MachineFG:Startup` confirmed
- **THEN** the response reports `Success`

#### Scenario: State Manager reads a failed startup

- **WHEN** the initial `MachineFG:Startup` transition exhausted its startup budget
- **THEN** the response reports the terminal failure (`OperationFailed` or `UnexpectedTermination`)

#### Scenario: No MachineFG is declared

- **WHEN** the State Manager queries the service and the suite declares no `MachineFG`
- **THEN** the response reports `InvalidArgument`

### Requirement: Initial machine result conversion set

`InitialMachineStateTransitionResultResponse` SHALL provide a cold-path
`to_string()` member plus free helper, ADL `to_json`/`from_json` under
`JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Result response round-trips

- **WHEN** every `StateResult` value is placed in the response and passed through `to_string`, `std::format`, `to_json`, and `from_json`
- **THEN** the text and JSON name the same result and parsing restores it

#### Scenario: Hostile JSON leaves the response unchanged

- **WHEN** `from_json` receives an unknown result name or a non-object value
- **THEN** the destination response keeps its previous value
