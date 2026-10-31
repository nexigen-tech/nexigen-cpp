# Tasks

## 1. ChronoProvider two-phase lifecycle

- [x] 1.1 Add `Clock_log_level` (info/warning/error) with the serialization mandates and a `detail::Log_binding` hook in `include/nexigen/chrono/chrono_provider.hpp`; verify the enum round-trips through to_string/JSON/formatter
- [x] 1.2 Change `ChronoProvider` construction to not start the worker, and add `start()` (no logger) plus a `start(Logger&)` overload that binds the log hook and starts the worker idempotently; verify `snapshot_now()` works before `start()` and the worker starts only after `start()`
- [x] 1.3 Update `tests/chrono/chrono_test.cpp`: deferred-start (no snapshot before `start()`), `start()` idempotency, zero-interval no-op, and worker logging through a bound logger vs. silent without one; verify the chrono suites pass

## 2. Infrastructure integration

- [x] 2.1 Extend `InfrastructurePolicy` in `include/nexigen/app/infrastructure.hpp` with `Chrono_sink` and `emplace_chrono_sink`, plus a `static_assert` on `chrono::concepts::ChronoSink<Chrono_sink>`; verify the concept rejects a policy missing the chrono sink
- [x] 2.2 Add `chrono_sink_`/`chrono_` members (sink before provider, after the tracing pair, before the watcher) and a `chrono()` accessor; verify the accessor is callable after initialization
- [x] 2.3 Wire `initialize()` to construct the chrono provider first (no worker), emplace the logger, bind the logger to chrono via a bridge, call `chrono_->start(...)`, then metrics and tracing; verify a valid policy initializes with `startup_status()` OK
- [x] 2.4 Wire `shutdown()` to flush chrono last and `rollback()` to reset `chrono_` then `chrono_sink_` before `logger_`; verify shutdown flushes chrono and a failed initialize rolls chrono back without leaks

## 3. App settings integration

- [x] 3.1 Add `chrono::Chrono_settings chrono` to `App_settings` and `chrono::Chrono_settings_paths chrono` to `App_settings_paths`, including their `to_string`/`to_json` coverage; verify the aggregate serializes the chrono member
- [x] 3.2 Wire `load_app_settings` to call `chrono::load_chrono_settings(paths.chrono, out.chrono)` and fold its status into the aggregate; verify a chrono pair loads and a missing pair yields defaults

## 4. Example and test policies

- [x] 4.1 Update `examples/chrono/ex_chrono_snapshot.cpp` to call `start()` after construction; verify it still produces a parseable snapshot file and exits 0
- [x] 4.2 Update `examples/app/ex_app_lifecycle.cpp`: add `using Chrono_sink = chrono::NullChronoSink` and `emplace_chrono_sink`, and take a snapshot via `infra->chrono().snapshot_now()`; verify the example builds and exits 0
- [x] 4.3 Update every policy in `tests/app/app_test.cpp` to declare `Chrono_sink` + `emplace_chrono_sink`, add chrono coverage (accessor, shutdown flush, chrono-sink rollback), and verify the app test suites pass

## 5. Verification

- [x] 5.1 Build and pass 100% of unit tests under Debug; verify `ctest` under Debug passes
- [x] 5.2 Build and pass 100% of unit tests under Release (`-O3 -march=native -flto`); verify the Release build passes
- [x] 5.3 Build and pass 100% of unit tests under ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`); verify both sanitizer builds report no races or leaks

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result (specs synced into `openspec/specs/app/` and `openspec/specs/chrono/`).
