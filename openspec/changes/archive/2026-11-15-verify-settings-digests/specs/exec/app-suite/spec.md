# Spec Delta

## ADDED Requirements

### Requirement: Settings files are digest-verified before launch

The suite manifest SHALL accept an optional `settings_digests` array of
`{path, sha256}` entries. Every path SHALL be absolute and every digest SHALL be
64 hexadecimal characters. The suite supervisor SHALL verify each listed file
with a single-read SHA-256 check before launching any application; a mismatch
SHALL report `SecurityError` and start no process.

#### Scenario: Matching settings start the suite

- **WHEN** the manifest lists a settings file with its correct digest
- **THEN** the supervisor verifies it and the applications start as before

#### Scenario: A changed settings file starts nothing

- **WHEN** a listed settings file no longer matches its digest
- **THEN** the start fails with `SecurityError` and no application is launched

#### Scenario: An absent section keeps plain behavior

- **WHEN** the manifest omits `settings_digests`
- **THEN** the suite starts without reading any settings file for verification

#### Scenario: Malformed entries are rejected

- **WHEN** an entry has a relative path or a digest that is not 64 hex characters
- **THEN** manifest validation fails with `InvalidArgument`
