# Spec Delta

## ADDED Requirements

### Requirement: Configurable subscriber buffer depth

`Publisher::create` and `Subscriber::create` SHALL accept an optional `subscriber_max_buffer_size` that sets the service's maximum subscriber buffer depth. A value of `0` SHALL preserve the transport default, and a non-zero value SHALL be applied to the service so a subscriber can hold that many samples without deterministic discards.

#### Scenario: A full publish burst survives

- **WHEN** a publisher creates a service with a subscriber maximum of N and publishes N frames before the subscriber drains
- **THEN** a subscriber opened with the same maximum and a buffer of N receives all N frames without a discard

#### Scenario: Zero preserves the transport default

- **WHEN** `create` is called with `subscriber_max_buffer_size = 0`
- **THEN** the service keeps the transport default depth and existing callers behave unchanged

#### Scenario: Existing zero-argument callers are unaffected

- **WHEN** an existing caller invokes `create(node, name)` without the new option
- **THEN** the call compiles and behaves exactly as before
