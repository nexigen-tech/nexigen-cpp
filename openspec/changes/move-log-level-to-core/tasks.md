# Tasks

Ordering note: this change lands first; the companion `add-clock-authority-core` builds on `core::LogLevel` (see its tasks 1.3/2.x) and lands after. No design open questions remain.

## 1. Vocabulary move to core

- [x] 1.1 Create `include/nexigen/core/log_level.hpp` re-qualified per design D1: enum `core::LogLevel` (uint8_t, same seven enumerators/comments), `EnumTraits<core::LogLevel>` seven-entry lowercase table, constexpr `to_string`/`from_string` delegating to `core::enum_name`/`enum_value`, ADL `to_json`/`from_json` via `core::mandate`. Delete `include/nexigen/logging/log_level.hpp` (including the stale trailing doc comment about the delegate formatter). Verify: file exists at the core path, old path gone, project still configures.
- [x] 1.2 Re-qualify the logging module: swap the include to `<nexigen/core/log_level.hpp>` and `logging::LogLevel` → `core::LogLevel` across `include/nexigen/logging/**` (log_record, logger_options, null/file/console/composite loggers + configs, spam_protection_config, concepts/logger_concept, detail/{level_control,producer,json_writer,console_sink,file_sink,spam_protection}) and `src/logging/**` (settings, config sources, json_format, logger_options). Verify: the logging library target compiles.

## 2. Consumer sweep

- [x] 2.1 Re-qualify app + runner + tools: `include/nexigen/app/{command_line,command_line_options,infrastructure}.hpp`, `src/app/command_line.cpp`, `include/nexigen/runner/{runner_logging,suite_manifest,start_options,app_spec}.hpp`, `src/runner/{app_suite,suite_supervisor,suite_manifest,runner_logging,runner}.cpp`, `tools/nexigen-run/main.cpp`, `examples/logging/*` (ex_console, ex_composite, ex_file, ex_logging_settings). Verify: the app/runner/tools/example targets compile.
- [x] 2.2 Re-qualify test suites: `tests/logging` — log_level_test, enum_vocabulary_test, logging_test_support, producer, record, composite, console, json_format, settings, null_logger, file_logger, latency, logger_options, spam_protection, async_worker, stress (functional pins like `to_string(Warning) == "warning"` and `std::formattable` re-run unchanged); `tests/app` (command_line_test, app_test_support); `tests/runner` (runner_logging_test, app_suite_test, runner_test). Verify: `nexigen_logging_tests`, app and runner test targets build and pass their suites.
- [x] 2.3 Verify CMake registration need: confirm headers are not individually listed (or update the list where `logging/log_level.hpp` is referenced) so the removal does not break any host or install/component source lists; legacy `CMakeLists.txt` references outside the include/template registration are removed. Verify: `grep -r "log_level.hpp" --include='CMakeLists.txt'` shows no removed-path references outside CMake comment history.
      PROOF: no `log_level.hpp` references in any CMakeLists.txt; sources are
      globbed, so the header move needs no source-list edit.

## 3. Grep gate

- [x] 3.1 Sweep proof: `grep -rE 'logging::LogLevel|nexigen/logging/log_level\.hpp' include src tests examples tools` returns zero hits (comment mentions inside `chrono/**` are exempt this change — they are clock_log_level vocabulary, retired by `add-clock-authority-core`). Verify: the command output is empty.
      PROOF: grep returns empty (only `logging::to_string` for RotationPolicy/
      OverflowPolicy remains, which are not LogLevel).

## 4. Full gate

- [x] 4.1 Debug build: CMake configure + ninja, 100% of unit tests pass (`ctest` over the full set) with the re-qualified suite.
- [x] 4.2 Release build `-O3 -march=native -flto`: 100% tests pass with the latency gates unchanged — `NEXIGEN_LOGGING_STRICT_LATENCY` keeps the filtered-call ≈1 ns budget and the accepted-record gates (proves the namespace move added zero hot-path cost).
- [x] 4.3 ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`): the runtime level control contract stays race-free (relaxed threshold read/write across threads, `set_level` visibility).
- [x] 4.4 AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`): clean run — no leaks or invalid accesses introduced by the sweep; cold JSON conversion paths unchanged.
- [x] 4.5 `openspec validate "move-log-level-to-core" --strict` passes and the change directory carries all three deltas (`core/log-level` ADDED ×5; `logging/log-level` REMOVED ×5 + MODIFIED ×1; `app/command-line` MODIFIED ×1).

      PROOF (ci-tier VALIDATION GREEN): Debug/Release/ASAN/TSAN all 37/37 —
      Release 265 s (latency gates unchanged), ASAN 265 s, TSAN 374 s.
      `openspec validate --strict` → "Change is valid".

## Workflow follow-up

- Archive after review; the archive step rewrites `openspec/specs/logging/log-level` (trim) and `app/command-line`, and creates `core/log-level`.
- The companion `add-clock-authority-core` lands next; its tasks 1.3/2.x expect `core::LogLevel` to exist.
