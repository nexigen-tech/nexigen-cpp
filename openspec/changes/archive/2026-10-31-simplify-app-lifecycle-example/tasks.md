# Tasks

## 1. Minimal example

- [x] 1.1 Replace `examples/app/ex_app_lifecycle.cpp` with the direct `main` design: inline `Simple_policy` (`ConsoleLogger`, `ConsoleMetricSink`, `ConsoleTraceSink`), `make_infrastructure<Simple_policy>()`, `initialize(App_settings{})`, logger, counter via `meter()`, span via `tracer_for()`, three ticks, `request_stop(Requested)`, `shutdown()`, exit 0. Remove settings-file parsing, file sinks, SIGINT helper and `Demo_*` naming. Verify: the target builds under Debug and the executable runs with exit 0.
- [x] 1.2 Keep failure checks for factory/metric/tracer/init/shutdown and print a clear message with a non-zero exit. Verify: a deliberately failing policy or initialization path (manual scratch build) returns non-zero.

## 2. Build and docs

- [x] 2.1 Keep `ex_app_lifecycle` registered in `examples/app/CMakeLists.txt` (name unchanged) and confirm the CTest `examples` label still runs it. Verify: `ctest --test-dir build/debug -L examples` passes 15/15.
- [x] 2.2 Update the README `ex_app_lifecycle` bullet to the minimal direct-main flow (no settings files, no SIGINT, console sinks). Verify: the documented command runs the example and exits 0.

## 3. Verification

- [x] 3.1 Run Debug, Release (`-O3 -march=native -flto`), TSan (`-DNEXIGEN_ENABLE_TSAN=ON`) and ASan (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: `ctest --test-dir <dir> --output-on-failure` reports 100% in all four modes.
- [x] 3.2 Run `openspec validate simplify-app-lifecycle-example --strict`. Verify: command reports valid.

## Workflow follow-up

- Sync the `app/application` delta into the main spec and archive the change after review.
