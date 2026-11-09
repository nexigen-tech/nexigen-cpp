# Spec Delta

## MODIFIED Requirements

### Requirement: Log level accepts canonical enumerator names

`--log-level` MUST accept exactly the lowercase `nexigen::core::LogLevel` spellings:
`verbose`, `debug`, `info`, `warning`, `error`, `fatal`, and `off`. An unknown or differently
cased value MUST yield `InvalidArgument` with a diagnostic listing the accepted spellings. When
the option is omitted, no override SHALL be recorded.

#### Scenario: Every canonical level is accepted

- **WHEN** each of the seven canonical spellings is supplied to `--log-level`
- **THEN** parsing succeeds and records the matching `nexigen::core::LogLevel` value

#### Scenario: Unknown level is rejected

- **WHEN** `--log-level chatty` or `--log-level INFO` is supplied
- **THEN** parsing returns `InvalidArgument` and the diagnostic lists the accepted lowercase names

#### Scenario: Omitted level records no override

- **WHEN** no `--log-level` option is present
- **THEN** the parsed options report no log-level override
