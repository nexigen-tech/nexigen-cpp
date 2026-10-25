# Spec Delta

## ADDED Requirements

### Requirement: Timestamp diagnostic string

`nexigen::chrono::Timestamp` SHALL provide a cold-path `std::string to_string() const` (member, plus a free `to_string(const Timestamp&)` returning the same text) that renders the raw monotonic nanosecond count in a recognizable form (for example seconds with nanosecond remainder, or the full ns value labeled as monotonic). The function is a diagnostics utility; it SHALL NOT appear on any real-time hot path.

#### Scenario: Value appears in the text

- **WHEN** `to_string()` is called on a `Timestamp` constructed with 1'000'000'001 ns
- **THEN** the returned string conveys that exact nanosecond count (for example as `1.000000001` seconds or the integer ns)

#### Scenario: Member and free forms agree

- **WHEN** both `ts.to_string()` and `nexigen::chrono::to_string(ts)` are called
- **THEN** they return identical text
