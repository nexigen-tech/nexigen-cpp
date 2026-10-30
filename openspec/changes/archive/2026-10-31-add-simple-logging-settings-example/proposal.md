# Proposal

## Why

The existing `ex_logging_settings` demonstrates the full loader surface (temporary files, environment overrides, recursive output verification), which is more than a newcomer needs to understand the basic flow. A minimal example that only writes two small JSON files, loads them, initializes a logger and logs one line removes that cognitive overhead and makes the first-contact path obvious.

## What Changes

- Add `examples/logging/ex_logging_settings_simple.cpp`: a short self-contained program that
  - writes a minimal global JSON and a minimal local JSON into a temporary directory it creates,
  - calls `load_logging_settings()` with those two explicit paths (no environment variables),
  - prints any diagnostics and fails if the load reports an error,
  - initializes a `FileLogger` through `initialize(const Logging_settings&)`,
  - logs a couple of records, flushes, and exits 0.
- Register `ex_logging_settings_simple` in `examples/logging/CMakeLists.txt` (built by default, CTest `examples` label).
- Update `README.md`: add the executable to the runnable-examples list and describe the minimal flow.
- Update the `examples` capability catalogue to include both `ex_logging_settings` and the new `ex_logging_settings_simple`, correcting the catalog drift left when `ex_logging_settings` was added.

## Capabilities

### New Capabilities
None.

### Modified Capabilities

- `logging/logging-settings`: adds a runnable simple example requirement (temporary global/local files, no environment, initialize → log → flush → exit 0).
- `examples`: updates the runnable example catalogue and README invocation requirement to include the two logging-settings examples (fourteen executables total).

## Impact

- **Code:** new `examples/logging/ex_logging_settings_simple.cpp`, one line in `examples/logging/CMakeLists.txt`, README updates.
- **APIs:** none; uses only the existing public `logging_settings.hpp` and `FileLogger::initialize(const Logging_settings&)` surfaces.
- **Performance:** none on any hot path. The example runs once, is not linked into libraries, and performs only cold-path file I/O, JSON parsing and logger initialization. No allocation or latency impact on producer/worker paths; the example is excluded when `NEXIGEN_BUILD_EXAMPLES=OFF`.
