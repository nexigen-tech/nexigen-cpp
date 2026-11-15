# Design

## Context

`SuiteManifest` already has a complete `from_json`/validation path. ARXML can
reuse it if the XML document is converted to the same JSON object shape instead
of duplicating every field parser.

## Goals / Non-Goals

**Goals:**

- One `ManifestAccessor` for JSON and the ARXML profile.
- A dependency-free XML reader for the profile.
- Identical validation and runtime behavior after load.
- CLI auto-detection for `--config`.

**Non-Goals:**

- Full AUTOSAR XSD schema, reference resolution, or `MachineManifest`.
- XML attributes as first-class configuration (ignored).
- XPath, XSD validation, or namespace-aware semantics beyond prefix stripping.
- Replacing JSON as the internal normalized format.

## Decisions

### D1 — Format detection

`ManifestAccessor::detect(path, content)` returns `Arxml` when the extension is
`.arxml`/`.xml` or the first non-space character is `<`; otherwise `Json`.
JSON loading delegates to the existing `load_suite_manifest`, so its
pre-validation and error behavior stay unchanged.

### D2 — Minimal XML reader

`detail::XmlNode { name, text, children }`; attributes are parsed and skipped.
The reader handles `<?...?>`, `<!--...-->`, `<!DOCTYPE...>`, self-closing tags,
nested elements, entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`,
numeric entities), and namespace prefixes (`ns:NAME` becomes `NAME`). Malformed
nesting or unclosed tags return `InvalidArgument`.

### D3 — ARXML profile

The root is `<SUITE-MANIFEST>`; child tags map to JSON keys by lowercasing and
replacing `-` with `_`. Array containers are:

`FUNCTION-GROUPS`, `APPLICATIONS`, `STARTUP-CONFIGS`, `ARGS`, `STATES`, `CPUS`,
`DEPENDENCIES`, `RESOURCE-GROUPS`, `FUNCTION-GROUP-PORT-MAPPING`, `ENV`.

`CPUS` items are numbers; `ENV` items are `ENTRY` objects with `KEY`/`VALUE`;
`DEPENDENCY` items contain `PROCESS`/`STATE`; every other container yields an
array of objects. Numeric keys (`version`, timeouts, attempt counts, memory
limit, priority) and boolean keys (`reporting`,
`require_executable_digest`) are converted to JSON numbers/booleans.

The result is fed to `SuiteManifest::from_json` and
`validate_suite_manifest`, so ARXML and JSON share every validation rule.

### D4 — CLI integration

`load_suite_manifest_auto` is the new entry point; `nexigen-exec` and
`nexigen-em` use it for `--config`. Direct library callers may keep
`load_suite_manifest` for strict JSON.

### D5 — No hot-path or concurrency impact

Parsing is cold path and happens before any process starts. No atomics, locks,
or shard state are introduced.

## Risks / Trade-offs

- **Profile drift** → the profile is documented as a subset; unknown tags are
  ignored by the generic mapping, and validation catches missing required
  fields.
- **Attributes ignored** → the profile uses elements only; ARXML documents that
  put configuration in attributes are rejected as missing fields.
- **XML parser bugs** → covered by valid/hostile tests; the reader is small and
  strictly bounded by the file content.

## Migration Plan

1. Add the XML reader and profile conversion.
2. Add `ManifestAccessor`/`load_suite_manifest_auto` and CLI wiring.
3. Add valid/hostile ARXML tests and a JSON-regression test.
4. Document the profile and run the matrix.

## Open Questions

None blocking. Machine Manifest and reference resolution are separate changes.
