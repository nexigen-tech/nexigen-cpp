# Proposal

## Why

The `Complexity` CI job (`cmake --build --target complexity-check`, CCN ≤ 15) fails today: `cmake/check_complexity.py` reports 12 functions above the threshold — 5 production JSON deserializers in `nexigen::com`, `nexigen::exec`, `nexigen::logging`, `nexigen::phm` and 7 test suites. At the same time the gate does not scan `tools/`, where four daemon entry points sit at CCN 32–34, so the project's worst hotspots are invisible to the check that is supposed to catch them.

All production hotspots are cold-path JSON deserialization that repeats the same field-probing boilerplate (`is_object` → `find` → `is_<type>` → `get`) across ~55 sites, and the bulk of that code lives `inline` in headers of STATIC modules, so every consumer translation unit recompiles it. Fixing the gate therefore also removes duplicated parsing code, moves cold-path serialization bodies to compiled TUs, and brings the four daemons under the same structural contract.

## What Changes

- **Shared JSON field vocabulary (cold path).** Introduce a small set of non-throwing helpers for probing/reading object fields (string, signed/unsigned integer with range clamp, bool, array-of-object) under `-fno-exceptions`, and use them in every manifest/config deserializer instead of per-file lambdas and hand-written `is_*`/`get` chains.
- **Decompose the five over-threshold production deserializers** (`com::from_json` 22, `logging::from_json` in `iceoryx2_logger_config.hpp` 19, `logging::from_json` in `log_frame.hpp` 17, `exec::from_json` 17, `phm::from_json` 17) into per-field and per-sub-object parsers, following the split already used in `src/tracing/record_format.cpp`.
- **Move eligible cold-path serialization bodies out of STATIC-module headers** into `src/<module>/<entity>.cpp`, matching the existing pattern of `src/logging/console_logger_config.cpp`, `file_logger_config.cpp`, `logger_options.cpp` and `src/metrics/snapshot_format.cpp`. Templates, `constexpr` code, hot-path inline functions and the header-only `nexigen::core` INTERFACE target are explicitly exempt.
- **Decompose the four daemon entry points** (`tools/nexigen-sm` `run_sm` 34, `tools/nexigen-phmd` `run_phmd` 33, `tools/nexigen-logd` `main` 33, `tools/nexigen-metricsd` `main` 32) into `parse_args` + `run_daemon` stages, reusing the shape `nexigen-sm`/`nexigen-phmd` already have. CLI surface, diagnostics text and exit codes stay identical.
- **Split the seven over-threshold test functions** (`test_producer` 34, `test_file_logger` 28, `test_latency` ×2 27, `test_stress` 23, `test_composite` 17, `test_method` 16) into scenario-level functions registered under the existing test names, without dropping any assertion.
- **Widen and wire the complexity gate**: add `tools/` to its scope, register it as a CTest entry so it runs with the rest of the suite (not only as an opt-in custom target), and keep the `alignas` false-positive filter.
- No public API, CMake option, JSON field name, CLI flag, log line or test name changes.

## Capabilities

### New Capabilities

- `quality/code-structure`: the enforceable maintainability contract — a CCN ≤ 15 ceiling across `include/`, `src/`, `tests/` and `tools/`, how the gate is executed and what it filters, the rule that cold-path non-template serialization bodies of STATIC modules are compiled once in `src/<module>/`, the test-function ceiling, and the guarantee that complexity refactors (including entry-point decomposition) preserve behavior and CLI contracts.

### Modified Capabilities

- `core/serialization`: adds the project-wide rule that manifest/config `from_json` bodies are expressed with the shared non-throwing field-extraction vocabulary rather than per-file probing code, and that this vocabulary is cold-path only.

## Impact

- **Code:** `include/nexigen/core/` (new detail helper header, INTERFACE target unchanged), `src/com/service_manifest.cpp`, `src/exec/child_process.cpp`, `src/phm/phm_manifest.cpp`, `include/nexigen/logging/{log_frame,iceoryx2_logger_config,log_daemon_config}.hpp` + new `src/logging/*.cpp`, `include/nexigen/metrics/metric_daemon_config.hpp` + new `src/metrics/*.cpp`, `tools/nexigen-{sm,phmd,logd,metricsd}/main.cpp`, seven test files, `cmake/Complexity.cmake`, `cmake/check_complexity.py`, `tests/CMakeLists.txt`, `.github/workflows/ci.yml`.
- **APIs:** unchanged. Public headers keep the same declarations; only where the definition is compiled moves.
- **Performance / latency:** no hot-path change. All touched functions are cold-path configuration/JSON/diagnostic code that the mandates already forbid on producer and worker hot paths. No new `delete`/`new`, no extra allocations, no container growth in runtime loops; parsing keeps the current per-field pass over an already-materialized `nlohmann::json`, so allocation count per document is unchanged. Record/frame layouts, `alignas(64)` alignment and `std::memory_order` choices are untouched, so the latency gates (`build-verify-lat`, `build-verify-tsan`) must report the same numbers.
- **Memory:** negligible code-size reduction per consumer TU (cold-path inline bodies no longer recompiled); no change in runtime footprint.
- **Build:** expected minor reduction in compile time for consumers of `nexigen::logging`/`nexigen::metrics` config headers; no new dependencies.
- **CI:** the `Complexity` job turns green and gains `tools/` coverage; a new CTest entry runs the same gate locally.
- **Out of scope:** `examples/` (the gate deliberately excludes it), and the `analyzer.py`/`cloc` build-directory measurement issue found during the audit.
