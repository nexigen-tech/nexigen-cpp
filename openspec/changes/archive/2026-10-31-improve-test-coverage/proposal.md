# Proposal

## Why

Current unit-test line coverage is **90.5%** (5 772 / 6 381). Reaching 95% requires covering **290 more lines** of the 609 uncovered lines. The uncovered inventory is well understood and concentrated in cold-path settings loaders, JSON writers, sink I/O error handling, and App failure branches — not in the wait-free hot paths. This change plans a measurable path to **≥96% overall** with explicit per-file budgets and a hard stop if a target would require production changes.

## What Changes

- **Tests only.** No production code, APIs, build flags, or runtime behaviour changes. `.openspec.yaml` keeps `skip_specs: true`.
- Cover the identified uncovered lines with behaviour-driven tests in the existing single-TU suites. No new test executables or dependencies.
- Add a coverage budget and per-file targets (below), measured with the existing `coverage` target.
- If a target cannot be reached without artificial tests or production hooks, stop and report the remaining unreachable lines instead of faking coverage.

### Coverage budget

Baseline: 5 772 / 6 381 covered, 609 uncovered. 95% = 6 062 covered (+290). Plan targets **≥96%** (≥6 126 covered, +354), with margin.

| File | Current | Uncovered | Planned cover | Target |
|---|---:|---:|---:|---:|
| `src/logging/logging_settings.cpp` | 306/432 (70.8%) | 126 | +110 | ≥96% |
| `src/app/app_settings.cpp` | 236/339 (69.6%) | 103 | +85 | ≥94% |
| `src/metrics/json_writer.cpp` | 38/58 (65.5%) | 20 | +18 | ≥96% |
| `src/tracing/json_writer.cpp` | 41/66 (62.1%) | 25 | +20 | ≥92% |
| `include/.../json_file_metric_sink.hpp` | 93/133 (69.9%) | 40 | +30 | ≥92% |
| `include/.../console_metric_sink.hpp` | 80/112 (71.4%) | 32 | +26 | ≥94% |
| `include/nexigen/app/app.hpp` | 135/150 (90.0%) | 15 | +10 | ≥96% |
| `src/metrics/snapshot_format.cpp` | 304/352 (86.4%) | 48 | +30 | ≥94% |
| `include/.../spam_protection.hpp` | 32/46 (69.6%) | 14 | +11 | ≥93% |
| `include/nexigen/chrono/stamp_source.hpp` | 26/35 (74.3%) | 9 | +6 | ≥91% |
| `src/metrics/provider.cpp` | 239/255 (93.7%) | 16 | +8 | ≥96% |
| `src/tracing/record_format.cpp` | 330/352 (93.8%) | 22 | +12 | ≥97% |
| `include/.../chrome_trace_sink.hpp` | 96/111 (86.5%) | 15 | +10 | ≥95% |
| `src/logging/rotation.cpp` | 105/119 (88.2%) | 14 | +8 | ≥95% |
| `include/nexigen/logging/file_logger.hpp` | 174/185 (94.1%) | 11 | +6 | ≥97% |
| `src/app/app_stop_reason.hpp` | 23/24 (95.8%) | 1 | +1 | 100% |

Planned additions sum to ~391 lines, which lands overall at ~96.6% even if a few cases are platform-dependent.

### Unreachable-line policy

Defensive `default:` arms, platform-specific TSan/ASan-only branches, and failure paths that cannot be triggered through public APIs are documented in the coverage report section rather than forced with `#ifdef`-only tests or production test hooks. If the remaining budget makes 95% impossible, stop and propose a separate production/refactor change.

## Capabilities

### New Capabilities
None — test-only change; `skip_specs: true` is declared in `.openspec.yaml`.

### Modified Capabilities
None — no observable behaviour changes; existing capability requirements gain test evidence.

## Impact

- **Code:** additions to `tests/logging/logging_test.cpp`, `tests/app/app_test.cpp`, `tests/metrics/metrics_test.cpp`, `tests/tracing/tracing_test.cpp`, `tests/chrono/chrono_test.cpp`; small test-only helpers where needed.
- **Build:** unchanged targets and dependencies; the existing `coverage` target is the verification loop. `NEXIGEN_COVERAGE_FAIL_UNDER=95` is documented as the project expectation, not forced in CMake.
- **Performance/memory:** none in production; test binaries grow in size/runtime only. The metrics suite is already the slowest under coverage (~58 s).
- **Risks:** timing flakiness in signal/latency tests and test-runtime growth. Mitigation: reuse existing deterministic margins, keep cases table-driven, and avoid heavy loops.
