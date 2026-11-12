# Spec Delta

## MODIFIED Requirements

### Requirement: Manifest and config deserialization uses a shared field vocabulary

Every cold-path `from_json` that reads object fields from nlohmann::json SHALL obtain them through the project-wide field-extraction vocabulary provided by `nexigen::core`: the field readers in `nexigen::core::detail` (`read_string`, `read_bool`, `read_signed<T>`, `read_unsigned<T>`, `read_object`, `collect_array`, `collect_object_array`, `read_fixed_string`, `read_fixed_bytes`) and the enum field reader `nexigen::core::mandate::read_enum`, covering optional string, boolean, signed integer, range-bounded unsigned integer, nested object, and array of objects. New deserialization code SHALL NOT add per-file probe lambdas or hand-written `find` → `is_<type>` → `get` chains. The per-type duplicate readers that still live in `nexigen::exec::detail::sys_helpers.hpp` (`read_string`, `read_u32`, `read_u64`, `read_i32`, `read_optional_i32`, `read_bool`) SHALL be removed and every call site migrated to the `nexigen::core` vocabulary, so the vocabulary has a single definition site.

#### Scenario: Missing field leaves the destination untouched

- **WHEN** a helper is asked for a field absent from the JSON object
- **THEN** it writes nothing to the destination and reports the field as absent to the caller

#### Scenario: Wrong-typed field is ignored

- **WHEN** a helper is asked for a string but the field holds a number, or for a boolean but the field holds a string
- **THEN** the destination keeps its previous value and no exception path is taken

#### Scenario: Object and array composition keep the current semantics

- **WHEN** a deserializer composes helpers for a nested object or an array of objects
- **THEN** it applies the same accept/reject rule the type applies today: a non-object element rejects the whole collection, while a missing or wrong-typed individual field preserves that element's default value

#### Scenario: The field vocabulary has a single definition site

- **WHEN** the field-extraction helpers are searched across the public headers
- **THEN** `read_string`, `read_bool`, `read_signed<T>`, `read_unsigned<T>`, `read_object`, `collect_array`, `collect_object_array`, `read_fixed_string` and `read_fixed_bytes` are defined only in `nexigen::core::detail`, and `read_enum` only in `nexigen::core::mandate`

#### Scenario: Enum fields are read through the shared reader

- **WHEN** a manifest or config deserializer reads an enum-typed field of a JSON object
- **THEN** it obtains the value through `nexigen::core::mandate::read_enum` rather than a hand-written `find` → `is_string` → `from_string` chain
