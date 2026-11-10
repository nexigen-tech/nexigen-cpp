# Spec Delta

## Purpose

Defines the declarative health-logic manifest: a YAML/JSON configuration that the daemon reads at startup to learn the supervised processes, checkpoint graphs, deadline pairs, and recovery rules.

## ADDED Requirements

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
