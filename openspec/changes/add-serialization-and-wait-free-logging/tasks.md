# Tasks

## 1. nlohmann_json build integration (design D1)

- [ ] 1.1 In root `CMakeLists.txt`, add `find_package(nlohmann_json QUIET)` with a FetchContent fallback (pinned release), link `nlohmann_json::nlohmann_json` INTERFACE into `nexigen_core`, and add `target_compile_definitions(nexigen_core INTERFACE JSON_NOEXCEPTION=1)`; verify a clean configure succeeds both with and without a system-installed package (simulate the miss via `-DFETCHCONTENT_FULLY_DISCONNECTED=OFF` path or by hiding the config) and all existing targets still build.
- [ ] 1.2 Add a smoke TU snippet in `tests/core/core_test.cpp` including `<nlohmann/json.hpp>` and building one `json` value; verify compilation under the project-wide `-fno-exceptions -Werror` flags.

## 2. Enum vocabulary: from_string, JSON, formatter (design D2)

- [ ] 2.1 `include/nexigen/core/result.hpp`: add `from_string(std::string_view, ErrorCode&) noexcept`, ADL `to_json`/`from_json` (non-throwing, unchanged-on-invalid), and `std::formatter<ErrorCode>`; verify with static_asserts and runtime checks in `tests/core/core_test.cpp`: name round-trips for all enumerators, hostile JSON (number/array/null/unknown string) leaves a pre-set fallback with no terminate, `std::format("{}", ErrorCode::BufferFull) == "BufferFull"` and width/fill behavior matches `string_view`.
- [ ] 2.2 `include/nexigen/logging/log_level.hpp`: add `from_string`, ADL `to_json`/`from_json`, `std::formatter<LogLevel>` (same pattern); verify in `tests/logging/logging_test.cpp`: seven-name round-trip, invalid-input fallback, `std::format("{}", LogLevel::Warning) == "warning"`.
- [ ] 2.3 `include/nexigen/logging/detail/rotation.hpp`: add the missing `constexpr to_string(Rotation_kind)` (`"none"/"hourly"/"daily"/"custom"`), `from_string`, ADL `to_json`/`from_json`, `std::formatter<Rotation_kind>`; verify round-trip + silent rejection + `std::format("rotate={}", Rotation_schedule::Daily) == "rotate=daily"` in `tests/logging/logging_test.cpp`.

## 3. Universal to_string inventory (design D9)

- [ ] 3.1 `include/nexigen/core/spsc_ring_buffer.hpp`: member `std::string to_string() const` snapshot (capacity, size, empty/full) + free `to_string(const SpscRingBuffer&)`; verify in `tests/core/core_test.cpp` that a 3-of-8 ring's dump names `8` and `3` and a third thread can call it while producer/consumer run.
- [ ] 3.2 `include/nexigen/chrono/timestamp.hpp`, `stamp_source.hpp`, `time_format.hpp`: `to_string() const` + free helpers for `Timestamp` (ns value), `Stamp_source` (calibration state + anchors + last ns), `Civil_time` (ISO-like fields); verify in `tests/chrono/chrono_test.cpp` member/free equality per type, uncalibrated vs calibrated Stamp_source dumps, and field presence for a known instant.
- [ ] 3.3 `include/nexigen/logging/detail/producer.hpp` + `file_logger.hpp` headers: `to_string() const` + free helpers for `Dropped_stats`, `Throttle_config`, `Rotation_config`, `FileLogger_config`, and `ConsoleLogger::Config`; verify in `tests/logging/logging_test.cpp` that populated configs render every field (including Cyrillic-safe app names) and drop totals appear.

## 4. FormatWithLocation, packing, deferred LogRecord (design D3–D5)

- [ ] 4.1 Create `include/nexigen/logging/format_with_location.hpp` with `FormatWithLocation<Args...>` (consteval ctor, defaulted `std::source_location::current()`); verify compile-level: zero-arg `info("lit")`, one-arg `info("{}", x)`, and mismatched-format calls rejected — as static_assert-style requires-clause tests plus a compile-fail note checked by manual `g++ -fsyntax-only` fixtures under the C++23 strict flags.
- [ ] 4.2 Add `detail::Packable<T>` concept and `detail::Arg_pack<Args...>` compile-time offset serializer (scalars/enums/`Timestamp` at fixed offsets; `std::string_view` as `{u16 len; inline bytes}` capped at `MessageCapacity/2` with truncation flag bit0); verify in `tests/logging/logging_test.cpp` with a round-trip harness: pack then invoke the renderer into scratch equals `std::format` output, long views truncate and flag.
- [ ] 4.3 Reshape `include/nexigen/logging/log_record.hpp`: pinned 192-byte layout with `fmt`, `render` fn-ptr, `args_` blob replacing `message` (design D4 table), consumer-side `render(scratch, cap)`, truncation flag bit1 on overflow, `to_string() const` + free helper; verify static_asserts (`is_trivially_copyable_v`, `alignof == 64`, `sizeof == 192` at MessageCapacity 128) and field survival across enqueue/dequeue in `tests/logging/logging_test.cpp`.

## 5. Multi-producer shard transport (design D6–D7)

- [ ] 5.1 Rework `include/nexigen/logging/detail/producer.hpp` into `Producer<ShardCapacity, MessageCapacity, MaxThreads=32>`: shard ring array, registration cell (`next_slot` relaxed fetch_add once/thread, `active` release fetch_max), per-shard stats/throttle cells with relaxed store publishing, shared `slot_exhausted` relaxed fetch_add only past capacity, thread-local `{producer*, slot}` map (4 entries), pack-and-push emit path returning void, per-shard drop summary retention; delete the `NEXIGEN_LOGGING_BIND_PRODUCER` guard and rewrite SPSC header comments to the multi-producer contract; verify `dropped()` aggregation, pending() sums, and layout static_asserts (`sizeof(Shard_stats) % 64 == 0`) compile and pass in `tests/logging/logging_test.cpp`.
- [ ] 5.2 Rework `include/nexigen/logging/detail/async_logger.hpp`: acquire-load `active`, round-robin sweep ≤16 records/shard/batch with total batch cap 64, idle-backoff unchanged, flush re-sweep across all shards before `publish_sync`; verify in `tests/logging/logging_test.cpp` that records from 2+ threads all appear after `flush()` and per-thread order is FIFO.

## 6. Logger API migration (design D8, D10)

- [ ] 6.1 Update `include/nexigen/logging/concepts/logger_concept.hpp` to the void variadic surface (literal-call requires shapes, `flush` still `Status`) and drop-semantics wording (snapshot-only); verify conformance static_asserts for all four loggers pin the contract and a Status-returning fake logger fails the concept (compile-shape test).
- [ ] 6.2 Migrate `NullLogger`, `ConsoleLogger`, `FileLogger`, `CompositeLogger` to `log()`/six conveniences as `void ... noexcept` variadics over `FormatWithLocation`; NullLogger keeps compile-away behavior; Console/File route through the sharded producer and render packed records in `Console_sink`/`File_sink` (JSON schema and escaping unchanged, message = render callback output); Composite folds forward the wrapper and pack verbatim; add logger-instance `to_string()` dumps (design D9). Verify in `tests/logging/logging_test.cpp`: exact call `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);` produces that rendered line with the test file/line/function; `logger.info("Level is {}", LogLevel::Warning)` renders `Level is warning`; JSON lines keep the exact key order and UTF-8 round-trip.

## 7. Concurrency and latency verification

- [ ] 7.1 Extend the logging test TU with a multi-thread stress suite: 4–8 producer threads × known record counts on one `ConsoleLogger`, one `FileLogger`, and a `CompositeLogger<ConsoleLogger, FileLogger>`; assert every record emitted exactly once, per-thread FIFO, flush barrier completes, `dropped()` zero under drained conditions, and the 33rd-thread path counts `slot_exhausted` without blocking; verify the suite runs clean under TSan (task 8.2).
- [ ] 7.2 Re-center the Release latency gate: benchmark pack+push (with float + string_view args) through the sharded producer, keep the ≤ 20 ns median assertion under `NEXIGEN_LOGGING_STRICT_LATENCY`, and add a filtered-call ≈ 1 ns assertion for the new variadic shape; verify the gate passes on the target machine in Release (task 8.1).

## 8. Full-matrix integration checks

- [ ] 8.1 Build and run 100% of unit tests in Debug and Release (`-O3 -march=native -flto`) with the latency gates active in Release: `cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug && ctest --test-dir build-debug --output-on-failure`, same for Release; all suites pass, `ctest` reports zero failures.
- [ ] 8.2 Build and run 100% of unit tests with ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`): stress suites (7.1) execute race-free, all ctest pass.
- [ ] 8.3 Build and run 100% of unit tests with AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`, includes UBSan): zero sanitizer reports across all suites, ctest green.
- [ ] 8.4 Update `README.md` (API examples with the variadic logger calls and enum formatting, JSON dependency note) and verify the documented configure/build/test commands run as written on a fresh clone of the worktree.
