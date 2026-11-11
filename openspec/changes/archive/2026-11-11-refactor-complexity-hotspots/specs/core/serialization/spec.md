# Spec Delta

## ADDED Requirements

### Requirement: Manifest and config deserialization uses a shared field vocabulary

Every cold-path `from_json` that reads object fields from nlohmann::json SHALL obtain them through the project-wide field-extraction vocabulary provided by `nexigen::core`, covering optional string, boolean, signed integer, range-bounded unsigned integer, nested object, and array of objects. New deserialization code SHALL NOT add per-file probe lambdas or hand-written `find` → `is_<type>` → `get` chains; existing ones SHALL be migrated when their owning type is next refactored.

#### Scenario: Missing field leaves the destination untouched

- **WHEN** a helper is asked for a field absent from the JSON object
- **THEN** it writes nothing to the destination and reports the field as absent to the caller

#### Scenario: Wrong-typed field is ignored

- **WHEN** a helper is asked for a string but the field holds a number, or for a boolean but the field holds a string
- **THEN** the destination keeps its previous value and no exception path is taken

#### Scenario: Object and array composition keep the current semantics

- **WHEN** a deserializer composes helpers for a nested object or an array of objects
- **THEN** it applies the same accept/reject rule the type applies today: a non-object element rejects the whole collection, while a missing or wrong-typed individual field preserves that element's default value

### Requirement: Field-extraction helpers are non-throwing and cold-path only

The field-extraction helpers SHALL be `noexcept` and SHALL never throw, abort or terminate under `JSON_NOEXCEPTION=1`, whatever the input. They SHALL be invoked only from cold-path configuration and diagnostic code — never from a producer or worker hot path — and SHALL introduce no allocation, no lock and no shared state.

#### Scenario: Out-of-range unsigned value is rejected without terminating

- **WHEN** a helper reads an unsigned field whose value exceeds the destination type's maximum (for example 2^32 into a 32-bit field)
- **THEN** the destination keeps its previous value, the helper reports the field as rejected, and the process does not abort

#### Scenario: Vocabulary is cold-path only

- **WHEN** the helper header is included by a producer or worker hot-path translation unit
- **THEN** it introduces no allocation, no lock and no JSON operation that could run inside the real-time loop
