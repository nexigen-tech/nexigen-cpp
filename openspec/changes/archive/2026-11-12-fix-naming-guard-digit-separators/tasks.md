# Tasks

## 1. Fix the stripper in both guards

- [x] 1.1 Add the digit-separator rule to `cmake/check_naming.py` and `cmake/check_result_value.py` (apostrophe between two hex digits is skipped, everything else keeps the literal branch). Verify `python3 cmake/check_naming.py` now reports `Capture_sink` and `Stress_provider`.
- [x] 1.2 Verify the rule does not misclassify real character literals: run both guards over the repository and confirm no new false positive appears outside the two expected declarations.

## 2. Clear the revealed violations

- [x] 2.1 Rename `Capture_sink` → `CaptureSink` in `examples/metrics/ex_counters_and_gauges.cpp` and `Stress_provider` → `StressProvider` in `tests/tracing/tracer_provider_test.cpp`. Verify both guards exit 0 and `nexigen_metrics_tests` / `nexigen_tracing_tests` still pass.

## 3. Pin the regression

- [x] 3.1 Add `--self-test` to both guards with fixtures: a violation after `250'000` must be reported, a declaration inside a string or `L'x'` literal must not, and a plain violation must still be reported. Verify `python3 cmake/check_naming.py --self-test` and `python3 cmake/check_result_value.py --self-test` exit 0.
- [x] 3.2 Register `nexigen_lint_guard_selftest` as a CTest entry running both self-tests; verify `ctest -R nexigen_lint_guard_selftest` passes and that it fails if the separator rule is removed (temporary revert check).

## 4. Verification

- [x] 4.1 Run `check_naming.py`, `check_result_value.py`, `check_complexity.py`, `format-check` and `ctest -R complexity`; verify all green.
- [x] 4.2 Build and run `ctest` in Debug (`build-com`), Release, ThreadSanitizer and AddressSanitizer; verify zero failures.
- [x] 4.3 Run `openspec validate fix-naming-guard-digit-separators --strict`.

## Workflow follow-up

- Commit the fix, archive with `/opsx-archive`, and push.
