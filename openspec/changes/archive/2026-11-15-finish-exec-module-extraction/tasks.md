# Tasks

Status note: the duplicate implementation in `src/exec/app_suite.cpp` was
removed as part of `introduce-platformfg-process-model`, so the extracted units
are now the sole owners. The duplicate-symbol gate (3.1) reports 0. The full
four-config matrix (3.3) is green apart from the known environmental `mlockall`
failures, so it is closed.

Ordering note: the drift reconciliation (section 1) must land BEFORE the bulk
deletion (section 2) — the owner's copy is replaced with the currently
effective body first, then `app_suite.cpp` drops its copy. The duplicate-symbol
gate (3.1) is the acceptance criterion for the whole change.

## 1. Drift reconciliation (design D2)

- [x] 1.1 `SuiteManifest::to_string`: move the currently effective body from
      `src/exec/app_suite.cpp` into `src/exec/suite_manifest.cpp` (replacing
      the owner's drifted copy: 118 vs 124 instructions), then drop the
      `app_suite.cpp` definition. Verify: `nexigen_exec` links, and the
      manifest suite passes.
- [x] 1.2 `load_suite_manifest`: same treatment (222 vs 221 instructions) —
      `app_suite.cpp`'s body into `suite_manifest.cpp`, then delete the
      `app_suite.cpp` definition. Verify: manifest load/validate suites pass.
- [x] 1.3 `stop_suite`: same treatment (369 vs 383 instructions) —
      `app_suite.cpp`'s body into `suite_control.cpp`, then delete the
      `app_suite.cpp` definition. Verify: suite control suites pass.
- [x] 1.4 `suite_status`: same treatment (447 vs 446 instructions) —
      `app_suite.cpp`'s body into `suite_reporting.cpp`, then delete the
      `app_suite.cpp` definition. Verify: status reporting suites pass.

## 2. Bulk deletion of the identical duplicates (design D3)

- [x] 2.1 Remove the 32 duplicated manifest-vocabulary definitions from
      `src/exec/app_suite.cpp`: the seven `to_string` members
      (`AppSpec`, `SuiteManifest`, `SuiteAppMetadata`, `SuiteMetadata`,
      `SuiteExit`, `SuiteAppStatus`, `SuiteStatus`), the 14 `to_json`/
      `from_json` overloads, `json_enum_fields_valid`, the five `valid_json_*`
      validators, `attempt_dir_path`, `suite_dir_path`,
      `load_suite_manifest`, `validate_suite_manifest`. Verify: the module
      compiles and links; `nexigen_exec` tests pass.
- [x] 2.2 Remove the 11 duplicated suite-control definitions from
      `src/exec/app_suite.cpp`: `start_suite`, `stop_suite`, `stop_suite_apps`,
      `suite_is_running`, `preflight_suite_threads`,
      `run_suite_supervisor_child`, `signal_supervisor_with_grace`,
      `create_suite_pipes`, `effective_stop_grace`, `wait_for_suite_exit`,
      `write_stop_request`. Verify: compiles, links, control suites pass.
- [x] 2.3 Remove the remaining duplicated `suite_supervisor` block from
      `src/exec/app_suite.cpp` (the ≈300 duplicated lines measured against
      `src/exec/suite_supervisor.cpp`). Verify: compiles, links, supervisor
      suites pass.
- [x] 2.4 Remove `src/exec/app_suite.cpp` entirely: the extracted units own
      every entity, and the app-instance lifecycle lives in
      `child_process.cpp` / `suite_supervisor.cpp`, leaving the file with no
      owned definitions.

## 3. Verification gates (design D5)

- [x] 3.1 Duplicate-symbol gate: `nm -C --defined-only` over the built
      `nexigen_exec` objects, strong symbols (`T`/`D`/`B`) only, reports
      **0** symbols defined in more than one object.
- [x] 3.2 Duplicate-block gate: the duplication detector over `src/` reports
      no block ≥6 lines between the removed `src/exec/app_suite.cpp` and
      `suite_manifest.cpp` / `suite_control.cpp` / `suite_reporting.cpp` /
      `suite_supervisor.cpp`.
- [x] 3.3 Full matrix: Debug, Release (`-O3 -march=native -flto`), TSAN and
      ASAN builds all green, 37/37 tests each; the exec-specific suites
      (`nexigen_exec_tests`, `nexigen_app_suite_tests`) included. The
      exec/sm suites are green in all four configs; full runs still carry the
      known environmental `mlockall` failures.
- [x] 3.4 Static gates unchanged: clang-format clean, naming/result-value
      guards clean for the touched files, CCN ≤ 20, no new TODO/FIXME.
- [x] 3.5 `graphify update .` — the knowledge graph reflects the new file
      layout (the committed `graphify-out/graph.json` is rewritten whole).
