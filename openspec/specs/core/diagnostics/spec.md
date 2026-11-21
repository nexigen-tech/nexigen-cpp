# core/diagnostics Specification

## Purpose

Defines the shared cold-path diagnostics channel that telemetry subsystems use to report lifecycle and runtime state changes to an application-provided logger without depending on the logging module.

## Requirements

### Requirement: Diagnostic vocabulary and serialization

The system SHALL provide `DiagnosticSource` (`Chrono`, `Metrics`, `Tracing`), `DiagnosticKind` (`Started`, `Degraded`, `Recovered`, `Quiesced`), and `DiagnosticEvent` carrying a source, a kind, and a `core::ErrorCode` (`Ok` for non-error transitions). Each enum SHALL implement `to_string`, `from_string`, `std::formatter`, and non-throwing `to_json`/`from_json`; the event SHALL implement `to_string`, `to_json`, and `from_json` under `JSON_NOEXCEPTION=1`.

#### Scenario: Enum names round-trip and format

- **WHEN** every enum value is rendered with `to_string`, parsed back with `from_string`, formatted with `std::format`, and serialized to JSON and back
- **THEN** each path returns the canonical lowercase name, parsing succeeds only for canonical names, and JSON round-trips preserve the value under `JSON_NOEXCEPTION=1`

#### Scenario: Event renders and round-trips

- **WHEN** a `DiagnosticEvent` carrying a source, kind, and error is rendered with `to_string` and with `std::format`, then serialized to JSON and back
- **THEN** both renderings agree and are non-empty, and the parsed event preserves source, kind, and error

#### Scenario: Unknown enum names are rejected

- **WHEN** `from_json` receives a non-string or an unknown string value
- **THEN** the destination keeps its previous value and no exception escapes

### Requirement: Diagnostic binding and logger concept

The system SHALL provide a type-erased `DiagnosticBinding` holding a borrowed `void*` and a `noexcept` event callback, where a null callback is a successful no-op. It SHALL provide the `DiagnosticLogger` concept requiring `diagnostic(const DiagnosticEvent&) noexcept`, and a stateless `NullDiagnosticLogger` that satisfies it. A binding SHALL never own the logger and SHALL allocate nothing.

#### Scenario: A bound callback receives the event

- **WHEN** an emitter reports an event through a binding whose callback is installed
- **THEN** the callback receives that exact event once and the emitter does not retain ownership

#### Scenario: An unbound binding is a no-op

- **WHEN** an emitter reports through a default-constructed binding
- **THEN** the call returns without touching any logger and without faulting

#### Scenario: The concept accepts and rejects

- **WHEN** `DiagnosticLogger<T>` is evaluated for `NullDiagnosticLogger` and for a type lacking `diagnostic`
- **THEN** it is `true` for the first and `false` for the second at compile time

#### Scenario: Null logger conforms and renders

- **WHEN** `diagnostic()` is called on `NullDiagnosticLogger` and its `to_string()` is rendered
- **THEN** the call is a no-op and the text names the null logger
