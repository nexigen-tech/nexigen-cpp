# Spec Delta

## ADDED Requirements

### Requirement: Single-call publication of a caller-owned payload

`nexigen::com::Publisher<T, H>` SHALL provide `send_copy(const T&)` that publishes a caller-owned payload in one transport call, equivalent to loaning a slot, writing the payload once, and sending it. The call SHALL be non-blocking under the existing discard policy, SHALL allocate no heap memory, and SHALL update the same published and lost-sample telemetry as `send()`.

#### Scenario: Payload is delivered unchanged

- **WHEN** a publisher calls `send_copy()` with a payload and a subscriber is connected to the same service
- **THEN** the subscriber receives one sample whose bytes equal the payload exactly

#### Scenario: Lost-sample snapshot spans the call

- **WHEN** a subscriber's queue is full and `lost_samples()` is read before and after one `send_copy()` call
- **THEN** the second reading is greater, so the caller can count the evicted sample

#### Scenario: Publication is allocation-free

- **WHEN** an armed allocation counter observes `send_copy()` calls after the publisher was created
- **THEN** zero heap allocations occur

#### Scenario: Transport refusal is reported, not blocked

- **WHEN** the transport cannot provide a slot or cannot deliver the sample
- **THEN** `send_copy()` returns an error status promptly and never waits on a subscriber

#### Scenario: Diagnostics stay consistent

- **WHEN** `to_string()` and `std::format("{}", publisher)` are evaluated after successful `send_copy()` calls
- **THEN** the published count includes those calls and the formatter output equals the member output
