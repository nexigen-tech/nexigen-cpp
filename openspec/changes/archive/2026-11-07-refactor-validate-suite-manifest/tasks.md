# Tasks

## 1. Baseline and scaffolding

- [x] 1.1 Run `lizard src/runner/app_suite.cpp` and record the current cyclomatic complexity of `validate_suite_manifest` (baseline: **59**).
- [x] 1.2 Build Debug and run the runner tests to establish a green baseline:  
  `cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build/debug && ctest --test-dir build/debug --output-on-failure -R runner`.

## 2. Decompose `validate_suite_manifest`

- [x] 2.1 Extract `validate_suite_header_and_paths` (anonymous namespace) covering `version`, suite log level, `suite_id`, absolute `run_dir`/`artifacts_path`, null bytes, `global_settings_path`, and restart budgets. Verify existing runner tests still pass.
- [x] 2.2 Extract `validate_app_count` covering empty `apps` and `apps.size() > max_apps`. Verify the two boundary cases still reject with `InvalidArgument`.
- [x] 2.3 Extract `validate_app_spec` covering per-application identity, command, args, env, impact enum, app log level, timeouts, settings paths, CPU set membership, and derived path lengths. Verify each rejection path still returns `InvalidArgument`.
- [x] 2.4 Extract `validate_unique_app_instances` covering duplicate `(app, instance)` pairs. Verify the duplicate case still rejects with `InvalidArgument`.
- [x] 2.5 Reduce `validate_suite_manifest` to an orchestrator that calls the validators in order and returns the first failure. Measure with `lizard` and confirm `validate_suite_manifest` complexity is under 15 (result: **5**).

## 3. Extend unit tests

- [x] 3.1 Add tests in `tests/runner/app_suite_test.cpp` for each suite-level rejection path introduced by `validate_suite_header_and_paths`.
- [x] 3.2 Add tests for each app-level rejection path introduced by `validate_app_spec`.
- [x] 3.3 Add a test for duplicate `(app, instance)` rejection.
- [x] 3.4 Add a happy-path test that a fully valid `SuiteManifest` passes `validate_suite_manifest`.
- [x] 3.5 Build Debug and run runner tests; verify all new checks pass (`NX_CHECK` failures == 0).

## 4. Verification and quality gates

- [x] 4.1 Build Release (`-O3 -march=native -flto`) and run runner tests:  
  `cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release && ctest --test-dir build/release --output-on-failure -R runner`.
- [x] 4.2 Build ThreadSanitizer and run runner tests:  
  `cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON && cmake --build build/tsan && ctest --test-dir build/tsan --output-on-failure -R runner`.
- [x] 4.3 Build AddressSanitizer and run runner tests:  
  `cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON && cmake --build build/asan && ctest --test-dir build/asan --output-on-failure -R runner`.
- [x] 4.4 Run clang-format check and fix any formatting issues:  
  `cmake --build build/debug --target format-check` (or `format` to rewrite).
- [x] 4.5 Run `lizard src/runner/app_suite.cpp` and confirm:  
  - `validate_suite_manifest` complexity < 15 (result: **5**);  
  - no newly introduced function exceeds complexity 15 (max sub-validator: **9**);  
  - overall file average complexity does not regress.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
