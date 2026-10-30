# Tasks

## 1. Module scaffolding and stop reason

- [x] 1.1 Create the `nexigen::app` module layout (`include/nexigen/app/`, `src/app/`, `tests/app/`, `examples/app/`) and the `nexigen_app` STATIC target with alias `nexigen::app` linking `nexigen_logging`, `nexigen_metrics`, `nexigen_tracing`, `nexigen_chrono`, and `nexigen_core`; register `tests/app` and `examples/app` in their parent CMake files. Verify: an empty test TU compiles and links against `nexigen::app`.
- [x] 1.2 Add `include/nexigen/app/app_stop_reason.hpp` with `App_stop_reason` (`None`, `Requested`, `Sigint`, `Sigterm`), the constexpr enum trio, non-throwing ADL JSON, and `std::formatter`; add its contract tests to `tests/app/app_test.cpp`. Verify: `to_string`/`from_string`, `to_json`/`from_json` round-trip, `std::format("{}", ...)` and hostile JSON tests pass.

## 2. App settings loaders

- [x] 2.1 Add `include/nexigen/app/app_settings.hpp` with `Metrics_settings`, `Tracing_settings`, `App_settings_paths`, and `App_settings`, plus member/free `to_string`, `to_json`, `std::formatter`, `from_json` for `Settings_paths` only, and compile-time pins for the absent `from_json` on loader outputs. Verify: header compiles standalone under `-std=c++23 -fno-exceptions -fno-rtti -Werror`; serialization tests pass.
- [x] 2.2 Implement `load_metrics_settings` and `load_tracing_settings` in `src/app/app_settings.cpp`: explicit paths, defaults → global → local → env, all documented JSON keys, `std::from_chars` env parsing, best-effort diagnostics reusing `logging::Settings_diagnostic`, and status precedence. Verify: tests cover both-pairs, missing files, malformed JSON, wrong types, out-of-range values, env overrides, and unset variables.
- [x] 2.3 Implement `load_app_settings` aggregating the logging loader plus the two new loaders with worst-status reporting and best-effort population. Verify: a test with one clean pair and one unreadable pair returns `SystemError` while the other subsystems remain populated.
- [x] 2.4 Ensure the duplicated cold-path reader performs no allocation on any tick path and validate that parsing is confined to `src/app/app_settings.cpp`. Verify: allocation-window test around the loaders is allowed to allocate, and the tick loop test shows zero growth.

## 3. App lifecycle and signals

- [x] 3.1 Add `include/nexigen/app/app.hpp` with `App<Derived, Logger, Metrics_sink, Trace_sink, Tracer_capacity, Tracer_threads>`: concept `static_assert`s, the member order from design D2, `logger()`/`metrics()`/`tracer()` accessors, `current()`, and the `run` loop with `App_stop_reason` semantics. Verify: a compile-time fixture with dummy components instantiates; a `from_json`/concept negative test fails as expected.
- [x] 3.2 Implement `initialize(const App_settings&)`: signal blocking, derived hooks, provider construction, tracer initialization, watcher start, `current()` publication, idempotency, and rollback on failure. Verify: tests cover success, double initialize, each failure point leaving `current()` null and no live workers.
- [x] 3.3 Implement `Signal_watcher` with `pthread_sigmask` + `signalfd` + `poll` timeout and `std::jthread`; map SIGINT/SIGTERM to reasons and request the stop. Verify: a test raises `SIGINT` and `SIGTERM` from a helper thread and observes the matching `run()` return; the process never terminates abruptly.
- [x] 3.4 Implement `shutdown()` and the destructor: watcher join, final reason record with flush-before/log/flush-after, tracer → metrics → logger flush order, mask restore, `current()` clear, sequential idempotency, and first-error aggregation. Verify: tests assert exactly one final reason record, flush of pending data, double shutdown, and `current()` clearing.
- [x] 3.5 Implement `App::current()` publication/clearing and document its non-owning contract. Verify: tests publish, read, and clear the pointer across two sequential App instances without cross-talk.
- [x] 3.6 Implement `meter(name, version)` and `tracer_for(scope)` forwarders over `MetricProvider::get_meter` and `TracerProvider::get_tracer`, returning `NotInitialized` before initialization and propagating factory errors. Verify: tests create a counter through `meter()`, record it, create a tracer through `tracer_for()`, start a span, and assert both handles are usable and the pre-initialization calls return `NotInitialized`.

## 4. Runnable example

- [x] 4.1 Add `examples/app/ex_app_lifecycle.cpp` with a `Demo_app` defining the three component aliases and emplacement hooks, temporary logging/metrics/tracing settings pairs, a logger call, a metric created through `meter()` and recorded, a span opened through `tracer_for()`, ticks, a helper thread raising `SIGINT`, `run()` returning `Sigint`, `shutdown()`, and verification of the shutdown reason, metric value, and span output. Register it in `examples/app/CMakeLists.txt`. Verify: `ctest -L examples` runs it to completion with status 0.
- [x] 4.2 Update the README Runnable Examples section and the module map: add `ex_app_lifecycle`, its temporary directories, and the new `nexigen::app` target. Verify: the documented command runs the example and exits 0; the module map lists `app`.

## 5. Verification matrix

- [x] 5.1 Compile and pass 100% of unit tests and examples under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: `ctest --test-dir <dir> --output-on-failure` reports 100% in all four build directories.
- [x] 5.2 Run `openspec validate add-app-class --strict`. Verify: command reports valid.

## Workflow follow-up

- Sync the `app/application`, `app/settings`, and `examples` deltas into `openspec/specs/**` and archive the change after review.
