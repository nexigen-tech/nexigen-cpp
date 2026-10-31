# Tasks

## 1. LoggerOptions gains runtime message_capacity

- [x] 1.1 Add `std::uint32_t message_capacity{128U}` to `LoggerOptions` (include/nexigen/logging/logger_options.hpp) and verify it appears in the existing `to_string()` (src/logging/logger_options_format.cpp).
- [x] 1.2 Add `message_capacity` to `LoggerOptions` `to_json`/`from_json` (src/logging/logger_options_format.cpp) under `JSON_NOEXCEPTION=1`, and verify a round-trip test of a non-default value (e.g. 256) recovers the exact value and the default-constructed value is 128.

## 2. Settings loader and capacity check

- [x] 2.1 Add `bool message_capacity_set{false}` to `Logging_settings` (include/nexigen/logging/logging_settings.hpp) and update its `to_string()` and `to_json` (src/logging/logging_settings.cpp) to report the flag.
- [x] 2.2 Add a `message_capacity` JSON field to `sanitize_document`, accepting a positive 32-bit unsigned integer and setting `message_capacity_set` (mirror `spsc_ring_buffer_capacity`), and add the `NEXIGEN_LOG_MESSAGE_CAPACITY` override to `apply_environment`. Verify a local file and an env var each set the field and flag.
- [x] 2.3 Widen `check_capacity` to `check_capacity<ShardCapacity, MaxThreads, MessageCapacity>` and add the message-capacity branch that returns `InvalidArgument` (plus a `Computed` diagnostic naming both values) when `message_capacity_set` and `options.message_capacity > MessageCapacity`. Verify over-limit is rejected and default (not explicitly set) is never checked.

## 3. Logger call sites

- [x] 3.1 Update the `check_capacity` call sites in `ConsoleLogger` and `FileLogger` `initialize(const Logging_settings&)` (include/nexigen/logging/console_logger.hpp, file_logger.hpp) to pass the logger's `MessageCapacity` template parameter, and verify both loggers still initialize and the existing initialize tests pass unchanged.

## 4. Tests

- [x] 4.1 Add tests in tests/logging/logging_test.cpp covering: default `message_capacity` is 128; JSON round-trip preserves it; `NEXIGEN_LOG_MESSAGE_CAPACITY` env override sets field + flag; explicit `message_capacity` over the compile-time `MessageCapacity` returns `InvalidArgument` naming both values; an unset value is not checked. Verify they compile and pass.
- [x] 4.2 Update the existing `check_capacity<128, 32>` test instantiations to the three-parameter form and verify the ring-capacity / max-threads accept-reject cases still pass.

## 5. Full build and test matrix

- [x] 5.1 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates, confirming the ≤ 20 ns enqueue budget is unchanged), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`), and verify no new allocation shows up on the producer path.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result.