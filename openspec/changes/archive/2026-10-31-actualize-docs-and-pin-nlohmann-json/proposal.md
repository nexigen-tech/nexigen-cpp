# Proposal

## Why
The documentation and build dependency declarations have drifted from the actual project state. `docs/architecture.md` states "25 capabilities / 160 requirements / 387 scenarios", but the OpenSpec corpus has grown to 43 capabilities, and the module map omits `nexigen::tracing` and `nexigen::diagnostics`. Separately, the nlohmann::json FetchContent pin sits on v3.11.3 while a newer stable release (v3.12.0) is available.

## What Changes
- Pin nlohmann::json to v3.12.0 (CMakeLists.txt FetchContent URL + README mention).
- Update `docs/architecture.md` to report the current capability corpus: 43 capabilities / 271 requirements / 671 scenarios.
- Add the `nexigen::tracing` and `nexigen::diagnostics` rows to the architecture module-map table.
- Update the README serialization-contract note that mentions the pinned json version.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
(none)

## Impact
- **Performance/latency**: none — documentation and a header-only dependency version bump only; no production code path changes.
- **Memory allocation**: none.
- **Code/dependencies**: `CMakeLists.txt` (FetchContent URL), `README.md`, `docs/architecture.md`. No API, behavior, or ABI changes.