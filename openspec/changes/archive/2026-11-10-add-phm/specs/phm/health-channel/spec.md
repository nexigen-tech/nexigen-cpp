# Spec Delta

## Purpose

Defines the inter-process health channel: the `nexigen::phm` client API that applications use to emit checkpoints and health-status notifications over the iceoryx2 event pattern, carrying a health state of `Ok`/`Warning`/`Error`/`Critical` with no sockets and no serialization.

## ADDED Requirements

### Requirement: Health channel states

`nexigen::phm` SHALL expose a `HealthState` scoped enum with the values `Ok`, `Warning`, `Error`, and `Critical`, and SHALL provide the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter`).

#### Scenario: HealthState round-trips

- **WHEN** each `HealthState` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name

### Requirement: Health status notification

A supervised process SHALL report a health-channel transition by publishing a `HealthState` over an iceoryx2 event; the transition SHALL reach the PHM daemon without a socket or a serialization step.

#### Scenario: Channel transition reaches the daemon

- **WHEN** an application publishes a channel transition from `Ok` to `Error`
- **THEN** the daemon's listener observes the new state promptly without polling

### Requirement: Checkpoint emission

The client library SHALL provide a checkpoint emission API that a process calls at each step of its control flow; each checkpoint SHALL carry a checkpoint id and be delivered to the daemon through the event pattern.

#### Scenario: Checkpoint is emitted with an id

- **WHEN** an application emits checkpoint id `7`
- **THEN** the daemon's listener receives an activation identifying checkpoint `7`

### Requirement: Health channel diagnostics

The client-side health channel handle SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the channel and its current state.

#### Scenario: Channel renders its state

- **WHEN** `to_string()` and `std::format("{}", channel)` are evaluated
- **THEN** the text names the channel and the state, and the formatter output equals the member output
