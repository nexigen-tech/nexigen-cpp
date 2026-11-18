# Spec Delta

## Purpose

Defines the shared wire payload, channel configuration, deterministic phase
schedule, start and late-event policies, state vocabularies, statistics, and
error model used by every synchronization endpoint.

## ADDED Requirements

### Requirement: Synchronization payload and channel configuration

`SyncPayload` SHALL be a 32-byte trivially copyable value carrying the cycle
number, phase index, total phases, absolute scheduled time, and cycle period.
`SyncChannelConfig` SHALL bind the service name, channel limits, cycle period,
and phases per cycle, and SHALL reject a configuration that cannot produce a
valid schedule.

#### Scenario: Payload layout is pinned

- **WHEN** the synchronization headers are compiled
- **THEN** `SyncPayload` is exactly 32 bytes, 8-byte aligned, and trivially copyable

#### Scenario: Payload carries the full schedule identity

- **WHEN** a phase event is constructed for cycle 7, phase 3 of 10
- **THEN** the payload carries cycle 7, phase 3, 10 phases, the absolute scheduled time, and the cycle period

#### Scenario: Payload diagnostics serialize

- **WHEN** `to_string()`, `std::format("{}")`, and JSON are evaluated for a `SyncPayload`
- **THEN** the diagnostic is deterministic, the formatter matches `to_string()`, and JSON round-trips every field

#### Scenario: Channel configuration is validated

- **WHEN** `SyncChannelConfig` is created with a zero period, zero phases, a phase shorter than 1 ms, or an invalid channel limit
- **THEN** creation returns `SyncError` with kind `InvalidConfig` before any service is opened

#### Scenario: Channel configuration serializes

- **WHEN** `SyncChannelConfig` is rendered and round-tripped through JSON
- **THEN** the formatter matches `to_string()`, every field keeps its value, and hostile JSON leaves the destination unchanged

### Requirement: Deterministic phase schedule

The schedule SHALL be computed from one absolute start instant and integer
nanosecond arithmetic: `T_scheduled(c, p) = T_start + c * Tc + floor(p * Tc / N)`.
No phase deadline SHALL be derived from the previous wake-up, so missed phases
do not introduce cumulative drift.

#### Scenario: Auto-aligned start is strictly in the future

- **WHEN** the start policy selects the next cycle and the current raw time is between two cycle boundaries
- **THEN** `T_start` is the first cycle boundary strictly after the current time

#### Scenario: Phase deadlines never accumulate rounding drift

- **WHEN** a cycle is divided into phases whose duration is not an integer number of nanoseconds
- **THEN** every deadline is derived from `T_start` and the absolute cycle and phase indices

#### Scenario: Cycle numbering starts at zero

- **WHEN** a generator starts a new lifecycle after `on_source_reset()`
- **THEN** its first payload carries cycle number zero and the header carries a new producer epoch

#### Scenario: Minimum phase period is enforced

- **WHEN** `cycle_period_ns / phases_per_cycle` is below 1 ms
- **THEN** configuration validation fails with `InvalidConfig`

### Requirement: Start policy

`StartPolicy` SHALL select either the next cycle boundary or a caller-supplied
target. An `AtTarget` policy SHALL optionally align the target to the cycle
grid, and SHALL either reject a target in the past or realign it to the next
cycle.

#### Scenario: Next-cycle start ignores a target

- **WHEN** the policy mode is `NextCycle`
- **THEN** the target is ignored and the start is the next cycle boundary

#### Scenario: Future target is aligned to the grid

- **WHEN** the policy mode is `AtTarget`, the target is in the future, and target alignment is enabled
- **THEN** `T_start` is the first cycle boundary at or after the target

#### Scenario: Future target is used exactly

- **WHEN** the policy mode is `AtTarget`, the target is in the future, and target alignment is disabled
- **THEN** `T_start` equals the target

#### Scenario: Past target is rejected

- **WHEN** the target is not in the future and the past-target policy is `Reject`
- **THEN** start fails with `InvalidConfig`

#### Scenario: Past target is realigned

- **WHEN** the target is not in the future and the past-target policy is `RealignToNextCycle`
- **THEN** the start is moved to the next cycle boundary and a warning is logged

#### Scenario: Start policy serializes

- **WHEN** `StartMode`, `PastTargetPolicy`, and `StartPolicy` round-trip through their conversion helpers and JSON
- **THEN** every canonical name and field value is preserved and hostile JSON leaves the destination unchanged

### Requirement: Late-event policy

`LateEventPolicy` SHALL define whether a consumer drops a late event with a
warning or delivers it to application handlers with an `is_late` flag. A late
event SHALL still update continuity and telemetry state.

#### Scenario: Drop-and-warn policy

- **WHEN** an event exceeds the allowed latency and the policy is `DropAndWarn`
- **THEN** the event is not delivered, the dropped-late counter increases, and a warning is logged

#### Scenario: Deliver-with-flag policy

- **WHEN** an event exceeds the allowed latency and the policy is `DeliverWithFlag`
- **THEN** handlers receive the event with `is_late = true` and the late counter increases

#### Scenario: Late-event policy serializes

- **WHEN** `LateEventPolicy` passes through `to_string`, `from_string`, JSON, and `std::format`
- **THEN** every canonical name round-trips and hostile JSON leaves the destination unchanged

### Requirement: State vocabularies and endpoint statistics

`SyncState` SHALL describe the generator lifecycle and `SyncConsumerState`
SHALL describe synchronization lock state. `SyncGeneratorStats` and
`SyncConsumerStats` SHALL expose the counters and timing snapshots required by
the generator and consumer capabilities.

#### Scenario: Generator states are exhaustive

- **WHEN** a generator transitions through stop, wait, run, and fault
- **THEN** `SyncState` reports `Stopped`, `WaitingForStart`, `Running`, or `Faulted` respectively

#### Scenario: Consumer lock states are exhaustive

- **WHEN** a consumer acquires or loses synchronization
- **THEN** `SyncConsumerState` reports `Locked` or `Unsynced` respectively

#### Scenario: State vocabularies serialize

- **WHEN** `SyncState` and `SyncConsumerState` pass through `to_string`, `from_string`, JSON, and `std::format`
- **THEN** every canonical name round-trips and hostile JSON leaves the destination unchanged

#### Scenario: Statistics serialize

- **WHEN** `SyncGeneratorStats` and `SyncConsumerStats` are rendered and round-tripped through JSON
- **THEN** the formatter matches `to_string()`, every counter keeps its value, and hostile JSON leaves the destination unchanged

### Requirement: Synchronization error model

`SyncErrorKind` SHALL classify initialization and lifecycle failures.
`SyncError` SHALL carry the kind, an optional `com::Error`, an optional
`core::ErrorCode`, and an optional OS error number, so callers can distinguish
transport, metrics, clock, eventfd, thread, and configuration failures.

#### Scenario: Transport failure preserves the transport cause

- **WHEN** channel or endpoint creation fails
- **THEN** `SyncError.kind` is `Transport` and `SyncError.transport` carries the original `com::Error`

#### Scenario: Metrics failure preserves the core cause

- **WHEN** a metric instrument cannot be registered
- **THEN** `SyncError.kind` is `Metrics` and `SyncError.core` carries the original `core::ErrorCode`

#### Scenario: OS failure carries errno

- **WHEN** `clock_gettime`, `eventfd`, or a thread operation fails
- **THEN** `SyncError.kind` names the failing area and `SyncError.os_errno` carries the OS error number

#### Scenario: Error model serializes

- **WHEN** `SyncErrorKind` and `SyncError` pass through `to_string`, `from_string`, JSON, and `std::format`
- **THEN** every canonical name and populated cause field round-trips and hostile JSON leaves the destination unchanged
