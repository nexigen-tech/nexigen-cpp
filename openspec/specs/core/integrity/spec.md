# core/integrity Specification

## Purpose

Provides allocation-free checksums, digests, and a verified file-load contract
in the base library, so configuration and evidence can be checked for
corruption without depending on a higher-level module.

## Requirements

### Requirement: CRC-32C and CRC-64 primitives

`nexigen::core::integrity` SHALL provide CRC-32C (Castagnoli) and CRC-64/XZ as
both one-shot and streaming (init/update/finalize) functions. They SHALL be
`noexcept`, allocation-free, table-driven from `constexpr` data, and SHALL
produce the published check values for the standard `"123456789"` vector.

#### Scenario: Standard vectors match

- **WHEN** `"123456789"` is hashed with CRC-32C and CRC-64/XZ
- **THEN** the results equal the published check values `0xE3069283` and `0x995DC9BBDF1939FA`

#### Scenario: Streaming equals one-shot

- **WHEN** a buffer is hashed in one call and again in two arbitrary chunks
- **THEN** both results are identical

#### Scenario: Empty input has a defined result

- **WHEN** an empty buffer is hashed
- **THEN** the result is the algorithm's empty-input value, not an error

### Requirement: SHA-256 in the base library

The verified SHA-256 implementation SHALL live in `nexigen::core::integrity`
with incremental update and finalize, hex rendering, and file hashing. The
existing `nexigen::exec` names SHALL remain available as aliases with unchanged
signatures.

#### Scenario: Existing call sites keep compiling

- **WHEN** code that used `nexigen::exec::Sha256`, `sha256_hex`, or `sha256_file` is rebuilt
- **THEN** it compiles and produces the same digests as before the move

#### Scenario: Known digest matches

- **WHEN** the empty string and `"abc"` are hashed
- **THEN** the hex results equal the FIPS 180-4 values

### Requirement: Constant-time digest comparison

The capability SHALL provide a comparison of two equal-length hex digests whose
running time does not depend on the position of the first differing character.

#### Scenario: Mismatch in the first character takes as long as the last

- **WHEN** two digests of equal length differ at the first character and at the last character
- **THEN** both comparisons traverse the full length and return `false`

#### Scenario: Length mismatch is rejected

- **WHEN** the digests have different lengths
- **THEN** the comparison returns `false` without reading past either input

### Requirement: Verified file load

The capability SHALL provide a loader that reads a file once, hashes exactly the
bytes it returns, and returns them only when they match an expected SHA-256. A
mismatch or an unreadable file SHALL return an error and no content.

#### Scenario: Matching content is returned

- **WHEN** a file is loaded with the digest of its current bytes
- **THEN** the returned content equals the file contents

#### Scenario: Changed content is refused

- **WHEN** one byte changes between computing the digest and loading the file
- **THEN** the load returns a security error and no content

#### Scenario: The loader hashes the bytes it returns

- **WHEN** the file is replaced between the read and the hash
- **THEN** the loader still hashes and returns the same buffer, so the check cannot be bypassed

### Requirement: Allocation-free hashing path

Every checksum and digest operation over an in-memory buffer SHALL perform no
heap allocation.

#### Scenario: Buffer hashing allocates nothing

- **WHEN** CRC-32C, CRC-64, and SHA-256 hash a fixed buffer under the project's allocation counter
- **THEN** the allocation count stays unchanged
