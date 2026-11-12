# phm/manifest Specification

## Purpose

Defines the declarative health-logic manifest: a YAML/JSON configuration that the daemon reads at startup to learn the supervised processes, checkpoint graphs, deadline pairs, and recovery rules.

## Requirements

### Requirement: Declarative health manifest

The daemon SHALL read its monitoring and recovery configuration from a YAML/JSON manifest at startup; the manifest SHALL list the supervised processes and their iceoryx2 service identifiers, the Alive supervision parameters, the Logical supervision transition graphs, the Deadline supervision pairs with `min`/`max` limits, and the Recovery rules (`RestartProcess`, `SwitchFunctionGroupState`, `TriggerWatchdogReset`).

#### Scenario: Manifest configures supervision

- **WHEN** a manifest names a process with its checkpoints, graph, deadlines, and recovery rules
- **THEN** the daemon supervises exactly that process according to those rules

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
