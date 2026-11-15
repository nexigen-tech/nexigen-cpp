# phm/manifest Specification

## Purpose

Defines the declarative health-logic manifest: a YAML/JSON configuration that the daemon reads at startup to learn the supervised processes, checkpoint graphs, deadline pairs, and recovery rules.

## Requirements

### Requirement: Declarative health manifest

The daemon SHALL load a JSON health manifest describing each supervised process, its checkpoint service identifier, Alive window and margins, Logical transition graph, Deadline pairs, failure tolerance, safety-critical flag, and recovery rules. The EM lifecycle channel SHALL be the standard `Nexigen/PHM/ExecutionState` service and SHALL NOT require a new per-process manifest field; a process's checkpoint service and name remain sufficient to bind its supervision.

#### Scenario: Manifest configures supervision

- **WHEN** a manifest names a process with its checkpoints, graph, deadlines, and recovery rules
- **THEN** the daemon supervises exactly that process according to those rules

#### Scenario: Process entry carries its supervision parameters

- **WHEN** a manifest entry declares a checkpoint service and Alive/Logical/Deadline parameters
- **THEN** the daemon loads them and supervises the process

#### Scenario: Lifecycle channel needs no new field

- **WHEN** a process entry has no lifecycle-specific field
- **THEN** the daemon still receives its EM lifecycle events on the standard channel

### Requirement: Manifest serialization

The manifest SHALL be parseable from and serializable to JSON without throwing under `JSON_NOEXCEPTION=1`, and a hostile or malformed input SHALL leave the parsed configuration unchanged.

#### Scenario: Manifest round-trips

- **WHEN** a manifest with one process and its rules is serialized and parsed back
- **THEN** the parsed manifest equals the original, and a malformed JSON input leaves the target unchanged

### Requirement: Manifest diagnostics

The manifest SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization listing the supervised processes.

#### Scenario: Manifest renders its processes

- **WHEN** `to_string()` and `std::format("{}", manifest)` are evaluated
- **THEN** the text names the supervised processes and the formatter output equals the member output

### Requirement: PHM manifest carries background thread specs

`PhmManifest` SHALL carry a `bg_thread` spec and a `fault_storage_thread` spec as `core::ThreadSpec` fields. The fault-storage worker SHALL apply `effective_thread_spec(fault_storage_thread, bg_thread)` on its own thread. Empty specs SHALL preserve inherited attributes. Both fields SHALL participate in the manifest JSON round-trip and diagnostics.

#### Scenario: Empty specs preserve inherited attributes

- **WHEN** the manifest omits both fields
- **THEN** the fault-storage thread applies no attributes and keeps its inherited placement

#### Scenario: Storage spec overrides the background fallback

- **WHEN** `fault_storage_thread` names one CPU and `bg_thread` names another
- **THEN** the fault-storage thread runs on the storage spec's CPU

#### Scenario: Empty storage spec inherits the background fallback

- **WHEN** `fault_storage_thread` is empty and `bg_thread` names a CPU
- **THEN** the fault-storage thread runs on the background CPU

#### Scenario: Thread specs round-trip through JSON

- **WHEN** a manifest with both specs is serialized and parsed back
- **THEN** every thread-spec field equals the original value

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

### Requirement: Expected process identity

A supervised process entry SHALL accept an optional `expected_uid`. A value of
`0` SHALL be rejected when the manifest is loaded. When the field is present,
the daemon SHALL require a verified claim before it accepts checkpoints from
that process.

#### Scenario: Expected UID round-trips

- **WHEN** an entry declares `expected_uid`
- **THEN** the manifest serializes and parses it unchanged

#### Scenario: UID zero is rejected

- **WHEN** an entry declares `expected_uid = 0`
- **THEN** manifest validation fails with `InvalidArgument`

#### Scenario: An absent field keeps the legacy path

- **WHEN** an entry omits `expected_uid`
- **THEN** the process is supervised without an identity claim

#### Scenario: A name that cannot fit a claim is rejected

- **WHEN** an entry declares `expected_uid` with an empty name or a name of 32
  bytes or more
- **THEN** manifest validation fails, because the published claim would always
  be truncated and the process could never verify
