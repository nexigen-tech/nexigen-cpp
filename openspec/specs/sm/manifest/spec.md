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
