# Tasks

## 1. Simple settings example

- [ ] 1.1 Add `examples/logging/ex_logging_settings_simple.cpp`: create a temporary directory with one minimal global JSON (log root, level, max file size) and one minimal local JSON (app name, level), load them through explicit `Settings_paths`, print any diagnostics, initialize `FileLogger` via `initialize(const Logging_settings&)`, log one or two records, flush, and return 0. Verify: target builds under Debug and the executable runs to completion with exit code 0.
- [ ] 1.2 Make failure paths visible: non-zero exit plus a printed message when loading returns an error, initialization fails, or flush fails. Verify: temporarily pointing the loader at a malformed local file (manual run) produces a non-zero exit and the diagnostic text.

## 2. Build and CTest registration

- [ ] 2.1 Register `ex_logging_settings_simple` in `examples/logging/CMakeLists.txt` using the existing `nexigen_add_example` helper. Verify: `ctest --test-dir build/debug -L examples` lists and passes the new test.

## 3. Documentation

- [ ] 3.1 Update the README Runnable Examples section: add `ex_logging_settings_simple` to the executable list and describe its minimal flow next to the existing `ex_logging_settings` entry. Verify: the documented command runs the example and exits 0.
- [ ] 3.2 Update the README text to state that the logging examples write to `/tmp/nexigen-examples-logs/` (or the example's own temporary directory) consistently with the implementation. Verify: text matches the paths used by both settings examples.

## 4. Verification matrix

- [ ] 4.1 Compile and pass 100% of unit tests and examples under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: `ctest --test-dir <dir> --output-on-failure` reports 100% in all four build directories.
- [ ] 4.2 Run `openspec validate add-simple-logging-settings-example --strict`. Verify: command reports valid.

## Workflow follow-up

- Sync the two capability deltas into `openspec/specs/**` and archive the change after review.
