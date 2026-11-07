# Proposal

## Why

The 2026-11-07 `cpp-project-analyzer` run (cloc + lizard) shows that the
production surface is now fully under the project's `CCN ≤ 15` target, but
14 test functions remain above it:

| CCN | Function | File |
|---:|---|---|
| 38 | `test_sinks` | `tests/tracing/tracing_test.cpp` |
| 37 | `test_command_line_parser` | `tests/app/app_test.cpp` |
| 25 | `test_http_server` | `tests/hub/hub_test.cpp` |
| 24 | `test_explicit_histogram_mt` | `tests/metrics/metrics_test.cpp` |
| 23 | `test_exponential_histogram` | `tests/metrics/metrics_test.cpp` |
| 22 | `test_latency` | `tests/metrics/metrics_test.cpp` |
| 22 | `test_latency` | `tests/tracing/tracing_test.cpp` |
| 19 | `test_serialization` | `tests/metrics/metrics_test.cpp` |
| 19 | `test_sinks_json` | `tests/metrics/metrics_test.cpp` |
| 19 | `test_scoped_span` | `tests/tracing/tracing_test.cpp` |
| 17 | `test_provider` | `tests/chrono/chrono_test.cpp` |
| 17 | `test_sink_io` | `tests/core/core_test.cpp` |
| 17 | `test_provider_harvester` | `tests/metrics/metrics_test.cpp` |
| 16 | `test_ids_and_context` | `tests/tracing/tracing_test.cpp` |

This change brings the whole test surface under the target so a `lizard -C 15`
gate can later cover tests as well as production code.

This is a **pure behaviour-preserving test decomposition**: no assertion,
printed output, execution order, timing window, thread slot, or coverage
behaviour changes. No production code is touched.

## What Changes

- Split each of the 14 oversized test bodies into focused `static`-linkage
  sub-functions placed inside the test TU's existing anonymous namespace and
  called from the test function in the original statement order. Shared
  fixtures, helpers, and counters stay where they are; state that crosses a
  block boundary is passed as a parameter.
- The suites registered in `main()`, their names, and their order stay
  identical; only the per-suite function bodies get decomposed.
- Every existing `NX_CHECK`, `std::printf`, and side effect is moved verbatim
  into one of the sub-functions — same text, same order.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. Test-only internal refactor; the change sets `skip_specs: true`.

## Impact

- **Code**: `tests/tracing/tracing_test.cpp`, `tests/app/app_test.cpp`,
  `tests/hub/hub_test.cpp`, `tests/metrics/metrics_test.cpp`,
  `tests/chrono/chrono_test.cpp`, `tests/core/core_test.cpp`.
- **Public API**: none (test TUs only; anonymous namespace, no new external
  symbols).
- **Production code**: untouched.
- **Performance / behaviour**: none. Test-side decomposition only; the
  measured latency suites keep their exact measurement loops and prints.
- **Verification**: full Debug, Release, ASan, and TSan matrices plus the
  existing coverage gates; `clang-format` clean; lizard shows every touched
  function ≤ CCN 15.
