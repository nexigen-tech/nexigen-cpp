# chrono/time-format Specification

## Purpose

Provides allocation-free UTC calendar math and fixed-width ISO-8601 nanosecond timestamp formatting for any subsystem that renders or buckets event times, without touching process-wide timezone or locale state.

## Requirements

### Requirement: UTC civil-time decomposition

The system SHALL decompose a signed UTC nanosecond count into year, month, day, hour, minute, second, and nanosecond fields in UTC, using floor semantics so pre-epoch (negative) values map to the day and time before 1970-01-01, and SHALL expose the whole-days-since-epoch component of the same value.

#### Scenario: Known epoch decomposes exactly

- **WHEN** a UTC nanosecond count corresponding to a known civil instant is decomposed
- **THEN** the fields equal that instant's year, month, day, hour, minute, second, and nanoseconds

#### Scenario: Leap day and year-end boundaries

- **WHEN** counts at a leap day and at the final nanosecond of a year are decomposed
- **THEN** the results are February 29 of the leap year and December 31 23:59:59.999999999 respectively

#### Scenario: Pre-epoch floor division

- **WHEN** a negative UTC nanosecond count (before 1970-01-01T00:00:00Z) is decomposed
- **THEN** the day component is the previous calendar day and the time-of-day component is near midnight, never a truncated-toward-zero misdate

### Requirement: Fixed-width ISO-8601 nanosecond formatting

The system SHALL format a signed UTC nanosecond count as exactly `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` — 30 characters plus a terminating NUL — into a caller-provided fixed buffer, with nine fractional digits, and SHALL never write beyond the buffer for any input in the signed 64-bit nanosecond domain.

#### Scenario: Byte-exact rendering

- **WHEN** a known UTC nanosecond count is formatted
- **THEN** the buffer holds exactly the expected 30-character string ending in `Z` with a NUL at index 30

#### Scenario: Out-of-representable-range years clamp

- **WHEN** an input whose year field falls outside four decimal digits is formatted
- **THEN** the year clamps to the field width and the output keeps the exact 30-character layout

#### Scenario: Formatting touches no shared state

- **WHEN** formatting runs concurrently on several threads with independent buffers
- **THEN** each call completes correctly with no heap allocation and no dependency on timezone or locale state

### Requirement: Civil time diagnostic string

`nexigen::chrono::Civil_time` SHALL provide a cold-path `std::string to_string() const` (member, plus a free `to_string(const Civil_time&)` returning the same text) rendering the broken-down UTC time in ISO-8601-like readable form (`YYYY-MM-DD HH:MM:SS.nnnnnnnnn UTC` field content; exact decoration is diagnostics-only). This is a diagnostics utility for configuration and tests, never a hot-path formatter.

#### Scenario: Decomposition renders fully

- **WHEN** `to_string()` is called on the `Civil_time` produced for a known UTC nanosecond instant
- **THEN** the returned string contains that year, month, day, hour, minute, second, and nanosecond value

#### Scenario: Free helper matches member

- **WHEN** `to_string(ct)` (free) and `ct.to_string()` (member) are compared
- **THEN** they return identical text
