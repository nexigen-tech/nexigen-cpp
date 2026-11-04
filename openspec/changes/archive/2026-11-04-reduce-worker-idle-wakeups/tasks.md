# Tasks

## 1. Shared idle policy

- [x] 1.1 Add `include/nexigen/core/worker_tuning.hpp` with `nexigen::core::WorkerTuning` (defaults, clamp, `backoff_us`, `apply_timer_slack`).
- [x] 1.2 Verify the header is self-contained (compiles with `_GNU_SOURCE` on Linux, `prctl` guarded).

## 2. Logging consumer

- [x] 2.1 Replace `idle_spin_budget = 64` + fixed `sleep_for(200 µs)` in `detail/async_logger.hpp` with `WorkerTuning` (spin budget, geometric backoff, timer slack).
- [x] 2.2 Add `AsyncWorker::set_tuning()` and read the tuning once on the worker thread.
- [x] 2.3 Add `worker_poll_interval_us` to `FileLoggerConfig` and `ConsoleLoggerConfig` (default 1000) and render it in their `to_string()`.
- [x] 2.4 Wire the field from `LoggerOptions` into the file (`src/logging/logging_settings.cpp`) and console (`console_logger.hpp`) apply paths.

## 3. Tracing consumer

- [x] 3.1 Replace the tracing `consume_loop` idle constants with `WorkerTuning` from `config_.worker_poll_interval_us`.
- [x] 3.2 Add `worker_poll_interval_us` to `TracerProviderConfig`; render it in `to_string` and round-trip it in `to_json`/`from_json` (`src/tracing/record_format.cpp`).
- [x] 3.3 Read `worker_poll_interval_us` from the tracing settings file (`src/tracing/tracing_settings.cpp`).

## 4. Tests and docs

- [x] 4.1 Add logging idle-wake-up / config-round-trip / flush-under-long-poll checks (`tests/logging/logging_test.cpp`).
- [x] 4.2 Add tracing config round-trip and idle-wake-up checks (`tests/tracing/tracing_test.cpp`).
- [x] 4.3 Update README (drop the "parsed but not enforced" caveat, document the ceiling) and the affected specs.
- [x] 4.4 Run the logging/tracing suites under Release and TSan.
