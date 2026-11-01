# Proposal

## Why

After P4 the quality gates are strong but verification still stops at
example-driven HTTP tests and filesystem fault simulation:

- The HTTP parser has a fixed-seed malformed-request corpus, but no
  coverage-guided fuzzer. Parser regressions that need unusual byte
  combinations can still slip through.
- Remaining platform/fork failure branches (`pipe2`, `fork`, `/dev/null`,
  `socket`, `bind`, `epoll_create1`, `eventfd`) cannot be reached without fault
  injection, so they stay uncovered and untested.
- `.clang-tidy` currently excludes all opt-in analyzers because the padding
  check is noisy for intentionally aligned hot-path classes.

P5 closes those gaps without changing runtime behaviour.

## What Changes

- **Fuzzable parser:** extract the pure HTTP helpers (`url_decode`,
  case-insensitive header lookup, query parsing, request-line validation) into
  `include/nexigen/hub/detail/http_parse.hpp`; the server uses them directly.
- **libFuzzer harness:** add `tests/fuzz/http_parse_fuzz.cpp`, a seed corpus,
  and the `NEXIGEN_BUILD_FUZZERS` CMake option (Clang only). CI runs a 20 000
  iteration smoke fuzz in the Clang job.
- **Fault injection:** add the test-only
  `core::detail::fault_injection` seam and `NEXIGEN_ENABLE_FAULT_INJECTION`.
  Runner and hub start paths consult injectable cold-path points; Debug and
  coverage CI builds enable the option.
- **Analyzer depth:** enable `clang-analyzer-optin.cplusplus.*` and
  `clang-analyzer-optin.portability.*` while keeping the noisy padding check
  documented as disabled.
- **Tests:** add fault-injection tests for runner start and hub listen setup.
- **Docs:** record P5 completion notes and update the verification matrix.

No production build defaults, public API, wire format, or hot-path behaviour
changes.

## Capabilities

### New Capabilities

None. This is a verification-tooling change; it opts out of spec deltas with
`skip_specs: true`.

### Modified Capabilities

None.

## Impact

- **Code:** `src/hub/http_server.cpp` now delegates to the pure parser header;
  runner/hub start paths gain inert-by-default fault checks.
- **Tests/tooling:** `tests/fuzz/`, `tests/runner/runner_test.cpp`,
  `tests/hub/hub_test.cpp`, `CMakeLists.txt`, `.clang-tidy`,
  `.github/workflows/ci.yml`.
- **Docs:** `README.md`, `docs/architecture.md`, `docs/project-analysis.md`.
- **Performance / latency / memory:** none. Fault checks compile to constant
  `false` unless the test-only option is enabled, and only cold-path start
  functions consult them. The extracted parser is the same code moved to a
  header.
- **Compatibility:** no API or format change; fuzzing and fault injection are
  opt-in build modes.
