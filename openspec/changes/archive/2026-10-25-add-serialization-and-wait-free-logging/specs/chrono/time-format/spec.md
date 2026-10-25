# Spec Delta

## ADDED Requirements

### Requirement: Civil time diagnostic string

`nexigen::chrono::Civil_time` SHALL provide a cold-path `std::string to_string() const` (member, plus a free `to_string(const Civil_time&)` returning the same text) rendering the broken-down UTC time in ISO-8601-like readable form (`YYYY-MM-DD HH:MM:SS.nnnnnnnnn UTC` field content; exact decoration is diagnostics-only). This is a diagnostics utility for configuration and tests, never a hot-path formatter.

#### Scenario: Decomposition renders fully

- **WHEN** `to_string()` is called on the `Civil_time` produced for a known UTC nanosecond instant
- **THEN** the returned string contains that year, month, day, hour, minute, second, and nanosecond value

#### Scenario: Free helper matches member

- **WHEN** `to_string(ct)` (free) and `ct.to_string()` (member) are compared
- **THEN** they return identical text
