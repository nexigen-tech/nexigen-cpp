# Spec Delta

## ADDED Requirements

### Requirement: Function Group configuration

The EM SHALL support Function Groups declared in its configuration, each with its own set of states; every application SHALL declare the groups whose states keep it active, and a group state switch SHALL start or stop exactly the processes bound to that group.

#### Scenario: Group switch stops its processes

- **WHEN** the group `ADAS` transitions to `Off`
- **THEN** every process bound to `ADAS` is stopped with the graceful shutdown sequence, and processes of other groups keep running

#### Scenario: Group switch starts its processes

- **WHEN** the group `ADAS` transitions from `Off` to `Running`
- **THEN** every process bound to `ADAS` is started

### Requirement: Function Group diagnostics

The EM's group registry SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization listing the groups and their current states.

#### Scenario: Groups render their states

- **WHEN** `to_string()` and `std::format("{}", groups)` are evaluated
- **THEN** the text names each group and its state, and the formatter output equals the member output
