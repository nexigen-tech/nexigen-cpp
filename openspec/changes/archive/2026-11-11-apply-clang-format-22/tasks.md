# Tasks

## 1. Apply and prove the reformat

- [x] 1.1 Run `cmake --build build-com --target format` and verify `cmake --build build-com --target format-check` exits 0 with no file reported.
- [x] 1.2 Re-run the `format` target and verify `git status --short` shows no C++ modifications (idempotency), then compare every changed file against `HEAD` with all spaces, tabs and newlines stripped and verify only `tests/logging/log_daemon_test.cpp` (macro continuations) and `tests/metrics/prometheus_render_test.cpp` (adjacent string literals) differ.
- [x] 1.3 Inspect the `tests/logging/log_daemon_test.cpp` and `tests/metrics/prometheus_render_test.cpp` diffs and verify the expanded `NX_CHECK`/`run_suite` definitions and split literals still behave identically: both files compile and their suites pass.

## 2. Verify no behavioral change

- [x] 2.1 Build and run `ctest` in Debug (`build-com`, all modules) and `build-verify`: verify 60/60 and 38/38 pass.
- [x] 2.2 Build and run `ctest` in Release (`build-release`): verify 38/38 pass and the logging latency gate still reports ≤ 20 ns median and ≤ 2 ns p99.9.
- [x] 2.3 Build and run `ctest` with ThreadSanitizer and AddressSanitizer: verify 18/18 pass in both.
- [x] 2.4 Run `check_naming.py`, `check_result_value.py`, `check_complexity.py` and `ctest -R complexity`; verify all stay green.

## 3. Blame hygiene and documentation

- [x] 3.1 Commit the reformat as a single formatting-only commit (`style: apply clang-format 22.1.8 across the tree`) and verify `git show --stat` touches only the 115 expected files, with the two documented non-whitespace cases and no others.
- [x] 3.2 Create `.git-blame-ignore-revs` containing the formatting commit hash, and add the opt-in line (`git config blame.ignoreRevsFile .git-blame-ignore-revs`) to the README Formatting section. Verify `git blame --ignore-revs-file .git-blame-ignore-revs src/phm/phm_manifest.cpp` attributes lines to commits before the reformat.
- [x] 3.3 Verify `cmake/RunClangFormat.cmake` still covers exactly `include/ src/ tests/ examples/ tools/` and that `.clang-format`, the pin and the CI job are unchanged (`git diff` on those paths is empty).

## 4. Close out the change

- [x] 4.1 Run `openspec validate apply-clang-format-22 --strict` and verify it reports the change valid with `skip_specs`.
- [x] 4.2 Verify `openspec list --json` shows no leftover active changes after archiving.

## Workflow follow-up

- Archive with `/opsx-archive`.
- Push the formatting commit and the blame-ignore commit; CI `format` turns green.
