# quality/coverage Specification

## Purpose
Defines how nexigen measures line coverage across process boundaries and what
minimum coverage the execution_manager and hub modules must keep.

## Requirements

### Requirement: Coverage includes forked supervisors

The coverage build SHALL flush instrumentation counters from forked execution_manager
supervisor and application-setup processes before they terminate or replace
their image, so work performed in those children contributes to line coverage.

#### Scenario: Suite supervisor contributes coverage

- **WHEN** the coverage build runs a suite test that starts a detached suite supervisor
- **THEN** lines executed by the supervisor before it exits are reported as covered

#### Scenario: Application setup contributes coverage

- **WHEN** the coverage build starts an instance whose child process is launched with `exec`
- **THEN** lines executed between `fork` and `exec` are reported as covered

#### Scenario: Non-coverage builds are unaffected

- **WHEN** the project is built without coverage instrumentation
- **THEN** the flush helper compiles to an empty inline call and forks behave exactly as before

### Requirement: ExecutionManager and hub coverage gates

The coverage report SHALL score execution_manager and hub module line coverage separately,
and both modules SHALL remain at or above 90%.

#### Scenario: ExecutionManager stays above the gate

- **WHEN** the coverage target runs after the full unit-test suite
- **THEN** the execution_manager module reports at least 90.0% line coverage

#### Scenario: Hub stays above the gate

- **WHEN** the coverage target runs after the full unit-test suite
- **THEN** the hub module reports at least 90.0% line coverage

#### Scenario: Coverage gate fails a regression

- **WHEN** module coverage falls below the configured gate
- **THEN** the coverage target exits non-zero
