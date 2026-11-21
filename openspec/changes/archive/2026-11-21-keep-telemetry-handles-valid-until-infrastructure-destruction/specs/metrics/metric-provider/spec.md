# Spec Delta

## ADDED Requirements

### Requirement: Explicit provider quiescence

`MetricProvider::quiesce() noexcept` SHALL return `core::Status`, stop and join the harvester, perform one final collection, flush the bound sink, and seal the provider while keeping registry storage alive. It SHALL be idempotent, allocate nothing, and throw nothing.

#### Scenario: Final collection is delivered

- **WHEN** instruments hold pending values and `quiesce()` is called
- **THEN** one final batch carrying those values is delivered to the sink and flushed before the call returns

#### Scenario: Storage stays alive and creation closes

- **WHEN** `quiesce()` returns and an already-issued meter or instrument is used
- **THEN** the handle is memory-safe, `get_meter()` returns `NotInitialized`, and no new instrument is registered

#### Scenario: Repeated quiescence is harmless

- **WHEN** `quiesce()` is called twice
- **THEN** the second call returns success, the sink is not contacted again, and no sanitizer reports a problem

#### Scenario: Destruction does not drain twice

- **WHEN** a quiesced provider is destroyed
- **THEN** no collection or sink flush occurs during destruction, and storage is released without allocating

#### Scenario: Sink failure still seals the provider

- **WHEN** the bound sink reports a flush failure during `quiesce()`
- **THEN** the call returns that failure, the provider is still sealed, and destruction performs no second flush

#### Scenario: Diagnostics remain available

- **WHEN** `to_string()`, `startup_status()`, or the worker report is read after `quiesce()`
- **THEN** each returns its normal content and no memory error occurs

## MODIFIED Requirements

### Requirement: Provider control plane

The provider SHALL expose `collect_now() noexcept -> core::Status`, `flush() noexcept -> core::Status`, and `startup_status() const noexcept -> core::Status`. Failures from harvesting or the bound sink SHALL propagate through these returns, and the provider SHALL remain usable after a failed collection. After quiescence, `collect_now()` and `flush()` SHALL return `NotInitialized` while `startup_status()` and read-only diagnostics remain available.

#### Scenario: Flush failure is propagated

- **WHEN** the bound sink reports an I/O failure from `flush()`
- **THEN** `MetricProvider::flush()` returns that failure as a `core::Status` error arm

#### Scenario: Collection errors do not crash

- **WHEN** the sink rejects a batch or an internal harvest operation fails
- **THEN** `collect_now()` reports the failure through its `core::Status` and the provider remains usable

#### Scenario: Startup status reports success by default

- **WHEN** the provider starts with an empty worker CPU set and a healthy sink
- **THEN** `startup_status()` returns success

#### Scenario: Control plane closes after quiescence

- **WHEN** `collect_now()` or `flush()` is called on a quiesced provider
- **THEN** each returns `ErrorCode::NotInitialized` and no sink I/O occurs

### Requirement: Provider shutdown

Destruction SHALL release the pre-allocated registry storage without allocating or throwing. A provider that has not been quiesced SHALL stop and join the harvester, deliver one final collection of pending values, and flush the bound sink before releasing storage; a quiesced provider SHALL NOT collect or flush again. Instrument and meter handles SHALL NOT outlive their provider.

#### Scenario: Clean shutdown drains the sink

- **WHEN** a provider with pending writes is destroyed
- **THEN** the harvester stops, a final batch carrying the pending values is delivered, `flush()` reaches the sink, and no sanitizer reports a race or leak

#### Scenario: Handles do not outlive the provider

- **WHEN** a `Meter` or instrument is used after its provider is destroyed
- **THEN** the behavior is a documented lifetime violation, and debug/test builds detect it through a provider liveness flag

#### Scenario: Quiesced provider is not drained again

- **WHEN** a provider that has already been quiesced is destroyed
- **THEN** the destructor skips the final collection and sink flush, and no duplicate batch reaches the sink
