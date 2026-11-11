# Tasks

## 1. Policy

- [x] 1.1 Add `BreakStringLiterals: false` with an explanatory comment to `.clang-format`; verify `cmake --build build-com --target format-check` stays green on the unchanged tree.
- [x] 1.2 Verify the option is effective: remove it temporarily, confirm `clang-format --dry-run --Werror` fails on `tests/metrics/prometheus_render_test.cpp`, restore it and confirm it passes.

## 2. Rejoin the formatter artefacts

- [x] 2.1 Merge the six adjacent-literal pairs in `tests/metrics/prometheus_render_test.cpp` and reformat the file. Verify the concatenation of every literal body is byte-identical to `HEAD` (1605 bytes) and that no pair remains.
- [x] 2.2 Verify the file keeps exactly the six intended long lines and that `ctest -R nexigen_metrics_tests` passes.

## 3. Verification

- [x] 3.1 Re-run the `format` target and verify the tree is unchanged (idempotent), then run `format-check` and confirm 455 files clean.
- [x] 3.2 Build and run `ctest` in Debug (`build-com`), Release, ThreadSanitizer and AddressSanitizer; verify zero failures.
- [x] 3.3 Run `check_naming.py`, `check_result_value.py`, `check_complexity.py` and `ctest -R complexity`; verify all green.
- [x] 3.4 Run `openspec validate keep-long-literals-on-one-line --strict`.

## Workflow follow-up

- Commit, archive with `/opsx-archive`, and push.
