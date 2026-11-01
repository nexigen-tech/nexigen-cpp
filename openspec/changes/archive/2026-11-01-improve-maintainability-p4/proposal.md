# Proposal

## Why

After P3 the project is at **94.2%** line coverage (`runner` 90.6%, `hub`
91.8%), just below the documented **≥95%** project target, and CI still enforces
only the interim 90% overall gate. The project also has no static-analysis
profile: compiler warnings, sanitizers, and the `Result::value()` guard are
enforced, but clang-tidy findings are not part of CI.

This is the P4 quality milestone selected from the remaining backlog: close the
coverage gap and add a reviewable clang-tidy profile.

## What Changes

- **Coverage:** add fault-path tests for the remaining runner/hub branches
  (streaming replacement/read errors, SSE gaps, percent-decoded routes,
  discovery states, runner settings/stream preparation failures, suite metadata
  writes, live-supervisor stops, hostile JSON) until overall coverage is above
  95%.
- **CI gate:** raise the coverage job from 90% to the **95%** project gate while
  keeping the 90% runner/hub module gates.
- **clang-tidy:** add `.clang-tidy` with `bugprone-*`, `performance-*`,
  `portability-*`, and non-opt-in `clang-analyzer-*` checks, warnings as errors,
  and documented exclusions for noisy checks. Run it on every `src/*.cpp`
  translation unit in the Clang CI job.
- **Finding fixes:** enum underlying sizes, widening-cast ordering, explicit
  multi-level pointer conversion, a vector reserve, optional-access guards, and
  documented `string_view::data()` exceptions.
- **Docs:** record P4 completion notes and update the coverage/verification
  documentation.

No runtime behaviour, public API, wire format, or build default changes.

## Capabilities

### New Capabilities

None. This is a verification and static-analysis change; it opts out of spec
deltas with `skip_specs: true`.

### Modified Capabilities

None.

## Impact

- **Code:** small cold-path fixes in `src/app`, `src/chrono`, `src/logging`,
  `src/metrics`, `src/tracing`, and includes for `.clang-tidy` findings.
- **Tests:** `tests/hub/hub_test.cpp`, `tests/runner/runner_test.cpp`,
  `tests/runner/app_suite_test.cpp`.
- **Tooling:** `.clang-tidy`, `.github/workflows/ci.yml`.
- **Docs:** `README.md`, `docs/architecture.md`, `docs/project-analysis.md`.
- **Performance / latency / memory:** none. Touched code is cold-path control
  plane or test code; no hot-path allocation or memory-layout change. The enum
  size changes are internal cold-path enums.
- **Compatibility:** no API or format change; clang-tidy is CI-only.
