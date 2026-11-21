# Spec Delta

## MODIFIED Requirements

### Requirement: Provider diagnostics binding

`MetricProvider` SHALL expose `bind_diagnostics(DiagnosticBinding) noexcept -> core::Status` with a templated logger convenience and `harvest_status() const noexcept -> core::Status` reporting the last collection and sink-flush result. The binding SHALL be installable only in configuration mode — while the harvester is not running and the provider is not sealed; a bind attempt on a running or sealed provider SHALL return `ErrorCode::Conflict` and leave the previous binding unchanged. The status SHALL be readable without collecting, flushing, or contacting the sink.

#### Scenario: Configuration binding succeeds

- **WHEN** a binding is installed before the harvester starts (or on a worker-less provider)
- **THEN** it returns success and later transitions reach the callback

#### Scenario: Live rebinding is rejected

- **WHEN** `bind_diagnostics` is called while the harvester is running
- **THEN** it returns `ErrorCode::Conflict`, the previous binding stays installed, and the worker never observes a torn 16-byte binding

#### Scenario: Sealed provider rejects binding

- **WHEN** `bind_diagnostics` is called after quiescence
- **THEN** it returns `ErrorCode::Conflict` and no event is emitted

#### Scenario: A bound callback receives provider events

- **WHEN** a provider with a bound callback reports a transition
- **THEN** the callback receives exactly one event and the provider retains no ownership of the logger

#### Scenario: Unbound provider stays silent

- **WHEN** no binding is installed and collections fail
- **THEN** no callback is invoked and `harvest_status()` still exposes the failure

#### Scenario: Status is pollable without I/O

- **WHEN** `harvest_status()` is read between collections
- **THEN** it returns the last result without collecting, flushing, or contacting the sink
