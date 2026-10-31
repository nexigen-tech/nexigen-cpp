# Spec Delta

## ADDED Requirements

### Requirement: Message capacity is runtime-configurable

The system SHALL provide a runtime-configurable `message_capacity` field on `LoggerOptions` (default `128`) that the async logger validates against its compile-time `MessageCapacity` bound at initialization.

#### Scenario: Default message capacity
- **WHEN** a `LoggerOptions` is default-constructed
- **THEN** `message_capacity` equals `128`

#### Scenario: Message capacity round-trips JSON
- **WHEN** a `LoggerOptions` with a non-default `message_capacity` passes through `to_json` then `from_json` under `JSON_NOEXCEPTION=1`
- **THEN** the recovered `message_capacity` equals the original value

#### Scenario: Message capacity renders in to_string
- **WHEN** `to_string()` is called on a `LoggerOptions`
- **THEN** the result is non-empty and contains the `message_capacity` field name and its value