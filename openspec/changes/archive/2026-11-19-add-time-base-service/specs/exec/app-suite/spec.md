# Spec Delta

## ADDED Requirements

### Requirement: Time-base manifest binding

The suite manifest SHALL accept an optional `time_base` object with `process` and `segment`. `process` SHALL name an application declared in the same manifest; an absent `process` SHALL disable the time-base gate. A `process` without `segment` SHALL default the segment to `/nexigen-timebase-<suite_id>`; `segment` without `process`, or a segment that is not a single-slash POSIX shared-memory name, SHALL be rejected.

#### Scenario: Binding names the daemon and the segment

- **WHEN** a manifest declares `time_base.process` naming one of its applications and a valid `segment`
- **THEN** validation accepts the suite and both values survive serialization unchanged

#### Scenario: Absent binding disables the gate

- **WHEN** the manifest omits `time_base`
- **THEN** validation accepts the suite and no time-base readiness check applies

#### Scenario: Segment defaults from the suite id

- **WHEN** `time_base.process` is present and `time_base.segment` is omitted
- **THEN** the effective segment is `/nexigen-timebase-<suite_id>`

#### Scenario: Unknown daemon process is rejected

- **WHEN** `time_base.process` names no application in the manifest
- **THEN** validation fails with `InvalidArgument` and no process starts

#### Scenario: Segment without a process is rejected

- **WHEN** the manifest supplies `time_base.segment` but no `time_base.process`
- **THEN** validation fails with `InvalidArgument`

#### Scenario: Malformed segment is rejected

- **WHEN** `time_base.segment` is empty, lacks the leading slash, contains another slash, or exceeds the POSIX name limit
- **THEN** validation fails with `InvalidArgument`
