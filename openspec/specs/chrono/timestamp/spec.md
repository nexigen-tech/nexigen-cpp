# chrono/timestamp Specification

## Purpose

Provides the shared point-in-time value type for the ecosystem: a monotonic, nanosecond-resolution instant sourced from the operating system's raw monotonic clock, for deadlines, interval measurement, and event ordering on real-time paths.

## Requirements

### Requirement: Nanosecond timestamp value type

The system SHALL expose a `Timestamp` value type in `nexigen::chrono` that represents an instant as a count of nanoseconds held in a 64-bit unsigned integer, copyable by value with no ownership semantics, and SHALL provide access to the raw nanosecond count.

#### Scenario: Wrapping a raw count

- **WHEN** a `Timestamp` is constructed from a given nanosecond count
- **THEN** reading the count back yields exactly the value it was constructed with

#### Scenario: Copy and compare of plain values

- **WHEN** two `Timestamp` values are copied and compared for equality or ordering
- **THEN** comparison reflects only their nanosecond counts and copying preserves the count

### Requirement: Monotonic clock sourcing

The system SHALL provide a way to obtain the current `Timestamp` from the system's raw monotonic clock, and successive reads on the same thread SHALL never return a value lower than a previously returned value from that read path.

#### Scenario: Time advances

- **WHEN** the current timestamp is read twice with work performed in between
- **THEN** the second reading is greater than or equal to the first

#### Scenario: Clock discipline

- **WHEN** a timestamp is read from the clock source
- **THEN** the value derives from the monotonic raw clock domain and is unaffected by setting the system wall clock

#### Scenario: Clock failure is non-fatal

- **WHEN** the underlying clock read fails
- **THEN** obtaining the current timestamp returns without throwing and reports failure through the value or the error model rather than terminating the process

### Requirement: Timestamp arithmetic

The system SHALL support measuring the elapsed nanoseconds between two timestamps as an unsigned 64-bit difference, with no floating-point conversion on the hot path.

#### Scenario: Elapsed interval

- **WHEN** the difference is taken between a later timestamp and an earlier one
- **THEN** the result equals the nanoseconds between them exactly as stored

#### Scenario: Saturating backwards difference

- **WHEN** a timestamp is subtracted by an earlier operand whose count is greater, or by an offset larger than its own count
- **THEN** the result is zero, never a wrapped-around interval

#### Scenario: Ordering for deadline checks

- **WHEN** a timestamp is compared against a deadline timestamp
- **THEN** ordering compares nanosecond counts directly, so a value judged not-past-deadline remains valid for that decision

### Requirement: Hot-path safety of timestamps

Reading and manipulating timestamps SHALL perform no heap allocation, acquire no lock, and be usable under build flags where exceptions are disabled.

#### Scenario: Repeated reads allocate nothing

- **WHEN** the current timestamp is read many times in a loop on a hot path
- **THEN** no allocation, deallocation, or blocking call occurs during the loop

#### Scenario: Register-sized value

- **WHEN** the `Timestamp` type is inspected at compile time
- **THEN** it is exactly 8 bytes and trivially copyable

### Requirement: Timestamp diagnostic string

`nexigen::chrono::Timestamp` SHALL provide a cold-path `std::string to_string() const` (member, plus a free `to_string(const Timestamp&)` returning the same text) that renders the raw monotonic nanosecond count in a recognizable form (for example seconds with nanosecond remainder, or the full ns value labeled as monotonic). The function is a diagnostics utility; it SHALL NOT appear on any real-time hot path.

#### Scenario: Value appears in the text

- **WHEN** `to_string()` is called on a `Timestamp` constructed with 1'000'000'001 ns
- **THEN** the returned string conveys that exact nanosecond count (for example as `1.000000001` seconds or the integer ns)

#### Scenario: Member and free forms agree

- **WHEN** both `ts.to_string()` and `nexigen::chrono::to_string(ts)` are called
- **THEN** they return identical text
