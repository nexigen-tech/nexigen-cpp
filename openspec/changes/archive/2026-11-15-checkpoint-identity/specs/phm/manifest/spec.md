# Spec Delta

## ADDED Requirements

### Requirement: Expected process identity

A supervised process entry SHALL accept an optional `expected_uid`. A value of
`0` SHALL be rejected when the manifest is loaded. When the field is present,
the daemon SHALL require a verified claim before it accepts checkpoints from
that process.

#### Scenario: Expected UID round-trips

- **WHEN** an entry declares `expected_uid`
- **THEN** the manifest serializes and parses it unchanged

#### Scenario: UID zero is rejected

- **WHEN** an entry declares `expected_uid = 0`
- **THEN** manifest validation fails with `InvalidArgument`

#### Scenario: An absent field keeps the legacy path

- **WHEN** an entry omits `expected_uid`
- **THEN** the process is supervised without an identity claim

#### Scenario: A name that cannot fit a claim is rejected

- **WHEN** an entry declares `expected_uid` with an empty name or a name of 32
  bytes or more
- **THEN** manifest validation fails, because the published claim would always
  be truncated and the process could never verify
