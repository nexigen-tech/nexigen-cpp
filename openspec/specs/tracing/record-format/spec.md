# tracing/record-format Specification

## Purpose

Provides the cold-path JSON serialization bodies for all tracing value types: identifiers (`TraceId`, `SpanId`), `SpanContext`, `SpanAttribute`, `SpanRecord`, `TracerProviderConfig`, and `DroppedStats`. Every allocating or nlohmann operation for the record model lives in this unit so that the hot headers remain allocation-free.

## Requirements

### Requirement: Identifier hex format is fixed and round-trips

`TraceId` and `SpanId` SHALL render as exactly 16 zero-padded lowercase hex characters. `from_json` SHALL accept 1..16 hex digits of any case and SHALL leave the value untouched on malformed or non-string input.

#### Scenario: Full 64-bit id renders padded

- **WHEN** a 64-bit id with value `0x1a2b3c` is formatted
- **THEN** the string is `"00000000001a2b3c"`

#### Scenario: Mixed-case hex parses

- **WHEN** the JSON string `"1A2b3C"` is parsed
- **THEN** the resulting id value is `0x1a2b3c`

#### Scenario: Non-string input leaves id unchanged

- **WHEN** `from_json` receives a number or null for an id
- **THEN** the destination id keeps its previous value and no exception is thrown

### Requirement: SpanContext JSON is non-throwing and partial-field safe

`SpanContext` SHALL serialize to a JSON object with `trace_id`, `span_id`, and `sampled` fields. `from_json` SHALL ignore missing or wrong-typed fields and leave the destination's other fields untouched.

#### Scenario: Missing sampled field is ignored

- **WHEN** a `SpanContext` JSON object omits `sampled`
- **THEN** the destination's `sampled` value is preserved

### Requirement: SpanAttribute deserialization is all-or-nothing

`SpanAttribute::from_json` SHALL reject the whole attribute if the key is missing or oversized, the type tag is unknown, or the value type does not match the tag. A rejected attribute SHALL leave the destination unchanged.

#### Scenario: Int64 attribute round-trips

- **WHEN** a JSON object with `"key"="x"`, `"type"="int64"`, `"value"=-7` is parsed
- **THEN** the attribute stores key `"x"`, type `Int64`, and value `-7`

#### Scenario: String longer than inline capacity is rejected whole

- **WHEN** the JSON `value` string exceeds `attribute_inline_text_capacity`
- **THEN** the destination `SpanAttribute` is unchanged

### Requirement: SpanRecord deserialization delegates to substructure helpers

`SpanRecord::from_json` SHALL be composed of helpers that deserialize logical substructures: header fields (`trace_id`, `span_id`, `parent_span_id`, `kind`, `status`, `flags`, `attribute_count`, `thread_id`, `start_ns`, `end_ns`), name, location fields (`file`, `function`, `line`, `column`), and the attribute array. Each helper SHALL ignore missing or wrong-typed fields and SHALL not mutate the destination on malformed input for its own fields.

#### Scenario: Header helper parses timing fields

- **WHEN** `start_ns` and `end_ns` are present as unsigned numbers
- **THEN** the helper sets `start` and `end` timestamps and leaves other header fields untouched

#### Scenario: Location helper interns file and function strings

- **WHEN** `file` and `function` strings fit the intern table
- **THEN** stable process-static pointers are stored and remain valid for the process lifetime

#### Scenario: Attribute array helper respects the maximum

- **WHEN** the JSON array contains more than `MaxAttributes` elements
- **THEN** only the first `MaxAttributes` are stored and `attribute_count` is set accordingly

### Requirement: TracerProviderConfig deserialization uses safe nlohmann accessors

`TracerProviderConfig::from_json` SHALL read numeric fields with type checks and clamps, SHALL prefer the canonical `worker_thread` object over the legacy `core_mask`, and SHALL leave unspecified fields at their current values.

#### Scenario: Legacy core_mask is accepted when canonical worker_thread is absent

- **WHEN** only `core_mask` is present as an unsigned number
- **THEN** `worker_thread.cpus` is set from the mask and other fields keep their defaults

#### Scenario: Out-of-range poll interval is clamped

- **WHEN** `worker_poll_interval_us` exceeds `UINT32_MAX`
- **THEN** the field is clamped to `UINT32_MAX`

### Requirement: DroppedStats deserialization is non-throwing and partial-field safe

`DroppedStats::from_json` SHALL ignore missing or non-numeric fields and SHALL only update counters for valid unsigned numbers.

#### Scenario: Partial DroppedStats JSON is accepted

- **WHEN** only `rate_limited` and `buffer_full` are present
- **THEN** those counters are updated and `slot_exhausted` keeps its previous value

### Requirement: All deserialization is exception-free under `JSON_NOEXCEPTION=1`

Every `from_json` overload in this unit SHALL use `is_*` guards and pointer accessors rather than exceptions. A wrong shape SHALL be a silent no-op for the affected fields, not a runtime abort.

#### Scenario: Parsing runs under `-fno-exceptions`

- **WHEN** the unit is compiled with `JSON_NOEXCEPTION=1`
- **THEN** malformed JSON inputs do not throw and the binary links successfully

### Requirement: Serialization helpers do not change hot-path contracts

No function added or modified by this refactor SHALL be placed in a hot header or called from a producer/recording/drain path. The hot headers (`span_context.hpp`, `span_record.hpp`, `tracer_provider.hpp`) SHALL keep their current inline, allocation-free contracts.

#### Scenario: Tracer hot paths remain unchanged

- **WHEN** `start_span`, `set_attribute`, `set_status`, and `enqueue` run in the existing latency benchmarks
- **THEN** they do not include `record_format.cpp` machinery and their gate times are unchanged

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `src/tracing/record_format.cpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `src/tracing/record_format.cpp`
- **THEN** no function in the file exceeds complexity 15
