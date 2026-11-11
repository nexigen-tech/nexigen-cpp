# Tasks

## 1. Execution-manager CLI renames

- [x] 1.1 Rename `Start_args` → `StartArgs`, `Status_args` → `StatusArgs`, `Stop_args` → `StopArgs` and `String_field` → `StringField` in `tools/nexigen-exec/main.cpp`, updating every use. Verify `python3 cmake/check_naming.py` no longer reports those names and the file builds.
- [x] 1.2 Rename `enum class Start_scan` → `StartScan` in the same file, including its `Scanned`/`Missing_value`/`Unknown` uses and `scan_start_value`'s return type. Verify the build is warning-free and `nexigen-exec --help` output is unchanged against `build/baseline/cli/`.

## 2. Hub CLI rename

- [x] 2.1 Rename `Hub_options` → `HubOptions` in `tools/nexigen-hub/main.cpp`, updating every use. Verify the file builds and `python3 cmake/check_naming.py` exits 0.

## 3. Tracing test renames and result-value finding

- [x] 3.1 Rename the `Bench_provider` aliases to `BenchProvider` in both functions of `tests/tracing/latency_test.cpp` and `Big_provider` → `BigProvider` in `tests/tracing/tracer_provider_test.cpp`. Verify `nexigen_tracing_tests` and the latency test binary still pass.
- [x] 3.2 Rename the `span_keep` local to `span` in `tests/tracing/record_format_test.cpp` and verify `python3 cmake/check_result_value.py` exits 0 and the suite passes.

## 4. Verification

- [x] 4.1 Run `python3 cmake/check_naming.py` and `python3 cmake/check_result_value.py`; verify both exit 0 with no reported names.
- [x] 4.2 Build and run the full test matrix (Debug, Release with latency gates, ThreadSanitizer, AddressSanitizer); verify zero failures and unchanged Release latency numbers.
- [x] 4.3 Diff the `nexigen-exec` and `nexigen-hub` CLI baselines (`--help`, no args, unknown flag) and verify byte-identical output.
- [x] 4.4 Run `python3 cmake/check_complexity.py` and `ctest -R complexity`; verify both stay green, so this rename did not disturb the complexity contract.
- [x] 4.5 Run `openspec validate fix-naming-and-result-value-gates --strict`.

## Workflow follow-up

- Archive the change with `/opsx-archive` once reviewed.
- Open the separate change for the `format-check` migration (115 files, clang-format 22.1.8 idempotency).
