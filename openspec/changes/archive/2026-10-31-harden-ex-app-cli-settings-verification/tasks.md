# Tasks

## 1. Failure-demo mode

- [x] 1.1 Implement `--fail-demo`: pre-scan and strip the token before `parse_command_line`, write one malformed `metrics.json` in that mode, and verify `ex_app_cli_settings --fail-demo` exits non-zero with a diagnostic on stderr, creates no workload artifacts, and finishes in under 2 seconds
- [x] 1.2 Document `--fail-demo` in the README `ex_app_cli_settings` bullet and verify the rendered text matches the observed behavior

## 2. Trace-shape self-verification

- [x] 2.1 Extend the end-of-run checks with the trace-shape floors (>=1 parent-child edge, >=2 distinct span kinds) and verify a normal run still exits 0 and reports the observed edge and kind counts on the summary line
- [x] 2.2 Verify the negative path by temporarily disabling child-span creation locally and confirming the run exits non-zero with a diagnostic, then restoring the code and confirming the run passes again

## 3. Help smoke test and integration

- [x] 3.1 Register the `ex_app_cli_settings_help` CTest entry (`ex_app_cli_settings --help`, same TIMEOUT/LABELS) and document it in the README, then verify `ctest -R ex_app_cli_settings_help` passes and a configure with `-DNEXIGEN_BUILD_EXAMPLES=OFF` registers no example tests or variants
- [x] 3.2 Verify `nexigen_app_tests`, `ex_app_cli_settings`, and `ex_app_cli_settings_help` pass under a Release build and a TSan build (`ctest --output-on-failure`) without new warnings under `-Werror`

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
