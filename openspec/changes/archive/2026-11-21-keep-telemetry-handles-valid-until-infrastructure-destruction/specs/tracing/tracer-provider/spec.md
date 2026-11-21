# Spec Delta

## ADDED Requirements

### Requirement: Explicit provider quiescence

`TracerProvider::quiesce() noexcept` SHALL return `core::Status`, drain accepted spans through the flush barrier, stop and join the consumer, close the sink, seal the provider, and publish the liveness flag while keeping pool and tracer storage alive. It SHALL be idempotent, allocate nothing, and throw nothing.

#### Scenario: Accepted spans are delivered

- **WHEN** spans are ended and `quiesce()` is called
- **THEN** every span accepted before the call is delivered to the sink and the sink is closed before the call returns

#### Scenario: Handles stay memory-safe while span starts close

- **WHEN** a tracer obtained before `quiesce()` starts a span afterwards
- **THEN** the call returns an inactive guard with `ErrorCode::NotInitialized` and no memory error or sanitizer diagnostic occurs

#### Scenario: Repeated quiescence is harmless

- **WHEN** `quiesce()` is called twice
- **THEN** the second call returns success, the sink is not contacted again, and no sanitizer reports a problem

#### Scenario: Destruction does not drain twice

- **WHEN** a quiesced provider is destroyed
- **THEN** the destructor skips the drain and sink control calls, and storage is released without allocating

#### Scenario: Spans already in flight

- **WHEN** a span started before `quiesce()` ends after it
- **THEN** the record is enqueued or dropped by the existing ring policy without a memory error, and it is never delivered

#### Scenario: Diagnostics remain available

- **WHEN** `to_string()`, the worker report, or the drop snapshot is read after `quiesce()`
- **THEN** each returns its normal content and no memory error occurs

## MODIFIED Requirements

### Requirement: Provider and tracer factory hierarchy

`TracerProvider` SHALL be the only source of `Tracer` instances: `get_tracer(scope_name)` returns an existing tracer for a repeated scope name and a distinct tracer for a new name; tracers are created from a pre-allocated pool sized by configuration, with pool exhaustion returning `BufferFull`; tracer and span construction outside the factory hierarchy SHALL fail at compile time; handles stay memory-safe until provider destruction, and once the provider is quiesced both `get_tracer` and span starts report `NotInitialized`.

#### Scenario: Repeated scope names return the same tracer

- **WHEN** `get_tracer("engine")` is called twice on one provider
- **THEN** both calls return the same tracer, whose diagnostic `to_string()` renders the scope name

#### Scenario: Distinct scope names return distinct tracers

- **WHEN** `get_tracer` is called with two different names
- **THEN** two distinct tracers are returned, each rendering its own scope name, and spans start through both concurrently

#### Scenario: Pool exhaustion is surfaced

- **WHEN** more distinct scope names are requested than the configured tracer pool holds
- **THEN** the excess request returns `BufferFull` and previously created tracers remain usable

#### Scenario: Factory-only construction is pinned at compile time

- **WHEN** a translation unit attempts to construct a `Tracer` or a `ScopedSpan` directly
- **THEN** compilation fails — only the provider hierarchy can create them

#### Scenario: Quiesced hierarchy is closed

- **WHEN** `get_tracer()` is called on a quiesced provider
- **THEN** it returns `ErrorCode::NotInitialized` and no tracer slot is claimed

### Requirement: Shutdown drain and flush barrier

`TracerProvider` destruction SHALL stop the worker, drain every accepted record to the sink, and leave no record accepted before destruction undelivered; a quiesced provider SHALL NOT drain or touch its sink again. A `flush()` control-plane barrier SHALL return only after every span accepted before the call is sink-durable. Operations on handles after provider destruction or after quiescence SHALL fail with `NotInitialized` rather than corrupt state.

#### Scenario: Destruction does not lose accepted records

- **WHEN** producers end spans and the provider is destroyed immediately after
- **THEN** every span accepted before destruction is delivered to the sink before the destructor returns

#### Scenario: Flush waits for durability

- **WHEN** spans are ended and `flush()` is called
- **THEN** the call returns only after those spans have drained and the sink's flush has completed, surfacing sink errors

#### Scenario: Dead handles report NotInitialized

- **WHEN** a tracer handle outlives its provider and starts a span
- **THEN** the operation fails with `NotInitialized` and no memory error or sanitizer diagnostic occurs

#### Scenario: Quiesced provider is not drained again

- **WHEN** a provider that has already been quiesced is destroyed
- **THEN** the destructor performs no drain, no sink flush, and no sink stop, and no duplicate record reaches the sink
