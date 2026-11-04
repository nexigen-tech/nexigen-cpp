# Spec Delta

## Purpose

Defines the single, exception-free plumbing contracts that observability fan-out sinks and file-backed sinks are built on: a variadic member pack (compile-time lookup, ordered delivery, first-failure status combination) and shared cold-path file descriptor operations (open flag parity, full-write with EINTR retry, fsync). The project keeps exactly one implementation of each; module sinks delegate to it instead of keeping local copies.

## ADDED Requirements

### Requirement: Variadic fan-out member pack

Every composite fan-out sink (chrono, metrics, tracing, and future consumers) SHALL store its members in pack order (`std::tuple`), expose index-based and type-based non-owning member access resolved entirely at compile time, deliver by visiting each member exactly once, and combine member outcomes so the first failure wins. Lookup of out-of-range indices or absent/duplicate types SHALL fail at compile time.

#### Scenario: Index and type access agree

- **WHEN** a composite over distinct sink types has `get<0>()` and `get<FirstSinkType>()` taken
- **THEN** both expressions refer to the same member object, and the static pack size equals the number of configured members

#### Scenario: Iteration visits members in pack order

- **WHEN** the pack iterates a hand-rolled order probe over members `A, B, C`
- **THEN** the visit sequence is exactly `A, B, C` (each visited once, index 0 first)

#### Scenario: First-failure combination

- **WHEN** member outcomes are combined as `(ok, failing(X))` and as `(failing(X), failing(Y))`
- **THEN** both results are `failing(X)` (first failure wins; success only when both succeed; no exceptions)

#### Scenario: Duplicate or absent member types fail at compile time

- **WHEN** type access is instantiated with a type absent from the pack or present more than once
- **THEN** the translation unit does not compile (negative static-assert probe)

### Requirement: Fan-out construction constraints

The fan-out pack SHALL support default construction only when all members are default-constructible, and member-injection construction (moving configured, possibly non-default-constructible members) otherwise, matching the current composite constructor surface.

#### Scenario: Empty composite default-satisfies the module sink concept

- **WHEN** an empty fan-out construction is instantiated under any module sink concept
- **THEN** it satisfies that concept (the existing "empty composite must satisfy the sink contract" guarantee)

#### Scenario: Configured member injection keeps working

- **WHEN** a composite is constructed by injecting a path-configured file sink together with default-constructible console sinks
- **THEN** construction succeeds without default-constructing the configured member, and the injected member's configured state is preserved

### Requirement: Shared full-write loop

Every file-backed sink SHALL write through one shared write loop that completes any number of bytes across partial `write` calls in file order, retries interrupted (`EINTR`) writes without data loss or duplication, and reports failure as a first-failure error status (no exceptions). Flushing SHALL request durable persistence for an open descriptor.

#### Scenario: Partial writes complete in order

- **WHEN** a blob larger than the single-`write` capacity under test is written through the shared loop — the probe uses a blocking pipe whose reader throttles under-process drains and an interval timer interrupting blocked writes, which forces repeated partial `write` returns (and real `EINTR` retries)
- **THEN** the consumer reads back exactly the original bytes, in order, and the loop reports success after the last byte

#### Scenario: Write failure is reported, not swallowed

- **WHEN** the loop writes against a descriptor whose target rejects writes (live pipe with closed read end, or an invalid descriptor probe)
- **THEN** the result is a first-failure error status (system error), and no bytes are reported as written

### Requirement: Shared file-open policy

Opening an output file SHALL use one shared policy owner: close-on-exec, create-if-missing, mode 0644, with the caller's append policy selecting appended writes vs truncating the existing file.

#### Scenario: Open flag parity

- **WHEN** the same pre-existing content `hello\n` is opened twice — once with the append policy, once without — and each writes `world\n`
- **THEN** the append path contains `hello\nworld\n`, the non-append path only `world\n`, and the descriptor returned by the shared open is close-on-exec with mode 0644 (default umask)

### Requirement: Shared settings loader vocabulary

The chrono, metrics, and tracing settings loaders SHALL share one implementation of the loader vocabulary helpers: in-range unsigned key read, `sink.<key>` field naming, optional `sink` object location, and sink string/bool/unsigned reads. Diagnostics SHALL be byte-identical to the current per-module texts, on the same fields and source layers; invalid values SHALL leave the previous settings intact.

#### Scenario: Loader diagnostics unchanged after consolidation

- **WHEN** the regression inputs are loaded — out-of-range unsigned option, non-string `path`, non-boolean `append`, non-object `sink`
- **THEN** each diagnostic is byte-identical to its pre-consolidation text (`expected an integer in range`, `expected a string`, `expected a boolean`, `expected a JSON object`) on the same field and source layer, and previous settings survive

#### Scenario: Single implementation backs all three loaders

- **WHEN** the loaders are audited for private copies of the vocabulary helpers
- **THEN** chrono/metrics/tracing define no private `FileRead`/read-file/unsigned/diagnostic/pack-read copies and reference the shared core implementation

### Requirement: Plumbing serialization scope

The fan-out pack and fd plumbing are implementation plumbing, not serializable domain objects, and SHALL NOT provide `to_string` / `to_json` / `std::formatter` surfaces — this continues the `fixed_writer.hpp` precedent. Module diagnostic contracts stay at the composite/sink level where they already exist.

#### Scenario: Composite diagnostics preserved after migration

- **WHEN** any composite sink is serialized for diagnostics (`to_json`) or its config type is rendered via std::format
- **THEN** the `{type, members}`-shaped report matches the module sinks/tracing-metadata specs (regression-guarded by existing module suites)

#### Scenario: Plumbing types carry no serialization surface

- **WHEN** the new plumbing types' public API is reviewed against the project serialization mandate
- **THEN** no member/to_json/formatter surface exists on the pack or fd plumbing (plumbing exemption, `fixed_writer` precedent)
