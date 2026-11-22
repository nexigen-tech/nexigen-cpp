# Spec Delta

## Purpose

Defines the canonical 16-byte application identifier that names one logical application within a
suite, so every telemetry frame, manifest entry, and diagnostic can refer to the same stable value
instead of a build-time tag.

## ADDED Requirements

### Requirement: Application identifier is a fixed 16-byte code

An application identifier SHALL be a non-empty sequence of 1 to 16 printable ASCII bytes limited to
`A-Z`, `a-z`, `0-9`, `_`, and `-`. The canonical value SHALL store exactly 16 bytes of fixed
storage with the unused tail zero-filled and SHALL render as the stored bytes up to the first zero
byte. A default-constructed value SHALL represent the unset identifier and SHALL render as an empty
string.

#### Scenario: A valid identifier is stored verbatim

- **WHEN** the identifier `CN_LOOP_1` is constructed
- **THEN** its rendered text equals `CN_LOOP_1`, its byte length is 9, and the remaining seven
  storage bytes are zero

#### Scenario: Sixteen bytes fit exactly

- **WHEN** a 16-byte identifier consisting of permitted characters is constructed
- **THEN** construction succeeds and the rendered text contains all 16 characters

#### Scenario: The unset identifier is distinguishable

- **WHEN** an identifier is default-constructed
- **THEN** it compares unequal to every valid identifier and renders as an empty string

### Requirement: Identifier construction never truncates

Construction from text SHALL reject an empty value, a value longer than 16 bytes, a byte outside
the permitted set, and any embedded NUL, and SHALL NOT truncate, hash, or otherwise alter the
input. The rejection SHALL be observable as a failed `core::Result` or a manifest validation error
naming the offending value.

#### Scenario: Seventeen bytes are rejected

- **WHEN** a 17-character identifier is supplied
- **THEN** construction fails and no truncated 16-byte value is produced

#### Scenario: Invalid characters are rejected

- **WHEN** an identifier contains a space, a quote, a backslash, a slash, a dot, a control byte, or
  a non-ASCII byte
- **THEN** construction fails and the value is not accepted by manifest validation

#### Scenario: Embedded NUL is rejected

- **WHEN** the input text contains a zero byte before its reported length
- **THEN** construction fails instead of silently shortening the identifier

### Requirement: Identifiers are unique within the suite

Every application of one suite SHALL have a distinct identifier. Duplicate identifiers within one
manifest SHALL fail manifest validation with `InvalidArgument` and no child process SHALL start.
Identifiers SHALL NOT be reserved across suites: two different suites MAY declare the same
identifier.

#### Scenario: Duplicate identifiers in one manifest are rejected

- **WHEN** two application entries of the same manifest declare `CN_LOOP_1`
- **THEN** manifest validation returns `InvalidArgument` and the suite starts nothing

#### Scenario: Different suites may reuse an identifier

- **WHEN** suite A and suite B both declare `CN_LOOP_1` in their own manifests
- **THEN** both manifests validate and both suites may run; the shared identifier is not an
  admission error

### Requirement: Identifier is attribution, not authentication

The identifier SHALL be treated as an observability label only. Consumers SHALL NOT use it to
authorize a process, grant capabilities, or accept a health claim; identity verification SHALL
remain the responsibility of the platform health claim and its supervisor.

#### Scenario: A spoofed identifier grants nothing

- **WHEN** a process publishes frames carrying another application's identifier
- **THEN** the frames may be attributed to that identifier for diagnostics, but no authorization,
  capability, or health-claim acceptance follows from it

#### Scenario: Health identity remains separately verified

- **WHEN** a supervised application publishes its identity claim
- **THEN** the health supervisor validates the claim against its manifest and process credentials,
  not against the telemetry identifier

### Requirement: Identifier values render and serialize

The identifier SHALL provide a member and free `to_string()`, a `std::formatter` specialization,
and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. JSON SHALL carry the
rendered text. Invalid or mistyped JSON SHALL leave the destination unchanged.

#### Scenario: Text and formatter agree

- **WHEN** `to_string()` and `std::format("{}", id)` are evaluated for a valid identifier
- **THEN** both produce the same rendered text

#### Scenario: JSON round-trips the identifier

- **WHEN** a valid identifier is written with `to_json` and read back with `from_json`
- **THEN** the recovered value equals the original

#### Scenario: Invalid JSON leaves the destination unchanged

- **WHEN** `from_json` receives a non-string, an empty string, an oversized string, or a string
  with forbidden characters
- **THEN** the destination keeps its previous value and no exception escapes
