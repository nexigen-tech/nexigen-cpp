# Tasks

## 1. Baseline and shared JSON vocabulary

- [x] 1.1 Capture pre-refactor baselines into `build/baseline/`: external defined symbols per module (`nm -C --defined-only --extern-only libnexigen_*.a`), `--help`/`--config`/no-args/unknown-flag stdout+stderr+exit code for the four daemons, `run_suite("…")` names and `NX_CHECK` counts per test file, and the current `python3 cmake/check_complexity.py` output. Verify each baseline file is non-empty and the complexity baseline lists the 12 known violations.
- [x] 1.2 Add `include/nexigen/core/detail/json_field.hpp` with the eight helpers of design D1/D2 (`read_string`, `read_bool`, `read_signed`, `read_unsigned`, `read_object`, `read_object_array`, plus the 4-byte and payload bound-copy helpers as needed), all `inline noexcept`, with no new public type. Verify it compiles under `-fno-exceptions`/`-fno-rtti` with `JSON_NOEXCEPTION=1` and that `grep -l json_field include/nexigen/logging/log_record.hpp include/nexigen/core/spsc_ring_buffer.hpp include/nexigen/core/thread_management.hpp` stays empty.
- [x] 1.3 Add hostile-input and happy-path unit tests for the vocabulary in `tests/core/json_field_test.cpp` and register the TU in the `nexigen_core_tests` target. Verify `ctest -R nexigen_core_tests` passes and the tests cover: missing key, wrong type, out-of-range unsigned (no abort), all-or-nothing object array, and bound-checked truncation.

## 2. Production deserializers (CCN ≤ 15, cold-path in `src/`)

- [x] 2.1 Decompose `com::from_json` in `src/com/service_manifest.cpp` into `parse_provided_service` / `parse_required_service` plus `read_object_array` call sites, preserving "non-object element rejects the whole collection". Verify `lizard -l cpp -C 15 src/com/service_manifest.cpp` reports no violation, `ctest -R com` passes, and `ServiceManifest` round-trip/hostile-input tests in `tests/com/` stay green.
- [x] 2.2 Decompose `phm::from_json` in `src/phm/phm_manifest.cpp` into `parse_supervised_entity` / `parse_recovery_policy`. Verify lizard reports ≤ 15 for the file, `ctest -R phm` passes, and existing manifest rejection tests are unchanged.
- [x] 2.3 Decompose `exec::from_json` in `src/exec/child_process.cpp` into `parse_env_entry` / `parse_affinity` sub-parsers. Verify lizard ≤ 15 for the file, `ctest -R exec` passes, and child-process round-trip tests are unchanged.
- [x] 2.4 Move `to_json`/`from_json` of `Iceoryx2LoggerConfig` from `include/nexigen/logging/iceoryx2_logger_config.hpp` into new `src/logging/iceoryx2_logger_config.cpp` and decompose the deserializer into `parse_worker` / `parse_throttle`. Verify the header keeps only declarations (no `inline` definition remains), lizard reports ≤ 15 for both files, `ctest -R logging` passes, and config round-trip/formatter tests are unchanged.
- [x] 2.5 Move `to_json`/`from_json` of `LogFrame` into new `src/logging/log_frame.cpp` with `read_app_id` / `read_payload` bound-checking helpers; keep the `static_assert`s, `app_id_text()` and `payload_text()` inline in the header. Verify lizard ≤ 15, `sizeof(LogFrame) == 256` and `alignof == 64` assertions still compile, `ctest -R logging` passes, and the moved pair still round-trips in `tests/logging/json_format_test.cpp`.
- [x] 2.6 Move `from_json` of `LogDaemonConfig` (`include/nexigen/logging/log_daemon_config.hpp`) and `MetricDaemonConfig` (`include/nexigen/metrics/metric_daemon_config.hpp`) into `src/logging/log_daemon_config.cpp` and `src/metrics/metric_daemon_config.cpp`, decomposing each to ≤ 15. Verify both headers contain declarations only, lizard passes on the new files, and `ctest -R "logging|metrics"` passes.

## 3. Daemon entry points (P3)

- [x] 3.1 Restructure `tools/nexigen-logd/main.cpp` into `struct Options`, `parse_args`, `load_and_validate_config`, `open_services`, `run_loop`, `shutdown`, with `main` reduced to three statements. Verify lizard ≤ 15 for the file and that the CLI golden capture from 1.1 diffs clean for all four invocations.
- [x] 3.2 Restructure `tools/nexigen-metricsd/main.cpp` the same way, keeping `apply_thread_spec_self`, UTC sampling and the storage-directory check in their current order. Verify lizard ≤ 15 and a clean CLI golden diff.
- [x] 3.3 Split `run_sm` in `tools/nexigen-sm/main.cpp` into staged functions while keeping the existing `parse_args`. Verify lizard ≤ 15 and a clean CLI golden diff.
- [x] 3.4 Split `run_phmd` in `tools/nexigen-phmd/main.cpp` into staged functions while keeping the existing `parse_args`. Verify lizard ≤ 15 and a clean CLI golden diff, plus a smoke run of each daemon against its example config exiting 0 on SIGTERM.
- [x] 3.5 Add a hostile-config test per daemon (out-of-range numeric, wrong type, unknown field) executed by the daemon test target if one exists, otherwise verify by the smoke run that malformed configs are rejected with the pre-refactor exit code and message. Verify exit codes match the 1.1 baselines.

## 4. Test decomposition (P4)

- [x] 4.1 Split `test_producer` in `tests/logging/producer_test.cpp` into scenario functions called from the existing `suite_producer`. Verify lizard ≤ 15, `run_suite("producer", …)` still registered, and `NX_CHECK` count ≥ baseline.
- [x] 4.2 Split `test_file_logger` in `tests/logging/file_logger_test.cpp`. Verify lizard ≤ 15, suite name unchanged, assertion count ≥ baseline, `ctest -R logging` green.
- [x] 4.3 Split `test_latency` in `tests/logging/latency_test.cpp` and in `tests/com/latency_test.cpp` into setup/measurement/assertion stages, keeping warm-up and measured loops byte-identical. Verify lizard ≤ 15 in both files, suite names unchanged, and the latency numbers from `build-verify-lat` stay within the existing gates.
- [x] 4.4 Split `test_stress` in `tests/logging/stress_test.cpp`. Verify lizard ≤ 15, suite name unchanged, assertion count ≥ baseline, and the TSan run of the logging suite passes.
- [x] 4.5 Split `test_composite` in `tests/logging/composite_logger_test.cpp` and `test_method` in `tests/com/com_test.cpp`. Verify lizard ≤ 15 in both files, suite names unchanged, and `ctest -R "logging|com"` green.

## 5. Gate widening and wiring

- [x] 5.1 Update `cmake/check_complexity.py`: default dirs `include/ src/ tests/ tools/`, identifier-scoped `alignas` filter, correct summary accounting, exit code 77 when lizard is absent. Verify the script exits 0 on the current tree, exits 1 on a synthetic file with CCN 16 (then remove it), and exits 77 with `PATH` stripped of lizard.
- [x] 5.2 Register `nexigen_complexity_gate` as a CTest entry with `SKIP_RETURN_CODE 77` alongside the existing `complexity-check` target. Verify `ctest -R complexity` reports pass locally and `cmake --build <dir> --target complexity-check` gives the same verdict, and confirm the CI job `complexity` needs no change by running the same target locally.
- [x] 5.3 Write `docs/header-compilation-audit.md` listing every remaining compiled-module header that still defines cold-path `to_string`/`to_json`/`from_json` eligible for out-of-line compilation, with the audit command, per-module counts, and the exemption rationale. Verify the counts match a fresh run of the audit command and that `nexigen::core` is reported as exempt (INTERFACE target).

## 6. Integration verification

- [x] 6.1 Compile and pass 100% of unit tests in all four configurations: Debug, Release (`-O3 -march=native -flto`, latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify each configuration reports zero failures and the Release latency gates stay within their budgets.
- [x] 6.2 Diff the symbol baselines from 1.1 against the post-change `nm` output for `libnexigen_{logging,metrics,com,exec,phm,sm}.a`. Verify no public (`T`) symbol was removed or added; only internal (`t`/`W`) additions are acceptable.
- [x] 6.3 Diff all CLI goldens from 1.1 and the test-name/assertion inventory. Verify identical output and exit codes, unchanged `run_suite` names, and assertion counts ≥ baseline.
- [ ] 6.4 Run the project gates: `cmake/check_naming.py`, `cmake/check_result_value.py`, `clang-format` check, plus `graphify update .`. Verify each exits 0 and the graph rebuild completes.
- [x] 6.5 Run `openspec validate refactor-complexity-hotspots --strict` and `openspec validate --specs`. Verify both report the change and specs as valid.

## Workflow follow-up

- Review the change and archive it with the project's OpenSpec archive workflow once every task is complete.
- Verify the archived specs contain the `quality/code-structure` capability and the extended `core/serialization` requirement.
