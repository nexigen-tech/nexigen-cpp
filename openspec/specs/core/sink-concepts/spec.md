# core/sink-concepts Specification

## Purpose
Defines one sink delivery contract and one shared type-erasure binding for every module that pushes
records to a consumer, so chrono, metrics, tracing, and logging stop carrying private variants.

## Requirements

### Requirement: Sink delivery contract

`core::concepts::Sink<S, Event>` SHALL be satisfied by a type offering
`on_event(const Event&) noexcept -> core::Status` and `flush() noexcept -> core::Status`. It SHALL
NOT name any module type beyond the `Event` supplied by the caller.

#### Scenario: Conforming sink satisfies the concept

- **WHEN** `core::concepts::Sink<S, Event>` is evaluated for a sink implementing `on_event` and
  `flush`
- **THEN** the concept is true in a constant expression

#### Scenario: Wrong member name is rejected

- **WHEN** a type still exposes `on_snapshot`, `on_batch`, `on_span`, or `handle` instead of
  `on_event`
- **THEN** the concept is false and the module does not compile until the sink is renamed

#### Scenario: Throwing sink is rejected

- **WHEN** `on_event` or `flush` is not `noexcept` or does not return `core::Status`
- **THEN** the concept is false

### Requirement: Lifecycle sink contract

`core::concepts::LifecycleSink<S, Event>` SHALL extend the sink contract with
`start() noexcept -> core::Status` and `stop() noexcept`, for consumers that own setup and teardown.

#### Scenario: Lifecycle sink satisfies the stronger concept

- **WHEN** a type implements `start`, `on_event`, `flush`, and `stop`
- **THEN** both `core::concepts::Sink` and `core::concepts::LifecycleSink` are satisfied

#### Scenario: Stop has no status

- **WHEN** `stop()` returns a value or is not `noexcept`
- **THEN** the lifecycle concept is false

### Requirement: Shared sink binding

`core::SinkBinding<Event>` SHALL hold a borrowed sink pointer plus `on_event` and `flush` function
pointers, and optional `start` and `stop` pointers that may be null. A null `on_event` or `flush`
SHALL mean "no sink" and be treated as a successful no-op. The binding SHALL NOT allocate, own the
sink, or require a virtual table.

#### Scenario: Provider erases a conforming sink

- **WHEN** a provider is constructed from a sink satisfying the concept
- **THEN** it stores one `core::SinkBinding<Event>` and the sink type does not appear in the
  provider's type

#### Scenario: Null binding is a successful no-op

- **WHEN** a provider has no bound sink and delivers a record
- **THEN** the delivery reports success and no callback is invoked

#### Scenario: Sink lifetime is the caller's contract

- **WHEN** a binding is created from a sink reference
- **THEN** the binding states that the sink must outlive it and the provider never destroys it

#### Scenario: Binding costs no allocation

- **WHEN** a binding is constructed
- **THEN** it performs no heap allocation and holds only pointers

### Requirement: One sink method vocabulary

Every module's sink SHALL deliver through `on_event` and `flush`, and lifecycle sinks SHALL use
`start` and `stop`. The former names `on_snapshot`, `on_batch`, `on_span`, `handle`, and
`synchronize` SHALL NOT remain.

#### Scenario: Delivery names are uniform

- **WHEN** the chrono, metrics, tracing, and logging sinks are inspected
- **THEN** each delivers through `on_event` and flushes through `flush`

#### Scenario: Old names are gone

- **WHEN** a probe calls `on_snapshot`, `on_batch`, `on_span`, `handle`, or `synchronize`
- **THEN** it fails to compile against the updated sink
