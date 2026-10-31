# Tasks

## 1. Example source and build wiring

- [x] 1.1 Add `examples/app/ex_app_cli_settings.cpp` with the fixed path constants, the stateless `Cli_file_policy`, and a `main` skeleton; add the `nexigen_add_example(ex_app_cli_settings ex_app_cli_settings.cpp nexigen::app)` line to `examples/app/CMakeLists.txt`; verify `cmake --build build --target ex_app_cli_settings` succeeds.
- [x] 1.2 Implement setup before parsing: clear and recreate `./nexigen-example-output`, create `./nexigen-example-settings`, and write the four settings JSON files; verify by running the example with `--help` from a scratch directory and checking that all four files exist and stale output files are gone.
- [x] 1.3 Implement command-line composition and initialization: append the default `--local-settings` when no directory option is present, parse `argv`, print usage on `--help`, print diagnostics on parse errors, and call `initialize(options, loaded)`; verify normal, help, and invalid-option runs behave as specified.
- [x] 1.4 Implement the bounded workload and artifact verification: create a counter, gauge, span, periodic log records, `collect_now()`, and `snapshot_now()` calls under a 2-second deadline, shut down and flush, then check that `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, and `chrono.jsonl` are non-empty; verify the run exits 0 within 60 seconds.
- [x] 1.5 Implement failure reporting for filesystem, loader, initialization, and flush failures, returning non-zero with a stderr diagnostic; verify by forcing an invalid `--log-level` and by making the output directory unwritable.

## 2. Scripts, documentation, and repository hygiene

- [x] 2.1 Add executable `examples/app/run_ex_app_cli_settings.sh` that resolves the repository root, locates or builds `ex_app_cli_settings`, runs it from the root under a 60-second timeout when available, and forwards the exit status; verify a successful run and a failing-argument run propagate their status.
- [x] 2.2 Add executable `examples/app/clean_ex_app_cli_settings.sh` that removes the two generated directories; verify it removes populated directories and exits 0 when they are already absent.
- [x] 2.3 Update `README.md` to list `ex_app_cli_settings` as the sixteenth example, describe its generated settings and output files, and document both scripts; verify the documented commands work from a clean checkout.
- [x] 2.4 Add `.gitignore` entries for `nexigen-example-settings/` and `nexigen-example-output/`; verify `git status` remains clean after a manual run.

## 3. Build-mode verification

- [x] 3.1 Build Debug and run the full unit-test suite plus `ctest -L examples`; verify 100% of tests pass and the new example test exits 0.
- [x] 3.2 Build Release with `-O3 -march=native -flto` and the latency gates, then run the full unit-test suite and `ctest -L examples`; verify 100% pass and no latency gate fails.
- [x] 3.3 Build with `-DNEXIGEN_ENABLE_TSAN=ON`, run the full unit-test suite and `ctest -L examples`; verify 100% pass with no ThreadSanitizer report.
- [x] 3.4 Build with `-DNEXIGEN_ENABLE_ASAN=ON`, run the full unit-test suite and `ctest -L examples`; verify 100% pass with no AddressSanitizer report.
- [x] 3.5 Run `openspec validate add-app-cli-settings-example --strict` and a final manual run/cleanup cycle; verify the change validates and the cleanup leaves no generated directories behind.

## Workflow follow-up

- Archive the change after review and verification are complete.
