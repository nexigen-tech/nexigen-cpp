# Tasks

## 1. Convention anchor

- [x] 1.1 Add the "Code Organization — File-per-entity layout" convention (design D7) to the `context:` block of `openspec/config.yaml`. Verify `openspec list --json` still resolves the root and `openspec show` of a logging spec is unaffected.

## 2. Leaf enums

- [x] 2.1 Split `enum OverflowPolicy` out of `logger_options.hpp` into `include/nexigen/logging/overflow_policy.hpp`; make `logger_options.hpp` include it. Verify `nexigen_logging` compiles and the overflow-policy JSON/`to_string` tests pass.
- [x] 2.2 Split `enum ShardState` out of `logger_options.hpp` into `include/nexigen/logging/shard_state.hpp`; update includes. Verify `nexigen_logging` compiles and the shard-state JSON/`to_string` tests pass.
- [x] 2.3 Rename `detail::RotationKind` to `nexigen::logging::RotationSchedule` in `include/nexigen/logging/rotation_schedule.hpp`; update `detail/rotation.hpp`, `detail/file_sink.hpp`, and `src/logging/rotation.cpp`; delete the `using RotationSchedule = detail::RotationKind` alias. Verify `nexigen_logging` compiles and the rotation-schedule JSON/`to_string` tests pass.

## 3. Public vocabulary structs

- [x] 3.1 Promote `detail::ThrottleConfig` to `nexigen::logging::ThrottleConfig` in `include/nexigen/logging/throttle_config.hpp`; delete the `using ThrottleConfig` alias; update `detail/producer.hpp` and `file_logger_config.hpp` includes. Verify `nexigen_logging` compiles and throttle-config `to_string` tests pass.
- [x] 3.2 Move `struct DroppedStats` from `detail/producer.hpp` to `include/nexigen/logging/dropped_stats.hpp`; update `producer.hpp` and the logger headers that call `dropped()`. Verify `nexigen_logging` compiles and dropped-stats tests pass.
- [x] 3.3 Split `struct SpamProtectionConfig` out of `logger_options.hpp` into `include/nexigen/logging/spam_protection_config.hpp`; update `logger_options.hpp` and `detail/spam_protection.hpp`. Verify `nexigen_logging` compiles and the spam-protection JSON/`to_string` tests pass.

## 4. Config structs

- [x] 4.1 Split `struct RotationConfig` out of `file_logger.hpp` into `include/nexigen/logging/rotation_config.hpp`. Verify `nexigen_logging` compiles.
- [x] 4.2 Split `struct FileLoggerConfig` out of `file_logger.hpp` into `include/nexigen/logging/file_logger_config.hpp`; move `apply_to(Settings, FileLoggerConfig&)` into it; remove the `struct FileLoggerConfig;` forward declaration from `settings.hpp`. Verify `nexigen_logging` compiles and the file-logger-config JSON tests pass.
- [x] 4.3 Split `struct ConsoleLoggerConfig` out of `console_logger.hpp` into `include/nexigen/logging/console_logger_config.hpp`; move `apply_to(Settings, ConsoleLoggerConfig&)` into it. Verify `nexigen_logging` compiles and the console-logger-config JSON tests pass.
- [x] 4.4 Rename `logging_settings.hpp` to `settings.hpp` (`struct Settings` + `load_logging_settings` + `check_capacity`). Verify `nexigen_logging` compiles and the settings-loader tests pass.

## 5. Format/record helpers

- [x] 5.1 Split `detail::format_param` and `format_param_t` out of `format_with_location.hpp` into `include/nexigen/logging/detail/format_param.hpp`; update the headers that reference `detail::format_param_t`. Verify `nexigen_logging` compiles and format-string tests pass.
- [x] 5.2 Split `struct BoundedSink` out of `arg_pack.hpp` into `include/nexigen/logging/detail/bounded_sink.hpp`; keep `Packable` and the pack/unpack functions in `arg_pack.hpp`. Verify `nexigen_logging` compiles and the argument-pack tests pass.

## 6. detail machinery

- [x] 6.1 Split `detail::ConsoleSink` out of `console_logger.hpp` into `include/nexigen/logging/detail/console_sink.hpp`. Verify `nexigen_logging` compiles.
- [x] 6.2 Split `detail::FileSink` out of `file_logger.hpp` into `include/nexigen/logging/detail/file_sink.hpp`. Verify `nexigen_logging` compiles.
- [x] 6.3 Split `struct SpamShardState` out of `spam_protection.hpp` into `include/nexigen/logging/detail/spam_shard_state.hpp`; keep `SpamProtection` in `spam_protection.hpp`. Verify `nexigen_logging` compiles and the spam-protection tests pass.
- [x] 6.4 Rename `detail/async_logger.hpp` to `include/nexigen/logging/detail/async_worker.hpp` (`class AsyncWorker`); update the logger headers. Verify `nexigen_logging` compiles.
- [x] 6.5 Trim `detail/producer.hpp` to `class Producer` + `cached_thread_id()`. Verify `nexigen_logging` compiles and producer/multi-producer tests pass.
- [x] 6.6 Split `struct RotationScratch` out of `detail/rotation.hpp` into `include/nexigen/logging/detail/rotation_scratch.hpp`; keep `period_key`/`period_label`/`open_period`/`open_segment` in `rotation.hpp`. Verify `nexigen_logging` compiles and rotation tests pass.

## 7. Logger classes

- [x] 7.1 Trim `console_logger.hpp` to `class ConsoleLogger` only (plus its `to_string` free helper and `static_assert`). Verify `nexigen_logging` compiles and the console-logger tests pass.
- [x] 7.2 Trim `file_logger.hpp` to `class FileLogger` only. Verify `nexigen_logging` compiles and the file-logger tests pass.
- [x] 7.3 Confirm `null_logger.hpp`, `composite_logger.hpp`, `log_level.hpp`, `log_record.hpp`, `level_control.hpp`, `json_writer.hpp`, and `affinity.hpp` are already single-entity/utility and need no split. Verify they compile standalone (no missing transitive include).

## 8. src/logging split

- [x] 8.1 Split `logger_options_format.cpp` into `spam_protection_config.cpp`, `logger_options.cpp`, `file_logger_config.cpp`, and `console_logger_config.cpp`, moving the corresponding `to_string`/`to_json`/`from_json` bodies. Verify `nexigen_logging` links and the JSON round-trip tests pass.
- [x] 8.2 Rename `logging_settings.cpp` to `settings.cpp`. Verify `nexigen_logging` links and the settings-loader tests pass.

## 9. External include-site updates

- [x] 9.1 Update `#include <nexigen/logging/...>` sites in `include/nexigen/{runner,app}/*.hpp` and `src/runner/*.cpp` to the new per-entity headers. Verify `nexigen_runner` and `nexigen_app` compile.
- [x] 9.2 Update `#include` sites in `examples/logging/*.cpp`, `examples/app/*.cpp`, `examples/suite/*.cpp`, and `tests/packaging/consumer/main.cpp`. Verify those targets compile.
- [x] 9.3 Update `#include` sites in `tests/logging/logging_test.cpp`, `tests/runner/runner_logging_test.cpp`, and `tests/app/app_test.cpp`. Verify those test targets compile.

## 10. Verification

- [x] 10.1 Build and run 100% of unit tests under Debug and Release (`-O3 -march=native -flto` with latency gates). Verify `ctest` reports no failures and the Release enqueue latency gate (≤ 20 ns) passes.
- [x] 10.2 Build and run 100% of unit tests under ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify `ctest` reports no failures and no sanitizer reports.

## Workflow follow-up

- After implementation passes review, archive the change.
- The same `refactor-<module>-file-per-entity` pattern applies next to `core`, `chrono`, `metrics`, `tracing`, `diagnostics`, `runner`, `hub`, and `app`.
