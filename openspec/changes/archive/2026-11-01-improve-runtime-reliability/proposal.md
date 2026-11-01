# Proposal

## Why

The P2 findings in `docs/project-analysis.md` leave the runtime modules less
trustworthy than the rest of the library:

- Measured runner coverage is 58.8% and hub coverage is 80.8%. Most of the
  missing runner lines execute inside forked supervisors whose gcov counters
  are discarded by `_exit`, so the number understates the tested behaviour and
  hides genuinely untested branches.
- Control-plane failures are silently discarded: hub rescan/sync errors,
  runner state/exit writes, and suite status writes can fail without a single
  diagnostic. For an observability tool this is worse than failing loudly.
- `Last-Event-ID` is matched against two hard-coded case variants even though
  HTTP header names are case-insensitive, and malformed-request handling has
  no regression coverage.
- Release binaries use `-march=native`, so they cannot be distributed, and the
  `nlohmann/json` FetchContent download has no content hash, so a rebuilt
  dependency cannot be verified.

## What Changes

- **Coverage:** make gcov see work performed in forked supervisors and child
  processes, add runner/hub tests for the uncovered error and HTTP edge paths,
  and raise the runner and hub module gates to at least 90%.
- **Control-plane diagnostics:** report hub rescan/sync failures through a
  diagnostic sink, and report runner instance/suite state-write failures as
  runner warnings instead of discarding the `core::Status`.
- **HTTP edges:** parse request headers properly, compare header names
  case-insensitively and allow optional whitespace around the
  `Last-Event-ID` value; extend the HTTP suite with malformed request lines,
  versions, oversized inputs, invalid cursor/limit values, and non-GET methods.
- **Reproducible builds:** pin the `nlohmann/json` archive with a SHA-256
  `URL_HASH`.
- **Portable Release:** add `NEXIGEN_NATIVE_ARCH` (default `ON`); setting it to
  `OFF` removes `-march=native`/`-mtune=native` while keeping the `-O3` and LTO
  Release pipeline.
- **Docs and gates:** document the new option, refresh the analysis notes, and
  raise the CI coverage gate from the interim 85% to the newly achieved level.

No public API, wire format, hot-path code, or application-facing behaviour
changes. The only new option and diagnostic sink are cold-path control-plane
surfaces.

## Capabilities

### New Capabilities

- `quality/coverage`: how line coverage is measured across fork boundaries and
  the minimum per-module coverage gates for runner and hub.
- `quality/reproducible-builds`: content-hash pinning of downloaded
  dependencies and the portable Release architecture mode.

### Modified Capabilities

- `hub/http-api`: case-insensitive header handling, tolerant cursor parsing,
  consistent malformed-request behaviour, and reported control-plane refresh
  failures.
- `runner/logging`: state and exit write failures are reported through the
  runner warning channel.

## Impact

- **Code:** `src/hub/http_server.cpp`, `include/nexigen/hub/http_server.hpp`,
  `src/hub/streaming.cpp`, `include/nexigen/hub/streaming.hpp`,
  `src/runner/runner.cpp`, `src/runner/app_suite.cpp`,
  `src/runner/coverage_flush.hpp`, `tools/nexigen-hub/main.cpp`,
  `CMakeLists.txt`, `cmake/Optimizations.cmake`,
  `tests/hub/hub_test.cpp`, `tests/runner/*_test.cpp`.
- **Build metadata:** a new default-ON CMake option and a FetchContent hash.
  Existing build commands keep working unchanged.
- **Docs:** `README.md`, `docs/architecture.md`, `docs/project-analysis.md`.
- **Performance / latency / memory:** none on tick, producer, snapshot, sink, or
  HTTP serving paths. Coverage flushing exists only in coverage builds and is
  an empty inline call otherwise; diagnostics run once per control-loop
  refresh or on an already-failing cold write.
- **Compatibility:** no library API removal. `Stream_manager::sync` gains a
  status result and `Http_server` gains an optional diagnostic sink; existing
  call sites continue to compile through defaults.
