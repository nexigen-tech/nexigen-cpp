# Spec Delta

## ADDED Requirements

### Requirement: LogDaemon main thread applies a configurable spec

`LogDaemonConfig` SHALL carry a `main_thread` spec. When it is empty, the daemon SHALL derive a spec from the existing `bg_cpu_affinity` CPU set; when non-empty, it SHALL use the explicit spec. The daemon SHALL apply the effective spec on its own main thread before opening the subscriber and drain loop. A `Required` application failure SHALL exit non-zero before any port is created. The field SHALL participate in the config JSON round-trip.

#### Scenario: Default derives affinity from the legacy field

- **WHEN** `main_thread` is empty and `bg_cpu_affinity` names a CPU
- **THEN** the daemon main thread is pinned to that CPU

#### Scenario: Explicit spec adds scheduler attributes

- **WHEN** `main_thread` requests a CPU, policy, and priority
- **THEN** the main thread applies all of them before the first drain

#### Scenario: Required failure stops startup

- **WHEN** a required attribute in the effective spec is rejected
- **THEN** the daemon exits non-zero before creating its subscriber

#### Scenario: Main spec round-trips through JSON

- **WHEN** a config with a non-empty `main_thread` is serialized and parsed back
- **THEN** the recovered spec equals the original
