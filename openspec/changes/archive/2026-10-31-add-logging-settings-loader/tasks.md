# Tasks

## 1. Settings vocabulary and diagnostics

- [x] 1.1 Add `include/nexigen/logging/logging_settings.hpp` with `Settings_source`, `Settings_paths`, `Settings_diagnostic`, and `Logging_settings` (including `global_loaded`, `local_loaded`, `ring_capacity_set`, `max_threads_set`, diagnostics vector), the `load_logging_settings` declaration, and mandate 9–11 vocabulary: member + free `to_string`, `std::formatter` for every type, non-throwing ADL `to_json`/`from_json` for `Settings_paths`/`Settings_diagnostic`, `to_json`-only for `Logging_settings` (pinned with a compile-time absence check). Verify: header compiles standalone under `-std=c++23 -fno-exceptions -fno-rtti -Werror`; `std::format("{}", ...)` matches `to_string()` for all four types; JSON round-trip tests pass for the two serializable structs.
- [x] 1.2 Add `Settings_source` serialization (`constexpr to_string`/`from_string`, `to_json`/`from_json`, `std::formatter`) and add its scenarios to the logging test TU. Verify: `openspec`-mandated enum contract tests pass (`to_string`, `from_string`, JSON, formatter).
- [x] 1.3 Implement the serialization bodies in `src/logging/logging_settings.cpp` (or a dedicated format unit) so consumer TUs stay lean. Verify: `nexigen_logging` links and the new logging tests pass in Debug.

## 2. Global/local loader and merge

- [x] 2.1 Implement explicit-path file reading (`global_file` / `local_file`, empty disables source) with `::open`/`::read`, `ENOENT` treated as "source absent", and other errors as `SystemError` diagnostics. Verify: new test cases cover both paths present, one absent, both absent, and an unreadable path.
- [x] 2.2 Implement JSON parsing under `JSON_NOEXCEPTION=1` with `is_discarded()` detection, global `app_name` erasure, and sequential overlay via the existing `from_json(LoggerOptions&)` so absent fields inherit. Verify: merge tests prove local-over-global, absent-local-inherits-global, defaults-when-absent, and global `app_name` ignored.
- [x] 2.3 Implement environment overrides (`NEXIGEN_LOG_LEVEL`, `_DIR`, `_APP_NAME`, `_MAX_FILE_SIZE_BYTES`, `_CPU_AFFINITY`, `_RING_CAPACITY`, `_MAX_PRODUCER_THREADS`, `_OVERFLOW_POLICY`) with `std::from_chars` and the documented affinity grammar (`0,2-3`). Verify: tests cover enum parsing, decimal parsing, affinity ranges/ids > 63, and unset variables leaving file values intact.
- [x] 2.4 Implement best-effort validation and diagnostics: wrong type, unparsable value, invalid range, malformed JSON each append one `Settings_diagnostic` and leave the last valid/default value; `load_logging_settings` returns `Ok` with no diagnostics, `InvalidArgument` with value/format diagnostics, `SystemError` for read failures. Verify: tests assert the exact `Status`, diagnostic source/field/message content, and that merged settings remain usable after every failure mode.
- [x] 2.5 Set `ring_capacity_set`/`max_threads_set` whenever any source (including a failing validation) provides the key, so default values never trigger capacity rejection. Verify: a test loads no capacity settings and confirms both flags are false; another sets them via JSON and via env and confirms both flags are true.

## 3. Apply helpers and logger initialization

- [x] 3.1 Implement `apply_to(const Logging_settings&, FileLogger_config&)`: level, app name, root directory, `max_file_size_bytes`, `flush_cpu_affinity` → `core_mask`, spam/throttle mapping; validate field sizes and return `InvalidArgument` before the logger's own validation. Verify: mapping test checks every field, empty affinity yields `core_mask == 0`, and oversized values are rejected.
- [x] 3.2 Implement the `ConsoleLogger` `apply_to` specialization (initial level only) and `check_capacity<ShardCapacity, MaxThreads>(settings, diagnostics)` that rejects only explicitly configured over-limit capacities. Verify: tests cover explicit overflow → `InvalidArgument` with both values named, default-over-small-template → OK, and fitting explicit values → OK.
- [x] 3.3 Add `initialize(const Logging_settings&)` overloads to `FileLogger` and `ConsoleLogger` that run `check_capacity`, call `apply_to`, and delegate to the existing `initialize` without leaking `string_view`s. Verify: an integration test initializes a `FileLogger` from `Logging_settings`, logs and flushes; existing `initialize(FileLogger_config)` / `initialize(Config)` tests still pass unchanged.

## 4. Size-based file rotation

- [x] 4.1 Add `std::uint64_t max_file_size_bytes{0}` to `FileLogger_config` and `detail::File_sink::Settings`; map it in `apply_to` and include it in the `FileLogger_config::to_string()` dump (update its test). Verify: default-constructed config still produces the previous single-file output and the dump contains the field.
- [x] 4.2 Extend `detail::rotation` with the numbered-segment path builder (segment 0 = configured name; segment N inserts `.<N>` before the extension) using pre-allocated scratch and `std::to_chars`; reject overflow paths with `InvalidArgument`. Verify: unit tests assert `app.jsonl` → `app.1.jsonl`, extensionless names, directory creation, and no allocation.
- [x] 4.3 Implement the switch in `File_sink`: seed `bytes_written_` from `fstat` at start/period switch, rotate before a write that would exceed the limit when the segment is non-empty, open-before-close ordering, reset on period boundary, and `max == 0` bypass. Verify: tests prove segment splitting at a tiny limit, a single oversized line still lands once, pre-existing file content triggers a switch, `0` disables segmentation, `flush()` syncs the active segment after switches, and the existing rotation tests are unchanged.
- [x] 4.4 Update the file-logger section of `README.md` and `docs/architecture.md` with the new `max_file_size_bytes` behaviour and segment naming. Verify: documented commands and examples still run; text matches the implemented naming.

## 5. Example, documentation and integration

- [x] 5.1 Add `examples/logging/ex_logging_settings.cpp` (temporary global/local JSON, env override, load, apply, initialize `FileLogger`, log, flush, verify output, exit 0) and register it in `examples/CMakeLists.txt` and the `examples` CTest label. Verify: `ctest -L examples` runs it to completion with status 0.
- [x] 5.2 Document the loader, precedence, environment variables, and the "parsed but not yet enforced" option list in `README.md` (logging API section) and `docs/architecture.md` (module map / process notes). Verify: a reader can reproduce the example from the docs; `ex_logging_settings` matches the documented snippet.
- [x] 5.3 Run the full verification matrix and confirm `100% tests passed` with no sanitizer diagnostics: Debug, Release (`-O3 -march=native -flto`, latency gates active), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer/UBSan (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: `ctest --test-dir <dir> --output-on-failure` in all four build directories.
- [x] 5.4 Run `openspec validate add-logging-settings-loader --strict` and, after archive, `openspec validate --specs`. Verify: both commands report valid.

## Workflow follow-up

- Sync the change's capability deltas into `openspec/specs/**` and archive the change after review.
- Open the follow-up change for runtime-sized shard storage (`SpscShardPool` + `Producer`) and decide the fate of `worker_poll_interval_us`, `sync_flush_on_critical`, `overflow_policy`, and the remaining spam quotas.
