# Proposal

## Why

The P3 findings in `docs/project-analysis.md` are maintainability and
robustness debts rather than user-visible defects:

- Three control-plane functions are long enough that their phases are hard to
  review independently: `Runner::start` (~336 lines), `run_suite_supervisor`
  (~209), and `Http_server::handle_request` (~143).
- The HTTP parser has malformed-request coverage but no randomized stress test,
  so parser regressions that only appear on unusual byte sequences can slip
  through.
- The `Result::value()` ban is enforced only by convention; nothing catches a
  new call in CI.
- Two safe-but-noisy conversion sites and one missing `reserve` were flagged by
  static analysis.

This change pays down those debts without changing runtime behaviour, public
APIs, wire formats, or build defaults.

## What Changes

- **Function structure:** split `Runner::start` into validation, preparation,
  and supervisor phases; split the suite supervisor loop into stop, restart,
  and per-app step helpers; split HTTP request handling into route handlers.
- **Parser robustness:** add a deterministic malformed-request corpus test that
  drives the live HTTP server with pseudo-random byte sequences and bounded
  inputs.
- **Convention guard:** add `cmake/check_result_value.py` and a CI step that
  rejects `.value()` calls on `core::Result`/`Status` receivers. Existing
  non-Result `.value()` calls (span IDs, nlohmann JSON) stay allowed through an
  explicit receiver allowlist.
- **Static-analysis nits:** cast constant products to `std::size_t` before
  multiplication and reserve the standalone-instance vector.
- **Docs:** record the P3 completion notes and update the verification matrix.

No new dependency, no new CMake option, and no behavioural change.

## Capabilities

### New Capabilities

None. This is a pure refactor, test, and CI-tooling change; it opts out of spec
deltas with `skip_specs: true`.

### Modified Capabilities

None.

## Impact

- **Code:** `src/runner/runner.cpp`, `src/runner/app_suite.cpp`,
  `src/hub/http_server.cpp`, `src/hub/discovery.cpp`,
  `include/nexigen/hub/streaming.hpp`.
- **Tests:** `tests/hub/hub_test.cpp`, `tests/runner/*_test.cpp` as needed for
  refactor verification.
- **Tooling:** `cmake/check_result_value.py`, `.github/workflows/ci.yml`.
- **Docs:** `README.md`, `docs/architecture.md`, `docs/project-analysis.md`.
- **Performance / latency / memory:** none. All touched code is cold-path
  control-plane code; no hot-path function, allocation, or memory layout
  changes. Splitting functions may change inlining decisions, so the Release
  latency gates remain part of verification.
- **Compatibility:** no public header removal or signature change; no format or
  option changes.
