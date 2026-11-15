# Spec Delta

## ADDED Requirements

### Requirement: AccessDenied transition result

`exec::StateResult` SHALL gain the value `AccessDenied` with the full project
conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json` under
`JSON_NOEXCEPTION=1`, and `std::formatter`). The value SHALL be append-only so
existing results keep their numeric values.

#### Scenario: AccessDenied round-trips

- **WHEN** `AccessDenied` passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** the canonical name round-trips and the formatter renders it

#### Scenario: Existing results keep their values

- **WHEN** the enum table is inspected after the addition
- **THEN** every pre-existing result retains its original numeric value and spelling
