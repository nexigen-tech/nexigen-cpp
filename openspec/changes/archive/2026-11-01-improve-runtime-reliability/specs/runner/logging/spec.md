# Spec Delta

## ADDED Requirements

### Requirement: State write failures are reported

Every runner JSON state write SHALL report a failure through the runner warning
channel before returning the error. When runner logging is active the warning
SHALL be a warning-level record in `runner.jsonl`; otherwise it SHALL be one
`nexigen-run:` line on standard error.

#### Scenario: Failed suite state write is recorded

- **WHEN** the suite supervisor cannot write `suite-state.json`
- **THEN** `runner.jsonl` contains a warning that names the failed write

#### Scenario: Failed suite exit write is recorded

- **WHEN** the suite supervisor cannot write `suite-exit.json`
- **THEN** `runner.jsonl` contains a warning that names the failed write

#### Scenario: Failed instance exit write is recorded

- **WHEN** a supervisor cannot write `exit.json`
- **THEN** the failure is reported through the runner warning channel

#### Scenario: Successful writes stay quiet

- **WHEN** a state write succeeds
- **THEN** no warning is emitted for that write
