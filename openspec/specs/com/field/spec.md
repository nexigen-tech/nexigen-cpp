# com/field Specification

## Purpose

Defines the Field pattern: a shared, service-owned variable of type `T` that combines Get (read the current value), Set (update it), and Notify (event on change), with history size at least one so a late subscriber reads the latest value immediately on connect.

## Requirements

### Requirement: Field get-set-notify

`nexigen::com::Field<T>` SHALL provide read of the current value, write of a new value, and an event notification delivered to field listeners on every update; all three SHALL operate on zero-copy shared memory with `T` constrained by `com::concepts::Message`.

#### Scenario: Set is visible to get

- **WHEN** a field writer sets a new value and a reader queries the field afterwards
- **THEN** the reader observes exactly the new value

#### Scenario: Update notifies listeners

- **WHEN** a field is updated while a listener is waiting
- **THEN** the listener wakes with the change notification without polling

### Requirement: Last value on connect

A field SHALL keep history size at least one, so a subscriber that connects after the last update SHALL immediately receive the current value without waiting for the next write.

#### Scenario: Late subscriber reads the current value

- **WHEN** a field holds value `V` and a new subscriber connects
- **THEN** the subscriber's first read returns `V`

### Requirement: Field is owned by one writer

Field SHALL follow the single-writer contract of the underlying shared memory: exactly one process owns the field and publishes updates; other processes read and subscribe only.

#### Scenario: Reader-only access

- **WHEN** a reader process obtains a field handle
- **THEN** it can read and listen but cannot write the field

### Requirement: Field diagnostics

`Field` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the field service and the last-update sequence.

#### Scenario: Field renders its state

- **WHEN** `to_string()` and `std::format("{}", field)` are evaluated on a field after an update
- **THEN** the text names the service and the update count, and the formatter output equals the member output
