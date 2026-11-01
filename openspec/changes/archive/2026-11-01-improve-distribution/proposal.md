# Proposal

## Why

The P1 findings in `docs/project-analysis.md` block the project from being usable
as an ecosystem backbone:

- The project claims Clang 18+ support, but a Clang build fails under the active
  `-Werror` policy on four diagnostics. GCC is therefore the only verified
  compiler, and CI only exercises GCC.
- Documentation has drifted: the module map and README omit `tracing`, `runner`,
  and `hub`; the README still promises capabilities the code does not have and
  says "all three modes" for a four-configuration matrix.
- There are zero `install(` rules. `cmake --install` does nothing, there is no
  exported target set, no `nexigenConfig.cmake`, and no version file, so no
  external project can consume the library.

This change makes the library buildable with Clang, consumable through
`find_package(nexigen CONFIG)`, and documents all nine modules accurately.

## What Changes

- **Clang cleanliness:** fix the four `-Werror` diagnostics without weakening
  warning flags, and add a Clang job to CI.
- **Packaging:** add `install(TARGETS ...)`, public-header installation, an
  export set with the `nexigen::` namespace, `nexigenConfig.cmake`, and a
  version file generated from the project version. Install `nexigen-run` and
  `nexigen-hub` when tools are built.
- **Documentation:** add `tracing`, `runner`, and `hub` sections to `README.md`,
  add an install/consumption section, refresh the architecture page, and remove
  stale claims.
- **Verification:** add an install-tree smoke test that builds a tiny external
  consumer with `find_package(nexigen CONFIG REQUIRED)`.

No production source, public API, compiler flag, or runtime behaviour changes.
The one example edit only calls an existing `to_string` free function that was
already part of the demonstrated contract.

## Capabilities

### New Capabilities

- `packaging` — install rules, exported targets, package configuration, version
  compatibility, and transitive dependency discovery.

### Modified Capabilities

None — the runtime capabilities are unchanged.

## Impact

- **Code:** `CMakeLists.txt`, `cmake/nexigenConfig.cmake.in`,
  `include/nexigen/metrics/{meter,null_metric_sink}.hpp`,
  `tests/metrics/metrics_test.cpp`, `examples/core/ex_spsc_ring_buffer.cpp`,
  `.github/workflows/ci.yml`, `tests/packaging/consumer/`.
- **Docs:** `README.md`, `docs/architecture.md`, `docs/project-analysis.md`.
- **Performance / latency / memory:** none. Only build metadata, install rules,
  and cold-path diagnostics change.
- **Consumers:** an installed `nexigen` package now requires `Threads` and
  `nlohmann_json` to be discoverable by `find_package`; this is declared through
  `find_dependency` in the package config.
