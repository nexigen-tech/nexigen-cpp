# Tasks

## 1. XML reader

- [x] 1.1 Add `ManifestFormat` with the full conversion set.
- [x] 1.2 Add the bounded XML reader (`XmlNode`, nesting, text, comments, entities, namespaces, hostile input).
- [x] 1.3 Add valid/hostile XML reader tests.

## 2. ARXML profile and accessor

- [x] 2.1 Add `ManifestAccessor::detect`/`load` and `load_suite_manifest_auto`.
- [x] 2.2 Map `<SUITE-MANIFEST>` to the JSON shape (containers, `CPUS`, `ENV`, `DEPENDENCIES`, numbers/bools).
- [x] 2.3 Reuse `from_json` and `validate_suite_manifest`; keep JSON behavior unchanged.
- [x] 2.4 Add valid ARXML, invalid ARXML, and JSON-regression tests.

## 3. CLI and docs

- [x] 3.1 Use `load_suite_manifest_auto` in `nexigen-exec` and `nexigen-em`.
- [x] 3.2 Document the ARXML profile subset in `docs/architecture.md`.
- [x] 3.3 Run Debug, Release, TSAN, ASAN and gate-OFF; run complexity/naming/result gates and `graphify update .`.
