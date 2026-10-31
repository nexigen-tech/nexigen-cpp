# Spec Delta

## Purpose

Defines the persisted snapshot record — a monotonic-raw stamp and its signed UTC offset — and its serialization, so post-hoc readers can re-bind monotonic events to wall-clock time.

## ADDED Requirements

### Requirement: Snapshot value type

`nexigen::chrono::Chrono_snapshot` SHALL be a plain value type holding a `chrono::Timestamp` `stamp` (the monotonic-raw series key) and a signed `std::int64_t` `realtime_offset_ns` (the `realtime` minus `monotonic_raw` offset), copyable by value with no ownership semantics and trivially copyable.

#### Scenario: Wrapping and reading fields

- **WHEN** a `Chrono_snapshot` is constructed from a known stamp and offset
- **THEN** reading the fields back yields exactly those values

#### Scenario: Copy preserves the value

- **WHEN** a `Chrono_snapshot` is copied
- **THEN** the copy compares equal to the original field for field

#### Scenario: Register-sized and trivially copyable

- **WHEN** the `Chrono_snapshot` type is inspected at compile time
- **THEN** it is trivially copyable and no larger than the sum of its two fields plus natural alignment

### Requirement: Monotonic series key

The `stamp` field SHALL be the monotonic-raw nanosecond count at which the offset was sampled, so successive snapshots in one series are ordered by their stamps.

#### Scenario: Stamps order the series

- **WHEN** two snapshots from one provider are compared
- **THEN** the later snapshot's `stamp` is greater than or equal to the earlier snapshot's `stamp`

### Requirement: Signed UTC offset semantics

The `realtime_offset_ns` field SHALL represent the signed difference between the wall clock and the monotonic-raw clock at the sampled instant, so a monotonic event stamp can be re-bound to wall-clock time as `stamp + realtime_offset_ns`. Values before the Unix epoch SHALL be representable as negative offsets.

#### Scenario: Re-binding reproduces wall-clock time

- **WHEN** a monotonic event stamp is added to the offset from the snapshot covering that instant
- **THEN** the result equals the wall-clock time within the documented two-read latency

#### Scenario: Negative offset is preserved

- **WHEN** an offset is negative
- **THEN** the value round-trips through the type without overflow or sign loss

### Requirement: Snapshot serialization

`Chrono_snapshot` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Snapshot JSON round-trip

- **WHEN** a `Chrono_snapshot` is serialized and parsed back
- **THEN** the stamp and the offset survive the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Snapshot renders diagnostics

- **WHEN** `to_string()`, the free `to_string`, and `std::format("{}", snapshot)` are applied
- **THEN** the member and free results are equal non-empty strings naming the stamp and offset, and the formatter renders the same text
