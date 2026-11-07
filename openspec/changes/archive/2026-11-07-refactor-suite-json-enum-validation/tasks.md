# Tasks

## 1. Baseline and scaffolding

- [x] 1.1 Run `lizard src/runner/app_suite.cpp` and record baseline complexity for
  `json_enum_fields_valid` (26).
- [x] 1.2 Build Debug and run app-suite tests to establish a green baseline:  
  `cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build/debug --target nexigen_app_suite_tests && ctest --test-dir build/debug --output-on-failure -R app_suite`.

## 2. Extract JSON enum validators

- [x] 2.1 Extract `valid_json_log_level(const nlohmann::json&, const char*)` covering
  missing/null, string type, and recognized log level values.
- [x] 2.2 Extract `valid_json_cpu_array(const nlohmann::json&, const char*)` covering
  missing or array of unsigned integers ≤ `UINT32_MAX`.
- [x] 2.3 Extract `valid_json_thread_spec(const nlohmann::json&, const char*)` covering
  missing or parseable canonical thread spec.
- [x] 2.4 Extract `valid_json_app_entry(const nlohmann::json&)` covering object type,
  string `impact_on_app_suite_operability`, and valid app-level `log_level`.

## 3. Recompose `json_enum_fields_valid`

- [x] 3.1 Reduce `json_enum_fields_valid` to an orchestrator that calls the four new
  validators in the original order and returns false on the first failure.
- [x] 3.2 Verify all existing app-suite tests still pass.

## 4. Extend unit tests

- [x] 4.1 Add tests for suite-level invalid `log_level` through `load_suite_manifest`.
- [x] 4.2 Add tests for invalid legacy `runner_control_cpus` / `runner_logger_cpus`.
- [x] 4.3 Add tests for invalid canonical `runner_control_thread` / `runner_logger_thread`.
- [x] 4.4 Add tests for invalid app `impact_on_app_suite_operability` and app `log_level`.
- [x] 4.5 Build Debug and run app-suite tests; verify all new checks pass.

## 5. Verification and quality gates

- [x] 5.1 Build Release and run app-suite tests:
  `cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release --target nexigen_app_suite_tests && ctest --test-dir build/release --output-on-failure -R app_suite`.
- [x] 5.2 Build ThreadSanitizer and run app-suite tests:
  `cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON && cmake --build build/tsan --target nexigen_app_suite_tests && ctest --test-dir build/tsan --output-on-failure -R app_suite`.
- [x] 5.3 Build AddressSanitizer and run app-suite tests:
  `cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON && cmake --build build/asan --target nexigen_app_suite_tests && ctest --test-dir build/asan --output-on-failure -R app_suite`.
- [x] 5.4 Run clang-format check:
  `cmake --build build/debug --target format-check` (or `format` to rewrite).
- [x] 5.5 Run `lizard src/runner/app_suite.cpp` and confirm:
  - `json_enum_fields_valid` complexity < 10;
  - no newly introduced helper exceeds complexity 10;
  - `validate_suite_manifest` complexity does not regress.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
