# sm/manifest Specification

## Purpose

Defines the declarative state-management manifest: the YAML/JSON configuration that lists the Function Groups, their states, allowed transitions, timeouts, requester priorities, and the metrics flush interval.

## Requirements

### Requirement: Declarative state-management manifest

The SM SHALL read its arbitration logic from a YAML/JSON manifest without recompilation: the `function_groups` with their `states` and `allowed_transitions` (`from -> to`), `transition_max_time_ms`, `preparation_timeout_ms`, `requester_priorities`, and `metrics_flush_interval_ms`.

#### Scenario: Manifest configures the arbitration

- **WHEN** a manifest lists a Function Group with its states, transitions, and timeouts
- **THEN** the SM arbitrates exactly according to those rules

### Requirement: Manifest serialization

The manifest SHALL be parseable from and serializable to JSON without throwing under `JSON_NOEXCEPTION=1`, and a hostile or malformed input SHALL leave the parsed configuration unchanged.

#### Scenario: Manifest round-trips

- **WHEN** a manifest with one Function Group and its rules is serialized and parsed back
- **THEN** the parsed manifest equals the original, and a malformed JSON input leaves the target unchanged

### Requirement: Manifest diagnostics

The manifest SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization listing the Function Groups.

#### Scenario: Manifest renders its groups

- **WHEN** `to_string()` and `std::format("{}", manifest)` are evaluated
- **THEN** the text names the Function Groups and the formatter output equals the member output

### Requirement: SM manifest carries background thread specs

`SmManifest` SHALL carry a `bg_thread` spec and an `audit_thread` spec as `core::ThreadSpec` fields. The transition-audit worker SHALL apply `effective_thread_spec(audit_thread, bg_thread)` on its own thread. Empty specs SHALL preserve inherited attributes. Both fields SHALL participate in the manifest JSON round-trip and diagnostics.

#### Scenario: Empty specs preserve inherited attributes

- **WHEN** the manifest omits both fields
- **THEN** the audit thread applies no attributes and keeps its inherited placement

#### Scenario: Audit spec overrides the background fallback

- **WHEN** `audit_thread` names one CPU and `bg_thread` names another
- **THEN** the audit thread runs on the audit spec's CPU

#### Scenario: Empty audit spec inherits the background fallback

- **WHEN** `audit_thread` is empty and `bg_thread` names a CPU
- **THEN** the audit thread runs on the background CPU

#### Scenario: Thread specs round-trip through JSON

- **WHEN** a manifest with both specs is serialized and parsed back
- **THEN** every thread-spec field equals the original value

### Requirement: SM manifest carries lightweight state machines

`SmManifest` SHALL accept an optional `state_machines` array of
`LightweightStateMachineConfig` values. The field SHALL default to empty,
round-trip through `to_json`/`from_json`, and leave every manifest without it
valid and behaviorally unchanged.

#### Scenario: State machines round-trip

- **WHEN** a manifest with one state machine is serialized and parsed back
- **THEN** its states, actions, transitions, and recovery rules are preserved

#### Scenario: Existing manifest remains valid

- **WHEN** a manifest omits `state_machines`
- **THEN** validation succeeds and the SM keeps its existing behavior

### Requirement: LSM manifest validation

`SmManifest::validate` SHALL reject an LSM binding to an unknown Function Group,
a duplicate binding for one group, and an LSM state that the group does not
declare. Manifests without `state_machines` SHALL remain valid.

#### Scenario: Unknown group is rejected

- **WHEN** an LSM names a Function Group absent from `function_groups`
- **THEN** validation fails

#### Scenario: Existing manifest remains valid

- **WHEN** a manifest omits `state_machines`
- **THEN** validation succeeds
