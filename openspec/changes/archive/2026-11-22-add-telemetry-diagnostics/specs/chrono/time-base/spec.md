# Spec Delta

## ADDED Requirements

### Requirement: Time-base diagnostics binding

`TimeBaseClient` SHALL expose `bind_diagnostics(DiagnosticBinding)` and a templated convenience that accepts any `DiagnosticLogger`. The client SHALL report at most one event per state transition: `Started` when a healthy client is bound, `Degraded` on entering `Holdover` or `Lost`, and `Recovered` on returning to `Attached`. It SHALL NOT report from conversion paths and SHALL allocate nothing.

#### Scenario: Binding a healthy client reports Started

- **WHEN** a diagnostic binding is installed on an attached, healthy client
- **THEN** exactly one `Started` event is delivered and no conversion call is needed to produce it

#### Scenario: Holdover degrades once

- **WHEN** `on_tick()` observes the base entering `Holdover`
- **THEN** exactly one `Degraded` event carrying the applicable error is delivered, and further ticks in `Holdover` deliver nothing

#### Scenario: Recovery is reported once

- **WHEN** the base returns to `Attached` after `Holdover`
- **THEN** exactly one `Recovered` event is delivered

#### Scenario: Loss degrades once

- **WHEN** `on_tick()` observes `Lost`
- **THEN** exactly one `Degraded` event is delivered and the client stops reporting further events

#### Scenario: Unbound client stays silent

- **WHEN** no binding is installed and the state transitions through `Holdover` and back
- **THEN** no callback is invoked and the client behaves exactly as before

#### Scenario: Conversion never reports

- **WHEN** `utc_from_canonical()` or any domain conversion runs on a bound client
- **THEN** no diagnostic callback is invoked

## MODIFIED Requirements

### Requirement: Client attach and required domains

An application SHALL attach to the published block and SHALL declare which domains it requires. Attaching SHALL fail when no block exists or the status is not usable, and the client SHALL treat a daemon restart that preserves every required epoch as transparent and SHALL refresh the cached rows on generation change. `attach` SHALL accept an optional diagnostic binding used only to report an attach failure as one `Degraded` event before returning; a missing binding is a no-op.

#### Scenario: Attach requires a published base

- **WHEN** an application attaches and no time-base block exists
- **THEN** attach fails with a not-available status and the application does not start

#### Scenario: Restart with unchanged epochs is transparent

- **WHEN** the daemon restarts and every required domain keeps its epoch
- **THEN** the client re-reads the new generation and continues without failing

#### Scenario: Epoch change fails fast

- **WHEN** a required domain's epoch changes
- **THEN** the client reports the base as lost instead of applying the old map

#### Scenario: Attach failure reaches the bootstrap channel

- **WHEN** `attach` is called with a diagnostic binding and no usable block exists
- **THEN** one `Degraded` event carrying the failure code is delivered before `attach` returns its error

#### Scenario: Silent attach remains available

- **WHEN** `attach` is called without a diagnostic binding
- **THEN** it behaves as before and invokes no callback
