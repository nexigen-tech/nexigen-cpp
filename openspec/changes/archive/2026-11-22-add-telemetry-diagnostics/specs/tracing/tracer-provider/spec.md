# Spec Delta

## ADDED Requirements

### Requirement: Provider diagnostics binding

`TracerProvider` SHALL expose `bind_diagnostics(DiagnosticBinding)` with a templated logger convenience and `delivery_status() const noexcept -> core::Status` reporting the last consumer delivery and sink-flush result. The status SHALL be readable without draining, flushing, or contacting the sink.

#### Scenario: A bound callback receives provider events

- **WHEN** a provider with a bound callback reports a transition
- **THEN** the callback receives exactly one event and the provider retains no ownership of the logger

#### Scenario: Unbound provider stays silent

- **WHEN** no binding is installed and delivery fails
- **THEN** no callback is invoked and `delivery_status()` still exposes the failure

#### Scenario: Status is pollable without I/O

- **WHEN** `delivery_status()` is read between spans
- **THEN** it returns the last result without draining, flushing, or contacting the sink

### Requirement: Provider diagnostic transitions

A bound provider SHALL emit `Started` once after the consumer starts, `Degraded` on the first failing transition, `Recovered` when delivery succeeds again, and `Quiesced` after sealing — at most one event per transition. Emission SHALL be cold-path only, SHALL allocate nothing, and SHALL stop after quiescence.

#### Scenario: Start is reported once

- **WHEN** a bound provider starts its consumer
- **THEN** exactly one `Started` event is delivered

#### Scenario: A failing delivery degrades once

- **WHEN** sink delivery or the flush barrier fails repeatedly
- **THEN** exactly one `Degraded` event carrying the first error is delivered, and later identical failures deliver nothing

#### Scenario: Recovery is reported

- **WHEN** delivery succeeds after a reported failure
- **THEN** exactly one `Recovered` event is delivered and the next failure reports again

#### Scenario: Quiescence is reported last

- **WHEN** `quiesce()` completes and the provider is sealed
- **THEN** exactly one `Quiesced` event is delivered and no later event is emitted
