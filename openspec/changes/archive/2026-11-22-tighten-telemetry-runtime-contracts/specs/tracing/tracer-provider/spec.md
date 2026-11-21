# Spec Delta

## MODIFIED Requirements

### Requirement: Provider diagnostics binding

`TracerProvider` SHALL expose `bind_diagnostics(DiagnosticBinding) noexcept -> core::Status` with a templated logger convenience and `delivery_status() const noexcept -> core::Status` reporting the last consumer delivery and sink-flush result. The binding SHALL be installable only in configuration mode — before `initialize()` starts the consumer or while the provider is not running and not sealed; a bind attempt on a running or sealed provider SHALL return `ErrorCode::Conflict` and leave the previous binding unchanged. The status SHALL be readable without draining, flushing, or contacting the sink.

#### Scenario: Configuration binding succeeds

- **WHEN** a binding is installed before `initialize()`
- **THEN** it returns success and later transitions reach the callback

#### Scenario: Live rebinding is rejected

- **WHEN** `bind_diagnostics` is called while the consumer is running
- **THEN** it returns `ErrorCode::Conflict`, the previous binding stays installed, and the worker never observes a torn 16-byte binding

#### Scenario: Sealed provider rejects binding

- **WHEN** `bind_diagnostics` is called after quiescence
- **THEN** it returns `ErrorCode::Conflict` and no event is emitted

#### Scenario: A bound callback receives provider events

- **WHEN** a provider with a bound callback reports a transition
- **THEN** the callback receives exactly one event and the provider retains no ownership of the logger

#### Scenario: Unbound provider stays silent

- **WHEN** no binding is installed and delivery fails
- **THEN** no callback is invoked and `delivery_status()` still exposes the failure

#### Scenario: Status is pollable without I/O

- **WHEN** `delivery_status()` is read between spans
- **THEN** it returns the last result without draining, flushing, or contacting the sink
