# Proposal

## Why

The 2026-11-07 static analysis (cloc + lizard) found five remaining
high-complexity hotspots after the previous complexity reduction pass. Each
exceeds the project target of cyclomatic complexity (CCN) ≤ 15 and makes the
code harder to reason about and test:

- `nexigen::hub::Discovery::scan_suite` (`src/hub/discovery.cpp`, CCN 21)
- `nexigen::logging::detail::open_segment` (`src/logging/rotation.cpp`, CCN 22)
- `nexigen::run` CLI `cmd_start` / `cmd_stop` / `cmd_status`
  (`tools/nexigen-run/main.cpp`, CCN 36 / 23 / 21)
- `nexigen-hub` `main` (`tools/nexigen-hub/main.cpp`, CCN 32)
- Oversized test functions `test_provider`
  (`tests/tracing/tracing_test.cpp`, CCN 50) and
  `test_exponential_histogram_mt` (`tests/metrics/metrics_test.cpp`, CCN 37)

This is a **pure behaviour-preserving refactor**: no public API, on-disk
contract, signal sequence, output format, timing gate, or observable
behaviour changes.

## What Changes

- **hub/discovery**: split `Discovery::scan_suite` into helpers for metadata
  loading, containment/stream derivation, recorded-state resolution, per-app
  instance construction, and suite/app state aggregation. No change to the
  `HubSuite`/`HubApp`/`HubInstance` model or discovery output.
- **logging/rotation**: split `open_segment` into validation, directory
  preparation, segment-name resolution, and file-open helpers. Keep the
  fixed-buffer, allocation-free path-building contract and the exact
  `core::Status`/`errno` semantics.
- **nexigen-run CLI**: move per-verb argument parsing, target resolution, and
  reporting into small helpers / a dispatcher so `cmd_start`, `cmd_stop`, and
  `cmd_status` become thin orchestrators. Preserve every user-facing message,
  exit code, log line, and mutual-exclusivity rule byte-for-byte.
- **nexigen-hub CLI**: split `main` into argument parsing/config struct,
  listen-host classification, discovery/server setup, and run-loop helpers.
  Preserve usage text, warning text, exit codes, and the `--listen` parsing
  rules.
- **Tests**: split `test_provider` and `test_exponential_histogram_mt` into
  focused sub-functions / parameterised sections while keeping identical
  assertions and coverage.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is a pure internal refactor with no spec-level behaviour change, so
the change sets `skip_specs: true`.

## Impact

- **Code**: `src/hub/discovery.cpp`, `src/logging/rotation.cpp`,
  `tools/nexigen-run/main.cpp`, `tools/nexigen-hub/main.cpp`,
  `tests/tracing/tracing_test.cpp`, `tests/metrics/metrics_test.cpp`.
- **Public API**: none. No header gains or loses a symbol.
- **Performance / latency / allocations**: none expected. `scan_suite` and
  `open_segment` stay on cold/consumer paths; `open_segment` remains
  allocation-free and keeps its fixed scratch buffers. All refactored helper
  calls are trivially inlined or cold, so no hot-path latency gate changes.
- **Dependencies / build**: none.
- **Verification**: full Debug, Release, ASan, and TSan matrices plus the
  existing coverage gates must remain green; `clang-format` and lizard targets
  must show every touched function ≤ CCN 15.
