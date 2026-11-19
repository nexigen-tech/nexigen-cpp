# Spec Delta

## MODIFIED Requirements

### Requirement: Provider construction and sink binding

A `Provider` SHALL be constructed from a `ProviderConfig` with a bound `concepts::Sink` sink type-erased through plain function pointers (no virtual dispatch, no RTTI), fixed pre-allocated storage, and no background thread, and construction SHALL NOT start any thread. Construction SHALL NOT deliver any snapshot and the `snapshot_now()` and UTC authority methods SHALL be usable immediately after construction. The bound sink MUST outlive the provider. The platform SHALL construct exactly one provider, inside the time-base daemon; application processes SHALL obtain time by attaching to the published time base instead of constructing a provider.

#### Scenario: Construction succeeds with a valid config and sink

- **WHEN** a provider is constructed with a valid `ProviderConfig` and a healthy sink
- **THEN** construction succeeds and no thread is created

#### Scenario: Construction delivers nothing

- **WHEN** a provider is constructed and observed before any trigger
- **THEN** the sink has received no snapshot

#### Scenario: Authority is usable immediately after construction

- **WHEN** `snapshot_now()` and `utc_offset()` are called right after construction
- **THEN** the offsets are sampled and exactly one snapshot is delivered to the bound sink

#### Scenario: Only the daemon owns a provider

- **WHEN** the platform is running with the time-base daemon active
- **THEN** exactly one provider instance exists, in the daemon process, and application processes hold no provider of their own
