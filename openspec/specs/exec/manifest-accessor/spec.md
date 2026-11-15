# exec/manifest-accessor Specification

## Purpose
TBD - created by archiving change p3-arxml-manifest-accessor. Update Purpose after archive.

## Requirements

### Requirement: Manifest format vocabulary

`nexigen::exec` SHALL expose a `ManifestFormat` enum with `Json` and `Arxml` and
the full conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json`
under `JSON_NOEXCEPTION=1`, and `std::formatter`).

#### Scenario: Format round-trips

- **WHEN** each `ManifestFormat` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders it

### Requirement: Format detection

`ManifestAccessor::detect(path, content)` SHALL return `Arxml` when the path
ends in `.arxml` or `.xml`, or when the first non-space character is `<`;
otherwise it SHALL return `Json`.

#### Scenario: Extension selects ARXML

- **WHEN** the path ends in `.arxml`
- **THEN** detection reports `Arxml`

#### Scenario: JSON remains the default

- **WHEN** the path ends in `.json` and the content starts with `{`
- **THEN** detection reports `Json`

### Requirement: Minimal XML reader

The accessor SHALL parse a bounded XML document with nested elements, text,
comments, processing instructions, doctype declarations, self-closing tags,
entity decoding, and namespace-prefix stripping. Malformed nesting, an unclosed
tag, or a missing root SHALL fail with `InvalidArgument`.

#### Scenario: Nested elements parse

- **WHEN** the document contains nested elements with text
- **THEN** the reader returns a tree with the same names and text

#### Scenario: Malformed document is rejected

- **WHEN** a closing tag does not match its opening tag
- **THEN** loading fails with `InvalidArgument`

### Requirement: ARXML execution-manifest profile

The accessor SHALL map a `<SUITE-MANIFEST>` document to the existing
`SuiteManifest` JSON shape: tags become lower_snake keys, container tags become
arrays, `CPUS` becomes a number array, `ENV` becomes key/value entries, and
`DEPENDENCIES` becomes process/state objects. The mapped document SHALL pass
through `from_json`, `validate_suite_manifest`, and the same runtime path as
JSON.

#### Scenario: ARXML suite loads

- **WHEN** a valid `<SUITE-MANIFEST>` with a Function Group and one application is loaded
- **THEN** the resulting `SuiteManifest` equals the equivalent JSON manifest

#### Scenario: Validation still applies

- **WHEN** the ARXML profile omits a required field or declares an unsafe id
- **THEN** `validate_suite_manifest` rejects the suite

### Requirement: Auto-detected suite load

`load_suite_manifest_auto` SHALL select the JSON or ARXML path from
`ManifestAccessor::detect` and SHALL be used by `nexigen-exec` and
`nexigen-em` for `--config`.

#### Scenario: CLI accepts either format

- **WHEN** `--config` points at a valid JSON or ARXML manifest
- **THEN** the tool starts the suite with identical behavior

### Requirement: Machine Manifest loading

`ManifestAccessor` SHALL load a Machine Manifest from JSON or from the ARXML
`<MACHINE-MANIFEST>` profile, resolve its references, validate it, and return
the normalized model. Malformed content SHALL fail with `InvalidArgument`;
unreadable files SHALL fail with `SystemError`.

#### Scenario: ARXML machine loads

- **WHEN** a valid `<MACHINE-MANIFEST>` is loaded
- **THEN** the model contains the machine, groups, processes, and resolved Function Groups

#### Scenario: Malformed machine is rejected

- **WHEN** the XML nesting is invalid or a reference cannot be resolved
- **THEN** loading fails with `InvalidArgument`
