# Proposal

## Why

The `extract-child-process-class` change moved the suite-manifest / suite-control /
suite-reporting / suite-supervisor implementation out of `src/exec/app_suite.cpp` into
dedicated translation units, but **left the originals behind**: `app_suite.cpp` (1718
lines) still carries its own copies of everything that was moved.

Measured on the current tree (`nm -C --defined-only` over `nexigen_exec`'s objects,
strong `T`/`D`/`B` symbols only — weak inline/template copies excluded):

- **44 strong global symbols are defined in more than one translation unit** —
  32 in `app_suite.cpp` + `suite_manifest.cpp`, 11 in `app_suite.cpp` +
  `suite_control.cpp`, 1 (`suite_status`) in `app_suite.cpp` + `suite_reporting.cpp`.
  That is an ODR violation: two definitions of the same entity with external
  linkage. It is currently *latent* only because `nexigen_exec` is a static
  library — the linker pulls archive members on demand, so no final binary
  happens to need a symbol from both members. Any change that makes a binary
  reference a symbol present in both objects turns this into a hard
  `multiple definition` link error.
- The duplication detector (custom: comment/literal stripping, whitespace
  normalization, rolling-hash over 6-line windows, overlap merge) measures
  **≈1300 duplicated lines** between `app_suite.cpp` and the four extracted
  units: 711 (`suite_manifest.cpp`), 302 (`suite_supervisor.cpp`), 177
  (`suite_control.cpp`), 113 (`suite_reporting.cpp`).
- **Four of the copies have drifted**: `SuiteManifest::to_string` (118 vs 124
  instructions), `load_suite_manifest` (222 vs 221), `stop_suite` (369 vs 383),
  `suite_status` (447 vs 446). The two sides of these are no longer the same
  function, so "delete the duplicate" is not mechanical for them — they need an
  explicit decision about which implementation is canonical.

The result is a module whose ownership is ambiguous: a reader cannot tell which
of the two copies is authoritative, and the build cannot prove it either.

## What Changes

- **Reconcile the four drifted pairs** (`SuiteManifest::to_string`,
  `load_suite_manifest`, `stop_suite`, `suite_status`): determine the intended
  behavior, keep exactly one implementation, and make the other side call it.
- **Delete the 40 identical duplicate definitions** from `src/exec/app_suite.cpp`
  (manifest serialization trio, `valid_json_*` validators, path helpers,
  `load_suite_manifest` / `validate_suite_manifest`, the suite control surface
  `start_suite` / `stop_suite` / `stop_suite_apps` / `suite_is_running` /
  `preflight_suite_threads` / `run_suite_supervisor_child` /
  `signal_supervisor_with_grace` / `create_suite_pipes` / `effective_stop_grace` /
  `wait_for_suite_exit` / `write_stop_request`, and `suite_status`).
- **Establish one owner per entity**: `suite_manifest.cpp` owns the manifest
  vocabulary and its serialization, `suite_control.cpp` owns the suite control
  surface, `suite_reporting.cpp` owns status reporting, `suite_supervisor.cpp`
  owns the supervisor loop; `app_suite.cpp` keeps only the application-instance
  lifecycle it actually owns.
- No public header changes, no signature changes, no wire-format or vocabulary
  changes: the same symbols with the same declarations keep existing in the same
  headers (`include/nexigen/exec/suite_manifest.hpp`,
  `suite_control.hpp`, `suite_status.hpp`).
- The `detail::` helpers are **not** in scope: they are `inline` in
  `include/nexigen/exec/detail/sys_helpers.hpp` (the `.cpp` files only carry
  `using detail::…;`), so their repeated weak symbols are legal and carry no
  source duplication.

## Capabilities

Pure refactor — code ownership inside one module. No observable behavior
change, no spec-level requirement moves. `skip_specs: true`.

## Impact

- **Runtime / latency (ns) / memory-allocation impact: none.** The change
  removes duplicate *definitions*, not code paths: every symbol keeps exactly
  one definition and is called exactly as before. No header is added or
  removed, no function signature changes, no branch or data layout changes. The
  linker sees fewer objects pulled into the archive members — strictly less
  code in the binary, no extra work at runtime. The four reconciled symbols
  are behavior-neutral by construction: the task list requires that the
  surviving implementation reproduces the currently-effective behavior, proven
  by the existing suite.
- **Build**: `nexigen_exec` shrinks by ≈1300 source lines and the archive
  members stop colliding; the full 4-config matrix must stay 37/37.
- **Risk**: the four drifted pairs are the only behavior-sensitive part of the
  change; they are handled first, one pair at a time, each with its own
  verification, before any bulk deletion.
