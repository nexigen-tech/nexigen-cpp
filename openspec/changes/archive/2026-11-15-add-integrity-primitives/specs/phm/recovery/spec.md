# Spec Delta

## ADDED Requirements

### Requirement: Fault log record integrity

Each appended fault record SHALL carry a monotonic sequence number and a CRC-32C
over its fields. A verification scan SHALL classify every line as intact,
corrupted, truncated, or legacy, SHALL accept pre-change four-field lines as
legacy and unverified, and SHALL NOT abort on malformed input.

#### Scenario: Written records verify

- **WHEN** records are written and the log is scanned
- **THEN** every record is reported intact with a strictly increasing sequence number

#### Scenario: A flipped byte is detected

- **WHEN** one byte of a record is changed on disk and the log is scanned
- **THEN** that record is reported corrupted and the scan continues with the next line

#### Scenario: A truncated tail is reported

- **WHEN** the final line is cut before its checksum
- **THEN** it is reported truncated, not corrupted, and no previous record is affected

#### Scenario: Legacy records stay readable

- **WHEN** the log contains pre-change four-field lines
- **THEN** they are reported legacy and unverified, and the scan exits successfully
