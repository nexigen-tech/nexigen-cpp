# Spec Delta

## ADDED Requirements

### Requirement: Trusted executable policy

`nexigen::exec` SHALL expose a `TrustedPlatformConfig` value with a
`require_executable_digest` policy flag. When the policy is enabled, every
application SHALL declare an absolute command and a 64-character lowercase or
uppercase hex SHA-256 digest; validation SHALL reject a relative command, a
missing digest, or a malformed digest. When the policy is disabled, a supplied
digest SHALL still be verified. `TrustedPlatformConfig` SHALL provide
`to_string()`, ADL JSON, and `std::formatter`.

#### Scenario: Policy requires digests

- **WHEN** a suite enables the trusted policy and one application omits its digest
- **THEN** manifest validation rejects the suite

#### Scenario: Policy accepts a complete manifest

- **WHEN** every application declares an absolute command and a valid SHA-256 digest
- **THEN** manifest validation accepts the suite

### Requirement: Executable digest verification

Before forking a trusted application, the EM SHALL compute the SHA-256 digest of
the executable file and compare it with the declared digest. A missing,
unreadable, or mismatched executable SHALL fail the start with
`core::ErrorCode::SecurityError`, and no `resource.json` SHALL be published.

#### Scenario: Matching digest starts

- **WHEN** the executable's SHA-256 matches the manifest digest
- **THEN** the start proceeds

#### Scenario: Mismatch is refused

- **WHEN** the executable's SHA-256 differs from the manifest digest
- **THEN** `start` returns `SecurityError` and publishes no `resource.json`

#### Scenario: Missing executable is refused

- **WHEN** the trusted executable path does not exist or cannot be read
- **THEN** `start` returns `SecurityError`

### Requirement: SHA-256 implementation

The digest helper SHALL implement FIPS 180-4 SHA-256 and SHALL match the
published test vectors for the empty string, `abc`, and a payload spanning
multiple 64-byte blocks.

#### Scenario: Known vector matches

- **WHEN** the helper hashes `abc`
- **THEN** it returns `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`

#### Scenario: Empty input matches

- **WHEN** the helper hashes an empty file
- **THEN** it returns `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`
