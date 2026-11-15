# Spec Delta

## ADDED Requirements

### Requirement: Alive count configuration

Each supervised process entry SHALL accept optional `alive_expected_indications`, `alive_min_margin_cycles`, and `alive_max_margin_cycles`. An entry without `alive_expected_indications` or with a zero value SHALL keep the legacy at-least-one behaviour. Negative values SHALL be rejected when the manifest is loaded.

#### Scenario: Count fields are parsed

- **WHEN** a manifest entry declares expected indications and both count margins
- **THEN** the daemon uses those counts for that process and renders them in diagnostics

#### Scenario: Counts default to legacy behaviour

- **WHEN** a manifest entry omits the count fields
- **THEN** the process supervises at-least-one-checkpoint-per-window as before

#### Scenario: Negative count is rejected

- **WHEN** a manifest declares a negative expected count or margin
- **THEN** the daemon refuses the manifest and starts no supervision

### Requirement: Fork/join configuration

A supervised process entry SHALL accept an optional `logical_forks` array whose items declare `fork_id`, `branch_entries`, and `join_id`. Every branch entry and join identifier SHALL also appear in the process `checkpoints`, fork identifiers SHALL be unique within a process, and a fork SHALL declare at least one branch. A violation SHALL be rejected when the manifest is loaded.

#### Scenario: Fork region is parsed

- **WHEN** a process declares a fork with two branch entries and a join
- **THEN** logical supervision tracks both branches for that process

#### Scenario: Unknown checkpoint in a fork is rejected

- **WHEN** a fork names a branch entry or join that is absent from `checkpoints`
- **THEN** the daemon refuses the manifest

#### Scenario: Fork without branches is rejected

- **WHEN** a fork declares an empty `branch_entries` array
- **THEN** the daemon refuses the manifest

### Requirement: Watchdog configuration

The manifest SHALL accept an optional `watchdog` object with `enabled`, `device`, `timeout_ms`, and `feed_period_ms`. The watchdog SHALL default to disabled, an empty `device` SHALL mean `/dev/watchdog`, and `feed_period_ms` SHALL be positive and at most half of `timeout_ms`. Invalid values SHALL be rejected when the manifest is loaded.

#### Scenario: Watchdog section is parsed

- **WHEN** a manifest enables the watchdog with a device, timeout, and feed period
- **THEN** the daemon reports those values in its startup diagnostics

#### Scenario: Disabled by default

- **WHEN** a manifest omits the watchdog section
- **THEN** the daemon opens no device and supervision behaves as before

#### Scenario: Feed period longer than half the timeout is rejected

- **WHEN** `feed_period_ms` exceeds half of `timeout_ms`
- **THEN** the daemon refuses the manifest
