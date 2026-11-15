# Proposal

## Why

The project reads its suite manifest only as JSON, while AUTOSAR Adaptive
deployments describe execution and machine manifests in ARXML. A deployment
cannot point `nexigen-exec`/`nexigen-em` at an ARXML manifest, and there is no
single accessor that hides the source format from the rest of the EM.

## What Changes

- Add `ManifestFormat` (`Json`, `Arxml`) and `ManifestAccessor`, which detects
  the source format and loads a normalized `SuiteManifest`.
- Add a minimal ARXML execution-manifest profile: a namespaced XML document
  with the same fields as the JSON schema, mapped through the existing
  `from_json`/validation path.
- Add a small self-contained XML reader (elements, nested children, text,
  comments, processing instructions, entity decoding) with no external
  dependency.
- Add `load_suite_manifest_auto` and use it in `nexigen-exec`/`nexigen-em`, so
  `--config` accepts either `manifest.json` or `manifest.arxml`.

## Capabilities

### New Capabilities

- `exec/manifest-accessor`: format detection, the XML reader, the ARXML profile,
  and the normalized load API.

### Modified Capabilities

- `exec/app-suite`: the suite manifest may be loaded from the ARXML profile as
  well as JSON, with identical validation and runtime behavior.

## Impact

- **Performance:** cold-path file parsing only; the XML reader allocates the
  document tree once before the suite starts.
- **Compatibility:** JSON remains the default and is byte-for-byte unchanged.
- **Scope:** the ARXML profile is intentionally a documented subset of the
  AUTOSAR execution manifest, not a full XSD implementation. Machine manifests
  and reference resolution are follow-ups.
