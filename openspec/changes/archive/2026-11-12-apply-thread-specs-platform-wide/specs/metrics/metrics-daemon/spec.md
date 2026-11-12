# Spec Delta

## ADDED Requirements

### Requirement: MetricsDaemon HTTP thread is configurable

`MetricDaemonConfig` SHALL carry an `http_thread` spec. When it is empty, the HTTP serving thread SHALL inherit `bg_cpu_affinity` through `effective_thread_spec`; when non-empty, it SHALL use the explicit spec. The HTTP front end SHALL serve on a `WorkerThread` and SHALL stop and join it during daemon shutdown before closing the listener. The field SHALL participate in the config JSON round-trip.

#### Scenario: Empty HTTP spec inherits the background spec

- **WHEN** `http_thread` is empty and `bg_cpu_affinity` names a CPU
- **THEN** the HTTP thread runs on that CPU

#### Scenario: Explicit HTTP spec wins

- **WHEN** `http_thread` names a different CPU from `bg_cpu_affinity`
- **THEN** the HTTP thread runs on the explicit CPU

#### Scenario: Shutdown joins the HTTP thread

- **WHEN** the daemon stops
- **THEN** the HTTP worker observes the stop request, joins, and the listener closes afterwards

#### Scenario: HTTP spec round-trips through JSON

- **WHEN** a config with a non-empty `http_thread` is serialized and parsed back
- **THEN** the recovered spec equals the original
