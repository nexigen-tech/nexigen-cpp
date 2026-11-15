# Spec Delta

## ADDED Requirements

### Requirement: Security error code

`core::ErrorCode` SHALL gain the value `SecurityError` for a failed integrity or
trust precondition. It SHALL be append-only, participate in
`to_string`/`from_string`, ADL JSON, and `std::formatter`, and SHALL not change
the numeric value or spelling of any existing code.

#### Scenario: SecurityError round-trips

- **WHEN** `SecurityError` passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** the canonical name round-trips and the formatter renders it

#### Scenario: Existing error values are stable

- **WHEN** the enum table is inspected after the addition
- **THEN** every pre-existing code retains its original numeric value and spelling
